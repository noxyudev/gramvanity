// EN: tgkey.go - the tg-wallet keys and the signed external message: BIP39 half derivation, the rev00
// EN: storage/StateInit cells and the deploy + key-rotation message builder with its local self-checks.
// RU: tgkey.go - ключи tg-wallet и подписанное внешнее сообщение: вывод половины BIP39, ячейки
// RU: хранения/StateInit rev00 и сборка сообщения развёртывания + смены ключа с локальными самопроверками.
package main

import (
	"bytes"
	"crypto/ed25519"
	"crypto/sha512"
	"encoding/base64"
	"fmt"
	"strings"

	"github.com/xssnick/tonutils-go/address"
	"github.com/xssnick/tonutils-go/ton/wallet/hdwallet"
	"github.com/xssnick/tonutils-go/tvm/cell"
	"golang.org/x/crypto/pbkdf2"
)

// EN: tgRotationTag is the KeyRotationProofPayload tag (96 bits, ASCII "KEY_ROTATION").
// RU: tgRotationTag - тег KeyRotationProofPayload (96 бит, ASCII "KEY_ROTATION").
var tgRotationTag = []byte("KEY_ROTATION")

// EN: tgKeyOf is the BIP39 half derivation used by gramvanity and by tg-activate.mjs: always
// EN: PBKDF2-HMAC-SHA512("mnemonic", 2048) + SLIP-0010 m/44'/607'/0'. Unlike the TON-standard
// EN: import path it never falls back to the TON salt, so a phrase that happens to satisfy the TON
// EN: seed-version check still gets the BIP39 key the search actually produced.
// RU: tgKeyOf - вывод половины BIP39, используемый gramvanity и tg-activate.mjs: всегда
// RU: PBKDF2-HMAC-SHA512("mnemonic", 2048) + SLIP-0010 m/44'/607'/0'. В отличие от пути импорта по
// RU: стандарту TON он никогда не переходит на соль TON, поэтому фраза, случайно прошедшая проверку
// RU: seed-version TON, всё равно даёт ключ BIP39, который реально получил поиск.
func tgKeyOf(words []string) (ed25519.PrivateKey, error) {
	seed := pbkdf2.Key([]byte(strings.Join(words, " ")), []byte("mnemonic"), 2048, 64, sha512.New)
	dk, err := hdwallet.Derived("m/44'/607'/0'", seed)
	if err != nil {
		return nil, err
	}
	return ed25519.NewKeyFromSeed(dk.PrivateKey), nil
}

// EN: tgStorage is the rev00 WalletTg storage cell: revision 0 | seqno 0 | subwallet_id | public_key.
// RU: tgStorage - ячейка хранения WalletTg rev00: revision 0 | seqno 0 | subwallet_id | public_key.
func tgStorage(anchorPub ed25519.PublicKey, subwallet uint32) *cell.Cell {
	return cell.BeginCell().
		MustStoreUInt(0, 8).
		MustStoreUInt(0, 32).
		MustStoreUInt(uint64(subwallet), 32).
		MustStoreSlice(anchorPub, 256).
		EndCell()
}

// EN: tgStateInit is the StateInit cell for the WalletTg trampoline code and its storage.
// RU: tgStateInit - ячейка StateInit для кода trampoline WalletTg и его хранилища.
func tgStateInit(anchorPub ed25519.PublicKey, subwallet uint32) (*cell.Cell, *cell.Cell, error) {
	code, err := cell.FromBOC(trampolineBOC)
	if err != nil {
		return nil, nil, err
	}
	data := tgStorage(anchorPub, subwallet)
	si := cell.BeginCell().MustStoreUInt(0b00110, 5).MustStoreRef(code).MustStoreRef(data).EndCell()
	return code, si, nil
}

