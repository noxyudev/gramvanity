# gramvanity

> Language: **English** | [Русский](README.ru.md)

**Choose how your TON wallet address looks — and get a real seed phrase that produces it.**

A wallet address is usually a random string like `UQAbC…7xQ2`. gramvanity searches for a seed phrase
whose address ends with something you pick: `HELLO`, `777`, your nickname. When it finds one, you
import the phrase into an ordinary wallet — **Tonkeeper** or **My Wallet** or **Telegram Wallet** —
choose the W5 wallet, and the wallet shows exactly that address. Nothing is fake or wrapped: it is
a normal seed phrase with a normal address.

The search runs on your graphics card, so a four- or five-character ending takes seconds to about an
hour at the default power setting — not weeks.

**Telegram Wallet addresses are the default** (`--mode tg-wallet`). They use a special 24-word
phrase and need one online step (activation) before the wallet can be imported. Other formats are
chosen with `--mode`: `bip39-12` or `bip39-24` for Tonkeeper and My Wallet, `ton-24` for the TON
standard mnemonic. The Telegram Wallet guide: [docs/tg-wallet.md](docs/tg-wallet.md).

Project channel: **[t.me/gramvanity](https://t.me/gramvanity)**.

## Where this came from

- **Original project:** [necrovolt/seedvanity](https://github.com/necrovolt/seedvanity), created by
  [miutalk](https://t.me/miutalk) and [Vibes 18+](https://t.me/vibes).
- **This fork:** maintained by **[noxyu](https://t.me/pauper)**.

The fork keeps the same idea and the same search engine, and changes the parts that get in the way
of real use:

- the macOS build is gone — only the OpenCL build remains, for Linux and Windows;
- one run can search **several addresses at once**, each with its own patterns, limits and output
  folder;
- every run keeps all of its files in **one job folder** (hits, stop report, checkpoint);
- the checkpoint format was extended to store those targets; checkpoints from older versions still
  load and continue normally;
- hit files name every target they matched (`hit-A+B-<index>.txt`).

## Why not a mining wrapper?

There is another well-known approach: [ton-org/vanity](https://github.com/ton-org/vanity) mines a
salt for a wrapper contract tied to an owner address. It is extremely fast, but the result is a
contract, not your wallet. If you import the seed phrase into a wallet, the wallet computes the
standard address from the key and will not show the mined contract.

gramvanity does the slower but honest thing: it searches over seed phrases themselves. Whatever the
wallet derives on import is what you get. The price is a full key derivation per candidate — which
is exactly the work a GPU is good at.

## How it works, in plain words

1. The program makes up a random seed phrase, just like a wallet would.
2. It turns the phrase into a private key, then a public key, the same way Tonkeeper and My Wallet
   do on import.
3. It builds the wallet address from that key.
4. It compares the address with your pattern. If the address fits, the phrase (or the search key and
   index that recreate it) is saved.
5. If not, the program throws the phrase away and tries another one.

Steps 2–4 take a few microseconds or less on a GPU, so the program can try hundreds of thousands of
phrases per second. Every hit is then recomputed on the CPU and, when the verifier is installed,
checked by the independent tonutils-go implementation before it is reported.

## What you need

- A GPU with OpenCL 1.2 support: NVIDIA, AMD or Intel. The driver you already use for games or
  graphics normally includes OpenCL.
- Linux (x86_64 or arm64) or Windows (x86_64).
- Nothing else to run a release build: no Node.js, no Go, no Python, no compiler.

The search itself needs no TON node, no internet connection, and no account anywhere. It runs fully
local.

## Install

### Prebuilt binaries (recommended)

Download the archive for your system from the releases page and unpack it. A release keeps the
repository layout, because the search looks for the verifier and writes `results/` one level above
its own folder:

```text
opencl/gramvanity        # or opencl/gramvanity.exe — the search
verify-go/w5addr         # or verify-go/w5addr.exe — the independent verifier / tg-wallet activator
```

Unpack it anywhere, `cd opencl`, and run `./gramvanity` (Windows: `.\gramvanity.exe`). Do not move
the two files apart, and keep the `opencl/` and `verify-go/` folders side by side. No installation
step is needed.

### Build from source

First clone the repository and build the verifier:

```sh
git clone <repository-url> gramvanity
cd gramvanity
go build -o verify-go/w5addr ./verify-go     # tonutils-go verifier (w5addr.exe on Windows)
```

Then build the search with the platform commands below (Linux / Windows).

The verifier is optional. If it is missing, the program still works, but hits are marked as
unverified and you should not use them.

### Linux (Ubuntu and similar)

```sh
sudo apt install build-essential ocl-icd-opencl-dev clinfo
clinfo -l                 # your GPU should be listed here
cd opencl
make
```

GPU runtimes: NVIDIA drivers already include OpenCL; AMD uses ROCm OpenCL; for Intel install
`intel-opencl-icd`.

### Windows

Install [MSYS2](https://www.msys2.org/), open the **MSYS2 UCRT64** shell, then:

```sh
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-opencl-icd mingw-w64-ucrt-x86_64-opencl-headers
cd /c/path/to/gramvanity/opencl
make
```

From then on, run `./gramvanity` in that same shell. The GPU driver already contains OpenCL, so
there is nothing else to install.

After the build, all further commands in this guide run from the `opencl/` folder.

## First run

From the `opencl/` folder:

```sh
./gramvanity --list-devices                               # show the GPUs OpenCL can see
./gramvanity --mode bip39-12 --bench 60                   # 1-minute speed test
./gramvanity --mode bip39-12 --bench 60 --power 100       # same at full speed
```

`--bench` only measures speed; it finds nothing. The first 10–20 seconds are also used to tune the
batch size and kernel length for your GPU.

By default the program uses **65%** of your GPU/CPU time (`--power 65`). That keeps the machine
usable and the fans quiet. Use `--power 100` if you want the maximum rate.

## Find your address

```sh
# address ends with HELLO (any single lowercase letter or _ before it)
./gramvanity --suffix '[a-z_]HELLO' --mode bip39-12
```

Without `--mode` the default is `tg-wallet` (Telegram Wallet, 24 words and activation). The example
above asks for `bip39-12`, which Tonkeeper and My Wallet import directly.

A run stops after the first saved hit by default (`--count 1` per target). Pass `--count N` to
collect several addresses for the same pattern, and `--all-patterns` to apply the count to every
pattern variant of a target (see "Several addresses in one run").

While it runs you will see:

- a long line every 30 seconds: speed, how far the search has gone, how many hits were found, and
  how likely a hit is by now;
- a one-time estimate about four seconds after startup: the average wait for each target and for
  the first hit on any target. Read it before starting a run that may take days.

A hit is saved to a file; the seed phrase is never printed to the terminal. By default the hit file
stores the search key and index, so recover the phrase later with `--reveal` or `--derive` (with
`--random` the phrase itself is in the file). The program also re-derives the key on the CPU and asks
the independent verifier (when installed) to confirm the address before it reports it.

## How to write a pattern

- Plain text matches exactly, case-sensitively: `--suffix HELLO` looks for `…HELLO`, not `…hello`.
- `[...]` means "one character from this list". Ranges work: `[a-z]` is any lowercase letter,
  `[0-9A-F]` is a hex digit. So `[a-z_]HELLO` means "a lowercase letter or _, then HELLO".
- `--prefix` matches the beginning of the address right after the two-character form tag (`UQ`, or
  `EQ`/`0Q`/`kQ` depending on the options).
- `--suffix` matches the end of the address.
- `--ignore-case` lets the found address use any capitalization; helpful when the ending should look
  good rather than be a specific spelling.
- If a target has both a prefix and a suffix, the address must satisfy **both**. Alternatives
  inside one list are joined with "or": you can repeat `--suffix` or separate values with commas.
- `--match or` relaxes that rule: one group suffices, so `--prefix MOON --suffix 777 --match or`
  finds addresses that start with MOON **or** end with 777. The option works globally (before the
  first `--target`) and inside a target, where it overrides the global value.
- Addresses only use `A-Z a-z 0-9 - _`. Always put patterns containing `[...]` in quotes, otherwise
  the shell may try to expand them.

Examples:

```sh
./gramvanity --suffix '777' --mode bip39-12
./gramvanity --suffix '[Hh]ELLO' --mode bip39-12          # HELLO or hELLO
./gramvanity --prefix MOON --suffix '777' --mode bip39-12 # starts with MOON, ends with 777
./gramvanity --suffix 'HELLO,MOON' --mode bip39-12        # either ending
./gramvanity --prefix MOON --suffix '777' --match or --mode bip39-12 # MOON at the start or 777 at the end
```

## How long it takes

Search time grows by roughly 64× for every extra fixed character. The table assumes about 681,000
phrases per second, which is full speed (`--power 100`); the default `--power 65` adds about 50%
to these times.

| Ending | Average time |
|---|---|
| 4 characters | ~25 seconds |
| 5 characters (`HELLO`) | ~26 minutes |
| `[a-z_]` + 5 characters | ~12 hours |
| 6 characters | ~1.2 days |
| 7 characters | ~2.5 months |

These are averages, not deadlines. The outcome is random: half of all runs finish within 0.7× the
average, 90% within 2.3×, 99% within 4.6×. Stopping and restarting loses nothing.

For reference, on an NVIDIA RTX 4070 Ti Super with a Ryzen 7 7800X3D under Windows at
`--power 100`: about 681,000 phrases per second for `bip39-12` and 675,000 for `tg-wallet`. PBKDF2
alone reaches about 696,000, so the search is limited by PBKDF2, not by address checking.

Practical advice:

- Decide the length first, then the exact characters.
- Brackets are for covering spellings (`[Hh]ELLO`), not for making the search cheaper — they add a
  character to the cost.
- Avoid one- and two-character endings for a wallet you will actually use: they are cheap enough
  for someone else to find a different phrase with the same ending (address poisoning).
- Five fixed characters is a reasonable minimum; six or seven make the ending practically unique.

## Several addresses in one run

Each search block starts with `--target [NAME]`. Everything after it belongs to that target until
the next `--target`:

```sh
./gramvanity --mode bip39-12 \
  --target HELLO --suffix '[a-z_]HELLO' --count 1 \
  --target MOON  --prefix MOON --suffix '777' --count 2 \
  --target SHORT --prefix '[A-Z]' --suffix 'X'
```

Rules, in plain words:

- Without any `--target`, the run has one unnamed target — the usual case.
- A target without a name is called `t1`, `t2`, … in the order it appears. If a name repeats, the
  second one becomes `A2`, then `A3`, and so on.
- A target name appears in hit file names. Cyrillic names are transliterated to an ASCII id
  (`--target Паша` writes `hit-Pasha-…`), while the original name is kept as a label in reports and
  the checkpoint. Explicit ids may contain only letters, digits, `-` and `_`.
- There is no fixed limit on the number of targets: the target list grows as needed and is bounded
  only by memory.
- `--count N` means "stop this target after N hits". The default is `1`, for every target including
  all targets of a multi-target run; use a larger `--count` to collect several addresses for one
  pattern.
- `--all-patterns` moves the count from the target to its pattern variants: `--count N` then applies
  to every prefix and every ending of the target, and the search runs until each one has N hits.
  For example `--target PASHA --suffix '[_-]Pasha' --suffix '[_-]PASHA' --count 1 --all-patterns`
  finds one address per variant instead of stopping at the first. A hit counts toward every pattern
  it matches, and hits that fill no still-missing variant are reported but not saved. The flag works
  as a global default too and is stored in the checkpoint (so a resumed job keeps it, and it can be
  added later with `--resume ... --all-patterns`).
- Options written **before** the first `--target` are defaults. The same option inside a target
  overrides it for that target only.
- `--match and|or` chooses how the two groups of a target combine: `and` (default) needs one matching
  prefix and one matching suffix; `or` accepts an address that satisfies either group alone.
- `--mode`, `--form` and `--net` are global: every target in a run uses the same wallet format.

When one address fits several targets, it is saved once, both targets count the hit, and the file
name lists all of them: `hit-A+B-<index>.txt`. If two targets write to different `--out` folders,
each folder gets its own copy. If they share a folder, the file is written once.

A target that reaches its `--count` is skipped; the other targets keep searching. The run stops when
every target has reached its count (the normal ending, since every target has a limit by default),
when you stop it, or when a fixed-size deterministic range ends.

## Where the files go

Every run gets its own folder. By default it is `results/job-<timestamp>/` in the repository
(`../results/job-<timestamp>/` when you run from `opencl/`):

```text
job-20261028-101500/                       # inside results/
  hit-HELLO-123456.txt                     # one file per hit
  hit-MOON+SHORT-654321.txt                # an address that matched two targets
  stop-bip39-12-idx1234567-20261028-104500.txt
  job.ckpt                                 # saved search state
```

`--out DIR` before the first target chooses that folder yourself. Inside a target, `--out` changes
only where that target's hits are written.

- **Hit file** — the address, the public key, and what was checked. By default the phrase is
  deliberately **not** written: the file contains the search key and index instead, and you recover
  the phrase with `--reveal` or `--derive`. With `--random` the file contains the phrase itself,
  because there is no search key to replay.
- **Stop file** — written at the end of every session (Ctrl+C, window close, finished range, all
  counts reached). It contains statistics, the list of hit files, and the exact command to continue.
- **Checkpoint** — the saved state. Keep it secret: it contains the search key, which can recreate
  every phrase the job would ever produce.

## Wallet formats (`--mode`)

| Mode | Phrase | Imports into | Speed |
|---|---|---|---|
| `bip39-12` | 12 words, BIP39 | Tonkeeper, My Wallet | full |
| `bip39-24` | 24 words, BIP39 | Tonkeeper, My Wallet | about the same |
| `ton-24` | 24 words, TON standard | every TON wallet | ~100× slower |
| `tg-wallet` | 24 words = two BIP39-12 halves | Telegram Wallet, after activation | same as `bip39-12` |

By default the address is the W5 (v5r1) wallet on mainnet, workchain 0, subwallet 0, and the phrase
format is `tg-wallet`. `--form eq` shows the same wallet in its bounceable `EQ…` form instead of
`UQ…`.

`ton-24` is much slower and is rarely what you want for a new wallet. Use it only if the phrase must
be a TON standard mnemonic.

## Telegram Wallet

In `tg-wallet` mode the phrase is 24 words, but it is **not** one 24-word BIP39 phrase. It is two
independent 12-word phrases:

- words 1–12 (the *anchor*) define the address and are the half the search runs over;
- words 13–24 (the *signing* half) become the key Telegram Wallet signs with.

A new address exists only on paper: nothing is deployed on chain yet. Before Telegram Wallet accepts
the phrase, the wallet must be created and switched to the signing key in one message. The included
Go verifier does exactly that:

```sh
../verify-go/w5addr activate --phrase "w1 … w24" --check    # address, balance, state
../verify-go/w5addr activate --phrase "w1 … w24" --send     # deploy + rotate (needs >= 0.02 GRAM)
```

On Windows use `..\verify-go\w5addr.exe`. Fund the address first (0.05–0.1 TON is comfortable), wait
for `--check` to see the balance, then send. On testnet use `--net testnet` and get free GRAM from
[https://t.me/testgiver_ton_bot](https://t.me/testgiver_ton_bot).

Full details, including the exact message layout and import steps:
[docs/tg-wallet.md](docs/tg-wallet.md).

## Stopping, resuming, and replaying

Searches are **deterministic and resumable by default**. Every new job gets a random secret 32-byte
search key, and candidate number `index` is derived from that key:

- within one job, candidate number 1000 is always the same phrase, no matter when you continue it;
- the search can be stopped and continued exactly where it stopped;
- the job folder can be moved to another machine and continued there;
- any hit can be replayed later from its index, together with the checkpoint.

A new job always gets a new random key, so two identical commands started separately search
different phrases. To continue a job, use `--resume` with its checkpoint — a fresh run with
`--checkpoint FILE` replaces that file and starts a new sequence.

```sh
# deterministic and resumable; stops after the first hit (default --count 1)
./gramvanity --mode bip39-12 --suffix '[a-z_]HELLO'

# five addresses for the same pattern
./gramvanity --mode bip39-12 --suffix '[a-z_]HELLO' --count 5

# a bounded range: three million candidates starting at index 1,000,000,000
./gramvanity --mode bip39-12 --suffix '[a-z_]HELLO' --start-offset 1000000000 --tries 3000000

# continue later (the stop file prints this exact command)
./gramvanity --resume ../results/job-20261028-101500/job.ckpt

# after a hit the default count is full: ask for one more address and extend the range
./gramvanity --resume job.ckpt --count 2 --tries 2000000

# recover a hit without the phrase in the file
./gramvanity --reveal job.ckpt 123456
./gramvanity --mode bip39-12 --derive <64-hex search key> 123456

# inspect a checkpoint (the secret key is not printed)
./gramvanity --ckpt-info job.ckpt
```

`--random` opts out: every candidate is fresh OS randomness, there is no search key, and nothing can
be resumed. Use it for short one-off searches only. It cannot be combined with `--start-offset`,
`--tries`, `--checkpoint`, `--resume` or `--stop-after`.

The checkpoint is saved every 30 seconds by default
(`--checkpoint-interval SEC`). If the machine crashes, at most the work since the last save is
repeated. It never skips candidates: the saved position is the last index whose address was fully
checked.

On `--resume` all targets come back from the checkpoint. You may change their `--count` and
`--out`; patterns, `--ignore-case` and `--match` must stay the same and are checked. A named `--target` block
picks the checkpoint target by its id or its label (so `--target Паша` and `--target Pasha` both
work); a block without a name picks the next unused target in order. Raising the count of a finished
target continues from the current shared index, so already checked candidates are not repeated.

Checkpoints are version 8. Files from versions 2–7 still load; 2–4 load as one legacy target with
their original "any pattern matches" behaviour, so old jobs can be resumed.

## Staying safe

- The seed phrase is written **only** to hit files (or a `--dump` file you ask for), never to the
  terminal. On Linux the files are readable by your user only. By default the phrase is not in the
  hit file at all (only the search key and index); `--random` stores the phrase there instead.
- Keep the job folder private. The checkpoint can recreate every candidate phrase of the job.
- Every hit is checked independently: the program recomputes it on the CPU, and (when installed) the
  verifier re-derives the address with the wallets' own import logic. A hit that fails is written as
  `UNVERIFIED HIT, DO NOT USE`.
- Phrases that wallets could read in two different ways are skipped on purpose, because different
  wallets would show different addresses for them.
- The test phrases printed in the source code or used by the tests are public throwaway phrases.
  Never send funds to them.
- Before sending real money: import the phrase, select W5, compare the address, and make a small
  test transfer out and back. In `tg-wallet` mode compare the address with `--check` or a block
  explorer after activation.
- Then write the phrase down on paper, and delete the job folder.

## If something goes wrong

- `no OpenCL GPU found` — the GPU driver does not expose OpenCL. Check `clinfo -l` on Linux, and
  install the OpenCL package of your GPU vendor.
- `OpenCL kernel build failed` — please report the full error text.
- `SELF-TEST FAILED` — the GPU or driver computes wrong results. Do not search on that device.
- `GRAMVANITY_DEBUG=1` prints internal tuning details of every batch.
- Long runs on Windows: keep the window open and disable sleep in the power settings.

Build instructions and internals: [opencl/README.md](opencl/README.md).

## Project layout

- `opencl/` — the program itself (C + OpenCL). Build instructions and internals:
  [opencl/README.md](opencl/README.md).
- `opencl/src/` — the host program's modules (platform, pattern, crypto, addresses, gpu, checkpoint,
  hits, search, ...); see [docs/architecture.md](docs/architecture.md) for the module map, the
  ownership of shared state and the lock discipline.
- `docs/` — guides, e.g. [docs/tg-wallet.md](docs/tg-wallet.md) for Telegram Wallet.
- `verify-go/`, `go.mod`, `go.sum` — the Go verifier (tonutils-go) and the `activate` command.
- `opencl/tests/` — end-to-end tests.
- `results/` — created at run time; one `job-<timestamp>/` folder per run, ignored by git.

## Credits

Original project and idea: [miutalk](https://t.me/miutalk) and [Vibes 18+](https://t.me/vibes) —
[necrovolt/seedvanity](https://github.com/necrovolt/seedvanity).

This fork is maintained by [noxyu](https://t.me/pauper).

Project channel: [t.me/gramvanity](https://t.me/gramvanity).

## Disclaimer

Provided as is, without warranty of any kind. You are responsible for checking every address before
using it.

## License

MIT — see [LICENSE](LICENSE). Third-party notices: [NOTICE](NOTICE).