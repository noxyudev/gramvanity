// EN: pattern.c - target blocks and their patterns: parsing, compilation, matching and probability.
// EN: Also holds the UTF-8 codec, the CP1251 fallback and the Cyrillic transliteration used to turn
// EN: --target names into safe file-name ids.
// RU: pattern.c - блоки целей и их шаблоны: разбор, компиляция, сопоставление и вероятность.
// RU: Здесь же UTF-8-кодек, запасной разбор CP1251 и кириллическая транслитерация, превращающая
// RU: имена --target в безопасные идентификаторы для имён файлов.
#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "pattern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// EN: Target storage and the current --target block: targets grows on demand, the number of targets
// EN: is limited only by memory; cur_target is -1 while only global options are being parsed.
// RU: Хранилище целей и текущий блок --target: targets растёт по необходимости, число целей
// RU: ограничено только памятью; cur_target равен -1, пока разбираются только глобальные параметры.
Target *targets;
int n_targets;
int n_targets_cap;
int targets_declared; // EN: at least one --target was given | RU: был задан хотя бы один --target
int cur_target = -1;  // EN: target block that receives target options, -1 = globals
                      // RU: блок цели, принимающий параметры цели, -1 = глобальные

StrList g_prefix, g_suffix;

void strlist_add(StrList *l, const char *s, size_t len) {
    if ((size_t)l->n == (size_t)l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        char **v = realloc(l->v, (size_t)cap * sizeof *v);
        if (!v) die(tr("out of memory", "не хватает памяти"));
        l->v = v;
        l->cap = cap;
    }
    char *copy = malloc(len + 1);
    if (!copy) die(tr("out of memory", "не хватает памяти"));
    memcpy(copy, s, len);
    copy[len] = 0;
    l->v[l->n++] = copy;
}

void strlist_add_csv(StrList *l, const char *s) {
    const char *p = s;
    int added = 0;
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len) {
            strlist_add(l, p, len);
            added++;
        }
        if (!comma) break;
        p = comma + 1;
    }
    if (!added) die(tr("empty pattern list", "пустой список шаблонов"));
}

void target_add_patterns(Target *t, int prefix, const char *value) {
    if (prefix) {
        strlist_add_csv(&t->prefix, value);
        t->has_prefix = 1;
    } else {
        strlist_add_csv(&t->suffix, value);
        t->has_suffix = 1;
    }
}

Target *new_target(void) {
    if (n_targets == n_targets_cap) {
        int cap = n_targets_cap ? n_targets_cap * 2 : 16;
        Target *p = realloc(targets, (size_t)cap * sizeof *p);
        if (!p) die(tr("out of memory", "не хватает памяти"));
        targets = p;
        n_targets_cap = cap;
    }
    Target *t = &targets[n_targets++];
    memset(t, 0, sizeof *t);
    return t;
}

