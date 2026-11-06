// EN: crypto.c - CPU crypto helpers (see crypto.h): PBKDF2-HMAC-SHA512, SLIP-10 ed25519, BIP39/TON
// EN: mnemonic handling and the GPU-side block packing. Built on Monocypher, sha256.c, the embedded
// EN: BIP39 wordlist and ed25519_fast.c.
// RU: crypto.c - криптографические помощники CPU (см. crypto.h): PBKDF2-HMAC-SHA512, SLIP-10 ed25519,
// RU: работа с мнемониками BIP39/TON и упаковка блоков для GPU. Опирается на Monocypher, sha256.c,
// RU: встроенный список слов BIP39 и ed25519_fast.c.
#include "sv.h"
#include "crypto.h"

#include <string.h>

#include "embedded.h"
#include "sha256.h"
#include "third_party/monocypher/monocypher-ed25519.h"
#include "third_party/monocypher/monocypher.h"

const uint8_t SALT_MNEMONIC[] = "mnemonic";
const uint8_t SALT_TON_VERSION[] = "TON seed version";
const uint8_t SALT_TON_DEFAULT[] = "TON default seed";

void hmac512(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[64]) {
    crypto_sha512_hmac(out, key, klen, msg, mlen);
}

void cpu_pbkdf2(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t iters, uint8_t out[64]) {
    crypto_sha512_hmac_ctx base, c;
    uint8_t u[64], blk[128];
    crypto_sha512_hmac_init(&base, pw, pwlen);
    memcpy(blk, salt, slen);
    blk[slen] = 0; blk[slen + 1] = 0; blk[slen + 2] = 0; blk[slen + 3] = 1;
    c = base;
    crypto_sha512_hmac_update(&c, blk, slen + 4);
    crypto_sha512_hmac_final(&c, u);
    memcpy(out, u, 64);
    for (uint32_t it = 1; it < iters; it++) {
        c = base;
        crypto_sha512_hmac_update(&c, u, 64);
        crypto_sha512_hmac_final(&c, u);
        for (int j = 0; j < 64; j++) out[j] ^= u[j];
    }
}

void cpu_slip10(const uint8_t seed[64], uint8_t out[32]) {
    uint8_t I[64], data[37];
    hmac512((const uint8_t *)"ed25519 seed", 12, seed, 64, I);
    const uint32_t path[3] = {44, 607, 0};
    for (int l = 0; l < 3; l++) {
        uint32_t i = path[l] | 0x80000000u;
        data[0] = 0;
        memcpy(data + 1, I, 32);
        data[33] = (uint8_t)(i >> 24); data[34] = (uint8_t)(i >> 16); data[35] = (uint8_t)(i >> 8); data[36] = (uint8_t)i;
        uint8_t c[32];
        memcpy(c, I + 32, 32);
        hmac512(c, 32, data, 37, I);
    }
    memcpy(out, I, 32);
}

void ed25519_public(const uint8_t priv[32], uint8_t pub[32]) {
    ed25519_public_fast(pub, priv);
}

void bip39_key(const char *phrase, size_t len, uint8_t priv[32]) {
    uint8_t seed[64];
    cpu_pbkdf2((const uint8_t *)phrase, len, SALT_MNEMONIC, 8, 2048, seed);
    cpu_slip10(seed, priv);
    crypto_wipe(seed, sizeof seed);
}

void indices11(const uint8_t *b, int count, int *out) {
    uint32_t acc = 0;
    int nbits = 0, pos = 0;
    for (int k = 0; k < count; k++) {
        while (nbits < 11) { acc = (acc << 8) | b[pos++]; nbits += 8; }
        out[k] = (int)((acc >> (nbits - 11)) & 0x7FF);
        nbits -= 11;
        acc &= (1u << nbits) - 1;
    }
}

void bip39_indices(const uint8_t *ent, int entlen, int *out) {
    uint8_t buf[33], h[32];
    memcpy(buf, ent, (size_t)entlen);
    sha256(ent, (size_t)entlen, h);
    buf[entlen] = h[0];
    indices11(buf, entlen * 3 / 4, out);
}

int bip39_valid(const int *idx, int nw) {
    if (nw != 12 && nw != 24) return 0;
    uint8_t bits[33] = {0};
    int nb = 0;
    for (int k = 0; k < nw; k++)
        for (int b = 10; b >= 0; b--, nb++)
            if ((idx[k] >> b) & 1) bits[nb / 8] |= (uint8_t)(0x80 >> (nb % 8));
    int entbits = nw * 11 * 32 / 33, csbits = nw * 11 - entbits;
    uint8_t h[32];
    sha256(bits, (size_t)entbits / 8, h);
    for (int k = 0; k < csbits; k++) {
        int got = (bits[(entbits + k) / 8] >> (7 - (entbits + k) % 8)) & 1;
        if (got != ((h[0] >> (7 - k)) & 1)) return 0;
    }
    return 1;
}

size_t phrase_bytes(const int *idx, int nw, char *out) {
    size_t n = 0;
    for (int k = 0; k < nw; k++) {
        if (k) out[n++] = ' ';
        size_t l = strlen(WORDS[idx[k]]);
        memcpy(out + n, WORDS[idx[k]], l);
        n += l;
    }
    out[n] = 0;
    return n;
}

void ton_entropy(const char *phrase, size_t len, uint8_t out[64]) {
    hmac512((const uint8_t *)phrase, len, NULL, 0, out);
}

int ton_valid(const char *phrase, size_t len) {
    uint8_t e[64], s[64];
    ton_entropy(phrase, len, e);
    cpu_pbkdf2(e, 64, SALT_TON_VERSION, 16, TON_BASIC_ITERS, s);
    return s[0] == 0;
}

void key_block(const uint8_t *pw, size_t len, uint64_t *w) {
    uint8_t k[128] = {0};
    if (len > 128) { crypto_sha512(k, pw, len); } else memcpy(k, pw, len);
    for (int j = 0; j < 16; j++) {
        uint64_t v = 0;
        for (int b = 0; b < 8; b++) v = (v << 8) | k[j * 8 + b];
        w[j] = v;
    }
}

void salt_block(const uint8_t *salt, size_t slen, uint64_t *w) {
    uint8_t b[128] = {0};
    memcpy(b, salt, slen);
    b[slen + 3] = 1;
    b[slen + 4] = 0x80;
    uint64_t bits = (uint64_t)(128 + slen + 4) * 8;
    for (int j = 0; j < 8; j++) b[120 + j] = (uint8_t)(bits >> (56 - 8 * j));
    for (int j = 0; j < 16; j++) {
        uint64_t v = 0;
        for (int k = 0; k < 8; k++) v = (v << 8) | b[j * 8 + k];
        w[j] = v;
    }
}
