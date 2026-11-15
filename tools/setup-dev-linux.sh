#!/bin/sh
# gramvanity - developer environment setup (Ubuntu/Debian).
#
# EN: Prepares a Linux machine for building gramvanity after the repository has been cloned:
# RU: Готовит машину с Linux к сборке gramvanity после клонирования репозитория:
#
# EN: 1. installs the missing system packages via apt (sudo is used ONLY for these packages; the
# EN:    script refuses to run entirely as root);
# RU: 1. ставит недостающие системные пакеты через apt (sudo используется ТОЛЬКО для них; сам
# RU:    скрипт целиком от root не запускается);
# EN: 2. uses the existing Go when it satisfies go.mod; when it is missing or too old, unpacks the
# EN:    official tarball into ~/.local/gramvanity-dev/tools and puts it on the SESSION PATH -
# EN:    the existing Go and the distro packages are never upgraded;
# RU: 2. использует существующий Go, если он удовлетворяет go.mod; если его нет или он слишком
# RU:    стар - распаковывает официальный tarball в ~/.local/gramvanity-dev/tools и подключает
# RU:    его через СЕССИОННЫЙ PATH - существующий Go и пакеты дистрибутива никогда не обновляются;
# EN: 3. builds both programs with the project's own commands and runs the CPU checks
# EN:    (--selftest-det, go vet, go test, the i18n and tr() checks);
# RU: 3. собирает обе программы штатными командами проекта и выполняет CPU-проверки
# RU:    (--selftest-det, go vet, go test, проверки i18n и tr());
# EN: 4. with --with-gpu-tests also checks the OpenCL device, runs a short benchmark and the full
# EN:    GPU integration suite (needs a real GPU, takes about 7 minutes).
# RU: 4. с --with-gpu-tests дополнительно проверяет устройство OpenCL, запускает короткий
# RU:    benchmark и полный GPU-набор (нужен настоящий GPU, около 7 минут).
#
# Usage / Использование:
#   ./tools/setup-dev-linux.sh [--lang en|ru] [--dry-run] [--with-gpu-tests]

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

DEV_ROOT="$HOME/.local/gramvanity-dev"
TOOLS_DIR="$DEV_ROOT/tools"
MANIFEST="$DEV_ROOT/.dev-setup-manifest"

# EN: system packages from the plan; installed only when actually missing.
# RU: системные пакеты из плана; ставятся только те, которых действительно нет.
APT_PACKAGES='git build-essential ocl-icd-opencl-dev clinfo python3 ca-certificates curl'

LANG_CODE="${GRAMVANITY_LANG:-en}"
DRY_RUN=0
WITH_GPU_TESTS=0

msg() { if [ "$LANG_CODE" = "ru" ]; then printf '%s' "${2-}"; else printf '%s' "$1"; fi; }
say() { msg "$1" "${2-}"; printf '\n'; }
warn() { printf '%s\n' "$(msg "$1" "${2-}")" >&2; }
fail() { printf '\nERROR: %s\n' "$(msg "$1" "${2-}")" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --lang)
            [ $# -ge 2 ] || fail "Option --lang requires a value (en or ru)." "Опция --lang требует значение (en или ru)."
            LANG_CODE="$2"; shift ;;
        --lang=*) LANG_CODE="${1#*=}" ;;
        --dry-run) DRY_RUN=1 ;;
        --with-gpu-tests) WITH_GPU_TESTS=1 ;;
        -h|--help)
            say "Usage: ./tools/setup-dev-linux.sh [--lang en|ru] [--dry-run] [--with-gpu-tests]" \
                "Использование: ./tools/setup-dev-linux.sh [--lang en|ru] [--dry-run] [--with-gpu-tests]"
            say "  --dry-run         print the plan without installing or building anything" \
                "  --dry-run         показать план действий, ничего не ставя и не собирая"
            say "  --with-gpu-tests  after the CPU checks also run the GPU benchmark and test suite" \
                "  --with-gpu-tests  после CPU-проверок также запустить benchmark и GPU-набор"
            exit 0 ;;
        *) fail "Unknown argument: $1 (see --help)" "Неизвестный аргумент: $1 (см. --help)" ;;
    esac
    shift
done

case "$LANG_CODE" in
    en|ru) ;;
    *) LANG_CODE='en' ;;
esac

