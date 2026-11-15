#!/bin/sh
# gramvanity - developer environment setup, stage 2 (MSYS2 UCRT64).
#
# EN: Runs inside an MSYS2 shell (launched by tools/setup-dev.ps1, but works standalone too):
# RU: Запускается внутри оболочки MSYS2 (её вызывает tools/setup-dev.ps1, но можно и вручную):
#
# EN: 1. installs the four UCRT64 packages the build needs (targeted, --needed, never a full
# EN:    `pacman -Syu`);
# RU: 1. ставит четыре нужных сборке пакета UCRT64 (прицельно, --needed, полный `pacman -Syu`
# RU:    не выполняется никогда);
# EN: 2. checks the toolchain gates (UCRT64 shell, gcc target, Go version against go.mod);
# RU: 2. проверяет точки контроля (оболочка UCRT64, цель gcc, версия Go против go.mod);
# EN: 3. calls the existing build gate: `bash ./build-windows.sh --check` - its logic (UCRT64
# EN:    checks, GCC, Go discovery, the BCryptGenRandom fallback, output checks, self-test,
# EN:    go vet/test, i18n) is NOT duplicated here.
# RU: 3. вызывает существующую точку сборки: `bash ./build-windows.sh --check` - её логика
# RU:    (проверки UCRT64, GCC, поиск Go, запасной путь BCryptGenRandom, проверка результатов,
# RU:    self-test, go vet/test, i18n) здесь НЕ дублируется.
#
# Usage / Использование:
#   ./tools/setup-dev-msys2.sh [--lang en|ru]

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

LANG_CODE="${GRAMVANITY_LANG:-en}"

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
        -h|--help)
            say "Usage: ./tools/setup-dev-msys2.sh [--lang en|ru]" "Использование: ./tools/setup-dev-msys2.sh [--lang en|ru]"
            say "Stage 2 of the developer setup: UCRT64 packages + build-windows.sh --check." "Этап 2 настройки разработчика: пакеты UCRT64 + build-windows.sh --check."
            exit 0 ;;
        *) fail "Unknown argument: $1 (see --help)" "Неизвестный аргумент: $1 (см. --help)" ;;
    esac
    shift
done

case "$LANG_CODE" in
    en|ru) ;;
    *) LANG_CODE='en' ;;
esac

# ---------------------------------------------------------------- shell gates

# EN: 1. This stage must run in MSYS2 UCRT64 - the build-windows.sh contract, and the same
# RU: 1. Этот этап должен выполняться в MSYS2 UCRT64 - контракт build-windows.sh, и то же
# EN:    environment the project documents everywhere else.
# RU:    окружение задокументировано проектом везде.
if [ "${MSYSTEM:-}" != "UCRT64" ]; then
    fail "MSYS2 UCRT64 is required; this shell reports MSYSTEM='${MSYSTEM:-not set}'. Open the MSYS2 UCRT64 shell and re-run this script." "Нужна MSYS2 UCRT64; эта оболочка сообщает MSYSTEM='${MSYSTEM:-не задан}'. Откройте оболочку MSYS2 UCRT64 и запустите скрипт снова."
fi
if [ "${MINGW_PREFIX:-}" != "/ucrt64" ]; then
    fail "Wrong MINGW_PREFIX: '${MINGW_PREFIX:-not set}' (expected /ucrt64)." "Неверный MINGW_PREFIX: '${MINGW_PREFIX:-not set}' (ожидался /ucrt64)."
fi

say "=== gramvanity developer setup: stage 2 (MSYS2 UCRT64) ===" "=== Настройка окружения разработчика gramvanity: этап 2 (MSYS2 UCRT64) ==="
say "Repository: $ROOT" "Репозиторий: $ROOT"
say "MSYSTEM=$MSYSTEM, MINGW_PREFIX=$MINGW_PREFIX" "MSYSTEM=$MSYSTEM, MINGW_PREFIX=$MINGW_PREFIX"
say '' ''

# ---------------------------------------------------------------- targeted pacman packages

