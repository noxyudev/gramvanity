// EN: addresses.c - TON account ids and address strings (see addresses.h): the W5 v5r1 and WalletTg
// EN: state-init hashing, the base64url formatting and the full CPU derivation of one candidate.
// RU: addresses.c - идентификаторы аккаунтов и строки адресов TON (см. addresses.h): хеширование
// RU: state-init для W5 v5r1 и WalletTg, форматирование base64url и полный вывод кандидата на CPU.
#include "sv.h"
#include "platform.h"
#include "options.h"
#include "crypto.h"
#include "detsearch.h"
#include "addresses.h"

#include <stdio.h>
#include <string.h>

#include "sha256.h"
#include "third_party/monocypher/monocypher.h"

// ---------------------------------------------------------------- W5 (v5r1) address

// EN: Code cell of wallet v5r1 (as in @ton/ton WalletContractV5R1, tonutils-go ConfigV5R1Final)
// RU: Ячейка кода кошелька v5r1 (как в @ton/ton WalletContractV5R1, tonutils-go ConfigV5R1Final)
static const uint8_t W5_CODE_HASH[32] = {
    0x20, 0x83, 0x4b, 0x7b, 0x72, 0xb1, 0x12, 0x14, 0x7e, 0x1b, 0x2f, 0xb4, 0x57, 0xb8, 0x4e, 0x74,
    0xd1, 0xa3, 0x0f, 0x04, 0xf7, 0x37, 0xd4, 0xf6, 0x2a, 0x66, 0x8e, 0x95, 0x52, 0xd2, 0xb7, 0x2f};
#define W5_CODE_DEPTH 6
// EN: wallet_id = network_global_id (-239) XOR context{client=1, workchain=0, version=v5r1(0), subwallet=0}
// RU: wallet_id = network_global_id (-239) XOR context{client=1, workchain=0, version=v5r1(0), subwallet=0}
#define W5_WALLET_ID (0xFFFFFF11u ^ 0x80000000u) // 2147483409

void w5_account_id(const uint8_t pub[32], uint8_t id[32]) {
    // EN: data cell, 322 bits: is_signature_allowed=1 | seqno:32=0 | wallet_id:32 | public_key:256 | extensions=0
    // RU: ячейка data, 322 бита: is_signature_allowed=1 | seqno:32=0 | wallet_id:32 | public_key:256 | extensions=0
    uint8_t d[2 + 41] = {0}, y[36], dh[32], si[71];
    d[0] = 0x00; // EN: refs 0, level 0 | RU: 0 ссылок, уровень 0
    d[1] = 81;   // EN: floor(322/8) + ceil(322/8) | RU: floor(322/8) + ceil(322/8)
    d[2] = 0x80;
    y[0] = (uint8_t)(W5_WALLET_ID >> 24); y[1] = (uint8_t)(W5_WALLET_ID >> 16);
    y[2] = (uint8_t)(W5_WALLET_ID >> 8); y[3] = (uint8_t)W5_WALLET_ID;
    memcpy(y + 4, pub, 32);
    uint8_t prev = 0;
    for (int k = 0; k < 36; k++) { d[2 + 4 + k] = (uint8_t)((prev << 7) | (y[k] >> 1)); prev = y[k]; }
    d[2 + 40] = (uint8_t)((prev << 7) | 0x20); // EN: last pubkey bit, extensions bit 0, completion tag
                                               // RU: последний бит ключа, бит 0 extensions, тег завершения
    sha256(d, sizeof d, dh);
    // EN: StateInit: split_depth 0, special 0, code 1, data 1, library 0 -> 0x34; 2 refs
    // RU: StateInit: split_depth 0, special 0, code 1, data 1, library 0 -> 0x34; 2 ссылки
    si[0] = 0x02; si[1] = 0x01; si[2] = 0x34;
    si[3] = 0; si[4] = W5_CODE_DEPTH; si[5] = 0; si[6] = 0;
    memcpy(si + 7, W5_CODE_HASH, 32);
    memcpy(si + 39, dh, 32);
    sha256(si, sizeof si, id);
}

// ---------------------------------------------------------------- WalletTg (Telegram Wallet) address

// EN: Code cell of the WalletTrampoline contract (contracts/WalletTrampoline.fif): a tiny immutable
// EN: "JMP to config[-123]" cell, stored as the account code of every Telegram wallet. The WalletTg
// EN: logic behind it is upgraded by validator voting, so this code (and every address) never changes.
// RU: Ячейка кода контракта WalletTrampoline (contracts/WalletTrampoline.fif): крошечная неизменяемая
// RU: ячейка "JMP на config[-123]", хранящаяся как код аккаунта каждого кошелька Telegram. Логика
// RU: WalletTg за ней обновляется голосованием валидаторов, поэтому этот код (и все адреса) не меняется.
static const uint8_t TG_CODE_HASH[32] = {
    0x91, 0x49, 0xae, 0x51, 0xc1, 0xe4, 0x68, 0x97, 0x10, 0xce, 0xbf, 0x78, 0x30, 0x29, 0x7b, 0x16,
    0xac, 0xfb, 0xad, 0xb3, 0x63, 0xa9, 0x20, 0xa5, 0x37, 0x89, 0x3e, 0x7f, 0xfe, 0xec, 0xa7, 0x68};
#define TG_CODE_DEPTH 0

uint32_t tg_subwallet(void) { return opt.testnet ? TG_SUBWALLET_TESTNET : TG_SUBWALLET_MAINNET; }

