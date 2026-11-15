#!/bin/sh
# gramvanity - user installer for Linux.
#
# EN: Downloads a ready release archive from GitHub Releases, verifies its SHA-256 and installs it
# EN: into ~/.local/opt/gramvanity. No administrator rights are needed. The script does not change
# EN: the system PATH, does not install a GPU driver, and never touches results/, checkpoints or
# EN: hit files (an update replaces only the program files).
# RU: Скачивает готовый релизный архив с GitHub Releases, проверяет его SHA-256 и устанавливает в
# RU: ~/.local/opt/gramvanity. Права администратора не нужны. Скрипт не меняет системный PATH,
# RU: не устанавливает драйвер GPU и никогда не трогает results/, checkpoints и hit-файлы
# RU: (обновление заменяет только программные файлы).
#
# Usage / Использование:
#   install-user.sh [--install-dir PATH] [--version vX.Y.Z] [--lang en|ru] [--dry-run] [--help]
#
# EN: Quick variant (installs the latest release):
# RU: Быстрый вариант (ставит последний релиз):
#   curl -fsSL https://raw.githubusercontent.com/noxyudev/gramvanity/main/tools/install-user.sh | sh
#
# EN: The installer asks for the installation folder; --install-dir PATH answers from the command
# EN: line and is required for --dry-run without a terminal.
# RU: Установщик запрашивает папку установки; --install-dir ПУТЬ отвечает из командной строки и
# RU: нужен для --dry-run без терминала.
#
# EN: Safer variant: download the script, read it, then run it.
# RU: Более безопасный вариант: скачайте скрипт отдельным файлом, просмотрите его и только
# RU: потом запускайте.

set -u

REPO='noxyudev/gramvanity'
RELEASE_BASE="https://github.com/$REPO/releases/download"
LATEST_PAGE="https://github.com/$REPO/releases/latest"
LATEST_API="https://api.github.com/repos/$REPO/releases/latest"

LANG_CODE="${GRAMVANITY_LANG:-en}"

# EN: message helpers, same style as build-windows.sh.
# RU: помощники сообщений в стиле build-windows.sh.
msg() { if [ "$LANG_CODE" = "ru" ]; then printf '%s' "${2-}"; else printf '%s' "$1"; fi; }
say() { msg "$1" "${2-}"; printf '\n'; }
warn() { printf '%s\n' "$(msg "$1" "${2-}")" >&2; }
fail() { printf '\nERROR: %s\n' "$(msg "$1" "${2-}")" >&2; exit 1; }

usage() {
    if [ "$LANG_CODE" = "ru" ]; then
        cat <<'EOF'
Использование:
  install-user.sh [--install-dir ПУТЬ] [--version vX.Y.Z] [--lang en|ru] [--dry-run] [--help]

  --install-dir ПУТЬ  папка установки (иначе установщик запросит её у вас)
  --version vX.Y.Z    установить конкретный релиз (по умолчанию - последний стабильный)
  --lang en|ru        язык сообщений (по умолчанию: GRAMVANITY_LANG, затем en)
  --dry-run           показать план действий, ничего не скачивая и не устанавливая
  --help              показать эту справку

Папка установки выбирается явно: установщик запрашивает её, молчаливого значения нет.
Права администратора не нужны. Существующий results/, checkpoints и hit-файлы не трогаются.
EOF
    else
        cat <<'EOF'
Usage:
  install-user.sh [--install-dir PATH] [--version vX.Y.Z] [--lang en|ru] [--dry-run] [--help]

  --install-dir PATH  the installation folder (otherwise the installer asks for it)
  --version vX.Y.Z    install a specific release (default: the latest stable one)
  --lang en|ru        message language (default: GRAMVANITY_LANG, then en)
  --dry-run           print the plan without downloading or installing anything
  --help              show this help

The install folder is chosen explicitly: the installer asks for it, there is no silent default.
No administrator rights are needed. An existing results/ folder, checkpoints and hit files are
never touched.
EOF
    fi
}

