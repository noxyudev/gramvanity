// EN: pattern.h - target blocks and their patterns: parsing, compilation, matching and probability.
// RU: pattern.h - блоки --target и их шаблоны: разбор, компиляция, сопоставление и вероятность.
#ifndef SV_PATTERN_H
#define SV_PATTERN_H

#include <stddef.h>
#include <stdint.h>

enum { MAXPLEN = 10 };
enum { MAX_TARGET_NAME = 64 };
// EN: UTF-8 bytes of the name as the user typed it
// RU: UTF-8-байты имени в том виде, как его ввёл пользователь
enum { MAX_TARGET_LABEL = 128 };

typedef struct {
    uint8_t set[MAXPLEN][256]; // EN: set[k][c]: byte c allowed at position k of the pattern
                               // RU: set[k][c]: байт c допустим в позиции k шаблона
    int len;
    // EN: 1 = match right after the 2-char form tag (UQ/EQ/0Q/kQ), 0 = match at the end
    // RU: 1 = совпадение сразу после 2-символьного тега формы (UQ/EQ/0Q/kQ), 0 = в конце
    int prefix;
    char str[128];
} Pattern;

typedef struct {
    char **v;
    int n, cap;
} StrList;

typedef struct {
    char name[MAX_TARGET_NAME];       // EN: resolved ASCII id used in file names (t1, t2, ...)
                                      // RU: итоговый ASCII-идентификатор для имён файлов (t1, t2, ...)
    char label[MAX_TARGET_LABEL];     // EN: name from --target (UTF-8), shown in reports
                                      // RU: имя из --target (UTF-8), показывается в отчётах
    char given_name[MAX_TARGET_NAME]; // EN: transliterated id from --target, empty when unnamed
                                      // RU: транслитерированный id из --target, пусто без имени
    int name_given;                   // EN: --target had a name (label/given_name are set)
                                      // RU: у --target было имя (label/given_name заданы)
    StrList prefix, suffix;           // EN: patterns given inside the target block
                                      // RU: шаблоны, заданные внутри блока цели
    int has_prefix, has_suffix;       // EN: an own pattern list replaces the global default list
                                      // RU: свой список шаблонов заменяет глобальный список по умолчанию
    int count_given;
    uint64_t count;                   // EN: 0 with count_given = 0 means unlimited
                                      // RU: 0 при count_given = 0 означает без ограничения
    int ignore_case_given, ignore_case;
    // EN: --match or: one group (prefix or suffix) suffices
    // RU: --match or: достаточно одной группы (префикса или окончания)
    int match_given, match_or;
    // EN: --all-patterns: --count applies to every pattern
    // RU: --all-patterns: --count применяется к каждому шаблону
    int all_patterns_given, all_patterns;
    // EN: saved hits per compiled pattern (grown in target_compile)
    // RU: сохранённые хиты по скомпилированным шаблонам (растут в target_compile)
    uint64_t *pre_hits, *suf_hits;
    int pre_hits_cap, suf_hits_cap;
    int out_given;
    char out_dir[4096];               // EN: effective hit directory | RU: действующий каталог хитов
    // EN: checkpoints v2-v4: old patterns were alternatives, not AND
    // RU: checkpoint v2-v4: старые шаблоны были альтернативами, а не AND
    int legacy_or;
    Pattern *pre, *suf;
    int n_pre, n_suf;
    int hits;                         // EN: hits saved for this target | RU: сохранённые хиты этой цели
} Target;

// EN: grows on demand: the number of targets is limited only by memory
// RU: растёт по необходимости: число целей ограничено только памятью
extern Target *targets;
extern int n_targets;
extern int n_targets_cap;
// EN: at least one --target was given | RU: был задан хотя бы один --target
extern int targets_declared;
// EN: target block that receives target options, -1 = globals
// RU: блок цели, принимающий опции целей, -1 = глобальные
extern int cur_target;

// EN: Raw global pattern lists. They are compiled per target after all options are parsed, so that
// EN: --ignore-case works regardless of its position.
// RU: Исходные глобальные списки шаблонов. Они компилируются для каждой цели после разбора всех опций,
// RU: чтобы --ignore-case работал независимо от своей позиции.
extern StrList g_prefix, g_suffix;

