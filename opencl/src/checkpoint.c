// EN: checkpoint.c - the text checkpoint format (see checkpoint.h): one "key = value" per line,
// EN: [target] sections, value enumerations as trailing comments, and a trailing SHA-256 checksum
// EN: line that covers every other line of the file (CR bytes are ignored, so CRLF copies verify
// EN: too). Serialization snapshots the live state into reusable storage: after the first save
// EN: nothing allocates, so the save stays safe from the Windows console-control handler. The
// EN: parser is strict - unknown and duplicate keys are errors - so a typo can never silently
// EN: change the search state.
// RU: checkpoint.c - текстовый формат checkpoint (см. checkpoint.h): одна строка "ключ = значение",
// RU: секции [target], перечисления значений в комментариях в конце строк и последняя строка
// RU: контрольной суммы SHA-256, накрывающая все остальные строки файла (CR-байты игнорируются,
// RU: поэтому CRLF-копии тоже проходят проверку). Сериализация снимает живое состояние в
// RU: переиспользуемое хранилище: после первого сохранения ничего не выделяет, поэтому запись
// RU: остаётся безопасной из обработчика консоли Windows. Парсер строг - неизвестные и повторные
// RU: ключи являются ошибками, - поэтому опечатка не может молча изменить состояние поиска.
#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "pattern.h"
#include "stats.h"
#include "detsearch.h"
#include "checkpoint.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

mutex_t checkpoint_lock;

// EN: column layout of the written lines: keys are padded to CHECKPOINT_KEYW columns, values to
// EN: CHECKPOINT_VALW, then the trailing comment; aligned columns are easier to read and hand-edit
// RU: колоночная раскладка записываемых строк: ключи добиваются до CHECKPOINT_KEYW колонок,
// RU: значения до CHECKPOINT_VALW, затем комментарий в конце строки; выровненные колонки легче
// RU: читать и править руками
enum { CHECKPOINT_KEYW = 14, CHECKPOINT_VALW = 22 };

// EN: serialize buffer, allocated on the first save and reused | RU: буфер сериализации, выделяется
// RU: при первом сохранении и переиспользуется
static uint8_t *checkpoint_buf;
static size_t checkpoint_cap;
static size_t cb_o;
static int cb_ovf;

// EN: reusable view of the live search state for checkpoint_save(): grows only when the target or
// EN: pattern count grows, which cannot happen after the first save (the target table is final by
// EN: then), so later saves - including the one from the console handler - never allocate. Pattern
// EN: strings are borrowed from the target table, which is final and stable during the search.
// RU: многоразовое представление живого состояния поиска для checkpoint_save(): растёт только при
// RU: росте числа целей или шаблонов, что невозможно после первого сохранения (таблица целей к
// RU: этому моменту окончательна), поэтому последующие записи - в том числе из обработчика консоли -
// RU: память не выделяют. Строки шаблонов заимствуются из таблицы целей, которая окончательна и
// RU: неизменна во время поиска.
static CheckpointTarget *snap_targets;
static uint32_t snap_targets_cap;
static char **snap_suf_ptrs, **snap_pre_ptrs;
static uint64_t *snap_suf_hits, *snap_pre_hits;
static size_t snap_suf_cap, snap_pre_cap;

void checkpoint_free(struct Checkpoint *in) {
    if (in->targets) {
        for (uint32_t t = 0; t < in->n_targets; t++) {
            CheckpointTarget *ct = &in->targets[t];
            for (uint32_t k = 0; k < ct->n_suffix; k++) free(ct->suffix[k]);
            for (uint32_t k = 0; k < ct->n_prefix; k++) free(ct->prefix[k]);
            free(ct->suffix);
            free(ct->prefix);
            free(ct->suf_hits);
            free(ct->pre_hits);
        }
        free(in->targets);
    }
    in->targets = NULL;
    in->n_targets = 0;
}

// ---------------------------------------------------------------- writing

static void cb_puts(const char *s, size_t n) {
    if (cb_ovf || n > checkpoint_cap - cb_o) { cb_ovf = 1; return; }
    memcpy(checkpoint_buf + cb_o, s, n);
    cb_o += n;
}

static void cb_printf(const char *fmt, ...) {
    if (cb_ovf) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf((char *)checkpoint_buf + cb_o, checkpoint_cap - cb_o, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= checkpoint_cap - cb_o) { cb_ovf = 1; return; }
    cb_o += (size_t)n;
}

// EN: length of the valid UTF-8 sequence at p (1-4 bytes) or 0; rejects lone continuation bytes,
// EN: overlong forms, surrogates and values above U+10FFFF, so the written file stays valid UTF-8
// RU: длина корректной UTF-8-последовательности в p (1-4 байта) или 0; отвергает одиночные байты
// RU: продолжения, overlong-формы, суррогаты и значения выше U+10FFFF, чтобы записанный файл
// RU: оставался корректным UTF-8
static int utf8_seq_len(const unsigned char *p) {
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) return (p[1] & 0xc0) == 0x80 ? 2 : 0;
    if (c >= 0xe0 && c <= 0xef) {
        if (!p[1] || (p[1] & 0xc0) != 0x80 || !p[2] || (p[2] & 0xc0) != 0x80) return 0;
        if (c == 0xe0 && p[1] < 0xa0) return 0;  // EN: overlong | RU: overlong
        if (c == 0xed && p[1] >= 0xa0) return 0; // EN: surrogates | RU: суррогаты
        return 3;
    }
    if (c >= 0xf0 && c <= 0xf4) {
        if (!p[1] || (p[1] & 0xc0) != 0x80 || !p[2] || (p[2] & 0xc0) != 0x80 || !p[3] ||
            (p[3] & 0xc0) != 0x80)
            return 0;
        if (c == 0xf0 && p[1] < 0x90) return 0;
        if (c == 0xf4 && p[1] > 0x8f) return 0;
        return 4;
    }
    return 0;
}

// EN: does the value need quotes: an empty value, or any byte outside printable ASCII, or a quote,
// EN: a backslash or a '#' (would start a comment)
// RU: нужно ли значение в кавычках: пустое значение, любой байт вне печатного ASCII, кавычка,
// RU: обратная косая черта или '#' (начал бы комментарий)
static int qstr_needs_quotes(const char *s) {
    if (!*s) return 1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c < 0x21 || c > 0x7e || c == '"' || c == '\\' || c == '#') return 1;
    }
    return 0;
}

// EN: the quoted form of one value: escapes \\ \" \n \r \t, \xNN for other non-printable bytes and
// EN: for bytes outside valid UTF-8 sequences. Reused between lines: the writer runs under
// EN: checkpoint_lock and writes at most one quoted value per line.
// RU: форма значения в кавычках: экранирование \\ \" \n \r \t, \xNN для прочих непечатных байтов и
// RU: для байтов вне корректных UTF-8-последовательностей. Переиспользуется между строками: писатель
// RU: работает под checkpoint_lock и пишет не более одного значения в кавычках на строку.
static char qbuf[4 * 4096 + 16];

static const char *qstr(const char *s) {
    if (!qstr_needs_quotes(s)) return s;
    size_t o = 0;
    qbuf[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        const char *esc = c == '\\'   ? "\\\\"
                          : c == '"'  ? "\\\""
                          : c == '\n' ? "\\n"
                          : c == '\r' ? "\\r"
                          : c == '\t' ? "\\t"
                                      : NULL;
        if (esc) {
            size_t l = strlen(esc);
            memcpy(qbuf + o, esc, l);
            o += l;
        } else if (c < 0x20 || c == 0x7f) {
            o += (size_t)snprintf(qbuf + o, sizeof qbuf - o, "\\x%02x", c);
        } else if (c < 0x80) {
            qbuf[o++] = (char)c;
        } else {
            int n = utf8_seq_len(p);
            if (n) {
                memcpy(qbuf + o, p, (size_t)n);
                o += (size_t)n;
                p += n - 1;
            } else {
                o += (size_t)snprintf(qbuf + o, sizeof qbuf - o, "\\x%02x", c);
            }
        }
        if (o > sizeof qbuf - 8) break; // EN: unreachable for the validated lengths | RU: недостижимо при проверенных длинах
    }
    qbuf[o++] = '"';
    qbuf[o] = 0;
    return qbuf;
}

// EN: one "key<pad>= value<pad># comment" line | RU: одна строка "key<pad>= value<pad># комментарий"
static void put_line(const char *key, const char *val, const char *comment) {
    cb_printf("%-*s= %s", CHECKPOINT_KEYW, key, val);
    if (cb_ovf) return;
    size_t vlen = strlen(val);
    if (comment) {
        int pad = vlen < CHECKPOINT_VALW ? (int)(CHECKPOINT_VALW - vlen) : 1;
        cb_printf("%*s# %s\n", pad, "", comment);
    } else {
        cb_puts("\n", 1);
    }
}

static void put_u64(const char *key, uint64_t v, const char *comment) {
    char tmp[24];
    snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)v);
    put_line(key, tmp, comment);
}

static void put_qline(const char *key, const char *s, const char *comment) {
    put_line(key, qstr(s), comment);
}

static void put_hex64(const char *key, const uint8_t *raw, const char *comment) {
    static const char HEX[] = "0123456789abcdef";
    char hex[65];
    for (int i = 0; i < 32; i++) {
        hex[2 * i] = HEX[raw[i] >> 4];
        hex[2 * i + 1] = HEX[raw[i] & 15];
    }
    hex[64] = 0;
    put_line(key, hex, comment);
}

// EN: worst-case serialized size: every string counted quoted with the worst escaping (4 bytes per
// EN: byte), every number as 20 digits, plus the fixed lines
// RU: верхняя граница размера сериализации: каждая строка считается в кавычках с худшим
// RU: экранированием (4 байта на байт), каждое число как 20 цифр, плюс фиксированные строки
static size_t qstr_need(const char *s) { return 2 + 4 * strlen(s) + 1; }

