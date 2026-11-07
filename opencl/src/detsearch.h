// EN: detsearch.h - deterministic, resumable search state: index -> entropy and the cursor bookkeeping
// EN: that the checkpoint stores.
// RU: detsearch.h - состояние детерминированного возобновляемого поиска: индекс -> энтропия и учёт
// RU: курсоров, который сохраняет checkpoint.
#ifndef SV_DETSEARCH_H
#define SV_DETSEARCH_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int enabled;
    // EN: secret search key, CSPRNG, stored only in the checkpoint
    // RU: секретный ключ поиска, CSPRNG, хранится только в checkpoint
    uint8_t key[32];
    uint64_t start;     // EN: first index of the job (--start-offset) | RU: первый индекс задания (--start-offset)
    uint64_t cursor;    // EN: next index handed to a batch (may be ahead of s1_next)
                        // RU: следующий индекс, выданный пакету (может опережать s1_next)
    // EN: first index whose stage-1 decision is not yet made (ton-24: the basic-seed check; bip39: the
    // EN: address check). Survivors of [next, s1_next) are never stored: stage 1 is deterministic, a
    // EN: resume regenerates them from the indices.
    // RU: первый индекс, по которому решение стадии 1 ещё не принято (ton-24: проверка basic-seed;
    // RU: bip39: проверка адреса). Выжившие из [next, s1_next) не сохраняются: стадия 1
    // RU: детерминирована, при продолжении они восстанавливаются из индексов.
    uint64_t s1_next;
    // EN: first index NOT fully processed, i.e. address-checked (the resume point);
    // EN: in bip39 modes always equal to s1_next
    // RU: первый индекс, НЕ обработанный полностью, то есть не прошедший проверку адреса (точка
    // RU: продолжения); в режимах bip39 всегда равен s1_next
    uint64_t next;
    uint64_t end;       // EN: exclusive upper bound; valid when has_end (0 = unlimited)
                        // RU: верхняя граница, не включая; действует при has_end (0 = без границы)
    int has_end;
    // EN: ton-24 after a crash: regenerate survivors of [base, end)
    // RU: ton-24 после сбоя: заново получить выживших из [base, end)
    uint64_t backfill_base, backfill_end;
    char checkpoint_path[4096];
    double last_save; // EN: last successful checkpoint write | RU: время последней успешной записи checkpoint
} DetSearch;

extern DetSearch det;

// EN: Candidate entropy for index: HMAC-SHA512(searchKey, "svDET1" || mode || be64 index), truncated.
// EN: The prefix keeps this construction separate from every other use of HMAC in the program; the mode
// EN: byte keeps entropy spaces of bip39-12/bip39-24/ton-24 disjoint. Same key + index = same candidate.
// RU: Энтропия кандидата для индекса: HMAC-SHA512(searchKey, "svDET1" || mode || be64 index), усечённая.
// RU: Префикс отделяет это построение от любого другого использования HMAC в программе; байт режима
// RU: делает пространства энтропии bip39-12/bip39-24/ton-24 непересекающимися. Тот же ключ + индекс =
// RU: тот же кандидат.
void det_entropy_ex(const uint8_t key[32], int mode, uint64_t index, uint8_t *out, size_t n);
void det_entropy(uint64_t index, uint8_t *out, size_t n);

#endif
