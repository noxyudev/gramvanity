# Changelog

> Language: **English** | [Русский](CHANGELOG.ru.md)

All notable changes to gramvanity are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Added
- One-command user installers (`tools/install-user.ps1`, `tools/install-user.sh`): download the
  ready release, verify its SHA-256 and install it into `%LOCALAPPDATA%\gramvanity` /
  `~/.local/opt/gramvanity` without administrator rights, run `--selftest-det` and
  `--list-devices`, and never touch an existing `results/` folder. A specific release can be
  picked with `--version`, and `--dry-run` prints the plan without changing anything. The GPU
  driver is never installed automatically; without an OpenCL GPU the installer prints
  vendor-specific instructions.
- Developer setup in one command after cloning: `tools/setup-dev.ps1` plus
  `tools/setup-dev-msys2.sh` (Windows, MSYS2 UCRT64) and `tools/setup-dev-linux.sh`
  (Ubuntu/Debian). Only missing components are installed, and an existing Go is never upgraded:
  a portable one goes into `%LOCALAPPDATA%\gramvanity-dev` / `~/.local/gramvanity-dev` and is
  connected by the session PATH only, so the global PATH stays unchanged. A fully configured
  machine passes the setup without a single installation. On Linux, `--with-gpu-tests` adds the
  benchmark and the full GPU integration suite.
- Docker build environment (`docker/Dockerfile`): build and CPU checks in a container without
  installing MSYS2, Go, Python or GCC on the host. The repository is mounted, so the binaries
  land on the host in the expected `opencl/` + `verify-go/` layout; the GPU is reachable only on
  Linux hosts (`--device /dev/dri` or nvidia-container-toolkit).
- The release workflow now unpacks each packaged archive, verifies the expected file layout, runs
  `w5addr activate --help` and (on Linux) `--selftest-det` from the archive before publishing.

### Changed
- `make -C opencl embed` uses the configurable `PYTHON` variable (default `python3`); when only
  `python` is available, run `make embed PYTHON=python`.

### Fixed
- `w5addr activate` reports an error instead of a fake "balance 0 GRAM" when toncenter returns an
  unreadable balance.
- `w5addr activate` refuses to continue when the account state is `active` but toncenter returned
  no contract code: it cannot be verified that the address is a Telegram Wallet (fail-closed; an
  empty code for `uninitialized` stays expected).
- `w5addr activate --valid-for` is limited to 1..604800 seconds (7 days) instead of silently
  accepting values that could overflow the message validity window.

## [1.1.3] - 2026-11-15

### Fixed
- Prefixes whose first character cannot occur after the form tag are rejected at startup; prefix expectations now use the correct four-character first domain instead of 64.
- Invalid startup arguments, malformed patterns, failed merges and GPU self-test failures no longer leave empty job folders. Failed atomic writes remove their temporary files, and a successful deterministic self-test removes its empty job folder.
- `--net testnet` is rejected before command-specific setup unless the mode is restored from a resume checkpoint.

## [1.1.2] - 2026-11-08

A correctness and hardening release: the Windows emergency save, resumability
diagnostics and the verifier path now work as documented, and secret files get
owner-only ACLs on Windows.

### Fixed
- The Windows console-close emergency save is wired again: closing the window
  mid-batch saves the checkpoint and writes the stop report from the handler
  thread instead of losing everything since the last autosave.
- `--resume --tries` refuses an end that falls behind the uncommitted stage-1
  frontier of a ton-24 job (a crash can leave it millions of indices wide).
  The previously accepted case searched past the end, counted the hits and then
  rolled the checkpoint back, so the next extension duplicated hit files and
  counters; resume with a larger `--tries` or without it.
- Failed checkpoint saves are reported - one stderr line per autosave interval
  and an explicit final-failure line instead of "checkpoint saved" - instead of
  being silently ignored.
- Launched from any working directory, the binary resolves the verifier and the
  default `results/` folder from its own location again: `exe_dir` mishandled
  the backslash path from `GetModuleFileNameA`, so outside `opencl/` the
  verifier reported "not runnable" and job folders landed next to the working
  directory.
- The verifier temp file is named after the pid, so two concurrent runs cannot
  read each other's answer.
- A 1.0.x binary checkpoint is named as such in the load error instead of the
  generic foreign-file message.