static size_t serialize_need(const struct Checkpoint *cp) {
    size_t need = 4096 + qstr_need(cp->job_dir);
    for (uint32_t i = 0; i < cp->n_targets; i++) {
        const CheckpointTarget *t = &cp->targets[i];
        need += qstr_need(t->name) + qstr_need(t->label) + qstr_need(t->out) + 768;
        for (uint32_t k = 0; k < t->n_suffix; k++) need += 2 * (qstr_need(t->suffix[k]) + 128);
        for (uint32_t k = 0; k < t->n_prefix; k++) need += 2 * (qstr_need(t->prefix[k]) + 128);
    }
    return need;
}

static int checkpoint_reserve(size_t need) {
    if (need > checkpoint_cap) {
        uint8_t *p = realloc(checkpoint_buf, need);
        if (!p) return -1;
        checkpoint_buf = p;
        checkpoint_cap = need;
    }
    return 0;
}

// EN: the writer works only on a struct Checkpoint: checkpoint_save() fills a borrowed view of the
// EN: live state, checkpoint_fix() passes the parsed file back in. The two prose header lines
// EN: follow the run language (like hit and stop files); every other comment is format
// EN: documentation and stays English.
// RU: писатель работает только со struct Checkpoint: checkpoint_save() заполняет заимствованное
// RU: представление живого состояния, checkpoint_fix() передаёт разобранный файл. Две текстовые
// RU: строки шапки следуют языку запуска (как hit- и stop-файлы); все остальные комментарии —
// RU: документация формата и остаются английскими.
static size_t checkpoint_serialize(const struct Checkpoint *cp) {
    cb_o = 0;
    cb_ovf = 0;
    cb_puts("# gramvanity checkpoint v1 (text key=value)\n",
            sizeof "# gramvanity checkpoint v1 (text key=value)\n" - 1);
    cb_printf("# %s\n", tr("After manual edits run: gramvanity --checkpoint-fix <file>",
                           "После ручной правки выполните: gramvanity --checkpoint-fix <файл>"));
    cb_printf("# %s\n", tr("Do not edit the \"key\" and \"checksum\" lines.",
                           "Не редактируйте строки \"key\" и \"checksum\"."));
    cb_puts("\n", 1);
    put_line("format", CHECKPOINT_FORMAT_ID, NULL);
    put_line("version", "1", "only 1");
    put_line("mode", MODE_NAME[cp->mode], "bip39-12 | bip39-24 | ton-24 | tg-wallet");
    put_line("bounceable", (cp->flags & 1) ? "yes" : "no", "yes | no");
    put_line("net", (cp->flags & 2) ? "testnet" : "mainnet", "mainnet | testnet");
    // EN: an empty job_dir is omitted: without the line the file's own folder becomes the job
    // EN: folder on resume (the hand-written file rule), which is what --checkpoint-merge writes
    // RU: пустой job_dir не пишется: без строки папкой задания при продолжении становится папка
    // RU: самого файла (правило файла, собранного вручную), что и пишет --checkpoint-merge
    if (cp->job_dir[0]) put_qline("job_dir", cp->job_dir, NULL);
    cb_puts("\n# search position\n", 19);
    put_u64("start", cp->start, "first index of the job");
    put_u64("next", cp->next, "first index not fully checked yet");
    put_u64("s1_next", cp->s1_next, "stage-1 cursor, >= next");
    put_u64("end", cp->end, "exclusive; 0 = unlimited");
    cb_puts("\n# counters\n", 12);
    put_u64("done", cp->done, "candidates fully processed");
    put_u64("unique_hits", cp->unique_hits, "unique hits saved");
    put_u64("verified", cp->verified, "hits verified by w5addr");
    cb_puts("\n# secret\n", 10);
    put_hex64("key", cp->key, "64 hex, SECRET, do not edit");
    for (uint32_t i = 0; i < cp->n_targets; i++) {
        const CheckpointTarget *t = &cp->targets[i];
        cb_puts("\n[target]\n", 10);
        put_qline("name", t->name, NULL);
        put_qline("label", t->label, NULL);
        // EN: an empty out is omitted: the target then shares the job folder on resume
        // RU: пустой out не пишется: цель тогда делит папку задания при продолжении
        if (t->out[0]) put_qline("out", t->out, NULL);
        if (t->flags & 2)
            put_u64("count", t->count, "stop after N hits; remove the line for unlimited");
        put_u64("hits", t->hits, "hits found for this target");
        put_line("ignore_case", (t->flags & 1) ? "yes" : "no", "yes | no");
        put_line("match", (t->flags & 4) ? "or" : "and", "and | or");
        put_line("all_patterns", (t->flags & 8) ? "yes" : "no", "yes | no");
        if (t->n_suffix) {
            cb_puts("\n", 1);
            for (uint32_t k = 0; k < t->n_suffix; k++) {
                put_qline("suffix", t->suffix[k], NULL);
                put_u64("suffix_hits", t->suf_hits ? t->suf_hits[k] : 0, NULL);
            }
        }
        if (t->n_prefix) {
            cb_puts("\n", 1);
            for (uint32_t k = 0; k < t->n_prefix; k++) {
                put_qline("prefix", t->prefix[k], NULL);
                put_u64("prefix_hits", t->pre_hits ? t->pre_hits[k] : 0, NULL);
            }
        }
    }
    // EN: the checksum is the last significant line and covers every other line of the file, so a
    // EN: corruption or a hand edit anywhere is caught; the file uses LF endings only
    // RU: контрольная сумма — последняя значащая строка и накрывает все остальные строки файла, поэтому
    // RU: повреждение или ручная правка в любом месте будут пойманы; файл использует только концы строк LF
    cb_puts("\n", 1);
    uint8_t d[32];
    sha256(checkpoint_buf, cb_o, d);
    put_hex64("checksum", d, "sha256 of the file without this line; run --checkpoint-fix after edits");
    return cb_ovf ? 0 : cb_o;
}

// EN: fills *cp with a view of the live state. The pattern strings point into the target table;
// EN: hits, per-pattern counters and the session counters are copied under stat_lock. All storage
// EN: is reused, so the only allocations happen when the target or pattern count grows.
// RU: заполняет *cp представлением живого состояния. Строки шаблонов указывают в таблицу целей;
// RU: хиты, счётчики по шаблонам и счётчики сессии копируются под stat_lock. Всё хранилище
// RU: переиспользуется, поэтому выделения происходят только при росте числа целей или шаблонов.
static int capture_view(struct Checkpoint *cp) {
    memset(cp, 0, sizeof *cp);
    cp->mode = (uint32_t)opt.mode;
    cp->flags = (uint32_t)((opt.bounceable ? 1 : 0) | (opt.testnet ? 2 : 0));
    snprintf(cp->job_dir, sizeof cp->job_dir, "%s", job_dir);
    mutex_lock(&stat_lock);
    int nt = n_targets;
    size_t n_suf = 0, n_pre = 0;
    for (int i = 0; i < nt; i++) {
        n_suf += (size_t)targets[i].n_suf;
        n_pre += (size_t)targets[i].n_pre;
    }
    if ((uint32_t)nt > snap_targets_cap) {
        CheckpointTarget *p = realloc(snap_targets, (size_t)nt * sizeof *p);
        if (!p) goto oom;
        snap_targets = p;
        snap_targets_cap = (uint32_t)nt;
    }
    if (n_suf > snap_suf_cap) {
        char **ps = realloc(snap_suf_ptrs, n_suf * sizeof *ps);
        if (ps) snap_suf_ptrs = ps;
        uint64_t *ph = realloc(snap_suf_hits, n_suf * sizeof *ph);
        if (ph) snap_suf_hits = ph;
        if (!ps || !ph) goto oom;
        snap_suf_cap = n_suf;
    }
    if (n_pre > snap_pre_cap) {
        char **pp = realloc(snap_pre_ptrs, n_pre * sizeof *pp);
        if (pp) snap_pre_ptrs = pp;
        uint64_t *ph = realloc(snap_pre_hits, n_pre * sizeof *ph);
        if (ph) snap_pre_hits = ph;
        if (!pp || !ph) goto oom;
        snap_pre_cap = n_pre;
    }
    cp->done = (uint64_t)total_done;
    cp->unique_hits = (uint64_t)hits_found;
    cp->verified = (uint64_t)verified;
    size_t soff = 0, poff = 0;
    for (int i = 0; i < nt; i++) {
        Target *t = &targets[i];
        CheckpointTarget *ct = &snap_targets[i];
        memset(ct, 0, sizeof *ct);
        memcpy(ct->name, t->name, sizeof ct->name);
        memcpy(ct->label, t->label, sizeof ct->label);
        memcpy(ct->out, t->out_dir, sizeof ct->out);
        ct->flags = (uint32_t)((t->ignore_case ? 1 : 0) | (t->count_given ? 2 : 0) |
                               (t->match_or ? 4 : 0) | (t->all_patterns ? 8 : 0));
        ct->count = t->count_given ? t->count : 0;
        ct->hits = (uint64_t)t->hits;
        ct->n_suffix = (uint32_t)t->n_suf;
        ct->n_prefix = (uint32_t)t->n_pre;
        ct->suffix = snap_suf_ptrs + soff;
        ct->suf_hits = snap_suf_hits + soff;
        for (int k = 0; k < t->n_suf; k++) {
            ct->suffix[k] = t->suf[k].str;
            ct->suf_hits[k] = t->suf_hits[k];
        }
        soff += (size_t)t->n_suf;
        ct->prefix = snap_pre_ptrs + poff;
        ct->pre_hits = snap_pre_hits + poff;
        for (int k = 0; k < t->n_pre; k++) {
            ct->prefix[k] = t->pre[k].str;
            ct->pre_hits[k] = t->pre_hits[k];
        }
        poff += (size_t)t->n_pre;
    }
    mutex_unlock(&stat_lock);
    cp->targets = snap_targets;
    cp->n_targets = (uint32_t)nt;
    cp->start = det.start;
    cp->next = det.next;
    cp->s1_next = det.s1_next > det.next ? det.s1_next : det.next;
    cp->end = det.has_end ? det.end : 0;
    memcpy(cp->key, det.key, 32);
    return 0;
oom:
    mutex_unlock(&stat_lock);
    return -1;
}

