#!/usr/bin/env bash
set -Eeuo pipefail

# EN: gramvanity — Windows build (MSYS2 UCRT64)
# RU: gramvanity — сборка для Windows (MSYS2 UCRT64)
#
# EN: Usage:
# RU: Использование:
#   bash ./build-windows.sh [--clean] [--check] [--lang en|ru]
#
# EN:   --clean   remove previous binaries first (same as CLEAN_BUILD=1)
# RU:   --clean   удалить старые бинарники перед сборкой (CLEAN_BUILD=1)
# EN:   --check   after a successful build run --selftest-det, go vet, go test and the i18n check
# RU:   --check   после сборки выполнить --selftest-det, go vet, go test и проверку i18n
# EN:   --lang    message language: en or ru (default: GRAMVANITY_LANG, then en)
# RU:   --lang    язык сообщений: en или ru (по умолчанию GRAMVANITY_LANG, затем en)
# EN:   --help    show this help
# RU:   --help    показать эту справку
#
# EN: Result: the release layout the search expects
# RU: Результат — раскладка, которую ожидает программа:
#   opencl/gramvanity.exe
#   verify-go/w5addr.exe

CLEAN_BUILD="${CLEAN_BUILD:-0}"
RUN_CHECKS="${RUN_CHECKS:-0}"
LANG_CODE="${GRAMVANITY_LANG:-en}"

# EN: Select the English or Russian variant of a message.
# RU: Выбрать английский или русский вариант сообщения.
msg() {
    if [[ "$LANG_CODE" == "ru" ]]; then
        printf '%s' "$2"
    else
        printf '%s' "$1"
    fi
}

say() {
    msg "$1" "$2"
    printf '\n'
}

warn() {
    printf '%s\n' "$(msg "$1" "$2")" >&2
}

fail() {
    printf '\nERROR: %s\n' "$(msg "$1" "$2")" >&2
    exit 1
}

usage_error() {
    printf 'ERROR: %s\n' "$(msg "$1" "$2")" >&2
    usage >&2
    exit 2
}

usage() {
    if [[ "$LANG_CODE" == "ru" ]]; then
        cat <<'EOF'
Использование:
  bash ./build-windows.sh [--clean] [--check] [--lang en|ru]

  --clean   удалить старые бинарники перед сборкой (CLEAN_BUILD=1)
  --check   после сборки выполнить --selftest-det, go vet, go test и проверку i18n
  --lang    язык сообщений: en или ru (по умолчанию GRAMVANITY_LANG, затем en)
  --help    показать эту справку

Результат — раскладка, которую ожидает программа:
  opencl/gramvanity.exe
  verify-go/w5addr.exe
EOF
    else
        cat <<'EOF'
Usage:
  bash ./build-windows.sh [--clean] [--check] [--lang en|ru]

  --clean   remove previous binaries first (same as CLEAN_BUILD=1)
  --check   after a successful build run --selftest-det, go vet, go test and the i18n check
  --lang    message language: en or ru (default: GRAMVANITY_LANG, then en)
  --help    show this help

Result: the release layout the search expects
  opencl/gramvanity.exe
  verify-go/w5addr.exe
EOF
    fi
}

# EN: Parse arguments before computing paths, so --help works in any shell. A pre-scan picks up
#     --lang first, so even error and help messages respect a --lang that appears later on the
#     command line. --help itself is deferred until the whole line is read.
# RU: Разбираем аргументы до вычисления путей, чтобы --help работал в любой оболочке. Предварительный
#     проход сначала находит --lang, поэтому даже сообщения об ошибках и справка учитывают --lang,
#     указанный позже в командной строке. Сам --help обрабатывается после разбора всей строки.
_prescan=("$@")
_prescan_i=0
while [[ $_prescan_i -lt ${#_prescan[@]} ]]; do
    case "${_prescan[$_prescan_i]}" in
        --lang=*) LANG_CODE="${_prescan[$_prescan_i]#*=}" ;;
        --lang)
            if [[ $((_prescan_i + 1)) -lt ${#_prescan[@]} ]]; then
                LANG_CODE="${_prescan[$((_prescan_i + 1))]}"
                _prescan_i=$((_prescan_i + 1))
            fi
            ;;
    esac
    _prescan_i=$((_prescan_i + 1))
done
unset _prescan _prescan_i

SHOW_HELP=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean) CLEAN_BUILD=1 ;;
        --check) RUN_CHECKS=1 ;;
        --lang)
            [[ $# -ge 2 ]] ||
                usage_error "Option --lang requires a value (en or ru)." "Опция --lang требует значение (en или ru)."
            LANG_CODE="$2"
            shift
            ;;
        --lang=*) LANG_CODE="${1#*=}" ;;
        -h|--help) SHOW_HELP=1 ;;
        *) usage_error "Unknown argument: $1" "Неизвестный аргумент: $1" ;;
    esac
    shift
done

case "$LANG_CODE" in
    en|ru) ;;
    *) fail "Unsupported language '$LANG_CODE' (expected en or ru)." "Неподдерживаемый язык '$LANG_CODE' (ожидается en или ru)." ;;
