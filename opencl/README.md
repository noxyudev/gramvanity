# gramvanity — build and internals

> Language: **English** | [Русский](README.ru.md)

This folder contains the search program itself: the C host in `src/` and the OpenCL kernels. It is a
development and build document. Everything about **using** the program — installation, patterns,
targets, wallet modes, resume, safety — lives in the [main README](../README.md); the Telegram Wallet
guide is [../docs/tg-wallet.md](../docs/tg-wallet.md), and the module map of `src/` is
[../docs/architecture.md](../docs/architecture.md).

In one sentence: the program tries random seed phrases, turns each one into a TON wallet address,
and keeps the phrases whose address matches the pattern you asked for. The heavy part (key
derivation) runs on the GPU; the checks run on the CPU.

> **Where this came from:** the original project is
> [necrovolt/seedvanity](https://github.com/necrovolt/seedvanity) by
> [miutalk](https://t.me/miutalk) and [Vibes 18+](https://t.me/vibes). This fork is maintained by
> [pauper](https://t.me/pauper): macOS support was removed (OpenCL only), and the fork adds several
> targets per run, one job folder per run, and checkpoint version 8.

## Status

- Tested for correctness on an NVIDIA RTX 4070 Ti Super under Windows, with thousands of phrases
  cross-checked against an independent tonutils-go implementation.
- Compiles and links for Linux (x86_64, arm64) and Windows (x86_64).
- Not yet run on AMD hardware.
- On every start the program compares GPU results with the CPU and refuses to search if they
  differ.

Benchmarks and reports are welcome.

## Build

Requirements: a C compiler and an OpenCL SDK/ICD loader (see the per-platform steps). The GPU driver
itself normally provides the OpenCL runtime. Building the optional Go verifier is described in the
[main README](../README.md) (`go build -o verify-go/w5addr ./verify-go` from the repository root).

### Linux (Ubuntu and similar)

```sh
sudo apt install build-essential ocl-icd-opencl-dev clinfo
clinfo -l                 # the GPU must appear here
cd opencl
make
```

GPU runtimes: NVIDIA's proprietary driver already includes OpenCL; AMD uses ROCm OpenCL; on Intel
install `sudo apt install intel-opencl-icd`.

### Windows

Install [MSYS2](https://www.msys2.org/), open the **MSYS2 UCRT64** shell and run:

```sh
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-opencl-icd mingw-w64-ucrt-x86_64-opencl-headers
cd /c/path/to/gramvanity/opencl
make
```

If linking fails with `undefined reference to BCryptGenRandom`, the Makefile did not detect Windows
(`OS` is not `Windows_NT`, which happens when MSYS2 is driven from another shell instead of being
opened normally). Pass the Windows libraries explicitly:

```sh
make LIBS="-lOpenCL -lbcrypt -static-libgcc"
```

From then on, `gramvanity.exe` runs from the MSYS2 shell or a regular command prompt.

### Make targets

| Target | Does |
|---|---|
| `make` | builds `gramvanity` (`gramvanity.exe` on Windows) from `src/` + `embedded.h` |
| `make embed` | regenerates `embedded.h` (see below) |
| `make clean` | removes the built binary |

`embedded.h` (kernels + wordlist) is generated and committed, so building needs only a C compiler.
After editing `kernel.cl`, `ed25519_gpu.cl`, `sha512_consts.h` or `english.txt`, run `make embed`
(needs `python3`, or `python` on Windows) and commit the result — the build never reads the source
files directly.

### Smoke check

```sh
./gramvanity --list-devices                              # GPUs visible to OpenCL
./gramvanity --mode bip39-12 --bench 60                  # one-minute speed test, finds nothing
```

For real searches — patterns, targets, resume — follow the [main README](../README.md).

## Tests

```sh
./gramvanity --selftest-det   # deterministic search and target unit tests, no GPU needed
sh tests/test_det.sh          # end-to-end tests on the GPU: stop/resume/crash/hit/replay/targets
```

`--selftest-det` checks deterministic entropy and separation between modes (including the tg-wallet
signing domain), BIP39 checksums of generated candidates in both tg-wallet halves, index/range
bookkeeping (no gaps, no overrun, 2^64 overflow handling), checkpoint round-trips with target
records plus rejection of corrupted and truncated files, legacy v2–v4 loading, target AND/OR
matching, `--ignore-case`, name resolution, and Cyrillic/CP1251 labels.

`tests/test_det.sh` adds GPU end-to-end scenarios: stop-after → resume → completion; verified hit →
key+index → replay with `--derive`/`--reveal`; tg-wallet on mainnet and testnet; corrupted and
conflicting checkpoints; `ton-24` interruption; multi-pattern and multi-target hits with per-target
counts and output folders; resume with raised counts; Cyrillic labels, their transliterated file
names, and resume by label.

## Command-line reference

Every option in one place (the program explains each in `--help` output and the usage message):

```text
--mode bip39-12|bip39-24|ton-24|tg-wallet    --net mainnet|testnet (tg-wallet)
--form uq|eq          --ignore-case          --lang en|ru
--target [NAME]                   (starts a target block; repeatable)
--prefix PATTERN                  (per target/default; repeatable / comma-separated)
--suffix PATTERN                  (per target/default; repeatable / comma-separated)
--count N                         (per target/default)
--out DIR                         (job folder before the first target; hit dir inside a target)
--threads N                       --power N             --device N
--list-devices                    --bench SEC           --no-verify
--dump N FILE
--start-offset N                  --tries N             --checkpoint FILE
--resume FILE                     --checkpoint-interval SEC
--stop-after N                    --ckpt-info FILE
--random                          --derive KEYHEX INDEX
--reveal CHECKPOINT INDEX         --selftest-det
--help
```

- `--threads N` — CPU threads for address checking and hit verification (default: all logical CPUs).
- `--power N` — keep the GPU and CPU busy N% of the time (default 65, `100` = no limit).
- `--device N` — use GPU number N from `--list-devices`.
- `--lang en|ru` — language of human-readable messages (default `en`; also `GRAMVANITY_LANG`).
- `--no-verify` — skip the external verifier. The CPU re-derivation of every hit still runs.
- `--dump N FILE` — write N fresh phrases and their addresses to FILE (JSON lines) and exit; useful
  for testing, not for searching.
- `GRAMVANITY_DEBUG=1` — environment variable; prints batch size and kernel-length tuning for every
  loop.

## Troubleshooting

- `no OpenCL GPU found` — the driver does not expose OpenCL. On Linux check `clinfo -l`; install the
  OpenCL package for your GPU vendor.
- `OpenCL kernel build failed` — please report the full error text.
- `SELF-TEST FAILED` — the GPU or its driver gives wrong results. Do not search on that device.
- `undefined reference to BCryptGenRandom` — see the Windows build section above.
- Long runs on Windows: keep the window open and disable sleep in the power settings.

## Under the hood (optional reading)

This section is for the curious; you do not need it to use the program.

**What runs where.** The GPU does the expensive per-candidate work: PBKDF2-HMAC-SHA512
(`kernel.cl`; 2,048 rounds for BIP39, 100,000 for the TON standard mnemonic), the BIP39 key
derivation path `m/44'/607'/0'`, and the Ed25519 public key (`ed25519_gpu.cl`). The CPU turns the
public key into a wallet address (W5 v5r1, or WalletTg in `tg-wallet` mode), checks every target's
patterns on all threads, and re-derives every hit independently with
[ed25519-donna](https://github.com/floodyberry/ed25519-donna).

**Windows and long kernels.** Windows resets the GPU driver when one kernel runs longer than about
two seconds (TDR). PBKDF2 is therefore split into launches of `chunk` iterations, with the state
kept in GPU memory between them; each launch targets about 0.25 seconds. The cost per iteration is
estimated conservatively (the larger of profiled kernel time and whole-loop wall time) and decays
slowly, because some drivers report wrong profiling times or signal events late.

**Batches.** A batch starts at 16,384 phrases and grows up to 1,000,000 while a batch stays short.
The GPU works on the next batch while the CPU finishes the previous one (double buffering).

**Power limiting.** `--power N` (default 65, `100` = no limit) throttles the whole pipeline by duty
cycle: after each batch the loop sleeps `busy * (100 - N) / N` seconds. Below 100% the next batch is
not queued in advance, otherwise the GPU would keep working during the pause. The pause is cut into
100 ms slices, so Ctrl+C stays responsive.

**Ed25519 on the GPU.** The clamped SHA-512 hash is recoded into 64 signed base-16 digits. The
public key is the sum of `d[i]` times a precomputed table `[j*16^i]B` of 512 affine points, built at
startup. Each digit costs one mixed addition (8 field multiplications), with no doublings; the final
affine `y` costs one Fermat inversion. The field uses ref10's radix-2^25.5 layout with ten signed
32-bit limbs, so every product fits in a 64-bit integer (the 5×51-bit layout used on the CPU would
need 64×64-bit products). On an RTX 4070 Ti Super one key costs about 0.03 µs of GPU time — around
2% of the PBKDF2 work, so the search remains PBKDF2-bound.

**Address checking.** With Ed25519 on the GPU, the CPU only hashes the wallet StateInit (SHA-256),
builds the address, and checks every target's prefix/ending groups for every candidate. Public keys
are read back together with the private keys and used as-is. A hit is still re-derived from scratch
on the CPU (Monocypher HMAC-SHA512 PBKDF2 and SLIP-10, ed25519-donna Ed25519), must match the GPU
result, and is then re-checked by the Go verifier.

**Checkpoint format.** Binary, magic `SVANCKP`, version 8 (versions 2–7 still load). It stores the
job folder, global flags, per-target ids/labels/patterns/counts/hits/output directories, cursors and
the secret search key, protected by a SHA-256 integrity tag; it is written atomically (temp file,
flush, rename), so a crash cannot leave a half-written file. Indices are processed strictly in order
`[start, end)` and the saved position is the last **confirmed** index — everything below it has had
its address checked — so a resumed run starts exactly there, with no gaps, repeating at most the
work since the last autosave. All arithmetic is overflow-checked. Version 2–4 files have no target
records and load as one target with their original "any pattern matches" behaviour. Flag layout:
global bit 0 = bounceable (`EQ`), bit 1 = testnet; per-target bits: ignore-case, count limit, legacy
OR matching, `--match or`, `--all-patterns` (with per-pattern saved-hit counters). In `tg-wallet`
mode the signing half of a hit is derived from the same search key with its own domain tag, so
`--derive`, `--reveal` and `--resume` always reproduce the exact 24 words. Keep checkpoints, hit and
stop files secret: whoever holds the search key can derive every candidate phrase of the job.

**Startup self-test.** SHA-256 and the BIP39 PBKDF2 test vector on the CPU; GPU against CPU for
PBKDF2 and SLIP-10 (keys longer than 128 bytes, forced chunk boundaries); GPU Ed25519 against the CPU
(the RFC 8032 vector, random keys, and the TON stage-2 layout); W5 and WalletTg addresses of fixed
test phrases against reference vectors originally produced with @ton/ton (both networks); BIP39
checksums.

**Files.** The host program lives in `src/` as focused modules (`platform`, `options`, `pattern`,
`crypto`, `addresses`, `detsearch`, `gpu`, `stats`, `checkpoint`, `hits`, `search`, `selftest`,
`main`); the module map, the ownership of shared state and the lock discipline are documented in
[../docs/architecture.md](../docs/architecture.md). The GPU kernels stay next to it: `kernel.cl`
(PBKDF2/SLIP-10) and `ed25519_gpu.cl` (Ed25519). `embedded.h` holds the kernel sources and the
BIP-0039 wordlist. `third_party/monocypher/` is Monocypher (CC0 or 2-clause BSD, see `LICENCE.md`
and `VERSION`). `third_party/ed25519/` is ed25519-donna (public domain, see its `README.md`); it is
used for the CPU re-derivation of every hit and for the fixed-base table in the GPU kernel
(`ed25519_gpu_table` in `ed25519_fast.c`).

**Secret files.** Checkpoints, hit files and stop files are created owner-only: mode `0600` on
Linux; on Windows they inherit the user-profile ACLs, so keep the results folder out of shared
locations.

## Credits

Original project: [necrovolt/seedvanity](https://github.com/necrovolt/seedvanity) by
[miutalk](https://t.me/miutalk) and [Vibes 18+](https://t.me/vibes).

This fork: [pauper](https://t.me/pauper).