int checkpoint_save(void) {
    if (!det.enabled || !det.checkpoint_path[0]) return 0;
    mutex_lock(&checkpoint_lock);
    int ok = -1;
    struct Checkpoint cp;
    if (capture_view(&cp) == 0 && checkpoint_reserve(serialize_need(&cp)) == 0) {
        size_t len = checkpoint_serialize(&cp);
        if (len) ok = write_file_atomic(det.checkpoint_path, checkpoint_buf, len);
    }
    mutex_unlock(&checkpoint_lock);
    if (ok == 0) det.last_save = now_sec();
    return ok;
}

// EN: see checkpoint.h. The sentinel start makes the first warning print regardless of the
// EN: CLOCK_MONOTONIC value (a fresh boot would otherwise suppress it for one interval).
// RU: см. checkpoint.h. Стартовое значение-сентинел гарантирует печать первого предупреждения
// RU: независимо от значения CLOCK_MONOTONIC (иначе после свежей загрузки оно подавлялось бы
// RU: на один интервал).
void checkpoint_save_warn(void) {
    static double last_warn = -1e12;
    double now = now_sec();
    if (now - last_warn < (double)opt.checkpoint_interval) return;
    last_warn = now;
    fprintf(stderr,
            tr("checkpoint save FAILED: %s (the search continues without a saved state; check the disk and the path)\n",
               "запись checkpoint НЕ УДАЛАСЬ: %s (поиск продолжается без сохранённого состояния; проверьте диск и путь)\n"),
            det.checkpoint_path);
    fflush(stderr);
}

// ---------------------------------------------------------------- parsing

// EN: per-target accumulation during parsing; flushed into in->targets at the section end. Strings
// EN: are owned until the flush, the pointer arrays are transferred to the CheckpointTarget.
// RU: накопление по цели во время разбора; в конце секции переносится в in->targets. Строки
// RU: принадлежат накопителю до переноса, массивы указателей переходят к CheckpointTarget.
typedef struct {
    char *name, *label, *out;
    uint32_t flags;
    uint64_t count, hits;
    char **suffix, **prefix;
    uint32_t n_suffix, n_prefix, cap_suffix, cap_prefix;
    uint64_t *suf_hits, *pre_hits;
    uint32_t n_suf_hits, n_pre_hits, cap_suf_hits, cap_pre_hits;
    // EN: which scalar keys were seen in this target (duplicates are errors)
    // RU: какие скалярные ключи встречались в этой цели (повторы - ошибки)
    unsigned smask;
} TargetBuild;

enum {
    TB_NAME = 1 << 0, TB_LABEL = 1 << 1, TB_OUT = 1 << 2, TB_COUNT = 1 << 3,
    TB_HITS = 1 << 4, TB_CASE = 1 << 5, TB_MATCH = 1 << 6, TB_ALL = 1 << 7
};

typedef struct {
    struct Checkpoint *in;
    // EN: normalized lines covered by the checksum, accumulated while parsing
    // RU: нормализованные строки, накрытые контрольной суммой, накапливаются при разборе
    uint8_t *hash_buf;
    size_t hash_len, hash_cap;
    // EN: reusable value buffer (one value per line) | RU: многоразовый буфер значения (одно на строку)
    char *vbuf;
    size_t vbuf_cap;
    TargetBuild tb;
    int in_target;        // EN: inside a [target] section | RU: внутри секции [target]
    int have_checksum;
    unsigned seen_mask;   // EN: global scalar keys seen | RU: встреченные глобальные скалярные ключи
} PState;

enum {
    SEEN_FORMAT = 1u << 0, SEEN_VERSION = 1u << 1, SEEN_MODE = 1u << 2, SEEN_BOUNCE = 1u << 3,
    SEEN_NET = 1u << 4, SEEN_JOB = 1u << 5, SEEN_START = 1u << 6, SEEN_NEXT = 1u << 7,
    SEEN_S1 = 1u << 8, SEEN_END = 1u << 9, SEEN_DONE = 1u << 10, SEEN_UNIQ = 1u << 11,
    SEEN_VER = 1u << 12, SEEN_KEY = 1u << 13
};

static int perr(char *err, size_t errn, const char *en, const char *ru, ...) {
    va_list ap;
    va_start(ap, ru);
    vsnprintf(err, errn, tr(en, ru), ap);
    va_end(ap);
    return -1;
}

static int hash_line(PState *ps, const char *line, size_t cl) {
    if (ps->hash_len + cl + 1 > ps->hash_cap) {
        size_t nc = ps->hash_cap ? ps->hash_cap : 8192;
        while (nc < ps->hash_len + cl + 1) nc *= 2;
        uint8_t *p = realloc(ps->hash_buf, nc);
        if (!p) return -1;
        ps->hash_buf = p;
        ps->hash_cap = nc;
    }
    memcpy(ps->hash_buf + ps->hash_len, line, cl);
    ps->hash_buf[ps->hash_len + cl] = '\n';
    ps->hash_len += cl + 1;
    return 0;
}

