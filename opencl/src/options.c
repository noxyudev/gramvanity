// EN: options.c - the parsed command line state (opt), the job folder paths and the usage text.
// RU: options.c - разобранное состояние командной строки (opt), пути папки задания и текст справки.
#include "sv.h"
#include "platform.h"
#include "options.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>

const char *MODE_NAME[4] = {"bip39-12", "bip39-24", "ton-24", "tg-wallet"};

Options opt;

char root_dir[4096];
char job_dir[4096];

void job_dir_make(void) {
    if (opt.out_given) {
        snprintf(job_dir, sizeof job_dir, "%s", opt.out_dir);
    } else {
        char ts[32];
        ts_name(ts, sizeof ts);
        snprintf(job_dir, sizeof job_dir, "%s%cresults%cjob-%s", root_dir, PATH_SEP, PATH_SEP, ts);
        // EN: Two runs started in the same second must not share a job folder.
        // RU: Два запуска в одну секунду не должны попасть в одну папку задания.
        for (int n = 2; dir_exists(job_dir); n++)
            snprintf(job_dir, sizeof job_dir, "%s%cresults%cjob-%s-%d", root_dir, PATH_SEP, PATH_SEP, ts, n);
    }
    mkdir_p(job_dir);
    if (!dir_exists(job_dir))
        die(tr("cannot create job folder %s", "не удалось создать папку задания %s"), job_dir);
}

