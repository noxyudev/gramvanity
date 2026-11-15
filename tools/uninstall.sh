#!/bin/sh
# gramvanity - user uninstaller for Linux.
#
# EN: Removes a gramvanity installation from the folder it was installed into. Program files
# EN: (opencl/, verify-go/, docs/, tools/, README, CHANGELOG, LICENSE, NOTICE) are deleted; user
# EN: data - results/, checkpoints, hit and stop files - is kept, unless --full is given and
# EN: confirmed by typing 'yes'. The script touches nothing outside the installation folder: it
# EN: never changes the system PATH or anything else, and it needs no network.
# RU: Удаляет установку gramvanity из папки, в которую она была установлена. Программные файлы
# RU: (opencl/, verify-go/, docs/, tools/, README, CHANGELOG, LICENSE, NOTICE) удаляются; данные
# RU: пользователя - results/, checkpoints, hit- и stop-файлы - сохраняются, если не задан
# RU: --full с подтверждением вводом 'yes'. Скрипт не трогает ничего за пределами папки
# RU: установки: не меняет системный PATH и вообще ничего, и не требует сети.
#
# Usage / Использование:
#   tools/uninstall.sh [--dir PATH] [--full] [--yes] [--dry-run] [--lang en|ru] [--help]
#
# EN: By default the script uninstalls the installation it lives in (the parent of tools/);
# EN: --dir PATH points at another installation. --yes skips all prompts, including the typed
# EN: 'yes' for --full. --dry-run prints the plan without deleting anything.
# RU: По умолчанию скрипт удаляет ту установку, в которой лежит (родитель tools/); --dir ПУТЬ
# RU: указывает другую установку. --yes пропускает все вопросы, включая ввод 'yes' для --full.
# RU: --dry-run печатает план, ничего не удаляя.

set -u

LANG_CODE="${GRAMVANITY_LANG:-en}"

# EN: message helpers, same style as install-user.sh.
# RU: помощники сообщений в стиле install-user.sh.
msg() { if [ "$LANG_CODE" = "ru" ]; then printf '%s' "${2-}"; else printf '%s' "$1"; fi }
say() { msg "$1" "${2-}"; printf '\n'; }
warn() { printf '%s\n' "$(msg "$1" "${2-}")" >&2; }
fail() { printf '\nERROR: %s\n' "$(msg "$1" "${2-}")" >&2; exit 1; }

usage() {
    if [ "$LANG_CODE" = "ru" ]; then
        cat <<'EOF'
Использование:
  tools/uninstall.sh [--dir ПУТЬ] [--full] [--yes] [--dry-run] [--lang en|ru] [--help]

  --dir ПУТЬ   папка установки (по умолчанию - та, в которой лежит скрипт)
  --full       удалить и данные пользователя (results/, checkpoints, hit-файлы);
               требует ввода 'yes', если не задан --yes
  --yes        пропустить все вопросы
  --dry-run    показать план действий, ничего не удаляя
  --lang en|ru язык сообщений (по умолчанию: GRAMVANITY_LANG, затем en)
  --help       показать эту справку

По умолчанию удаляются только программные файлы; results/, checkpoints и hit-файлы
сохраняются. Скрипт не трогает ничего за пределами папки установки.
EOF
    else
        cat <<'EOF'
Usage:
  tools/uninstall.sh [--dir PATH] [--full] [--yes] [--dry-run] [--lang en|ru] [--help]

  --dir PATH   the installation folder (default: the one this script lives in)
  --full       also delete the user data (results/, checkpoints, hit files);
               requires typing 'yes' unless --yes is given
  --yes        skip all prompts
  --dry-run    print the plan without deleting anything
  --lang en|ru message language (default: GRAMVANITY_LANG, then en)
  --help       show this help

By default only the program files are removed; results/, checkpoints and hit files are kept.
The script touches nothing outside the installation folder.
EOF
    fi
}

# ---------------------------------------------------------------- parse arguments

INSTALL_DIR=''
FULL=0
YES=0
DRY_RUN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --dir)
            [ $# -ge 2 ] || fail "Option --dir requires a value (a folder path)." \
                                 "Опция --dir требует значение (путь к папке)."
            INSTALL_DIR="$2"; shift ;;
        --dir=*) INSTALL_DIR="${1#*=}" ;;
        --full) FULL=1 ;;
        --yes) YES=1 ;;
        --dry-run) DRY_RUN=1 ;;
        --lang)
            [ $# -ge 2 ] || fail "Option --lang requires a value (en or ru)." \
                                 "Опция --lang требует значение (en или ru)."
            LANG_CODE="$2"; shift ;;
        --lang=*) LANG_CODE="${1#*=}" ;;
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