static int vbuf_need(PState *ps, size_t need) {
    if (need <= ps->vbuf_cap) return 0;
    size_t nc = ps->vbuf_cap ? ps->vbuf_cap : 256;
    while (nc < need) nc *= 2;
    char *p = realloc(ps->vbuf, nc);
    if (!p) return -1;
    ps->vbuf = p;
    ps->vbuf_cap = nc;
    return 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// EN: 64 hex characters -> 32 bytes | RU: 64 шестнадцатеричных символа -> 32 байта
static int parse_hex32(const char *s, uint8_t *out) {
    if (strlen(s) != 64) return -1;
    for (int i = 0; i < 32; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}

// EN: yes/no, with 1/0 and true/false accepted for hand edits; the writer emits yes/no
// RU: yes/no, для ручной правки принимаются 1/0 и true/false; писатель пишет yes/no
static int parse_yes_no(const char *v) {
    if (!strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1")) return 1;
    if (!strcmp(v, "no") || !strcmp(v, "false") || !strcmp(v, "0")) return 0;
    return -1;
}

// EN: parses the value starting at line[vb] (after the '='; quotes, escapes, trailing comment) into
// EN: ps->vbuf; returns 0 and sets *val (NUL-terminated) and *vlen, or -1 with a message in err.
// EN: Unquoted values run to a '#' preceded by a space/tab (inline comment) or to the line end, so
// EN: a pattern like A#B needs no quotes; a value containing " #", a quote or whitespace is written
// EN: quoted by the writer.
// RU: разбирает значение с позиции line[vb] (после '='; кавычки, экранирование, комментарий в
// RU: конце) в ps->vbuf; возвращает 0 и заполняет *val (с NUL на конце) и *vlen или -1 с сообщением
// RU: в err. Значение без кавычек идёт до '#', перед которой стоит пробел/таб (комментарий в конце
// RU: строки), или до конца строки, поэтому шаблон вроде A#B в кавычках не нуждается; значение
// RU: с " #", кавычкой или пробелами писатель записывает в кавычках.
static int parse_value(PState *ps, const char *line, size_t cl, size_t vb, const char **val,
                       size_t *vlen, uint32_t lineno, char *err, size_t errn) {
    if (vbuf_need(ps, cl + 1))
        return perr(err, errn, "out of memory", "не хватает памяти");
    size_t v = vb;
    while (v < cl && (line[v] == ' ' || line[v] == '\t')) v++;
    if (v >= cl || line[v] == '#')
        return perr(err, errn, "line %u: missing value", "строка %u: нет значения", lineno);
    size_t o = 0;
    if (line[v] == '"') {
        v++;
        for (;;) {
            if (v >= cl)
                return perr(err, errn, "line %u: unterminated quoted value",
                            "строка %u: значение не закрыто кавычкой", lineno);
            char c = line[v];
            if (c == '"') {
                v++;
                break;
            }
            if (c != '\\') {
                ps->vbuf[o++] = c;
                v++;
                continue;
            }
            if (v + 1 >= cl)
                return perr(err, errn, "line %u: unterminated quoted value",
                            "строка %u: значение не закрыто кавычкой", lineno);
            char e = line[v + 1];
            if (e == '\\' || e == '"') {
                ps->vbuf[o++] = e;
                v += 2;
            } else if (e == 'n') {
                ps->vbuf[o++] = '\n';
                v += 2;
            } else if (e == 'r') {
                ps->vbuf[o++] = '\r';
                v += 2;
            } else if (e == 't') {
                ps->vbuf[o++] = '\t';
                v += 2;
            } else if (e == 'x') {
                if (v + 3 >= cl || hexval(line[v + 2]) < 0 || hexval(line[v + 3]) < 0)
                    return perr(err, errn, "line %u: bad \\x escape",
                                "строка %u: неверная escape-последовательность \\x", lineno);
                ps->vbuf[o++] = (char)((hexval(line[v + 2]) << 4) | hexval(line[v + 3]));
                v += 4;
            } else {
                return perr(err, errn, "line %u: unknown escape \\%c",
                            "строка %u: неизвестная escape-последовательность \\%c", lineno, e);
            }
        }
        // EN: after the closing quote only spaces and a trailing comment may follow
        // RU: после закрывающей кавычки допускаются только пробелы и комментарий в конце
        while (v < cl && (line[v] == ' ' || line[v] == '\t')) v++;
        if (v < cl && line[v] != '#')
            return perr(err, errn, "line %u: unexpected text after the quoted value",
                        "строка %u: лишний текст после значения в кавычках", lineno);
        ps->vbuf[o] = 0;
        *val = ps->vbuf;
        *vlen = o;
        return 0;
    }
    size_t end = v;
    while (end < cl) {
        if (line[end] == '#' && (end == v || line[end - 1] == ' ' || line[end - 1] == '\t')) break;
        end++;
    }
    size_t e = end;
    while (e > v && (line[e - 1] == ' ' || line[e - 1] == '\t')) e--;
    for (size_t k = v; k < e; k++)
        if (line[k] == '"')
            return perr(err, errn, "line %u: unexpected '\"' in the value; wrap the value in quotes",
                        "строка %u: лишняя '\"' в значении; возьмите значение в кавычки", lineno);
    if (e == v)
        return perr(err, errn, "line %u: missing value", "строка %u: нет значения", lineno);
    memcpy(ps->vbuf, line + v, e - v);
    ps->vbuf[e - v] = 0;
    *val = ps->vbuf;
    *vlen = e - v;
    return 0;
}

static char *dup_str(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) {
        memcpy(p, s, n);
        p[n] = 0;
    }
    return p;
}

// EN: frees the per-target accumulation (the error path) | RU: освобождает накопление цели (путь ошибки)
static void tb_free(TargetBuild *tb) {
    for (uint32_t k = 0; k < tb->n_suffix; k++) free(tb->suffix[k]);
    for (uint32_t k = 0; k < tb->n_prefix; k++) free(tb->prefix[k]);
    free(tb->suffix);
    free(tb->prefix);
    free(tb->suf_hits);
    free(tb->pre_hits);
    free(tb->name);
    free(tb->label);
    free(tb->out);
    memset(tb, 0, sizeof *tb);
}

// EN: closes the open [target] section: validates it, pads the per-pattern hit counters (k-th hits
// EN: line belongs to the k-th pattern line; a missing tail defaults to zero) and appends the
// EN: CheckpointTarget to in->targets.
// RU: закрывает открытую секцию [target]: проверяет её, дополняет счётчики хитов по шаблонам (k-я
// RU: строка хитов относится к k-й строке шаблона; недостающие в хвосте считаются нулями) и
// RU: добавляет CheckpointTarget в in->targets.
static int flush_target(PState *ps, uint32_t lineno, char *err, size_t errn) {
    if (!ps->in_target) return 0;
    ps->in_target = 0;
    TargetBuild *tb = &ps->tb;
    if (!tb->name)
        return perr(err, errn, "line %u: [target] without a \"name\"",
                    "строка %u: в [target] нет \"name\"", lineno);
    if (!tb->n_suffix && !tb->n_prefix)
        return perr(err, errn, "line %u: target without patterns",
                    "строка %u: у цели нет шаблонов", lineno);
    CheckpointTarget *nt =
        realloc(ps->in->targets, (ps->in->n_targets + 1) * sizeof *nt);
    if (!nt) return perr(err, errn, "out of memory", "не хватает памяти");
    ps->in->targets = nt;
    CheckpointTarget *ct = &ps->in->targets[ps->in->n_targets++];
    memset(ct, 0, sizeof *ct);
    snprintf(ct->name, sizeof ct->name, "%s", tb->name);
    snprintf(ct->label, sizeof ct->label, "%s", tb->label ? tb->label : tb->name);
    snprintf(ct->out, sizeof ct->out, "%s", tb->out ? tb->out : "");
    ct->flags = tb->flags;
    ct->count = tb->count;
    ct->hits = tb->hits;
    // EN: the pointer arrays are transferred immediately and tb is zeroed field by field, so the
    // EN: error paths below can safely run tb_free() without a double free
    // RU: массивы указателей передаются сразу, а tb обнуляется по полям, поэтому пути ошибок ниже
    // RU: могут безопасно вызвать tb_free() без двойного освобождения
    ct->suffix = tb->suffix;
    ct->n_suffix = tb->n_suffix;
    tb->suffix = NULL;
    tb->n_suffix = tb->cap_suffix = 0;
    ct->prefix = tb->prefix;
    ct->n_prefix = tb->n_prefix;
    tb->prefix = NULL;
    tb->n_prefix = tb->cap_prefix = 0;
    if (ct->n_suffix) {
        ct->suf_hits = calloc(ct->n_suffix, sizeof *ct->suf_hits);
        if (!ct->suf_hits) return perr(err, errn, "out of memory", "не хватает памяти");
        if (tb->n_suf_hits) // EN: tb->suf_hits is NULL when no hits lines were given | RU: tb->suf_hits пуст, когда строк хитов не было
            memcpy(ct->suf_hits, tb->suf_hits, (size_t)tb->n_suf_hits * sizeof *ct->suf_hits);
    }
    if (ct->n_prefix) {
        ct->pre_hits = calloc(ct->n_prefix, sizeof *ct->pre_hits);
        if (!ct->pre_hits) return perr(err, errn, "out of memory", "не хватает памяти");
        if (tb->n_pre_hits) // EN: tb->pre_hits is NULL when no hits lines were given | RU: tb->pre_hits пуст, когда строк хитов не было
            memcpy(ct->pre_hits, tb->pre_hits, (size_t)tb->n_pre_hits * sizeof *ct->pre_hits);
    }
    free(tb->name);
    free(tb->label);
    free(tb->out);
    free(tb->suf_hits);
    free(tb->pre_hits);
    memset(tb, 0, sizeof *tb);
    return 0;
}

static int dup_key_err(char *err, size_t errn, uint32_t lineno, const char *key) {
    return perr(err, errn, "line %u: duplicate key \"%s\"",
                "строка %u: повторный ключ \"%s\"", lineno, key);
}

static int apply_key(PState *ps, const char *key, const char *val, size_t vlen, uint32_t lineno,
                     char *err, size_t errn) {
    struct Checkpoint *in = ps->in;
    if (!ps->in_target) {
        if (!strcmp(key, "format")) {
            if (ps->seen_mask & SEEN_FORMAT) return dup_key_err(err, errn, lineno, key);
            if (strcmp(val, CHECKPOINT_FORMAT_ID))
                return perr(err, errn,
                            "line %u: unsupported checkpoint format \"%s\" (this build supports \"%s\")",
                            "строка %u: неподдерживаемый формат checkpoint \"%s\" (эта сборка поддерживает \"%s\")",
                            lineno, val, CHECKPOINT_FORMAT_ID);
            ps->seen_mask |= SEEN_FORMAT;
            return 0;
        }
        if (!strcmp(key, "version")) {
            if (ps->seen_mask & SEEN_VERSION) return dup_key_err(err, errn, lineno, key);
            uint64_t v;
            if (parse_u64(val, &v) || v != CHECKPOINT_VERSION)
                return perr(err, errn, "line %u: checkpoint format version %s, this build supports %d",
                            "строка %u: версия формата checkpoint %s, эта сборка поддерживает %d",
                            lineno, val, CHECKPOINT_VERSION);
            ps->seen_mask |= SEEN_VERSION;
            return 0;
        }
        if (!strcmp(key, "mode")) {
            if (ps->seen_mask & SEEN_MODE) return dup_key_err(err, errn, lineno, key);
            for (int m = 0; m < 4; m++)
                if (!strcmp(val, MODE_NAME[m])) {
                    in->mode = (uint32_t)m;
                    ps->seen_mask |= SEEN_MODE;
                    return 0;
                }
            return perr(err, errn, "line %u: mode: bip39-12 | bip39-24 | ton-24 | tg-wallet",
                        "строка %u: режим: bip39-12 | bip39-24 | ton-24 | tg-wallet", lineno);
        }
        if (!strcmp(key, "bounceable")) {
            if (ps->seen_mask & SEEN_BOUNCE) return dup_key_err(err, errn, lineno, key);
            int yn = parse_yes_no(val);
            if (yn < 0)
                return perr(err, errn, "line %u: bad yes/no value for \"%s\"",
                            "строка %u: ожидалось yes или no для \"%s\"", lineno, key);
            if (yn) in->flags |= 1;
            ps->seen_mask |= SEEN_BOUNCE;
            return 0;
        }
        if (!strcmp(key, "net")) {
            if (ps->seen_mask & SEEN_NET) return dup_key_err(err, errn, lineno, key);
            if (!strcmp(val, "testnet")) in->flags |= 2;
            else if (strcmp(val, "mainnet"))
                return perr(err, errn, "line %u: net: mainnet | testnet",
                            "строка %u: сеть: mainnet | testnet", lineno);
            ps->seen_mask |= SEEN_NET;
            return 0;
        }
        if (!strcmp(key, "job_dir")) {
            if (ps->seen_mask & SEEN_JOB) return dup_key_err(err, errn, lineno, key);
            if (vlen > 4095)
                return perr(err, errn, "line %u: \"%s\" is longer than %d bytes",
                            "строка %u: \"%s\" длиннее %d байт", lineno, key, 4095);
            snprintf(in->job_dir, sizeof in->job_dir, "%s", val);
            ps->seen_mask |= SEEN_JOB;
            return 0;
        }
        static const struct {
            const char *name;
            unsigned bit;
            size_t off;
        } nums[] = {
            {"start", SEEN_START, offsetof(struct Checkpoint, start)},
            {"next", SEEN_NEXT, offsetof(struct Checkpoint, next)},
            {"s1_next", SEEN_S1, offsetof(struct Checkpoint, s1_next)},
            {"end", SEEN_END, offsetof(struct Checkpoint, end)},
            {"done", SEEN_DONE, offsetof(struct Checkpoint, done)},
            {"unique_hits", SEEN_UNIQ, offsetof(struct Checkpoint, unique_hits)},
            {"verified", SEEN_VER, offsetof(struct Checkpoint, verified)},
        };
        for (size_t i = 0; i < sizeof nums / sizeof nums[0]; i++) {
            if (strcmp(key, nums[i].name)) continue;
            if (ps->seen_mask & nums[i].bit) return dup_key_err(err, errn, lineno, key);
            uint64_t v;
            if (parse_u64(val, &v))
                return perr(err, errn, "line %u: \"%s\" is not a number",
                            "строка %u: \"%s\" не является числом", lineno, key);
            *(uint64_t *)((char *)in + nums[i].off) = v;
            ps->seen_mask |= nums[i].bit;
            return 0;
        }
        if (!strcmp(key, "key")) {
            if (ps->seen_mask & SEEN_KEY) return dup_key_err(err, errn, lineno, key);
            if (parse_hex32(val, in->key))
                return perr(err, errn, "line %u: the key must be 64 hex characters",
                            "строка %u: ключ должен состоять из 64 шестнадцатеричных символов", lineno);
            ps->seen_mask |= SEEN_KEY;
            return 0;
        }
        static const char *const target_keys[] = {"name", "label", "out", "count", "hits",
                                                  "ignore_case", "match", "all_patterns",
                                                  "suffix", "suffix_hits", "prefix", "prefix_hits"};
        for (size_t i = 0; i < sizeof target_keys / sizeof target_keys[0]; i++)
            if (!strcmp(key, target_keys[i]))
                return perr(err, errn, "line %u: key \"%s\" must be inside a [target] section",
                            "строка %u: ключ \"%s\" должен находиться внутри секции [target]",
                            lineno, key);
        return perr(err, errn, "line %u: unknown key \"%s\"",
                    "строка %u: неизвестный ключ \"%s\"", lineno, key);
    }
    // EN: inside a [target] section | RU: внутри секции [target]
    TargetBuild *tb = &ps->tb;
    if (!strcmp(key, "name")) {
        if (tb->smask & TB_NAME) return dup_key_err(err, errn, lineno, key);
        if (vlen > MAX_TARGET_NAME - 1)
            return perr(err, errn, "line %u: \"%s\" is longer than %d bytes",
                        "строка %u: \"%s\" длиннее %d байт", lineno, key, MAX_TARGET_NAME - 1);
        tb->name = dup_str(val, vlen);
        if (!tb->name) return perr(err, errn, "out of memory", "не хватает памяти");
        tb->smask |= TB_NAME;
        return 0;
    }
    if (!strcmp(key, "label")) {
        if (tb->smask & TB_LABEL) return dup_key_err(err, errn, lineno, key);
        if (vlen > MAX_TARGET_LABEL - 1)
            return perr(err, errn, "line %u: \"%s\" is longer than %d bytes",
                        "строка %u: \"%s\" длиннее %d байт", lineno, key, MAX_TARGET_LABEL - 1);
        tb->label = dup_str(val, vlen);
        if (!tb->label) return perr(err, errn, "out of memory", "не хватает памяти");
        tb->smask |= TB_LABEL;
        return 0;
    }
    if (!strcmp(key, "out")) {
        if (tb->smask & TB_OUT) return dup_key_err(err, errn, lineno, key);
        if (vlen > 4095)
            return perr(err, errn, "line %u: \"%s\" is longer than %d bytes",
                        "строка %u: \"%s\" длиннее %d байт", lineno, key, 4095);
        tb->out = dup_str(val, vlen);
        if (!tb->out) return perr(err, errn, "out of memory", "не хватает памяти");
        tb->smask |= TB_OUT;
        return 0;
    }
    if (!strcmp(key, "count")) {
        if (tb->smask & TB_COUNT) return dup_key_err(err, errn, lineno, key);
        uint64_t v;
        if (parse_u64(val, &v) || !v)
            return perr(err, errn, "line %u: count must be a positive number",
                        "строка %u: count должен быть положительным числом", lineno);
        tb->flags |= 2;
        tb->count = v;
        tb->smask |= TB_COUNT;
        return 0;
    }
    if (!strcmp(key, "hits")) {
        if (tb->smask & TB_HITS) return dup_key_err(err, errn, lineno, key);
        uint64_t v;
        if (parse_u64(val, &v))
            return perr(err, errn, "line %u: \"%s\" is not a number",
                        "строка %u: \"%s\" не является числом", lineno, key);
        tb->hits = v;
        tb->smask |= TB_HITS;
        return 0;
    }
    if (!strcmp(key, "ignore_case")) {
        if (tb->smask & TB_CASE) return dup_key_err(err, errn, lineno, key);
        int yn = parse_yes_no(val);
        if (yn < 0)
            return perr(err, errn, "line %u: bad yes/no value for \"%s\"",
                        "строка %u: ожидалось yes или no для \"%s\"", lineno, key);
        if (yn) tb->flags |= 1;
        tb->smask |= TB_CASE;
        return 0;
    }
    if (!strcmp(key, "match")) {
        if (tb->smask & TB_MATCH) return dup_key_err(err, errn, lineno, key);
        if (!strcmp(val, "or")) tb->flags |= 4;
        else if (strcmp(val, "and"))
            return perr(err, errn, "line %u: match: and | or",
                        "строка %u: сопоставление: and | or", lineno);
        tb->smask |= TB_MATCH;
        return 0;
    }
    if (!strcmp(key, "all_patterns")) {
        if (tb->smask & TB_ALL) return dup_key_err(err, errn, lineno, key);
        int yn = parse_yes_no(val);
        if (yn < 0)
            return perr(err, errn, "line %u: bad yes/no value for \"%s\"",
                        "строка %u: ожидалось yes или no для \"%s\"", lineno, key);
        if (yn) tb->flags |= 8;
        tb->smask |= TB_ALL;
        return 0;
    }
    if (!strcmp(key, "suffix") || !strcmp(key, "prefix")) {
        int is_pre = key[0] == 'p';
        uint32_t n = is_pre ? tb->n_prefix : tb->n_suffix;
        uint32_t cap = is_pre ? tb->cap_prefix : tb->cap_suffix;
        char **arr = is_pre ? tb->prefix : tb->suffix;
        if (vlen == 0)
            return perr(err, errn, "line %u: empty pattern", "строка %u: пустой шаблон", lineno);
        if (vlen > 127)
            return perr(err, errn, "line %u: the pattern is longer than 127 bytes",
                        "строка %u: шаблон длиннее 127 байт", lineno);
        if (n == cap) {
            uint32_t nc = cap ? cap * 2 : 8;
            char **na = realloc(arr, (size_t)nc * sizeof *na);
            if (!na) return perr(err, errn, "out of memory", "не хватает памяти");
            arr = na;
            cap = nc;
            if (is_pre) {
                tb->prefix = arr;
                tb->cap_prefix = cap;
            } else {
                tb->suffix = arr;
                tb->cap_suffix = cap;
            }
        }
        arr[n] = dup_str(val, vlen);
        if (!arr[n]) return perr(err, errn, "out of memory", "не хватает памяти");
        if (is_pre) tb->n_prefix = n + 1;
        else tb->n_suffix = n + 1;
        return 0;
    }
    if (!strcmp(key, "suffix_hits") || !strcmp(key, "prefix_hits")) {
        int is_pre = key[0] == 'p';
        uint32_t n_pat = is_pre ? tb->n_prefix : tb->n_suffix;
        uint32_t n_hits = is_pre ? tb->n_pre_hits : tb->n_suf_hits;
        if (n_hits >= n_pat)
            return perr(err, errn, "line %u: more %s lines than %s lines",
                        "строка %u: строк %s больше, чем строк %s", lineno, key,
                        is_pre ? "prefix" : "suffix");
        uint64_t v;
        if (parse_u64(val, &v))
            return perr(err, errn, "line %u: \"%s\" is not a number",
                        "строка %u: \"%s\" не является числом", lineno, key);
        uint32_t cap = is_pre ? tb->cap_pre_hits : tb->cap_suf_hits;
        uint64_t *arr = is_pre ? tb->pre_hits : tb->suf_hits;
        if (n_hits == cap) {
            uint32_t nc = cap ? cap * 2 : 8;
            uint64_t *na = realloc(arr, (size_t)nc * sizeof *na);
            if (!na) return perr(err, errn, "out of memory", "не хватает памяти");
            arr = na;
            if (is_pre) {
                tb->pre_hits = arr;
                tb->cap_pre_hits = nc;
            } else {
                tb->suf_hits = arr;
                tb->cap_suf_hits = nc;
            }
        }
        arr[n_hits] = v;
        if (is_pre) tb->n_pre_hits = n_hits + 1;
        else tb->n_suf_hits = n_hits + 1;
        return 0;
    }
    if (!strcmp(key, "format") || !strcmp(key, "version") || !strcmp(key, "mode") ||
        !strcmp(key, "bounceable") || !strcmp(key, "net") || !strcmp(key, "job_dir") ||
        !strcmp(key, "start") || !strcmp(key, "next") || !strcmp(key, "s1_next") ||
        !strcmp(key, "end") || !strcmp(key, "done") || !strcmp(key, "unique_hits") ||
        !strcmp(key, "verified") || !strcmp(key, "key"))
        return perr(err, errn, "line %u: key \"%s\" must appear before the [target] sections",
                    "строка %u: ключ \"%s\" должен стоять до секций [target]", lineno, key);
    return perr(err, errn, "line %u: unknown key \"%s\"",
                "строка %u: неизвестный ключ \"%s\"", lineno, key);
}

// EN: sniff: the first significant line must declare `format = ...`; an old binary checkpoint or
// EN: any other foreign file fails here with the same message as in the parser
// RU: сниффер: первая значащая строка должна объявлять `format = ...`; старый бинарный checkpoint
// RU: или любой другой чужой файл получают здесь то же сообщение, что и при разборе
static int sniff_format(const uint8_t *data, size_t len) {
    size_t pos = 0;
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) pos = 3;
    while (pos < len) {
        const char *line = (const char *)data + pos;
        const char *nl = memchr(line, '\n', len - pos);
        size_t cl = nl ? (size_t)(nl - line) : len - pos;
        pos += cl + (nl ? 1 : 0);
        while (cl && line[cl - 1] == '\r') cl--;
        size_t b = 0;
        while (b < cl && (line[b] == ' ' || line[b] == '\t')) b++;
        if (b >= cl || line[b] == '#') continue;
        size_t rem = cl - b;
        if (rem < 6 || memcmp(line + b, "format", 6)) return 0;
        size_t k = b + 6;
        while (k < cl && (line[k] == ' ' || line[k] == '\t')) k++;
        return k < cl && line[k] == '=';
    }
    return 0;
}

static int parse_text(PState *ps, const char *data, size_t len, int relaxed, char *err,
                      size_t errn) {
    struct Checkpoint *in = ps->in;
    size_t pos = 0;
    uint32_t lineno = 0;
    if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF)
        pos = 3; // EN: UTF-8 BOM | RU: UTF-8 BOM
    while (pos < len) {
        lineno++;
        const char *line = data + pos;
        const char *nl = memchr(line, '\n', len - pos);
        size_t cl = nl ? (size_t)(nl - line) : len - pos;
        pos += cl + (nl ? 1 : 0);
        while (cl && line[cl - 1] == '\r') cl--;
        size_t b = 0;
        while (b < cl && (line[b] == ' ' || line[b] == '\t')) b++;
        size_t e = cl;
        while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t')) e--;
        if (b >= e || line[b] == '#') { // EN: blank line or comment | RU: пустая строка или комментарий
            if (!ps->have_checksum && hash_line(ps, line, cl))
                return perr(err, errn, "out of memory", "не хватает памяти");
            continue;
        }
        if (ps->have_checksum)
            return perr(err, errn, "line %u: unexpected content after the checksum line",
                        "строка %u: лишнее содержимое после строки checksum", lineno);
        if (e - b == 8 && !memcmp(line + b, "[target]", 8)) {
            if (hash_line(ps, line, cl))
                return perr(err, errn, "out of memory", "не хватает памяти");
            if (flush_target(ps, lineno, err, errn)) return -1;
            ps->in_target = 1;
            continue;
        }
        const char *eq = memchr(line + b, '=', e - b);
        if (!eq)
            return perr(err, errn, "line %u: expected \"key = value\"",
                        "строка %u: ожидается \"ключ = значение\"", lineno);
        size_t kb = b, ke = (size_t)(eq - line);
        while (ke > kb && (line[ke - 1] == ' ' || line[ke - 1] == '\t')) ke--;
        while (kb < ke && (line[kb] == ' ' || line[kb] == '\t')) kb++;
        if (kb >= ke)
            return perr(err, errn, "line %u: expected \"key = value\"",
                        "строка %u: ожидается \"ключ = значение\"", lineno);
        size_t keylen = ke - kb;
        char key[24];
        if (keylen >= sizeof key)
            return perr(err, errn, "line %u: unknown key", "строка %u: неизвестный ключ", lineno);
        memcpy(key, line + kb, keylen);
        key[keylen] = 0;
        // EN: the checksum line is excluded from the hashed content and verified against the hash
        // EN: of every line before it; it is the last significant line of the file
        // RU: строка checksum исключается из хешируемого содержимого и сверяется с хешем всех
        // RU: строк до неё; это последняя значащая строка файла
        if (keylen == 8 && !memcmp(key, "checksum", 8)) {
            if (ps->have_checksum) return dup_key_err(err, errn, lineno, key);
            const char *val;
            size_t vlen;
            if (parse_value(ps, line, cl, (size_t)(eq - line) + 1, &val, &vlen, lineno, err, errn))
                return -1;
            uint8_t want[32], got[32];
            if (parse_hex32(val, want))
                return perr(err, errn, "line %u: the checksum must be 64 hex characters",
                            "строка %u: checksum должен состоять из 64 шестнадцатеричных символов",
                            lineno);
            sha256(ps->hash_buf, ps->hash_len, got);
            if (!relaxed && memcmp(want, got, 32))
                return perr(err, errn,
                            "line %u: checksum mismatch (the file is corrupted or hand-edited; "
                            "run: gramvanity --checkpoint-fix <file>)",
                            "строка %u: несовпадение контрольной суммы (файл повреждён или "
                            "отредактирован вручную; выполните: gramvanity --checkpoint-fix <файл>)",
                            lineno);
            ps->have_checksum = 1;
            continue;
        }
        if (hash_line(ps, line, cl))
            return perr(err, errn, "out of memory", "не хватает памяти");
        const char *val;
        size_t vlen;
        if (parse_value(ps, line, cl, (size_t)(eq - line) + 1, &val, &vlen, lineno, err, errn))
            return -1;
        if (apply_key(ps, key, val, vlen, lineno, err, errn)) return -1;
    }
    if (flush_target(ps, lineno, err, errn)) return -1;
    if (in->n_targets == 0)
        return perr(err, errn, "no [target] section", "нет секции [target]");
    if (!(ps->seen_mask & SEEN_FORMAT) || !(ps->seen_mask & SEEN_VERSION) ||
        !(ps->seen_mask & SEEN_MODE) || !(ps->seen_mask & SEEN_NEXT) || !(ps->seen_mask & SEEN_KEY)) {
        static const struct { unsigned bit; const char *name; } req[] = {
            {SEEN_FORMAT, "format"}, {SEEN_VERSION, "version"}, {SEEN_MODE, "mode"},
            {SEEN_NEXT, "next"}, {SEEN_KEY, "key"},
        };
        for (size_t i = 0; i < sizeof req / sizeof req[0]; i++)
            if (!(ps->seen_mask & req[i].bit))
                return perr(err, errn, "incomplete checkpoint: missing \"%s\"",
                            "неполный checkpoint: нет \"%s\"", req[i].name);
    }
    if (!(ps->seen_mask & SEEN_S1)) in->s1_next = in->next;
    if (in->next < in->start || (in->end && in->next > in->end) || in->s1_next < in->next ||
        (in->end && in->s1_next > in->end))
        return perr(err, errn, "inconsistent indices", "несогласованные индексы");
    if ((in->flags & 2) && in->mode != TG)
        return perr(err, errn, "bad flags: testnet is only defined for tg-wallet",
                    "неверные флаги: testnet определён только для tg-wallet");
    return 0;
}