// EN: The full CLI reference. English is the primary text; --lang ru prints the Russian variant.
// RU: Полная справка по CLI. Основной текст английский; --lang ru печатает русский вариант.
void usage(int code) {
    if (g_lang == LANG_RU) {
        printf("использование: gramvanity --suffix '[a-z_]HELLO' [--prefix PATTERN] [параметры]\n"
               "               gramvanity [глобальные параметры] --target [ИМЯ] [параметры цели] [--target [ИМЯ] ...]\n"
               "  Цель совпадает, когда один из её шаблонов --prefix (если заданы) совпадает сразу после тега\n"
               "  формы И один из её шаблонов --suffix (если заданы) совпадает в конце (--match or принимает\n"
               "  совпадение в любой из групп). Повторённые или перечисленные через запятую шаблоны внутри\n"
               "  одной группы — альтернативы (ИЛИ). Без --target у запуска одна цель.\n"
               "  Параметры цели до первого --target задают значения по умолчанию для всех целей; внутри\n"
               "  цели они переопределяют только эту цель:\n"
               "  --prefix PATTERN   начало для совпадения сразу после UQ/EQ (0Q/kQ в testnet tg-wallet);\n"
               "                     [..] = один из перечисленных символов\n"
               "  --suffix PATTERN   окончание для совпадения\n"
               "  --match and|or     как комбинируются две группы: and (по умолчанию) требует один подходящий\n"
               "                     префикс и одно подходящее окончание, or принимает совпадение в любой\n"
               "                     группе отдельно\n"
               "  --count N          остановить эту цель после N сохранённых хитов (по умолчанию: 1)\n"
               "  --all-patterns     счёт по шаблонам: --count применяется к каждому варианту шаблона цели, и\n"
               "                     поиск идёт, пока у каждого не будет N хитов (хит засчитывается каждому\n"
               "                     шаблону, которому он соответствует; хиты, не заполняющие ни один\n"
               "                     недостающий вариант, сообщаются, но не сохраняются)\n"
               "  --ignore-case      сопоставление без учёта регистра для этой цели\n"
               "  --out DIR          папка хитов только для этой цели\n"
               "  --target [ИМЯ]     начать блок цели; безымянные цели получают имена t1, t2, ...;\n"
               "                     кириллические имена транслитерируются для имён файлов (Паша -> Pasha)\n"
               "  --mode         формат кошелька и фразы (глобально; по умолчанию tg-wallet):\n"
               "                   bip39-12  мнемоника BIP39, ключ через SLIP-10 m/44'/607'/0' (импортируют\n"
               "                             Tonkeeper и My Wallet; этот же ключ использует W5 по умолчанию)\n"
               "                   bip39-24  тот же вывод ключа с 24-словной мнемоникой BIP39\n"
               "                   ton-24    стандартная мнемоника TON (любой кошелёк TON), примерно в 100\n"
               "                             раз дороже\n"
               "                   tg-wallet Telegram Wallet (WalletTg): фраза из 24 слов = две половины\n"
               "                             BIP39-12, слова 1-12 задают адрес, слова 13-24 — ключ подписи;\n"
               "                             перед импортом активируйте кошелёк, см. docs/tg-wallet.md\n"
               "  --net          сеть tg-wallet: mainnet (по умолчанию, UQ/EQ, subwallet 0x7FFF7F11) или\n"
               "                 testnet (0Q/kQ, subwallet 0x7FFF7FFD); testnet работает только с\n"
               "                 --mode tg-wallet\n"
               "  --form uq      не-bounceable форма UQ... (её показывают кошельки для W5), по умолчанию\n"
               "  --threads N    потоки CPU для адресов и проверки хитов (по умолчанию: все логические CPU)\n"
               "  --power N      ограничение мощности, проценты: GPU и CPU заняты N%% времени\n"
               "                 (по умолчанию 65, 100 = без ограничения)\n"
               "  --lang en|ru   язык сообщений (по умолчанию en; также переменная GRAMVANITY_LANG)\n"
               "  --out DIR      папка задания: hit-файлы, stop-отчёт и checkpoint по умолчанию (по\n"
               "                 умолчанию ../results/job-<UTC timestamp>/)\n"
               "детерминированный возобновляемый поиск (по умолчанию):\n"
               "  --start-offset N  индекс первого кандидата (по умолчанию 0)\n"
               "  --tries N         обработать не более N кандидатов от start offset (диапазон индексов)\n"
               "  --checkpoint FILE где хранить состояние поиска (по умолчанию <папка задания>/job.ckpt)\n"
               "  --resume FILE     продолжить сохранённый поиск с последнего подтверждённого индекса;\n"
               "                    --tries расширяет диапазон\n"
               "  --checkpoint-interval SEC  период автосохранения, по умолчанию 30\n"
               "  --stop-after N    аккуратно остановиться после ещё N кандидатов (состояние сохраняется)\n"
               "  --random          свежая случайность ОС для каждого кандидата вместо детерминированной\n"
               "                    последовательности; не возобновляется и несовместим с --start-offset,\n"
               "                    --tries, --checkpoint, --resume и --stop-after\n"
               "  --derive KEYHEX INDEX    напечатать фразу кандидата по ключу поиска и индексу\n"
               "  --reveal CHECKPOINT INDEX  то же, с ключом из файла checkpoint\n"
               "  --ckpt-info FILE  напечатать содержимое checkpoint (без секретного ключа)\n"
               "  --selftest-det    запустить unit-тесты детерминированного поиска и выйти (GPU не нужен)\n"
               "  --device N     какой GPU OpenCL использовать (см. --list-devices), по умолчанию 0\n");
        exit(code);
    }
    printf("usage: gramvanity --suffix '[a-z_]HELLO' [--prefix PATTERN] [options]\n"
           "       gramvanity [global options] --target [NAME] [target options] [--target [NAME] ...]\n"
           "  A target matches when one of its --prefix patterns (if any) matches right after the form tag\n"
           "  AND one of its --suffix patterns (if any) matches at the end (--match or accepts a match in\n"
           "  either group). Repeated/comma-separated patterns within one group are alternatives (OR).\n"
           "  Without --target the run has one target.\n"
           "  Target options before the first --target set defaults for every target; inside a target they\n"
           "  override that target only:\n"
           "  --prefix PATTERN   beginning to match right after UQ/EQ (0Q/kQ on tg-wallet testnet); [..] = one\n"
           "                     of the listed characters\n"
           "  --suffix PATTERN   ending to match\n"
           "  --match and|or     how the two groups combine: and (default) needs one matching prefix and one\n"
           "                     matching suffix, or accepts a match in either group alone\n"
           "  --count N          stop this target after N saved hits (default: 1)\n"
           "  --all-patterns     per-pattern counting: --count applies to every pattern variant of the\n"
           "                     target and the search runs until each one has its N hits (a hit counts\n"
           "                     for every pattern it matches; hits that fill no missing variant are\n"
           "                     reported but not saved)\n"
           "  --ignore-case      case-insensitive matching for this target\n"
           "  --out DIR          hit directory for this target only\n"
           "  --target [NAME]    start a target block; unnamed targets are named t1, t2, ...;\n"
           "                     Cyrillic names are transliterated for file names (Паша -> Pasha)\n"
           "  --mode         wallet and phrase format (global; default tg-wallet):\n"
           "                   bip39-12  BIP39 mnemonic, key via SLIP-10 m/44'/607'/0' (Tonkeeper and\n"
           "                             My Wallet import it; also what the W5 default key uses)\n"
           "                   bip39-24  the same derivation with a 24-word BIP39 mnemonic\n"
           "                   ton-24    TON standard mnemonic (every TON wallet), ~100x more expensive\n"
           "                   tg-wallet Telegram Wallet (WalletTg): the phrase is 24 words = two BIP39-12\n"
           "                             halves, words 1-12 define the address, words 13-24 are the signing\n"
           "                             key; activate the wallet before importing, see docs/tg-wallet.md\n"
           "  --net          tg-wallet network: mainnet (default, UQ/EQ, subwallet 0x7FFF7F11) or testnet\n"
           "                 (0Q/kQ, subwallet 0x7FFF7FFD); testnet only works with --mode tg-wallet\n"
           "  --form uq      non-bounceable form UQ... (what wallets show for W5), default\n"
           "  --threads N    CPU threads for addresses and hit checks (default: all logical CPUs)\n"
           "  --power N      performance limit, percent: the GPU and the CPU stay busy N%% of the time\n"
           "                 (default 65, 100 = no limit)\n"
           "  --lang en|ru   message language (default en; also the GRAMVANITY_LANG variable)\n"
           "  --out DIR      job folder: hit files, stop report and the default checkpoint (default\n"
           "                 ../results/job-<UTC timestamp>/)\n"
           "deterministic, resumable search (the default):\n"
           "  --start-offset N  first candidate index (default 0)\n"
           "  --tries N         process at most N candidates from the start offset (index range)\n"
           "  --checkpoint FILE where to keep the search state (default <job folder>/job.ckpt)\n"
           "  --resume FILE     continue a saved search from its last confirmed index; --tries extends\n"
           "  --checkpoint-interval SEC  autosave period, default 30\n"
           "  --stop-after N    stop gracefully after N more candidates (state is saved)\n"
           "  --random          fresh OS randomness for every candidate instead of the deterministic\n"
           "                    sequence; not resumable, and incompatible with --start-offset, --tries,\n"
           "                    --checkpoint, --resume and --stop-after\n"
           "  --derive KEYHEX INDEX    print the candidate phrase for a search key and index\n"
           "  --reveal CHECKPOINT INDEX  same, with the key from a checkpoint file\n"
           "  --ckpt-info FILE  print the contents of a checkpoint (without the secret key)\n"
           "  --selftest-det    run the deterministic-search unit tests and exit (no GPU needed)\n"
           "  --device N     OpenCL GPU to use (see --list-devices), default 0\n");
    exit(code);
}
