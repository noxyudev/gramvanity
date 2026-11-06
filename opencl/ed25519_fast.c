// EN: Ed25519 on the CPU: the independent check of every hit, and the table for the GPU kernel.
// RU: Ed25519 на CPU: независимая проверка каждого хита и таблица для ядра GPU.
//
// EN: The search derives public keys on the GPU (ed25519_gpu.cl). Every hit is derived again from its
// EN: phrase (Monocypher PBKDF2/SLIP-10 in src/crypto.c, then ed25519_public_fast below); both the
// EN: private and the public key must match the GPU result.
// RU: Поиск выводит публичные ключи на GPU (ed25519_gpu.cl). Каждый хит выводится заново из своей
// RU: фразы (PBKDF2/SLIP-10 Monocypher в src/crypto.c, затем ed25519_public_fast ниже); и приватный,
// RU: и публичный ключ должны совпасть с результатом GPU.
//
// EN: ed25519_public_fast uses the fixed-base scalar multiplication of Andrew Moon's ed25519-donna
// EN: (the portable 5x51-bit "64-bit" path, vendored in third_party/ed25519). Hashing stays on
// EN: Monocypher, so the result is the standard Ed25519 key pair: SHA-512(seed) -> clamp -> [a]B ->
// EN: compressed point, identical to every other correct implementation.
// RU: ed25519_public_fast использует умножение на фиксированную базу из ed25519-donna Эндрю Муна
// RU: (переносимый путь 5x51 бит "64-bit", вендорен в third_party/ed25519). Хеширование остаётся на
// RU: Monocypher, поэтому результат — стандартная пара ключей Ed25519: SHA-512(seed) -> clamp -> [a]B ->
// RU: сжатая точка, как в любой другой корректной реализации.
//
// EN: ed25519_gpu_table builds the fixed-base table for the GPU kernel with the same donna group code:
// EN: T[i][j] = [j*16^i]B in affine coordinates, plus 2d, in the kernel's field layout.
// RU: ed25519_gpu_table строит таблицу фиксированной базы для ядра GPU тем же групповым кодом donna:
// RU: T[i][j] = [j*16^i]B в аффинных координатах, плюс 2d, в раскладке поля ядра.
//
// Vendored code: public domain by Andrew M. (liquidsun@gmail.com), modified from the
// amd64-51-30k implementation by Bernstein, Duif, Lange, Schwabe and Yang;
// see third_party/ed25519/README.md (attribution in the top-level NOTICE).

#include <stdint.h>

// EN: ed25519-donna is header-only: it defines many static helpers, and a given translation
// EN: unit uses only a few of them.
// RU: ed25519-donna состоит только из заголовков: он определяет много static-помощников, а
// RU: конкретная единица трансляции использует лишь несколько из них.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "third_party/ed25519/ed25519-donna.h"
#include "third_party/monocypher/monocypher.h"
#include "third_party/monocypher/monocypher-ed25519.h"

#if !defined(ED25519_64BIT)
#error "ed25519_fast.c needs the 64-bit ed25519-donna path (no 32-bit targets)"
#endif

