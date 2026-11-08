// EN: options.h - the parsed command line (opt), the job folder paths and the usage text.
// RU: options.h - разобранная командная строка (opt), пути папки задания и текст использования.
#ifndef SV_OPTIONS_H
#define SV_OPTIONS_H

#include <stdint.h>

// EN: --checkpoint-merge input limit | RU: ограничение числа входов --checkpoint-merge
enum { CHECKPOINT_MERGE_MAX_IN = 64 };

typedef struct {
    int mode, bounceable, ignore_case, threads, verify, device, list_devices;
    // EN: performance limit, percent of the wall time the GPU and the CPU stay busy
    // RU: предел нагрузки, процент времени, в течение которого GPU и CPU заняты
    int power;
    // EN: tg-wallet testnet (0Q addresses, subwallet 0x7FFF7FFD)
    // RU: testnet tg-wallet (адреса 0Q, subwallet 0x7FFF7FFD)
    int testnet;
    char out_dir[4096], dump_file[4096];
    int out_given;
    double bench;
    int dump;
    // EN: deterministic search | RU: детерминированный поиск
    uint64_t start_offset, tries, stop_after, derive_index, reveal_index;
    char checkpoint[4096], resume[4096], derive_key[128], reveal_file[4096];
    int checkpoint_interval, start_given, tries_given, checkpoint_info, checkpoint_fix;
    // EN: --checkpoint-merge: the inputs are pointers into argv (valid for the program lifetime);
    // EN: the output job folder reuses out_dir/out_given
    // RU: --checkpoint-merge: входы — указатели в argv (живут до конца программы); папка задания
    // RU: для результата переиспользует out_dir/out_given
    int checkpoint_merge, n_merge_ins;
    const char *merge_ins[CHECKPOINT_MERGE_MAX_IN];
    int selftest_det;
    // EN: --random: fresh OS randomness for every candidate (not resumable)
    // RU: --random: свежая случайность ОС для каждого кандидата (не возобновляется)
    int random_mode;
    // EN: target defaults (options outside a --target block)
    // RU: значения целей по умолчанию (опции вне блока --target)
    uint64_t count;
    int count_given;
    int match_or;       // EN: --match or: one of the two pattern groups suffices (default: AND)
                        // RU: --match or: достаточно одной из двух групп шаблонов (по умолчанию: AND)
    int all_patterns;   // EN: --all-patterns: --count applies to every pattern of a target
                        // RU: --all-patterns: --count применяется к каждому шаблону цели
    int set_all_patterns;
    // EN: which search-defining options came from the command line (checked on --resume)
    // RU: какие опции, задающие поиск, пришли из командной строки (проверяется при --resume)
    int set_mode, set_suffix, set_prefix, set_form, set_case, set_net, set_match;
} Options;

extern Options opt;
extern char root_dir[4096];
extern char job_dir[4096];

// EN: Creates the folder that holds the hit files, the stop report and the default checkpoint. --out is
// EN: used exactly as given; otherwise results/job-<UTC timestamp>, made unique with a numeric suffix.
// RU: Создаёт папку для hit-файлов, stop-отчёта и checkpoint по умолчанию. --out используется ровно
// RU: как задан; иначе results/job-<UTC-метка>, уникализированная числовым суффиксом.
void job_dir_make(void);

void usage(int code) __attribute__((noreturn));

#endif