void tg_account_id(const uint8_t pub[32], uint32_t subwallet, uint8_t id[32]) {
    uint8_t d[2 + 41] = {0}, dh[32], si[71];
    d[0] = 0x00; // EN: refs 0, level 0 | RU: 0 ссылок, уровень 0
    d[1] = 0x52; // EN: floor(328/8) + ceil(328/8) | RU: floor(328/8) + ceil(328/8)
    // EN: d[2] = revision 0x00, d[3..6] = seqno 0
    // RU: d[2] = ревизия 0x00, d[3..6] = seqno 0
    d[7] = (uint8_t)(subwallet >> 24); d[8] = (uint8_t)(subwallet >> 16);
    d[9] = (uint8_t)(subwallet >> 8); d[10] = (uint8_t)subwallet;
    memcpy(d + 11, pub, 32);
    sha256(d, sizeof d, dh);
    // EN: StateInit: split_depth 0, special 0, code 1, data 1, library 0 -> 0x34; 2 refs
    // RU: StateInit: split_depth 0, special 0, code 1, data 1, library 0 -> 0x34; 2 ссылки
    si[0] = 0x02; si[1] = 0x01; si[2] = 0x34;
    si[3] = 0; si[4] = TG_CODE_DEPTH; si[5] = 0; si[6] = 0;
    memcpy(si + 7, TG_CODE_HASH, 32);
    memcpy(si + 39, dh, 32);
    sha256(si, sizeof si, id);
}

void account_id_for_mode(const uint8_t pub[32], uint8_t id[32]) {
    if (opt.mode == TG) tg_account_id(pub, tg_subwallet(), id);
    else w5_account_id(pub, id);
}

const char *wallet_desc(void) {
    static char b[128];
    if (opt.mode == TG)
        snprintf(b, sizeof b, "WalletTg (Telegram Wallet) %s subwallet 0x%08X",
                 opt.testnet ? "testnet" : "mainnet", tg_subwallet());
    else
        snprintf(b, sizeof b, "W5 v5r1 mainnet subwallet 0");
    return b;
}

const char *form_name(void) {
    if (opt.testnet) return opt.bounceable ? "kQ" : "0Q";
    return opt.bounceable ? "EQ" : "UQ";
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void friendly(const uint8_t id[32], int bounceable, char out[49]) {
    uint8_t b[36];
    b[0] = (uint8_t)((bounceable ? 0x11 : 0x51) | (opt.testnet ? 0x80 : 0)); // EN: testnet tags: kQ / 0Q
                                                                             // RU: теги testnet: kQ / 0Q
    b[1] = 0x00;
    memcpy(b + 2, id, 32);
    uint16_t crc = 0;
    for (int i = 0; i < 34; i++) {
        crc ^= (uint16_t)(b[i] << 8);
        for (int k = 0; k < 8; k++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    b[34] = (uint8_t)(crc >> 8);
    b[35] = (uint8_t)crc;
    for (int i = 0, o = 0; i < 36; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16 | (uint32_t)b[i + 1] << 8 | b[i + 2];
        out[o++] = B64[v >> 18]; out[o++] = B64[(v >> 12) & 63]; out[o++] = B64[(v >> 6) & 63]; out[o++] = B64[v & 63];
    }
    out[48] = 0;
}

void hexs(const uint8_t *b, int n, char *out) {
    for (int i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

void tg_signing_indices(uint64_t cand_index, int idx[12]) {
    uint8_t ent[16];
    if (det.enabled) det_entropy_ex(det.key, DETSIGN, cand_index, ent, 16);
    else random_bytes(ent, 16);
    bip39_indices(ent, 16, idx);
    crypto_wipe(ent, sizeof ent);
}

void derive_candidate(const uint8_t key[32], int mode, uint64_t index, Derived *d) {
    uint8_t ent[33];
    if (mode == TG) {
        d->nw = 24;
        det_entropy_ex(key, TG, index, ent, 16);
        bip39_indices(ent, 16, d->idx);
        det_entropy_ex(key, DETSIGN, index, ent, 16);
        bip39_indices(ent, 16, d->idx + 12);
        crypto_wipe(ent, sizeof ent);
        d->phrase_len = phrase_bytes(d->idx, 24, d->phrase);
        char anchor[128], signing[128];
        size_t al = phrase_bytes(d->idx, 12, anchor), sl = phrase_bytes(d->idx + 12, 12, signing);
        bip39_key(anchor, al, d->priv);
        uint8_t spriv[32];
        bip39_key(signing, sl, spriv);
        ed25519_public(d->priv, d->pub);
        ed25519_public(spriv, d->spub);
        tg_account_id(d->pub, tg_subwallet(), d->id);
        crypto_wipe(anchor, sizeof anchor); crypto_wipe(signing, sizeof signing); crypto_wipe(spriv, sizeof spriv);
        return;
    }
    size_t elen = mode == BIP12 ? 16 : mode == BIP24 ? 32 : 33;
    det_entropy_ex(key, mode, index, ent, elen);
    d->nw = mode == BIP12 ? 12 : 24;
    if (mode == TON24) {
        indices11(ent, 24, d->idx);
    } else {
        bip39_indices(ent, (int)elen, d->idx);
    }
    crypto_wipe(ent, sizeof ent);
    d->phrase_len = phrase_bytes(d->idx, d->nw, d->phrase);
    if (mode == TON24) {
        uint8_t e[64], s[64];
        ton_entropy(d->phrase, d->phrase_len, e);
        cpu_pbkdf2(e, 64, SALT_TON_DEFAULT, 16, TON_ITERS, s);
        memcpy(d->priv, s, 32);
        crypto_wipe(e, sizeof e); crypto_wipe(s, sizeof s);
    } else {
        bip39_key(d->phrase, d->phrase_len, d->priv);
    }
    ed25519_public(d->priv, d->pub);
    w5_account_id(d->pub, d->id);
}
