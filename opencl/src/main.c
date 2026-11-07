// EN: gramvanity (OpenCL): finds seed phrases whose TON wallet addresses match target patterns.
// EN: Wallets:
// EN:   w5         W5 = v5r1 (Tonkeeper, My Wallet): the phrase is one BIP39 or TON mnemonic.
// EN:   tg-wallet  WalletTg (Telegram Wallet): the phrase is 24 words = two independent BIP39-12
// EN:              halves; words 1-12 ("anchor") define the address and are the key the contract is
// EN:              deployed with, words 13-24 ("signing") are the key Telegram Wallet signs with;
// EN:              wallets are activated (deploy + key rotation) with verify-go/w5addr activate.
// RU: gramvanity (OpenCL): ищет seed-фразы, адреса кошельков TON которых совпадают с шаблонами.
// RU: Кошельки:
// RU:   w5         W5 = v5r1 (Tonkeeper, My Wallet): фраза — одна мнемоника BIP39 или TON.
// RU:   tg-wallet  WalletTg (Telegram Wallet): фраза из 24 слов = две независимые половины BIP39-12;
// RU:              слова 1-12 ("якорь") задают адрес и являются ключом, с которым разворачивается
// RU:              контракт, слова 13-24 ("подпись") — ключ, которым подписывает Telegram Wallet;
// RU:              кошельки активируются (deploy + смена ключа) через verify-go/w5addr activate.
// EN:
// EN: Platforms: Linux and Windows with any OpenCL 1.2 GPU (NVIDIA, AMD, Intel).
// EN: GPU: PBKDF2-HMAC-SHA512 (+ SLIP-10 m/44'/607'/0' for BIP39) in kernel.cl, Ed25519 public keys
// EN:      in ed25519_gpu.cl (fixed-base scalar multiplication, ref10 field arithmetic).
// EN: CPU: wallet address and target check on all threads; every hit is derived again on the CPU
// EN:      (Monocypher + ed25519-donna) and both keys must match the GPU result.
// RU: Платформы: Linux и Windows с любым GPU OpenCL 1.2 (NVIDIA, AMD, Intel).
// RU: GPU: PBKDF2-HMAC-SHA512 (+ SLIP-10 m/44'/607'/0' для BIP39) в kernel.cl, публичные ключи
// RU:      Ed25519 в ed25519_gpu.cl (умножение на фиксированную базу, арифметика поля ref10).
// RU: CPU: адрес кошелька и проверка целей на всех потоках; каждый хит заново выводится на CPU
// RU:      (Monocypher + ed25519-donna), и оба ключа должны совпасть с результатом GPU.
// EN:
// EN: Targets: one run can search several target blocks. A target matches when one of its prefix
// EN: patterns (if any) matches right after the form tag AND one of its suffix patterns (if any)
// EN: matches at the end (--match or accepts a match in either group). Runs without --target have
// EN: one implicit target. --mode/--form/--net are global.
// RU: Цели: один запуск может искать несколько блоков целей. Цель совпадает, когда один из её
// RU: шаблонов префикса (если есть) совпадает сразу после тега формы И один из её шаблонов окончания
// RU: (если есть) совпадает в конце (--match or принимает совпадение в любой группе). Запуск без
// RU: --target имеет одну неявную цель. --mode/--form/--net глобальны.
// EN:
// EN: Two search modes:
// EN:   random (--random):       every candidate gets fresh entropy from the OS CSPRNG.
// EN:   deterministic (default): index -> entropy = HMAC-SHA512(searchKey, "svDET1"|mode|be64 index).
// EN:     The 32-byte searchKey comes from the OS CSPRNG and lives only in the checkpoint file
// EN:     (mode 600), never in argv or logs. Same key + same index = same candidate, so a search can
// EN:     be stopped (Ctrl+C, console close, crash) and resumed with --resume without gaps or lost
// EN:     work. In tg-wallet mode the signing half of a hit uses a separate entropy domain.
// RU: Два режима поиска:
// RU:   случайный (--random):             каждый кандидат получает свежую энтропию от CSPRNG ОС.
// RU:   детерминированный (по умолчанию): индекс -> энтропия = HMAC-SHA512(ключ, "svDET1"|режим|be64).
// RU:     32-байтовый ключ поиска берётся из CSPRNG ОС и живёт только в файле checkpoint (режим 600),
// RU:     никогда в argv или логах. Тот же ключ + индекс = тот же кандидат, поэтому поиск можно
// RU:     остановить (Ctrl+C, закрытие консоли, крэш) и продолжить через --resume без пропусков и
// RU:     потери работы. В режиме tg-wallet подписывающая половина хита использует отдельный домен.
// EN:
// EN: Results: every run gets one job folder (default ../results/job-<date>-<time>/) that holds the
// EN: hit files, the checkpoint and the stop report. A hit file is named
// EN: hit-<target>[-<target>...]-<index>.txt. Seed phrases are never printed to stdout; in the
// EN: default deterministic mode even the hit files store the search key + index instead of the
// EN: phrase (recover it with --derive/--reveal). Use --random to opt out of deterministic search;
// EN: such a run stores the phrase in its hit files and cannot resume.
// RU: Результаты: каждый запуск получает одну папку задания (по умолчанию ../results/job-<дата>-<время>/),
// RU: в которой лежат hit-файлы, checkpoint и stop-отчёт. Hit-файл называется
// RU: hit-<цель>[-<цель>...]-<индекс>.txt. Seed-фразы никогда не печатаются в stdout; в
// RU: детерминированном режиме по умолчанию даже hit-файлы хранят ключ поиска и индекс вместо фразы
// RU: (восстановить её можно через --derive/--reveal). --random отключает детерминированный поиск:
// RU: такой запуск хранит фразу в hit-файлах и не может быть продолжен.
// EN:
// EN: This file ties the modules together: option parsing and validation, the CPU-only utility modes
// EN: (--checkpoint-info, --checkpoint-fix, --reveal, --derive), the search setup and the batch loop.
// RU: Этот файл связывает модули: разбор и проверку параметров, служебные режимы только на CPU
// RU: (--checkpoint-info, --checkpoint-fix, --reveal, --derive), подготовку поиска и цикл пакетов.

#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "pattern.h"
#include "crypto.h"
#include "detsearch.h"
#include "addresses.h"
#include "gpu.h"
#include "stats.h"
#include "checkpoint.h"
#include "hits.h"
#include "search.h"
#include "selftest.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/monocypher/monocypher.h"

// EN: Power limit: after `busy` seconds of work, sleep so that the GPU and the CPU stay busy only
// EN: opt.power percent of the wall time. The caller leaves the GPU queue empty (it waits for its
// EN: batches), so the pause really idles both. Long pauses are cut into 100 ms slices to keep
// EN: Ctrl+C responsive.
// RU: Ограничение мощности: после `busy` секунд работы спим так, чтобы GPU и CPU были заняты только
// RU: opt.power процентов времени. Вызывающий оставляет очередь GPU пустой (он ждёт свои пакеты),
// RU: поэтому пауза действительно простаивает обоими. Длинные паузы нарезаны на срезы по 100 мс,
// RU: чтобы Ctrl+C оставался отзывчивым.
static void power_pause(double busy) {
    if (opt.power >= 100 || busy <= 0) return;
    double idle = busy * (100.0 - (double)opt.power) / (double)opt.power;
    while (idle > 0 && !g_stop) {
        double s = idle > 0.1 ? 0.1 : idle;
        sleep_ms((int)(s * 1000.0 + 0.5));
        idle -= s;
    }
}

