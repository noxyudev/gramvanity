// EN: crypto.h - CPU crypto helpers shared by the pipelines, the GPU layer, the hit re-derivation and
// EN: the self-tests: PBKDF2-HMAC-SHA512, SLIP-10 ed25519, BIP39/TON mnemonic handling and the GPU-side
// EN: block packing. The heavy hashing is Monocypher (crypto_*) and sha256.c; the Ed25519 point
// EN: arithmetic is ed25519-donna via ed25519_fast.c.
// RU: crypto.h - криптографические помощники CPU для конвейеров, слоя GPU, повторного вывода хитов и
// RU: self-тестов: PBKDF2-HMAC-SHA512, SLIP-10 ed25519, работа с мнемониками BIP39/TON и упаковка
// RU: блоков для GPU. Основное хеширование — Monocypher (crypto_*) и sha256.c; арифметика точек
// RU: Ed25519 — ed25519-donna через ed25519_fast.c.
#ifndef SV_CRYPTO_H
#define SV_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

// EN: TON/BIP39 salt strings used by PBKDF2 (GPU buffers are built from them with salt_block).
// RU: Строки соли TON/BIP39 для PBKDF2 (буферы GPU строятся из них через salt_block).
extern const uint8_t SALT_MNEMONIC[];      // "mnemonic"
extern const uint8_t SALT_TON_VERSION[];   // "TON seed version"
extern const uint8_t SALT_TON_DEFAULT[];   // "TON default seed"
// EN: max(1, 100000 / 256) | RU: max(1, 100000 / 256)
#define TON_BASIC_ITERS 390
#define TON_ITERS 100000

void hmac512(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[64]);
// EN: Reference PBKDF2-HMAC-SHA512 on the CPU (one 64-byte block): self-test and hit re-derivation.
// RU: Эталонный PBKDF2-HMAC-SHA512 на CPU (один 64-байтный блок): self-test и повторный вывод хитов.
void cpu_pbkdf2(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t iters, uint8_t out[64]);
// EN: SLIP-0010 ed25519, path m/44'/607'/0' | RU: SLIP-0010 ed25519, путь m/44'/607'/0'
void cpu_slip10(const uint8_t seed[64], uint8_t out[32]);

// EN: CPU Ed25519 from ed25519_fast.c (vendored ed25519-donna): the GPU derives the keys of the search,
// EN: this is the independent check of a hit and the self-test reference.
// RU: Ed25519 на CPU из ed25519_fast.c (вендоренный ed25519-donna): ключи поиска выводит GPU, а это
// RU: независимая проверка хита и эталон для self-теста.
void ed25519_public_fast(uint8_t pub[32], const uint8_t seed[32]);
// EN: Fixed-base table for the GPU Ed25519 kernel: tab[0..9] = 2d, then 512 points (x, y, 2d*x*y).
// RU: Таблица фиксированной базы для GPU-ядра Ed25519: tab[0..9] = 2d, затем 512 точек (x, y, 2d*x*y).
void ed25519_gpu_table(int32_t *tab);
#define ED_TAB_WORDS (10 + 64 * 8 * 30)

void ed25519_public(const uint8_t priv[32], uint8_t pub[32]);

// EN: BIP39 key of a 12/24-word phrase: PBKDF2-HMAC-SHA512 ("mnemonic", 2048) then SLIP-10 m/44'/607'/0'.
// RU: Ключ BIP39 фразы из 12/24 слов: PBKDF2-HMAC-SHA512 ("mnemonic", 2048), затем SLIP-10 m/44'/607'/0'.
void bip39_key(const char *phrase, size_t len, uint8_t priv[32]);

// EN: 11-bit word indices from a bit string
// RU: 11-битные индексы слов из битовой строки
void indices11(const uint8_t *b, int count, int *out);
void bip39_indices(const uint8_t *ent, int entlen, int *out);
// EN: BIP39 checksum validity of an arbitrary 12- or 24-word sequence
// RU: корректность контрольной суммы BIP39 для произвольной последовательности из 12 или 24 слов
int bip39_valid(const int *idx, int nw);
// EN: Words joined with spaces; returns the length without the terminator.
// RU: Слова, соединённые пробелами; возвращает длину без завершающего нуля.
size_t phrase_bytes(const int *idx, int nw, char *out);

void ton_entropy(const char *phrase, size_t len, uint8_t out[64]);
// EN: TON standard validity (what @ton/crypto mnemonicValidate checks, without a password)
// RU: корректность по стандарту TON (то, что проверяет @ton/crypto mnemonicValidate, без пароля)
int ton_valid(const char *phrase, size_t len);

// EN: HMAC key zero-padded to one 128-byte block as 16 big-endian words (long keys pre-hashed)
// RU: Ключ HMAC, дополненный нулями до одного 128-байтного блока как 16 big-endian слов (длинные ключи
// RU: предварительно хешируются)
void key_block(const uint8_t *pw, size_t len, uint64_t *w);
void salt_block(const uint8_t *salt, size_t slen, uint64_t *w);

#endif
