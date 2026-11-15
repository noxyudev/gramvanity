#requires -Version 5.1
<#
 gramvanity - user uninstaller for Windows.

 EN: Removes a gramvanity installation from the folder it was installed into. Program files
 EN: (opencl\, verify-go\, docs\, tools\, README, CHANGELOG, LICENSE, NOTICE) are deleted; user
 EN: data - results\, checkpoints, hit and stop files - is kept, unless -Full is given and
 EN: confirmed by typing 'yes'. The script touches nothing outside the installation folder: it
 EN: never changes the system PATH, the registry or anything else, and it needs no network.
 RU: Удаляет установку gramvanity из папки, в которую она была установлена. Программные файлы
 RU: (opencl\, verify-go\, docs\, tools\, README, CHANGELOG, LICENSE, NOTICE) удаляются; данные
 RU: пользователя - results\, checkpoints, hit- и stop-файлы - сохраняются, если не задан -Full
 RU: с подтверждением вводом 'yes'. Скрипт не трогает ничего за пределами папки установки: не
 RU: меняет системный PATH, реестр и вообще ничего, и не требует сети.

 Usage / Использование:
   powershell -ExecutionPolicy Bypass -File tools\uninstall.ps1 [-InstallDir <path>] [-Full] [-Yes] [-DryRun] [-Lang en|ru]

 EN: By default the script uninstalls the installation it lives in (the parent of tools\);
 EN: -InstallDir <path> points at another installation. -Yes skips all prompts, including the
 EN: typed 'yes' for -Full. -DryRun prints the plan without deleting anything.
 RU: По умолчанию скрипт удаляет ту установку, в которой лежит (родитель tools\);
 RU: -InstallDir <путь> указывает другую установку. -Yes пропускает все вопросы, включая ввод
 RU: 'yes' для -Full. -DryRun печатает план, ничего не удаляя.
#>
param(
    [string]$InstallDir = "",
    [switch]$Full,
    [switch]$Yes,
    [switch]$DryRun,
    [string]$Lang = ""
)

$ErrorActionPreference = 'Stop'

# EN: message language: -Lang, then GRAMVANITY_LANG, then en (anything else falls back to en).
# RU: язык сообщений: -Lang, затем GRAMVANITY_LANG, затем en (любое другое значение - как en).
if (-not $Lang) { $Lang = $env:GRAMVANITY_LANG }
if ($Lang -ne 'ru') { $Lang = 'en' }