// ---------------------------------------------------------------- command line

// EN: Pre-scan for --lang so that even the messages of option validation and --help use the
// EN: language the user asked for, wherever --lang appears on the line.
// RU: Предварительный поиск --lang, чтобы даже сообщения проверки параметров и --help использовали
// RU: запрошенный язык, где бы --lang ни стоял в командной строке.
static void lang_prescan(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--lang") && i + 1 < argc) lang_set(argv[++i]);
        else if (!strncmp(argv[i], "--lang=", 7)) lang_set(argv[i] + 7);
    }
}

// EN: Parses argv into opt and the target/pattern tables. Every option keeps its exact CLI position
// EN: semantics: target options inside a --target block modify that target, outside it they set the
// EN: global defaults (which targets_prepare() applies to every target without own values).
// RU: Разбирает argv в opt и таблицы целей/шаблонов. Каждый параметр сохраняет свою позиционную
// RU: семантику: параметры внутри блока --target меняют эту цель, вне его задают глобальные значения
// RU: по умолчанию (их targets_prepare() применяет ко всем целям без собственных значений).
static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : \
    (die(tr("missing value for %s", "не указано значение для %s"), a), ""))
        if (!strcmp(a, "--target")) {
            Target *t = new_target();
            targets_declared = 1;
            cur_target = n_targets - 1;
            if (i + 1 < argc && argv[i + 1][0] && argv[i + 1][0] != '-') {
                const char *name = argv[++i];
                target_label_normalize(name, t->label, sizeof t->label);
                target_slug(t->label, t->given_name, sizeof t->given_name);
                t->name_given = 1;
            }
        } else if (!strcmp(a, "--suffix")) {
            if (cur_target >= 0) target_add_patterns(&targets[cur_target], 0, NEXT());
            else { strlist_add_csv(&g_suffix, NEXT()); opt.set_suffix = 1; }
        } else if (!strcmp(a, "--prefix")) {
            if (cur_target >= 0) target_add_patterns(&targets[cur_target], 1, NEXT());
            else { strlist_add_csv(&g_prefix, NEXT()); opt.set_prefix = 1; }
        } else if (!strcmp(a, "--mode")) {
            const char *m = NEXT();
            if (!strcmp(m, "bip39-12")) opt.mode = BIP12;
            else if (!strcmp(m, "bip39-24")) opt.mode = BIP24;
            else if (!strcmp(m, "ton-24")) opt.mode = TON24;
            else if (!strcmp(m, "tg-wallet")) opt.mode = TG;
            else die(tr("mode: bip39-12 | bip39-24 | ton-24 | tg-wallet",
                        "режим: bip39-12 | bip39-24 | ton-24 | tg-wallet"));
            opt.set_mode = 1;
        } else if (!strcmp(a, "--net")) {
            const char *v = NEXT();
            if (!strcmp(v, "mainnet")) opt.testnet = 0;
            else if (!strcmp(v, "testnet")) opt.testnet = 1;
            else die(tr("net: mainnet | testnet", "сеть: mainnet | testnet"));
            opt.set_net = 1;
        } else if (!strcmp(a, "--form")) {
            const char *f = NEXT();
            if (!strcmp(f, "uq")) opt.bounceable = 0;
            else if (!strcmp(f, "eq")) opt.bounceable = 1;
            else die(tr("form: uq | eq", "форма: uq | eq"));
            opt.set_form = 1;
        } else if (!strcmp(a, "--lang")) {
            const char *l = NEXT();
            if (!lang_set(l))
                die(tr("unsupported language '%s' (expected en or ru)",
                       "неподдерживаемый язык '%s' (ожидается en или ru)"), l);
        } else if (!strncmp(a, "--lang=", 7)) {
            if (!lang_set(a + 7))
                die(tr("unsupported language '%s' (expected en or ru)",
                       "неподдерживаемый язык '%s' (ожидается en или ru)"), a + 7);
        } else if (!strcmp(a, "--ignore-case")) {
            if (cur_target >= 0) {
                targets[cur_target].ignore_case_given = 1;
                targets[cur_target].ignore_case = 1;
            } else {
                opt.ignore_case = 1;
                opt.set_case = 1;
            }
        } else if (!strcmp(a, "--match")) {
            const char *m = NEXT();
            int orv;
            if (!strcmp(m, "and")) orv = 0;
            else if (!strcmp(m, "or")) orv = 1;
            else die(tr("match: and | or", "сопоставление: and | or"));
            if (cur_target >= 0) {
                targets[cur_target].match_given = 1;
                targets[cur_target].match_or = orv;
            } else {
                opt.match_or = orv;
                opt.set_match = 1;
            }
        } else if (!strcmp(a, "--threads")) opt.threads = atoi(NEXT());
        else if (!strcmp(a, "--all-patterns")) {
            if (cur_target >= 0) {
                targets[cur_target].all_patterns_given = 1;
                targets[cur_target].all_patterns = 1;
            } else {
                opt.all_patterns = 1;
                opt.set_all_patterns = 1;
            }
        }
        else if (!strcmp(a, "--power")) opt.power = atoi(NEXT());
        else if (!strcmp(a, "--count")) {
            uint64_t nv;
            if (parse_u64(NEXT(), &nv) || !nv)
                die(tr("--count must be a positive number", "--count должен быть положительным числом"));
            if (cur_target >= 0) {
                targets[cur_target].count_given = 1;
                targets[cur_target].count = nv;
            } else {
                opt.count_given = 1;
                opt.count = nv;
            }
        } else if (!strcmp(a, "--out")) {
            const char *d = NEXT();
            if (cur_target >= 0) {
                snprintf(targets[cur_target].out_dir, sizeof targets[cur_target].out_dir, "%s", d);
                targets[cur_target].out_given = 1;
            } else {
                snprintf(opt.out_dir, sizeof opt.out_dir, "%s", d);
                opt.out_given = 1;
            }
        } else if (!strcmp(a, "--bench")) opt.bench = atof(NEXT());
        else if (!strcmp(a, "--dump")) { opt.dump = atoi(NEXT()); snprintf(opt.dump_file, sizeof opt.dump_file, "%s", NEXT()); }
        else if (!strcmp(a, "--no-verify")) opt.verify = 0;
        else if (!strcmp(a, "--device")) opt.device = atoi(NEXT());
        else if (!strcmp(a, "--list-devices")) opt.list_devices = 1;
        else if (!strcmp(a, "--start-offset")) {
            if (parse_u64(NEXT(), &opt.start_offset))
                die(tr("--start-offset must be a number in [0, 2^64-1]",
                       "--start-offset должен быть числом в диапазоне [0, 2^64-1]"));
            opt.start_given = 1;
        } else if (!strcmp(a, "--tries")) {
            if (parse_u64(NEXT(), &opt.tries) || !opt.tries)
                die(tr("--tries must be a positive number", "--tries должен быть положительным числом"));
            opt.tries_given = 1;
        } else if (!strcmp(a, "--checkpoint")) snprintf(opt.checkpoint, sizeof opt.checkpoint, "%s", NEXT());
        else if (!strcmp(a, "--resume")) snprintf(opt.resume, sizeof opt.resume, "%s", NEXT());
        else if (!strcmp(a, "--checkpoint-interval")) {
            opt.checkpoint_interval = atoi(NEXT());
            if (opt.checkpoint_interval < 1) opt.checkpoint_interval = 1;
        } else if (!strcmp(a, "--stop-after")) {
            if (parse_u64(NEXT(), &opt.stop_after) || !opt.stop_after)
                die(tr("--stop-after must be a positive number", "--stop-after должен быть положительным числом"));
        } else if (!strcmp(a, "--random")) opt.random_mode = 1;
        else if (!strcmp(a, "--derive")) {
            snprintf(opt.derive_key, sizeof opt.derive_key, "%s", NEXT());
            if (parse_u64(NEXT(), &opt.derive_index))
                die(tr("--derive index must be a number", "индекс --derive должен быть числом"));
        } else if (!strcmp(a, "--reveal")) {
            snprintf(opt.reveal_file, sizeof opt.reveal_file, "%s", NEXT());
            if (parse_u64(NEXT(), &opt.reveal_index))
                die(tr("--reveal index must be a number", "индекс --reveal должен быть числом"));
        } else if (!strcmp(a, "--checkpoint-info")) { opt.checkpoint_info = 1; snprintf(opt.checkpoint, sizeof opt.checkpoint, "%s", NEXT()); }
        else if (!strcmp(a, "--checkpoint-fix")) { opt.checkpoint_fix = 1; snprintf(opt.checkpoint, sizeof opt.checkpoint, "%s", NEXT()); }
        else if (!strcmp(a, "--selftest-det")) opt.selftest_det = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) usage(0);
        else usage(1);