# EN: 1. OS: this uninstaller is for Linux; on Windows use tools/uninstall.ps1.
# RU: 1. ОС: этот деинсталлятор для Linux; в Windows используйте tools/uninstall.ps1.
case "$(uname -s)" in
    Linux) ;;
    *) fail "This uninstaller is for Linux; this shell reports $(uname -s). On Windows use tools/uninstall.ps1." \
           "Этот деинсталлятор - для Linux; оболочка сообщает $(uname -s). В Windows используйте tools/uninstall.ps1." ;;
esac

# ---------------------------------------------------------------- target

# EN: 2. Which installation to remove: --dir PATH, or the one this script lives in (the script
# EN:    sits in tools/, so its own root is the parent of tools/).
# RU: 2. Какую установку удалять: --dir ПУТЬ, либо та, в которой лежит сам скрипт (для скрипта
# RU:    это родитель tools/).
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || SCRIPT_DIR=''
if [ -n "$SCRIPT_DIR" ] && [ "$(basename "$SCRIPT_DIR")" = "tools" ]; then
    OWN_ROOT=$(dirname "$SCRIPT_DIR")
else
    OWN_ROOT="$SCRIPT_DIR"
fi

# EN: Expands a user-entered folder: strips surrounding whitespace and one pair of quotes,
# EN: expands a leading ~ into $HOME, refuses the root folder and the home directory itself.
# EN: Relative paths stay relative to the current directory.
# RU: Разворачивает введённую пользователем папку: снимает окружающие пробелы и одну пару
# RU: кавычек, раскрывает ведущий ~ в $HOME, запрещает корневую папку и сам домашний каталог.
# RU: Относительные пути остаются относительными текущему каталогу.
resolve_install_dir() {
    p=$(printf '%s' "$1" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
    case "$p" in
        '') fail "The folder is empty. Enter a folder path, for example /YOUR/PATH/TO/INSTALL." \
                "Папка пуста. Введите путь к папке, например /YOUR/PATH/TO/INSTALL." ;;
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
        /) fail "Refusing to touch the root folder (/). Choose a subfolder." \
               "Нельзя трогать корневую папку (/). Выберите вложенную папку." ;;
        "$HOME") fail "Refusing to touch the home directory itself ($HOME). Choose a subfolder, for example $HOME/gramvanity." \
                      "Нельзя трогать сам домашний каталог ($HOME). Выберите вложенную папку, например $HOME/gramvanity." ;;
    esac
    printf '%s' "$p"
}

if [ -n "$INSTALL_DIR" ]; then
    TARGET=$(resolve_install_dir "$INSTALL_DIR")
elif [ -n "$OWN_ROOT" ]; then
    TARGET="$OWN_ROOT"
else
    fail "Cannot detect the installation folder (run this script from the installation folder) or pass --dir PATH." \
         "Не удалось определить папку установки (запустите скрипт из папки установки) или передайте --dir ПУТЬ."
fi

# EN: 3. Marker check: the folder is a gramvanity installation only if BOTH program files are
# EN:    there. Anything else is refused - a typo must not delete an arbitrary folder.
# RU: 3. Проверка маркеров: папка считается установкой gramvanity, только если на месте ОБА
# RU:    программных файла. Всё остальное отклоняется - опечатка не должна удалять чужую папку.
[ -f "$TARGET/opencl/gramvanity" ] || fail "'$TARGET' does not look like a gramvanity installation (opencl/gramvanity is missing). Refusing to delete anything; check the path and retry." \
                                          "'$TARGET' не похож на установку gramvanity (opencl/gramvanity отсутствует). Ничего не удаляю; проверьте путь и повторите."
[ -f "$TARGET/verify-go/w5addr" ] || fail "'$TARGET' does not look like a gramvanity installation (verify-go/w5addr is missing). Refusing to delete anything; check the path and retry." \
                                         "'$TARGET' не похож на установку gramvanity (verify-go/w5addr отсутствует). Ничего не удаляю; проверьте путь и повторите."

PROGRAM_ITEMS="opencl verify-go docs tools README.md CHANGELOG.md LICENSE NOTICE"

# ---------------------------------------------------------------- plan and confirmation

# EN: 4. The plan. Default mode keeps user data: results/, checkpoints, hit and stop files and
# EN:    anything else that is not a program file.
# RU: 4. План действий. По умолчанию данные пользователя сохраняются: results/, checkpoints,
# RU:    hit- и stop-файлы и всё, что не является программным файлом.
say ''
say "Uninstall plan for $TARGET" "План удаления для $TARGET"
say '  delete:  program files (opencl/, verify-go/, docs/, tools/, README.md, CHANGELOG.md, LICENSE, NOTICE)' \
    '  удалить: программные файлы (opencl/, verify-go/, docs/, tools/, README.md, CHANGELOG.md, LICENSE, NOTICE)'