# ---------------------------------------------------------------- parse arguments

VERSION_TAG=''
INSTALL_DIR=''
while [ $# -gt 0 ]; do
    case "$1" in
        --install-dir)
            [ $# -ge 2 ] || fail "Option --install-dir requires a value (a folder path)." \
                                 "Опция --install-dir требует значение (путь к папке)."
            INSTALL_DIR="$2"; shift ;;
        --install-dir=*) INSTALL_DIR="${1#*=}" ;;
        --version)
            [ $# -ge 2 ] || fail "Option --version requires a value (vX.Y.Z)." "Опция --version требует значение (vX.Y.Z)."
            VERSION_TAG="$2"; shift ;;
        --version=*) VERSION_TAG="${1#*=}" ;;
        --lang)
            [ $# -ge 2 ] || fail "Option --lang requires a value (en or ru)." "Опция --lang требует значение (en или ru)."
            LANG_CODE="$2"; shift ;;
        --lang=*) LANG_CODE="${1#*=}" ;;
        --dry-run) DRY_RUN=1 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown argument: $1 (see --help)" "Неизвестный аргумент: $1 (см. --help)" ;;
    esac
    shift
done

case "$LANG_CODE" in
    en|ru) ;;
    *) LANG_CODE='en' ;;
esac

# ---------------------------------------------------------------- checks

# EN: 1. OS and architecture. Only x86_64 archives are published on GitHub Releases for now;
# RU: 1. ОС и архитектура. На GitHub Releases публикуются только архивы x86_64; arm64 ещё
# EN:    arm64 has no archive yet, so it gets an explicit message instead of a broken install.
# RU:    нет, поэтому вместо сломанной установки - понятное сообщение.
case "$(uname -s)" in
    Linux) ;;
    *) fail "This installer is for Linux; this shell reports $(uname -s). On Windows use tools/install-user.ps1." \
            "Этот установщик - для Linux; оболочка сообщает $(uname -s). В Windows используйте tools/install-user.ps1." ;;
esac
case "$(uname -m)" in
    x86_64) ;;
    aarch64|arm64)
        fail "Linux arm64 is supported by the program, but no arm64 release archive is published yet. Build from source (see the README) or use an x86_64 machine." \
             "Linux arm64 программой поддерживается, но архив релиза для arm64 пока не публикуется. Соберите из исходников (см. README) или используйте машину x86_64." ;;
    *) fail "Unsupported architecture: $(uname -m). The release archives are built for x86_64." \
           "Неподдерживаемая архитектура: $(uname -m). Архивы релизов собираются для x86_64." ;;
esac

# EN: a downloader: curl (preferred, matches the documented command) or wget.
# RU: загрузчик: curl (предпочтительно, совпадает с документированной командой) или wget.
fetch() {
    # fetch URL DEST
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1"
    else
        fail "Neither curl nor wget is installed; one of them is needed to download the release." \
             "Не установлен ни curl, ни wget; один из них нужен для скачивания релиза."
    fi
}

