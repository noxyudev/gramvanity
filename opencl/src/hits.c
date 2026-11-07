// EN: hits.c - hit handling and the end-of-session report (see hits.h). A hit is re-derived on the
// EN: CPU, checked for wallet ambiguity, optionally verified by the external tonutils-go verifier and
// EN: written into owner-only hit files; the stop report captures everything needed to resume.
// RU: hits.c - обработка хитов и отчёт конца сессии (см. hits.h). Хит заново выводится на CPU,
// RU: проверяется на неоднозначность для кошельков, необязательно подтверждается внешним
// RU: верификатором tonutils-go и пишется в hit-файлы, доступные только владельцу; stop-отчёт
// RU: сохраняет всё необходимое для продолжения.
//
// EN: Localization: labels, tags and the secret-bearing lines of hit and stop files stay English
// EN: (they are machine-readable: verify-go/w5addr --hit parses ^phrase (N words...): and the tests
// EN: grep the labels). Explanatory prose follows the run language.
// RU: Локализация: метки, теги и строки с секретами в hit- и stop-файлах остаются английскими
// RU: (они машиночитаемы: verify-go/w5addr --hit разбирает ^phrase (N words...):, а тесты ищут
// RU: метки через grep). Пояснительный текст пишется на языке запуска.
#include "sv.h"
#include "platform.h"
#include "i18n.h"
#include "options.h"
#include "pattern.h"
#include "stats.h"
#include "detsearch.h"
#include "crypto.h"
#include "addresses.h"
#include "checkpoint.h"
#include "gpu.h"
#include "hits.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/monocypher/monocypher.h"

mutex_t finish_lock;
char g_stop_path[4096];

// EN: Append helper for the stop file: never lets the cursor leave the buffer.
// RU: Помощник добавления для stop-файла: курсор никогда не выходит за границы буфера.
static size_t sapp(char *buf, size_t cap, size_t o, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + o, o < cap ? cap - o : 0, fmt, ap);
    va_end(ap);
    if (w < 0) return o;
    return o + (size_t)w >= cap ? cap - 1 : o + (size_t)w;
}

#ifdef _WIN32
// EN: WSL ships bash.exe as an app-execution alias in these two places. It is not MSYS2 and cannot
// EN: run the native Windows paths given to the verifiers, so PATH scanning skips it.
// RU: WSL поставляет bash.exe как alias запуска приложений в этих двух местах. Это не MSYS2, и он не
// RU: может работать с нативными путями Windows, которые получают верификаторы, поэтому сканирование
// RU: PATH его пропускает.
static int is_wsl_bash(const char *cand) {
    char win[MAX_PATH], local[MAX_PATH], probe[MAX_PATH * 2];
    DWORD n = GetWindowsDirectoryA(win, sizeof win);
    if (n > 0 && n < sizeof win) {
        snprintf(probe, sizeof probe, "%s\\System32\\bash.exe", win);
        if (!_stricmp(cand, probe)) return 1;
        snprintf(probe, sizeof probe, "%s\\SysWOW64\\bash.exe", win);
        if (!_stricmp(cand, probe)) return 1;
    }
    n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof local);
    if (n > 0 && n < sizeof local) {
        snprintf(probe, sizeof probe, "%s\\Microsoft\\WindowsApps\\bash.exe", local);
        if (!_stricmp(cand, probe)) return 1;
    }
    return 0;
}

// EN: Finds an MSYS2/Cygwin bash: PATH first, then the usual MSYS2 install locations.
// RU: Ищет bash из MSYS2/Cygwin: сначала PATH, затем обычные места установки MSYS2.
static int find_bash(char *out, size_t cap) {
    const char *path = getenv("PATH");
    while (path && *path) {
        const char *end = strchr(path, ';');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        while (len && path[0] == ' ') { path++; len--; }
        while (len && path[len - 1] == ' ') len--;
        if (len >= 2 && path[0] == '"' && path[len - 1] == '"') { path++; len -= 2; }
        if (len) {
            char cand[MAX_PATH * 2];
            int m = snprintf(cand, sizeof cand, "%.*s\\bash.exe", (int)len, path);
            if (m > 0 && (size_t)m < sizeof cand && GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES &&
                !is_wsl_bash(cand)) {
                snprintf(out, cap, "%s", cand);
                return 1;
            }
        }
        if (!end) break;
        path = end + 1;
    }
    static const char *fallback[] = {"C:\\msys64\\usr\\bin\\bash.exe", "C:\\msys32\\usr\\bin\\bash.exe"};
    for (size_t i = 0; i < sizeof fallback / sizeof fallback[0]; i++) {
        if (GetFileAttributesA(fallback[i]) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, cap, "%s", fallback[i]);
            return 1;
        }
    }
    return 0;
}

// EN: Quotes one argument for the Windows command-line parser used by CreateProcessA. The result
// EN: wraps the argument in quotes and escapes what the parser treats as special, so bash receives
// EN: exactly the original command string (including the quotes around the verifier path).
// RU: Экранирует один аргумент для разборщика командной строки Windows, который использует
// RU: CreateProcessA. Результат оборачивает аргумент в кавычки и экранирует то, что разборщик считает
// RU: специальным, поэтому bash получает ровно исходную строку команды (включая кавычки вокруг пути
// RU: верификатора).
static int win_quote_arg(const char *arg, char *out, size_t cap) {
    size_t o = 0;
    if (cap < 3) return 0;
    out[o++] = '"';
    for (size_t i = 0; arg[i];) {
        if (arg[i] == '\\') {
            size_t run = 0;
            while (arg[i + run] == '\\') run++;
            int quote_next = arg[i + run] == '"' || arg[i + run] == 0;
            size_t need = run * (quote_next ? 2 : 1);
            if (o + need + 2 > cap) return 0;
            for (size_t k = 0; k < need; k++) out[o++] = '\\';
            i += run;
        } else if (arg[i] == '"') {
            if (o + 3 > cap) return 0;
            out[o++] = '\\';
            out[o++] = '"';
            i++;
        } else {
            if (o + 2 > cap) return 0;
            out[o++] = arg[i++];
        }
    }
    out[o++] = '"';
    out[o] = 0;
    return 1;
}
#endif