add_manifest() {
    mkdir -p "$DEV_ROOT" 2>/dev/null
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M')" "$1" >> "$MANIFEST" 2>/dev/null
}

# ---------------------------------------------------------------- environment checks

# EN: 1. This script supports Ubuntu/Debian (apt) only. Other distributions get the dependency
# RU: 1. Скрипт поддерживает только Ubuntu/Debian (apt). Другим дистрибутивам выводится список
# EN:    list instead of a fake install.
# RU:    зависимостей вместо имитации поддержки.
case "$(uname -s)" in
    Linux) ;;
    *) fail "This setup is for Linux. On Windows use tools/setup-dev.ps1." \
            "Этот setup - для Linux. В Windows используйте tools/setup-dev.ps1." ;;
esac
if ! command -v apt-get >/dev/null 2>&1; then
    warn "This setup supports Ubuntu/Debian (apt) only. Install these packages with your distribution's package manager and build as the README describes:" \
         "Этот setup поддерживает только Ubuntu/Debian (apt). Установите следующие пакеты менеджером вашего дистрибутива и соберите, как описано в README:"
    say '' ''
    say "    $APT_PACKAGES" "    $APT_PACKAGES"
    say ''
    say "    Go $(sed -n 's/^go \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1.\2/p' go.mod | head -n 1) or newer - https://go.dev/dl/" \
        "    Go $(sed -n 's/^go \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1.\2/p' go.mod | head -n 1) или новее - https://go.dev/dl/"
    exit 1
fi

# EN: not as root: sudo is used only for the apt step, everything else stays a user process.
# RU: не от root: sudo используется только для шага с apt, всё остальное - от пользователя.
if [ "$(id -u)" = "0" ]; then
    fail "Do not run this setup as root. Run it as a normal user: sudo is used only for the system packages step." \
         "Не запускайте setup от root. Запустите от обычного пользователя: sudo используется только на шаге системных пакетов."
fi
if ! command -v sudo >/dev/null 2>&1; then
    fail "sudo is not available, so the missing system packages cannot be installed. Install them manually: $APT_PACKAGES" \
         "sudo недоступен, поэтому недостающие системные пакеты не установить. Установите их вручную: $APT_PACKAGES"
fi

say "=== gramvanity developer setup (Ubuntu/Debian) ===" "=== Настройка окружения разработчика gramvanity (Ubuntu/Debian) ==="
say "Repository: $ROOT" "Репозиторий: $ROOT"
say "Tools folder: $TOOLS_DIR" "Папка инструментов: $TOOLS_DIR"
say '' ''

# ---------------------------------------------------------------- system packages

# EN: 2. Detect, do not reinstall: only packages dpkg does not know about are passed to apt.
# RU: 2. Обнаруживаем, а не переустанавливаем: apt передаём только пакеты, которых нет у dpkg.
MISSING=''
for pkg in $APT_PACKAGES; do
    if dpkg -s "$pkg" >/dev/null 2>&1; then
        say "apt: $pkg - present" "apt: $pkg - установлен"
    else
        say "apt: $pkg - missing" "apt: $pkg - отсутствует"
        MISSING="$MISSING $pkg"
    fi
done

if [ -n "$MISSING" ]; then
    if [ "$DRY_RUN" = "1" ]; then
        say ''
        say "DRY RUN - would run: sudo apt-get update && sudo apt-get install -y$MISSING" \
            "DRY RUN - выполнил бы: sudo apt-get update && sudo apt-get install -y$MISSING"
    else
        say ''
        say "Installing system packages (sudo is used only here):$MISSING" \
            "Устанавливаем системные пакеты (sudo используется только здесь):$MISSING"
        sudo apt-get update || fail "apt-get update failed. Check the network and the sources list, then re-run." \
                                   "apt-get update не удался. Проверьте сеть и список источников, затем запустите снова."
        sudo apt-get install -y$MISSING || fail "apt-get install failed. Read the output above; fix the package problem and re-run." \
                                                 "apt-get install не удался. Прочитайте вывод выше, устраните проблему с пакетами и запустите снова."
    fi
else
    say "apt: all system packages are present - nothing to install" "apt: все системные пакеты установлены - ставить нечего"
fi
say '' ''

# ---------------------------------------------------------------- go

