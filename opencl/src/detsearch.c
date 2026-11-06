// EN: detsearch.c - deterministic search state and the index -> entropy construction (see detsearch.h).
// RU: detsearch.c - состояние детерминированного поиска и построение энтропии из индекса (см. detsearch.h).
#include "sv.h"
#include "options.h"
#include "crypto.h"
#include "detsearch.h"

#include <string.h>

#include "third_party/monocypher/monocypher.h"

DetSearch det;

void det_entropy_ex(const uint8_t key[32], int mode, uint64_t index, uint8_t *out, size_t n) {
    uint8_t msg[15], d[64];
    memcpy(msg, "svDET1", 6);
    msg[6] = (uint8_t)mode;
    for (int i = 0; i < 8; i++) msg[7 + i] = (uint8_t)(index >> (56 - 8 * i));
    hmac512(key, 32, msg, sizeof msg, d);
    memcpy(out, d, n);
    crypto_wipe(d, sizeof d);
}

void det_entropy(uint64_t index, uint8_t *out, size_t n) {
    det_entropy_ex(det.key, opt.mode, index, out, n);
}