// EN: Runs one verifier: sends {"words": phrase, [mode, net]} on stdin, reads its JSON answer from a
// EN: temp file. Fills uq / pub / as and, in tg-wallet mode, signpub (signpub must then be non-NULL).
// EN: On Windows the verifier runs under MSYS2 bash (CreateProcessA, stdin pipe, stdout temp file,
// EN: stderr NUL) instead of popen, which would go through cmd.exe. Returns 0 if it could not run.
// RU: Запускает один верификатор: отправляет {"words": phrase, [mode, net]} в stdin, читает его
// RU: JSON-ответ из временного файла. Заполняет uq / pub / as и, в режиме tg-wallet, signpub (тогда
// RU: signpub должен быть не NULL). На Windows верификатор запускается через bash MSYS2
// RU: (CreateProcessA, канал stdin, stdout во временный файл, stderr в NUL), а не через popen,
// RU: который пошёл бы через cmd.exe. Возвращает 0, если запустить не удалось.
static int run_verifier(const char *cmd_prefix, const char *phrase, char *uq, char *pub, char *as, char *signpub) {
    char tmp[4200];
    snprintf(tmp, sizeof tmp, "%s%cresults", root_dir, PATH_SEP);
    mkdir_p(tmp);
    snprintf(tmp, sizeof tmp, "%s%cresults%cverify-%d.tmp", root_dir, PATH_SEP, PATH_SEP, (int)(now_sec() * 1000) % 100000);
#ifdef _WIN32
    char bash[MAX_PATH];
    if (!find_bash(bash, sizeof bash)) return 0;

    char quoted[10000], line[11000];
    if (!win_quote_arg(cmd_prefix, quoted, sizeof quoted)) return 0;
    int ll = snprintf(line, sizeof line, "\"%s\" -c %s", bash, quoted);
    if (ll <= 0 || (size_t)ll >= sizeof line) return 0;

    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE in_rd = NULL, in_wr = NULL, out_h = INVALID_HANDLE_VALUE, nul_h = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    int started = 0;

    if (!CreatePipe(&in_rd, &in_wr, &sa, 0)) return 0;
    SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
    out_h = CreateFileA(tmp, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    nul_h = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (out_h != INVALID_HANDLE_VALUE && nul_h != INVALID_HANDLE_VALUE) {
        STARTUPINFOA si;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_rd;
        si.hStdOutput = out_h;
        si.hStdError = nul_h;
        started = CreateProcessA(bash, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    }
    CloseHandle(in_rd);
    if (out_h != INVALID_HANDLE_VALUE) CloseHandle(out_h);
    if (nul_h != INVALID_HANDLE_VALUE) CloseHandle(nul_h);
    if (!started) {
        CloseHandle(in_wr);
        remove(tmp);
        return 0;
    }
    CloseHandle(pi.hThread);

    char input[4400];
    int ilen = opt.mode == TG
                   ? snprintf(input, sizeof input, "{\"words\":\"%s\",\"mode\":\"tg\",\"net\":\"%s\"}\n", phrase,
                              opt.testnet ? "testnet" : "mainnet")
                   : snprintf(input, sizeof input, "{\"words\":\"%s\"}\n", phrase);
    int sent = 0;
    if (ilen > 0 && (size_t)ilen < sizeof input) {
        DWORD written = 0;
        sent = WriteFile(in_wr, input, (DWORD)ilen, &written, NULL) && written == (DWORD)ilen;
    }
    CloseHandle(in_wr); // EOF for bash
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    if (!sent) {
        remove(tmp);
        return 0;
    }
#else
    char cmd[9000];
    snprintf(cmd, sizeof cmd, "%s > \"%s\" 2>/dev/null", cmd_prefix, tmp);
    FILE *p = popen(cmd, "w");
    if (!p) return 0;
    if (opt.mode == TG)
        fprintf(p, "{\"words\":\"%s\",\"mode\":\"tg\",\"net\":\"%s\"}\n", phrase, opt.testnet ? "testnet" : "mainnet");
    else
        fprintf(p, "{\"words\":\"%s\"}\n", phrase);
    pclose(p);
#endif
    FILE *f = fopen(tmp, "rb");
    if (!f) return 0;
    char buf[2048] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    remove(tmp);
    buf[n] = 0;
    const char *keys[4] = {"\"uq\":\"", "\"pub\":\"", "\"as\":\"", "\"signpub\":\""};
    char *outs[4] = {uq, pub, as, signpub};
    for (int k = 0; k < (signpub ? 4 : 3); k++) {
        char *s = strstr(buf, keys[k]);
        outs[k][0] = 0;
        if (!s) return 0;
        s += strlen(keys[k]);
        char *e = strchr(s, '"');
        if (!e || e - s > 100) return 0;
        memcpy(outs[k], s, (size_t)(e - s));
        outs[k][e - s] = 0;
    }
    return 1;
}

size_t target_desc(const Target *t, char *buf, size_t cap, size_t o) {
    const char *sep = "";
    if (t->legacy_or) { o = sapp(buf, cap, o, "%s", tr("legacy OR checkpoint", "legacy OR из checkpoint")); sep = ", "; }
    if (t->n_pre) {
        o = sapp(buf, cap, o, "%s%s", sep, t->n_pre > 1 ? tr("prefixes", "префиксы") : tr("prefix", "префикс"));
        for (int k = 0; k < t->n_pre; k++) o = sapp(buf, cap, o, " \"%s\"", t->pre[k].str);
        sep = ", ";
    }
    if (t->n_suf) {
        o = sapp(buf, cap, o, "%s%s", sep, t->n_suf > 1 ? tr("endings", "окончания") : tr("ending", "окончание"));
        for (int k = 0; k < t->n_suf; k++) o = sapp(buf, cap, o, " \"%s\"", t->suf[k].str);
    }
    if (!t->legacy_or)
        o = sapp(buf, cap, o, "%s", t->match_or
                 ? tr(" (one group suffices; OR)", " (достаточно одной группы; ИЛИ)")
                 : tr(" (one from each group; AND)", " (по одному из каждой группы; И)"));
    return o;
}

// EN: Writes one hit file per distinct target hit directory (targets sharing an out share one file).
// EN: The file name lists every matched target: hit-A+B-<index>.txt (deterministic) or
// EN: hit-A+B-<timestamp>.txt (random). Registers the files for the stop report and prints the console
// EN: line. Deterministic mode stores the search key + index instead of the phrase.
// RU: Пишет один hit-файл на каждую отдельную папку хитов (цели с общей папкой делят один файл).
// RU: Имя файла перечисляет все совпавшие цели: hit-A+B-<индекс>.txt (детерминированный режим) или
// RU: hit-A+B-<метка времени>.txt (случайный). Регистрирует файлы для stop-отчёта и печатает строку
// RU: в консоль. Детерминированный режим хранит ключ поиска и индекс вместо фразы.
static int write_hit_files(const uint8_t *matched, int ok, uint64_t cand_index, int nw,
                           const char *phrase, const char *sign_phrase, const char *pubhex,
                           const char *signhex, const char *uq, const char *eq, const char *checks,
                           const char *shown) {
    char names[2048] = "";
    char display_names[4096] = "";
    char matched_text[16384] = "";
    size_t no = 0, dno = 0, mo = 0;
    const char **outs = malloc((size_t)(n_targets ? n_targets : 1) * sizeof *outs);
    if (!outs) die(tr("out of memory", "не хватает памяти"));
    int n_outs = 0;
    for (int i = 0; i < n_targets; i++) {
        if (!matched[i]) continue;
        char disp[MAX_TARGET_LABEL + MAX_TARGET_NAME + 4];
        target_display(&targets[i], disp, sizeof disp);
        no = sapp(names, sizeof names, no, "%s%s", no ? "+" : "", targets[i].name);
        dno = sapp(display_names, sizeof display_names, dno, "%s%s", dno ? "+" : "", disp);
        mo = sapp(matched_text, sizeof matched_text, mo, "matched target %s: ", disp);
        mo = target_desc(&targets[i], matched_text, sizeof matched_text, mo);
        mo = sapp(matched_text, sizeof matched_text, mo, "\n");
        const char *dir = targets[i].out_dir[0] ? targets[i].out_dir : job_dir;
        int seen = 0;
        for (int k = 0; k < n_outs; k++) if (!strcmp(outs[k], dir)) { seen = 1; break; }
        if (!seen && n_outs < n_targets) outs[n_outs++] = dir;
    }
    char stamp[64], ts[32], fname[4096], text[32768];
    char keyhex[65] = "";
    ts_iso(stamp, sizeof stamp);
    if (det.enabled) hexs(det.key, 32, keyhex);
    if (det.enabled)
        snprintf(fname, sizeof fname, "hit-%s-%llu.txt", names, (unsigned long long)cand_index);
    else {
        ts_name(ts, sizeof ts);
        snprintf(fname, sizeof fname, "hit-%s-%s.txt", names, ts);
    }
    size_t o = 0;
    o = sapp(text, sizeof text, o, "=== %s %s\n", ok ? "HIT (verified)" : "UNVERIFIED HIT, DO NOT USE", stamp);
    o = sapp(text, sizeof text, o, "%s", matched_text);
    if (det.enabled) {
        o = sapp(text, sizeof text, o, "candidate index: %llu\n", (unsigned long long)cand_index);
        o = sapp(text, sizeof text, o, "search key (SECRET, also opens every other candidate of this job): %s\n", keyhex);
        o = sapp(text, sizeof text, o, "checkpoint: %s\n", det.ckpt_path);
        if (opt.mode == TG) {
            const char *netopt = opt.testnet ? " --net testnet" : "";
            o = sapp(text, sizeof text, o,
                     "mode: %s, 24 words = two BIP39-12 halves (anchor + signing)\n"
                     "wallet: WalletTg (Telegram Wallet), %s, workchain 0, subwallet 0x%08X\n"
                     "public key (anchor, words 1-12): %s\n"
                     "address %s (non-bounceable, shown by wallets): %s\n"
                     "address %s (bounceable): %s\n"
                     "independent checks: %s\n"
                     "replay the 24 words: ./gramvanity --mode tg-wallet%s --derive %s %llu\n"
                     "                or:  ./gramvanity%s --reveal \"%s\" %llu\n",
                     MODE_NAME[opt.mode], opt.testnet ? "testnet" : "mainnet", tg_subwallet(),
                     pubhex, opt.testnet ? "0Q" : "UQ", uq, opt.testnet ? "kQ" : "EQ", eq, checks,
                     netopt, keyhex, (unsigned long long)cand_index,
                     netopt, det.ckpt_path, (unsigned long long)cand_index);
            o = sapp(text, sizeof text, o,
                     tr("the 24 words are two independent BIP39-12 phrases: words 1-12 (anchor) define the address,\n"
                        "words 13-24 (signing) are the key Telegram Wallet signs with\n"
                        "activate the wallet before importing into Telegram Wallet: fund the address, then run\n"
                        "  " W5ADDR_CLI " activate --phrase \"<24 words from --derive/--reveal>\"%s --send\n"
                        "the phrase is not stored here on purpose; keep this file and the checkpoint secret\n\n",
                        "24 слова — это две независимые фразы BIP39-12: слова 1-12 (якорь) задают адрес,\n"
                        "слова 13-24 (подпись) — ключ, которым подписывает Telegram Wallet\n"
                        "перед импортом в Telegram Wallet активируйте кошелёк: пополните адрес и выполните\n"
                        "  " W5ADDR_CLI " activate --phrase \"<24 слова из --derive/--reveal>\"%s --send\n"
                        "фраза намеренно не хранится здесь; держите этот файл и checkpoint в секрете\n\n"),
                     netopt);
        } else {
            o = sapp(text, sizeof text, o,
                     "mode: %s%s\n"
                     "wallet: W5 (v5r1), mainnet, workchain 0, subwallet 0\n"
                     "public key: %s\n"
                     "address UQ (non-bounceable, shown by wallets): %s\n"
                     "address EQ (bounceable): %s\n"
                     "independent checks: %s\n"
                     "replay the phrase: ./gramvanity --mode %s --derive %s %llu\n"
                     "             or: ./gramvanity --reveal \"%s\" %llu\n",
                     MODE_NAME[opt.mode], opt.mode == TON24 ? ", TON standard" : ", BIP39, path m/44'/607'/0'",
                     pubhex, uq, eq, checks, MODE_NAME[opt.mode], keyhex,
                     (unsigned long long)cand_index, det.ckpt_path, (unsigned long long)cand_index);
            o = sapp(text, sizeof text, o,
                     tr("the phrase is not stored here on purpose; keep this file and the checkpoint secret\n\n",
                        "фраза намеренно не хранится здесь; держите этот файл и checkpoint в секрете\n\n"));
        }
    } else {
        o = sapp(text, sizeof text, o, "phrase (%d words, SECRET): %s\n", nw, phrase);
        if (opt.mode == TG) {
            const char *netopt = opt.testnet ? " --net testnet" : "";
            o = sapp(text, sizeof text, o,
                     "  words 1-12  (anchor, define the address): %s\n"
                     "  words 13-24 (signing, Telegram Wallet key): %s\n"
                     "mode: %s, 24 words = two BIP39-12 halves (anchor + signing)\n"
                     "wallet: WalletTg (Telegram Wallet), %s, workchain 0, subwallet 0x%08X\n"
                     "public key (anchor, words 1-12): %s\n"
                     "public key (signing, words 13-24): %s\n"
                     "address %s (non-bounceable, shown by wallets): %s\n"
                     "address %s (bounceable): %s\n"
                     "independent checks: %s\n",
                     phrase, sign_phrase, MODE_NAME[opt.mode], opt.testnet ? "testnet" : "mainnet",
                     tg_subwallet(), pubhex, signhex, opt.testnet ? "0Q" : "UQ", uq,
                     opt.testnet ? "kQ" : "EQ", eq, checks);
            o = sapp(text, sizeof text, o,
                     tr("activate the wallet before importing into Telegram Wallet: fund the address, then run\n"
                        "  " W5ADDR_CLI " activate --phrase \"<the 24 words above>\"%s --send\n"
                        "(random mode: this search cannot be resumed; run without --random for a resumable one)\n\n",
                        "перед импортом в Telegram Wallet активируйте кошелёк: пополните адрес и выполните\n"
                        "  " W5ADDR_CLI " activate --phrase \"<24 слова выше>\"%s --send\n"
                        "(случайный режим: этот поиск нельзя продолжить; запустите без --random, чтобы\n"
                        "поиск можно было продолжить)\n\n"),
                     netopt);
        } else {
            o = sapp(text, sizeof text, o,
                     "mode: %s%s\n"
                     "wallet: W5 (v5r1), mainnet, workchain 0, subwallet 0\n"
                     "public key: %s\n"
                     "address UQ (non-bounceable, shown by wallets): %s\n"
                     "address EQ (bounceable): %s\n"
                     "independent checks: %s\n",
                     MODE_NAME[opt.mode], opt.mode == TON24 ? ", TON standard" : ", BIP39, path m/44'/607'/0'",
                     pubhex, uq, eq, checks);
            o = sapp(text, sizeof text, o,
                     tr("(random mode: this search cannot be resumed; run without --random for a resumable one)\n\n",
                        "(случайный режим: этот поиск нельзя продолжить; запустите без --random, чтобы поиск\n"
                        "можно было продолжить)\n\n"));
        }
    }
    char saved[4096] = "";
    size_t so = 0;
    int wrote = 0;
    for (int k = 0; k < n_outs; k++) {
        mkdir_p(outs[k]);
        char path[8192];
        unique_path(path, sizeof path, outs[k], fname);
        if (write_new_secret(path, (const uint8_t *)text, o) == 0) {
            wrote = 1;
            if (n_hit_files < 64) snprintf(hit_files[n_hit_files++], sizeof hit_files[0], "%s", path);
            so = sapp(saved, sizeof saved, so, "%s%s", so ? ", " : "", path);
        }
    }
    crypto_wipe(text, sizeof text);
    free(outs);
    printf(tr("%s %s  [%s]  %s \"%s\" index %llu  %s %s\n",
              "%s %s  [%s]  %s \"%s\" индекс %llu  %s %s\n"),
           ok ? "HIT" : "UNVERIFIED HIT", shown, checks,
           strchr(display_names, '+') ? tr("targets", "цели") : tr("target", "цель"),
           display_names, (unsigned long long)cand_index,
           wrote ? tr("saved to", "сохранено в") : tr("NOT SAVED, cannot write", "НЕ СОХРАНЕНО, запись невозможна"),
           wrote ? saved : tr("(all hit directories failed)", "(все папки хитов недоступны)"));
    return wrote;
}

static void handle_hit(const int *idx, int nw, const uint8_t priv_gpu[32], const uint8_t pub_gpu[32],
                       const uint8_t *matched, uint64_t cand_index, const char *addr_shown) {
    mutex_lock(&hit_lock);
    uint8_t *active = calloc((size_t)(n_targets ? n_targets : 1), 1);
    if (!active) die(tr("out of memory", "не хватает памяти"));
    int any_active = 0;
    char full_names[512] = ""; // targets dropped because every fitted pattern is full (--all-patterns)
    size_t fno = 0;
    mutex_lock(&stat_lock);
    for (int i = 0; i < n_targets; i++) {
        if (!matched[i]) continue;
        active[i] = (uint8_t)(!target_done_locked(&targets[i]) &&
                              target_fills_pattern_locked(&targets[i], addr_shown));
        if (active[i]) {
            any_active = 1;
        } else if (targets[i].all_patterns) {
            char disp[MAX_TARGET_LABEL + MAX_TARGET_NAME + 4];
            target_display(&targets[i], disp, sizeof disp);
            fno = sapp(full_names, sizeof full_names, fno, "%s%s", fno ? ", " : "", disp);
        }
    }
    mutex_unlock(&stat_lock);
    if (!any_active) {
        mutex_unlock(&hit_lock);
        if (full_names[0])
            printf(tr("hit %s matches %s %s but every pattern it fits already has its --count (--all-patterns); not saved\n",
                      "хит %s совпадает с %s %s, но у каждого подходящего шаблона уже есть свой --count (--all-patterns); не сохранён\n"),
                   addr_shown, strchr(full_names, ',') ? tr("targets", "целями") : tr("target", "целью"), full_names);
        free(active);
        return;
    }
    char phrase[256]; // the searched words: 12 (tg anchor) or 24/12 depending on the mode
    size_t plen = phrase_bytes(idx, nw, phrase);
    char sign_phrase[128] = "";
    size_t sign_len = 0;
    uint8_t sign_pub[32];
    if (opt.mode == TG) {
        int sidx[12];
        tg_signing_indices(cand_index, sidx);
        sign_len = phrase_bytes(sidx, 12, sign_phrase);
        uint8_t spriv[32];
        bip39_key(sign_phrase, sign_len, spriv);
        ed25519_public(spriv, sign_pub);
        crypto_wipe(spriv, sizeof spriv);
    }
    char vphrase[400]; // what the verifiers get: the complete phrase
    if (opt.mode == TG) snprintf(vphrase, sizeof vphrase, "%s %s", phrase, sign_phrase);
    else snprintf(vphrase, sizeof vphrase, "%s", phrase);
    // EN: 1. independent CPU re-derivation of the key from the phrase (tg: the anchor half)
    // RU: 1. независимый повторный вывод ключа из фразы на CPU (tg: половина-якорь)
    uint8_t priv[32], pub[32], id[32];
    if (opt.mode == TON24) {
        uint8_t e[64], s[64];
        ton_entropy(phrase, plen, e);
        cpu_pbkdf2(e, 64, SALT_TON_DEFAULT, 16, TON_ITERS, s);
        memcpy(priv, s, 32);
        crypto_wipe(e, sizeof e); crypto_wipe(s, sizeof s);
    } else {
        bip39_key(phrase, plen, priv);
    }
    ed25519_public(priv, pub);
    account_id_for_mode(pub, id);
    char uq[49], eq[49], pubhex[65], signhex[65] = "";
    friendly(id, 0, uq);
    friendly(id, 1, eq);
    hexs(pub, 32, pubhex);
    if (opt.mode == TG) hexs(sign_pub, 32, signhex);
    const char *shown = opt.bounceable ? eq : uq;
    int ok = memcmp(priv, priv_gpu, 32) == 0 && memcmp(pub, pub_gpu, 32) == 0;
    for (int i = 0; i < n_targets && ok; i++)
        if (active[i] && !target_matches(&targets[i], shown)) ok = 0;
    char checks[512];
    snprintf(checks, sizeof checks, "%s", ok ? "CPU re-derivation OK" : "CPU re-derivation MISMATCH");
    // EN: 2. a phrase that is valid in both formats would be read differently by wallets: skip it.
    // EN:    tg-wallet: each half is imported as BIP39, so neither half may be TON-standard-valid.
    // RU: 2. фразу, допустимую в обоих форматах, кошельки прочитают по-разному: пропускаем её.
    // RU:    tg-wallet: каждая половина импортируется как BIP39, поэтому ни одна не должна быть
    // RU:    допустимой по стандарту TON.
    int ambiguous = opt.mode == TON24 ? bip39_valid(idx, nw)
                  : opt.mode == TG ? (ton_valid(phrase, plen) || ton_valid(sign_phrase, sign_len))
                  : ton_valid(phrase, plen);
    if (ambiguous) {
        mutex_lock(&stat_lock);
        skipped++;
        mutex_unlock(&stat_lock);
        printf(tr("hit %s skipped: %s (ambiguous in wallets)\n",
                  "хит %s пропущен: %s (неоднозначно в кошельках)\n"), shown,
               opt.mode == TG ? tr("a BIP39-12 half is also valid as a TON standard phrase",
                                   "половина BIP39-12 допустима и как стандартная фраза TON")
                              : tr("phrase is valid both as TON standard and as BIP39",
                                   "фраза допустима и как стандартная TON, и как BIP39"));
        fflush(stdout);
        crypto_wipe(phrase, sizeof phrase);
        crypto_wipe(sign_phrase, sizeof sign_phrase);
        crypto_wipe(vphrase, sizeof vphrase);
        free(active);
        mutex_unlock(&hit_lock);
        return;
    }
    // EN: 3. independent verifier (tonutils-go)
    // RU: 3. независимый верификатор (tonutils-go)
    const char *want = opt.mode == TON24 ? "ton" : opt.mode == TG ? "tg" : "bip39";
    if (opt.verify) {
        char cmd[4400];
#ifdef _WIN32
        snprintf(cmd, sizeof cmd, "\"%s%cverify-go%cw5addr.exe\"", root_dir, PATH_SEP, PATH_SEP);
#else
        snprintf(cmd, sizeof cmd, "\"%s%cverify-go%cw5addr\"", root_dir, PATH_SEP, PATH_SEP);
#endif
        char vuq[128], vpub[128], vas[128], vsign[128] = "";
        int ran = run_verifier(cmd, vphrase, vuq, vpub, vas, opt.mode == TG ? vsign : NULL);
        size_t l = strlen(checks);
        if (ran && !strcmp(vuq, uq) && !strcmp(vpub, pubhex) && !strcmp(vas, want) &&
            (opt.mode != TG || !strcmp(vsign, signhex))) {
            snprintf(checks + l, sizeof checks - l, ", tonutils-go OK");
        } else {
            ok = 0;
            snprintf(checks + l, sizeof checks - l, ", tonutils-go %s", ran ? "MISMATCH" : "not runnable");
        }
    } else {
        size_t l = strlen(checks);
        snprintf(checks + l, sizeof checks - l, ", verifier not run (--no-verify)");
    }
    int wrote = write_hit_files(active, ok, cand_index, nw, phrase, sign_phrase, pubhex, signhex, uq, eq, checks, shown);
    if (wrote) {
        mutex_lock(&stat_lock);
        for (int i = 0; i < n_targets; i++)
            if (active[i]) {
                targets[i].hits++;
                target_count_pattern_hits_locked(&targets[i], addr_shown);
            }
        hits_found++;
        if (ok) verified++;
        mutex_unlock(&stat_lock);
    } else if (ok) {
        mutex_lock(&stat_lock);
        verified++;
        mutex_unlock(&stat_lock);
    }
    crypto_wipe(phrase, sizeof phrase);
    crypto_wipe(sign_phrase, sizeof sign_phrase);
    crypto_wipe(vphrase, sizeof vphrase);
    if (!ok && wrote && !unverified_hint_printed) {
        printf(tr("note: this hit is saved and counts toward the target counts, but the external verifier did not confirm it.\n"
                  "      run \"go build -o verify-go/w5addr ./verify-go\" (or use the prebuilt binary), or pass --no-verify.\n",
                  "примечание: хит сохранён и идёт в счётчики целей, но внешний верификатор его не подтвердил.\n"
                  "      выполните \"go build -o verify-go/w5addr ./verify-go\" (или возьмите готовый бинарник) либо передайте --no-verify.\n"));
        unverified_hint_printed = 1;
    }
    fflush(stdout);
    free(active);
    mutex_unlock(&hit_lock);
}

void finish(const int *idx, int nw, const uint8_t priv[32], const uint8_t pub_gpu[32], uint64_t cand_index) {
    const uint8_t *pub = pub_gpu;
    uint8_t id[32];
    char addr[49];
    account_id_for_mode(pub, id);
    friendly(id, opt.bounceable, addr);
    if (opt.dump) {
        mutex_lock(&stat_lock);
        if (dumped < opt.dump) {
            char phrase[256], uq[49], ph[65];
            size_t pl = phrase_bytes(idx, nw, phrase);
            friendly(id, 0, uq);
            hexs(pub, 32, ph);
            if (opt.mode == TG) {
                int sidx[12]; // the dump holds the full 24 words: anchor + signing half
                tg_signing_indices(cand_index, sidx);
                phrase[pl] = ' ';
                phrase_bytes(sidx, 12, phrase + pl + 1);
                fprintf(dump_fp, "{\"words\":\"%s\",\"uq\":\"%s\",\"pub\":\"%s\",\"mode\":\"tg\",\"net\":\"%s\",\"src\":\"opencl\"}\n",
                        phrase, uq, ph, opt.testnet ? "testnet" : "mainnet");
            } else {
                fprintf(dump_fp, "{\"words\":\"%s\",\"uq\":\"%s\",\"pub\":\"%s\",\"src\":\"opencl\"}\n", phrase, uq, ph);
            }
            dumped++;
        }
        mutex_unlock(&stat_lock);
        return;
    }
    if (n_targets) {
        uint8_t matched[n_targets];
        memset(matched, 0, (size_t)n_targets);
        int any = targets_match_all(addr, matched);
        if (any) handle_hit(idx, nw, priv, pub, matched, cand_index, addr);
    }
}

// ---------------------------------------------------------------- end-of-session report

// EN: The stop report lists every target and pattern, so its size grows with the target table. The
// EN: buffer is allocated once per process (a session writes at most one report).
// RU: Stop-отчёт перечисляет все цели и шаблоны, поэтому его размер растёт вместе с таблицей целей.
// RU: Буфер выделяется один раз на процесс (за сессию пишется не больше одного отчёта).
static char *stop_text;
static size_t stop_text_cap;

static char *stop_text_reserve(void) {
    size_t need = 32768;
    for (int i = 0; i < n_targets; i++) {
        const Target *t = &targets[i];
        need += 512 + strlen(t->name) + strlen(t->label) + strlen(t->out_dir);
        for (int k = 0; k < t->n_pre; k++) need += strlen(t->pre[k].str) + 16;
        for (int k = 0; k < t->n_suf; k++) need += strlen(t->suf[k].str) + 16;
    }
    need += (size_t)n_hit_files * 512;
    if (need > stop_text_cap) {
        char *p = realloc(stop_text, need);
        if (!p) return NULL;
        stop_text = p;
        stop_text_cap = need;
    }
    return stop_text;
}

// EN: Writes stop-<mode>-...txt with everything needed to understand and continue the session, saves
// EN: the checkpoint, and (outside a signal handler) prints a summary and exits. Idempotent via
// EN: finish_lock. The report is human-readable prose in the run language; the anchor labels the
// EN: tests and scripts look for ("deterministic search: indices", "random mode: progress cannot be
// EN: resumed") stay identical in English.
// RU: Пишет stop-<режим>-...txt со всем, что нужно для понимания и продолжения сессии, сохраняет
// RU: checkpoint и (вне обработчика сигнала) печатает сводку и выходит. Идемпотентно через
// RU: finish_lock. Отчёт — человекочитаемый текст на языке запуска; опорные метки, которые ищут
// RU: тесты и скрипты ("deterministic search: indices", "random mode: progress cannot be resumed"),
// RU: в английском варианте остаются неизменными.
static void write_stop_file(const char *reason) {
    char ts[32], stamp[64], name[512];
    char *text = stop_text_reserve();
    if (!text) die(tr("out of memory", "не хватает памяти"));
    size_t cap = stop_text_cap;
    ts_name(ts, sizeof ts);
    ts_iso(stamp, sizeof stamp);
    mutex_lock(&stat_lock);
    double done = total_done;
    int hits = verified;
    int found = hits_found;
    int skipped_now = skipped;
    mutex_unlock(&stat_lock);
    double elapsed = now_sec() - session_start_time;
    double rate = elapsed > 0.001 ? (done - session_start_done) / elapsed : 0;
    uint64_t resume_at = det.enabled ? det.next : 0;
    if (det.enabled)
        snprintf(name, sizeof name, "stop-%s-idx%llu-%s.txt", MODE_NAME[opt.mode], (unsigned long long)resume_at, ts);
    else
        snprintf(name, sizeof name, "stop-%s-n%llu-%s.txt", MODE_NAME[opt.mode], (unsigned long long)done, ts);
    char path[8192];
    unique_path(path, sizeof path, job_dir, name);
    size_t o = 0;
    o = sapp(text, cap, o, tr("=== gramvanity session ended: %s ===\n",
                              "=== сессия gramvanity завершена: %s ===\n"), reason);
    o = sapp(text, cap, o, tr("job folder: %s\n", "папка задания: %s\n"), job_dir);
    o = sapp(text, cap, o,
             tr("ended: %s, ran %.0f s, %.0f phrases checked this session, %.0f/s average\n",
                "окончено: %s, прошло %.0f с, за сессию проверено фраз: %.0f, в среднем %.0f/с\n"),
             stamp, elapsed, done - session_start_done, rate);
    o = sapp(text, cap, o,
             tr("mode: %s, form: %s, GPU: %s, CPU threads: %d\n",
                "режим: %s, форма: %s, GPU: %s, потоков CPU: %d\n"),
             MODE_NAME[opt.mode], form_name(), gpu.name[0] ? gpu.name : "?", opt.threads);
    o = sapp(text, cap, o, tr("wallet: %s\n", "кошелёк: %s\n"), wallet_desc());
    o = sapp(text, cap, o, tr("targets (%d):\n", "цели (%d):\n"), n_targets);
    for (int i = 0; i < n_targets; i++) {
        const Target *t = &targets[i];
        char disp[MAX_TARGET_LABEL + MAX_TARGET_NAME + 4];
        target_display(t, disp, sizeof disp);
        mutex_lock(&stat_lock);
        int th = t->hits;
        int done_t = target_done_locked(t);
        o = sapp(text, cap, o,
                 tr("  %s: %d hit(s), %s, out %s\n", "  %s: %d хит(ов), %s, папка %s\n"), disp, th,
                 done_t ? tr("count reached", "счётчик достигнут")
                        : (t->count_given ? tr("count not reached", "счётчик не достигнут")
                                          : tr("no count limit", "без счётчика")),
                 t->out_dir[0] ? t->out_dir : job_dir);
        if (t->all_patterns && t->count_given) { // EN: per-pattern progress toward --all-patterns
                                                 // RU: прогресс по шаблонам для --all-patterns
            o = sapp(text, cap, o, tr("    per-pattern hits: ",
                                      "    хитов по шаблонам: "));
            const char *psep = "";
            for (int k = 0; k < t->n_pre; k++) {
                o = sapp(text, cap, o, tr("%sprefix \"%s\" %llu/%llu", "%sпрефикс \"%s\" %llu/%llu"), psep,
                         t->pre[k].str, (unsigned long long)t->pre_hits[k], (unsigned long long)t->count);
                psep = ", ";
            }
            for (int k = 0; k < t->n_suf; k++) {
                o = sapp(text, cap, o, tr("%sending \"%s\" %llu/%llu", "%sокончание \"%s\" %llu/%llu"), psep,
                         t->suf[k].str, (unsigned long long)t->suf_hits[k], (unsigned long long)t->count);
                psep = ", ";
            }
            o = sapp(text, cap, o, "\n");
        }
        mutex_unlock(&stat_lock);
        o = sapp(text, cap, o, "    ");
        o = target_desc(t, text, cap, o);
        o = sapp(text, cap, o, "\n");
    }
    o = sapp(text, cap, o,
             tr("hits: %d unique address(es) saved (%d passed all enabled checks, %d did not), %d skipped (ambiguous)\n",
                "хиты: сохранено уникальных адресов: %d (прошли все включённые проверки: %d, не прошли: %d), "
                "пропущено (неоднозначных): %d\n"),
             found, hits, found > hits ? found - hits : 0, skipped_now);
    for (int k = 0; k < n_hit_files; k++)
        o = sapp(text, cap, o, tr("hit file: %s\n", "hit-файл: %s\n"), hit_files[k]);
    if (det.enabled) {
        o = sapp(text, cap, o,
                 tr("deterministic search: indices [%llu, %s), confirmed resume index %llu, stage-1 frontier %llu (%.0f candidates derived in total)\n",
                    "детерминированный поиск: индексы [%llu, %s), подтверждённый индекс продолжения %llu, "
                    "граница стадии 1 %llu (всего выведено кандидатов: %.0f)\n"),
                 (unsigned long long)det.start, det.has_end ? tr("bounded", "ограничен") : tr("unbounded", "без границы"),
                 (unsigned long long)det.next, (unsigned long long)det.s1_next, done);
        if (det.has_end)
            o = sapp(text, cap, o,
                     tr("range end (exclusive): %llu, %.4f%% complete\n",
                        "конец диапазона (не включая): %llu, завершено %.4f%%\n"),
                     (unsigned long long)det.end,
                     100.0 * (double)(det.s1_next - det.start) / (double)(det.end - det.start));
        else
            o = sapp(text, cap, o, tr("range end: unlimited\n", "конец диапазона: без ограничения\n"));
        o = sapp(text, cap, o, tr("checkpoint (SECRET, keep private): %s\n",
                                  "checkpoint (СЕКРЕТ, храните приватно): %s\n"), det.ckpt_path);
        o = sapp(text, cap, o, tr("resume with: ./gramvanity --resume \"%s\"\n",
                                  "продолжить: ./gramvanity --resume \"%s\"\n"), det.ckpt_path);
        char keyhex[65];
        hexs(det.key, 32, keyhex);
        o = sapp(text, cap, o, tr("search key (SECRET): %s\n", "ключ поиска (СЕКРЕТ): %s\n"), keyhex);
    } else {
        o = sapp(text, cap, o,
                 tr("random mode: progress cannot be resumed; run without --random for a resumable deterministic search\n",
                    "случайный режим: прогресс нельзя продолжить; запустите без --random для "
                    "возобновляемого детерминированного поиска\n"));
    }
    if (write_new_secret(path, (const uint8_t *)text, o) == 0) snprintf(g_stop_path, sizeof g_stop_path, "%s", path);
    crypto_wipe(text, o);
}

// EN: Saves the checkpoint, writes the stop report and prints the closing summary. Called from the
// EN: search loop and from signal handlers; finish_lock plus g_finished make it run once. A signal
// EN: handler must not print, so in_handler returns before the console summary.
// RU: Сохраняет checkpoint, пишет stop-отчёт и печатает итоговую сводку. Вызывается из цикла поиска
// RU: и из обработчиков сигналов; finish_lock вместе с g_finished гарантируют один запуск.
// RU: Обработчик сигнала не должен печатать, поэтому при in_handler сводка в консоль не выводится.
void save_all_and_report(const char *reason, int in_handler) {
    mutex_lock(&finish_lock);
    if (!g_finished) {
        if (det.enabled && !opt.dump) checkpoint_save();
        if (!opt.dump && !opt.bench) write_stop_file(reason);
        g_finished = 1;
    }
    mutex_unlock(&finish_lock);
    if (in_handler) return;
    if (!opt.dump && !opt.bench) {
        printf(tr("stopped: %s\n", "остановлено: %s\n"), reason);
        if (det.enabled && det.ckpt_path[0])
            printf(tr("checkpoint saved: %s\nresume with: ./gramvanity --resume \"%s\"\n",
                      "checkpoint сохранён: %s\nпродолжить: ./gramvanity --resume \"%s\"\n"),
                   det.ckpt_path, det.ckpt_path);
        if (g_stop_path[0])
            printf(tr("session report: %s\n", "отчёт сессии: %s\n"), g_stop_path);
    }
    exit(0);
}