#undef NEXT
    }
}

// EN: Cross-option validation after parsing (before any state is built).
// RU: Проверка сочетаний параметров после разбора (до создания какого-либо состояния).
static void validate_args(void) {
    if (opt.threads < 1) opt.threads = 1;
    if (opt.power < 1 || opt.power > 100)
        die(tr("--power: expected 1..100 (percent, 100 = no limit)",
               "--power: ожидается 1..100 (проценты, 100 = без ограничения)"));
    if (opt.random_mode && (opt.start_given || opt.tries_given || opt.checkpoint[0] || opt.resume[0]))
        die(tr("--random cannot be combined with --start-offset, --tries, --checkpoint or --resume",
               "--random нельзя сочетать с --start-offset, --tries, --checkpoint или --resume"));
    if (opt.random_mode && opt.stop_after)
        die(tr("--random cannot be combined with --stop-after (it counts deterministic indices)",
               "--random нельзя сочетать с --stop-after (он считает детерминированные индексы)"));
}

// ---------------------------------------------------------------- CPU-only utility modes

// EN: --checkpoint-info: dump of a checkpoint file (without the secret key). The field labels stay
// EN: English on purpose: this output is machine-readable and is parsed by tests and scripts.
// RU: --checkpoint-info: дамп файла checkpoint (без секретного ключа). Названия полей намеренно
// RU: остаются английскими: этот вывод машиночитаем, его разбирают тесты и скрипты.
static int cmd_checkpoint_info(void) {
    struct Checkpoint in;
    char err[256];
    if (checkpoint_load(opt.checkpoint, &in, err, sizeof err)) die("%s: %s", opt.checkpoint, err);
    printf("checkpoint: %s\n  job folder: %s\n  mode %s, form %s%s\n", opt.checkpoint,
           in.job_dir[0] ? in.job_dir : "(not stored)", MODE_NAME[in.mode],
           (in.flags & 2) ? ((in.flags & 1) ? "kQ" : "0Q") : ((in.flags & 1) ? "EQ" : "UQ"),
           in.mode == TG ? ((in.flags & 2) ? ", testnet" : ", mainnet") : "");
    printf("  targets (%u):\n", in.n_targets);
    for (uint32_t t = 0; t < in.n_targets; t++) {
        const CheckpointTarget *ct = &in.targets[t];
        printf("    %s: %llu hit(s), %s\n", ct->name, (unsigned long long)ct->hits,
               (ct->flags & 2) ? "count reached/limited" : "no count limit");
        if (ct->label[0] && strcmp(ct->label, ct->name)) printf("      label: %s\n", ct->label);
        if (ct->flags & 2) printf("      count: %llu\n", (unsigned long long)ct->count);
        if (ct->out[0]) printf("      out: %s\n", ct->out);
        if (ct->flags & 1) printf("      ignore case\n");
        if (ct->flags & 4) printf("      prefix OR suffix (--match or)\n");
        if (ct->flags & 8) printf("      --all-patterns (--count per pattern)\n");
        if (ct->n_prefix) {
            printf("      prefixes:");
            const char *isep = "";
            for (uint32_t k = 0; k < ct->n_prefix; k++) {
                char h[32] = "";
                if (ct->pre_hits) snprintf(h, sizeof h, " (%llu hit(s))", (unsigned long long)ct->pre_hits[k]);
                printf("%s \"%s\"%s", isep, ct->prefix[k], h);
                isep = ",";
            }
            printf("\n");
        }
        if (ct->n_suffix) {
            printf("      endings:");
            const char *isep = "";
            for (uint32_t k = 0; k < ct->n_suffix; k++) {
                char h[32] = "";
                if (ct->suf_hits) snprintf(h, sizeof h, " (%llu hit(s))", (unsigned long long)ct->suf_hits[k]);
                printf("%s \"%s\"%s", isep, ct->suffix[k], h);
                isep = ",";
            }
            printf("\n");
        }
    }
    printf("  start index %llu, next index %llu (resume point), stage-1 frontier %llu\n",
           (unsigned long long)in.start, (unsigned long long)in.next, (unsigned long long)in.s1_next);
    if (in.end) printf("  range end: %llu (exclusive)\n", (unsigned long long)in.end);
    else printf("  range end: unlimited\n");
    if (in.mode == TG)
        printf("  wallet: WalletTg (Telegram Wallet) %s subwallet 0x%08X\n",
               (in.flags & 2) ? "testnet" : "mainnet",
               (in.flags & 2) ? TG_SUBWALLET_TESTNET : TG_SUBWALLET_MAINNET);
    printf("  candidates done %llu, unique hits %llu, verified hits %llu\n",
           (unsigned long long)in.done, (unsigned long long)in.unique_hits,
           (unsigned long long)in.verified);
    checkpoint_free(&in);
    return 0;
}

// EN: --checkpoint-fix FILE: validate a hand-edited checkpoint (every field check, but not the
// EN: checksum) and rewrite it with a fresh checksum, normalized quoting, comments and line endings.
// RU: --checkpoint-fix FILE: проверить отредактированный вручную checkpoint (все проверки полей, кроме
// RU: контрольной суммы) и перезаписать его с новой суммой, нормализованными кавычками, комментариями
// RU: и концами строк.
static int cmd_checkpoint_fix(void) {
    struct Checkpoint in;
    char err[256];
    if (checkpoint_fix(opt.checkpoint, &in, err, sizeof err)) die("%s: %s", opt.checkpoint, err);
    printf("checkpoint fixed: %s\n", opt.checkpoint);
    printf("  mode %s, %u target(s), next index %llu, %llu unique hit(s)\n",
           MODE_NAME[in.mode], in.n_targets, (unsigned long long)in.next,
           (unsigned long long)in.unique_hits);
    checkpoint_free(&in);
    return 0;
}

