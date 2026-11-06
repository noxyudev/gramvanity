// EN: Go port of verify/tg-activate.mjs: activates a gramvanity tg-wallet address.
// RU: Порт verify/tg-activate.mjs на Go: активирует адрес tg-wallet из gramvanity.
//
// EN: It deploys the WalletTg trampoline and switches the contract key from the anchor (words 1-12)
// EN: to the signing key (words 13-24) in ONE external message, exactly like the Telegram Wallet
// EN: does when it creates a wallet.
// RU: Он разворачивает trampoline WalletTg и меняет ключ контракта с якоря (слова 1-12) на
// RU: подписывающий ключ (слова 13-24) ОДНИМ внешним сообщением — точно так же, как делает
// RU: Telegram Wallet при создании кошелька.
//
// EN: Until this is done, the address exists only as a hash: the contract is not deployed, the
// EN: on-chain key is undefined, and Telegram Wallet cannot use the phrase (it signs with words
// EN: 13-24, which must be the on-chain key).
// RU: Пока это не сделано, адрес существует только как хеш: контракт не развёрнут, ключ в
// RU: блокчейне не определён, и Telegram Wallet не может использовать фразу (он подписывает
// RU: словами 13-24, а они должны быть ключом в блокчейне).
//
// EN: The usage text below mirrors the English output of --help: this binary stays English-only
// EN: because scripts consume its output.
// RU: Текст использования ниже повторяет английский вывод --help: этот бинарник остаётся только
// RU: английским, потому что его вывод читают скрипты.
//
//	w5addr activate --phrase "w1 ... w24" [--net mainnet|testnet] [--check] [--send]
//	w5addr activate --hit results/hit-....txt [--net mainnet|testnet] [--check] [--send]
//
// EN: Without --send the signed external message (base64 BoC) is only printed/saved: fund the address
// EN: first (0.1 GRAM recommended, 0.02 is the minimum it accepts), then run again with --send,
// EN: or POST the BoC to <toncenter>/sendBoc yourself.
// RU: Без --send подписанное внешнее сообщение (base64 BoC) только печатается/сохраняется: сначала
// RU: пополните адрес (рекомендуется 0.1 GRAM, минимум 0.02), затем запустите снова с --send или
// RU: сами отправьте BoC POST-запросом на <toncenter>/sendBoc.
//
// Options:
//
//	--phrase "24 words"   the phrase from a gramvanity hit (words 1-12 + words 13-24)
//	--phrase -            read the phrase from stdin
//	--hit FILE            take the phrase from a gramvanity hit file (only --random hit files contain
//	                      the phrase; default deterministic hits need: gramvanity --reveal CKPT IDX)
//	--net mainnet|testnet network (default mainnet; testnet addresses start with 0Q, spend only
//	                      testnet GRAM from a faucet)
//	--check               only report address state/balance/on-chain key, do not build anything
//	--send                send the message via toncenter (needs >= 0.02 GRAM on the address)
//	--api-key KEY         toncenter API key (or env TONCENTER_API_KEY); optional but raises rate limits
//	--out FILE            save the BoC to FILE (default: print only)
//	--valid-for SEC       message validity window (default 300)
//
// EN: Safety: run it on your own machine; it derives keys from the phrase and never sends the phrase
// EN: anywhere (only the signed BoC goes to toncenter). A --random hit file keeps the phrase until you
// EN: delete it and write it down offline; default deterministic hit files store only the search key and
// EN: index, so keep the checkpoint and use --reveal.
// RU: Безопасность: запускайте на своей машине; ключи выводятся из фразы, и фраза никуда не
// RU: отправляется (в toncenter уходит только подписанный BoC). Hit-файл --random хранит фразу, пока
// RU: вы его не удалите и не запишете фразу офлайн; обычные детерминированные hit-файлы содержат
// RU: только ключ поиска и индекс, поэтому храните checkpoint и используйте --reveal.
package main

import (
	"bytes"
	"crypto/ed25519"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"io"
	"math/big"
	"net/http"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/tyler-smith/go-bip39"
	"github.com/xssnick/tonutils-go/tvm/cell"
)

