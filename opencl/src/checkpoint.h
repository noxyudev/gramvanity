// EN: checkpoint.h - the text checkpoint format ("gramvanity-checkpoint" v1): the whole search state
// EN: serialized as one "key = value" per line, atomic durable saves, strict parsing, and the
// EN: resume-side reconstruction of targets and job folders from a checkpoint. The file is plain
// EN: UTF-8 text: it can be read and hand-edited in any editor; after editing, run --checkpoint-fix
// EN: to validate the fields and re-sign the file.
// RU: checkpoint.h - текстовый формат checkpoint ("gramvanity-checkpoint" v1): всё состояние поиска,
// RU: сериализованное по одному "ключ = значение" на строку, атомарные надёжные записи, строгий
// RU: разбор и восстановление целей и папок задания из checkpoint при продолжении. Файл — обычный
// RU: UTF-8-текст: его можно читать и править в любом редакторе; после правки запустите
// RU: --checkpoint-fix, чтобы проверить поля и пересчитать подпись файла.
#ifndef SV_CHECKPOINT_H
#define SV_CHECKPOINT_H

#include <stddef.h>
#include <stdint.h>

#include "platform.h" // mutex_t
#include "pattern.h"  // MAX_TARGET_NAME, MAX_TARGET_LABEL

// EN: The first significant line of every checkpoint file declares `format = <CHECKPOINT_FORMAT_ID>`;
// EN: `version` must be CHECKPOINT_VERSION. The format id, the keys and the generated comments stay
// EN: English, like every other machine-readable contract of the program.
// RU: Первая значащая строка каждого файла checkpoint объявляет `format = <CHECKPOINT_FORMAT_ID>`;
// RU: `version` должен быть CHECKPOINT_VERSION. Идентификатор формата, ключи и сгенерированные
// RU: комментарии остаются английскими, как и любой другой машиночитаемый контракт программы.
#define CHECKPOINT_FORMAT_ID "gramvanity-checkpoint"
#define CHECKPOINT_VERSION 1

// EN: Guards checkpoint_save() against concurrent saves (main loop, autosave and console handler).
// RU: Защищает checkpoint_save() от одновременных сохранений (главный цикл, автосохранение, обработчик консоли).
extern mutex_t checkpoint_lock;

typedef struct {
    char name[MAX_TARGET_NAME];
    char label[MAX_TARGET_LABEL];
    char out[4096];
    // EN: bit0 ignore-case, bit1 count given, bit2 --match or, bit3 --all-patterns
    // RU: bit0 ignore-case, bit1 count задан, bit2 --match or, bit3 --all-patterns
    uint32_t flags;
    uint64_t count, hits;
    char **suffix;
    uint32_t n_suffix;
    char **prefix;
    uint32_t n_prefix;
    // EN: saved hits per pattern; NULL when the list is empty
    // RU: сохранённые хиты по шаблонам; NULL, когда список пуст
    uint64_t *suf_hits, *pre_hits;
} CheckpointTarget;

struct Checkpoint {
    uint32_t mode; // EN: BIP12/BIP24/TON24/TG | RU: BIP12/BIP24/TON24/TG
    // EN: bit0 bounceable (EQ/kQ form), bit1 testnet (only defined for tg-wallet)
    // RU: bit0 bounceable (форма EQ/kQ), bit1 testnet (определён только для tg-wallet)
    uint32_t flags;
    char job_dir[4096];
    CheckpointTarget *targets;
    uint32_t n_targets;
    uint64_t start, next, s1_next, end, done, unique_hits, verified;
    uint8_t key[32]; // EN: search key, SECRET | RU: ключ поиска, СЕКРЕТ
};

void checkpoint_free(struct Checkpoint *in);

// EN: Writes the checkpoint durably and atomically. Safe to call from any thread, including the
// EN: Windows console-control handler (mutex + raw I/O only; the serialize buffer and the state
// EN: snapshot are allocated once and reused). No-op unless the search is deterministic.
// RU: Пишет checkpoint надёжно и атомарно. Безопасно из любого потока, включая обработчик консоли
// RU: Windows (только мьютекс и сырой I/O; буфер сериализации и снимок состояния выделяются один
// RU: раз и переиспользуются). Ничего не делает, если поиск не детерминированный.
int checkpoint_save(void);

// EN: Reads and validates a checkpoint file into *in. Every line of the file is covered by the
// EN: SHA-256 `checksum` line; a mismatch or a missing checksum is an error.
// RU: Читает и проверяет файл checkpoint в *in. Каждая строка файла накрыта контрольной суммой
// RU: SHA-256 в строке `checksum`; несовпадение или отсутствие суммы — ошибка.
int checkpoint_load(const char *path, struct Checkpoint *in, char *err, size_t errn);

