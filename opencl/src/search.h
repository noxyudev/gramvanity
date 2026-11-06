// EN: search.h - the candidate pipelines that feed the GPU (BIP39 single-stage, TON two-stage with
// EN: survivors) and the deterministic index-range bookkeeping that commits progress to the checkpoint.
// RU: search.h - конвейеры кандидатов для GPU (BIP39 в одну стадию, TON в две стадии с выжившими) и
// RU: учёт диапазона индексов, который фиксирует прогресс в checkpoint.
#ifndef SV_SEARCH_H
#define SV_SEARCH_H

#include <stdint.h>

#include "gpu.h" // Job

typedef struct {
    uint32_t n;
    // EN: index of candidate 0 (deterministic mode; 0 in random mode)
    // RU: индекс кандидата 0 (детерминированный режим; 0 в режиме --random)
    uint64_t base;
    int entlen, nw;
    uint8_t *ent;   // n * entlen
    uint64_t *keys; // n * 16
    // EN: n * 4 (big-endian words) | RU: n * 4 (big-endian слова)
    uint64_t *priv;
    // EN: n * 32 (GPU Ed25519 public keys) | RU: n * 32 (публичные ключи Ed25519 с GPU)
    uint8_t *pub;
    Job job;
} BipBatch;

typedef struct {
    int idx[24];
    uint8_t ent[64];
    uint64_t index;
} TonCand;

typedef struct {
    uint32_t n;
    uint64_t base;
    // EN: n * 33 (random mode only) | RU: n * 33 (только режим --random)
    uint8_t *rnd;
    TonCand *cand;      // n
    uint64_t *keys;     // n * 16
    uint64_t *out;      // n * out_words
    // EN: n * 32 (stage 2 only) | RU: n * 32 (только стадия 2)
    uint8_t *pub;
} TonBatch;

// EN: Stage-1 survivors of the current ton-24 search, FIFO index order, flushed by ton_flush.
// RU: Выжившие стадии 1 текущего поиска ton-24, в порядке индексов (FIFO), сбрасываются ton_flush.
extern TonCand *survivors;
extern uint32_t n_surv, cap_surv;

// EN: Builds one BIP39 batch: entropy (deterministic or random), phrases, PBKDF2 key blocks, GPU job.
// RU: Собирает один пакет BIP39: энтропия (детерминированная или случайная), фразы, блоки ключей
// RU: PBKDF2, задание GPU.
BipBatch *bip_new(uint32_t n, uint64_t base);
void bip_free(BipBatch *b);

// EN: Thread callbacks of the pipelines; the main loop drives them stage by stage for double buffering.
// RU: Потоковые колбэки конвейеров; главный цикл ведёт их по стадиям для двойной буферизации.
void bip_prep_fn(void *p, int tid, int nt);
void bip_post_fn(void *p, int tid, int nt);
void ton_prep_fn(void *p, int tid, int nt);
void ton_post_fn(void *p, int tid, int nt);

// EN: Stage 2 for the first min(n_survivors, maxn) survivors (FIFO index order): key derivation and
// EN: the address check. Commits those indices as fully processed.
// RU: Стадия 2 для первых min(n_survivors, maxn) выживших (в порядке индексов): вывод ключа и проверка
// RU: адреса. Отмечает эти индексы как полностью обработанные.
void ton_flush(uint32_t maxn);

// EN: Hands out the next [base, base+*got) slice of the index range. *got = 0 means the range is
// EN: exhausted. In random mode there is no range and the requested size is always granted.
// RU: Выдаёт следующий срез [base, base+*got) диапазона индексов. *got = 0 означает, что диапазон
// RU: исчерпан. В режиме --random диапазона нет, и запрошенный размер выдаётся всегда.
uint64_t det_alloc(uint32_t want, uint32_t *got);

// EN: Marks every index < upto as stage-1-decided (ton-24: basic-seed check passed/failed; bip39:
// EN: address checked). Batches commit in index order, so the cursors never run ahead of finished work.
// RU: Отмечает все индексы < upto как решённые стадией 1 (ton-24: проверка basic-seed пройдена/не
// RU: пройдена; bip39: адрес проверен). Пакеты фиксируются в порядке индексов, поэтому курсоры не
// RU: опережают завершённую работу.
void det_commit_s1(uint64_t upto);

// EN: Marks every index < upto as fully processed (address checked). In bip39 modes the address check
// EN: IS stage 1, so the same boundary commits both cursors.
// RU: Отмечает все индексы < upto как полностью обработанные (адрес проверен). В режимах bip39
// RU: проверка адреса И ЕСТЬ стадия 1, поэтому одна граница фиксирует оба курсора.
void det_commit(uint64_t upto);

// EN: Big-endian 64-bit words to bytes (the GPU PBKDF2 output layout).
// RU: Big-endian 64-битные слова в байты (раскладка вывода PBKDF2 на GPU).
void words_to_bytes(const uint64_t *w, int nwords, uint8_t *out);

#endif
