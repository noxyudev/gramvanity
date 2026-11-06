// EN: checkpoint.c - the binary checkpoint format (see checkpoint.h): serialization of the whole
// EN: search state, atomic durable saves, and parsing of formats 2-8 for --resume.
// RU: checkpoint.c - двоичный формат checkpoint (см. checkpoint.h): сериализация всего состояния
// RU: поиска, атомарные надёжные сохранения и разбор форматов 2-8 для --resume.
#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "pattern.h"
#include "stats.h"
#include "detsearch.h"
#include "checkpoint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

mutex_t ckpt_lock;

static uint32_t le32g(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64g(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

// EN: Serialized layout, format 8 (little-endian). The magic, the version and the field order are a
// EN: compatibility contract with every older reader; the "SVANCKP" magic string itself is never
// EN: translated. See docs/architecture.md for the version history.
// RU: Двоичная раскладка, формат 8 (little-endian). Magic, версия и порядок полей — контракт
// RU: совместимости со всеми старыми читателями; сама строка "SVANCKP" не переводится.
// RU: История версий описана в docs/architecture.md.
//   0x00  char[8]   "SVANCKP\0"
//   0x08  u32       format version (8)
//   0x0C  u32       mode (0 bip39-12, 1 bip39-24, 2 ton-24, 3 tg-wallet)
//   0x10  u32       global flags: bit0 bounceable(EQ/kQ), bit1 reserved, bit2 testnet
//   0x14  u32       number of targets
//   0x18  {u32 len, bytes[len]} job folder
//        for every target:
//          {u32 len, bytes[len]} name (ASCII id used in file names)
//          {u32 len, bytes[len]} label (UTF-8 name as typed; v8)
//          {u32 len, bytes[len]} hit directory
//          u32 target flags: bit0 ignore-case, bit1 count given, bit2 legacy OR semantics,
//                            bit3 prefix/suffix OR (--match or), bit4 --all-patterns
//          u64 count
//          u64 hits
//          u32 n_suffix, {u32 len, bytes[len]} x n_suffix
//          u64 x n_suffix saved hits per suffix (v7, always maintained)
//          u32 n_prefix, {u32 len, bytes[len]} x n_prefix
//          u64 x n_prefix saved hits per prefix (v7, always maintained)
//        u64 start index
//        u64 next index (address-checked resume point)
//        u64 s1_next index (stage-1-confirmed; >= next; equals next in bip39 modes)
//        u64 end index, exclusive (0 = unlimited)
//        u64 candidates fully processed
//        u64 unique saved hits
//        u64 externally verified hits
//        u8[32] search key (SECRET)
//        u8[32] SHA-256 of everything before it
// EN: Formats 2-7 (older layouts, without the target label) are still readable.
// RU: Форматы 2-7 (старые раскладки без метки цели) по-прежнему читаются.
static uint8_t *ckpt_buf;
static size_t ckpt_cap;
static int *ckpt_hits;       // EN: per-target hit snapshot used by ckpt_serialize
                             // RU: снимок хитов по целям для ckpt_serialize
static int ckpt_hits_cap;

void ckpt_in_free(struct CkptIn *in) {
    if (in->targets) {
        for (uint32_t t = 0; t < in->n_targets; t++) {
            for (uint32_t k = 0; k < in->targets[t].n_suffix; k++) free(in->targets[t].suffix[k]);
            for (uint32_t k = 0; k < in->targets[t].n_prefix; k++) free(in->targets[t].prefix[k]);
            free(in->targets[t].suffix);
            free(in->targets[t].prefix);
            free(in->targets[t].suf_hits);
            free(in->targets[t].pre_hits);
        }
        free(in->targets);
    }
    in->targets = NULL;
    in->n_targets = 0;
}

static size_t ckpt_str_need(size_t need, const char *s, size_t max) {
    size_t l = strlen(s);
    if (l > max || l > SIZE_MAX - need - 4) return SIZE_MAX;
    return need + 4 + l;
}

static size_t ckpt_need(void) {
    size_t need = 8 + 4 * 4 + 4 + 8 * 7 + 32 + 32; // EN: header + job dir length + indices + key + hash
                                                  // RU: заголовок + длина папки + индексы + ключ + хеш
    need = ckpt_str_need(need, job_dir, 4095);
    if (need == SIZE_MAX) return SIZE_MAX;
    for (int i = 0; i < n_targets; i++) {
        const Target *t = &targets[i];
        need = ckpt_str_need(need, t->name, MAX_TARGET_NAME - 1);
        if (need == SIZE_MAX) return SIZE_MAX;
        need = ckpt_str_need(need, t->label, MAX_TARGET_LABEL - 1);
        if (need == SIZE_MAX) return SIZE_MAX;
        need = ckpt_str_need(need, t->out_dir, 4095);
        if (need == SIZE_MAX) return SIZE_MAX;
        if (need > SIZE_MAX - (4 + 8 + 8 + 4 + 4)) return SIZE_MAX;
        need += 4 + 8 + 8 + 4 + 4;
        if (need > SIZE_MAX - (size_t)8 * (t->n_pre + t->n_suf)) return SIZE_MAX;
        need += (size_t)8 * (t->n_pre + t->n_suf); // EN: per-pattern saved hits (v7)
                                                   // RU: сохранённые хиты по шаблонам (v7)
        for (int k = 0; k < t->n_pre; k++) need = ckpt_str_need(need, t->pre[k].str, 127);
        if (need == SIZE_MAX) return SIZE_MAX;
        for (int k = 0; k < t->n_suf; k++) need = ckpt_str_need(need, t->suf[k].str, 127);
        if (need == SIZE_MAX) return SIZE_MAX;
    }
    return need;
}

static int ckpt_reserve(void) {
    size_t need = ckpt_need();
    if (need == SIZE_MAX) return -1;
    if (need > ckpt_cap) {
        uint8_t *p = realloc(ckpt_buf, need);
        if (!p) return -1;
        ckpt_buf = p;
        ckpt_cap = need;
    }
    if (n_targets > ckpt_hits_cap) {
        int *p = realloc(ckpt_hits, (size_t)n_targets * sizeof *p);
        if (!p) return -1;
        ckpt_hits = p;
        ckpt_hits_cap = n_targets;
    }
    return 0;
}

void le32p(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
void le64p(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void put_str(uint8_t *buf, size_t *o, const char *s) {
    uint32_t l = (uint32_t)strlen(s);
    le32p(buf + *o, l);
    *o += 4;
    memcpy(buf + *o, s, l);
    *o += l;
}

// EN: The buffer is grown on the first save, when the target list is already final. Later saves (also
// EN: from the console-control handler) reuse it, so the handler does not have to allocate memory.
// RU: Буфер выделяется при первом сохранении, когда список целей уже окончательный. Последующие
// RU: сохранения (в том числе из обработчика консоли) переиспользуют его, поэтому обработчику не
// RU: нужно выделять память.
static size_t ckpt_serialize(void) {
    uint8_t *buf = ckpt_buf;
    size_t o = 0;
    mutex_lock(&stat_lock);
    for (int i = 0; i < n_targets; i++) ckpt_hits[i] = targets[i].hits;
    uint64_t done = (uint64_t)total_done;
    uint64_t uniq = (uint64_t)hits_found;
    uint64_t ver = (uint64_t)verified;
    mutex_unlock(&stat_lock);
    memset(buf, 0, ckpt_cap);
    memcpy(buf, CKPT_MAGIC "\0", 8); o = 8;
    le32p(buf + o, CKPT_VERSION); o += 4;
    le32p(buf + o, (uint32_t)opt.mode); o += 4;
    le32p(buf + o, (uint32_t)((opt.bounceable ? 1 : 0) | (opt.testnet ? 4 : 0))); o += 4;
    le32p(buf + o, (uint32_t)n_targets); o += 4;
    put_str(buf, &o, job_dir);
    for (int i = 0; i < n_targets; i++) {
        const Target *t = &targets[i];
        put_str(buf, &o, t->name);
        put_str(buf, &o, t->label);
        put_str(buf, &o, t->out_dir);
        uint32_t tf = (uint32_t)((t->ignore_case ? 1 : 0) | (t->count_given ? 2 : 0) | (t->legacy_or ? 4 : 0) |
                                 (t->match_or ? 8 : 0) | (t->all_patterns ? 16 : 0));
        le32p(buf + o, tf); o += 4;
        le64p(buf + o, t->count_given ? t->count : 0); o += 8;
        le64p(buf + o, (uint64_t)ckpt_hits[i]); o += 8;
        le32p(buf + o, (uint32_t)t->n_suf); o += 4;
        for (int k = 0; k < t->n_suf; k++) put_str(buf, &o, t->suf[k].str);
        for (int k = 0; k < t->n_suf; k++) { le64p(buf + o, t->suf_hits[k]); o += 8; }
        le32p(buf + o, (uint32_t)t->n_pre); o += 4;
        for (int k = 0; k < t->n_pre; k++) put_str(buf, &o, t->pre[k].str);
        for (int k = 0; k < t->n_pre; k++) { le64p(buf + o, t->pre_hits[k]); o += 8; }
    }
    le64p(buf + o, det.start); o += 8;
    le64p(buf + o, det.next); o += 8;
    le64p(buf + o, det.s1_next > det.next ? det.s1_next : det.next); o += 8;
    le64p(buf + o, det.has_end ? det.end : 0); o += 8;
    le64p(buf + o, done); o += 8;
    le64p(buf + o, uniq); o += 8;
    le64p(buf + o, ver); o += 8;
    memcpy(buf + o, det.key, 32); o += 32;
    sha256(buf, o, buf + o);
    o += 32;
    return o;
}

int checkpoint_save(void) {
    if (!det.enabled || !det.ckpt_path[0]) return 0;
    mutex_lock(&ckpt_lock);
    int ok = -1;
    if (ckpt_reserve() == 0) {
        size_t len = ckpt_serialize();
        ok = write_file_atomic(det.ckpt_path, ckpt_buf, len);
    }
    mutex_unlock(&ckpt_lock);
    if (ok == 0) det.last_save = now_sec();
    return ok;
}

static char *ckpt_get_str(const uint8_t *buf, size_t *o, size_t limit, size_t maxlen, char *err, size_t errn) {
    if (*o + 4 > limit) { snprintf(err, errn, "%s", tr("truncated checkpoint", "обрезанный checkpoint")); return NULL; }
    uint32_t l = le32g(buf + *o);
    *o += 4;
    if (l > maxlen || *o + l > limit) { snprintf(err, errn, "%s", tr("truncated checkpoint", "обрезанный checkpoint")); return NULL; }
    char *s = malloc((size_t)l + 1);
    if (!s) { snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти")); return NULL; }
    memcpy(s, buf + *o, l);
    s[l] = 0;
    *o += l;
    return s;
}

static int ckpt_parse_legacy(const uint8_t *buf, size_t len, size_t o, struct CkptIn *in, uint32_t ver,
                             char *err, size_t errn) {
    uint32_t npats = le32g(buf + o); o += 4;
    if (in->mode > TG) { snprintf(err, errn, "%s", tr("bad mode", "неверный режим")); return -1; }
    if ((in->flags & 4) && in->mode != TG) { snprintf(err, errn, "%s", tr("bad flags: testnet is only defined for tg-wallet", "неверные флаги: testnet определён только для tg-wallet")); return -1; }
    if (npats > (len - 32 - o) / 4) { snprintf(err, errn, "%s", tr("bad pattern count", "неверное число шаблонов")); return -1; }
    CkptTarget *t = calloc(1, sizeof *t);
    in->targets = calloc(1, sizeof *in->targets);
    if (!t || !in->targets) { free(t); free(in->targets); in->targets = NULL; snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти")); return -1; }
    in->targets[0] = *t;
    free(t);
    in->n_targets = 1;
    CkptTarget *ct = &in->targets[0];
    snprintf(ct->name, sizeof ct->name, "t1");
    snprintf(ct->label, sizeof ct->label, "%s", ct->name);
    ct->flags = (uint32_t)(((in->flags & 2) ? 1 : 0) | 4); // EN: legacy OR, old ignore-case
                                                           // RU: legacy OR, старый ignore-case
    if (npats) {
        ct->suffix = calloc(npats, sizeof *ct->suffix);
        if (!ct->suffix) { snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти")); return -1; }
    }
    ct->n_suffix = npats;
    for (uint32_t k = 0; k < ct->n_suffix; k++) {
        ct->suffix[k] = ckpt_get_str(buf, &o, len - 32, 127, err, errn);
        if (!ct->suffix[k]) return -1;
        if (!ct->suffix[k][0]) { snprintf(err, errn, "%s", tr("truncated patterns", "обрезанные шаблоны")); return -1; }
    }
    if (ver >= 4) {
        if (o + 4 > len - 32) { snprintf(err, errn, "%s", tr("truncated prefixes", "обрезанные префиксы")); return -1; }
        uint32_t nppats = le32g(buf + o); o += 4;
        if (nppats > (len - 32 - o) / 4) { snprintf(err, errn, "%s", tr("bad pattern count", "неверное число шаблонов")); return -1; }
        if (nppats) {
            ct->prefix = calloc(nppats, sizeof *ct->prefix);
            if (!ct->prefix) { snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти")); return -1; }
        }
        ct->n_prefix = nppats;
        for (uint32_t k = 0; k < ct->n_prefix; k++) {
            ct->prefix[k] = ckpt_get_str(buf, &o, len - 32, 127, err, errn);
            if (!ct->prefix[k]) return -1;
            if (!ct->prefix[k][0]) { snprintf(err, errn, "%s", tr("truncated prefixes", "обрезанные префиксы")); return -1; }
        }
    }
    if (ct->n_suffix == 0 && ct->n_prefix == 0) { snprintf(err, errn, "%s", tr("no patterns in the checkpoint", "в checkpoint нет шаблонов")); return -1; }
    if (o + 8 * 6 + 32 != len - 32) { snprintf(err, errn, "%s", tr("unexpected size", "неожиданный размер")); return -1; }
    in->start = le64g(buf + o); o += 8;
    in->next = le64g(buf + o); o += 8;
    in->s1_next = le64g(buf + o); o += 8;
    in->end = le64g(buf + o); o += 8;
    in->done = le64g(buf + o); o += 8;
    ct->hits = le64g(buf + o); o += 8;
    in->unique_hits = ct->hits;
    in->verified = ct->hits;
    memcpy(in->key, buf + o, 32); o += 32;
    if (in->next < in->start || (in->end && in->next > in->end)) { snprintf(err, errn, "%s", tr("inconsistent indices", "несогласованные индексы")); return -1; }
    if (in->s1_next < in->next || (in->end && in->s1_next > in->end)) { snprintf(err, errn, "%s", tr("inconsistent indices", "несогласованные индексы")); return -1; }
    return 0;
}

int ckpt_parse(const uint8_t *buf, size_t len, struct CkptIn *in, char *err, size_t errn) {
    memset(in, 0, sizeof *in);
#define CKPT_FAIL(...) do { snprintf(err, errn, __VA_ARGS__); ckpt_in_free(in); return -1; } while (0)
#define CKPT_FAIL_ERR() do { char msg_[256]; snprintf(msg_, sizeof msg_, "%s", err); CKPT_FAIL("%s", msg_); } while (0)
    uint8_t h[32];
    if (len < 8 + 4 * 4 + 8 * 6 + 32 + 32) CKPT_FAIL("%s", tr("file too small", "файл слишком мал"));
    sha256(buf, len - 32, h);
    if (memcmp(h, buf + len - 32, 32)) CKPT_FAIL("%s", tr("checksum mismatch (file corrupted)", "несовпадение контрольной суммы (файл повреждён)"));
    if (memcmp(buf, CKPT_MAGIC "\0", 8)) CKPT_FAIL("%s", tr("bad magic (not a gramvanity checkpoint)", "неверный magic (это не checkpoint gramvanity)"));
    size_t o = 8;
    uint32_t ver = le32g(buf + o); o += 4;
    if (ver < 2 || ver > CKPT_VERSION)
        CKPT_FAIL(tr("checkpoint format version %u, this build supports 2..%u",
                     "версия формата checkpoint %u, эта сборка поддерживает 2..%u"), ver, CKPT_VERSION);
    in->mode = le32g(buf + o); o += 4;
    in->flags = le32g(buf + o); o += 4;
    if (ver < 5) {
        int rc = ckpt_parse_legacy(buf, len, o, in, ver, err, errn);
        if (rc) ckpt_in_free(in);
        return rc;
    }
    uint32_t nt = le32g(buf + o); o += 4;
    if (nt == 0 || (size_t)nt > (len - 32 - o) / 36) CKPT_FAIL("%s", tr("bad target count", "неверное число целей"));
    char *jd = ckpt_get_str(buf, &o, len - 32, 4095, err, errn);
    if (!jd) CKPT_FAIL_ERR();
    snprintf(in->job_dir, sizeof in->job_dir, "%s", jd);
    free(jd);
    in->targets = calloc(nt, sizeof *in->targets);
    if (!in->targets) CKPT_FAIL("%s", tr("out of memory", "не хватает памяти"));
    in->n_targets = nt;
    if (in->mode > TG) CKPT_FAIL("%s", tr("bad mode", "неверный режим"));
    if ((in->flags & 4) && in->mode != TG) CKPT_FAIL("%s", tr("bad flags: testnet is only defined for tg-wallet", "неверные флаги: testnet определён только для tg-wallet"));
    for (uint32_t t = 0; t < nt; t++) {
        CkptTarget *ct = &in->targets[t];
        char *name = ckpt_get_str(buf, &o, len - 32, MAX_TARGET_NAME - 1, err, errn);
        if (!name) CKPT_FAIL_ERR();
        snprintf(ct->name, sizeof ct->name, "%s", name);
        free(name);
        if (ver >= 8) {
            char *label = ckpt_get_str(buf, &o, len - 32, MAX_TARGET_LABEL - 1, err, errn);
            if (!label) CKPT_FAIL_ERR();
            snprintf(ct->label, sizeof ct->label, "%s", label);
            free(label);
        } else {
            snprintf(ct->label, sizeof ct->label, "%s", ct->name);
        }
        char *out = ckpt_get_str(buf, &o, len - 32, 4095, err, errn);
        if (!out) CKPT_FAIL_ERR();
        snprintf(ct->out, sizeof ct->out, "%s", out);
        free(out);
        if (o + 4 > len - 32) CKPT_FAIL("%s", tr("truncated target", "обрезанная цель"));
        ct->flags = le32g(buf + o); o += 4;
        if (ct->flags & ~31u) CKPT_FAIL("%s", tr("bad target flags", "неверные флаги цели"));
        if (o + 16 > len - 32) CKPT_FAIL("%s", tr("truncated target", "обрезанная цель"));
        ct->count = le64g(buf + o); o += 8;
        ct->hits = le64g(buf + o); o += 8;
        if (o + 4 > len - 32) CKPT_FAIL("%s", tr("truncated target", "обрезанная цель"));
        uint32_t ns = le32g(buf + o); o += 4;
        if (ns > (len - 32 - o) / 4) CKPT_FAIL("%s", tr("bad pattern count", "неверное число шаблонов"));
        if (ns) {
            ct->suffix = calloc(ns, sizeof *ct->suffix);
            if (!ct->suffix) CKPT_FAIL("%s", tr("out of memory", "не хватает памяти"));
        }
        ct->n_suffix = ns;
        for (uint32_t k = 0; k < ns; k++) {
            ct->suffix[k] = ckpt_get_str(buf, &o, len - 32, 127, err, errn);
            if (!ct->suffix[k]) CKPT_FAIL_ERR();
            if (!ct->suffix[k][0]) CKPT_FAIL("%s", tr("empty pattern", "пустой шаблон"));
        }
        if (ver >= 7) { // EN: saved hits per pattern, always maintained
                        // RU: сохранённые хиты по шаблонам, ведутся всегда
            if (o + (size_t)8 * ns > len - 32) CKPT_FAIL("%s", tr("truncated suffix hit counts", "обрезанные счётчики хитов окончаний"));
            if (ns) {
                ct->suf_hits = calloc(ns, sizeof *ct->suf_hits);
                if (!ct->suf_hits) CKPT_FAIL("%s", tr("out of memory", "не хватает памяти"));
            }
            for (uint32_t k = 0; k < ns; k++) { ct->suf_hits[k] = le64g(buf + o); o += 8; }
        }
        if (o + 4 > len - 32) CKPT_FAIL("%s", tr("truncated target", "обрезанная цель"));
        uint32_t np = le32g(buf + o); o += 4;
        if (np > (len - 32 - o) / 4) CKPT_FAIL("%s", tr("bad pattern count", "неверное число шаблонов"));
        if (np) {
            ct->prefix = calloc(np, sizeof *ct->prefix);
            if (!ct->prefix) CKPT_FAIL("%s", tr("out of memory", "не хватает памяти"));
        }
        ct->n_prefix = np;
        for (uint32_t k = 0; k < np; k++) {
            ct->prefix[k] = ckpt_get_str(buf, &o, len - 32, 127, err, errn);
            if (!ct->prefix[k]) CKPT_FAIL_ERR();
            if (!ct->prefix[k][0]) CKPT_FAIL("%s", tr("empty pattern", "пустой шаблон"));
        }
        if (ver >= 7) {
            if (o + (size_t)8 * np > len - 32) CKPT_FAIL("%s", tr("truncated prefix hit counts", "обрезанные счётчики хитов префиксов"));
            if (np) {
                ct->pre_hits = calloc(np, sizeof *ct->pre_hits);
                if (!ct->pre_hits) CKPT_FAIL("%s", tr("out of memory", "не хватает памяти"));
            }
            for (uint32_t k = 0; k < np; k++) { ct->pre_hits[k] = le64g(buf + o); o += 8; }
        }
        if (!ct->n_suffix && !ct->n_prefix) CKPT_FAIL("%s", tr("target without patterns", "цель без шаблонов"));
        if ((ct->flags & 2) == 0) ct->count = 0;
    }
    if (o + 8 * 7 + 32 != len - 32) CKPT_FAIL("%s", tr("unexpected size", "неожиданный размер"));
    in->start = le64g(buf + o); o += 8;
    in->next = le64g(buf + o); o += 8;
    in->s1_next = le64g(buf + o); o += 8;
    in->end = le64g(buf + o); o += 8;
    in->done = le64g(buf + o); o += 8;
    in->unique_hits = le64g(buf + o); o += 8;
    in->verified = le64g(buf + o); o += 8;
    memcpy(in->key, buf + o, 32); o += 32;
    if (in->next < in->start || (in->end && in->next > in->end)) CKPT_FAIL("%s", tr("inconsistent indices", "несогласованные индексы"));
    if (in->s1_next < in->next || (in->end && in->s1_next > in->end)) CKPT_FAIL("%s", tr("inconsistent indices", "несогласованные индексы"));
    return 0;
#undef CKPT_FAIL
#undef CKPT_FAIL_ERR
}

int ckpt_load(const char *path, struct CkptIn *in, char *err, size_t errn) {
    memset(in, 0, sizeof *in);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, tr("cannot open %s", "не удалось открыть %s"), path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); snprintf(err, errn, tr("cannot read %s", "не удалось прочитать %s"), path); return -1; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        snprintf(err, errn, tr("cannot read %s", "не удалось прочитать %s"), path);
        return -1;
    }
    size_t len = (size_t)sz;
    uint8_t *buf = malloc(len ? len : 1);
    if (!buf) { fclose(f); snprintf(err, errn, "%s", tr("out of memory", "не хватает памяти")); return -1; }
    size_t got = fread(buf, 1, len, f);
    int bad = ferror(f) || got != len;
    fclose(f);
    if (bad) { free(buf); snprintf(err, errn, tr("read error on %s", "ошибка чтения %s"), path); return -1; }
    int rc = ckpt_parse(buf, len, in, err, errn);
    free(buf);
    return rc;
}

// ---------------------------------------------------------------- targets from a checkpoint

static int strlist_matches_ckpt(const StrList *l, char **v, uint32_t n) {
    if ((uint32_t)l->n != n) return 0;
    for (int k = 0; k < l->n; k++)
        if (strcmp(l->v[k], v[k])) return 0;
    return 1;
}

void job_dir_from_checkpoint(struct CkptIn *in, const char *resume_path) {
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

void resume_apply_target_overrides(struct CkptIn *in) {
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
            if (((in->targets[k].flags & 8) != 0) != !!opt.match_or)
                die(tr("--resume: --match conflicts with checkpoint target \"%s\"",
                       "--resume: --match конфликтует с целью checkpoint \"%s\""), in->targets[k].name);
    // EN: --all-patterns does not change which addresses match, only when the targets are done, so it
    // EN: can be added on a resume (and the per-pattern counters of older v5/v6 parts start at zero).
    // RU: --all-patterns не меняет, какие адреса совпадают, только когда цели завершены, поэтому его
    // RU: можно добавить при продолжении (а счётчики по шаблонам старых частей v5/v6 начинаются с нуля).
    if (opt.set_all_patterns)
        for (uint32_t k = 0; k < in->n_targets; k++)
            in->targets[k].flags |= 16;
    // EN: Without --target blocks, global patterns have no block to act as defaults for; they must at
    // EN: least describe every checkpoint target, otherwise a mistyped pattern would be ignored.
    // RU: Без блоков --target глобальным шаблонам не для чего быть значениями по умолчанию; они
    // RU: должны хотя бы описывать каждую цель checkpoint, иначе опечатка в шаблоне была бы проигнорирована.
    if (n_targets == 0)
        for (uint32_t k = 0; k < in->n_targets; k++) {
            CkptTarget *ct = &in->targets[k];
            if (g_prefix.n && !strlist_matches_ckpt(&g_prefix, ct->prefix, ct->n_prefix))
                die(tr("--resume: --prefix conflicts with checkpoint target \"%s\"",
                       "--resume: --prefix конфликтует с целью checkpoint \"%s\""), ct->name);
            if (g_suffix.n && !strlist_matches_ckpt(&g_suffix, ct->suffix, ct->n_suffix))
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
                const CkptTarget *ct = &in->targets[j];
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
        CkptTarget *ct = &in->targets[k];
        if (t->has_prefix || g_prefix.n) {
            const StrList *l = t->has_prefix ? &t->prefix : &g_prefix;
            if (!strlist_matches_ckpt(l, ct->prefix, ct->n_prefix))
                die(tr("--resume: --prefix conflicts with checkpoint target \"%s\"",
                       "--resume: --prefix конфликтует с целью checkpoint \"%s\""), ct->name);
        }
        if (t->has_suffix || g_suffix.n) {
            const StrList *l = t->has_suffix ? &t->suffix : &g_suffix;
            if (!strlist_matches_ckpt(l, ct->suffix, ct->n_suffix))
                die(tr("--resume: --suffix conflicts with checkpoint target \"%s\"",
                       "--resume: --suffix конфликтует с целью checkpoint \"%s\""), ct->name);
        }
        if (t->ignore_case_given && ((ct->flags & 1) != 0) != !!t->ignore_case)
            die(tr("--resume: --ignore-case conflicts with checkpoint target \"%s\"",
                   "--resume: --ignore-case конфликтует с целью checkpoint \"%s\""), ct->name);
        if (t->match_given && ((ct->flags & 8) != 0) != !!t->match_or)
            die(tr("--resume: --match conflicts with checkpoint target \"%s\"",
                   "--resume: --match конфликтует с целью checkpoint \"%s\""), ct->name);
        if (t->count_given) {
            ct->flags |= 2;
            ct->count = t->count;
        }
        if (t->all_patterns_given) ct->flags |= 16;
        if (t->out_given) snprintf(ct->out, sizeof ct->out, "%s", t->out_dir);
    }
    free(used);
}

void targets_from_checkpoint(struct CkptIn *in) {
    n_targets = 0;
    cur_target = -1;
    for (uint32_t k = 0; k < in->n_targets; k++) {
        CkptTarget *ct = &in->targets[k];
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
        t->match_or = (ct->flags & 8) != 0;
        t->all_patterns_given = 1;
        t->all_patterns = (ct->flags & 16) != 0;
        t->legacy_or = (ct->flags & 4) != 0;
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