esac

if [[ "$SHOW_HELP" == "1" ]]; then
    usage
    exit 0
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENCL_DIR="$ROOT_DIR/opencl"
VERIFY_DIR="$ROOT_DIR/verify-go"

BUILD_LOG="$(mktemp)" || fail "Could not create a temporary build log." "Не удалось создать временный файл для лога."

cleanup() {
    rm -f "$BUILD_LOG"
}
trap cleanup EXIT

say "=== gramvanity Windows build ===" "=== Сборка gramvanity для Windows ==="
say "Project: $ROOT_DIR" "Проект: $ROOT_DIR"
printf '\n'

# EN: 1. Check the shell.
# RU: 1. Проверяем оболочку.
if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    fail "MSYS2 UCRT64 is required. Current MSYSTEM='${MSYSTEM:-not set}'." "Нужна MSYS2 UCRT64. Текущая MSYSTEM='${MSYSTEM:-не задана}'."
fi

if [[ "${MINGW_PREFIX:-}" != "/ucrt64" ]]; then
    fail "Wrong MINGW_PREFIX: '${MINGW_PREFIX:-not set}'. Expected /ucrt64." "Неверный MINGW_PREFIX: '${MINGW_PREFIX:-не задан}'. Ожидается /ucrt64."
fi

export OS=Windows_NT

say "MSYSTEM=$MSYSTEM" "MSYSTEM=$MSYSTEM"
say "OS=$OS" "OS=$OS"
say "MINGW_PREFIX=$MINGW_PREFIX" "MINGW_PREFIX=$MINGW_PREFIX"
printf '\n'

# EN: 2. Check the project layout.
# RU: 2. Проверяем структуру проекта.
[[ -f "$ROOT_DIR/go.mod" ]] || fail "go.mod not found in the repository root." "Не найден go.mod в корне проекта."
[[ -f "$OPENCL_DIR/Makefile" ]] || fail "opencl/Makefile not found." "Не найден opencl/Makefile."
[[ -f "$OPENCL_DIR/embedded.h" ]] ||
    fail "opencl/embedded.h not found. Generate it with: make -C opencl embed." "Не найден opencl/embedded.h. Сгенерируй его: make -C opencl embed."
[[ -f "$VERIFY_DIR/main.go" ]] || fail "verify-go/main.go not found." "Не найден verify-go/main.go."

# EN: embedded.h is generated; warn when a source is newer.
# RU: embedded.h — сгенерированный файл; предупреждаем, если ядра новее него.
for src in kernel.cl ed25519_gpu.cl sha512_consts.h english.txt; do
    if [[ -f "$OPENCL_DIR/$src" && "$OPENCL_DIR/$src" -nt "$OPENCL_DIR/embedded.h" ]]; then
        warn "WARNING: opencl/$src is newer than opencl/embedded.h." "WARNING: opencl/$src новее opencl/embedded.h."
        warn "         Run: make -C opencl embed" "         Выполни: make -C opencl embed"
        break
    fi
done

# EN: 3. Check the tools.
# RU: 3. Проверяем инструменты.
for tool in gcc make; do
    command -v "$tool" >/dev/null 2>&1 ||
        fail "'$tool' not found. Check the MSYS2 UCRT64 installation." "Не найдена команда '$tool'. Проверь установку в MSYS2 UCRT64."
done

# EN: GCC must belong to UCRT64.
# RU: GCC должен принадлежать UCRT64.
GCC_PATH="$(command -v gcc)"