// EN: tgBuildSignedMessage returns the base64 BoC of the external message that deploys (when the
// EN: account is not active yet) and rotates the key to the signing key.
// RU: tgBuildSignedMessage возвращает base64 BoC внешнего сообщения, которое разворачивает контракт
// RU: (если аккаунт ещё не активен) и меняет ключ на подписывающий.
func tgBuildSignedMessage(anchorPriv, signingPriv ed25519.PrivateKey, addr *address.Address,
	subwallet uint32, active bool, seqno uint32, validUntil uint32) (string, error) {

	anchorPub := anchorPriv.Public().(ed25519.PublicKey)
	signingPub := signingPriv.Public().(ed25519.PublicKey)
	_, si, err := tgStateInit(anchorPub, subwallet)
	if err != nil {
		return "", err
	}
	addrHash := addr.Data()

	// EN: KeyRotationProofPayload: tag:96 | workchain:int8 | wallet address hash:256
	// RU: KeyRotationProofPayload: tag:96 | workchain:int8 | хеш адреса кошелька:256
	rotationPayload := cell.BeginCell().
		MustStoreSlice(tgRotationTag, 96).
		MustStoreInt(0, 8).
		MustStoreSlice(addrHash, 256).
		EndCell()
	rotationSignature := ed25519.Sign(signingPriv, rotationPayload.Hash())

	// EN: ChangePublicKeyRequestE: opcode | subwallet | validUntil | seqno | new key |
	// EN: ref(rotation signature) | ref(encrypted old key, unused on the first rotation)
	// RU: ChangePublicKeyRequestE: opcode | subwallet | validUntil | seqno | новый ключ |
	// RU: ref(подпись смены ключа) | ref(зашифрованный старый ключ, не используется при первой смене)
	request := cell.BeginCell().
		MustStoreUInt(tgChangeKeyOpcode, 32).
		MustStoreUInt(uint64(subwallet), 32).
		MustStoreUInt(uint64(validUntil), 32).
		MustStoreUInt(uint64(seqno), 32).
		MustStoreSlice(signingPub, 256).
		MustStoreRef(cell.BeginCell().MustStoreSlice(rotationSignature, 512).EndCell()).
		MustStoreRef(cell.BeginCell().MustStoreUInt(0, 256).EndCell()).
		EndCell()
	signature := ed25519.Sign(anchorPriv, request.Hash())

	// EN: Body: anchor signature:512 then the request bits AND its refs.
	// RU: Тело: подпись якоря:512, затем биты запроса И его ссылки.
	body := cell.BeginCell().
		MustStoreSlice(signature, 512).
		MustStoreBuilder(request.ToBuilder()).
		EndCell()

	// EN: Local self-check: both signatures must verify before anything is sent.
	// RU: Локальная самопроверка: обе подписи должны проверяться до отправки чего-либо.
	if !ed25519.Verify(anchorPub, request.Hash(), signature) {
		return "", fmt.Errorf("internal error: the anchor signature does not verify")
	}
	if !ed25519.Verify(signingPub, rotationPayload.Hash(), rotationSignature) {
		return "", fmt.Errorf("internal error: the rotation signature does not verify")
	}
	if si.Hash() == nil || !bytes.Equal(si.Hash(), addrHash) {
		return "", fmt.Errorf("internal error: the state init does not hash to the wallet address")
	}

	// EN: External inbound message, laid out exactly like @ton/core storeMessage/external:
	// EN: ext_in_msg_info$10 src:addr_none dest:MsgAddressInt import_fee:Grams,
	// EN: init is Maybe(Either StateInit ^StateInit) as a ref (StateInit + body never fits inline),
	// EN: body is Either X ^X as a ref.
	// RU: Внешнее входящее сообщение, разложенное точно как @ton/core storeMessage/external:
	// RU: ext_in_msg_info$10 src:addr_none dest:MsgAddressInt import_fee:Grams,
	// RU: init — это Maybe(Either StateInit ^StateInit) ссылкой (StateInit + тело не влезают инлайн),
	// RU: body — это Either X ^X ссылкой.
	msg := cell.BeginCell()
	msg.MustStoreUInt(0b10, 2) // external-in
	msg.MustStoreUInt(0b00, 2) // src: addr_none
	msg.MustStoreUInt(0b10, 2) // dest: addr_std
	msg.MustStoreUInt(0, 1)    // no anycast
	msg.MustStoreInt(0, 8)     // workchain 0
	msg.MustStoreSlice(addrHash, 256)
	msg.MustStoreUInt(0, 4) // import fee 0
	if active {
		msg.MustStoreUInt(0, 1) // no init
	} else {
		msg.MustStoreUInt(1, 1) // init present
		msg.MustStoreUInt(1, 1) // init is a ref
		msg.MustStoreRef(si)
	}
	msg.MustStoreUInt(1, 1).MustStoreRef(body)

	return base64.StdEncoding.EncodeToString(msg.EndCell().ToBOCWithOptions(
		cell.BOCSerializeOptions{WithCRC32C: true, WithIndex: true})), nil
}