static int is_b64(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// ---------------------------------------------------------------- target names and labels

// EN: Decodes one strict UTF-8 sequence. Returns 0 for invalid, overlong or surrogate input.
// RU: Декодирует одну строгую последовательность UTF-8. Возвращает 0 для недопустимого,
// RU: избыточно длинного или суррогатного ввода.
static int utf8_decode(const unsigned char *s, size_t n, size_t *used, uint32_t *cp) {
    if (!n) return 0;
    unsigned char c = s[0];
    if (c < 0x80) { *cp = c; *used = 1; return 1; }
    int len;
    uint32_t v;
    if ((c & 0xE0) == 0xC0) { len = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; v = c & 0x07; }
    else return 0;
    if ((size_t)len > n) return 0;
    for (int k = 1; k < len; k++) {
        if ((s[k] & 0xC0) != 0x80) return 0;
        v = (v << 6) | (s[k] & 0x3F);
    }
    if ((len == 2 && v < 0x80) || (len == 3 && v < 0x800) || (len == 4 && v < 0x10000) ||
        v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
        return 0;
    *cp = v;
    *used = (size_t)len;
    return 1;
}

static int utf8_valid(const char *s) {
    size_t n = strlen(s), o = 0;
    while (o < n) {
        size_t used;
        uint32_t cp;
        if (!utf8_decode((const unsigned char *)s + o, n - o, &used, &cp)) return 0;
        o += used;
    }
    return 1;
}

static size_t utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// EN: Windows hands Cyrillic command-line arguments to a plain main() in the active ANSI code page,
// EN: which is usually Windows-1251. A name that is not valid UTF-8 is therefore read as CP1251.
// RU: Windows передаёт кириллические аргументы командной строки в обычный main() в активной
// RU: ANSI-кодовой странице, обычно Windows-1251. Поэтому имя, не являющееся корректным UTF-8,
// RU: читается как CP1251.
static uint32_t cp1251_to_unicode(unsigned char c) {
    if (c < 0x80) return c;
    switch (c) {
        case 0xA5: return 0x0490; // Ґ
        case 0xA6: return 0x0406; // І
        case 0xA7: return 0x0407; // Ї
        case 0xA8: return 0x0401; // Ё
        case 0xAA: return 0x0404; // Є
        case 0xAF: return 0x040E; // Ў
        case 0xB4: return 0x0491; // ґ
        case 0xB6: return 0x0456; // і
        case 0xB7: return 0x0457; // ї
        case 0xB8: return 0x0451; // ё
        case 0xBA: return 0x0454; // є
        case 0xBF: return 0x045E; // ў
        default:
            if (c >= 0xC0) return 0x0410 + (c - 0xC0); // А..я
            return '_';                                // EN: punctuation outside the common range
                                                       // RU: пунктуация вне общего диапазона
    }
}

void target_label_normalize(const char *in, char *out, size_t cap) {
    size_t n = strlen(in);
    if (utf8_valid(in)) {
        if (n >= cap) die(tr("--target: name too long (max %d bytes in UTF-8)",
                             "--target: слишком длинное имя (максимум %d байт в UTF-8)"), (int)cap - 1);
        memcpy(out, in, n + 1);
        return;
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char enc[4];
        size_t l = utf8_encode(cp1251_to_unicode((unsigned char)in[i]), enc);
        if (o + l >= cap) die(tr("--target: name too long (max %d bytes in UTF-8)",
                                 "--target: слишком длинное имя (максимум %d байт в UTF-8)"), (int)cap - 1);
        memcpy(out + o, enc, l);
        o += l;
    }
    out[o] = 0;
}

static const char *const translit_upper[32] = {
    "A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "Y", "K", "L", "M", "N", "O", "P",
    "R", "S", "T", "U", "F", "Kh", "Ts", "Ch", "Sh", "Shch", "", "Y", "", "E", "Yu", "Ya",
};
static const char *const translit_lower[32] = {
    "a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
    "r", "s", "t", "u", "f", "kh", "ts", "ch", "sh", "shch", "", "y", "", "e", "yu", "ya",
};

static const char *translit_cp(uint32_t cp) {
    if (cp >= 0x0410 && cp <= 0x042F) return translit_upper[cp - 0x0410];
    if (cp >= 0x0430 && cp <= 0x044F) return translit_lower[cp - 0x0430];
    switch (cp) {
        case 0x0401: return "Yo"; // Ё
        case 0x0451: return "yo"; // ё
        case 0x0404: return "Ye"; // Є
        case 0x0454: return "ye"; // є
        case 0x0406: return "I";  // І
        case 0x0456: return "i";  // і
        case 0x0407: return "Yi"; // Ї
        case 0x0457: return "yi"; // ї
        case 0x0490: return "G";  // Ґ
        case 0x0491: return "g";  // ґ
        case 0x040E: return "U";  // Ў
        case 0x045E: return "u";  // ў
        default: return NULL;
    }
}

void target_slug(const char *label, char *out, size_t cap) {
    const unsigned char *s = (const unsigned char *)label;
    size_t n = strlen(label), i = 0, o = 0;
    int sep = 0;
    while (i < n) {
        size_t used = 1;
        uint32_t cp = 0xFFFD;
        if (!utf8_decode(s + i, n - i, &used, &cp)) used = 1;
        i += used;
        const char *lat = translit_cp(cp);
        if (lat) {
            for (const char *p = lat; *p && o + 1 < cap; p++) { out[o++] = *p; sep = 0; }
        } else if (cp < 0x80 && is_b64((int)cp)) {
            if (o + 1 < cap) { out[o++] = (char)cp; sep = 0; }
        } else if (o > 0 && !sep && o + 1 < cap) {
            out[o++] = '_';
            sep = 1;
        }
    }
    while (o > 0 && out[o - 1] == '_') o--;
    out[o] = 0;
    if (!o) die(tr("--target: name \"%s\" has no letters or digits usable in a file name",
                   "--target: в имени \"%s\" нет букв или цифр, пригодных для имени файла"), label);
}

static const char *display_name(const char *name, const char *label, char *buf, size_t cap) {
    if (label && label[0] && strcmp(label, name)) snprintf(buf, cap, "%s (%s)", name, label);
    else snprintf(buf, cap, "%s", name);
    return buf;
}

const char *target_display(const Target *t, char *buf, size_t cap) {
    return display_name(t->name, t->label, buf, cap);
}

static void allow(uint8_t *set, int c, int ignore_case) {
    if (!is_b64(c)) die(tr("pattern may contain only A-Z a-z 0-9 - _ and [...] classes",
                           "шаблон может содержать только A-Z a-z 0-9 - _ и классы [...]"));
    set[c] = 1;
    if (ignore_case && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) set[c ^ 0x20] = 1;
}

// EN: Parses one pattern into an array. prefix != 0 matches right after the two-character form tag
// EN: (UQ/EQ/0Q/kQ), otherwise at the end of the address.
// RU: Разбирает один шаблон в массив. prefix != 0 — совпадение сразу после двухсимвольного тега
// RU: формы (UQ/EQ/0Q/kQ), иначе — в конце адреса.
static void pattern_add(Pattern **arr, int *count, int *cap, const char *s, int prefix, int ignore_case) {
    if (*count == *cap) {
        int ncap = *cap ? *cap * 2 : 8;
        Pattern *p = realloc(*arr, (size_t)ncap * sizeof *p);
        if (!p) die(tr("out of memory", "не хватает памяти"));
        *arr = p;
        *cap = ncap;
    }
    Pattern *p = &(*arr)[(*count)++];
    memset(p, 0, sizeof *p);
    size_t i = 0, n = strlen(s);
    int len = 0;
    while (i < n) {
        if (len >= MAXPLEN)
            die(tr("pattern longer than %d characters is hopeless",
                   "шаблон длиннее %d символов безнадёжен"), MAXPLEN);
        uint8_t *set = p->set[len];
        if (s[i] == '[') {
            const char *close = strchr(s + i, ']');
            if (!close || close == s + i + 1) die(tr("unclosed [ in pattern", "незакрытая [ в шаблоне"));
            size_t j = i + 1, end = (size_t)(close - s);
            while (j < end) {
                if (j + 2 < end && s[j + 1] == '-') {
                    if ((unsigned char)s[j] > (unsigned char)s[j + 2]) die(tr("bad range in pattern", "неверный диапазон в шаблоне"));
                    for (int c = (unsigned char)s[j]; c <= (unsigned char)s[j + 2]; c++)
                        if (is_b64(c)) allow(set, c, ignore_case);
                    j += 3;
                } else {
                    allow(set, (unsigned char)s[j], ignore_case);
                    j++;
                }
            }
            i = end + 1;
        } else {
            allow(set, (unsigned char)s[i], ignore_case);
            i++;
        }
        len++;
    }
    if (!len) die(tr("empty pattern", "пустой шаблон"));
    p->len = len;
    p->prefix = prefix != 0;
    snprintf(p->str, sizeof p->str, "%s", s);
}

// EN: Per-pattern saved-hit counters, one per compiled pattern. They are always maintained (also
// EN: without --all-patterns) and stored in the checkpoint, so a job can add --all-patterns on a later
// EN: --resume without losing the progress already made.
// RU: Счётчики сохранённых хитов по шаблонам, по одному на скомпилированный шаблон. Они ведутся
// RU: всегда (и без --all-patterns) и хранятся в checkpoint, поэтому задание может добавить
// RU: --all-patterns при более позднем --resume, не теряя уже сделанного прогресса.
static void pattern_hits_reset(uint64_t **arr, int *cap, int n) {
    if (n > *cap) {
        uint64_t *p = realloc(*arr, (size_t)n * sizeof *p);
        if (!p) die(tr("out of memory", "не хватает памяти"));
        *arr = p;
        *cap = n;
    }
    memset(*arr, 0, (size_t)n * sizeof **arr);
}

void target_compile(Target *t) {
    const StrList *pre = t->has_prefix ? &t->prefix : &g_prefix;
    const StrList *suf = t->has_suffix ? &t->suffix : &g_suffix;
    int cap_pre = 0, cap_suf = 0;
    for (int k = 0; k < pre->n; k++)
        pattern_add(&t->pre, &t->n_pre, &cap_pre, pre->v[k], 1, t->ignore_case);
    for (int k = 0; k < suf->n; k++)
        pattern_add(&t->suf, &t->n_suf, &cap_suf, suf->v[k], 0, t->ignore_case);
    if (!t->n_pre && !t->n_suf)
        die(tr("target \"%s\": no --prefix or --suffix",
               "цель \"%s\": нет --prefix или --suffix"), t->name);
    pattern_hits_reset(&t->pre_hits, &t->pre_hits_cap, t->n_pre);
    pattern_hits_reset(&t->suf_hits, &t->suf_hits_cap, t->n_suf);
}

// EN: Target names end up in hit file names, so only file-name-safe characters are allowed. The name
// EN: must also not contain '+' (the separator between targets in a multi-target hit file).
// RU: Имена целей попадают в имена hit-файлов, поэтому разрешены только безопасные для имени файла
// RU: символы. Имя также не должно содержать '+' (разделитель целей в мультицелевом hit-файле).
static int target_name_ok(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n >= MAX_TARGET_NAME) return 0;
    for (size_t i = 0; i < n; i++)
        if (!is_b64((unsigned char)name[i])) return 0;
    return 1;
}

void target_names_resolve(void) {
    int unnamed = 0;
    for (int i = 0; i < n_targets; i++) {
        Target *t = &targets[i];
        char base[MAX_TARGET_NAME];
        if (t->name_given && t->given_name[0]) {
            if (!target_name_ok(t->given_name))
                die(tr("--target: name \"%s\" transliterates to \"%s\", which is not a safe file name",
                       "--target: имя \"%s\" транслитерируется в \"%s\", что небезопасно для имени файла"),
                    t->label, t->given_name);
            snprintf(base, sizeof base, "%s", t->given_name);
        } else {
            snprintf(base, sizeof base, "t%d", ++unnamed);
        }
        snprintf(t->name, sizeof t->name, "%s", base);
        for (int suffix = 2;; suffix++) {
            int clash = 0;
            for (int k = 0; k < i; k++)
                if (!strcmp(targets[k].name, t->name)) { clash = 1; break; }
            if (!clash) break;
            snprintf(t->name, sizeof t->name, "%.40s%d", base, suffix);
        }
        if (!t->label[0]) snprintf(t->label, sizeof t->label, "%s", t->name);
    }
}

void targets_prepare(void) {
    target_names_resolve();
    for (int i = 0; i < n_targets; i++) {
        Target *t = &targets[i];
        if (!t->count_given) { // EN: every target stops after its first hit unless --count says otherwise
                               // RU: каждая цель останавливается после первого хита, если --count не задаёт иное
            t->count_given = 1;
            t->count = opt.count;
        }
        if (!t->ignore_case_given) t->ignore_case = opt.ignore_case;
        if (!t->match_given) t->match_or = opt.match_or;
        if (!t->all_patterns_given) t->all_patterns = opt.all_patterns;
        if (!t->out_given) snprintf(t->out_dir, sizeof t->out_dir, "%s", job_dir);
        target_compile(t);
    }
}

int pattern_matches(const Pattern *p, const char addr[49]) {
    int off = p->prefix ? 2 : 48 - p->len; // EN: skip the UQ/EQ/0Q/kQ form tag for prefixes
                                           // RU: для префиксов пропускаем тег формы UQ/EQ/0Q/kQ
    for (int k = 0; k < p->len; k++)
        if (!p->set[k][(unsigned char)addr[off + k]]) return 0;
    return 1;
}

static int group_matches(const Pattern *p, int n, const char addr[49]) {
    for (int k = 0; k < n; k++)
        if (pattern_matches(&p[k], addr)) return 1;
    return 0;
}

int target_matches(const Target *t, const char addr[49]) {
    if (t->match_or) {
        int any_pre = t->n_pre != 0 && group_matches(t->pre, t->n_pre, addr);
        int any_suf = t->n_suf != 0 && group_matches(t->suf, t->n_suf, addr);
        return any_pre || any_suf;
    }
    int pre = t->n_pre == 0 || group_matches(t->pre, t->n_pre, addr);
    int suf = t->n_suf == 0 || group_matches(t->suf, t->n_suf, addr);
    return pre && suf;
}

// ---------------------------------------------------------------- all-targets matcher

// EN: finish() checks every candidate against every target. With hundreds of targets the per-target
// EN: scan dominates the CPU post-phase (one target test costs a few first-character lookups and the
// EN: target loop itself), so all patterns are folded into two exact bitset NFAs. All suffix patterns
// EN: are anchored to the last address character (depth d counts from the end), all prefix patterns
// EN: to the character right after the form tag (depth d counts forward). At depth d one AND with the
// EN: table row for the actual character advances every pattern at once; a pattern of length d+1 that
// EN: is still alive has matched completely. The result is identical to scanning every pattern,
// EN: because every AND applies the very set[k][c] byte the per-pattern scan would read.
// RU: finish() проверяет каждого кандидата по всем целям. При сотнях целей перебор целей доминирует
// RU: в CPU-пост-фазе (проверка одной цели — несколько чтений первых символов и сам цикл по целям),
// RU: поэтому все шаблоны свёрнуты в два точных битсет-NFA. Все шаблоны окончаний заякорены к
// RU: последнему символу адреса (глубина d считается от конца), все шаблоны префиксов — к символу
// RU: сразу после тега формы (глубина d считается вперёд). На глубине d один AND со строкой таблицы
// RU: для фактического символа продвигает все шаблоны сразу; шаблон длины d+1, оставшийся живым,
// RU: совпал полностью. Результат идентичен перебору каждого шаблона, потому что каждый AND применяет
// RU: тот самый байт set[k][c], который прочитал бы перебор по шаблонам.

#define MATCH_CHARS 65 // EN: 64 address-alphabet chars, index 64 = everything else | RU: 64 символа алфавита адреса, индекс 64 — всё прочее
#define MATCH_IDS 96   // EN: per-address match list cap before the fallback scan | RU: предел списка совпадений до отката к перебору

// EN: Address characters are base64url; every other byte can never occur in an address but maps to
// EN: the catch-all index so the tables stay exact for any input byte.
// RU: Символы адреса — base64url; любой другой байт в адресе не встречается, но отображается в
// RU: общий индекс, чтобы таблицы оставались точными для любого входного байта.
static uint8_t m_char_idx[256];

static uint32_t m_words;               // EN: 64-bit words per pattern bitmask | RU: 64-битных слов на битмаску шаблонов
static uint32_t m_npat;                // EN: total compiled patterns | RU: всего скомпилированных шаблонов
static uint32_t m_suf_maxlen, m_pre_maxlen;
static uint64_t *m_suf_tab, *m_pre_tab; // EN: [depth][char][word] | RU: [глубина][символ][слово]
static uint64_t *m_suf_len, *m_pre_len; // EN: [len][word]: patterns of exactly this length | RU: [длина][слово]: шаблоны ровно этой длины
static uint64_t *m_suf_all, *m_pre_all; // EN: all suffix/prefix patterns | RU: все шаблоны окончаний/префиксов
static uint32_t *m_pat_target;          // EN: pattern id -> target index | RU: id шаблона -> индекс цели
static uint8_t *m_pat_suf;              // EN: pattern id -> 1 = suffix group | RU: id шаблона -> 1 = группа окончаний
static int m_check;                     // EN: GRAMVANITY_MATCHER_CHECK=1: cross-check every result | RU: сверять каждый результат

static void match_char_idx_init(void) {
    memset(m_char_idx, 64, sizeof m_char_idx);
    int n = 0;
    for (int c = 'A'; c <= 'Z'; c++) m_char_idx[c] = (uint8_t)n++;
    for (int c = 'a'; c <= 'z'; c++) m_char_idx[c] = (uint8_t)n++;
    for (int c = '0'; c <= '9'; c++) m_char_idx[c] = (uint8_t)n++;
    m_char_idx['-'] = (uint8_t)n++;
    m_char_idx['_'] = (uint8_t)n++;
}

// EN: Advances all patterns of one group through the address and collects fully matched pattern ids.
// EN: from_end: depth d reads addr[47-d] (suffix group), otherwise addr[2+d] (prefix group).
// RU: Продвигает все шаблоны одной группы по адресу и собирает id полностью совпавших шаблонов.
// RU: from_end: глубина d читает addr[47-d] (группа окончаний), иначе addr[2+d] (группа префиксов).
static int match_side(const uint64_t *tab, const uint64_t *lenm, const uint64_t *allm,
                      uint32_t maxlen, const char *addr, int from_end,
                      uint32_t *ids, int cap, int *n_ids, int *overflow) {
    if (!maxlen) return 0;
    // EN: a fixed buffer covers up to 16384 patterns (m_words <= 256, 2 KB of stack); the heap
    // EN: handles anything beyond, so the hot per-candidate path stays allocation-free for any
    // EN: configuration the tables can plausibly hold.
    // RU: фиксированный буфер покрывает до 16384 шаблонов (m_words <= 256, 2 КБ стека); сверх
    // RU: этого куча, поэтому горячий покандидатный путь остаётся без выделений при любой
    // RU: конфигурации, которую таблицы разумно вмещают.
    uint64_t stack_alive[256];
    uint64_t *alive = m_words <= 256 ? stack_alive : malloc((size_t)m_words * 8);
    if (!alive) die(tr("out of memory", "не хватает памяти"));
    memcpy(alive, allm, (size_t)m_words * 8);
    int any = 0;
    for (uint32_t d = 0; d < maxlen; d++) {
        uint8_t ci = m_char_idx[(unsigned char)addr[from_end ? 47 - d : 2 + d]];
        const uint64_t *row = tab + ((size_t)d * MATCH_CHARS + ci) * m_words;
        int nz = 0;
        for (uint32_t w = 0; w < m_words; w++) {
            alive[w] &= row[w];
            nz |= alive[w] != 0;
        }
        if (!nz) break;
        const uint64_t *lm = lenm + (size_t)(d + 1) * m_words;
        for (uint32_t w = 0; w < m_words; w++) {
            uint64_t f = alive[w] & lm[w];
            while (f) {
                int b = __builtin_ctzll(f);
                f &= f - 1;
                any = 1;
                if (*n_ids < cap) ids[(*n_ids)++] = (uint32_t)(w * 64 + b);
                else *overflow = 1;
            }
        }
    }
    if (alive != stack_alive) free(alive);
    return any;
}

void targets_match_build(void) {
    free(m_suf_tab), free(m_pre_tab), free(m_suf_len), free(m_pre_len);
    free(m_suf_all), free(m_pre_all), free(m_pat_target), free(m_pat_suf);
    m_suf_tab = m_pre_tab = m_suf_len = m_pre_len = m_suf_all = m_pre_all = NULL;
    m_pat_target = NULL;
    m_pat_suf = NULL;
    m_npat = 0;
    m_suf_maxlen = m_pre_maxlen = 0;
    static int idx_done;
    if (!idx_done) {
        match_char_idx_init();
        idx_done = 1;
        m_check = getenv("GRAMVANITY_MATCHER_CHECK") != NULL;
    }
    uint32_t n_pre = 0, n_suf = 0;
    for (int i = 0; i < n_targets; i++) {
        const Target *t = &targets[i];
        for (int k = 0; k < t->n_pre; k++) {
            n_pre++;
            if ((uint32_t)t->pre[k].len > m_pre_maxlen) m_pre_maxlen = (uint32_t)t->pre[k].len;
        }
        for (int k = 0; k < t->n_suf; k++) {
            n_suf++;
            if ((uint32_t)t->suf[k].len > m_suf_maxlen) m_suf_maxlen = (uint32_t)t->suf[k].len;
        }
    }
    m_npat = n_pre + n_suf;
    if (!m_npat) {
        m_words = 0;
        return;
    }
    m_words = (m_npat + 63) / 64;
    // EN: pattern ids: all prefixes first, then all suffixes; a pattern belongs to one target and group
    // RU: id шаблонов: сначала все префиксы, потом все окончания; шаблон принадлежит одной цели и группе
    m_pat_target = malloc((size_t)m_npat * sizeof *m_pat_target);
    m_pat_suf = malloc((size_t)m_npat);
    m_suf_len = calloc((size_t)(MAXPLEN + 1) * m_words, 8);
    m_pre_len = calloc((size_t)(MAXPLEN + 1) * m_words, 8);
    m_suf_all = calloc((size_t)m_words, 8);
    m_pre_all = calloc((size_t)m_words, 8);
    m_suf_tab = calloc((size_t)m_suf_maxlen * MATCH_CHARS * m_words, 8);
    m_pre_tab = calloc((size_t)m_pre_maxlen * MATCH_CHARS * m_words, 8);
    if (!m_pat_target || !m_pat_suf || !m_suf_len || !m_pre_len || !m_suf_all || !m_pre_all ||
        !m_suf_tab || !m_pre_tab)
        die(tr("out of memory", "не хватает памяти"));
    for (uint32_t g = 0; g < n_pre; g++) m_pre_all[g >> 6] |= 1ull << (g & 63);
    for (uint32_t g = n_pre; g < m_npat; g++) m_suf_all[g >> 6] |= 1ull << (g & 63);
    uint32_t g = 0;
    for (int pass = 0; pass < 2; pass++) {
        int is_suf = pass;
        for (int i = 0; i < n_targets; i++) {
            const Target *t = &targets[i];
            int n = is_suf ? t->n_suf : t->n_pre;
            const Pattern *pats = is_suf ? t->suf : t->pre;
            for (int k = 0; k < n; k++, g++) {
                const Pattern *p = &pats[k];
                m_pat_target[g] = (uint32_t)i;
                m_pat_suf[g] = (uint8_t)is_suf;
                uint64_t *lenm = is_suf ? m_suf_len : m_pre_len;
                lenm[(size_t)p->len * m_words + (g >> 6)] |= 1ull << (g & 63);
                for (int j = 0; j < p->len; j++) {
                    // EN: depth d of a suffix pattern reads set[len-1-d] (from the end); a prefix
                    // EN: pattern reads set[d] (from the start)
                    // RU: глубина d шаблона окончания читает set[len-1-d] (от конца); шаблон префикса
                    // RU: читает set[d] (от начала)
                    int si = is_suf ? p->len - 1 - j : j;
                    uint64_t *tab = is_suf ? m_suf_tab : m_pre_tab;
                    uint64_t *base = tab + (size_t)j * MATCH_CHARS * m_words;
                    for (int c = 0; c < 256; c++) {
                        if (!p->set[si][c]) continue;
                        uint64_t *cell = base + (size_t)m_char_idx[c] * m_words + (g >> 6);
                        *cell |= 1ull << (g & 63);
                    }
                }
            }
        }
    }
}

int targets_match_all(const char addr[49], uint8_t *matched) {
    if (!m_npat) { // EN: not built (or no targets): exact but slow per-target scan | RU: не построено (или целей нет): точный, но медленный перебор целей
        int any = 0;
        for (int i = 0; i < n_targets; i++)
            if (target_matches(&targets[i], addr)) {
                if (matched) matched[i] = 1;
                any = 1;
            }
        return any;
    }
    // EN: pattern/target ids are 32-bit on purpose: the arrays stay exact for any pattern count
    // EN: the tables can physically hold, with no silent truncation at 65535.
    // RU: id шаблонов/целей намеренно 32-битные: массивы точны при любом числе шаблонов,
    // RU: которое физически вмещают таблицы, без молчаливого усечения на 65535.
    uint32_t ids[MATCH_IDS];
    int n_ids = 0, overflow = 0;
    int full = match_side(m_suf_tab, m_suf_len, m_suf_all, m_suf_maxlen, addr, 1, ids, MATCH_IDS, &n_ids, &overflow);
    full |= match_side(m_pre_tab, m_pre_len, m_pre_all, m_pre_maxlen, addr, 0, ids, MATCH_IDS, &n_ids, &overflow);
    if (overflow) {
        // EN: pathologically many patterns matched one address: rerun the per-target scan
        // RU: аномально много шаблонов совпало с одним адресом: повторяем перебор целей
        int any = 0;
        for (int i = 0; i < n_targets; i++)
            if (target_matches(&targets[i], addr)) {
                if (matched) matched[i] = 1;
                any = 1;
            }
        return any;
    }
    if (!full) return 0;
    // EN: rare path: map matched patterns back to targets and apply the per-target group rule. A target
    // EN: counts only when its own AND/OR combination is satisfied, exactly like target_matches().
    // RU: редкий путь: возвращаем совпавшие шаблоны к целям и применяем правило групп. Цель засчитывается
    // RU: только при выполнении её комбинации AND/OR — ровно как в target_matches().
    uint32_t ut[MATCH_IDS];
    uint8_t uf[MATCH_IDS]; // EN: bit 0 = a prefix matched, bit 1 = a suffix matched | RU: бит 0 = совпал префикс, бит 1 = совпало окончание
    int nu = 0;
    for (int j = 0; j < n_ids; j++) {
        uint32_t tg = m_pat_target[ids[j]];
        int u = -1;
        for (int q = 0; q < nu; q++)
            if (ut[q] == tg) { u = q; break; }
        if (u < 0) {
            u = nu++;
            ut[u] = tg;
            uf[u] = 0;
        }
        uf[u] |= m_pat_suf[ids[j]] ? 2 : 1;
    }
    int any = 0;
    for (int q = 0; q < nu; q++) {
        const Target *t = &targets[ut[q]];
        int pre_hit = uf[q] & 1, suf_hit = uf[q] & 2;
        int m;
        if (t->match_or)
            m = (t->n_pre != 0 && pre_hit) || (t->n_suf != 0 && suf_hit);
        else
            m = (t->n_pre == 0 || pre_hit) && (t->n_suf == 0 || suf_hit);
        if (!m) continue;
        if (matched) matched[ut[q]] = 1;
        any = 1;
    }
    if (m_check) { // EN: GRAMVANITY_MATCHER_CHECK=1: die on any divergence from the per-target scan
                   // RU: GRAMVANITY_MATCHER_CHECK=1: аварийно останавливаться при любом расхождении с перебором целей
        // EN: cold debug path: plain heap allocation, no stack-size dependency (no VLA).
        // RU: холодный отладочный путь: обычное выделение из кучи, без зависимости от размера стека (без VLA).
        uint8_t *ref = malloc((size_t)n_targets);
        if (!ref) die(tr("out of memory", "не хватает памяти"));
        memset(ref, 0, (size_t)n_targets);
        int any2 = 0;
        for (int i = 0; i < n_targets; i++)
            if (target_matches(&targets[i], addr)) {
                ref[i] = 1;
                any2 = 1;
            }
        if (any2 != any || (matched && memcmp(ref, matched, (size_t)n_targets)))
            die(tr("internal error: fast target matcher diverged from the scan (address %.48s)",
                   "внутренняя ошибка: быстрый матчер целей разошёлся с перебором (адрес %.48s)"), addr);
        free(ref);
    }
    return any;
}

// EN: Expected number of phrases until a hit on one pattern alone.
// RU: Ожидаемое число фраз до хита по одному шаблону.
static double expected_pattern(const Pattern *p) {
    double e = 1;
    for (int k = 0; k < p->len; k++) {
        int c = 0;
        for (int b = 0; b < 256; b++) c += p->set[k][b];
        e *= 64.0 / c;
    }
    return e;
}

double target_probability(const Target *t) {
    double p_pre = 0, p_suf = 0;
    for (int k = 0; k < t->n_pre; k++) p_pre += 1 / expected_pattern(&t->pre[k]);
    for (int k = 0; k < t->n_suf; k++) p_suf += 1 / expected_pattern(&t->suf[k]);
    if (t->match_or) { // EN: one of the two groups suffices | RU: достаточно одной из двух групп
        if (t->n_pre && t->n_suf) return p_pre + p_suf - p_pre * p_suf;
        return p_pre + p_suf;
    }
    if (t->n_pre) {
        if (t->n_suf) return p_pre * p_suf;
        return p_pre;
    }
    if (t->n_suf) return p_suf;
    return 0;
}

double target_expected(const Target *t) {
    double p = target_probability(t);
    return p > 0 ? 1 / p : 1;
}