if [ "$FULL" -eq 1 ]; then
    say '  delete:  EVERYTHING, including results/ - seed phrases, checkpoints and hit files are lost forever' \
        '  удалить: ВСЁ, включая results/ - seed-фразы, checkpoints и hit-файлы будут потеряны навсегда'
else
    say '  keep:    user data (results/ and anything else)' \
        '  оставить: данные пользователя (results/ и всё остальное)'
fi

if [ "$DRY_RUN" -eq 1 ]; then
    say ''
    say 'DRY RUN - nothing was deleted.' 'DRY RUN - ничего не удалено.'
    exit 0
fi

if [ "$YES" -ne 1 ]; then
    printf '%s' "$(msg 'Uninstall? (y/n) ' 'Удалить? (y/n) ')"
    if [ -t 0 ]; then
        read -r ANS || ANS=''
    elif [ -r /dev/tty ]; then
        read -r ANS < /dev/tty || ANS=''
    else
        fail "No terminal is available to confirm the deletion; pass --yes." \
             "Нет терминала, чтобы подтвердить удаление; передайте --yes."
    fi
    case "$ANS" in
        y|Y|yes|Yes|д|Д|да|Да) ;;
        *) say 'Cancelled - nothing was deleted.' 'Отменено - ничего не удалено.'; exit 0 ;;
    esac
    if [ "$FULL" -eq 1 ]; then
        # EN: the extra typed confirmation exists so that wiping the seed phrases is never an
        # EN: accident of one keystroke.
        # RU: дополнительный ввод 'yes' нужен, чтобы удаление seed-фраз не случилось от одного
        # RU: случайного нажатия клавиши.
        printf '%s' "$(msg "Type 'yes' to also delete results/ and the seed phrases: " "Введите 'yes', чтобы удалить и results/ вместе с seed-фразами: ")"
        if [ -t 0 ]; then
            read -r ANS2 || ANS2=''
        else
            read -r ANS2 < /dev/tty || ANS2=''
        fi
        case "$ANS2" in
            yes|Yes|да|Да) ;;
            *) say 'Cancelled - nothing was deleted.' 'Отменено - ничего не удалено.'; exit 0 ;;
        esac
    fi
fi

# ---------------------------------------------------------------- deletion

# EN: 5. Delete the program files, then the rest depending on the mode. On Linux a running
# EN:    binary deletes fine; no special handling is needed.
# RU: 5. Удаляем программные файлы, затем остальное в зависимости от режима. Работающий
# RU:    бинарник в Linux удаляется без проблем; особая обработка не нужна.
for item in $PROGRAM_ITEMS; do
    [ -e "$TARGET/$item" ] || continue
    rm -rf "$TARGET/$item" || fail "Could not delete '$TARGET/$item'." "Не удалось удалить '$TARGET/$item'."
done
if [ "$FULL" -eq 1 ]; then
    [ -e "$TARGET" ] && rm -rf "$TARGET" || fail "Could not delete '$TARGET'." "Не удалось удалить '$TARGET'."
else
    # EN: remove the folder itself only if nothing but empty subfolders is left.
    # RU: саму папку удаляем, только если кроме пустых подкаталогов ничего не осталось.
    rmdir "$TARGET" 2>/dev/null || true
fi

# ---------------------------------------------------------------- summary

say ''
say '=== Uninstall summary ===' '=== Итог удаления ==='
if [ "$FULL" -eq 1 ]; then
    say "folder:   $TARGET - fully removed" "папка:    $TARGET - удалена полностью"
elif [ -d "$TARGET" ] && [ -d "$TARGET/results" ]; then
    say "folder:   $TARGET - program files removed, results/ kept" "папка:    $TARGET - программные файлы удалены, results/ сохранён"
elif [ -d "$TARGET" ]; then
    say "folder:   $TARGET - program files removed" "папка:    $TARGET - программные файлы удалены"
else
    say "folder:   $TARGET - removed" "папка:    $TARGET - удалена"
fi

if [ "$FULL" -ne 1 ] && [ -d "$TARGET/results" ]; then
    say ''
    warn "results/ with the seed phrases is still in $TARGET. Delete it yourself when it is no longer needed." \
         "results/ с seed-фразами остался в $TARGET. Удалите его сами, когда он больше не нужен."
fi

exit 0
