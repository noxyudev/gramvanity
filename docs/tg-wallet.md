# Telegram Wallet mode (`--mode tg-wallet`)

> Language: **English** | [Русский](tg-wallet.ru.md)

This guide explains how a `tg-wallet` hit works, what the 24 words mean, how to choose the suffix,
how to get the wallet on chain, and how to import it into Telegram Wallet.

Commands below run from the `opencl/` directory after `make`; the verifier/activator is
`../verify-go/w5addr` (`..\verify-go\w5addr.exe` on Windows), and the default job folder is
`../results/job-<UTC timestamp>/`.

`tg-wallet` is the default `--mode`: a search started without `--mode` looks for a Telegram Wallet
address. Like every target, it stops after the first hit unless `--count N` asks for more.

## 1. What is being searched

Telegram Wallet (the Wallet in Telegram, powered by WalletTg) does not use W5 or the
`m/44'/607'/0'` derivation for its main wallet. Its contract, **WalletTg**, is deployed behind an
immutable **WalletTrampoline** code cell, and the address is the hash of the contract's state init:

```
code = WalletTrampoline   (JMP to the WalletTg code stored in the network config)
data = WalletTg storage, revision 00:
       revision:u8=0 | seqno:u32=0 | subwallet_id:u32 | public_key:256
```

The `subwallet_id` identifies the wallet type and the network:

| network | subwallet_id | address form |
|---|---|---|
| mainnet | `0x7FFF7F11` | `UQ…` / `EQ…` |
| testnet | `0x7FFF7FFD` | `0Q…` / `kQ…` |

The trampoline code cell is frozen (immutable), so every Telegram Wallet address stays valid even
when the WalletTg implementation is upgraded. gramvanity uses the trampoline code hash with depth 0,
so its addresses match what Telegram Wallet shows for the same key.

## 2. How the 24 words work

A `tg-wallet` phrase is **two independent BIP39-12 phrases glued together** — the same layout Wallet
in Telegram uses for its own phrase, so it is *not* a BIP39-24 phrase (each half carries its own
checksum).

```
words  1-12  "anchor"   BIP39-12 -> key via m/44'/607'/0' -> public key in the state init
words 13-24  "signing"  BIP39-12 -> key via m/44'/607'/0' -> the key Telegram Wallet signs with
```

- **Words 1-12 (anchor)** are the key the contract is *deployed* with. They are the only half that
  affects the address, so the search runs over this half — exactly as fast as `bip39-12`.
- **Words 13-24 (signing)** are the key the contract is *rotated to* during activation, and the key
  Telegram Wallet actually signs transactions with. They do not affect the address at all: the same
  address works with any signing half.

Two properties are enforced on both halves:

1. each half must be a valid BIP39-12 phrase (valid checksum), so wallets accept it;
2. neither half may be valid as a TON standard phrase (the "basic seed" check). Otherwise a wallet
   (Tonkeeper, Wallet) could interpret the same words differently. Such hits are skipped.

### Deterministic search and the signing half

Searches are deterministic and resumable by default (with `--start-offset` you can also split them
into ranges). The signing half of a hit is derived from the same secret search key through a
separate HMAC domain (`DETSIGN`), so the *exact* 24 words are reproducible later:

```sh
./gramvanity --mode tg-wallet --suffix 'HELLO' --start-offset 0 --tries 100000000 --out ../results/tg-hello
./gramvanity --reveal ../results/tg-hello/job.ckpt 1234567   # 24 words, both keys, address
./gramvanity --mode tg-wallet --derive <key> 1234567         # same, from the key printed in the hit file
```

A new job always gets a new random search key, so two identical commands started separately search
different phrases; continue a job with `--resume` and its checkpoint.

With `--random` the signing half is fresh randomness generated at the moment a hit is written, so
keep the hit file: it is the only place that has (or can regenerate) it. Random mode cannot be
resumed and cannot be combined with `--start-offset`, `--tries`, `--checkpoint` or `--resume`.

## 3. Choosing the suffix

A TON address has 48 base64url characters of payload; the last characters are what you search.
The cost of a pattern is a product of factors, one per pattern character:

| pattern character | cost factor | example |
|---|---|---|
| a literal character | `64` (all 64 base64url characters) | `O` matches only `O` |
| a letter in `--ignore-case` runs | `32` (both cases) | `o` matches `o` and `O` |
| a character class `[abc]` | `64 / k` (k = number of characters in the class) | `[a-z_]` ≈ `2.37` |

| suffix | expected candidates | on one RTX 4070 Ti Super (~681k/s, `--power 100`) |
|---|---|---|
| `ZQ` | ~4 000 | seconds — good for a test run |
| `HELLO` | ~1.1 billion (`64^5`) | ~26 min |
| `[a-z_]HELLO` | ~2.5 billion (`27 * 64^5`) | ~12 h |
| six fixed characters | ~69 billion | ~1.2 days |
| seven fixed characters | ~4.4 trillion | ~2.5 months |

At the default `--power 65` the times grow by about 50%.

Rules of thumb:

- The startup line `expected ~N phrases until the first hit across all targets` is exact for your
  patterns, and the one-time estimate about 4 s after startup prints the average wait per target.
  Read it before committing to a long run. When a resumed job already has targets whose `--count`
  is full, the line says `across the N remaining target(s)` and skips them; the periodic `% of
  expected` line drops finished targets the same way, so the displayed odds stay current.