// EN: One --derive/--reveal result block (phrase, keys, addresses, replay hints). The two utility
// EN: modes share this printing; only their header lines and the TON validity note differ. The field
// EN: labels stay English: users copy this block and the tests parse it.
// RU: Один блок результата --derive/--reveal (фраза, ключи, адреса, подсказки для повтора). Оба
// RU: служебных режима используют эту печать; различаются только строки заголовка и примечание о
// RU: проверке TON. Названия полей остаются английскими: пользователи копируют этот блок, а тесты
// RU: его разбирают.
static void print_derived(const Derived *d) {
    char uq[49], eq[49], ph[65], sph[65];
    friendly(d->id, 0, uq);
    friendly(d->id, 1, eq);
    hexs(d->pub, 32, ph);
    printf("phrase (SECRET, do not share): %s\n", d->phrase);
    printf("public key: %s\naddress %s: %s\naddress %s: %s\n", ph,
           opt.testnet ? "0Q" : "UQ", uq, opt.testnet ? "kQ" : "EQ", eq);
    if (opt.mode == TG) {
        hexs(d->spub, 32, sph);
        printf("signing public key (words 13-24): %s\n", sph);
        printf(tr("note: words 1-12 (anchor) define the address, words 13-24 are the key Telegram Wallet signs with;\n"
                  "activate the wallet before importing it: " W5ADDR_CLI " activate --phrase \"<the 24 words>\"%s --send\n",
                  "примечание: слова 1-12 (якорь) задают адрес, слова 13-24 — ключ, которым подписывает\n"
                  "Telegram Wallet; активируйте кошелёк перед импортом: " W5ADDR_CLI " activate --phrase \"<24 слова>\"%s --send\n"),
               opt.testnet ? " --net testnet" : "");
    }
}

// EN: --reveal CHECKPOINT INDEX: derive and print the candidate stored at an index of a checkpoint.
// RU: --reveal CHECKPOINT INDEX: вывести и напечатать кандидата, сохранённого под индексом checkpoint.
static int cmd_reveal(void) {
    struct Checkpoint in;
    char err[256];
    if (checkpoint_load(opt.reveal_file, &in, err, sizeof err)) die("%s: %s", opt.reveal_file, err);
    if (opt.set_net && (!!(in.flags & 2)) != !!opt.testnet)
        die(tr("--reveal: --net conflicts with the checkpoint", "--reveal: --net конфликтует с checkpoint"));
    opt.mode = (int)in.mode;
    opt.testnet = (in.flags & 2) ? 1 : 0;
    Derived d;
    derive_candidate(in.key, opt.mode, opt.reveal_index, &d);
    printf("mode: %s, index %llu of checkpoint %s\n", MODE_NAME[opt.mode], (unsigned long long)opt.reveal_index, opt.reveal_file);
    print_derived(&d);
    if (opt.mode == TON24)
        printf(tr("note: indices whose phrase fails the TON basic-seed check were skipped by the search; "
                  "this one %s the check\n",
                  "примечание: индексы, чья фраза не проходит проверку basic-seed TON, поиском пропускались; "
                  "этот её %s\n"),
               ton_valid(d.phrase, d.phrase_len) ? tr("passes", "проходит") : tr("does NOT pass", "НЕ проходит"));
    crypto_wipe(&d, sizeof d);
    checkpoint_free(&in);
    return 0;
}

// EN: --derive KEYHEX INDEX: derive and print the candidate for an explicit search key.
// RU: --derive KEYHEX INDEX: вывести и напечатать кандидата для явно заданного ключа поиска.
static int cmd_derive(void) {
    if (opt.testnet && opt.mode != TG)
        die(tr("--net testnet is only supported for --mode tg-wallet",
               "--net testnet поддерживается только с --mode tg-wallet"));
    uint8_t key[32];
    if (strlen(opt.derive_key) != 64)
        die(tr("--derive KEYHEX must be 64 hex characters",
               "--derive KEYHEX должен состоять из 64 шестнадцатеричных символов"));
    for (int i = 0; i < 32; i++) {
        char hex[3] = {opt.derive_key[2 * i], opt.derive_key[2 * i + 1], 0};
        char *e;
        unsigned long v = strtoul(hex, &e, 16);
        if (*e || e == hex) die(tr("--derive KEYHEX must be hex", "--derive KEYHEX должен быть шестнадцатеричным"));
        key[i] = (uint8_t)v;
    }
    Derived d;
    derive_candidate(key, opt.mode, opt.derive_index, &d);
    printf("mode: %s, index %llu\n", MODE_NAME[opt.mode], (unsigned long long)opt.derive_index);
    print_derived(&d);
    crypto_wipe(&d, sizeof d);
    crypto_wipe(key, sizeof key);
    return 0;
}

// ---------------------------------------------------------------- search setup

