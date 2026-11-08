// EN: platform.c - OS portability layer (see platform.h): diagnostics, time, CSPRNG, filesystem,
// EN: threads and signal handling. Every #ifdef _WIN32 fork of the program lives here.
// RU: platform.c - слой переносимости ОС (см. platform.h): диагностика, время, CSPRNG, файловая
// RU: система, потоки и обработка сигналов. Все ветки #ifdef _WIN32 живут здесь.
#include "sv.h"
#include "platform.h"
#include "i18n.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/random.h>
#endif

__attribute__((noreturn)) void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

double now_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
#endif
}

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

void clock_str(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(buf, n, "%H:%M:%S", &tmv);
}

void fmt_wait(double sec, char *buf, size_t n) {
    // EN: Unit suffixes are the only localized part of a wait estimate; the number formatting stays
    // EN: the same so that "~2.1 h" and "~2.1 ч" carry identical precision.
    // RU: Из оценки ожидания переводятся только единицы; формат чисел остаётся прежним, чтобы
    // RU: "~2.1 h" и "~2.1 ч" имели одинаковую точность.
    if (!(sec > 0)) snprintf(buf, n, tr("0 s", "0 с"));
    else if (sec < 90) snprintf(buf, n, tr("%.0f s", "%.0f с"), sec);
    else if (sec < 5400) snprintf(buf, n, tr("%.0f min", "%.0f мин"), sec / 60);
    else if (sec < 172800) snprintf(buf, n, tr("%.1f h", "%.1f ч"), sec / 3600);
    else if (sec < 315576000) snprintf(buf, n, tr("%.1f days", "%.1f дн"), sec / 86400);
    else snprintf(buf, n, tr("%.3g years", "%.3g лет"), sec / 31557600);
}

void ts_name(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(buf, n, "%Y%m%d-%H%M%S", &tmv);
}

void ts_iso(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

void random_bytes(uint8_t *p, size_t n) {
#if defined(_WIN32)
    while (n) {
        ULONG c = n > 0x10000000 ? 0x10000000 : (ULONG)n;
        if (BCryptGenRandom(NULL, p, c, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) die(tr("BCryptGenRandom failed", "BCryptGenRandom завершился ошибкой"));
        p += c;
        n -= c;
    }
#elif defined(__linux__)
    while (n) {
        ssize_t r = getrandom(p, n > 33554431 ? 33554431 : n, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            die(tr("getrandom failed", "getrandom завершился ошибкой"));
        }
        p += r;
        n -= (size_t)r;
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) die(tr("cannot open /dev/urandom", "не удалось открыть /dev/urandom"));
    while (n) {
        size_t r = fread(p, 1, n, f);
        if (r == 0) { fclose(f); die(tr("cannot read /dev/urandom", "не удалось прочитать /dev/urandom")); }
        p += r;
        n -= r;
    }
    fclose(f);
#endif
}

int cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long c = sysconf(_SC_NPROCESSORS_ONLN);
    return c > 0 ? (int)c : 4;
#endif
}

void exe_dir(char *out, size_t n) {
    char p[4096] = {0};
#if defined(_WIN32)
    GetModuleFileNameA(NULL, p, sizeof p - 1);
#else
    ssize_t r = readlink("/proc/self/exe", p, sizeof p - 1);
    if (r <= 0) strcpy(p, "./x");
#endif
    char *s = strrchr(p, PATH_SEP);
    if (s) *s = 0; else strcpy(p, ".");
    snprintf(out, n, "%s", p);
}

void make_dir(const char *dir) {
#ifdef _WIN32
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0700);
#endif
}

int dir_exists(const char *dir) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(dir);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

void mkdir_p(const char *dir) {
    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s", dir) >= (int)sizeof tmp) return;
    for (char *s = tmp + 1; *s; s++) {
        if (*s != PATH_SEP) continue;
        char save = *s;
        *s = 0;
        if (tmp[0]) make_dir(tmp);
        *s = save;
    }
    make_dir(tmp);
}

int write_new_secret(const char *path, const uint8_t *data, size_t len) {
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return -1;
    int ok = (size_t)_write(fd, data, (unsigned)len) == len;
    _commit(fd);
    _close(fd);
    return ok ? 0 : -1;
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    int ok = (size_t)write(fd, data, len) == len;
    fsync(fd);
    close(fd);
    return ok ? 0 : -1;
#endif
}

int write_file_atomic(const char *path, const uint8_t *data, size_t len) {
    char tmp[4096 + 8];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
#ifdef _WIN32
    int fd = _open(tmp, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        int w = _write(fd, data + off, (unsigned)(len - off > 65536 ? 65536 : len - off));
        if (w <= 0) { _close(fd); return -1; }
        off += (size_t)w;
    }
    _commit(fd);
    _close(fd);
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) { _unlink(tmp); return -1; }
#else
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) { close(fd); return -1; }
        off += (size_t)w;
    }
    fsync(fd);
    fchmod(fd, 0600);
    close(fd);
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
#endif
    return 0;
}

void unique_path(char *out, size_t n, const char *dir, const char *name) {
    for (int k = 0;; k++) {
        if (!k) snprintf(out, n, "%s%c%s", dir, PATH_SEP, name);
        else snprintf(out, n, "%s%c%s-%d", dir, PATH_SEP, name, k);
#ifdef _WIN32
        if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) return;
#else
        struct stat st;
        if (stat(out, &st) != 0) return;
#endif
    }
}

int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s || *s == '-' || *s == '+') return -1;
    errno = 0;
    char *e;
    unsigned long long v = strtoull(s, &e, 10);
    if (errno || *e || e == s) return -1;
    *out = (uint64_t)v;
    return 0;
}

