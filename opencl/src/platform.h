// EN: platform.h - OS portability layer: diagnostics, time, CSPRNG, filesystem, threads, signals.
// EN: Every #ifdef _WIN32 fork of the program lives in platform.c; the rest of the code stays
// EN: platform-neutral.
// RU: platform.h - слой переносимости ОС: диагностика, время, CSPRNG, файловая система, потоки,
// RU: сигналы. Все ветки #ifdef _WIN32 живут в platform.c; остальной код не зависит от платформы.
#ifndef SV_PLATFORM_H
#define SV_PLATFORM_H

#include <signal.h> // sig_atomic_t
#include <stddef.h> // size_t
#include <stdint.h> // uint8_t, uint64_t

#ifdef _WIN32
#include <windows.h> // mutex_t = CRITICAL_SECTION
typedef CRITICAL_SECTION mutex_t;
#else
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#endif

void die(const char *fmt, ...) __attribute__((noreturn));
double now_sec(void);
void sleep_ms(int ms);
void clock_str(char *buf, size_t n);
// EN: Human-readable duration for the wait estimate: "42 s", "52 min", "3.5 h", "2.1 days", "4.2 years".
// RU: Читаемая длительность для оценки ожидания: "42 s", "52 min", "3.5 h", "2.1 days", "4.2 years".
void fmt_wait(double sec, char *buf, size_t n);
// EN: UTC timestamp for file names: 20261012-153045
// RU: метка времени UTC для имён файлов: 20261012-153045
void ts_name(char *buf, size_t n);
// EN: UTC timestamp for file contents: 2026-10-12T15:30:45Z
// RU: метка времени UTC для содержимого файлов: 2026-10-12T15:30:45Z
void ts_iso(char *buf, size_t n);
// EN: Cryptographically secure randomness from the operating system.
// RU: Криптостойкая случайность от операционной системы.
void random_bytes(uint8_t *p, size_t n);
int cpu_count(void);
// EN: Directory that contains the executable (the tool lives in <root>/opencl/).
// RU: Каталог, содержащий исполняемый файл (инструмент лежит в <root>/opencl/).
void exe_dir(char *out, size_t n);
void make_dir(const char *dir);
int dir_exists(const char *dir);
// EN: Creates a directory and any missing parents (best effort; a failure is reported by the first write).
// RU: Создаёт каталог и все недостающие родительские (по возможности; о сбое сообщит первая запись).
void mkdir_p(const char *dir);
// EN: Creates a new file with owner-only permissions and writes text into it (raw I/O, no stdio
// EN: buffers, so it is safe to call from the Windows console-control handler).
// RU: Создаёт новый файл с правами только для владельца и пишет в него текст (сырой I/O без буферов
// RU: stdio, поэтому безопасно из обработчика консоли Windows).
int write_new_secret(const char *path, const uint8_t *data, size_t len);
// EN: Atomic durable write: <path>.tmp then rename over <path>. Used for checkpoints, so a crash or
// EN: Ctrl+C in the middle of a save can never leave a half-written state file.
// RU: Атомарная надёжная запись: <path>.tmp, затем переименование поверх <path>. Используется для
// RU: checkpoint, чтобы сбой или Ctrl+C посреди сохранения не оставили недописанный файл состояния.
int write_file_atomic(const char *path, const uint8_t *data, size_t len);
// EN: dir/name, made unique with a -2, -3 ... suffix if the file already exists.
// RU: dir/name, уникализируется суффиксом -2, -3 ... если файл уже существует.
void unique_path(char *out, size_t n, const char *dir, const char *name);
int parse_u64(const char *s, uint64_t *out);
int add_ovf_u64(uint64_t a, uint64_t b, uint64_t *r);

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
// EN: Runs fn(ctx, tid, nt) on nt threads and waits for all of them.
// RU: Запускает fn(ctx, tid, nt) в nt потоках и ждёт их все.
void parallel(int nt, void (*fn)(void *, int, int), void *ctx);

// EN: Stop flags. g_stop: 1 = Ctrl+C-ish (graceful), 2 = console close / shutdown.
// RU: Флаги остановки. g_stop: 1 = подобие Ctrl+C (мягко), 2 = закрытие консоли / выключение.
extern volatile sig_atomic_t g_stop;
// EN: stop files written (guarded by finish_lock) | RU: stop-файлы записаны (под finish_lock)
extern int g_finished;
void install_signal_handlers(void);
// EN: The Windows console-close handler must save state from its own thread; main wires this hook to
// EN: save_all_and_report() once the paths are known, keeping platform.c free of higher layers.
// RU: Обработчик закрытия консоли Windows должен сохранять состояние из своего потока; main
// RU: подключает сюда save_all_and_report(), когда пути известны, и platform.c не знает о верхних слоях.
void set_emergency_save(void (*fn)(const char *why, int closing));

#endif