# EN: Expands a user-entered install folder: strips surrounding whitespace and one pair of
# EN: quotes, expands a leading ~ into $HOME, and refuses the root folder and the home directory
# EN: itself (the release layout opencl/, verify-go/, ... must never be poured into them).
# EN: Relative paths stay relative to the current directory.
# RU: Разворачивает введённую пользователем папку установки: снимает окружающие пробелы и одну
# RU: пару кавычек, раскрывает ведущий ~ в $HOME и запрещает корневую папку и сам домашний
# RU: каталог (раскладка релиза opencl/, verify-go/, ... никогда не должна высыпаться в них).
# RU: Относительные пути остаются относительными текущему каталогу.
resolve_install_dir() {
    p=$(printf '%s' "$1" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
    case "$p" in
        '') fail "The install folder is empty. Enter a folder path, for example /YOUR/PATH/TO/INSTALL." \
                "Папка установки пуста. Введите путь к папке, например /YOUR/PATH/TO/INSTALL." ;;
    esac
    case "$p" in
        '"'*'"') p=${p#\"}; p=${p%\"} ;;
        "'"*"'") p=${p#\'}; p=${p%\'} ;;
    esac
    case "$p" in
        "~") p="$HOME" ;;
        "~"/*) p="$HOME${p#"~"}" ;;
    esac
    case "$p" in
        /) fail "Refusing to install into the root folder (/). Choose a subfolder, for example /YOUR/PATH/TO/INSTALL." \
               "Нельзя устанавливать в корневую папку (/). Выберите вложенную папку, например /YOUR/PATH/TO/INSTALL." ;;
        "$HOME") fail "Refusing to install into the home directory itself ($HOME). Choose a subfolder, for example $HOME/gramvanity." \
                      "Нельзя устанавливать в сам домашний каталог ($HOME). Выберите вложенную папку, например $HOME/gramvanity." ;;
    esac
    printf '%s' "$p"
}

# EN: Asks for the installation folder. The prompt goes to the terminal and the answer is read
# EN: from the terminal, not from stdin: under 'curl ... | sh' stdin is the script itself, so
# EN: reading it would swallow the rest of the script. Without a terminal the caller must use
# EN: --install-dir.
# RU: Запрашивает папку установки. Подсказка и ввод идут с терминала, а не из stdin: при
# RU: 'curl ... | sh' stdin занят самим скриптом, и чтение из него проглотило бы остаток
# RU: скрипта. Без терминала нужно использовать --install-dir.
ask_folder() {
    say '' ''
    say "Where should gramvanity be installed? Enter the folder." \
        "Куда установить gramvanity? Введите папку."
    say '  An absolute or relative path works; a leading ~ is expanded; spaces are fine.' \
        '  Подойдёт абсолютный или относительный путь; ведущий ~ раскрывается в домашний каталог; пробелы допустимы.'
    say '  Examples: /YOUR/PATH/TO/INSTALL   ~/gramvanity   /opt/gramvanity' \
        '  Примеры: /YOUR/PATH/TO/INSTALL   ~/gramvanity   /opt/gramvanity'
    if [ -t 0 ]; then
        printf '%s' "$(msg 'Install folder (e.g. /YOUR/PATH/TO/INSTALL): ' 'Папка установки (например /YOUR/PATH/TO/INSTALL): ')"
        read -r RAW_DIR || RAW_DIR=''
    elif [ -r /dev/tty ]; then
        printf '%s' "$(msg 'Install folder (e.g. /YOUR/PATH/TO/INSTALL): ' 'Папка установки (например /YOUR/PATH/TO/INSTALL): ')" > /dev/tty
        read -r RAW_DIR < /dev/tty || RAW_DIR=''
    else
        fail "No terminal is available to ask for the install folder; pass --install-dir PATH." \
             "Нет терминала, чтобы запросить папку установки; передайте --install-dir ПУТЬ."
    fi
}

# ---------------------------------------------------------------- install folder

# EN: 2. Where to install: --install-dir PATH, or an interactive prompt. There is no silent
# EN:    default: the destination folder is always chosen explicitly, and nothing is downloaded
# EN:    before it is known. A dry run does not prompt - the plan just says the folder will be
# EN:    asked for.
# RU: 2. Куда устанавливать: --install-dir ПУТЬ или интерактивный запрос. Молчаливого значения по
# RU:    умолчанию больше нет: папка всегда выбирается явно, и до её определения ничего не
# RU:    скачивается. В dry run запроса нет - в плане сказано, что папка будет запрошена.
TARGET=''
RAW_DIR=''
if [ -n "$INSTALL_DIR" ]; then
    TARGET=$(resolve_install_dir "$INSTALL_DIR")
    say "Install folder: $TARGET" "Папка установки: $TARGET"
elif [ "${DRY_RUN:-0}" != "1" ]; then
    ask_folder
    [ -n "$RAW_DIR" ] || fail "No install folder was entered. Run the installer again and enter a folder, or pass --install-dir PATH (for example --install-dir ~/gramvanity)." \
                              "Папка установки не указана. Запустите установщик снова и введите папку, либо передайте --install-dir ПУТЬ (например --install-dir ~/gramvanity)."
    TARGET=$(resolve_install_dir "$RAW_DIR")
    say "Install folder: $TARGET" "Папка установки: $TARGET"
fi

# ---------------------------------------------------------------- version

# EN: 3. Which release to install: --version vX.Y.Z, or the latest stable one. The latest tag is
# RU: 3. Какой релиз ставить: --version vX.Y.Z, иначе последний стабильный. Последний тег берём
# EN:    taken from the releases/latest redirect (no API rate limits), with the API as a fallback.
# RU:    из перенаправления releases/latest (без лимитов API), API - запасной вариант.
if [ -n "$VERSION_TAG" ]; then
    case "$VERSION_TAG" in
        v[0-9]*.[0-9]*.[0-9]*) ;;
        [0-9]*.[0-9]*.[0-9]*) VERSION_TAG="v$VERSION_TAG" ;;
        *) fail "Version '$VERSION_TAG' does not look like vX.Y.Z (for example v1.2.0)." \
               "Версия '$VERSION_TAG' не похожа на vX.Y.Z (например v1.2.0)." ;;
    esac
    say "Version: $VERSION_TAG" "Версия: $VERSION_TAG"
else
    say "Resolving the latest release..." "Определяем последний релиз..."
    # EN: The tag is taken from the releases/latest redirect (curl only; no API rate limits);
    # RU: Тег берём из перенаправления releases/latest (только curl; без лимитов API);
    # EN: the GitHub API (curl or wget) is the fallback.
    # RU: запасной путь - GitHub API (curl или wget).
    latest_url=''
    if command -v curl >/dev/null 2>&1; then
        latest_url=$(curl -fsSL -o /dev/null -w '%{url_effective}' "$LATEST_PAGE" 2>/dev/null) || latest_url=''
    fi
    if [ -n "$latest_url" ]; then
        VERSION_TAG=${latest_url##*/tag/}
    else
        if command -v curl >/dev/null 2>&1; then
            VERSION_TAG=$(curl -fsSL "$LATEST_API" 2>/dev/null | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
        elif command -v wget >/dev/null 2>&1; then
            VERSION_TAG=$(wget -qO- "$LATEST_API" 2>/dev/null | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
        fi
    fi
    case "$VERSION_TAG" in
        v[0-9]*.[0-9]*.[0-9]*) ;;
        *) fail "Cannot determine the latest release tag. Check the network and retry, or download the archive manually from https://github.com/$REPO/releases and pass --version vX.Y.Z." \
               "Не удалось определить тег последнего релиза. Проверьте сеть и повторите, либо скачайте архив вручную с https://github.com/$REPO/releases и укажите --version vX.Y.Z." ;;
    esac
    say "Latest release: $VERSION_TAG" "Последний релиз: $VERSION_TAG"