# EN: 3. Go: use the existing one when it satisfies go.mod; otherwise unpack the official tarball
# RU: 3. Go: используем существующий, если он удовлетворяет go.mod; иначе распаковываем
# EN:    into the tools folder (the zone this setup may write) and use the SESSION PATH. The
# EN:    existing Go and the distro packages are never upgraded.
# RU:    официальный tarball в папку инструментов (зона записи этого setup) и подключаем через
# RU:    СЕССИОННЫЙ PATH. Существующий Go и пакеты дистрибутива никогда не обновляются.
GO_REQ=$(sed -n 's/^go \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p' go.mod | head -n 1)
[ -n "$GO_REQ" ] || fail "Could not parse the 'go' directive from go.mod." "Не удалось разобрать директиву 'go' в go.mod."
REQ_MAJOR=${GO_REQ% *}
REQ_MINOR=${GO_REQ#* }
GO_DOTREQ="$REQ_MAJOR.$REQ_MINOR"

go_ok() {
    # go_ok GO_EXE -> exit 0 when the binary satisfies go.mod
    _ver=$("$1" version 2>/dev/null | sed -n 's/^go version go\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p')
    [ -n "$_ver" ] || return 1
    awk -v im="${_ver% *}" -v in_="${_ver#* }" -v rm="$REQ_MAJOR" -v rn="$REQ_MINOR" \
        'BEGIN { exit !(im > rm || (im == rm && in_ >= rn)) }'
}

GO_SESSION=''
GO_SOURCE=''
if command -v go >/dev/null 2>&1; then
    if go_ok "$(command -v go)"; then
        GO_SESSION=$(command -v go)
        GO_SOURCE='existing'
        say "go: $(go version) - OK for go.mod" "go: $(go version) - подходит для go.mod"
    else
        warn "go: the existing $(go version 2>/dev/null | cut -d' ' -f3) is older than go.mod requires ($GO_DOTREQ); it is NOT touched. A portable Go will be used for this session." \
             "go: существующий $(go version 2>/dev/null | cut -d' ' -f3) старше требуемого в go.mod ($GO_DOTREQ); он НЕ тронут. Для этой сессии будет использован переносимый Go."
    fi
else
    say "go: not found on PATH" "go: не найден на PATH"
fi

if [ -z "$GO_SESSION" ]; then
    case "$(uname -m)" in
        x86_64) GO_ARCH='amd64' ;;
        aarch64|arm64) GO_ARCH='arm64' ;;
        *) fail "Unsupported architecture: $(uname -m). Official Go tarballs exist for x86_64 and arm64." \
               "Неподдерживаемая архитектура: $(uname -m). Официальные tarball Go есть для x86_64 и arm64." ;;
    esac
    GO_VER="go$GO_DOTREQ.0"
    GO_PORTABLE_DIR="$TOOLS_DIR/$GO_VER"
    GO_PORTABLE_EXE="$GO_PORTABLE_DIR/go/bin/go"
    if [ -x "$GO_PORTABLE_EXE" ]; then
        say "go: portable $GO_VER already present in $GO_PORTABLE_DIR - reusing it" \
            "go: переносимый $GO_VER уже есть в $GO_PORTABLE_DIR - переиспользуем"
    elif [ "$DRY_RUN" = "1" ]; then
        say "go: DRY RUN - would download https://go.dev/dl/$GO_VER.linux-$GO_ARCH.tar.gz and unpack it into $GO_PORTABLE_DIR" \
            "go: DRY RUN - скачал бы https://go.dev/dl/$GO_VER.linux-$GO_ARCH.tar.gz и распаковал в $GO_PORTABLE_DIR"
    else
        say "go: downloading https://go.dev/dl/$GO_VER.linux-$GO_ARCH.tar.gz ..." \
            "go: скачиваем https://go.dev/dl/$GO_VER.linux-$GO_ARCH.tar.gz ..."
        TMP_GO=$(mktemp -d) || fail "Could not create a temporary directory." "Не удалось создать временный каталог."
        trap 'rm -rf "$TMP_GO"' EXIT INT TERM
        if command -v curl >/dev/null 2>&1; then
            curl -fsSL -o "$TMP_GO/go.tar.gz" "https://go.dev/dl/$GO_VER.linux-$GO_ARCH.tar.gz" \
                || fail "Could not download the Go tarball. Check the network, or install Go manually and re-run." \
                        "Не удалось скачать tarball Go. Проверьте сеть или установите Go вручную и запустите снова."
        else
            wget -qO "$TMP_GO/go.tar.gz" "https://go.dev/dl/$GO_VER.linux-$GO_ARCH.tar.gz" \
                || fail "Could not download the Go tarball. Check the network, or install Go manually and re-run." \
                        "Не удалось скачать tarball Go. Проверьте сеть или установите Go вручную и запустите снова."
        fi
        mkdir -p "$GO_PORTABLE_DIR" || fail "Could not create $GO_PORTABLE_DIR." "Не удалось создать $GO_PORTABLE_DIR."
        tar -xzf "$TMP_GO/go.tar.gz" -C "$GO_PORTABLE_DIR" || fail "Could not unpack the Go tarball." "Не удалось распаковать tarball Go."
        [ -x "$GO_PORTABLE_EXE" ] || fail "The Go tarball did not produce go/bin/go where expected." "Tarball Go не создал go/bin/go там, где ожидалось."
        add_manifest "installed portable $GO_VER -> $GO_PORTABLE_DIR"
        rm -rf "$TMP_GO"
        trap - EXIT INT TERM
        say "go: portable $GO_VER unpacked into $GO_PORTABLE_DIR (session PATH only; the global PATH is not modified)" \
            "go: переносимый $GO_VER распакован в $GO_PORTABLE_DIR (только сессионный PATH; глобальный PATH не меняется)"
    fi
    GO_SESSION="$GO_PORTABLE_EXE"
    GO_SOURCE='portable'
