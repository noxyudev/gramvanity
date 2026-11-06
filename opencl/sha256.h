// EN: Minimal SHA-256 (FIPS 180-4) for BIP39 checksums and TON cell hashes.
// RU: Минимальная реализация SHA-256 (FIPS 180-4) для контрольных сумм BIP39 и хешей ячеек TON.
#pragma once
#include <stddef.h>
#include <stdint.h>
void sha256(const uint8_t *msg, size_t len, uint8_t out[32]);