fi

ASSET="gramvanity-$VERSION_TAG-linux-x64"
ARCHIVE_URL="$RELEASE_BASE/$VERSION_TAG/$ASSET.tar.gz"

if [ "${DRY_RUN:-0}" = "1" ]; then
    say '' ''
    say "DRY RUN - nothing was downloaded or installed. The plan:" \
        "DRY RUN - ничего не скачано и не установлено. План действий:"
    say "  1. download  $ARCHIVE_URL (+ .sha256)" "  1. скачать   $ARCHIVE_URL (+ .sha256)"
    say "  2. verify    SHA-256 of the archive" "  2. проверить SHA-256 архива"
    say "  3. unpack    to a temporary folder" "  3. распаковать во временную папку"
    if [ -n "$TARGET" ]; then
        say "  4. install   into $TARGET (existing results/ is kept)" \
            "  4. установить в $TARGET (существующий results/ сохраняется)"
    else
        say "  4. install   into the folder you enter when the installer prompts (existing results/ is kept)" \
            "  4. установить в папку, которую вы введёте по запросу установщика (существующий results/ сохраняется)"
    fi
    say "  5. run       --selftest-det and --list-devices" "  5. запустить --selftest-det и --list-devices"
    exit 0
fi

# ---------------------------------------------------------------- download and verify

