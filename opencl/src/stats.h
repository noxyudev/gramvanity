// EN: stats.h - search counters and the locks that guard them, shared by the checkpoint serializer, the
// EN: hit writer, the pipelines and the stop report.
// RU: stats.h - счётчики поиска и защищающие их блокировки; их используют сериализатор checkpoint,
// RU: запись хитов, конвейеры и итоговый отчёт.
#ifndef SV_STATS_H
#define SV_STATS_H

#include <stdint.h>
#include <stdio.h>

#include "platform.h" // mutex_t
#include "pattern.h"  // Target

// EN: hit_lock serializes hit handling end to end (re-derivation, file writing, counters); stat_lock
// EN: guards the plain counters below. Nested use: handle_hit holds hit_lock and takes stat_lock inside.
// RU: hit_lock сериализует обработку хита целиком (повторный вывод, запись файла, счётчики);
// RU: stat_lock защищает обычные счётчики ниже. Вложенность: handle_hit держит hit_lock и внутри
// RU: берёт stat_lock.
extern mutex_t hit_lock, stat_lock;

extern int verified, skipped;
// EN: unique addresses saved (an address matching several targets counts once)
// RU: уникальные сохранённые адреса (адрес, совпавший с несколькими целями, считается один раз)
extern int hits_found;
extern int unverified_hint_printed;
extern int dumped;
extern FILE *dump_fp;
// EN: phrases fully derived and checked | RU: фразы, полностью выведенные и проверенные
extern double total_done;
// EN: the one-time wait estimate has been printed | RU: разовая оценка ожидания напечатана
extern int wait_printed;
extern double session_start_done, session_start_time;
// EN: hit files of this session, listed in the stop file | RU: hit-файлы сессии, перечисленные в stop-файле
extern char hit_files[64][300];
extern int n_hit_files;

// EN: Must be called with stat_lock held.
// RU: Вызывать только с захваченным stat_lock.
int target_done_locked(const Target *t);
// EN: Must be called with stat_lock held. With --all-patterns a hit is only saved when the address
// EN: fills at least one pattern that has not reached its --count yet; a match for an already full
// EN: variant alone would only produce a duplicate hit file while other variants are still missing.
// RU: Вызывать только с захваченным stat_lock. С --all-patterns хит сохраняется, только если адрес
// RU: закрывает хотя бы один шаблон, ещё не набравший свой --count; совпадение лишь с уже полным
// RU: вариантом дало бы дубликат hit-файла, тогда как других вариантов всё ещё не хватает.
int target_fills_pattern_locked(const Target *t, const char addr[49]);
// EN: Must be called with stat_lock held. Counts one saved hit toward every pattern of the target the
// EN: address matches. An address that satisfies several variants counts for all of them, so
// EN: overlapping patterns (for example identical ones under --ignore-case) finish together.
// RU: Вызывать только с захваченным stat_lock. Засчитывает один сохранённый хит каждому шаблону цели,
// RU: которому отвечает адрес. Адрес, подходящий под несколько вариантов, засчитывается всем, поэтому
// RU: пересекающиеся шаблоны (например одинаковые при --ignore-case) завершаются вместе.
void target_count_pattern_hits_locked(Target *t, const char addr[49]);

int all_targets_done(void);

// EN: Expected number of phrases until the first hit over the targets that have not reached their
// EN: --count yet. A finished target drops out of the odds, so the progress line keeps describing the
// EN: targets that are still being searched. *active_out (optional) receives that target count.
// RU: Ожидаемое число фраз до первого хита по целям, ещё не набравшим свой --count. Завершённая цель
// RU: выпадает из оценки, поэтому строка прогресса описывает цели, которые ещё ищутся.
// RU: *active_out (необязательный) получает число таких целей.
double expected_tries_remaining(int *active_out);

#endif
