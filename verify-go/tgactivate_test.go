// EN: Parity tests for the Go port of verify/tg-activate.mjs.
// RU: Тесты паритета для порта verify/tg-activate.mjs на Go.
//
// EN: The expected values below were produced by the JavaScript tool with a fixed phrase, seqno,
// EN: validUntil and account state, using @ton/core + bip39 + ed25519-hd-key + tweetnacl. They pin
// EN: key derivation, the rotation/request layout and both signatures. The whole external message is
// EN: compared by root hash, which covers the complete cell graph and therefore every bit a validator
// EN: sees (the BoC container flags themselves do not affect the message).
// RU: Ожидаемые значения ниже получены инструментом на JavaScript с фиксированной фразой, seqno,
// RU: validUntil и состоянием аккаунта, через @ton/core + bip39 + ed25519-hd-key + tweetnacl. Они
// RU: фиксируют вывод ключей, раскладку запроса/смены ключа и обе подписи. Всё внешнее сообщение
// RU: сравнивается по корневому хешу, который покрывает весь граф ячеек, а значит и каждый бит,
// RU: видимый валидатору (флаги контейнера BoC на само сообщение не влияют).
package main

import (
	"bytes"
	"crypto/ed25519"
	"encoding/base64"
	"encoding/hex"
	"strings"
	"testing"

	"github.com/xssnick/tonutils-go/tvm/cell"
)

// EN: Two independent valid BIP39-12 halves (throwaway: never fund this phrase).
// RU: Две независимые корректные половины BIP39-12 (одноразовые: не пополняйте эту фразу).
const tgTestPhrase = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about " +
	"legal winner thank year wave sausage worth useful legal winner thank yellow"

const (
	tgTestAnchorPub  = "7952e94118f34607c75e23258dd9220d66ccac5a3ee074125c25068e8107bfbf"
	tgTestSigningPub = "434908c4c092113fcced4eec061b09c02b7b8934a9ca555e2032505aab7558ea"
	tgTestMainnetUQ  = "UQD4d2pzr-W4jzh1wOd52HrsH7rRagzkSUPC3_jCgMxmKz_L"
	tgTestMainnetEQ  = "EQD4d2pzr-W4jzh1wOd52HrsH7rRagzkSUPC3_jCgMxmK2IO"
	tgTestTestnetUQ  = "0QC41OIkrSNrd0hhRjqVKzUm-1luBAivADU423xoOxelSnT3"
	tgTestTestnetEQ  = "kQC41OIkrSNrd0hhRjqVKzUm-1luBAivADU423xoOxelSiky"

	tgTestBOCMainnetInactive = "te6cckECBwEAAUkAAkWIAfDu1Odfy3EecOuBzvOw9dg/daLUGciSh4W/8YUBmMxWHgEEAgE0AgMAMP8AIJgh10mDCLnyQN+Ahfgz0O0eIO1T2QBSAAAAAAB//38ReVLpQRjzRgfHXiMljdkiDWbMrFo+4HQSXCUGjoEHv78C4EPCsZdW6nqGQD7Y8vI1aduxSyIhyLHKukrG6waQQp92yvM8KgJBo7f0Uk1mnGif5FPA8DiA4Lp6sTB8rbD46gD7upnIf/9/EWVT8QAAAAAAQ0kIxMCSET/M7U7sBhsJwCt7iTSpylVeIDJQWqt1WOoFBgCAAiI/19gCwHlLsdVTvPXbqpDuuaZwY4UfuAGAY4qU22E2+BRas2pyUaPNc53eK8Jf0ZNb8k8nQrFIkMcn66PdAQBAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQFkK8"
	tgTestBOCMainnetActive   = "te6cckEBBAEA/gABRYgB8O7U51/LcR5w64HO87D12D91otQZyJKHhb/xhQGYzFYMAQLgwpvypM/z/8ph0RdDmztUXWStlLFnwT6usGQ1uGIleQW/LA2Zvgror5Z8+pXVcKzJhH4M+XVSMbwd8mdZiRieDvu6mch//38RZVPxAAAAAAdDSQjEwJIRP8ztTuwGGwnAK3uJNKnKVV4gMlBaq3VY6gIDAIACIj/X2ALAeUux1VO89duqkO65pnBjhR+4AYBjipTbYTb4FFqzanJRo81znd4rwl/Rk1vyTydCsUiQxyfro90BAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAANVjQO4="
	tgTestBOCTestnetActive   = "te6cckEBBAEA/gABRYgBcanESVpG1u6Qwox1KlZqTfay3AgRXgBqcbb40HYvSpQMAQLgt4MfeDgu6S3TpR+PnHBPo4lXx/elG1azjRxITZL2Pl+vkrXjYoHzuEFX699JskpDfqm9l6VvHMHw5q55nTtZB/u6mch//3/9ZVPxAAAAAAVDSQjEwJIRP8ztTuwGGwnAK3uJNKnKVV4gMlBaq3VY6gIDAIA8v2bOFykNe5TNhkRDb+6Nr1RoPNgD3vhb/PbBbR9StQtMESsk/cx7+8SO+MDqEI3jVQAuIZ5xv1EoJTusXQQMAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAOnDjII="
)