const (
	tgChangeKeyOpcode = 0xFBBA99C8  // EN: ChangePublicKeyRequestE opcode | RU: опкод ChangePublicKeyRequestE
	tgMinBalance      = 20_000_000  // EN: 0.02 GRAM: enough for deploy + rotation + the event
	                                // RU: 0.02 GRAM: хватает на деплой + смену ключа + событие
	tgRecommendedBal  = 100_000_000 // EN: 0.1 GRAM | RU: 0.1 GRAM
	tgActivateUsage   = `usage: w5addr activate --phrase "w1 ... w24" [options]
       w5addr activate --hit FILE [options]

Activates a gramvanity tg-wallet: deploys the WalletTg trampoline and switches the contract
key from the anchor (words 1-12) to the signing key (words 13-24) in one external message,
like Telegram Wallet does when it creates a wallet. Fund the address first: 0.02 GRAM is the
minimum it accepts, 0.1 GRAM is recommended.

options:
  --phrase "24 words"    phrase from a gramvanity hit (words 1-12 + words 13-24)
  --phrase -             read the phrase from stdin
  --hit FILE             take the phrase from a --random hit file (default hit files
                         store the search key + index: recover the phrase first with
                         gramvanity --reveal CKPT IDX)
  --net mainnet|testnet  network (default mainnet; testnet needs a faucet)
  --check                only report address, state, balance and on-chain key
  --send                 send the message via toncenter (needs >= 0.02 GRAM)
  --api-key KEY          toncenter API key (or env TONCENTER_API_KEY)
  --out FILE             save the signed BoC to FILE (default: print only)
  --valid-for SEC        message validity window in seconds (default 300)
  --help                 show this help

Without --send the signed message is only printed/saved, so you can fund the address first
and then re-run with --send (or POST the BoC to <toncenter>/sendBoc yourself).`
)

type tgNetwork struct {
	subwallet uint32
	testnet   bool
	base      string // EN: toncenter API root including /api/v2 | RU: корень API toncenter с /api/v2
}

var tgNetworks = map[string]tgNetwork{
	"mainnet": {subwallet: 0x7FFF7F11, testnet: false, base: "https://toncenter.com/api/v2"},
	"testnet": {subwallet: 0x7FFF7FFD, testnet: true, base: "https://testnet.toncenter.com/api/v2"},
}

func tgFail(format string, args ...any) int {
	fmt.Fprintf(os.Stderr, "error: "+format+"\n", args...)
	return 1
}

