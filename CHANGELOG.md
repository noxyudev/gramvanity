# Changelog

> Language: **English** | [Русский](CHANGELOG.ru.md)

All notable changes to gramvanity are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Documentation

- The fork maintainer is now noxyu, and the project Telegram channel
  ([t.me/gramvanity](https://t.me/gramvanity)) is linked in the READMEs.
- The Telegram Wallet import guide follows the current wallet UI (Money → ⋮ → Keys & Backup →
  Delete wallet → Add existing) and documents the optional built-in phrase rotation
  (Enable Backup → Disable Backup with Update Secret Phrase).

## [1.0.0] - 2026-11-06

First release of the fork. gramvanity started as
[necrovolt/seedvanity](https://github.com/necrovolt/seedvanity) and keeps the same
idea: the search runs over real seed phrases, so the mined address is the one a
wallet derives on import. The global changes relative to the original project:

### Search engine

- The monolithic `opencl/seedvanity.c` host is rewritten as the modular `opencl/src/` tree.
- Ed25519 public-key derivation runs on the GPU (`opencl/ed25519_gpu.cl`), removing the CPU
  bottleneck on discrete GPUs; ed25519-donna is vendored for the independent CPU check of every
  hit.

### Search workflow

- One run can search several addresses at once, each with its own patterns, limits and output.
- Every run keeps all of its files in one job folder (hits, stop report, checkpoint).
- The checkpoint format stores those targets; checkpoints from older versions still load.
- Hit files name every matched target (`hit-A+B-<index>.txt`).

### Wallets

- Telegram Wallet (WalletTg) support: special 24-word phrases and one-time activation via
  `w5addr activate`; `--mode tg-wallet` is the default.
- The verifier is rewritten from Node.js to Go (`verify-go/`): independent re-derivation with
  tonutils-go plus the activation command.

### Platforms and build

- The macOS/Metal build is removed; the supported platforms are Linux and Windows via OpenCL.
- One-shot Windows build script (`build-windows.sh`, MSYS2 UCRT64).

### Quality and documentation

- GPU integration suite `opencl/tests/test_det.sh` (20 scenarios, 113 checks) and `--selftest-det`
  (~50 CPU assertions).
- CI on GitHub Actions (Linux + Windows) and tag-driven releases with ready archives.
- Bilingual documentation (English/Russian) and runtime localization `--lang en|ru`.
