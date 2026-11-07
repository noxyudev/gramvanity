# gramvanity: architecture

> Language: **English** | [Русский](architecture.ru.md)

This document describes the module layout of the two programs, the ownership of shared state and
the contracts between them. It reflects the decomposition of the former 4,400-line `gramvanity.c`
monolith into focused modules (see the "Decisions" section for the reasoning).

## Bird's-eye view

One product, two cooperating binaries in one repository:

```
opencl/gramvanity(.exe)      C + OpenCL: the search itself (GPU derives candidates, CPU checks)
verify-go/w5addr(.exe)       Go: independent verifier (stdin JSON pipe) + tg-wallet activator
```

```
+--------------------------- opencl/src/ ---------------------------+
| main.c        CLI parsing/validation, utility modes, search loop  |
| platform      OS portability: die/time/CSPRNG/fs/threads/signals  |
| options       opt struct, job folder, usage()                     |
| pattern       targets, patterns, UTF-8/translit, matching, odds   |
| crypto        PBKDF2/SLIP-10/BIP39/TON (Monocypher, sha256.c)     |
| addresses     W5 v5r1 / WalletTg ids, friendly(), derive_candidate|
| detsearch     deterministic search state (index -> entropy)       |
| gpu           OpenCL context, job pipeline, chunk tuning          |
| stats         counters + hit_lock/stat_lock                       |
| checkpoint    text checkpoint format, resume reconstruction      |
| hits          re-derivation, verifier spawn, hit files, stop file |
| search        BIP39/TON pipelines, range bookkeeping              |
| selftest      startup vectors + --selftest-det unit tests         |
| i18n          --lang/GRAMVANITY_LANG, tr() message selection      |
+-------------------------------------------------------------------+
        |  process pipe: {"words",mode?,net?} -> {uq,eq,pub,as,...}
        v
+--------- verify-go (one Go package main) ----------+
| main.go       stdin verifier loop, w5 mode         |
| tgaddress.go  WalletTg addresses, tg check         |
| tgkey.go      key derivation, signed message build |
| tonclient.go  toncenter v2 HTTP client             |
| tgactivate.go `w5addr activate` CLI                |
+----------------------------------------------------+
```

Dependency rule: `main -> {search, hits, checkpoint, selftest, gpu, detsearch, addresses, crypto,
pattern, options, i18n, platform}`, and within the lower layers only downward
(`search -> {gpu, crypto, detsearch, stats, checkpoint, hits}`, `checkpoint -> {pattern, stats,
detsearch, options}`, `hits -> {checkpoint, stats, pattern, crypto, addresses, gpu}`). There are no
cycles; the former monolith cycle `checkpoint <-> hits` is broken by the dedicated `stats` module.
`i18n` is a leaf: it depends only on libc, so any module may include it.

## Module notes

- **platform** — every `#ifdef _WIN32` fork of the program lives here (CSPRNG, directory and file
  helpers, mutexes/threads, signal handling). The Windows console-close handler needs to save state
  from its own thread; instead of depending on a higher layer, `main` wires an
  `set_emergency_save()` hook to `save_all_and_report()` — `platform` stays a leaf module.
- **pattern** — the domain model: `Target`/`Pattern`/`StrList`, the UTF-8 codec, CP1251 fallback and
  Cyrillic transliteration for file-name ids, pattern compilation, matching and probability math.
- **stats** — the shared counters (`total_done`, `hits_found`, `verified`, ...) and the two locks
  `hit_lock`/`stat_lock`. It exists to break the checkpoint/hits cycle: the checkpoint serializer
  reads counters under `stat_lock`, while hit handling needs `checkpoint_save()`.
- **checkpoint** — owns the text checkpoint format (one `key = value` per line, `[target]` sections,
  a trailing SHA-256 `checksum` line) and the resume-side reconstruction (`job_dir_from_checkpoint`,
  `resume_apply_target_overrides`, `targets_from_checkpoint`). The writer snapshots the live state
  into reusable storage so saves stay allocation-free for the Windows console handler; the parser is
  strict (unknown and duplicate keys are errors), and `checkpoint_fix()` re-signs a hand-edited file.
- **hits** — one hit flows through `handle_hit` exactly as before: active-target filtering
  (`--all-patterns`), CPU re-derivation, wallet-ambiguity skip, optional external verifier, hit file
  writing, counter updates — all under `hit_lock`. The end-of-session report
  (`write_stop_file`/`save_all_and_report`) is idempotent via `finish_lock`.
- **search** — the two GPU pipelines. BIP39: single-stage double-buffered batches. TON: stage 1
  (basic-seed check, keeps 1/256) with stage 2 (`ton_flush`) for the survivors. The main loop calls
  the exported thread callbacks (`bip_prep_fn`/`bip_post_fn`/`ton_prep_fn`/`ton_post_fn`) directly
  for double buffering.
- **gpu** — OpenCL context and the `Job` submit/wait/release pipeline plus the adaptive chunk tuning
  (each launch stays ~0.25 s to survive Windows TDR). The CPU SHA-512 compressor (`K512C`) lives here
  as the one-off helper for the SLIP-10 master-key states; GPU/CPU agreement is guarded by the
  startup self-test.
- **selftest** — `self_test()` (published vectors: SHA-256, BIP39 PBKDF2, W5/WalletTg addresses,
  GPU-vs-CPU cross-checks) runs before every search; `--selftest-det` runs the ~50 deterministic
  search assertions without a GPU (this is what CI runs).