static int load_impl(const char *path, struct Checkpoint *in, int relaxed, char *err, size_t errn) {
    memset(in, 0, sizeof *in);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errn, tr("cannot open %s", "не удалось открыть %s"), path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, errn, tr("cannot read %s", "не удалось прочитать %s"), path);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        snprintf(err, errn, tr("cannot read %s", "не удалось прочитать %s"), path);
        return -1;
    }
    size_t len = (size_t)sz;
    uint8_t *data = malloc(len ? len : 1);
    if (!data) {
        fclose(f);
        snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти"));
        return -1;
    }
    size_t got = fread(data, 1, len, f);
    int bad = ferror(f) || got != len;
    fclose(f);
    if (bad) {
        free(data);
        snprintf(err, errn, tr("read error on %s", "ошибка чтения %s"), path);
        return -1;
    }
    if (!sniff_format(data, len)) {
        snprintf(err, errn, "%s",
                 tr("not a gramvanity checkpoint (text format expected)",
                    "это не checkpoint gramvanity (ожидается текстовый формат)"));
        free(data);
        return -1;
    }
    PState ps;
    memset(&ps, 0, sizeof ps);
    ps.in = in;
    int rc = parse_text(&ps, (const char *)data, len, relaxed, err, errn);
    if (rc) tb_free(&ps.tb);
    free(ps.vbuf);
    free(ps.hash_buf);
    free(data);
    if (rc) checkpoint_free(in);
    return rc;
}

