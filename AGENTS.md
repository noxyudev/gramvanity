# AGENTS.md — gramvanity

> Language: **English** | [Русский](AGENTS.ru.md)

## 1. Purpose and layout

`gramvanity` searches for seed phrases that produce a chosen vanity address on TON/GRAM. The heavy
work (PBKDF2, SLIP-10, Ed25519) runs on the GPU through OpenCL; address checks, hit re-derivation
and the independent verifier run on the CPU/Go.

| Path | Purpose |
|---|---|
| `opencl/` | the main program: C host (`opencl/src/`), OpenCL kernels (`kernel.cl`, `ed25519_gpu.cl`), `embedded.h`, `Makefile`, `tests/` |
| `verify-go/` | the independent tonutils-go verifier and the `w5addr activate` command for tg-wallet |
| `docs/` | `architecture.md` (module map and locking), `tg-wallet.md`, and their Russian pairs |
| `.github/workflows/` | CI (Linux + Windows) and release packaging |
| `results/` | run data; contains secrets and is never committed |

Linux (x86_64, arm64) and Windows (x86_64) are supported. The primary local Windows environment is
MSYS2 UCRT64.

Before changing anything, read:
- `README.md` — user-facing behavior and the CLI;
- `docs/architecture.md` — modules, ownership of shared state and lock discipline;
- for `opencl/` — `opencl/README.md`, `opencl/AGENTS.md` and `Makefile`;
- the sources and tests that the task touches. Do not guess the project structure.

## 2. Documentation and localization

English is the primary language for user documentation, agent instructions and source comments.
Russian copies use the `.ru.md` suffix:

- `README.md` / `README.ru.md`;
- `opencl/README.md` / `opencl/README.ru.md`;
- `AGENTS.md` / `AGENTS.ru.md`;
- `opencl/AGENTS.md` / `opencl/AGENTS.ru.md`;
- `docs/architecture.md` / `docs/architecture.ru.md`;
- `docs/tg-wallet.md` / `docs/tg-wallet.ru.md`;
- `CHANGELOG.md` / `CHANGELOG.ru.md`.

Every pair links to the other language at the top. When you change one version, update the other in
the same change. `tools/check_i18n.py` verifies that all pairs exist and link to each other; CI runs
it on Linux.

Runtime localization rules:

- `gramvanity` accepts `--lang en|ru` and the `GRAMVANITY_LANG` environment variable. English is the
  default; there is no locale auto-detection.
- `verify-go/w5addr` stays English; it is a verifier/activator and its output is consumed by scripts.
- Human-readable prose in hit, stop and report files follows the language of the run. Machine-readable
  keys and tags (`HIT`, `target`, `index`, JSON keys), CLI flags, checkpoint magic, file names and
  protocol strings stay English.
- Source comments use explicit `EN:` / `RU:` blocks for substantive comments. Do not translate trivial
  line comments or identifiers.

Do not translate `third_party/`, generated `opencl/embedded.h`, `go.sum`, binaries, `LICENSE` or
`NOTICE`.

## 3. Windows environment

Use MSYS2 UCRT64. Expected values:

```bash
printf 'MSYSTEM=%s\nOS=%s\nMINGW_PREFIX=%s\n' "${MSYSTEM:-}" "${OS:-}" "${MINGW_PREFIX:-}"
gcc -dumpmachine
command -v gcc make go
```

- `MSYSTEM=UCRT64`;
- `OS=Windows_NT`;
- `MINGW_PREFIX=/ucrt64`;
- GCC target `x86_64-w64-mingw32`;
- GCC, make, OpenCL headers/ICD loader, Go; Python 3 only for `make embed`.

Go may be absent from the UCRT64 PATH; `build-windows.sh` automatically looks in
`C:\Program Files\Go\bin`.

Do not replace UCRT64 with MINGW64, MSYS, Git Bash or WSL. Do not reinstall tools that are already
present without a concrete reason. Do not change the global Windows PATH and do not mix libraries
from different MSYS2 environments.

If the agent runs in a cloud environment or a container, do not assume it has the user's MSYS2 and
GPU: first determine where the command actually runs.

## 4. Build

The full Windows build is one script from the repository root:

```bash
bash ./build-windows.sh
bash ./build-windows.sh --clean     # remove old binaries and rebuild
bash ./build-windows.sh --check     # build + selftest-det + go vet/test
```

The script checks the environment and tools, builds `verify-go/w5addr.exe`, then
`opencl/gramvanity.exe`, handles the documented `BCryptGenRandom` fallback and checks that both
files exist. Its messages support `--lang en|ru` and `GRAMVANITY_LANG`; English is the default.

For targeted changes:

```bash
make -C opencl
go build -o verify-go/w5addr.exe ./verify-go
```

Linux:

```bash
make -C opencl
go build -o verify-go/w5addr ./verify-go
```

Do not duplicate the whole sequence by hand when `build-windows.sh` works. If a build fails: read the
full output, identify the stage, check the environment and dependencies, and fix the specific cause.
Do not report a successful build before checking the output files.

## 5. Build layout

Keep this layout:

```text
gramvanity/
├── opencl/
│   └── gramvanity.exe
└── verify-go/
    └── w5addr.exe
```

The search looks for the verifier relative to `opencl/` (`../verify-go/w5addr[.exe]`) and writes
results to `../results/`. Do not move the binaries separately or change this layout. Binaries are
ignored by Git — do not commit them.

The Go verifier is required for trusted hit checks and tg-wallet activation. Without it, results
marked `unverified` cannot be considered checked.

## 6. Generated files

`opencl/embedded.h` is generated from `kernel.cl`, `ed25519_gpu.cl`, `sha512_consts.h` and
`english.txt`, and is committed. The build reads only `embedded.h`, not the kernel sources directly.

After editing any of those files:

```bash
make -C opencl embed
```

Python 3 is required. The Makefile target calls `python3`; if only `python` is available, run
`python opencl/tools/embed.py` from the repository root. The kernel sources are UTF-8 and their
bilingual comments land in `embedded.h` verbatim, so a comment-only edit still needs a regenerate.
Review the `embedded.h` diff and include it in the change. Do not edit `embedded.h` by hand. Details
are in `opencl/AGENTS.md`.

## 7. Security

`results/`, checkpoints, hit and stop files, and `--dump` output may contain seed phrases and the
secret search key.

- Do not publish or open these files unless the task requires it.
- Do not add `results/` to Git.
- Do not send checkpoints or phrases to external services or cloud AI agents.
- Do not put secrets into logs, reports, examples or test fixtures.
- Test phrases in the source and tests are public throwaway phrases; never send funds to them.

## 8. Verification

Minimum after changes to C/OpenCL:

```bash
./opencl/gramvanity.exe --selftest-det
```

`--selftest-det` needs no GPU (~50 deterministic assertions), but it does not replace a check of the
GPU path.

After changes to the Go verifier:

```bash
go vet ./verify-go
go test -count=1 ./verify-go
```

Documentation and localization changes:

```bash
python3 tools/check_i18n.py
```

GPU checks, when the task touches them:

```bash
./opencl/gramvanity.exe --list-devices
./opencl/gramvanity.exe --mode bip39-12 --bench 60
sh opencl/tests/test_det.sh
```

`test_det.sh` is the GPU integration suite (20 scenarios, 113 checks, about 7 minutes). Run it from
the root or from `opencl/`: the script changes to the right directory itself and uses temporary
folders. Without a GPU this suite cannot run — state that explicitly in the final report.

CI (`.github/workflows/ci.yml`) builds Linux and Windows, runs `--selftest-det` on Linux,
`go vet`/`go test`, and the bilingual pair check. The GPU suite stays a local check. Do not break this
contract.

If the self-test reports `SELF-TEST FAILED`, do not search on that device until the cause is clear. A
successful build does not prove that the computation is correct.

## 9. Agent rules

- Study the current implementation and related tests before changing them.
- Do not rewrite working components without need; keep changes minimal and local.
- Do not delete the user's `results/`, checkpoints or configuration.
- Do not change the checkpoint, hit or stop file formats without a clear need and compatibility
  checks.
- Do not remove or weaken checks and tests just to make them pass.
- Do not start a long search for verification; use `--bench`, bounded `--tries` and
  `--selftest-det`.
- For changes in `opencl/`, also follow `opencl/AGENTS.md`.
- In the final message, list the changed files, the commands you ran, the test results and any
  remaining problems.