TMPDIR_INSTALL=$(mktemp -d) || fail "Could not create a temporary directory." "Не удалось создать временный каталог."
trap 'rm -rf "$TMPDIR_INSTALL"' EXIT INT TERM

# EN: 4. Download the archive and its checksum into a fresh temporary directory.
# RU: 4. Скачиваем архив и его контрольную сумму во свежий временный каталог.
say "Downloading $ASSET.tar.gz ..." "Скачиваем $ASSET.tar.gz ..."
fetch "$ARCHIVE_URL" "$TMPDIR_INSTALL/$ASSET.tar.gz" \
    || fail "Download failed. Check the network and retry; if the release does not exist, check https://github.com/$REPO/releases." \
            "Скачивание не удалось. Проверьте сеть и повторите; если релиза нет, проверьте https://github.com/$REPO/releases."
fetch "$ARCHIVE_URL.sha256" "$TMPDIR_INSTALL/$ASSET.tar.gz.sha256" \
    || fail "Could not download the checksum file." "Не удалось скачать файл контрольной суммы."

# EN: 5. Verify SHA-256 before unpacking.
# RU: 5. Проверяем SHA-256 до распаковки.
EXPECTED=$(awk 'NR==1 {print $1}' "$TMPDIR_INSTALL/$ASSET.tar.gz.sha256")
case "$EXPECTED" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) ;;
    *) fail "The checksum file is malformed; refusing to install." "Файл контрольной суммы повреждён; установка прервана." ;;
esac
ACTUAL=$(sha256sum "$TMPDIR_INSTALL/$ASSET.tar.gz" | awk '{print $1}')
if [ "$ACTUAL" != "$EXPECTED" ]; then
    fail "SHA-256 mismatch: expected $EXPECTED, got $ACTUAL. The download is corrupt - retry; if it repeats, report it in the project channel." \
         "SHA-256 не совпал: ожидалось $EXPECTED, получено $ACTUAL. Скачанный файл повреждён - повторите; если повторится, сообщите в канале проекта."
fi
say "SHA-256 verified." "SHA-256 проверен."

# EN: 6. Unpack into the temporary directory and check the release layout.
# RU: 6. Распаковываем во временную папку и проверяем раскладку релиза.
mkdir -p "$TMPDIR_INSTALL/unpack" || exit 1
tar -xzf "$TMPDIR_INSTALL/$ASSET.tar.gz" -C "$TMPDIR_INSTALL/unpack" \
    || fail "Could not unpack the archive." "Не удалось распаковать архив."
for f in opencl/gramvanity verify-go/w5addr docs/tg-wallet.md tools/uninstall.sh \
         README.md CHANGELOG.md LICENSE NOTICE; do
    [ -s "$TMPDIR_INSTALL/unpack/$f" ] || fail "The archive is incomplete: '$f' is missing or empty." \
                                                  "Архив неполный: '$f' отсутствует или пуст."
done
[ -x "$TMPDIR_INSTALL/unpack/opencl/gramvanity" ] || fail "opencl/gramvanity is not executable." "opencl/gramvanity не является исполняемым."
[ -x "$TMPDIR_INSTALL/unpack/verify-go/w5addr" ] || fail "verify-go/w5addr is not executable." "verify-go/w5addr не является исполняемым."
say "Archive structure verified." "Структура архива проверена."