func tgTestKeys(t *testing.T) (ed25519.PrivateKey, ed25519.PrivateKey) {
	t.Helper()
	words := splitWords(tgTestPhrase)
	anchor, signing := words[:12], words[12:]
	a, err := tgKeyOf(anchor)
	if err != nil {
		t.Fatalf("anchor key: %v", err)
	}
	s, err := tgKeyOf(signing)
	if err != nil {
		t.Fatalf("signing key: %v", err)
	}
	if got := hex.EncodeToString(a.Public().(ed25519.PublicKey)); got != tgTestAnchorPub {
		t.Fatalf("anchor public key = %s, want %s", got, tgTestAnchorPub)
	}
	if got := hex.EncodeToString(s.Public().(ed25519.PublicKey)); got != tgTestSigningPub {
		t.Fatalf("signing public key = %s, want %s", got, tgTestSigningPub)
	}
	return a, s
}

func splitWords(s string) []string { return strings.Fields(s) }

func TestTgActivateParity(t *testing.T) {
	anchorPriv, signingPriv := tgTestKeys(t)

	cases := []struct {
		name       string
		testnet    bool
		active     bool
		seqno      uint32
		validUntil uint32
		uq, eq     string
		boc        string
	}{
		{"mainnet-uninitialized", false, false, 0, 1700000000, tgTestMainnetUQ, tgTestMainnetEQ, tgTestBOCMainnetInactive},
		{"mainnet-active", false, true, 7, 1700000000, tgTestMainnetUQ, tgTestMainnetEQ, tgTestBOCMainnetActive},
		{"testnet-active", true, true, 5, 1700000000, tgTestTestnetUQ, tgTestTestnetEQ, tgTestBOCTestnetActive},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			net := tgNetworks["mainnet"]
			if c.testnet {
				net = tgNetworks["testnet"]
			}
			addr, uq, eq, err := tgAddress(anchorPriv.Public().(ed25519.PublicKey), net.testnet)
			if err != nil {
				t.Fatalf("address: %v", err)
			}
			if uq != c.uq || eq != c.eq {
				t.Fatalf("address = %s / %s, want %s / %s", uq, eq, c.uq, c.eq)
			}
			boc, err := tgBuildSignedMessage(anchorPriv, signingPriv, addr, net.subwallet, c.active, c.seqno, c.validUntil)
			if err != nil {
				t.Fatalf("build message: %v", err)
			}
			// EN: The BoC cell order is serializer-specific, but the root hash covers the whole
			// EN: reachable cell graph, so equal hashes mean a byte-identical message.
			// RU: Порядок ячеек в BoC зависит от сериализатора, но корневой хеш покрывает весь
			// RU: достижимый граф ячеек, поэтому равные хеши означают побайтово одинаковое сообщение.
			got, want := tgBOCRootHash(t, boc), tgBOCRootHash(t, c.boc)
			if !bytes.Equal(got, want) {
				t.Fatalf("message hash mismatch\n got: %x\nwant: %x", got, want)
			}
		})
	}
}

func tgBOCRootHash(t *testing.T, boc string) []byte {
	t.Helper()
	raw, err := base64.StdEncoding.DecodeString(boc)
	if err != nil {
		t.Fatalf("decode BoC: %v", err)
	}
	c, err := cell.FromBOC(raw)
	if err != nil {
		t.Fatalf("parse BoC: %v", err)
	}
	return c.Hash()
}

func TestTgCheckMatchesActivation(t *testing.T) {
	out := tgCheck(splitWords(tgTestPhrase), "mainnet")
	if err, ok := out["error"]; ok {
		t.Fatalf("tgCheck: %v", err)
	}
	if out["pub"] != tgTestAnchorPub {
		t.Fatalf("tgCheck pub = %v, want %s", out["pub"], tgTestAnchorPub)
	}
	if out["signpub"] != tgTestSigningPub {
		t.Fatalf("tgCheck signpub = %v, want %s", out["signpub"], tgTestSigningPub)
	}
	if out["uq"] != tgTestMainnetUQ || out["eq"] != tgTestMainnetEQ {
		t.Fatalf("tgCheck address = %v / %v", out["uq"], out["eq"])
	}
}
