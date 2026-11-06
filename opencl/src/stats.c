// EN: stats.c - search counters and locks (see stats.h).
// RU: stats.c - счётчики поиска и блокировки (см. stats.h).
#include "sv.h"
#include "pattern.h"
#include "stats.h"

int verified, skipped;
int hits_found;
int unverified_hint_printed;
int dumped;
FILE *dump_fp;
double total_done;
int wait_printed;
double session_start_done, session_start_time;
char hit_files[64][300];
int n_hit_files;

mutex_t hit_lock, stat_lock;

int target_done_locked(const Target *t) {
    if (!t->count_given) return 0;
    if (t->all_patterns) { // EN: --all-patterns: every pattern variant needs its own --count hits
                           // RU: --all-patterns: каждому варианту шаблона нужно своё число --count хитов
        for (int k = 0; k < t->n_pre; k++)
            if (t->pre_hits[k] < t->count) return 0;
        for (int k = 0; k < t->n_suf; k++)
            if (t->suf_hits[k] < t->count) return 0;
        return 1;
    }
    return (uint64_t)t->hits >= t->count;
}

int target_fills_pattern_locked(const Target *t, const char addr[49]) {
    if (!t->all_patterns || !t->count_given) return 1;
    for (int k = 0; k < t->n_pre; k++)
        if (pattern_matches(&t->pre[k], addr) && t->pre_hits[k] < t->count) return 1;
    for (int k = 0; k < t->n_suf; k++)
        if (pattern_matches(&t->suf[k], addr) && t->suf_hits[k] < t->count) return 1;
    return 0;
}

void target_count_pattern_hits_locked(Target *t, const char addr[49]) {
    for (int k = 0; k < t->n_pre; k++)
        if (pattern_matches(&t->pre[k], addr)) t->pre_hits[k]++;
    for (int k = 0; k < t->n_suf; k++)
        if (pattern_matches(&t->suf[k], addr)) t->suf_hits[k]++;
}

int all_targets_done(void) {
    int done = 1;
    mutex_lock(&stat_lock);
    for (int i = 0; i < n_targets; i++)
        if (!target_done_locked(&targets[i])) { done = 0; break; }
    mutex_unlock(&stat_lock);
    return n_targets > 0 && done;
}

double expected_tries_remaining(int *active_out) {
    int active = 0;
    double miss = 1;
    mutex_lock(&stat_lock);
    for (int i = 0; i < n_targets; i++) {
        if (target_done_locked(&targets[i])) continue;
        active++;
        miss *= 1 - target_probability(&targets[i]);
    }
    mutex_unlock(&stat_lock);
    if (active_out) *active_out = active;
    return miss < 1 ? 1 / (1 - miss) : 1;
}