// EN: --checkpoint-fix: loads a hand-edited checkpoint ignoring the checksum (every field check
// EN: still applies) and rewrites it with a fresh checksum, normalized quoting, comments and line
// EN: endings. Fills *in with the loaded state for the summary print.
// RU: --checkpoint-fix: загружает отредактированный вручную checkpoint, игнорируя контрольную сумму
// RU: (все остальные проверки полей действуют), и перезаписывает его с новой суммой, нормализованными
// RU: кавычками, комментариями и концами строк. Заполняет *in разобранным состоянием для сводки.
int checkpoint_fix(const char *path, struct Checkpoint *in, char *err, size_t errn);

// EN: --checkpoint-merge: builds a new checkpoint at out_path from the targets of one or more
// EN: existing checkpoints that have not reached their --count yet. The result is a new job: a
// EN: fresh search key, the search position restarts from index 0 and the job counters are zeroed,
// EN: because candidates are derived from the key and nothing has been searched under the new key
// EN: yet. Done targets are dropped: their hits stay recoverable only from the original files via
// EN: --reveal. Targets with the same name and the same patterns combine into one by taking the
// EN: per-field maximum (count, hits, per-pattern hits; unlimited wins over any count); a name
// EN: reused with different patterns gets a -2, -3... suffix. All inputs must share the mode and
// EN: the wallet form/network. job_dir and the per-target out lines are not stored: the merged
// EN: file's own folder is the job folder, like for a hand-written file. *out receives the merged
// EN: state for the caller's summary (free with checkpoint_free); *dropped and *renamed, when not
// EN: NULL, receive the target counts. The file is written only after every input has loaded.
// RU: --checkpoint-merge: собирает новый checkpoint в out_path из целей одного или нескольких
// RU: существующих checkpoint, ещё не достигших своего --count. Результат — новое задание: свежий
// RU: ключ поиска, позиция поиска начинается с индекса 0, счётчики задания обнулены, ведь
// RU: кандидаты выводятся из ключа, и под новым ключом ещё ничего не искали. Выполненные цели
// RU: отбрасываются: их хиты остаются восстановимыми только из исходных файлов через --reveal.
// RU: Цели с одинаковым именем и одинаковыми шаблонами склеиваются в одну по максимуму полей
// RU: (count, hits, хиты по шаблонам; unlimited сильнее любого count); повторно использованное
// RU: имя с другими шаблонами получает суффикс -2, -3... Все входы должны совпадать по режиму,
// RU: форме и сети кошелька. Строки job_dir и out у целей не сохраняются: папка самого
// RU: объединённого файла становится папкой задания, как у файла, собранного вручную. *out
// RU: получает объединённое состояние для сводки вызывающей стороны (освободить через
// RU: checkpoint_free); *dropped и *renamed, если не NULL, получают числа целей. Файл пишется
// RU: только после успешной загрузки всех входов.
int checkpoint_merge(const char *const *inputs, int n_inputs, const char *out_path,
                     struct Checkpoint *out, int *dropped, int *renamed,
                     char *err, size_t errn);

// EN: Sets job_dir from the checkpoint. A checkpoint without a job folder (a hand-written file)
// EN: uses the checkpoint file's own directory. --out on resume moves the job and every target that
// EN: shared the old job folder; per-target hit directories stay where they are.
// RU: Задаёт job_dir из checkpoint. Checkpoint без папки задания (файл, собранный вручную) берёт
// RU: каталог самого файла. --out при продолжении переносит задание и каждую цель, делившую старую
// RU: папку; каталоги хитов отдельных целей остаются на месте.
void job_dir_from_checkpoint(struct Checkpoint *in, const char *resume_path);

// EN: Applies --target blocks given together with --resume. A block selects a checkpoint target by name
// EN: (or, when unnamed, the next unused one) and may change its count and hit directory. Patterns and
// EN: ignore-case must describe the same search, otherwise old and new hits would be mixed.
// RU: Применяет блоки --target, заданные вместе с --resume. Блок выбирает цель checkpoint по имени
// RU: (или, если имя не задано, следующую неиспользованную) и может изменить её count и каталог хитов.
// RU: Шаблоны и ignore-case должны описывать тот же поиск, иначе старые и новые хиты смешаются.
void resume_apply_target_overrides(struct Checkpoint *in);

// EN: Rebuilds the target table from checkpoint records. The patterns in the checkpoint are already
// EN: compiled group lists (prefixes and endings), so target_compile reproduces their exact semantics.
// RU: Восстанавливает таблицу целей из записей checkpoint. Шаблоны в checkpoint — уже скомпилированные
// RU: списки групп (префиксы и окончания), поэтому target_compile воспроизводит их точную семантику.
void targets_from_checkpoint(struct Checkpoint *in);

#endif