- Each extra character multiplies the cost by up to 64. Decide the length first, then the exact
  characters. Bracket patterns exist to cover several spellings of the same length
  (`[Hh]ELLO`, `[a-z_]hello`), not to make things cheaper — `HELLO` is cheaper than `[a-z_]HELLO`
  because `[a-z_]HELLO` has one more character.
- One- and two-character endings are a bad idea for a real wallet: they are so cheap that an
  attacker can find a *different* phrase with the same ending trivially (address poisoning).
  5+ fixed characters is the usual minimum, 6-7 if you want the ending to be practically unique.
- Case matters: the address alphabet is case-sensitive, so `hello` and `HELLO` are different
  suffixes. `--ignore-case` allows any case in the found address, which is friendlier to the eye
  but halves the cost per letter — good for long readable words.
- You may start several jobs in parallel with disjoint index ranges (`--start-offset`, `--tries`)
  and different checkpoints; each job gets its own random search key, so they search independent
  candidate sets. A hit on any of them is enough, then stop the others.
- Test everything on **testnet first** (`--net testnet`, free GRAM from
  <https://t.me/testgiver_ton_bot>): the whole flow — search → activate → import — works there
  except that Telegram Wallet itself only uses mainnet.

## 4. Activation (this is the unusual part)

A fresh address from the search is just a hash: the contract is *not deployed*, there is no key on
chain, and no wallet — including Telegram Wallet — can use the phrase yet. Creating the wallet means
sending one external message that does everything at once, exactly like the Telegram Wallet app when
it creates a wallet:

1. deploy the trampoline + WalletTg with the anchor key;
2. rotate the key to the signing half (`ChangePublicKeyRequestE`), signed by the anchor key;
3. the rotation payload references a proof signed by the new signing key, so the contract proves it
   controls both halves.

`verify-go/w5addr activate` (part of the Go verifier) builds and, with `--send`, submits this
message:

```sh
# fund the address first: 0.05-0.1 TON is comfortable, 0.02 TON is the minimum the tool accepts
../verify-go/w5addr activate --phrase "w1 … w24" --check    # address, on-chain state, balance
../verify-go/w5addr activate --phrase "w1 … w24" --send     # deploy + rotate in one message
../verify-go/w5addr activate --hit ../results/tg-testnet/hit-t1-….txt --send # --random hit file: phrase inside
```

Default hit files do not contain the phrase; pipe it from `--reveal` instead:
`./gramvanity --reveal ../results/tg-main/job.ckpt <index> | ../verify-go/w5addr activate --phrase - --send`.

On Windows use `..\verify-go\w5addr.exe`. `--phrase -` reads the phrase from stdin, which is handy
for piping `--reveal` output without printing the words.

After `--send`, wait a few seconds and run `--check` again: it should report the contract as
active with `on-chain key` equal to the signing key. That is the same key Telegram Wallet derives
from words 13-24, so the phrase is now a normal Telegram Wallet phrase.

Notes:

- The activation message is signed by the anchor key (words 1-12). After activation the anchor key
  is dead — keep it anyway, the phrase is all-or-nothing (24 words).
- `--net testnet` does the same on testnet; get testnet GRAM from
  <https://t.me/testgiver_ton_bot>.
- If the address already has a balance and correct state (e.g. you re-run `--send`), the tool
  refuses to double-activate and tells you what it sees.

## 5. Importing into Telegram Wallet

1. Make sure the wallet was activated (section 4) and `--check` shows the signing key on chain.
2. Open Telegram → **Wallet** → menu → **Settings** → **Import wallet** (the exact wording depends
   on the Telegram version).
3. Enter the 24 words and pick the right wallet version if asked. The wallet shows the address.
4. Compare the address with the one printed by `gramvanity --reveal` / `--check`. They must match
   character by character.
5. Send a small test amount in and out before moving anything serious.
6. Write the 24 words down on paper (not in Telegram's "saved messages"), then delete the job
   folder (hit files, stop report and checkpoint): whoever has the checkpoint's search key can
   derive every candidate phrase of the job.

## 6. Cost considerations

- Activation costs a normal external message fee (a few hundredths of a TON including the 0.02 TON
  minimum balance you must leave on the fresh account).
- The deployed wallet is a normal WalletTg contract; its storage rent is covered the usual way when
  the wallet holds a small balance.
- Vanity suffix = raise the entry price for address-poisoning scams: an attacker who wants to trick
  you by sending from a look-alike address has to pay for the same suffix themselves, growing with
  the number of characters you pin.

## 7. Quick recipe

```sh
# 1. testnet experiment (free, fast, one-off: --random keeps the phrase in the hit file)
./gramvanity --mode tg-wallet --net testnet --suffix ZQ --random --out ../results/tg-testnet
../verify-go/w5addr activate --hit ../results/tg-testnet/hit-t1-*.txt --net testnet --check
# fund it from https://t.me/testgiver_ton_bot, then:
../verify-go/w5addr activate --hit ../results/tg-testnet/hit-t1-*.txt --net testnet --send

# 2. mainnet, a large bounded range (deterministic and resumable by default)
./gramvanity --mode tg-wallet --suffix '[a-z_]HELLO' --start-offset 0 \
             --tries 100000000000 --out ../results/tg-main
#    (Ctrl+C any time; the checkpoint is ../results/tg-main/job.ckpt; continue later with:)
./gramvanity --resume ../results/tg-main/job.ckpt

# 3. after a hit: recover the 24 words, activate, import
./gramvanity --reveal ../results/tg-main/job.ckpt <index>  # or the --derive command from the hit file
../verify-go/w5addr activate --phrase "<24 words>" --check
../verify-go/w5addr activate --phrase "<24 words>" --send
```