void strlist_add(StrList *l, const char *s, size_t len);
// EN: One --prefix/--suffix value may hold several comma-separated patterns.
// RU: Одно значение --prefix/--suffix может содержать несколько шаблонов через запятую.
void strlist_add_csv(StrList *l, const char *s);
void target_add_patterns(Target *t, int prefix, const char *value);
Target *new_target(void);
// EN: Normalizes the --target name to UTF-8 (Windows may hand it over in CP1251).
// RU: Приводит имя --target к UTF-8 (Windows может передать его в CP1251).
void target_label_normalize(const char *in, char *out, size_t cap);
// EN: Builds the ASCII id used in file names. Cyrillic is transliterated (Паша -> Pasha); other
// EN: punctuation becomes a single underscore. The original label stays untouched for reports.
// RU: Строит ASCII-идентификатор для имён файлов. Кириллица транслитерируется (Паша -> Pasha), прочая
// RU: пунктуация заменяется одним подчёркиванием. Исходная метка остаётся нетронутой для отчётов.
void target_slug(const char *label, char *out, size_t cap);
const char *target_display(const Target *t, char *buf, size_t cap);
// EN: Compiles the effective prefix and suffix lists of a target. Own lists replace the global defaults
// EN: per group: a target with only --suffix uses the global prefixes and its own suffixes.
// RU: Компилирует действующие списки префиксов и окончаний цели. Свои списки заменяют глобальные
// RU: значения по умолчанию по группам: цель только с --suffix берёт глобальные префиксы и свои окончания.
void target_compile(Target *t);
// EN: Resolves target ids after parsing: unnamed targets become t1, t2, ... and duplicate ids get a
// EN: numeric suffix (A, A2, A3). The label keeps the name exactly as the user typed it.
// RU: Определяет идентификаторы целей после разбора: безымянные становятся t1, t2, ..., а дубликаты
// RU: получают числовой суффикс (A, A2, A3). Метка сохраняет имя ровно как его ввёл пользователь.
void target_names_resolve(void);
// EN: Applies the global target defaults (count, ignore-case, job folder) and compiles the patterns.
// EN: The job folder must already be final.
// RU: Применяет глобальные значения целей по умолчанию (count, ignore-case, папка задания) и компилирует
// RU: шаблоны. Папка задания должна быть уже окончательной.
void targets_prepare(void);
int pattern_matches(const Pattern *p, const char addr[49]);
// EN: A normal target needs one matching prefix (if it has prefixes) AND one matching suffix (if it has
// EN: suffixes). --match or applies the old checkpoint v2-v4 rule on purpose: one of the two groups
// EN: suffices.
// RU: Обычной цели нужен один подходящий префикс (если префиксы есть) И одно подходящее окончание
// RU: (если окончания есть). --match or намеренно применяет старое правило checkpoint v2-v4:
// RU: достаточно одной из двух групп.
int target_matches(const Target *t, const char addr[49]);
// EN: Builds the all-targets matching tables from the compiled targets. Call after targets_prepare()
// EN: or targets_from_checkpoint(), and again whenever the target set changes.
// RU: Строит таблицы сопоставления по всем целям из скомпилированных целей. Вызывать после
// RU: targets_prepare() или targets_from_checkpoint() и при каждой смене набора целей.
void targets_match_build(void);
// EN: Matches the address against every target at once (exact bitset NFA over the anchored patterns):
// EN: sets matched[i] = 1 for every matching target and returns 1 if at least one target matched.
// EN: matched may be NULL when only the boolean result is needed. Requires targets_match_build().
// RU: Сопоставляет адрес со всеми целями сразу (точный битсет-NFA по заякоренным шаблонам): ставит
// RU: matched[i] = 1 для каждой совпавшей цели и возвращает 1, если совпала хотя бы одна цель.
// RU: matched может быть NULL, если нужен только логический результат. Требует targets_match_build().
int targets_match_all(const char addr[49], uint8_t *matched);
// EN: Probability that one address matches this target.
// RU: Вероятность того, что один адрес совпадёт с этой целью.
double target_probability(const Target *t);
// EN: Expected number of phrases until a hit on this target alone.
// RU: Ожидаемое число фраз до хита только по этой цели.
double target_expected(const Target *t);

#endif
