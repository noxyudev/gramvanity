// EN: tgaddress.go - WalletTg (Telegram Wallet) addresses and the stdin "tg" check: the WalletTrampoline
// EN: code cell, the StateInit hash for an anchor public key and the JSON answer for the verifier pipe.
// RU: tgaddress.go - адреса WalletTg (Telegram Wallet) и проверка "tg" на stdin: ячейка кода
// RU: WalletTrampoline, хеш StateInit для публичного ключа якоря и JSON-ответ для канала верификатора.
package main

import (
	"crypto/ed25519"
	"encoding/hex"
	"strings"

	"github.com/tyler-smith/go-bip39"
	"github.com/xssnick/tonutils-go/address"
	"github.com/xssnick/tonutils-go/ton/wallet"
	"github.com/xssnick/tonutils-go/tvm/cell"
)

// EN: WalletTrampoline cell (contracts/WalletTrampoline.fif): JMP to the WalletTg code in config[-123].
// RU: Ячейка WalletTrampoline (contracts/WalletTrampoline.fif): JMP на код WalletTg в config[-123].
var trampolineBOC = mustHex("b5ee9c7241010101001a000030ff00209821d7498308b9f240df8085f833d0ed1e20ed53d969427e39")

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

// EN: WalletTg address for an anchor public key: sha256 of the StateInit cell.
// RU: Адрес WalletTg для публичного ключа якоря: sha256 ячейки StateInit.
func tgAddress(anchorPub ed25519.PublicKey, testnet bool) (*address.Address, string, string, error) {
	code, err := cell.FromBOC(trampolineBOC)
	if err != nil {
		return nil, "", "", err
	}
	subwallet := uint64(0x7FFF7F11)
	if testnet {
		subwallet = 0x7FFF7FFD
	}
	// EN: rev00 storage: revision 0x00 | seqno:32 | subwallet_id:32 | public_key:256 (328 bits)
	// RU: хранение rev00: revision 0x00 | seqno:32 | subwallet_id:32 | public_key:256 (328 бит)
	data := cell.BeginCell().
		MustStoreUInt(0, 8).
		MustStoreUInt(0, 32).
		MustStoreUInt(subwallet, 32).
		MustStoreSlice(anchorPub, 256).
		EndCell()
	// EN: StateInit: split_depth none, special none, code ^, data ^, library empty -> bits 00110
	// RU: StateInit: split_depth нет, special нет, code ^, data ^, library пусто -> биты 00110
	si := cell.BeginCell().MustStoreUInt(0b00110, 5).MustStoreRef(code).MustStoreRef(data).EndCell()
	addr := address.NewAddress(0, 0, si.Hash()).Testnet(testnet)
	a1 := addr.Bounce(false)
	uq := a1.String()
	a2 := addr.Bounce(true)
	eq := a2.String()
	return addr, uq, eq, nil
}

func tgCheck(words []string, net string) map[string]any {
	if len(words) != 12 && len(words) != 24 {
		return map[string]any{"error": "tg: the phrase must have 12 (anchor only) or 24 (anchor + signing) words"}
	}
	testnet := net == "testnet"
	anchor, signing := words[:12], words[12:]
	// EN: gramvanity always derives tg-wallet halves through BIP39 m/44'/607'/0'; do not let the
	// EN: TON-standard import path take over for the ~1/256 phrases that also pass its check.
	// RU: gramvanity всегда выводит половины tg-wallet через BIP39 m/44'/607'/0'; не позволяем пути
	// RU: импорта по стандарту TON перехватить ~1/256 фраз, которые проходят и его проверку.
	anchorKey, err := tgKeyOf(anchor)
	if err != nil {
		return map[string]any{"error": err.Error()}
	}
	anchorPub := anchorKey.Public().(ed25519.PublicKey)
	_, uq, eq, err := tgAddress(anchorPub, testnet)
	if err != nil {
		return map[string]any{"error": err.Error()}
	}
	bipValid := bip39.IsMnemonicValid(strings.Join(anchor, " "))
	if len(signing) > 0 {
		bipValid = bipValid && bip39.IsMnemonicValid(strings.Join(signing, " "))
	}
	_, errTon := wallet.SeedToPrivateKeyWithOptions(anchor)
	out := map[string]any{
		"uq": uq, "eq": eq, "pub": hex.EncodeToString(anchorPub), "as": "tg",
		"bip39Valid": bipValid, "tonValid": errTon == nil,
	}
	if len(signing) == 12 {
		signKey, err := tgKeyOf(signing)
		if err != nil {
			return map[string]any{"error": err.Error()}
		}
		out["signpub"] = hex.EncodeToString(signKey.Public().(ed25519.PublicKey))
	}
	return out
}
