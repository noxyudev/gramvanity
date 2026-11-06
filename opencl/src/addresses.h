// EN: addresses.h - TON account ids and user-facing address strings for the two wallet types (W5 v5r1
// EN: and WalletTg) and the full CPU derivation of one candidate.
// RU: addresses.h - идентификаторы аккаунтов TON и адресные строки для двух типов кошельков (W5 v5r1
// RU: и WalletTg), а также полный вывод одного кандидата на CPU.
#ifndef SV_ADDRESSES_H
#define SV_ADDRESSES_H

#include <stddef.h>
#include <stdint.h>

#include "sv.h"

// EN: Default subwallet ids from the WalletTg contract storage (storage.tolk): mainnet / testnet.
// RU: Subwallet по умолчанию из хранилища контракта WalletTg (storage.tolk): mainnet / testnet.
#define TG_SUBWALLET_MAINNET 0x7FFF7F11u
#define TG_SUBWALLET_TESTNET 0x7FFF7FFDu

// EN: State-init hash of W5 for a public key = the account id
// RU: хеш state-init W5 для публичного ключа = идентификатор аккаунта
void w5_account_id(const uint8_t pub[32], uint8_t id[32]);
// EN: State-init hash of WalletTg for an anchor public key = the account id. Storage cell (328 bits,
// EN: byte-aligned): revision 0x00 | seqno:32=0 | subwallet_id:32 | public_key:256.
// RU: Хеш state-init WalletTg для публичного ключа якоря = идентификатор аккаунта. Ячейка хранения
// RU: (328 бит, выровнена по байту): revision 0x00 | seqno:32=0 | subwallet_id:32 | public_key:256.
void tg_account_id(const uint8_t pub[32], uint32_t subwallet, uint8_t id[32]);
uint32_t tg_subwallet(void);
// EN: Account id of the selected wallet: W5 always, WalletTg (with the network's subwallet) in tg mode.
// RU: Идентификатор аккаунта выбранного кошелька: всегда W5, а в режиме tg — WalletTg (с subwallet сети).
void account_id_for_mode(const uint8_t pub[32], uint8_t id[32]);
// EN: Description of the searched wallet for the console and the stop file.
// RU: Описание искомого кошелька для консоли и stop-файла.
const char *wallet_desc(void);
// EN: Address form letter: UQ/EQ on mainnet, 0Q/kQ on testnet.
// RU: Буква формы адреса: UQ/EQ в mainnet, 0Q/kQ в testnet.
const char *form_name(void);
// EN: User-facing base64url address (48 chars + NUL) from an account id.
// RU: Адрес base64url для пользователя (48 символов + NUL) из идентификатора аккаунта.
void friendly(const uint8_t id[32], int bounceable, char out[49]);
void hexs(const uint8_t *b, int n, char *out);

// EN: Signing half (words 13-24) of a tg-wallet phrase: 12 BIP39 words from a separate entropy domain
// EN: of the search key (deterministic mode) or fresh randomness (random mode). The signing half does
// EN: not affect the address, so it is only generated for hits and dumps, never per candidate.
// RU: Подписывающая половина (слова 13-24) фразы tg-wallet: 12 слов BIP39 из отдельного домена
// RU: энтропии ключа поиска (детерминированный режим) или свежей случайности (режим --random).
// RU: Подписывающая половина не влияет на адрес, поэтому создаётся только для хитов и дампов, но не
// RU: для каждого кандидата.
void tg_signing_indices(uint64_t cand_index, int idx[12]);

// EN: Full CPU derivation of a candidate for --derive/--reveal and the self-tests. In tg-wallet mode
// EN: "priv/pub" are the anchor key (words 1-12, the one the address is derived from) and "spub" is the
// EN: signing key (words 13-24); the phrase holds all 24 words.
// RU: Полный вывод кандидата на CPU для --derive/--reveal и self-тестов. В режиме tg-wallet "priv/pub" —
// RU: ключ якоря (слова 1-12, из него выводится адрес), а "spub" — подписывающий ключ (слова 13-24);
// RU: phrase содержит все 24 слова.
typedef struct {
    char phrase[256];
    size_t phrase_len;
    int idx[24];
    int nw;
    uint8_t priv[32], pub[32], spub[32], id[32];
} Derived;

void derive_candidate(const uint8_t key[32], int mode, uint64_t index, Derived *d);

#endif