// EN: Builds the target table, the job folder and the deterministic state (fresh or from --resume).
// RU: Собирает таблицу целей, папку задания и детерминированное состояние (новое или из --resume).
static void setup_search(int real_search) {
    if (real_search && !opt.resume[0] && n_targets == 0 && g_prefix.n == 0 && g_suffix.n == 0)
        die(tr("--suffix or --prefix is required (see --help)",
               "нужен --suffix или --prefix (см. --help)"));

    if (opt.resume[0]) {
        struct Checkpoint in;
        char err[256];
        if (checkpoint_load(opt.resume, &in, err, sizeof err)) die("%s: %s", opt.resume, err);
        if (opt.set_mode && (int)in.mode != opt.mode)
            die(tr("--resume: --mode %s conflicts with the checkpoint (%s)",
                   "--resume: --mode %s конфликтует с checkpoint (%s)"),
                MODE_NAME[opt.mode], MODE_NAME[in.mode]);
        if (opt.set_form && ((in.flags & 1) != 0) != !!opt.bounceable)
            die(tr("--resume: --form conflicts with the checkpoint",
                   "--resume: --form конфликтует с checkpoint"));
        if (opt.set_net && ((in.flags & 2) != 0) != !!opt.testnet)
            die(tr("--resume: --net conflicts with the checkpoint",
                   "--resume: --net конфликтует с checkpoint"));
        opt.mode = (int)in.mode;
        opt.bounceable = (in.flags & 1) != 0;
        opt.testnet = (in.flags & 2) != 0;
        if (opt.mode != TON24 && in.s1_next != in.next)
            die(tr("%s: inconsistent checkpoint (stage cursors differ in a single-stage mode)",
                   "%s: несогласованный checkpoint (курсоры стадий различаются в одностадийном режиме)"),
                opt.resume);
        if (opt.start_given && opt.start_offset != in.start)
            die(tr("--resume: --start-offset %llu differs from the checkpoint (%llu)",
                   "--resume: --start-offset %llu отличается от checkpoint (%llu)"),
                (unsigned long long)opt.start_offset, (unsigned long long)in.start);
        job_dir_from_checkpoint(&in, opt.resume);
        resume_apply_target_overrides(&in);
        targets_from_checkpoint(&in);
        if (!real_search) n_targets = 0;
        mkdir_p(job_dir);
        det.enabled = 1;
        memcpy(det.key, in.key, 32);
        det.start = in.start;
        det.next = in.next;
        det.s1_next = in.s1_next;
        det.cursor = in.s1_next;
        det.end = in.end;
        det.has_end = in.end != 0;
        if (opt.mode == TON24 && in.s1_next > in.next) { // EN: survivors of an interrupted run regenerate deterministically
            det.backfill_base = in.next;                 // RU: выжившие прерванного запуска восстанавливаются детерминированно
            det.backfill_end = in.s1_next;
        }
        snprintf(det.checkpoint_path, sizeof det.checkpoint_path, "%s", opt.checkpoint[0] ? opt.checkpoint : opt.resume);
        total_done = (double)in.done;
        hits_found = (int)in.unique_hits;
        verified = (int)in.verified;
        checkpoint_free(&in);
        // EN: --tries on resume re-scopes the remaining range: end = next + tries
        // RU: --tries при resume задаёт остаток диапазона: end = next + tries
        if (opt.tries_given) {
            uint64_t e;
            if (add_ovf_u64(det.next, opt.tries, &e))
                die(tr("--resume: next index + --tries overflows 2^64",
                       "--resume: следующий индекс + --tries переполняет 2^64"));
            det.end = e;
            det.has_end = 1;
        }
        printf(tr("resuming %s search from index %llu (started at %llu, end %s)\n",
                  "продолжаем поиск %s с индекса %llu (начало %llu, конец %s)\n"),
               MODE_NAME[opt.mode], (unsigned long long)det.next, (unsigned long long)det.start,
               det.has_end ? tr("at the value below", "см. значение ниже") : tr("unlimited", "без ограничения"));
    } else {
        if (real_search) {
            job_dir_make();
            if (n_targets == 0) new_target(); // EN: the implicit single target of a plain --suffix/--prefix run
                                              // RU: неявная единственная цель запуска с одним --suffix/--prefix
            targets_prepare();
        } else {
            n_targets = 0; // EN: bench/dump/list-devices: no hit handling
                           // RU: bench/dump/list-devices: обработка хитов не нужна
        }
        // EN: Deterministic, resumable search is the default. --random keeps the old behavior: fresh
        // EN: OS randomness for every candidate, no search key and no checkpoint.
        // RU: Детерминированный возобновляемый поиск — по умолчанию. --random сохраняет старое
        // RU: поведение: свежая случайность ОС для каждого кандидата, без ключа поиска и checkpoint.
        if (!opt.random_mode) {
            det.enabled = 1;
            random_bytes(det.key, 32); // EN: the secret search key: OS CSPRNG, stored only in the checkpoint
                                       // RU: секретный ключ поиска: CSPRNG ОС, хранится только в checkpoint
            det.start = opt.start_offset;
            det.next = det.s1_next = det.cursor = opt.start_offset;
            if (opt.tries_given) {
                uint64_t e;
                if (add_ovf_u64(det.start, opt.tries, &e))
                    die(tr("--start-offset + --tries overflows 2^64",
                           "--start-offset + --tries переполняет 2^64"));
                det.end = e;
                det.has_end = 1;
            }
            if (opt.checkpoint[0]) snprintf(det.checkpoint_path, sizeof det.checkpoint_path, "%s", opt.checkpoint);
            else if (real_search) snprintf(det.checkpoint_path, sizeof det.checkpoint_path, "%s%cjob.checkpoint", job_dir, PATH_SEP);
        }
    }
    // EN: targets are final here (parsed, or restored from the checkpoint): build the fast all-targets
    // EN: matching tables used by finish()
    // RU: цели здесь окончательны (разобраны или восстановлены из checkpoint): строим быстрые таблицы
    // RU: сопоставления по всем целям для finish()
    targets_match_build();

    if (opt.testnet && opt.mode != TG)
        die(tr("--net testnet is only supported for --mode tg-wallet",
               "--net testnet поддерживается только с --mode tg-wallet"));

    // EN: A resumed checkpoint may already hold every requested hit. Without this check every new hit
    // EN: would be dropped and the search would never stop. Unlimited targets keep the job running.
    // RU: Возобновлённый checkpoint может уже содержать все запрошенные хиты. Без этой проверки
    // RU: каждый новый хит отбрасывался бы и поиск никогда не остановился. Цели без лимита
    // RU: продолжают задание.
    if (real_search && opt.resume[0] && all_targets_done()) {
        printf(tr("nothing to do: every target has reached its --count; raise one with\n"
                  "  ./gramvanity --resume \"%s\" [--target NAME] --count N\n",
                  "делать нечего: все цели достигли своего --count; увеличьте его:\n"
                  "  ./gramvanity --resume \"%s\" [--target ИМЯ] --count N\n"), opt.resume);
        exit(0);
    }

    if (det.enabled && det.has_end && det.next >= det.end)
        die(tr("nothing to do: the checkpoint's range is already complete (next index %llu, end %llu); "
               "extend it with --resume FILE --tries N or start a new job",
               "делать нечего: диапазон checkpoint уже завершён (следующий индекс %llu, конец %llu); "
               "расширьте его через --resume FILE --tries N или начните новое задание"),
            (unsigned long long)det.next, (unsigned long long)det.end);
}

// ---------------------------------------------------------------- startup output

// EN: GPU setup and the startup self-test that gates every run.
// RU: Настройка GPU и стартовый self-test, который проверяется перед каждым запуском.
static void init_gpu_and_selftest(void) {
    gpu_init();
    make_mkey();
    double t0 = now_sec();
    self_test();
    printf(tr("GPU %s; self-test OK (GPU PBKDF2, SLIP-10 and Ed25519 == CPU, W5 v5r1 and WalletTg addresses == @ton/ton reference vectors) in %.1f s\n",
              "GPU %s; self-test пройден (GPU PBKDF2, SLIP-10 и Ed25519 == CPU, адреса W5 v5r1 и WalletTg == эталонные векторы @ton/ton) за %.1f с\n"),
           gpu.name, now_sec() - t0);
}