# EN: 7. Install into the target directory. Only program files are copied; an existing results/
# RU: 7. Устанавливаем в целевой каталог. Копируются только программные файлы; существующий
# EN:    folder is never in a release archive, so user data stays untouched.
# RU:    results/ в релизном архиве отсутствует, поэтому данные пользователя не затрагиваются.
if [ -d "$TARGET" ]; then
    say "Updating existing installation in $TARGET (results/ is kept)." \
        "Обновляем существующую установку в $TARGET (results/ сохраняется)."
else
    say "Installing into $TARGET ..." "Устанавливаем в $TARGET ..."
fi
mkdir -p "$TARGET" || fail "Could not create $TARGET." "Не удалось создать $TARGET."
cp -a "$TMPDIR_INSTALL/unpack/." "$TARGET/" \
    || fail "Installing failed (copy into $TARGET)." "Установка не удалась (копирование в $TARGET)."
say "Program files installed." "Программные файлы установлены."

# ---------------------------------------------------------------- checks after install

EXE="$TARGET/opencl/gramvanity"
SELFTEST_OK=0

# EN: 8a. The deterministic self-test needs no GPU and checks the computation itself. The output
# EN:    is captured so a "binary could not start" failure (missing shared library) can be named.
# RU: 8a. Детерминированный самотест не требует GPU и проверяет сами вычисления. Вывод
# RU:    перехватывается, чтобы сбой "бинарник не смог стартовать" (нет библиотеки) был назван.
say '' ''
say '-- selftest-det --' '-- selftest-det --'
ST_RC=0
ST_OUT=$("$EXE" --selftest-det 2>&1) || ST_RC=$?
if [ -n "$ST_OUT" ]; then
    printf '%s\n' "$ST_OUT"
fi
if [ "$ST_RC" -eq 0 ]; then
    SELFTEST_OK=1
    say 'Self-test: OK' 'Самотест: OK'
else
    warn "SELF-TEST FAILED: do not search on this machine until the cause is clear (see the README)." \
         "САМОТЕСТ ПРОВАЛЕН: не запускайте поиск на этой машине, пока причина не ясна (см. README)."
    if printf '%s\n' "$ST_OUT" | grep -qi 'error while loading shared libraries'; then
        say '  The binary could not start because an OpenCL runtime library is missing; the fix is' \
            '  Бинарник не смог стартовать: отсутствует библиотека OpenCL; как починить -'
        say '  in the list-devices section below.' \
            '  в разделе list-devices ниже.'
    fi
fi

# EN: 8b. List the OpenCL devices; the program exits 0 even without a GPU and prints a note.
# EN: --lang en pins the message language so the readiness check below matches the wording even
# EN:    when the user's environment sets GRAMVANITY_LANG=ru. The exit code is part of the verdict:
# EN:    a binary that cannot even start (missing libOpenCL.so.1) is never "ready".
# RU: 8b. Список устройств OpenCL; программа выходит с кодом 0 даже без GPU и печатает пометку.
# RU: --lang en фиксирует язык сообщений, чтобы проверка готовности ниже совпадала с формулировкой
# RU:    даже если в окружении пользователя задан GRAMVANITY_LANG=ru. Код возврата - часть вердикта:
# RU:    бинарник, который вообще не стартует (нет libOpenCL.so.1), никогда не считается "готовым".
say ''
say '-- list-devices --' '-- list-devices --'
LIST_RC=0
LIST_OUT=$("$EXE" --list-devices --lang en 2>&1) || LIST_RC=$?
if [ -n "$LIST_OUT" ]; then
    printf '%s\n' "$LIST_OUT"
fi
GPU_OK=0
if [ "$LIST_RC" -eq 0 ]; then
    printf '%s\n' "$LIST_OUT" | grep -qi 'no OpenCL GPU found' || GPU_OK=1