int checkpoint_load(const char *path, struct Checkpoint *in, char *err, size_t errn) {
    return load_impl(path, in, 0, err, errn);
}

// EN: serialize + durable write, shared by --checkpoint-fix and --checkpoint-merge
// RU: сериализация + надёжная запись, общие для --checkpoint-fix и --checkpoint-merge
static int write_checkpoint_file(const char *path, const struct Checkpoint *cp, char *err, size_t errn) {
    if (checkpoint_reserve(serialize_need(cp)) != 0) {
        snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти"));
        return -1;
    }
    size_t len = checkpoint_serialize(cp);
    int rc = len ? write_file_atomic(path, checkpoint_buf, len) : -1;
    if (rc) snprintf(err, errn, tr("cannot write %s", "не удалось записать %s"), path);
    return rc;
}

int checkpoint_fix(const char *path, struct Checkpoint *in, char *err, size_t errn) {
    if (load_impl(path, in, 1, err, errn)) return -1;
    if (write_checkpoint_file(path, in, err, errn)) {
        checkpoint_free(in);
        return -1;
    }
    // EN: on success *in stays filled for the caller's summary; the caller frees it
    // RU: при успехе *in остаётся заполненным для сводки вызывающей стороны; освобождает её вызывающий
    return 0;
}

// ---------------------------------------------------------------- targets from a checkpoint

static int strlist_matches_checkpoint(const StrList *l, char **v, uint32_t n) {
    if ((uint32_t)l->n != n) return 0;
    for (int k = 0; k < l->n; k++)
        if (strcmp(l->v[k], v[k])) return 0;
    return 1;
}

void job_dir_from_checkpoint(struct Checkpoint *in, const char *resume_path) {
    char old[4096];
    if (in->job_dir[0]) {
        snprintf(old, sizeof old, "%s", in->job_dir);
    } else {
        snprintf(old, sizeof old, "%s", resume_path);
        char *s = strrchr(old, PATH_SEP);
        if (s) {
            if (s == old) s[1] = 0;
            else *s = 0;
        } else {
            snprintf(old, sizeof old, ".");
        }
    }
    if (opt.out_given) {
        snprintf(job_dir, sizeof job_dir, "%s", opt.out_dir);
        for (uint32_t k = 0; k < in->n_targets; k++)
            if (!in->targets[k].out[0] || !strcmp(in->targets[k].out, old))
                snprintf(in->targets[k].out, sizeof in->targets[k].out, "%s", job_dir);
    } else {
        snprintf(job_dir, sizeof job_dir, "%s", old);
    }
}