// EN: Startup banner: targets, odds, the deterministic range and the first checkpoint save.
// RU: Стартовый баннер: цели, шансы, детерминированный диапазон и первое сохранение checkpoint.
static void print_banner(int real_search, double expected, int expected_active) {
    if (opt.dump) {
        dump_fp = fopen(opt.dump_file, "w");
        if (!dump_fp) die(tr("cannot open %s", "не удалось открыть %s"), opt.dump_file);
        return;
    }
    if (opt.bench) return;
    printf(tr("search in %s form, mode %s, %s, %d CPU threads, power %d%%\n",
              "поиск в форме %s, режим %s, %s, потоков CPU: %d, мощность %d%%\n"),
           form_name(), MODE_NAME[opt.mode], wallet_desc(), opt.threads, opt.power);
    printf(tr("targets (%d):\n", "цели (%d):\n"), n_targets);
    for (int i = 0; i < n_targets; i++) {
        const Target *t = &targets[i];
        char desc[8192], disp[MAX_TARGET_LABEL + MAX_TARGET_NAME + 4];
        target_desc(t, desc, sizeof desc, 0);
        target_display(t, disp, sizeof disp);
        printf(tr("  %s: %s%s; %s; out %s\n", "  %s: %s%s; %s; папка %s\n"), disp, desc,
               t->ignore_case ? tr(", ignore case", ", без учёта регистра") : "",
               t->count_given ? tr("count limit set", "счётчик задан") : tr("no count limit", "без счётчика"),
               t->out_dir);
        if (t->count_given)
            printf(tr("    stop after %llu hit(s)%s\n", "    остановить после %llu хит(ов)%s\n"),
                   (unsigned long long)t->count,
                   t->all_patterns ? tr(" per pattern (--all-patterns)", " на шаблон (--all-patterns)") : "");
    }
    printf(tr("job folder: %s (hit files%s; phrases are never printed)\n",
              "папка задания: %s (hit-файлы%s; фразы никогда не печатаются)\n"), job_dir,
           det.enabled ? tr(", stop report and checkpoint", ", stop-отчёт и checkpoint")
                       : tr(" and stop report", " и stop-отчёт"));
    if (det.enabled) {
        printf(tr("deterministic range: [%llu, %s), checkpoint %s, autosave every %d s; stop with Ctrl+C and resume with --resume\n",
                  "детерминированный диапазон: [%llu, %s), checkpoint %s, автосохранение каждые %d с; остановить Ctrl+C, продолжить через --resume\n"),
               (unsigned long long)det.start, det.has_end ? tr("end below", "конец ниже") : tr("unlimited", "без ограничения"),
               det.checkpoint_path, opt.checkpoint_interval);
        if (det.has_end)
            printf(tr("range end (exclusive): %llu (%.4g candidates to do)\n",
                      "конец диапазона (не включая): %llu (осталось кандидатов: %.4g)\n"),
                   (unsigned long long)det.end, (double)(det.end - det.next));
    } else if (real_search) {
        printf(tr("random mode: fresh OS randomness, no checkpoint; this search cannot be resumed (--random)\n",
                  "случайный режим: свежая случайность ОС, без checkpoint; этот поиск нельзя продолжить (--random)\n"));
    }
    if (n_targets > 0 && expected_active > 0) {
        if (expected_active == n_targets)
            printf(tr("expected ~%.3g phrases until the first hit across all targets\n",
                      "ожидается ~%.3g фраз до первого хита по всем целям\n"), expected);
        else
            printf(tr("expected ~%.3g phrases until the first hit across the %d remaining target(s)\n",
                      "ожидается ~%.3g фраз до первого хита по %d оставшимся целям\n"),
                   expected, expected_active);
    }
    checkpoint_save(); // EN: persist the search key before any work, so the job is resumable from now on
                       // RU: сохраняем ключ поиска до начала работы, чтобы задание было возобновляемым
}

// ---------------------------------------------------------------- search loop

// EN: Loop-scoped state: batch sizing, the double buffer and the progress printer cursors.
// RU: Состояние цикла: размер пакета, двойной буфер и курсоры печати прогресса.
typedef struct {
    double start, last_print, last_done;
    uint64_t session_start_index;
    uint32_t n;            // EN: current batch size | RU: текущий размер пакета
    uint32_t last_batch_n; // EN: size of the batch processed in the current iteration
                           // RU: размер пакета, обработанного в текущей итерации
} LoopState;

// EN: The 30-second progress line.
// RU: Строка прогресса раз в 30 секунд.
static void print_progress(const LoopState *st, double done, double now) {
    double rate = (done - st->last_done) / (now - st->last_print);
    char clk[16];
    clock_str(clk, sizeof clk);
    printf(tr("[%s] %.2fM phrases, %.0f/s", "[%s] %.2fM фраз, %.0f/с"), clk, done / 1e6, rate);
    if (det.enabled) {
        printf(tr(", index %llu", ", индекс %llu"), (unsigned long long)det.s1_next);
        if (det.s1_next > det.next)
            printf(tr(" (confirmed %llu)", " (подтверждено %llu)"), (unsigned long long)det.next);
        if (det.has_end) {
            double pct = 100.0 * (double)(det.s1_next - det.start) / (double)(det.end - det.start);
            printf("/%llu (%.2f%%)", (unsigned long long)det.end, pct);
        }
    }
    printf(tr(", %d hit(s)", ", хитов: %d"), hits_found);
    if (hits_found > verified)
        printf(tr(" (%d unverified)", " (%d неподтверждённых)"), hits_found - verified);
    if (det.enabled && det.last_save > 0)
        printf(tr(", checkpoint saved %.0f s ago", ", checkpoint сохранён %.0f с назад"), now - det.last_save);
    if (n_targets > 0) {
        int active = 0;
        double expected_now = expected_tries_remaining(&active);
        if (active > 0)
            printf(tr(", %.2f%% of expected, chance found by now %.2f%%",
                      ", %.2f%% от ожидаемого, вероятность находки к этому моменту %.2f%%"),
                   100 * done / expected_now, 100 * (1 - exp(-done / expected_now)));
        else
            printf(tr(", all targets reached", ", все цели достигнуты"));
    }
    printf("\n");
}

// EN: One-time estimate of the wait to the first hit, from the speed measured in this session.
// RU: Разовая оценка ожидания первого хита по скорости, измеренной в этой сессии.
static void print_wait_estimate(LoopState *st, double tb, double done) {
    double el2 = now_sec() - session_start_time;
    if (el2 < 4.0) return;
    double it = now_sec() - tb;
    double c = done - session_start_done;
    double rate = 0;
    if (opt.mode != TON24 && st->last_batch_n && it >= 0.5) {
        rate = (double)st->last_batch_n / it; // EN: steady rate of the batch just done, pause included
                                              // RU: установившаяся скорость последнего пакета, с паузой
    } else if (c > 0) {
        rate = c / el2; // EN: ton-24: full derivations per second, after the first stage-2 batch
                        // RU: ton-24: полных выводов в секунду, после первого пакета стадии 2
    }
    if (rate <= 0) return;
    char tbuf[32], desc[8192];
    printf(tr("average wait to a hit (approximate, %.0f phrases/s measured):\n",
              "среднее ожидание хита (приблизительно, измерено %.0f фраз/с):\n"), rate);
    int active = 0;
    double miss = 1;
    for (int i = 0; i < n_targets; i++) {
        mutex_lock(&stat_lock);
        int done_t = target_done_locked(&targets[i]);
        mutex_unlock(&stat_lock);
        if (done_t) continue;
        active++;
        double p = target_probability(&targets[i]);
        miss *= 1 - p;
        double e = target_expected(&targets[i]);
        target_desc(&targets[i], desc, sizeof desc, 0);
        char disp[MAX_TARGET_LABEL + MAX_TARGET_NAME + 4];
        target_display(&targets[i], disp, sizeof disp);
        fmt_wait(e / rate, tbuf, sizeof tbuf);
        printf(tr("  target %s (%s): ~%.3g phrases, ~%s\n", "  цель %s (%s): ~%.3g фраз, ~%s\n"),
               disp, desc, e, tbuf);
    }
    if (active > 1) {
        double exp_active = miss < 1 ? 1 / (1 - miss) : 1;
        fmt_wait(exp_active / rate, tbuf, sizeof tbuf);
        printf(tr("  first hit on any of the %d remaining targets: ~%.3g phrases, ~%s\n",
                  "  первый хит по любой из %d оставшихся целей: ~%.3g фраз, ~%s\n"),
               active, exp_active, tbuf);
    }
    wait_printed = 1;
}