case "$GCC_PATH" in
    /ucrt64/bin/*) ;;
    *) fail "GCC found outside UCRT64: $GCC_PATH" "GCC найден вне UCRT64: $GCC_PATH" ;;
esac

GCC_TARGET="$(gcc -dumpmachine)"

case "$GCC_TARGET" in
    x86_64-w64-mingw32*) ;;
    *) fail "Unsupported GCC target: $GCC_TARGET" "Неподходящая архитектура GCC: $GCC_TARGET" ;;
esac

# EN: 4. Locate Go.
# RU: 4. Ищем Go.
if ! command -v go >/dev/null 2>&1; then
    GO_WINDOWS_DIR="/c/Program Files/Go/bin"

    if [[ -f "$GO_WINDOWS_DIR/go.exe" ]]; then
        case ":$PATH:" in
            *":$GO_WINDOWS_DIR:"*) ;;
            *) export PATH="$PATH:$GO_WINDOWS_DIR" ;;
        esac
    fi
fi

command -v go >/dev/null 2>&1 ||
    fail "Go not found. Install Go or add its directory (for example, 'C:\\Program Files\\Go\\bin') to PATH." "Go не найден. Установи Go или добавь его каталог (например, 'C:\\Program Files\\Go\\bin') в PATH."

say "=== Toolchain ===" "=== Инструменты ==="
say "GCC: $GCC_PATH" "GCC: $GCC_PATH"
gcc --version | head -n 1 || true
say "GCC target: $GCC_TARGET" "GCC target: $GCC_TARGET"
say "Make: $(command -v make)" "Make: $(command -v make)"
make --version | head -n 1 || true
say "Go: $(command -v go)" "Go: $(command -v go)"
go version
printf '\n'

# EN: 5. Clean when requested.
# RU: 5. При необходимости выполняем чистую сборку.
if [[ "$CLEAN_BUILD" == "1" ]]; then
    say "=== Clean build requested ===" "=== Запрошена чистая сборка ==="
    make -C "$OPENCL_DIR" clean
    rm -f "$VERIFY_DIR/w5addr.exe" "$VERIFY_DIR/w5addr"
fi

# EN: 6. Build the Go verifier.
# RU: 6. Собираем Go-верификатор.
say "=== Building Go verifier ===" "=== Сборка Go-верификатора ==="

(
    cd "$ROOT_DIR"
    go build -trimpath -o "$VERIFY_DIR/w5addr.exe" ./verify-go
)

[[ -s "$VERIFY_DIR/w5addr.exe" ]] ||
    fail "verify-go/w5addr.exe is missing or empty after the build." "Не найден непустой verify-go/w5addr.exe после сборки."

say "Go verifier: OK" "Go-верификатор: OK"
printf '\n'

# EN: 7. Build OpenCL.
# RU: 7. Собираем OpenCL.
say "=== Building OpenCL application ===" "=== Сборка OpenCL-приложения ==="

if make -C "$OPENCL_DIR" 2>&1 | tee "$BUILD_LOG"; then
    say "OpenCL build: OK" "Сборка OpenCL: OK"

elif grep -Eqi 'undefined reference to.*BCryptGenRandom' "$BUILD_LOG"; then
    printf '\n'
    say "BCryptGenRandom error detected." "Обнаружена ошибка BCryptGenRandom."
    say "Checked shell: UCRT64." "Проверяемая оболочка: UCRT64."
    say "Retrying with the documented libraries." "Повторяем сборку с документированными библиотеками."

    make -C "$OPENCL_DIR" clean

    if make -C "$OPENCL_DIR" LIBS="-lOpenCL -lbcrypt -ladvapi32 -static-libgcc" 2>&1 | tee "$BUILD_LOG"; then
        say "OpenCL build with bcrypt: OK" "Сборка OpenCL с bcrypt: OK"
    else
        fail "The second OpenCL build failed." "Повторная сборка OpenCL завершилась ошибкой."
    fi

else
    fail "The OpenCL build failed. Read the make output above." "Сборка OpenCL завершилась ошибкой. Изучи вывод make выше."
fi

# EN: 8. Check the outputs.
# RU: 8. Проверяем результаты.
[[ -s "$OPENCL_DIR/gramvanity.exe" ]] ||
    fail "opencl/gramvanity.exe is missing or empty." "Не найден непустой opencl/gramvanity.exe."

[[ -s "$VERIFY_DIR/w5addr.exe" ]] ||
    fail "verify-go/w5addr.exe is missing or empty." "Не найден непустой verify-go/w5addr.exe."

# EN: 9. Optional checks after the build.
# RU: 9. Необязательные проверки после сборки.
if [[ "$RUN_CHECKS" == "1" ]]; then
    printf '\n'
    say "=== Post-build checks ===" "=== Проверки после сборки ==="

    say "-- selftest-det --" "-- selftest-det --"
    "$OPENCL_DIR/gramvanity.exe" --selftest-det

    say "-- go vet --" "-- go vet --"
    (
        cd "$ROOT_DIR"
        go vet ./verify-go
    )

    say "-- go test --" "-- go test --"
    (
        cd "$ROOT_DIR"
        go test -count=1 ./verify-go
    )

    say "-- i18n check --" "-- проверка i18n --"
    if command -v python3 >/dev/null 2>&1; then
        python3 "$ROOT_DIR/tools/check_i18n.py"
    elif command -v python >/dev/null 2>&1; then
        python "$ROOT_DIR/tools/check_i18n.py"
    else
        warn "Python not found; skipping the bilingual documentation check." "Python не найден; проверка пар документации пропущена."
    fi

    say "Checks: OK" "Проверки: OK"
fi

printf '\n'
say "=== BUILD SUCCESSFUL ===" "=== СБОРКА УСПЕШНА ==="
say "OpenCL:   $OPENCL_DIR/gramvanity.exe" "OpenCL:   $OPENCL_DIR/gramvanity.exe"
say "Verifier: $VERIFY_DIR/w5addr.exe" "Verifier: $VERIFY_DIR/w5addr.exe"
printf '\n'
say "Next steps:" "Дальнейшие шаги:"
say "  ./opencl/gramvanity.exe --selftest-det" "  ./opencl/gramvanity.exe --selftest-det"
say "  ./opencl/gramvanity.exe --list-devices" "  ./opencl/gramvanity.exe --list-devices"
say "  go test -count=1 ./verify-go" "  go test -count=1 ./verify-go"
say "  sh opencl/tests/test_det.sh   # GPU integration suite (needs a GPU)" "  sh opencl/tests/test_det.sh   # интеграционный GPU-набор (нужен GPU)"