// EN: The OpenCL kernel uses the ref10 radix-2^25.5 layout: ten signed 32-bit limbs, limbs
// EN: centred in [-2^25, 2^25). Pack a canonical little-endian field element into it.
// RU: Ядро OpenCL использует раскладку ref10 radix-2^25.5: десять знаковых 32-битных лимбов,
// RU: центрированных в [-2^25, 2^25). Упаковываем канонический little-endian элемент поля.
static void fe10_frombytes(int32_t h[10], const uint8_t s[32]) {
#define LD3(p) ((int64_t)((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16)))
#define LD4(p) ((int64_t)((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24)))
    int64_t h0 = LD4(s);
    int64_t h1 = LD3(s + 4) << 6;
    int64_t h2 = LD3(s + 7) << 5;
    int64_t h3 = LD3(s + 10) << 3;
    int64_t h4 = LD3(s + 13) << 2;
    int64_t h5 = LD4(s + 16);
    int64_t h6 = LD3(s + 20) << 7;
    int64_t h7 = LD3(s + 23) << 5;
    int64_t h8 = LD3(s + 26) << 4;
    int64_t h9 = (LD3(s + 29) & 8388607) << 2;
    int64_t c;
    c = (h0 + (1 << 25)) >> 26; h1 += c; h0 -= c << 26;
    c = (h4 + (1 << 25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h1 + (1 << 24)) >> 25; h2 += c; h1 -= c << 25;
    c = (h5 + (1 << 24)) >> 25; h6 += c; h5 -= c << 25;
    c = (h2 + (1 << 25)) >> 26; h3 += c; h2 -= c << 26;
    c = (h6 + (1 << 25)) >> 26; h7 += c; h6 -= c << 26;
    c = (h3 + (1 << 24)) >> 25; h4 += c; h3 -= c << 25;
    c = (h7 + (1 << 24)) >> 25; h8 += c; h7 -= c << 25;
    c = (h4 + (1 << 25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h8 + (1 << 25)) >> 26; h9 += c; h8 -= c << 26;
    c = (h9 + (1 << 24)) >> 25; h0 += c * 19; h9 -= c << 25;
    c = (h0 + (1 << 25)) >> 26; h1 += c; h0 -= c << 26;
    h[0] = (int32_t)h0; h[1] = (int32_t)h1; h[2] = (int32_t)h2; h[3] = (int32_t)h3; h[4] = (int32_t)h4;
    h[5] = (int32_t)h5; h[6] = (int32_t)h6; h[7] = (int32_t)h7; h[8] = (int32_t)h8; h[9] = (int32_t)h9;
#undef LD3
#undef LD4
}

// EN: Table for the GPU fixed-base kernel: tab[0..9] = 2d, then 64 windows of 8 points,
// EN: T[i][j-1] = [j*16^i]B in affine coordinates as (x, y, 2d*x*y), 30 limbs per point.
// RU: Таблица для GPU-ядра фиксированной базы: tab[0..9] = 2d, затем 64 окна по 8 точек,
// RU: T[i][j-1] = [j*16^i]B в аффинных координатах как (x, y, 2d*x*y), 30 лимбов на точку.
void ed25519_gpu_table(int32_t *tab) {
    bignum25519 x, y, z, c;
    uint8_t b[32];
    curve25519_contract(b, ge25519_ec2d);
    fe10_frombytes(tab, b);

    bignum256modm one = {1, 0, 0, 0, 0};
    ge25519 P, Q;
    ge25519_scalarmult_base_niels(&P, ge25519_niels_base_multiples, one);
    for (int i = 0; i < 64; i++) {
        ge25519 cur = P;
        for (int j = 1; j <= 8; j++) {
            curve25519_recip(z, cur.z);
            curve25519_mul(x, cur.x, z);
            curve25519_mul(y, cur.y, z);
            curve25519_mul(c, x, y);
            curve25519_mul(c, c, ge25519_ec2d);
            int32_t *e = tab + 10 + (i * 8 + (j - 1)) * 30;
            curve25519_contract(b, x); fe10_frombytes(e, b);
            curve25519_contract(b, y); fe10_frombytes(e + 10, b);
            curve25519_contract(b, c); fe10_frombytes(e + 20, b);
            if (j < 8) { ge25519_add(&Q, &cur, &P); cur = Q; }
        }
        for (int k = 0; k < 4; k++) { ge25519_double(&Q, &P); P = Q; }
    }
}

void ed25519_public_fast(uint8_t pub[32], const uint8_t seed[32]) {
    uint8_t h[64];
    bignum256modm a;
    ge25519 p;

    crypto_sha512(h, seed, 32);
    h[0] &= 248;  // EN: the standard Ed25519 clamp: clear the low 3 bits,
                  // RU: стандартный clamp Ed25519: очищаем младшие 3 бита,
    h[31] &= 63;  // EN: clear the top two bits, | RU: очищаем два старших бита,
    h[31] |= 64;  // EN: and set bit 254 | RU: и ставим бит 254

    expand256_modm(a, h, 32); // EN: a = h mod L (the group order) | RU: a = h mod L (порядок группы)
    ge25519_scalarmult_base_niels(&p, ge25519_niels_base_multiples, a);
    ge25519_pack(pub, &p);

    crypto_wipe(h, sizeof h);
    crypto_wipe(&a, sizeof a);
    crypto_wipe(&p, sizeof p);
}