fi

if [ "$GPU_OK" -eq 0 ]; then
    # EN: 9. GPU not ready: the driver is the user's job, we only explain what to install.
    # RU: 9. GPU не готов: драйвер ставит пользователь сам, мы лишь объясняем, что установить.
    warn "No OpenCL GPU was found. The program is installed, but the search needs a GPU driver with OpenCL support:" \
         "GPU с OpenCL не найден. Программа установлена, но поиску нужен драйвер GPU с поддержкой OpenCL:"
    if [ "$LIST_RC" -ne 0 ] && printf '%s\n' "$LIST_OUT" | grep -qi 'error while loading shared libraries'; then
        say '  The binary could not even start: the OpenCL ICD loader library is missing (that is the' \
            '  Бинарник вообще не стартует: отсутствует библиотека-загрузчик OpenCL (это и есть'
        say '  "error while loading shared libraries: libOpenCL.so.1" printed above).' \
            '  ошибка "error while loading shared libraries: libOpenCL.so.1" выше).'
        say '  On Ubuntu/Debian: sudo apt install ocl-icd-libopencl1. The GPU ICD itself comes with the vendor driver.' \
            '  В Ubuntu/Debian: sudo apt install ocl-icd-libopencl1. Сам ICD для GPU ставится с драйвером производителя.'
    fi
    say '  NVIDIA: the proprietary driver already includes OpenCL (https://www.nvidia.com/drivers).'
    say '  AMD:    ROCm OpenCL or the Mesa stack (https://rocm.docs.amd.com).'
    say '  Intel:  the intel-opencl-icd package (https://github.com/intel/compute-runtime).'
    if command -v apt-get >/dev/null 2>&1; then
        say ''
        say '  On Ubuntu/Debian the OpenCL loader for CPU/llvmpipe can be installed by hand (this script does not run it):' \
            '  В Ubuntu/Debian загрузчик OpenCL можно поставить вручную (этот скрипт этого не делает):'
        say '    sudo apt install ocl-icd-libopencl1 clinfo'
        say '  The GPU ICD itself comes with the vendor driver, not with apt.' \
            '  Сам ICD для GPU ставится вместе с драйвером производителя, а не через apt.'
    fi
    say ''
    say "  After installing the driver, run: '$EXE' --list-devices" \
        "  После установки драйвера выполните: '$EXE' --list-devices"
fi

# ---------------------------------------------------------------- summary

say '' ''
say '=== Installation summary ===' '=== Итог установки ==='
say "files:      installed into $TARGET" "файлы:      установлены в $TARGET"
if [ "$SELFTEST_OK" -eq 1 ]; then
    say 'self-test:  OK' 'самотест:   OK'
else
    say 'self-test:  FAILED (do not search on this machine)' 'самотест:   ПРОВАЛЕН (не запускайте поиск на этой машине)'
fi
if [ "$GPU_OK" -eq 1 ]; then
    say 'GPU:        ready for the search' 'GPU:        готов к поиску'
else
    say 'GPU:        not ready (see the driver instructions above)' 'GPU:        не готов (см. инструкции по драйверу выше)'
fi
say '' ''
say "To remove the program later, run tools/uninstall.sh in $TARGET (results/ is kept unless --full)." \
    "Чтобы удалить программу позже, запустите tools/uninstall.sh в $TARGET (results/ сохраняется без --full)."
say '' ''
say 'Next step - measure the speed of your GPU:' 'Следующий шаг - измерьте скорость вашего GPU:'
say "  cd '$TARGET/opencl'" "  cd '$TARGET/opencl'"
say '  ./gramvanity --mode bip39-12 --bench 60' '  ./gramvanity --mode bip39-12 --bench 60'
say '' ''
say "The README.md in $TARGET explains the rest." "Остальное описано в README.md в $TARGET."

if [ "$SELFTEST_OK" -eq 1 ]; then exit 0; else exit 1; fi