// EN: One BIP39 double-buffered loop iteration (all modes except ton-24): at 100% power the GPU works
// EN: on the next batch while the CPU finishes the current one.
// RU: Одна итерация цикла BIP39 с двойной буферизацией (все режимы кроме ton-24): при мощности 100%
// RU: GPU работает над следующим пакетом, пока CPU завершает текущий.
static void bip_step(LoopState *st, double tb, BipBatch **cur) {
    if (!*cur) {
        uint32_t g0;
        uint64_t b0 = det_alloc(st->n, &g0);
        if (g0) *cur = bip_new(g0, b0);
    }
    // EN: Below 100% no batch is queued in advance: the power pause at the end of the loop must
    // EN: find the GPU idle, otherwise the GPU would keep working while the CPU sleeps.
    // RU: Ниже 100% пакет не ставится в очередь заранее: пауза мощности в конце итерации должна
    // RU: заставать GPU простаивающим, иначе GPU продолжал бы работать, пока CPU спит.
    uint32_t gn = 0;
    uint64_t bn = 0;
    BipBatch *next = NULL;
    if (opt.power >= 100) {
        bn = det_alloc(st->n, &gn);
        next = gn ? bip_new(gn, bn) : NULL;
    }
    if (*cur) {
        job_wait(&(*cur)->job);
        parallel(opt.threads, bip_post_fn, *cur);
        tune_chunk(2048, (*cur)->n, now_sec() - tb);
        mutex_lock(&stat_lock);
        total_done += (*cur)->n;
        st->last_batch_n = (*cur)->n;
        mutex_unlock(&stat_lock);
        det_commit((*cur)->base + (*cur)->n);
        bip_free(*cur);
    }
    *cur = next;
    if (det.enabled && det.has_end && det.next >= det.end)
        save_all_and_report(tr("range complete", "диапазон завершён"), 0); // EN: random mode never runs out of indices
                                                                           // RU: случайный режим никогда не исчерпывает индексы
}

// EN: One ton-24 loop iteration: stage 1 (basic-seed check, keeps 1/256), stage 2 (key derivation for
// EN: the survivors). After a crash the survivors of the not-yet-flushed range are regenerated from
// EN: their deterministic indices (stage 1 only, cheap); new indices wait until that backfill ends.
// RU: Одна итерация цикла ton-24: стадия 1 (проверка basic-seed, оставляет 1/256), стадия 2 (вывод
// RU: ключей для выживших). После крэша выжившие ещё не сброшенного диапазона восстанавливаются из
// RU: своих детерминированных индексов (только стадия 1, дёшево); новые индексы ждут конца backfill.
static void ton_step(LoopState *st, double tb) {
    uint32_t bn;
    uint64_t bbase;
    int backfill = det.enabled && det.backfill_end > det.backfill_base;
    if (backfill) {
        bbase = det.backfill_base;
        bn = det.backfill_end - det.backfill_base > (uint64_t)st->n ? st->n : (uint32_t)(det.backfill_end - det.backfill_base);
        det.backfill_base += bn;
    } else {
        bbase = det_alloc(st->n, &bn);
    }
    int s1_done = det.enabled && !backfill && bn == 0;
    if (bn) {
        TonBatch b = {0};
        b.n = bn;
        b.base = bbase;
        b.rnd = malloc((size_t)bn * 33);
        b.cand = malloc((size_t)bn * sizeof(TonCand));
        b.keys = malloc((size_t)bn * 128);
        b.out = malloc((size_t)bn * 8);
        if (!b.rnd || !b.cand || !b.keys || !b.out) die(tr("out of memory", "не хватает памяти"));
        if (!det.enabled) random_bytes(b.rnd, (size_t)bn * 33);
        parallel(opt.threads, ton_prep_fn, &b);
        Job j;
        job_submit(&j, b.keys, bn, gpu.salt_ton_version, TON_BASIC_ITERS, 0, 1, b.out, NULL);
        job_wait(&j);
        tune_chunk(TON_BASIC_ITERS, bn, now_sec() - tb);
        job_release(&j);
        for (uint32_t i = 0; i < bn; i++) {
            if ((b.out[i] >> 56) != 0) continue;
            if (n_surv == cap_surv) {
                cap_surv = cap_surv ? cap_surv * 2 : 32768;
                survivors = realloc(survivors, (size_t)cap_surv * sizeof(TonCand));
                if (!survivors) die(tr("out of memory", "не хватает памяти"));
            }
            survivors[n_surv++] = b.cand[i];
        }
        crypto_wipe(b.cand, (size_t)bn * sizeof(TonCand));
        free(b.rnd); free(b.cand); free(b.keys); free(b.out);
        if (!backfill) det_commit_s1(bbase + bn);
    }
    uint32_t ton_stage2 = opt.dump ? (uint32_t)opt.dump : 16384;
    if (n_surv >= ton_stage2 || (s1_done && n_surv > 0)) ton_flush(n_surv);
    if (s1_done && n_surv == 0) {
        det.next = det.s1_next = det.has_end ? det.end : det.next; // EN: everything up to the end is decided
                                                                    // RU: всё до конца уже определено
        save_all_and_report(det.has_end ? tr("range complete", "диапазон завершён")
                                        : tr("index space exhausted", "пространство индексов исчерпано"), 0);
    }
}