void resume_apply_target_overrides(struct Checkpoint *in) {
    if (opt.count_given)
        for (uint32_t k = 0; k < in->n_targets; k++) {
            in->targets[k].flags |= 2;
            in->targets[k].count = opt.count;
        }
    if (opt.set_case)
        for (uint32_t k = 0; k < in->n_targets; k++)
            if (((in->targets[k].flags & 1) != 0) != !!opt.ignore_case)
                die(tr("--resume: --ignore-case conflicts with checkpoint target \"%s\"",
                       "--resume: --ignore-case конфликтует с целью checkpoint \"%s\""), in->targets[k].name);
    if (opt.set_match)
        for (uint32_t k = 0; k < in->n_targets; k++)
            if (((in->targets[k].flags & 4) != 0) != !!opt.match_or)
                die(tr("--resume: --match conflicts with checkpoint target \"%s\"",
                       "--resume: --match конфликтует с целью checkpoint \"%s\""), in->targets[k].name);
    // EN: --all-patterns does not change which addresses match, only when the targets are done, so it
    // EN: can be added on a resume.
    // RU: --all-patterns не меняет, какие адреса совпадают, только когда цели завершены, поэтому его
    // RU: можно добавить при продолжении.
    if (opt.set_all_patterns)
        for (uint32_t k = 0; k < in->n_targets; k++)
            in->targets[k].flags |= 8;
    // EN: Without --target blocks, global patterns have no block to act as defaults for; they must at
    // EN: least describe every checkpoint target, otherwise a mistyped pattern would be ignored.
    // RU: Без блоков --target глобальным шаблонам не для чего быть значениями по умолчанию; они
    // RU: должны хотя бы описывать каждую цель checkpoint, иначе опечатка в шаблоне была бы проигнорирована.
    if (n_targets == 0)
        for (uint32_t k = 0; k < in->n_targets; k++) {
            CheckpointTarget *ct = &in->targets[k];
            if (g_prefix.n && !strlist_matches_checkpoint(&g_prefix, ct->prefix, ct->n_prefix))
                die(tr("--resume: --prefix conflicts with checkpoint target \"%s\"",
                       "--resume: --prefix конфликтует с целью checkpoint \"%s\""), ct->name);
            if (g_suffix.n && !strlist_matches_checkpoint(&g_suffix, ct->suffix, ct->n_suffix))
                die(tr("--resume: --suffix conflicts with checkpoint target \"%s\"",
                       "--resume: --suffix конфликтует с целью checkpoint \"%s\""), ct->name);
        }
    uint8_t *used = calloc((size_t)in->n_targets, 1);
    if (!used) die(tr("out of memory", "не хватает памяти"));
    int positional = 0;
    for (int i = 0; i < n_targets; i++) {
        Target *t = &targets[i];
        int k = -1;
        if (t->name_given) {
            for (uint32_t j = 0; j < in->n_targets; j++) {
                if (used[j]) continue;
                const CheckpointTarget *ct = &in->targets[j];
                int match = 0;
                if (t->given_name[0])
                    match = !strcmp(ct->name, t->given_name) ||
                            (ct->label[0] && !strcmp(ct->label, t->given_name));
                if (!match && t->label[0])
                    match = !strcmp(ct->name, t->label) ||
                            (ct->label[0] && !strcmp(ct->label, t->label));
                if (match) { k = (int)j; break; }
            }
            if (k < 0)
                die(tr("--resume: no target named \"%s\" in the checkpoint",
                       "--resume: в checkpoint нет цели с именем \"%s\""),
                    t->label[0] ? t->label : t->given_name);
        } else {
            while (positional < (int)in->n_targets && used[positional]) positional++;
            if (positional >= (int)in->n_targets) die(tr("--resume: more --target blocks than checkpoint targets", "--resume: блоков --target больше, чем целей в checkpoint"));
            k = positional++;
        }
        if (used[k]) die(tr("--resume: target \"%s\" selected more than once", "--resume: цель \"%s\" выбрана более одного раза"), in->targets[k].name);
        used[k] = 1;
        CheckpointTarget *ct = &in->targets[k];
        if (t->has_prefix || g_prefix.n) {
            const StrList *l = t->has_prefix ? &t->prefix : &g_prefix;
            if (!strlist_matches_checkpoint(l, ct->prefix, ct->n_prefix))
                die(tr("--resume: --prefix conflicts with checkpoint target \"%s\"",
                       "--resume: --prefix конфликтует с целью checkpoint \"%s\""), ct->name);
        }
        if (t->has_suffix || g_suffix.n) {
            const StrList *l = t->has_suffix ? &t->suffix : &g_suffix;
            if (!strlist_matches_checkpoint(l, ct->suffix, ct->n_suffix))
                die(tr("--resume: --suffix conflicts with checkpoint target \"%s\"",
                       "--resume: --suffix конфликтует с целью checkpoint \"%s\""), ct->name);
        }
        if (t->ignore_case_given && ((ct->flags & 1) != 0) != !!t->ignore_case)
            die(tr("--resume: --ignore-case conflicts with checkpoint target \"%s\"",
                   "--resume: --ignore-case конфликтует с целью checkpoint \"%s\""), ct->name);
        if (t->match_given && ((ct->flags & 4) != 0) != !!t->match_or)
            die(tr("--resume: --match conflicts with checkpoint target \"%s\"",
                   "--resume: --match конфликтует с целью checkpoint \"%s\""), ct->name);
        if (t->count_given) {
            ct->flags |= 2;
            ct->count = t->count;
        }
        if (t->all_patterns_given) ct->flags |= 8;
        if (t->out_given) snprintf(ct->out, sizeof ct->out, "%s", t->out_dir);
    }
    free(used);
}

void targets_from_checkpoint(struct Checkpoint *in) {
    n_targets = 0;
    cur_target = -1;
    for (uint32_t k = 0; k < in->n_targets; k++) {
        CheckpointTarget *ct = &in->targets[k];
        Target *t = new_target();
        snprintf(t->given_name, sizeof t->given_name, "%s", ct->name);
        snprintf(t->label, sizeof t->label, "%s", ct->label[0] ? ct->label : ct->name);
        snprintf(t->name, sizeof t->name, "%s", ct->name);
        t->name_given = 1;
        t->count_given = (ct->flags & 2) != 0;
        t->count = ct->count;
        t->ignore_case_given = 1;
        t->ignore_case = (ct->flags & 1) != 0;
        t->match_given = 1;
        t->match_or = (ct->flags & 4) != 0;
        t->all_patterns_given = 1;
        t->all_patterns = (ct->flags & 8) != 0;
        t->out_given = 1;
        snprintf(t->out_dir, sizeof t->out_dir, "%s", ct->out[0] ? ct->out : job_dir);
        t->has_suffix = ct->n_suffix != 0;
        t->has_prefix = ct->n_prefix != 0;
        for (uint32_t j = 0; j < ct->n_suffix; j++)
            strlist_add(&t->suffix, ct->suffix[j], strlen(ct->suffix[j]));
        for (uint32_t j = 0; j < ct->n_prefix; j++)
            strlist_add(&t->prefix, ct->prefix[j], strlen(ct->prefix[j]));
        target_compile(t);
        t->hits = (int)ct->hits;
        if (ct->suf_hits) memcpy(t->suf_hits, ct->suf_hits, (size_t)ct->n_suffix * sizeof *t->suf_hits);
        if (ct->pre_hits) memcpy(t->pre_hits, ct->pre_hits, (size_t)ct->n_prefix * sizeof *t->pre_hits);
    }
}

// ---------------------------------------------------------------- --checkpoint-merge

// EN: mirror of target_done_locked() (stats.c) on a checkpoint record: a target with a count is
// EN: done once it has that many hits; with --all-patterns every pattern needs its own count
// RU: зеркало target_done_locked() (stats.c) на записи checkpoint: цель с count выполнена, когда
// RU: набрала столько хитов; с --all-patterns каждый шаблон должен набрать свой count
static int cp_target_done(const CheckpointTarget *t) {
    if (!(t->flags & 2)) return 0;
    if (t->flags & 8) {
        for (uint32_t k = 0; k < t->n_prefix; k++)
            if (!t->pre_hits || t->pre_hits[k] < t->count) return 0;
        for (uint32_t k = 0; k < t->n_suffix; k++)
            if (!t->suf_hits || t->suf_hits[k] < t->count) return 0;
        return 1;
    }
    return t->hits >= t->count;
}

// EN: the merge identity of a target: the name plus everything that defines which addresses match.
// EN: Equal identities combine into one; the same name with different identities stays separate.
// RU: идентичность цели при слиянии: имя плюс всё, что задаёт множество совпадающих адресов.
// RU: Одинаковые склеиваются в одну; то же имя с другим содержимым остаётся отдельной целью.
static int cp_target_same(const CheckpointTarget *a, const CheckpointTarget *b) {
    if (strcmp(a->name, b->name)) return 0;
    if ((a->flags ^ b->flags) & (1u | 4u | 8u)) return 0; // EN: ignore-case, match, all-patterns
                                                          // RU: ignore-case, match, all-patterns
    if (a->n_suffix != b->n_suffix || a->n_prefix != b->n_prefix) return 0;
    for (uint32_t k = 0; k < a->n_suffix; k++)
        if (strcmp(a->suffix[k], b->suffix[k])) return 0;
    for (uint32_t k = 0; k < a->n_prefix; k++)
        if (strcmp(a->prefix[k], b->prefix[k])) return 0;
    return 1;
}

