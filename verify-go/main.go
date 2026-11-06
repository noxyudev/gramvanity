// EN: Independent verifier (tonutils-go). The search feeds it JSON lines on stdin and compares the
// EN: address; `w5addr activate ...` is a separate subcommand that deploys/activates a tg-wallet
// EN: (see tgactivate.go).
// RU: Независимый верификатор (tonutils-go). Поиск передаёт ему JSON-строки на stdin и сверяет
// RU: адрес; `w5addr activate ...` — отдельная подкоманда, разворачивающая/активирующая tg-wallet
// RU: (см. tgactivate.go).
//
// EN: Default mode ("w5", the historical behavior): same Tonkeeper import logic as the original
// EN: @ton/ton verifier — SeedToPrivateKeyWithOptions(..., WithBIP39(true)) = TON standard if valid,
// EN: else BIP39 m/44'/607'/0'. Then W5 (v5r1) mainnet subwallet 0.
// RU: Режим по умолчанию ("w5", историческое поведение): та же логика импорта Tonkeeper, что и в
// RU: исходном верификаторе @ton/ton — SeedToPrivateKeyWithOptions(..., WithBIP39(true)) = стандарт
// RU: TON, если он корректен, иначе BIP39 m/44'/607'/0'. Затем W5 (v5r1) mainnet, subwallet 0.
//
// EN: Mode "tg" (Telegram Wallet / WalletTg): the phrase is a pair of independent BIP39-12 phrases.
// EN: Words 1-12 ("anchor") define the address and are the initial on-chain key; words 13-24
// EN: ("signing") are the key the contract is switched to on activation. The address is the
// EN: StateInit hash of the immutable WalletTrampoline code (config[-123] jumper) plus the rev00
// EN: WalletTg storage cell: revision 0 | seqno 0 | subwallet_id | anchor public key.
// EN: net: mainnet (subwallet 0x7FFF7F11) | testnet (0x7FFF7FFD).
// RU: Режим "tg" (Telegram Wallet / WalletTg): фраза — это пара независимых фраз BIP39-12.
// RU: Слова 1-12 ("якорь") задают адрес и являются исходным ключом в блокчейне; слова 13-24
// RU: ("подписывающая половина") — ключ, на который контракт переключается при активации. Адрес —
// RU: хеш StateInit неизменяемого кода WalletTrampoline (переходник config[-123]) плюс ячейка
// RU: хранения WalletTg rev00: revision 0 | seqno 0 | subwallet_id | публичный ключ якоря.
// RU: net: mainnet (subwallet 0x7FFF7F11) | testnet (0x7FFF7FFD).
//
// EN: stdin: JSON lines {"words": "...", "mode"?: "w5"|"tg", "net"?: "mainnet"|"testnet"}
// EN: stdout: JSON lines {uq, eq, pub, as, tonValid, bip39Valid} (+ signpub for tg)
// RU: stdin: JSON-строки {"words": "...", "mode"?: "w5"|"tg", "net"?: "mainnet"|"testnet"}
// RU: stdout: JSON-строки {uq, eq, pub, as, tonValid, bip39Valid} (+ signpub для tg)
// EN: This binary stays English-only: its output is consumed by scripts, not by people.
// RU: Этот бинарник остаётся только английским: его вывод читают скрипты, а не люди.
package main

import (
	"bufio"
	"crypto/ed25519"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"strings"

	"github.com/tyler-smith/go-bip39"
	"github.com/xssnick/tonutils-go/ton/wallet"
)

func main() {
	if len(os.Args) > 1 && os.Args[1] == "activate" {
		os.Exit(runActivate(os.Args[2:]))
	}
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		var in struct{ Words, Mode, Net string }
		if strings.TrimSpace(sc.Text()) == "" {
			continue
		}
		if err := json.Unmarshal(sc.Bytes(), &in); err != nil {
			fmt.Println(`{"error":"bad json"}`)
			continue
		}
		words := strings.Fields(in.Words)
		if in.Mode == "tg" {
			out, _ := json.Marshal(tgCheck(words, in.Net))
			fmt.Println(string(out))
			continue
		}
		_, errTon := wallet.SeedToPrivateKeyWithOptions(words)
		tonValid := errTon == nil
		bipValid := bip39.IsMnemonicValid(strings.Join(words, " "))
		k, err := wallet.SeedToPrivateKeyWithOptions(words, wallet.WithBIP39(true))
		if err != nil {
			fmt.Printf("{\"error\":%q}\n", err.Error())
			continue
		}
		w, err := wallet.FromPrivateKey(nil, k, wallet.ConfigV5R1Final{NetworkGlobalID: -239, Workchain: 0})
		if err != nil {
			fmt.Printf("{\"error\":%q}\n", err.Error())
			continue
		}
		a := w.WalletAddress()
		a.SetBounce(false)
		uq := a.String()
		a.SetBounce(true)
		eq := a.String()
		as := "bip39"
		if tonValid {
			as = "ton"
		}
		out, _ := json.Marshal(map[string]any{"uq": uq, "eq": eq, "pub": hex.EncodeToString(k.Public().(ed25519.PublicKey)), "as": as, "tonValid": tonValid, "bip39Valid": bipValid})
		fmt.Println(string(out))
	}
}
