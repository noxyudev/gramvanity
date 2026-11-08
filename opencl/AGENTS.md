# AGENTS.md — opencl/

> Language: **English** | [Русский](AGENTS.ru.md)

## 1. What is here and what to read

`opencl/` is the main program: the C host in `src/`, the OpenCL kernels (`kernel.cl`,
`ed25519_gpu.cl`), `Makefile`, generated `embedded.h`, tests and `third_party/`.

Before changing anything, read:
- the root `AGENTS.md`;
- `opencl/README.md`;
- `docs/architecture.md` — the module map, ownership of shared state and lock discipline;
- `Makefile`;
- the sources and tests that the task touches.

Do not guess module names or contracts: the dependency graph is documented in
`docs/architecture.md`, and cycles between modules are forbidden.

## 2. Localization in this directory

English is the primary language. Russian copies and translations are kept alongside:

- `opencl/README.md` / `opencl/README.ru.md`;
- `opencl/AGENTS.md` / `opencl/AGENTS.ru.md`.

For source comments, use explicit `EN:` / `RU:` blocks for substantive comments; do not translate
trivial line comments, identifiers, OpenCL code, CLI flags, JSON keys, checkpoint format line or
file names.

User-facing strings of `gramvanity` are localized at run time with `--lang en|ru` and
`GRAMVANITY_LANG` (English by default). Add both language variants together. The Go verifier
`verify-go/w5addr` stays English.

Do not translate `third_party/`, generated `embedded.h` or binary files.

## 3. Environment

Use MSYS2 UCRT64 only.

```bash
printf '%s\n' "${MSYSTEM:-}" "${OS:-}" "${MINGW_PREFIX:-}"
command -v gcc make
gcc -dumpmachine
```

Expect `UCRT64`, `Windows_NT`, `/ucrt64` and `x86_64-w64-mingw32`. Linux (gcc + OpenCL ICD) is
supported by the Makefile, but do not use it as a substitute for the Windows check. If the
environment does not match, stop and report it; do not switch compilers or shells at random.

## 4. Build

The full Windows build is run from the repository root:

```bash
bash ./build-windows.sh
```

Fast iteration inside `opencl/`:

```bash
make            # gramvanity.exe
make clean
```

or from the root `make -C opencl`.

Makefile notes:
- all `src/*.c` files are compiled in one command; when adding or removing a `.c`, update the `SRC`
  list in `Makefile`;
- Windows: `LIBS = -lOpenCL -lbcrypt -static-libgcc`, Linux: `-lOpenCL -lpthread -lm`;
- there are no object files; `make` rebuilds the whole binary;
- `embedded.h` must exist; if it is missing, run `make -C opencl embed`.

If linking fails with `undefined reference to BCryptGenRandom`, first check that the build runs in
UCRT64 and that `OS=Windows_NT`. Only if the error remains, use the documented fallback:

```bash
make LIBS="-lOpenCL -lbcrypt -static-libgcc"
```

Do not add `-lbcrypt` permanently and do not edit the Makefile just to work around the environment.

## 5. embedded.h

`embedded.h` is generated and committed; the build does not read the `.cl` sources directly.

```bash
make embed
```

Run it after changing:
- `kernel.cl`;
- `ed25519_gpu.cl`;
- `sha512_consts.h`;
- `english.txt`.

Python 3 is required. `make embed` calls `python3`; if only `python` is available, run
`python tools/embed.py` from `opencl/`. The script checks the wordlist SHA-256, so do not change
`english.txt` without understanding the consequences. After generation, review the `embedded.h`
diff and include it in the change. Do not edit `embedded.h` by hand.

The `.cl` sources are UTF-8: their bilingual `EN:`/`RU:` comments are copied verbatim into
`embedded.h`, so a comment-only edit still changes the generated file and must be committed together
with the regenerated header.

## 6. Checks

Baseline, no GPU:

```bash
./gramvanity.exe --selftest-det
```

GPU and integration checks:

```bash
./gramvanity.exe --list-devices
./gramvanity.exe --mode bip39-12 --bench 60
sh tests/test_det.sh
```

- `--selftest-det` — deterministic unit assertions, no GPU needed.
- `test_det.sh` — 20 scenarios/134 checks; requires a GPU and a built binary. It can be run from
  `opencl/` or from the root: the script changes to the right directory itself and uses temporary
  folders.
- `GRAMVANITY_DEBUG=1` enables debug output for batch/chunk tuning.
- A successful compilation does not prove that the computation is correct; after `SELF-TEST FAILED`
  do not use the device for searching.
- If there is no GPU, do not present `--selftest-det` as a check of the GPU path: state that the GPU
  suite did not run.

After changes to the verifier (`verify-go/`), from the repository root:

```bash
go vet ./verify-go
go test -count=1 ./verify-go
```

After documentation changes:

```bash
python3 tools/check_i18n.py
```

## 7. Architecture and contracts

- Modules and dependencies are described in `docs/architecture.md`:
  `main -> {search, hits, checkpoint, selftest, gpu, detsearch, addresses, crypto, pattern,
  options, i18n, platform}`; cycles are forbidden. `i18n` is a leaf (`tr()`, `--lang`,
  `GRAMVANITY_LANG`), so any module may include it.
- Shared state belongs to exactly one module and is exported through its header. Follow the lock
  discipline: `hit_lock`, `stat_lock`, `checkpoint_lock`, `finish_lock`; do not narrow critical
  sections without the full `tests/test_det.sh` suite.
- `search` starts `../verify-go/w5addr[.exe]` and exchanges JSON lines
  (`{"words","mode","net"}` → `{uq,eq,pub,...}`). When changing the format, update both sides and the
  Go tests.
- The checkpoint is the text format "gramvanity-checkpoint" (one `key = value` per line, `[target]`
  sections, a trailing SHA-256 `checksum` line; see `docs/architecture.md`). Any format change
  requires a version bump and round-trip coverage in `--selftest-det`; the file stays hand-editable,
  and `--checkpoint-fix` re-signs it after edits.
- Hit and stop files and target names are part of the user-facing contract; do not change them
  without need. Human-readable prose follows the run language; keys, tags and identifiers stay
  English.
- Windows TDR: a single kernel must not run longer than about 2 seconds. PBKDF2 is split into chunks
  (~0.25 s); tuning lives in `gpu.c`. Do not introduce long single launches.
- The startup self-test compares GPU results with the CPU and blocks searching on a mismatch; do not
  bypass it.

## 8. Change rules

- Keep the C host and the OpenCL kernels compatible; when the data layout changes, update both
  sides.
- Do not remove or disable checks/self-tests just to make a test pass.
- When changing algorithms, state which vectors and checks confirm correctness.
- Do not put secrets into sources, test fixtures, logs or reports.
- Do not change the checkpoint or hit file formats without need and compatibility checks.
- In the final message, list the changed files, the commands you ran, the test results and any
  remaining problems.