int add_ovf_u64(uint64_t a, uint64_t b, uint64_t *r) {
    uint64_t s = a + b;
    if (s < a) return -1; // overflow
    *r = s;
    return 0;
}

// ---------------------------------------------------------------- threads

typedef struct { void (*fn)(void *, int, int); void *ctx; int tid, nt; } TArg;
#ifdef _WIN32
static unsigned __stdcall thunk(void *p) { TArg *a = (TArg *)p; a->fn(a->ctx, a->tid, a->nt); return 0; }
#else
static void *thunk(void *p) { TArg *a = (TArg *)p; a->fn(a->ctx, a->tid, a->nt); return NULL; }
#endif

void mutex_init(mutex_t *m) {
#ifdef _WIN32
    InitializeCriticalSection(m);
#else
    pthread_mutex_init(m, NULL);
#endif
}

void mutex_lock(mutex_t *m) {
#ifdef _WIN32
    EnterCriticalSection(m);
#else
    pthread_mutex_lock(m);
#endif
}

void mutex_unlock(mutex_t *m) {
#ifdef _WIN32
    LeaveCriticalSection(m);
#else
    pthread_mutex_unlock(m);
#endif
}

void parallel(int nt, void (*fn)(void *, int, int), void *ctx) {
    if (nt <= 1) { fn(ctx, 0, 1); return; }
    TArg *args = calloc((size_t)nt, sizeof *args);
#ifdef _WIN32
    HANDLE *h = calloc((size_t)nt, sizeof *h);
#else
    pthread_t *h = calloc((size_t)nt, sizeof *h);
#endif
    if (!args || !h) die(tr("out of memory", "не хватает памяти"));
    for (int i = 0; i < nt; i++) {
        args[i] = (TArg){fn, ctx, i, nt};
#ifdef _WIN32
        h[i] = (HANDLE)_beginthreadex(NULL, 0, thunk, &args[i], 0, NULL);
        if (!h[i]) die(tr("thread start failed", "не удалось запустить поток"));
#else
        if (pthread_create(&h[i], NULL, thunk, &args[i]) != 0) die(tr("thread start failed", "не удалось запустить поток"));
#endif
    }
    for (int i = 0; i < nt; i++) {
#ifdef _WIN32
        WaitForSingleObject(h[i], INFINITE);
        CloseHandle(h[i]);
#else
        pthread_join(h[i], NULL);
#endif
    }
    free(args);
    free(h);
}

// ---------------------------------------------------------------- signals: Ctrl+C and console close

volatile sig_atomic_t g_stop; // EN: 1 = Ctrl+C-ish (graceful), 2 = console close / shutdown
                              // RU: 1 = подобие Ctrl+C (мягко), 2 = закрытие консоли / выключение
volatile int g_finished;      // EN: stop files written (guarded by finish_lock)
                              // RU: stop-файлы записаны (под finish_lock)

// EN: The console-close handler calls this to save state from its own thread; main wires it to
// EN: save_all_and_report(). NULL until then: no handler is installed before main sets it, so the
// EN: hook is always in place whenever the handler can fire.
// RU: Обработчик закрытия консоли вызывает это, чтобы сохранить состояние из своего потока; main
// RU: подключает сюда save_all_and_report(). До этого NULL: обработчик не устанавливается раньше,
// RU: поэтому хук всегда на месте, когда обработчик может сработать.
static void (*emergency_save)(const char *why, int closing);

void set_emergency_save(void (*fn)(const char *why, int closing)) {
    emergency_save = fn;
}

static void on_posix_signal(int sig) {
    // EN: SIGINT/SIGTERM: the main loop saves state at the next batch boundary and exits.
    // EN: SIGHUP (terminal gone): treat like a close, the loop may not get another chance to print.
    // RU: SIGINT/SIGTERM: главный цикл сохраняет состояние на следующей границе пакета и выходит.
    // RU: SIGHUP (терминал пропал): считаем закрытием, у цикла может не быть шанса напечатать ещё раз.
#ifdef SIGHUP
    g_stop = sig == SIGHUP ? 2 : 1;
#else
    (void)sig;
    g_stop = 1;
#endif
}

#ifdef _WIN32
static BOOL WINAPI win_ctrl_handler(DWORD ev) {
    if (ev == CTRL_C_EVENT || ev == CTRL_BREAK_EVENT) { g_stop = 1; return TRUE; } // EN: graceful stop
                                                                                   // RU: мягкая остановка
    // EN: Close/logoff/shutdown: Windows kills the process soon after the handler returns. Give the
    // EN: main loop a moment to finish its batch and save; if it does not make it, save here (the
    // EN: save path uses only mutexes, plain buffers and raw I/O, which is safe in this thread).
    // RU: Закрытие/выход из системы/выключение: Windows убивает процесс вскоре после возврата из
    // RU: обработчика. Даём главному циклу время завершить пакет и сохранить; если не успевает,
    // RU: сохраняем здесь (путь сохранения использует только мьютексы, обычные буферы и сырой I/O,
    // RU: что безопасно в этом потоке).
    g_stop = 2;
    for (int i = 0; i < 40 && !g_finished; i++) Sleep(80);
    if (!g_finished && emergency_save)
        emergency_save(ev == CTRL_CLOSE_EVENT ? tr("console closed", "окно консоли закрыто")
                                              : tr("session ended by system", "сессия завершена системой"), 1);
    return FALSE;
}
#endif

void install_signal_handlers(void) {
#ifdef _WIN32
    SetConsoleCtrlHandler(win_ctrl_handler, TRUE);
    signal(SIGINT, on_posix_signal);
    signal(SIGTERM, on_posix_signal);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_posix_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
#endif
}