# EN: 2. Install only the four documented packages, with --needed: on a configured machine this
# RU: 2. Ставим только четыре документированных пакета с --needed: на настроенной машине это
# EN:    is a no-op, never a full system upgrade.
# RU:    пустая операция, а не обновление всей системы.
PACKAGES='make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-opencl-headers mingw-w64-ucrt-x86_64-opencl-icd'
say "pacman packages: $PACKAGES" "Пакеты pacman: $PACKAGES"
if ! pacman -S --needed --noconfirm $PACKAGES; then
    warn '' ''
    warn "pacman could not install the packages. On a fresh MSYS2 the first step from https://www.msys2.org/ is required:" "pacman не смог установить пакеты. На свежей MSYS2 сначала нужен первый шаг с https://www.msys2.org/:"
    say ''
    say '    pacman -Syu' '    pacman -Syu'
    say ''
    say "Run it once (it may ask to close the terminal - close it, open MSYS2 UCRT64 again), then re-run this script. This setup never runs a full upgrade by itself." "Выполните его один раз (он может попросить закрыть терминал - закройте, снова откройте MSYS2 UCRT64), затем запустите этот скрипт снова. Этот setup никогда не выполняет полное обновление сам."
    exit 1
fi

# ---------------------------------------------------------------- toolchain gates

# EN: 3. Gates after the packages, fail fast with the next action:
# RU: 3. Проверки после пакетов, остановка с конкретным следующим действием:
# EN:    gcc must exist and target x86_64-w64-mingw32 (UCRT64);
# RU:    gcc должен существовать и целиться в x86_64-w64-mingw32 (UCRT64);
# EN:    Go must exist (stage 1 may have put a portable one on the session PATH) and satisfy go.mod.
# RU:    Go должен существовать (этап 1 мог подключить переносимый через сессионный PATH) и удовлетворять go.mod.
GCC_TARGET=$(gcc -dumpmachine 2>/dev/null) || GCC_TARGET=''
case "$GCC_TARGET" in
    x86_64-w64-mingw32*) say "gcc: $GCC_TARGET - OK" "gcc: $GCC_TARGET - подходит" ;;
    *) fail "gcc is missing or does not target x86_64-w64-mingw32 (got '$GCC_TARGET'). The UCRT64 gcc package is mingw-w64-ucrt-x86_64-gcc; reopen the UCRT64 shell and re-run." "gcc отсутствует или целится не в x86_64-w64-mingw32 (получено '$GCC_TARGET'). Пакет gcc для UCRT64 - mingw-w64-ucrt-x86_64-gcc; переоткройте оболочку UCRT64 и запустите снова." ;;
esac

if ! command -v go >/dev/null 2>&1; then
    fail "Go is not on PATH. Run tools/setup-dev.ps1 (it installs a portable Go when needed) or install Go from https://go.dev/dl/ and re-run." "Go нет на PATH. Запустите tools/setup-dev.ps1 (он при необходимости подключит переносимый Go) или установите Go с https://go.dev/dl/ и запустите снова."
fi

GO_REQ=$(sed -n 's/^go \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p' go.mod | head -n 1)
if [ -z "$GO_REQ" ]; then
    fail "Could not parse the 'go' directive from go.mod." "Не удалось разобрать директиву 'go' в go.mod."
fi
REQ_MAJOR=${GO_REQ% *}
REQ_MINOR=${GO_REQ#* }
GO_INST=$(go version | sed -n 's/^go version go\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p')
if [ -z "$GO_INST" ]; then
    fail "Could not parse the output of 'go version'." "Не удалось разобрать вывод 'go version'."
fi
INST_MAJOR=${GO_INST% *}
INST_MINOR=${GO_INST#* }
if ! awk -v im="$INST_MAJOR" -v in_="$INST_MINOR" -v rm="$REQ_MAJOR" -v rn="$REQ_MINOR" \
        'BEGIN { exit !(im > rm || (im == rm && in_ >= rn)) }'; then
    fail "Go $(go version | cut -d' ' -f3) is older than go.mod requires ($REQ_MAJOR.$REQ_MINOR). Update Go or let tools/setup-dev.ps1 install a portable one." "Go $(go version | cut -d' ' -f3) старше требуемого в go.mod ($REQ_MAJOR.$REQ_MINOR). Обновите Go или позвольте tools/setup-dev.ps1 подключить переносимый."
fi
say "go: $(go version) - OK for go.mod" "go: $(go version) - подходит для go.mod"
say '' ''

# ---------------------------------------------------------------- existing build gate

# EN: 4. The build itself: the existing script, no duplicated logic.
# RU: 4. Сама сборка: существующий скрипт, без дублирования логики.
exec bash ./build-windows.sh --check