- **i18n** — the message language: `--lang en|ru` and `GRAMVANITY_LANG`, English by default with no
  locale auto-detection. `tr("english", "русский")` picks the variant; both variants must carry the
  same printf conversion specifiers. Machine-readable output (tags, keys, JSON, file names, the
  checkpoint format line, the verifier pipe) stays English regardless of the language, and `w5addr`
  does not take `--lang` at all.

## Ownership of shared state

Every global has exactly one owning module and is exported from its header:

| State | Owner | Guards |
|---|---|---|
| `opt`, `root_dir`, `job_dir` | options.h | — (read-only after startup) |
| `targets[]`, `n_targets`, `cur_target`, `g_prefix/g_suffix` | pattern.h | — (final after startup) |
| `det` (cursors, search key) | detsearch.h | checkpoint format |
| `gpu`, `chunk_iters`, `iter_cost`, `last_iter_time` | gpu.h | — (single-threaded setup, loop thread) |
| counters, `hit_files[]`, `hit_lock`, `stat_lock` | stats.h | `stat_lock` |
| `g_stop`, `g_finished` | platform.h | signal handler semantics |
| `checkpoint_lock`, checkpoint buffers | checkpoint.h | `checkpoint_lock` |
| `finish_lock`, `g_stop_path` | hits.h | `finish_lock` |
| `survivors[]`, `n_surv` | search.h | main loop only |
| `g_lang` | i18n.h | — (set once during startup, read-only after) |

Lock discipline (unchanged from the monolith, documented here): `handle_hit` holds `hit_lock` for
its whole body and takes `stat_lock` inside for counter updates; `checkpoint_save` takes
`checkpoint_lock` (and `stat_lock` inside the state snapshot); `save_all_and_report` takes
`finish_lock` (and `stat_lock` inside `write_stop_file`). No lock is ever held across GPU calls. Do
not narrow these critical sections without re-running the full `tests/test_det.sh` suite.

## Contracts

**search <-> verifier (process pipe).** The search spawns `<root>/verify-go/w5addr` (MSYS2 bash on
Windows) and feeds JSON lines on stdin:

```
in:  {"words": "<phrase>", "mode"?: "w5"|"tg", "net"?: "mainnet"|"testnet"}
out: {"uq","eq","pub","as","tonValid","bip39Valid"}          (w5)
     {"uq","eq","pub","as","signpub","bip39Valid","tonValid"} (tg)
```

**checkpoint format "gramvanity-checkpoint" v1** (plain UTF-8 text, one `key = value` per line,
`[target]` sections, value enumerations as trailing comments): global scalars (`format`, `version`,
`mode`, `bounceable`, `net`, `job_dir`, `start`/`next`/`s1_next`/`end`, `done`/`unique_hits`/
`verified`, `key`), then per-target `{name, label, out, count, hits, ignore_case, match,
all_patterns, suffix/suffix_hits and prefix/prefix_hits pairs}`. The last significant line is
`checksum = <hex>` — SHA-256 over every other line of the file (CR bytes ignored), so any corruption
or hand edit is caught; `--checkpoint-fix` validates the fields and re-signs the file. Quoted values
(`\\ \" \n \r \t \xNN` escapes) keep spaces, quotes, `#` and arbitrary bytes intact; unknown or
duplicate keys are errors. The file is intentionally hand-editable: raise a `count`, change
patterns, add or remove a `[target]` block — then run `--checkpoint-fix` and `--resume`.

**Deterministic search:** candidate entropy = `HMAC-SHA512(searchKey, "svDET1" || mode || be64 index)`;
the tg-wallet signing half uses the separate mode byte 16 (`DETSIGN`). Same key + index = same
candidate; the key lives only in the checkpoint (owner-only file).

## Decisions and deviations from the original plan

1. **stats is its own module** (the plan initially folded it into hits): the checkpoint serializer
   must read counters under `stat_lock`, while hit handling calls `checkpoint_save()` — extracting
   stats keeps the dependency graph acyclic without changing any locking.
2. **The CLI parser stays a strcmp chain** inside `parse_args()` instead of becoming a table-driven
   parser: options have context-dependent semantics (global defaults vs the current `--target`
   block) and order-sensitive flags (`--ignore-case` position matters); a table would be a rewrite
   with regression risk for zero behavioral gain. The chain is now isolated in one function.
3. **`Derived` lives in addresses.h** (the plan placed it in sv.h): it is produced by
   `derive_candidate()` and consumed by main/hits/selftest, so the producer's header is its natural
   home; sv.h stays limited to cross-cutting enums and constants.
4. **`die()` is `__attribute__((noreturn))`** — it became a cross-module call during the split, and
   the annotation preserves both the optimizer's view and warning-parity with the monolith build.
5. Fixed-size buffers (`cmd[4400]`, `hit_files[64][300]`, 4 KB path buffers) were kept as-is on
   purpose: resizing them changes failure behavior; they are documented here as known limits
   (`hit_files` caps the stop-report file list at 64 per session).

## Verification

- `make -C opencl` — clean build, no new warnings (gcc -Wall -Wextra).
- `opencl/gramvanity --selftest-det` — all unit assertions pass (also in CI, no GPU needed).
- `sh opencl/tests/test_det.sh` — the 19-scenario GPU integration suite (121 checks; it pins
  `GRAMVANITY_LANG=en` because its assertions match English output).
- `python3 tools/check_i18n.py` — every `.md` pair exists and links to its translation (CI).
- Golden runs: deterministic-range scenarios produce identical `--checkpoint-info` output before and after
  the refactor (key-dependent fields such as the ton-24 "candidates done" count vary run-to-run by
  design, because every run draws a fresh random search key).
- `go vet ./verify-go && go test ./verify-go` — verifier parity tests pin the JS-tool golden values.