fi

# EN: session PATH only: the chosen Go goes first for this process and its children; the global
# RU: только сессионный PATH: выбранный Go становится первым для этого процесса и его потомков;
# EN: PATH, the shell profiles and the registry are never modified by this setup.
# RU: глобальный PATH, профили оболочки и реестр этим setup не изменяются.
GO_SESSION_DIR=$(dirname "$GO_SESSION")
case ":$PATH:" in
    *":$GO_SESSION_DIR:"*) ;;
    *) PATH="$GO_SESSION_DIR:$PATH"; export PATH ;;
esac
if ! go_ok "$GO_SESSION"; then
    fail "The session Go does not satisfy go.mod ($GO_DOTREQ required). Install Go $GO_DOTREQ or newer manually." \
         "Сессионный Go не удовлетворяет go.mod (требуется $GO_DOTREQ). Установите Go $GO_DOTREQ или новее вручную."
fi

# ---------------------------------------------------------------- build and CPU checks

# EN: 4. Build and checks: the project's own commands, no duplicated logic. Python is required by
# RU: 4. Сборка и проверки: штатные команды проекта, без дублирования логики. Python нужен для
# EN:    the i18n/tr() checks; a missing Python skips them with a warning (make/clang are not
# EN:    needed here - the Makefile drives the C build).
# RU:    проверок i18n/tr(); отсутствующий Python пропускает их с предупреждением (make/clang
# RU:    здесь не нужны - сборкой C управляет Makefile).
if [ "$DRY_RUN" = "1" ]; then
    say ''
    say "DRY RUN - the build and checks would now run:" "DRY RUN - сейчас выполнились бы сборка и проверки:"
    say "  go build -trimpath -o verify-go/w5addr ./verify-go" "  go build -trimpath -o verify-go/w5addr ./verify-go"
    say "  make -C opencl" "  make -C opencl"
    say "  ./opencl/gramvanity --selftest-det" "  ./opencl/gramvanity --selftest-det"
    say "  go vet ./verify-go && go test -count=1 ./verify-go" "  go vet ./verify-go && go test -count=1 ./verify-go"
    say "  python3 tools/check_i18n.py && python3 tools/check_tr.py" "  python3 tools/check_i18n.py && python3 tools/check_tr.py"
    exit 0
fi

say ''
say "=== Building the Go verifier ===" "=== Сборка Go-верификатора ==="
go build -trimpath -o verify-go/w5addr ./verify-go || fail "The verifier build failed. Read the output above." \
                                                            "Сборка верификатора не удалась. Прочитайте вывод выше."
say "verify-go/w5addr: OK" "verify-go/w5addr: OK"

say ''
say "=== Building the OpenCL search ===" "=== Сборка OpenCL-поиска ==="
make -C opencl || fail "The search build failed. Read the output above." \
                       "Сборка поиска не удалась. Прочитайте вывод выше."

