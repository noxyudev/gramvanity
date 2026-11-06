// EN: checkpoint.h - the binary checkpoint format (v8, reads v2-v7): serialization, atomic durable
// EN: save, parsing, and the resume-side reconstruction of targets and job folders from a checkpoint.
// RU: checkpoint.h - двоичный формат checkpoint (v8, читает v2-v7): сериализация, атомарное надёжное
// RU: сохранение, разбор и восстановление целей и папок задания из checkpoint при продолжении.
#ifndef SV_CHECKPOINT_H
#define SV_CHECKPOINT_H

#include <stddef.h>
#include <stdint.h>

#include "platform.h" // mutex_t
#include "pattern.h"  // MAX_TARGET_NAME, MAX_TARGET_LABEL

// EN: 8 bytes with the trailing NUL | RU: 8 байт с завершающим NUL
#define CKPT_MAGIC "SVANCKP"
// EN: 8 = target label (UTF-8) next to the ASCII id; 7 = --all-patterns flag + per-pattern saved-hit
// EN: counters; 6 = --match or target flag; 5 = target blocks; 4 = prefixes; 3 = before prefixes;
// EN: 2 = before tg-wallet
// RU: 8 = метка цели (UTF-8) рядом с ASCII-идентификатором; 7 = флаг --all-patterns + счётчики
// RU: сохранённых хитов по шаблонам; 6 = флаг цели --match or; 5 = блоки --target; 4 = префиксы;
// RU: 3 = до префиксов; 2 = до tg-wallet
#define CKPT_VERSION 8

// EN: Guards checkpoint_save() against concurrent saves (main loop, autosave and console handler).
// RU: Защищает checkpoint_save() от одновременных сохранений (главный цикл, автосохранение, обработчик консоли).
extern mutex_t ckpt_lock;

// EN: Little-endian writers, also used by the deterministic-search unit tests to build legacy buffers.
// RU: Запись little-endian; используется и unit-тестами детерминированного поиска для сборки старых буферов.
void le32p(uint8_t *p, uint32_t v);
void le64p(uint8_t *p, uint64_t v);

typedef struct {
    char name[MAX_TARGET_NAME];
    char label[MAX_TARGET_LABEL];
    char out[4096];
    // EN: bit0 ignore-case, bit1 count given, bit2 legacy OR, bit3 --match or, bit4 --all-patterns
    // RU: bit0 ignore-case, bit1 count задан, bit2 legacy OR, bit3 --match or, bit4 --all-patterns
    uint32_t flags;
    uint64_t count, hits;
    char **suffix;
    uint32_t n_suffix;
    char **prefix;
    uint32_t n_prefix;
    // EN: per-pattern saved hits (v7); NULL in older formats
    // RU: сохранённые хиты по шаблонам (v7); NULL в старых форматах
    uint64_t *suf_hits, *pre_hits;
} CkptTarget;

struct CkptIn {
    uint32_t mode, flags;
    char job_dir[4096];
    CkptTarget *targets;
    uint32_t n_targets;
    uint64_t start, next, s1_next, end, done, unique_hits, verified;
    uint8_t key[32];
};

void ckpt_in_free(struct CkptIn *in);

// EN: Writes the checkpoint durably and atomically. Safe to call from any thread, including the
// EN: Windows console-control handler (only mutex + raw I/O). No-op unless the search is deterministic.
// RU: Пишет checkpoint надёжно и атомарно. Безопасно из любого потока, включая обработчик консоли
// RU: Windows (только мьютекс и сырой I/O). Ничего не делает, если поиск не детерминированный.
int checkpoint_save(void);

// EN: Reads and validates a checkpoint file (any supported format version) into *in.
// RU: Читает и проверяет файл checkpoint (любая поддерживаемая версия формата) в *in.
int ckpt_load(const char *path, struct CkptIn *in, char *err, size_t errn);
// EN: Parses an in-memory checkpoint buffer (used by ckpt_load and the deterministic-search unit tests,
// EN: which build synthetic legacy buffers directly).
// RU: Разбирает буфер checkpoint в памяти (используется ckpt_load и unit-тестами детерминированного
// RU: поиска, которые собирают синтетические старые буферы напрямую).
int ckpt_parse(const uint8_t *buf, size_t len, struct CkptIn *in, char *err, size_t errn);

// EN: Sets job_dir from the checkpoint. v2-v4 files predate the job folder: their report and hit files
// EN: lived next to the checkpoint, so use its directory. --out on resume moves the job and every
// EN: target that shared the old job folder; per-target hit directories stay where they are.
// RU: Задаёт job_dir из checkpoint. Файлы v2-v4 появились до папки задания: их отчёт и hit-файлы
// RU: лежали рядом с checkpoint, поэтому берём его каталог. --out при продолжении переносит задание и
// RU: каждую цель, делившую старую папку; каталоги хитов отдельных целей остаются на месте.
void job_dir_from_checkpoint(struct CkptIn *in, const char *resume_path);

// EN: Applies --target blocks given together with --resume. A block selects a checkpoint target by name
// EN: (or, when unnamed, the next unused one) and may change its count and hit directory. Patterns and
// EN: ignore-case must describe the same search, otherwise old and new hits would be mixed.
// RU: Применяет блоки --target, заданные вместе с --resume. Блок выбирает цель checkpoint по имени
// RU: (или, если имя не задано, следующую неиспользованную) и может изменить её count и каталог хитов.
// RU: Шаблоны и ignore-case должны описывать тот же поиск, иначе старые и новые хиты смешаются.
void resume_apply_target_overrides(struct CkptIn *in);

// EN: Rebuilds the target table from checkpoint records. The patterns in the checkpoint are already
// EN: compiled group lists (prefixes and endings), so target_compile reproduces their exact semantics.
// RU: Восстанавливает таблицу целей из записей checkpoint. Шаблоны в checkpoint — уже скомпилированные
// RU: списки групп (префиксы и окончания), поэтому target_compile воспроизводит их точную семантику.
void targets_from_checkpoint(struct CkptIn *in);

#endif