static char *cp_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// EN: deep copy of one target record for the merged file. The out line is dropped on purpose: the
// EN: merged file's own folder is the job folder, so every target shares it by default.
// RU: глубокая копия записи цели для объединённого файла. Строка out отбрасывается намеренно:
// RU: папка объединённого файла становится папкой задания, и все цели по умолчанию делят её.
static int cp_target_copy(CheckpointTarget *dst, const CheckpointTarget *src, char *err, size_t errn) {
    *dst = *src;
    dst->out[0] = 0;
    dst->suffix = dst->prefix = NULL;
    dst->suf_hits = dst->pre_hits = NULL;
    if (src->n_suffix) {
        dst->suffix = calloc(src->n_suffix, sizeof *dst->suffix);
        dst->suf_hits = calloc(src->n_suffix, sizeof *dst->suf_hits);
        if (!dst->suffix || !dst->suf_hits) goto oom;
        for (uint32_t k = 0; k < src->n_suffix; k++) {
            if (!(dst->suffix[k] = cp_dup(src->suffix[k]))) goto oom;
            dst->suf_hits[k] = src->suf_hits ? src->suf_hits[k] : 0;
        }
    }
    if (src->n_prefix) {
        dst->prefix = calloc(src->n_prefix, sizeof *dst->prefix);
        dst->pre_hits = calloc(src->n_prefix, sizeof *dst->pre_hits);
        if (!dst->prefix || !dst->pre_hits) goto oom;
        for (uint32_t k = 0; k < src->n_prefix; k++) {
            if (!(dst->prefix[k] = cp_dup(src->prefix[k]))) goto oom;
            dst->pre_hits[k] = src->pre_hits ? src->pre_hits[k] : 0;
        }
    }
    return 0;
oom:
    // EN: the caller has not appended *dst yet, so free the partial copy here (calloc keeps the
    // EN: unassigned entries NULL, so the plain loop is safe)
    // RU: вызывающая сторона ещё не добавила *dst, поэтому частичную копию освобождаем здесь
    // RU: (calloc оставляет незаполненные элементы NULL, поэтому простой цикл безопасен)
    if (dst->suffix)
        for (uint32_t k = 0; k < src->n_suffix; k++) free(dst->suffix[k]);
    if (dst->prefix)
        for (uint32_t k = 0; k < src->n_prefix; k++) free(dst->prefix[k]);
    free(dst->suffix);
    free(dst->suf_hits);
    free(dst->prefix);
    free(dst->pre_hits);
    snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти"));
    return -1;
}

// EN: a name already taken by a target with different patterns gets -2, -3... until unique. The
// EN: base is truncated so that the -N suffix always fits: without that a name of 62+ characters
// EN: would truncate back to itself, always look taken, and loop forever.
// RU: имя, уже занятое целью с другими шаблонами, получает -2, -3..., пока не станет уникальным.
// RU: База обрезается так, чтобы суффикс -N всегда влез: иначе имя из 62+ символов обрезалось бы
// RU: обратно в себя, всегда оказывалось занятым и зациклилось бы.
static void cp_unique_name(char *name, size_t cap, const struct Checkpoint *acc) {
    for (int n = 2;; n++) {
        char try_name[MAX_TARGET_NAME];
        snprintf(try_name, sizeof try_name, "%.*s-%d", MAX_TARGET_NAME - 12, name, n);
        int taken = 0;
        for (uint32_t k = 0; k < acc->n_targets && !taken; k++)
            if (!strcmp(acc->targets[k].name, try_name)) taken = 1;
        if (!taken) {
            snprintf(name, cap, "%s", try_name);
            return;
        }
    }
}

// EN: the same target in several inputs: merge the counters by taking the per-field maximum
// EN: (unlimited beats any count). The identity check has already matched the pattern sets, so the
// EN: per-pattern arrays pair up positionally.
// RU: та же цель в нескольких входах: счётчики по максимуму полей (unlimited сильнее любого count).
// RU: Проверка идентичности уже сопоставила наборы шаблонов, поэтому массивы по шаблонам сходятся
// RU: позиционно.
static void cp_target_combine(CheckpointTarget *dst, const CheckpointTarget *ct) {
    if (!(ct->flags & 2)) dst->flags &= ~2u;
    else if ((dst->flags & 2) && ct->count > dst->count) dst->count = ct->count;
    if (ct->hits > dst->hits) dst->hits = ct->hits;
    for (uint32_t k = 0; k < ct->n_suffix; k++)
        if (ct->suf_hits && ct->suf_hits[k] > dst->suf_hits[k]) dst->suf_hits[k] = ct->suf_hits[k];
    for (uint32_t k = 0; k < ct->n_prefix; k++)
        if (ct->pre_hits && ct->pre_hits[k] > dst->pre_hits[k]) dst->pre_hits[k] = ct->pre_hits[k];
}

// EN: find the accumulated twin of a kept target or append a deep copy of it, renaming the copy
// EN: when its name is already taken by a target with different patterns. Returns the accumulated
// EN: record, or NULL on out of memory (err is filled; the caller frees *out).
// RU: ищет накопленного двойника сохранённой цели или добавляет её глубокую копию, переименовав
// RU: копию, если её имя занято целью с другими шаблонами. Возвращает накопленную запись или NULL
// RU: при нехватке памяти (err заполнен; вызывающая сторона освобождает *out).
static CheckpointTarget *cp_merge_target(struct Checkpoint *out, const CheckpointTarget *ct,
                                         int *nrenamed, char *err, size_t errn) {
    for (uint32_t k = 0; k < out->n_targets; k++)
        if (cp_target_same(&out->targets[k], ct)) {
            cp_target_combine(&out->targets[k], ct);
            return &out->targets[k];
        }
    CheckpointTarget *nt = realloc(out->targets, (out->n_targets + 1) * sizeof *nt);
    if (!nt) {
        snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти"));
        return NULL;
    }
    out->targets = nt;
    CheckpointTarget *dst = &out->targets[out->n_targets];
    if (cp_target_copy(dst, ct, err, errn)) return NULL;
    // EN: the name may already be taken by a kept target with different patterns
    // RU: имя может быть занято сохранённой целью с другими шаблонами
    for (uint32_t k = 0; k < out->n_targets; k++)
        if (!strcmp(out->targets[k].name, dst->name)) {
            cp_unique_name(dst->name, sizeof dst->name, out);
            (*nrenamed)++;
            break;
        }
    out->n_targets++;
    return dst;
}

int checkpoint_merge(const char *const *inputs, int n_inputs, const char *out_path,
                     struct Checkpoint *out, int *dropped, int *renamed,
                     char *err, size_t errn) {
    if (n_inputs < 1) {
        snprintf(err, errn, "%s", tr("--checkpoint-merge: no input files",
                                     "--checkpoint-merge: нет входных файлов"));
        return -1;
    }
    // EN: never overwrite: the output path may hold a precious checkpoint whose key would be lost
    // EN: forever (the check also rules out an input path as the output)
    // RU: никогда не перезаписываем: на выходном пути может лежать ценный checkpoint, чей ключ
    // RU: потерялся бы навсегда (проверка заодно исключает входной путь как выходной)
    FILE *probe = fopen(out_path, "rb");
    if (probe) {
        fclose(probe);
        snprintf(err, errn, "%s: %s", out_path,
                 tr("output checkpoint already exists", "выходной checkpoint уже существует"));
        return -1;
    }
    struct Checkpoint *cps = calloc((size_t)n_inputs, sizeof *cps);
    if (!cps) {
        snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти"));
        return -1;
    }
    // EN: load every input first: the merged file is written only when all of them parse, so a
    // EN: failed merge never touches the output path
    // RU: сначала загружаем все входы: объединённый файл пишется, только если все разобрались,
    // RU: поэтому неудачное слияние никогда не трогает выходной путь
    for (int i = 0; i < n_inputs; i++) {
        char sub[256];
        if (checkpoint_load(inputs[i], &cps[i], sub, sizeof sub)) {
            snprintf(err, errn, "%s: %s", inputs[i], sub);
            goto fail;
        }
        if (i && (cps[i].mode != cps[0].mode || cps[i].flags != cps[0].flags)) {
            snprintf(err, errn, "%s", tr("--checkpoint-merge: all inputs must share the mode, "
                                         "the wallet form and the network",
                                         "--checkpoint-merge: все входы должны совпадать по режиму, "
                                         "форме и сети кошелька"));
            goto fail;
        }
    }
    // EN: the merged file is a new job: a fresh search key and zeroed position and counters.
    // EN: Candidates are derived from the key (HMAC of the index), so nothing has been searched
    // EN: under the new key yet and index 0 is the honest starting point.
    // RU: объединённый файл — новое задание: свежий ключ поиска, позиция и счётчики с нуля.
    // RU: Кандидаты выводятся из ключа (HMAC индекса), под новым ключом ещё ничего не искали,
    // RU: и честная стартовая точка — индекс 0.
    memset(out, 0, sizeof *out);
    out->mode = cps[0].mode;
    out->flags = cps[0].flags;
    random_bytes(out->key, 32);
    int ndropped = 0, nrenamed = 0;
    for (int i = 0; i < n_inputs; i++) {
        for (uint32_t t = 0; t < cps[i].n_targets; t++) {
            const CheckpointTarget *ct = &cps[i].targets[t];
            // EN: done targets are dropped: a different key would make their hits unrecoverable
            // RU: выполненные цели отбрасываются: под другим ключом их хиты не восстановить
            if (cp_target_done(ct)) {
                ndropped++;
                continue;
            }
            if (!cp_merge_target(out, ct, &nrenamed, err, errn)) {
                checkpoint_free(out);
                goto fail;
            }
        }
    }
    if (out->n_targets == 0) {
        snprintf(err, errn, "%s",
                 tr("--checkpoint-merge: nothing to merge: every target has reached its --count",
                    "--checkpoint-merge: делать нечего: все цели достигли своего --count"));
        checkpoint_free(out);
        goto fail;
    }
    if (write_checkpoint_file(out_path, out, err, errn)) {
        checkpoint_free(out);
        goto fail;
    }
    if (dropped) *dropped = ndropped;
    if (renamed) *renamed = nrenamed;
    // EN: the inputs are no longer needed; *out stays filled for the caller's summary
    // RU: входы больше не нужны; *out остаётся заполненным для сводки вызывающей стороны
    for (int i = 0; i < n_inputs; i++) checkpoint_free(&cps[i]);
    free(cps);
    return 0;
fail:
    for (int i = 0; i < n_inputs; i++) checkpoint_free(&cps[i]);
    free(cps);
    return -1;
}