say ''
say "=== CPU checks ===" "=== CPU-проверки ==="
./opencl/gramvanity --selftest-det || fail "SELF-TEST FAILED - do not search on this machine until the cause is clear (see the README)." \
                                           "САМОТЕСТ ПРОВАЛЕН - не запускайте поиск на этой машине, пока причина не ясна (см. README)."
go vet ./verify-go || fail "go vet failed. Read the output above." "go vet не удался. Прочитайте вывод выше."
go test -count=1 ./verify-go || fail "go test failed. Read the output above." "go test не удался. Прочитайте вывод выше."

if command -v python3 >/dev/null 2>&1; then
    python3 tools/check_i18n.py || fail "The i18n check failed." "Проверка i18n не удалась."
    python3 tools/check_tr.py || fail "The tr() check failed." "Проверка tr() не удалась."
else
    warn "python3 is not available; the i18n and tr() checks were skipped." \
         "python3 недоступен; проверки i18n и tr() пропущены."
fi

# ---------------------------------------------------------------- GPU part

# EN: 5. GPU: --list-devices always runs so the user sees the state; the heavy GPU suite runs
# RU: 5. GPU: --list-devices выполняется всегда, чтобы пользователь видел состояние; тяжёлый
# EN:    only with --with-gpu-tests (it takes about 7 minutes and needs a real OpenCL device).
# RU:    GPU-набор запускается только с --with-gpu-tests (около 7 минут, нужен настоящий GPU).
say ''
say '-- list-devices --' '-- list-devices --'
LIST_OUT=$(./opencl/gramvanity --list-devices --lang en 2>&1) || LIST_OUT=''
[ -n "$LIST_OUT" ] && printf '%s\n' "$LIST_OUT"
GPU_OK=0
printf '%s\n' "$LIST_OUT" | grep -qi 'no OpenCL GPU found' || GPU_OK=1

if [ "$WITH_GPU_TESTS" = "1" ]; then
    if [ "$GPU_OK" = "0" ]; then
        warn "No OpenCL GPU was found, so the GPU benchmark and suite cannot run here. See the README for the driver packages of your vendor (NVIDIA/AMD/Intel)." \
             "GPU с OpenCL не найден, поэтому benchmark и GPU-набор здесь выполнить нельзя. Пакеты драйвера для NVIDIA/AMD/Intel описаны в README."
        exit 1
    fi
    say ''
    say "=== GPU benchmark (60 s) ===" "=== GPU benchmark (60 с) ==="
    ./opencl/gramvanity --mode bip39-12 --bench 60 || fail "The GPU benchmark failed. Read the output above." \
                                                           "GPU benchmark не удался. Прочитайте вывод выше."
    say ''
    say "=== GPU integration suite (about 7 minutes) ===" "=== Интеграционный GPU-набор (около 7 минут) ==="
    sh opencl/tests/test_det.sh || fail "The GPU suite failed. Read the output above." \
                                        "GPU-набор не удался. Прочитайте вывод выше."
else
    if [ "$GPU_OK" = "1" ]; then
        say ''
        say "GPU device found. The full GPU suite was NOT run (use --with-gpu-tests for the benchmark and the ~7-minute suite)." \
            "GPU найден. Полный GPU-набор НЕ запускался (используйте --with-gpu-tests для benchmark и набора на ~7 минут)."
    else
        warn "No OpenCL GPU found. The build and CPU checks are done; the search needs a GPU driver with OpenCL support (see the README)." \
             "GPU с OpenCL не найден. Сборка и CPU-проверки выполнены; поиску нужен драйвер GPU с поддержкой OpenCL (см. README)."
    fi
fi

say '' ''
say "=== Developer setup finished ===" "=== Настройка окружения разработчика завершена ==="
say "Binaries: opencl/gramvanity and verify-go/w5addr" "Бинарники: opencl/gramvanity и verify-go/w5addr"
say "Go in use: $GO_SESSION ($GO_SOURCE)" "Используемый Go: $GO_SESSION ($GO_SOURCE)"
if [ "$GO_SOURCE" = "portable" ]; then
    say "The portable Go is connected by the session PATH of this run only; new shells will not see it." \
        "Переносимый Go подключён только сессионным PATH этого запуска; новые оболочки его не увидят."
fi
exit 0