- `make -C opencl` picks the Windows branch by uname too, not only by the OS
  variable: a plain make in a clean MSYS2 shell no longer fails with an
  "undefined reference to BCryptGenRandom".

### Security
- On Windows, checkpoints, hit files, stop reports and newly created job
  folders get a protected owner-only DACL (the POSIX side already created them
  0600/0700); other local users can no longer read the search key from a shared
  directory.

### Added
- `tools/check_tr.py` with a CI step: every `tr()` pair must carry the same
  printf conversion specifiers (221 pairs verified).
- GPU-suite scenarios: bip39-24 end to end, a run from a foreign working
  directory, failing-checkpoint reporting, and the ton-24 frontier refusal.

## [1.1.1] - 2026-11-08

### Added

- New `--checkpoint-merge --in CKPT [--in CKPT...] [--out DIR]` command: rebuilds one or more
  checkpoints into a new checkpoint that keeps only the targets that have not reached their `--count`
  yet. Each `--in` is a checkpoint file or a job folder; the merged file is written as
  `job.checkpoint` in a new job folder (`--out DIR`, default `results/job-<UTC timestamp>`). Done
  targets are dropped — their hits stay recoverable only from the original checkpoints
  (`--reveal` there); the same target in several inputs combines by the per-field maximum; a reused
  name with different patterns gets a `-2`, `-3...` suffix. The result is a new job: a fresh search
  key, the position restarts from index 0 and the job counters are zeroed; the merged file stores no
  `job_dir`, so its own folder is the job folder on resume.
- The checkpoint writer omits empty `job_dir` and `out` lines (the merge relies on this;
  `--checkpoint-fix` normalizes them away in hand-written files).

## [1.1.0] - 2026-11-08

The checkpoint file becomes a hand-editable UTF-8 text format: edit it in any
editor and re-sign it with `--checkpoint-fix`.

### Changed

- **BREAKING**: the checkpoint file is now a hand-editable UTF-8 text format
  ("gramvanity-checkpoint" v1): one `key = value` per line, `[target]` sections, value
  enumerations as trailing comments, and a trailing `checksum` line (SHA-256 over every other
  line). The old binary format is fully removed: old `.ckpt` files do not load, and starting a
  fresh job is required instead of a resume. Released 1.0.x binaries cannot read the new files.
- New `--checkpoint-fix FILE` command: validates a hand-edited checkpoint (every field except the
  checksum) and rewrites it with a fresh checksum; `--resume` refuses a file whose checksum no
  longer matches. Typical edits: raise a target's `count`, change patterns, add or remove a
  `[target]` block, reset the range (see the README's checkpoint section).
- `--ckpt-info` is renamed to `--checkpoint-info` (same output); default checkpoint file name is
  `job.checkpoint`.
- Target flag bits are renumbered (global: bit0 bounceable, bit1 testnet; per-target: ignore-case,
  count given, `--match or`, `--all-patterns`) and the dead `legacy_or` field is removed together
  with the legacy checkpoint readers. No observable behavior change for new jobs.

## [1.0.2] - 2026-11-07

### Added

- The release process guide (`docs/release-process.md` / `docs/release-process.ru.md`):
  versioning, commit and changelog rules, the release checklist, what the `release` workflow
  does, the release notes template and the known limitations; checked as a language pair by
  `tools/check_i18n.py`.

### Changed

- Address matching in `finish()` folds all targets into two exact bitset NFAs (prefixes anchored
  after the form tag, suffixes to the last character) instead of scanning targets one by one; the
  results are identical, and the CPU post-phase is faster with many targets.
  `GRAMVANITY_MATCHER_CHECK=1` cross-checks the fast matcher against the per-target scan.

## [1.0.1] - 2026-11-06

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

[Unreleased]: https://github.com/noxyudev/gramvanity/compare/v1.1.3...HEAD
[1.1.3]: https://github.com/noxyudev/gramvanity/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/noxyudev/gramvanity/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/noxyudev/gramvanity/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/noxyudev/gramvanity/compare/v1.0.2...v1.1.0
[1.0.2]: https://github.com/noxyudev/gramvanity/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/noxyudev/gramvanity/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/necrovolt/seedvanity/compare/main...noxyudev:gramvanity:v1.0.0