// EN: runActivate implements `w5addr activate ...` and returns the process exit code.
// RU: runActivate реализует `w5addr activate ...` и возвращает код завершения процесса.
func runActivate(args []string) int {
	phrase := ""
	hitFile := ""
	netName := "mainnet"
	apiKey := os.Getenv("TONCENTER_API_KEY")
	check, send, outFile := false, false, ""
	validFor := 300

	for i := 0; i < len(args); i++ {
		key := args[i]
		next := func() string {
			if i+1 >= len(args) {
				fmt.Fprintf(os.Stderr, "error: missing value for %s\n", key)
				os.Exit(1)
			}
			i++
			return args[i]
		}
		switch key {
		case "--phrase":
			phrase = next()
		case "--hit":
			hitFile = next()
		case "--net":
			netName = next()
		case "--api-key":
			apiKey = next()
		case "--out":
			outFile = next()
		case "--check":
			check = true
		case "--send":
			send = true
		case "--valid-for":
			n, err := strconv.Atoi(next())
			if err != nil || n < 0 {
				return tgFail("--valid-for must be a non-negative number of seconds")
			}
			validFor = n
		case "--help", "-h":
			fmt.Println(tgActivateHelp())
			return 0
		default:
			return tgFail("unknown option %s (see --help)", key)
		}
	}

	network, ok := tgNetworks[netName]
	if !ok {
		return tgFail("--net must be mainnet or testnet")
	}
	if phrase == "" && hitFile != "" {
		text, err := os.ReadFile(hitFile)
		if err != nil {
			return tgFail("%s: %v", hitFile, err)
		}
		m := regexp.MustCompile(`(?m)^phrase \(\d+ words[^)]*\): (.+)$`).FindSubmatch(text)
		if m == nil {
			return tgFail("%s does not contain the phrase. Default hit files store the search key + index "+
				"instead; recover the phrase first with --reveal and pass it with --phrase.", hitFile)
		}
		phrase = string(m[1])
	}
	if phrase == "-" {
		data, err := io.ReadAll(os.Stdin)
		if err != nil {
			return tgFail("cannot read the phrase from stdin: %v", err)
		}
		phrase = string(data)
	}
	if phrase == "" {
		return tgFail(`pass the phrase with --phrase "w1 ... w24" or --hit FILE (see --help)`)
	}

	words := strings.Fields(phrase)
	if len(words) != 24 {
		return tgFail("expected 24 words (12 anchor + 12 signing), got %d. A 12-word phrase cannot be activated: "+
			"the signing half is what Telegram Wallet signs with. If the phrase is in a default hit file, use --reveal.", len(words))
	}
	anchor, signing := words[:12], words[12:]
	if !bip39.IsMnemonicValid(strings.Join(anchor, " ")) {
		return tgFail("words 1-12 are not a valid BIP39 phrase")
	}
	if !bip39.IsMnemonicValid(strings.Join(signing, " ")) {
		return tgFail("words 13-24 are not a valid BIP39 phrase")
	}

	anchorPriv, err := tgKeyOf(anchor)
	if err != nil {
		return tgFail("cannot derive the anchor key: %v", err)
	}
	signingPriv, err := tgKeyOf(signing)
	if err != nil {
		return tgFail("cannot derive the signing key: %v", err)
	}
	anchorPub := anchorPriv.Public().(ed25519.PublicKey)
	signingPub := signingPriv.Public().(ed25519.PublicKey)
	addr, _, _, err := tgAddress(anchorPub, network.testnet)
	if err != nil {
		return tgFail("cannot build the wallet address: %v", err)
	}
	addrStr := addr.Bounce(false).String()
	eqStr := addr.Bounce(true).String()
	anchorHex := hex.EncodeToString(anchorPub)
	signingHex := hex.EncodeToString(signingPub)

	fmt.Printf("network:      %s\n", netName)
	fmt.Printf("address:      %s\n", addrStr)
	fmt.Printf("  bounceable: %s\n", eqStr)
	fmt.Printf("  raw:        %s\n", addr.StringRaw())
	fmt.Printf("anchor key:   %s   (words 1-12, controls the address)\n", anchorHex)
	fmt.Printf("signing key:  %s   (words 13-24, Telegram Wallet signs with it)\n", signingHex)

	rpc := &tgRPC{base: network.base, apiKey: apiKey, http: &http.Client{Timeout: 30 * time.Second}}
	info, err := rpc.state(addrStr)
	if err != nil {
		return tgFail("%v", err)
	}
	balance, ok := new(big.Int).SetString(string(info.Balance), 10)
	if !ok {
		balance = new(big.Int)
	}
	fmt.Printf("on-chain:     %s, balance %s GRAM\n", info.State, formatTgCoins(balance))

	switch info.State {
	case "active":
		if info.Code != "" {
			rawCode, err := base64.StdEncoding.DecodeString(info.Code)
			if err != nil {
				return tgFail("cannot decode the on-chain code: %v", err)
			}
			onChainCode, err := cell.FromBOC(rawCode)
			if err != nil {
				return tgFail("cannot decode the on-chain code: %v", err)
			}
			want, err := cell.FromBOC(trampolineBOC)
			if err != nil {
				return tgFail("cannot decode the WalletTrampoline code: %v", err)
			}
			if !bytes.Equal(onChainCode.Hash(), want.Hash()) {
				return tgFail("the contract at this address is NOT a Telegram Wallet (code hash %s). "+
					"Do not send anything there; the phrase or the network is wrong.", hex.EncodeToString(onChainCode.Hash()))
			}
		}
		pub, err := rpc.getNumber(addrStr, "get_public_key")
		if err != nil {
			return tgFail("%v", err)
		}
		pubHex := fmt.Sprintf("%064x", pub)
		fmt.Printf("on-chain key: %s\n", pubHex)
		if pubHex == signingHex {
			fmt.Println("\nAlready activated: the on-chain key is the signing key (words 13-24).")
			fmt.Println("Import the 24 words into Telegram Wallet; the address above is the one it will show.")
			return 0
		}
		if pubHex != anchorHex {
			return tgFail("the on-chain key is neither the anchor nor the signing key: this wallet was created from other words. Nothing to do.")
		}
		fmt.Println("state:        active, still on the anchor key -> only the rotation is needed")
	case "uninitialized":
		fmt.Println("state:        not deployed yet -> the message will deploy the contract and switch the key in one go")
	default:
		return tgFail("unexpected account state %q", info.State)
	}

	if check {
		return 0
	}
	if send && balance.Cmp(big.NewInt(tgMinBalance)) < 0 {
		extra := ""
		if network.testnet {
			extra = " (testnet faucet: https://t.me/testgiver_ton_bot)"
		}
		return tgFail("balance is %s nanoTON, need at least %d (0.02 GRAM; %d recommended). "+
			"Send GRAM to %s first%s, then re-run.", balance.String(), tgMinBalance, tgRecommendedBal, addrStr, extra)
	}

	seqno := uint32(0)
	if info.State == "active" {
		n, err := rpc.getNumber(addrStr, "seqno")
		if err != nil {
			return tgFail("%v", err)
		}
		seqno = uint32(n.Uint64())
	}
	validUntil := uint32(time.Now().Unix() + int64(validFor))
	boc, err := tgBuildSignedMessage(anchorPriv, signingPriv, addr, network.subwallet,
		info.State == "active", seqno, validUntil)
	if err != nil {
		return tgFail("%v", err)
	}
	fmt.Println("self-check:   both signatures and the message layout verified")

	if outFile != "" {
		if err := os.WriteFile(outFile, []byte(boc+"\n"), 0o600); err != nil {
			return tgFail("%s: %v", outFile, err)
		}
		fmt.Printf("signed message saved to %s\n", outFile)
	}
	if !send {
		fmt.Print("\nSigned external message (send this AFTER funding the address):\n\n")
		fmt.Println(boc)
		fmt.Printf("\nHow to send it: re-run with --send, or POST it to %s/sendBoc as {\"boc\":\"...\"}.\n", network.base)
		return 0
	}

	fmt.Println("\nsending to toncenter...")
	if err := rpc.sendBoc(boc); err != nil {
		return tgFail("%v", err)
	}
	fmt.Println("sent; waiting for the contract to appear and switch the key...")

	start := time.Now()
	for {
		time.Sleep(5 * time.Second)
		st, err := rpc.state(addrStr)
		if err == nil && st.State == "active" {
			pub, err := rpc.getNumber(addrStr, "get_public_key")
			if err == nil && fmt.Sprintf("%064x", pub) == signingHex {
				fmt.Printf("\nDONE in %d s: the contract is deployed and the on-chain key is the signing key.\n",
					int(time.Since(start).Seconds()))
				fmt.Printf("Check the address in a browser: https://tonviewer.com/%s\n", addrStr)
				fmt.Printf("Next: import the 24 words into Telegram Wallet. It will show %s\n", addrStr)
				fmt.Println("Send a small test transfer first, and keep the phrase offline.")
				return 0
			}
		}
		if time.Since(start) > 3*time.Minute {
			fmt.Println("still no confirmation after 3 minutes. The message may be rejected (not enough balance?) or the network is slow.")
			fmt.Printf("Check the address: https://tonviewer.com/%s and re-run this tool (it will pick up the current state).\n", addrStr)
			return 1
		}
	}
}

func tgActivateHelp() string { return tgActivateUsage }

// EN: formatTgCoins prints nanoGRAM as GRAM with four decimals, like the JavaScript tool.
// RU: formatTgCoins печатает nanoGRAM как GRAM с четырьмя знаками, как инструмент на JavaScript.
func formatTgCoins(n *big.Int) string {
	f := new(big.Float).SetInt(n)
	f.Quo(f, big.NewFloat(1e9))
	return f.Text('f', 4)
}
