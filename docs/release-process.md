# Release process

> Language: **English** | [Русский](release-process.ru.md)

The practical guide to releasing gramvanity: versioning, commits, the
changelog, the checklist and what the automation does. The process is
tag-driven — the release commit plus one annotated tag is all that is done
by hand; `.github/workflows/release.yml` builds, packages and publishes
everything else.

## Versioning

- Versions follow [SemVer](https://semver.org/) — `MAJOR.MINOR.PATCH`:
  - **MAJOR** — backward-incompatible changes: checkpoints or hit files
    stop loading from older versions, a platform or a documented flag is
    removed, the binary layout changes;
  - **MINOR** — new user-visible features: new modes, flags, outputs,
    wallet support;
  - **PATCH** — bug fixes, documentation-only updates, internal
    refactors.
- The tag is annotated, named `vX.Y.Z`, with the message `Release vX.Y.Z`.
- The tag version must equal the newest version section in
  `CHANGELOG.md`/`CHANGELOG.ru.md`. Every published version gets its own
  tag: the changelog and the tag list must always agree.
- The version number lives only in the changelog and the tag. Release
  archives take it from the tag name (`gramvanity-vX.Y.Z-<platform>`,
  see `release.yml`). The binaries carry no version stamp yet — see
  [Known limitations](#known-limitations).

## Commits

- Messages follow
  [Conventional Commits](https://www.conventionalcommits.org/):
  `type(scope): summary`, imperative mood. Types in use: `feat`, `fix`,
  `docs`, `ci`, `build`, `test`, `refactor`, `chore`.
- Breaking changes: `!` after the type/scope plus a `BREAKING CHANGE:`
  footer (see `feat(verify-go)!` and `refactor!` in the history).
- A user-visible change adds its line under `[Unreleased]` in both
  changelogs in the same commit.

## Changelog

`CHANGELOG.md` / `CHANGELOG.ru.md` follow
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) with SemVer
versions. The two files are a language pair: change both in the same
commit (`tools/check_i18n.py` checks the pair; CI runs it).

- `## [Unreleased]` is always present at the top; changes accumulate
  there as they land.
- A version section is `## [X.Y.Z] - YYYY-MM-DD`.
- Categories inside a section: **Added**, **Changed**, **Deprecated**,
  **Removed**, **Fixed**, **Security** (in the Russian file:
  Добавлено, Изменено, Устарело, Удалено, Исправлено, Безопасность);
  drop the empty ones. Mark backward-incompatible lines with
  `**BREAKING**` and say how to migrate.
- Entries are written for users, not for git: what changed, not which
  commit changed it. Compare links for all versions and for
  `[Unreleased]` live at the end of the file.
- Must go in: user-visible behaviour, fixed bugs, removed features,
  platform and build changes, breaking changes, new configuration or
  environment variables, checkpoint and hit file format changes.
- Optional: internal refactors, dependency updates, CI changes.
- The GitHub release body is taken from the English `CHANGELOG.md`
  (`release.yml`), so entries must read as release notes on their own.

## Release checklist

### Preparation

1. All planned work is committed to `main` and CI is green
   (`.github/workflows/ci.yml` runs on every push).
2. File pairs EN/RU are updated together; `python3 tools/check_i18n.py`
   passes.
3. If `opencl/kernel.cl`, `opencl/ed25519_gpu.cl`,
   `opencl/sha512_consts.h` or `opencl/english.txt` changed:
   `make -C opencl embed`, review the `opencl/embedded.h` diff and
   include it in the change.
4. Local verification (`AGENTS.md` → "Verification"):
   - `./opencl/gramvanity.exe --selftest-det` — after any C/OpenCL
     change;
   - `go vet ./verify-go` and `go test -count=1 ./verify-go` — after any
     verifier change;
   - `bash ./build-windows.sh --check` — the full Windows build gate;
   - if the GPU path changed: `./opencl/gramvanity.exe --list-devices`,
     a short `--mode bip39-12 --bench 60` run and
     `sh opencl/tests/test_det.sh` (needs a GPU; runs locally only — CI
     cannot run it).
5. Both changelogs carry the release's changes under `[Unreleased]`.

### Cutting the release

6. In both changelogs: rename `[Unreleased]` → `[X.Y.Z] - YYYY-MM-DD`,
   start a fresh empty `[Unreleased]` above it, refresh the compare
   links at the bottom.
7. Commit both files:
   `chore(release): mark vX.Y.Z in the changelog`.
8. Tag the release commit:
   `git tag -a vX.Y.Z -m "Release vX.Y.Z"`.
9. Sanity check: `git tag -l` shows `vX.Y.Z`, it equals the newest
   changelog section, and `git status` is clean.
10. Push the commit and the tag together:
    `git push origin main vX.Y.Z`.
11. Watch the `release` workflow (Actions → release): two build jobs
    (linux-x64, windows-x64), then `publish`. Takes a few minutes.
12. Verify the release page: title `vX.Y.Z`; body from the changelog;
    four assets — `gramvanity-vX.Y.Z-linux-x64.tar.gz`,
    `gramvanity-vX.Y.Z-windows-x64.zip` and a `.sha256` for each.
13. Nothing else ships by hand: binaries are never committed
    (`.gitignore`), `results/` never leaves the machine.

## What the automation does

Pushing a `v*` tag triggers `.github/workflows/release.yml`:

1. Builds the search on both platforms (Linux: `make -C opencl`;
   Windows: MSYS2 UCRT64 `make` — the same commands as CI).
2. Builds the verifier: `go vet ./verify-go`, `go test ./verify-go`,
   `go build -trimpath -ldflags "-s -w"`.
3. Packages `gramvanity-<tag>-<platform>` archives that keep the
   repository layout — `opencl/gramvanity[.exe]`,
   `verify-go/w5addr[.exe]`, `docs/tg-wallet.md`, README, CHANGELOG,
   LICENSE, NOTICE — plus a `.sha256` per archive.
4. Publishes the GitHub release: body = `CHANGELOG.md`, the four files
   from step 3, plus GitHub's source-code archives and the auto
   changelog link.

Running the workflow manually (`workflow_dispatch`) builds and uploads
the archives as workflow artifacts only — no release is created. Use it
to test packaging changes.

## Release notes template

A changelog section doubles as the release notes; keep this shape:

```markdown
## [X.Y.Z] - YYYY-MM-DD

<One or two sentences: the point of the release.>

### Added
- ...

### Changed
- ...

### Fixed
- ...
```

Add `Deprecated`, `Removed` and `Security` sections only when there is
content for them, and drop the rest. Mirror the section in
`CHANGELOG.ru.md`. A `**BREAKING**` line names the affected command,
file or platform and the migration path.

## Known limitations

- Neither binary has `--version` or a build-time version stamp; a build
  is identified by its archive name or the changelog it ships with.
- The release body is the whole `CHANGELOG.md`, not only the new
  section, so release notes grow with every version.
- `release.yml` reruns `go vet`/`go test` but not `--selftest-det`, and
  the GPU suite never runs in CI — the local steps in the checklist are
  what actually covers them.