function msg([string]$en, [string]$ru) { if ($Lang -eq 'ru') { $ru } else { $en } }
function say([string]$en, [string]$ru) { Write-Host (msg $en $ru) }
function warn([string]$en, [string]$ru) { Write-Host (msg $en $ru) -ForegroundColor Yellow }
function fail([string]$en, [string]$ru) {
    Write-Host ''
    Write-Host ('ERROR: ' + (msg $en $ru)) -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- checks

# EN: 1. OS. RuntimeInformation works in every shell, including ones (like MSYS2) that do not
# EN:    set OS=Windows_NT.
# RU: 1. ОС. RuntimeInformation работает в любой оболочке, в том числе MSYS2, которая не задаёт
# RU:    OS=Windows_NT.
$IsWin = $false
try {
    $IsWin = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
} catch {
    $IsWin = ($env:OS -eq 'Windows_NT')
}
if (-not $IsWin) {
    fail "This uninstaller is for Windows. On Linux use tools/uninstall.sh." `
         "Этот деинсталлятор - для Windows. В Linux используйте tools/uninstall.sh."
}

# ---------------------------------------------------------------- target

# EN: 2. Which installation to remove: -InstallDir <path>, or the one this script lives in (the
# RU: 2. Какую установку удалять: -InstallDir <путь>, либо та, в которой лежит сам скрипт (для
# EN:    script lives in tools\, so its own root is the parent of tools\).
# RU:    скрипта это родитель tools\).
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$leaf = Split-Path -Leaf $scriptDir
$ownRoot = if ($leaf -eq 'tools') { Split-Path -Parent $scriptDir } else { $scriptDir }

# EN: Expands a user-entered folder into an absolute Windows path (same rules as the installer:
# RU: Разворачивает введённую пользователем папку в абсолютный путь Windows (те же правила, что
# EN: quotes, ~, %VAR%, $env:VAR, relative paths) and refuses drive roots.
# RU: у установщика: кавычки, ~, %VAR%, $env:VAR, относительные пути) и запрещает корень диска.
function Resolve-InstallDir([string]$Raw) {
    $p = $Raw.Trim()
    if ($p.Length -ge 2 -and (($p.StartsWith('"') -and $p.EndsWith('"')) -or ($p.StartsWith("'") -and $p.EndsWith("'")))) {
        $p = $p.Substring(1, $p.Length - 2).Trim()
    }
    if (-not $p) { return '' }
    $p = [Environment]::ExpandEnvironmentVariables($p)
    while ($p -match '\$env:([A-Za-z_][A-Za-z0-9_]*)') {
        $name = $Matches[1]
        $val = [Environment]::GetEnvironmentVariable($name)
        if ([string]::IsNullOrEmpty($val)) {
            fail "Environment variable '$name' is not set; cannot resolve the folder '$Raw'." `
                 "Переменная окружения '$name' не задана; нельзя определить папку '$Raw'."
        }
        $p = $p.Replace('$env:' + $name, $val)
    }
    if ($p -eq '~') {
        $p = $HOME
    } elseif ($p.StartsWith('~\') -or $p.StartsWith('~/')) {
        $p = $HOME + $p.Substring(1)
    }
    try {
        $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($p)
    } catch {
        fail "'$p' is not a valid Windows folder path." `
             "'$p' - некорректный путь к папке Windows."
    }
    if ($full -match '^[A-Za-z]:\\?$' -or $full -eq '\' -or $full -match '^\\\\[^\\]+$') {
        fail "Refusing to touch a drive root ('$full'). Choose a subfolder." `
             "Нельзя трогать корень диска ('$full'). Выберите вложенную папку."
    }
    if ($full.Length -gt 3 -and $full.EndsWith('\')) { $full = $full.TrimEnd('\') }
    return $full
}

if ($InstallDir) {
    $target = Resolve-InstallDir $InstallDir
} elseif ($ownRoot) {
    $target = $ownRoot
} else {
    fail "Cannot detect the installation folder (run this script from the installation folder) or pass -InstallDir <path>." `
         "Не удалось определить папку установки (запустите скрипт из папки установки) или передайте -InstallDir <путь>."
}

# EN: 3. Marker check: the folder is a gramvanity installation only if BOTH program files are
# EN:    there. Anything else is refused - a typo must not delete an arbitrary folder.
# RU: 3. Проверка маркеров: папка считается установкой gramvanity, только если на месте ОБА
# RU:    программных файла. Всё остальное отклоняется - опечатка не должна удалять чужую папку.
$exe = Join-Path $target 'opencl\gramvanity.exe'
$ver = Join-Path $target 'verify-go\w5addr.exe'
if (-not ((Test-Path $exe) -and (Test-Path $ver))) {
    fail "'$target' does not look like a gramvanity installation (opencl\gramvanity.exe or verify-go\w5addr.exe is missing). Refusing to delete anything; check the path and retry." `
         "'$target' не похож на установку gramvanity (opencl\gramvanity.exe или verify-go\w5addr.exe отсутствует). Ничего не удаляю; проверьте путь и повторите."
}

$programItems = @('opencl', 'verify-go', 'docs', 'tools', 'README.md', 'CHANGELOG.md', 'LICENSE', 'NOTICE')
$resultsDir = Join-Path $target 'results'

# ---------------------------------------------------------------- plan and confirmation

# EN: 4. The plan. Default mode keeps user data: results\, checkpoints, hit and stop files and
# EN:    anything else that is not a program file.
# RU: 4. План действий. По умолчанию данные пользователя сохраняются: results\, checkpoints,
# RU:    hit- и stop-файлы и всё, что не является программным файлом.
say ''
say ("Uninstall plan for $target") ("План удаления для $target")
say ('  delete:  program files (opencl\, verify-go\, docs\, tools\, README.md, CHANGELOG.md, LICENSE, NOTICE)') `
     ('  удалить: программные файлы (opencl\, verify-go\, docs\, tools\, README.md, CHANGELOG.md, LICENSE, NOTICE)')
if ($Full) {
    say ('  delete:  EVERYTHING, including results\ - seed phrases, checkpoints and hit files are lost forever') `
         ('  удалить: ВСЁ, включая results\ - seed-фразы, checkpoints и hit-файлы будут потеряны навсегда')
} else {
    say ('  keep:    user data (results\ and anything else)') `
         ('  оставить: данные пользователя (results\ и всё остальное)')
}

if ($DryRun) {
    say ''
    say ('DRY RUN - nothing was deleted.') ('DRY RUN - ничего не удалено.')
    exit 0
}

if (-not $Yes) {
    $answer = Read-Host (msg 'Uninstall? (y/n)' 'Удалить? (y/n)')
    if ($answer -notmatch '^(y|yes|д|да)$') {
        say ('Cancelled - nothing was deleted.') ('Отменено - ничего не удалено.')
        exit 0
    }
    if ($Full) {
        # EN: the extra typed confirmation exists so that wiping the seed phrases is never an
        # EN: accident of one keystroke.
        # RU: дополнительный ввод 'yes' нужен, чтобы удаление seed-фраз не случилось от одного
        # RU: случайного нажатия клавиши.
        $answer2 = Read-Host (msg "Type 'yes' to also delete results\ and the seed phrases" "Введите 'yes', чтобы удалить и results\ вместе с seed-фразами")
        if ($answer2 -ne 'yes' -and $answer2 -ne 'да') {
            say ('Cancelled - nothing was deleted.') ('Отменено - ничего не удалено.')
            exit 0
        }
    }
}

# ---------------------------------------------------------------- deletion

# EN: 5. Delete the program files, then the rest depending on the mode. A running gramvanity
# EN:    locks its .exe on Windows - the error names the remedy.
# RU: 5. Удаляем программные файлы, затем остальное в зависимости от режима. Запущенный
# RU:    gramvanity удерживает свой .exe в Windows - в сообщении об ошибке сказано, что делать.
foreach ($item in $programItems) {
    $p = Join-Path $target $item
    if (Test-Path $p) {
        try {
            Remove-Item -Recurse -Force $p
        } catch {
            fail "Could not delete '$p': $($_.Exception.Message). If gramvanity is running, close it and retry." `
                 "Не удалось удалить '$p': $($_.Exception.Message). Если gramvanity запущен, закройте его и повторите."
        }
    }
}
if ($Full) {
    if (Test-Path $target) {
        try {
            Remove-Item -Recurse -Force $target
        } catch {
            fail "Could not delete '$target': $($_.Exception.Message). If gramvanity is running, close it and retry." `
                 "Не удалось удалить '$target': $($_.Exception.Message). Если gramvanity запущен, закройте его и повторите."
        }
    }
} elseif (Test-Path $target) {
    # EN: remove the folder itself only if nothing but empty subfolders is left.
    # RU: саму папку удаляем, только если кроме пустых подкаталогов ничего не осталось.
    $left = @(Get-ChildItem -Force $target)
    if ($left.Count -eq 0) { Remove-Item -Force $target }
}

# ---------------------------------------------------------------- summary

say ''
say ('=== Uninstall summary ===') ('=== Итог удаления ===')
if ($Full) {
    say ("folder:   $target - fully removed") ("папка:    $target - удалена полностью")
} elseif (Test-Path $target) {
    if (Test-Path $resultsDir) {
        say ("folder:   $target - program files removed, results\ kept") ("папка:    $target - программные файлы удалены, results\ сохранён")
    } else {
        say ("folder:   $target - program files removed") ("папка:    $target - программные файлы удалены")
    }
} else {
    say ("folder:   $target - removed") ("папка:    $target - удалена")
}

if (-not $Full -and (Test-Path $resultsDir)) {
    say ''
    warn "results\ with the seed phrases is still in $target. Delete it yourself when it is no longer needed." `
         "results\ с seed-фразами остался в $target. Удалите его сами, когда он больше не нужен."
}

exit 0