// EN: The search loop: stop handling, batch stepping, progress lines, dump/bench exits and the power
// EN: limit. Runs until save_all_and_report() or a utility exit ends the process.
// RU: Цикл поиска: обработка остановки, шаги пакетов, строки прогресса, выходы dump/bench и
// RU: ограничение мощности. Работает, пока процесс не завершат save_all_and_report() или служебный выход.
static void run_search_loop(int real_search) {
    install_signal_handlers();
    session_start_time = now_sec();
    session_start_done = total_done;
    LoopState st = {0};
    st.session_start_index = det.enabled ? det.s1_next : 0; // EN: --stop-after counts index progress
                                                             // RU: --stop-after считает продвижение индексов
    st.start = st.last_print = now_sec();
    st.last_done = session_start_done;
    st.n = 16384;
    BipBatch *cur = NULL;
    for (;;) {
        double tb = now_sec();
        if (g_stop) {
            printf(tr("\nstop requested, finishing the current batch...\n",
                      "\nзапрошена остановка, завершаем текущий пакет...\n"));
            // EN: ton-24: decide the pending survivors now, so the checkpoint covers everything checked
            // RU: ton-24: решаем ожидающих выживших сейчас, чтобы checkpoint покрывал всё проверенное
            if (opt.mode == TON24 && n_surv > 0) ton_flush(n_surv);
            save_all_and_report(g_stop == 1 ? tr("interrupted (Ctrl+C)", "прервано (Ctrl+C)")
                                            : tr("console closed", "окно консоли закрыто"), 0);
        }
        // EN: Stop between batches, after det_commit has recorded everything checked so far. Stopping
        // EN: from inside handle_hit would save a checkpoint that still points before the batch.
        // RU: Останавливаемся между пакетами, после того как det_commit записал всё проверенное.
        // RU: Остановка изнутри handle_hit сохранила бы checkpoint, указывающий до пакета.
        if (real_search && all_targets_done())
            save_all_and_report(tr("all target counts reached", "все счётчики целей достигнуты"), 0);
        if (opt.mode != TON24) {
            bip_step(&st, tb, &cur);
        } else {
            ton_step(&st, tb);
        }
        // EN: adapt batch size: bigger batches for fast GPUs (up to 1M phrases), GPU time measured by profiling
        // EN: batch size does not affect the kernel length (chunking does), only how often the CPU and GPU hand over
        // RU: подбираем размер пакета: больше для быстрых GPU (до 1M фраз), время GPU измеряется профилированием
        // RU: размер пакета не влияет на длину ядра (влияет chunking), только на частоту передачи CPU<->GPU
        double batch_est = iter_cost * 2048 * st.n; // EN: conservative | RU: консервативно
        if (opt.mode != TON24 && iter_cost > 0 && batch_est < 1.5 && st.n < (1u << 20)) st.n *= 2;
        if (getenv("GRAMVANITY_DEBUG"))
            fprintf(stderr, "loop: n=%u loop %.3f s, profiled/iter %.2e s, est batch %.3f s, chunk %u\n", st.n,
                    now_sec() - tb, last_iter_time, batch_est, chunk_iters);

        double now = now_sec(), el = now - st.start;
        mutex_lock(&stat_lock);
        double done = total_done;
        int dmp = dumped;
        mutex_unlock(&stat_lock);
        if (opt.dump && dmp >= opt.dump) {
            fclose(dump_fp);
            printf(tr("dumped %d phrases with their addresses (%s) to %s\n",
                      "записано %d фраз с их адресами (%s) в %s\n"), dmp, wallet_desc(), opt.dump_file);
            exit(0);
        }
        if (opt.stop_after && det.enabled && !opt.dump && !opt.bench &&
            det.s1_next - st.session_start_index >= opt.stop_after) {
            if (opt.mode == TON24 && n_surv > 0) ton_flush(n_surv); // EN: decide pending survivors first
                                                                    // RU: сначала решаем ожидающих выживших
            save_all_and_report(tr("stopped by --stop-after", "остановлено по --stop-after"), 0);
        }
        if (opt.bench > 0 && el >= opt.bench) {
            if (done <= 0) {
                printf(tr("bench %s: no batch finished in %.0f s (batch %u, power %d%%), increase --bench or --power\n",
                          "bench %s: за %.0f с не завершён ни один пакет (пакет %u, мощность %d%%), увеличьте --bench или --power\n"),
                       MODE_NAME[opt.mode], el, st.n, opt.power);
                exit(0);
            }
            printf(tr("bench %s: %.0f phrases/s over %.0f s (batch %u, power %d%%); a 5-character ending (2^30) would take ~%.1f h on average\n",
                      "bench %s: %.0f фраз/с за %.0f с (пакет %u, мощность %d%%); окончание из 5 символов (2^30) в среднем заняло бы ~%.1f ч\n"),
                   MODE_NAME[opt.mode], done / el, el, st.n, opt.power, pow(2, 30) / (done / el) / 3600);
            exit(0);
        }
        if (!opt.bench && !opt.dump && now - st.last_print >= 30) {
            print_progress(&st, done, now);
            st.last_print = now;
            st.last_done = done;
        }
        // EN: power limit: pause in proportion to the work just done, while nothing is queued
        // RU: ограничение мощности: пауза пропорционально только что выполненной работе, пока очередь пуста
        power_pause(now_sec() - tb);
        // EN: one-time estimate of the wait, from the speed measured in this session
        // RU: разовая оценка ожидания по скорости, измеренной в этой сессии
        if (!wait_printed && n_targets > 0 && !opt.bench && !opt.dump)
            print_wait_estimate(&st, tb, done);
    }
}

// ---------------------------------------------------------------- main

static int real_main(int argc, char **argv) {
#ifdef _WIN32
    setvbuf(stdout, NULL, _IONBF, 0); // EN: the Windows CRT treats line buffering as full buffering
                                      // RU: CRT Windows считает построчную буферизацию полной
#else
    setvbuf(stdout, NULL, _IOLBF, 0);
#endif
    opt.mode = TG;
    opt.count = 1;
    opt.verify = 1;
    opt.threads = cpu_count();
    opt.power = 65;
    opt.checkpoint_interval = 30;
    exe_dir(root_dir, sizeof root_dir);
    { // EN: root = parent of the executable's directory | RU: корень = родитель каталога бинарника
        char *s = strrchr(root_dir, PATH_SEP);
        if (s) *s = 0; else strcpy(root_dir, "..");
    }
    // EN: The language comes from GRAMVANITY_LANG, then --lang overrides it. A bad value in the
    // EN: environment is an error; a bad --lang value is reported by parse_args().
    // RU: Язык берётся из GRAMVANITY_LANG, затем --lang его переопределяет. Неверное значение в
    // RU: окружении — ошибка; неверное значение --lang сообщает parse_args().
    const char *bad_lang = lang_init_env();
    lang_prescan(argc, argv);
    if (bad_lang)
        die(tr("GRAMVANITY_LANG: unsupported value '%s' (expected en or ru)",
               "GRAMVANITY_LANG: неподдерживаемое значение '%s' (ожидается en или ru)"), bad_lang);
    parse_args(argc, argv);
    validate_args();
    mutex_init(&hit_lock);
    mutex_init(&stat_lock);
    mutex_init(&checkpoint_lock);
    mutex_init(&finish_lock);

    // EN: CPU-only utility modes that need no GPU and no search state
    // RU: служебные режимы только на CPU, которым не нужны GPU и состояние поиска
    if (opt.selftest_det) return selftest_det();
    if (opt.checkpoint_fix) return cmd_checkpoint_fix();
    if (opt.checkpoint_info) return cmd_checkpoint_info();
    if (opt.reveal_file[0]) return cmd_reveal();
    if (opt.derive_key[0]) return cmd_derive();

    int real_search = !opt.bench && !opt.dump && !opt.list_devices;
    setup_search(real_search);

    init_gpu_and_selftest();
    int expected_active = 0;
    double expected = expected_tries_remaining(&expected_active);
    print_banner(real_search, expected, expected_active);

    run_search_loop(real_search);
    return 0;
}

// EN: Windows converts the Unicode command line to the active ANSI code page before main() sees
// EN: argv, so names like "Паша" can arrive as "????" on a non-Cyrillic system. Rebuild argv as UTF-8
// EN: from the wide command line instead. Other platforms pass their byte arguments through unchanged.
// RU: Windows преобразует Unicode-командную строку в активную ANSI-кодовую страницу до того, как
// RU: main() увидит argv, поэтому имена вроде "Паша" на не-кириллической системе могут прийти как
// RU: "????". Поэтому argv заново собирается как UTF-8 из широкой командной строки. Другие
// RU: платформы передают свои байтовые аргументы без изменений.
#ifdef _WIN32
int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8); // EN: labels and messages are UTF-8, and so is --help
                                 // RU: метки и сообщения в UTF-8, как и --help
    SetConsoleCP(CP_UTF8);
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || wargc <= 0) return real_main(argc, argv);
    char **uargv = calloc((size_t)wargc + 1, sizeof *uargv);
    if (!uargv) { LocalFree(wargv); return real_main(argc, argv); }
    int ok = 1;
    for (int i = 0; i < wargc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        if (n <= 0) { ok = 0; break; }
        uargv[i] = malloc((size_t)n);
        if (!uargv[i] || !WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, uargv[i], n, NULL, NULL)) {
            ok = 0;
            break;
        }
    }
    LocalFree(wargv);
    if (!ok) {
        for (int i = 0; i < wargc; i++) free(uargv[i]);
        free(uargv);
        return real_main(argc, argv);
    }
    uargv[wargc] = NULL;
    int rc = real_main(wargc, uargv);
    for (int i = 0; i < wargc; i++) free(uargv[i]);
    free(uargv);
    return rc;
}
#else
int main(int argc, char **argv) { return real_main(argc, argv); }
#endif
