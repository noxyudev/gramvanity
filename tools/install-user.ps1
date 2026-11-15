#requires -Version 5.1
<#
 gramvanity - user installer for Windows.

 EN: Downloads a ready release archive from GitHub Releases, verifies its SHA-256 and installs it
 EN: into the folder you choose. No administrator rights are needed. The script does not change
 EN: the system PATH, does not install a GPU driver, and never touches results/, checkpoints or
 EN: hit files (an update replaces only the program files).
 RU: Скачивает готовый релизный архив с GitHub Releases, проверяет его SHA-256 и устанавливает в
 RU: выбранную вами папку. Права администратора не нужны. Скрипт не меняет системный PATH,
 RU: не устанавливает драйвер GPU и никогда не трогает results/, checkpoints и hit-файлы
 RU: (обновление заменяет только программные файлы).

 Usage / Использование:
   powershell -ExecutionPolicy Bypass -File install-user.ps1 [-Version vX.Y.Z] [-InstallDir <path>] [-Lang en|ru] [-DryRun]

 Quick variant (installs the latest release) / Быстрый вариант (ставит последний релиз):
   & ([scriptblock]::Create((irm https://raw.githubusercontent.com/noxyudev/gramvanity/main/tools/install-user.ps1).TrimStart([char]0xFEFF)))

 EN: The script file is stored as UTF-8 with a BOM, and Invoke-RestMethod returns that BOM as a
 EN: character; the parser then rejects the param block - that is exactly the 'irm ... | iex'
 EN: failure. The quick variant strips the BOM and builds a scriptblock, which, unlike
 EN: Invoke-Expression, can also take parameters (-Version, -InstallDir, -Lang, -DryRun). The
 EN: installer asks for the installation folder (an absolute or relative path works; ~, %VAR% and
 EN: $env:VAR are expanded). Pass -InstallDir <path> to answer from the command line; that is
 EN: also required for -DryRun without an interactive console.
 RU: Файл скрипта хранится в UTF-8 с BOM, и Invoke-RestMethod возвращает этот BOM как символ;
 RU: парсер после этого отвергает param-блок - именно поэтому падает 'irm ... | iex'. Быстрый
 RU: вариант снимает BOM и собирает scriptblock, который, в отличие от Invoke-Expression, может
 RU: принимать параметры (-Version, -InstallDir, -Lang, -DryRun). Установщик запросит папку
 RU: установки (подойдёт абсолютный или относительный путь; ~, %VAR% и $env:VAR раскрываются).
 RU: Чтобы ответить из командной строки, передайте -InstallDir <путь>; это же нужно для -DryRun
 RU: без интерактивной консоли.

 Safer variant: download the script, read it, then run it / Более безопасный вариант:
   скачайте скрипт отдельным файлом, просмотрите его и только потом запускайте.
#>
param(
    [string]$Version = "",
    [string]$InstallDir = "",
    [string]$Lang = "",
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$Repo        = 'noxyudev/gramvanity'
$ReleaseBase = "https://github.com/$Repo/releases/download"
$LatestApi   = "https://api.github.com/repos/$Repo/releases/latest"

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

# EN: Expands a user-entered install folder into an absolute Windows path. Accepts absolute and
# EN: relative paths, ~, %VAR% (cmd format) and $env:VAR (PowerShell format), and one pair of
# EN: surrounding quotes. Refuses drive roots: the release layout (opencl/, verify-go/, ...) must
# EN: never be poured into the root of a disk.
# RU: Разворачивает введённую пользователем папку установки в абсолютный путь Windows. Принимает
# RU: абсолютные и относительные пути, ~, %VAR% (формат cmd) и $env:VAR (формат PowerShell), а
# RU: также одну пару окружающих кавычек. Запрещает корень диска: раскладка релиза (opencl/,
# RU: verify-go/, ...) никогда не должна высыпаться в корень диска.
function Resolve-InstallDir([string]$Raw) {
    $p = $Raw.Trim()
    if ($p.Length -ge 2 -and (($p.StartsWith('"') -and $p.EndsWith('"')) -or ($p.StartsWith("'") -and $p.EndsWith("'")))) {
        $p = $p.Substring(1, $p.Length - 2).Trim()
    }
    if (-not $p) { return '' }
    # EN: %VAR% first (cmd format), then $env:VAR (PowerShell format).
    # RU: сначала %VAR% (формат cmd), затем $env:VAR (формат PowerShell).
    $p = [Environment]::ExpandEnvironmentVariables($p)
    while ($p -match '\$env:([A-Za-z_][A-Za-z0-9_]*)') {
        $name = $Matches[1]
        $val = [Environment]::GetEnvironmentVariable($name)
        if ([string]::IsNullOrEmpty($val)) {
            fail "Environment variable '$name' is not set; cannot resolve the install folder '$Raw'." `
                 "Переменная окружения '$name' не задана; нельзя определить папку установки '$Raw'."
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
        fail "Refusing to install into a drive root ('$full'). Choose a subfolder." `
             "Нельзя устанавливать в корень диска ('$full'). Выберите вложенную папку."
    }
    if ($full.Length -gt 3 -and $full.EndsWith('\')) { $full = $full.TrimEnd('\') }
    return $full
}

# EN: TLS 1.2 is not always the default in Windows PowerShell 5.1.
# RU: TLS 1.2 не всегда включён по умолчанию в Windows PowerShell 5.1.
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}

# ---------------------------------------------------------------- checks

# EN: 1. OS and architecture: Windows x86_64. RuntimeInformation works in every shell, including
# RU: 1. ОС и архитектура: Windows x86_64. RuntimeInformation работает в любой оболочке, в том
# EN:    ones (like MSYS2) that do not set OS=Windows_NT or PROCESSOR_ARCHITECTURE.
# RU:    числе MSYS2, которая не задаёт OS=Windows_NT и PROCESSOR_ARCHITECTURE.
$IsWindows = $false
try {
    $IsWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
} catch {
    $IsWindows = ($env:OS -eq 'Windows_NT')
}
if (-not $IsWindows) {
    fail "This installer is for Windows. On Linux use tools/install-user.sh." `
         "Этот установщик - для Windows. В Linux используйте tools/install-user.sh."
}
$OsArch = ''
try {
    $OsArch = [string][System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture
} catch {
    if ($env:PROCESSOR_ARCHITECTURE -eq 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'AMD64') { $OsArch = 'X64' }
}
if ($OsArch -ne 'X64') {
    fail "Windows x86_64 is required; this machine reports '$OsArch'." `
         "Нужна Windows x86_64; на этой машине архитектура '$OsArch'."
}

# ---------------------------------------------------------------- install folder

# EN: 2. Where to install: -InstallDir <path>, or an interactive prompt. There is no silent
# EN:    default: the destination folder is always chosen explicitly, and nothing is downloaded
# EN:    before it is known. A dry run does not prompt - the plan just says the folder will be
# EN:    asked for.
# RU: 2. Куда устанавливать: -InstallDir <путь> или интерактивный запрос. Молчаливого значения по
# RU:    умолчанию больше нет: папка всегда выбирается явно, и до её определения ничего не
# RU:    скачивается. В dry run запроса нет - в плане сказано, что папка будет запрошена.
$target = ''
if ($InstallDir) {
    $target = Resolve-InstallDir $InstallDir
    say ("Install folder: $target") ("Папка установки: $target")
} elseif (-not $DryRun) {
    say ''
    say ("Where should gramvanity be installed? Enter the folder.") `
        ("Куда установить gramvanity? Введите папку.")
    say ('  An absolute or relative path works; ~, %VAR% and $env:VAR are expanded; spaces are fine.') `
        ('  Подойдёт абсолютный или относительный путь; ~, %VAR% и $env:VAR раскрываются; пробелы допустимы.')
    say ('  Examples: C:/YOUR/PATH/TO/INSTALL   ~\gramvanity   $env:LOCALAPPDATA\gramvanity') `
        ('  Примеры: C:/YOUR/PATH/TO/INSTALL   ~\gramvanity   $env:LOCALAPPDATA\gramvanity')
    $raw = Read-Host (msg 'Install folder (e.g. C:/YOUR/PATH/TO/INSTALL)' 'Папка установки (например C:/YOUR/PATH/TO/INSTALL)')
    if (-not ($raw -and $raw.Trim())) {
        fail "No install folder was entered. Run the installer again and enter a folder, or pass -InstallDir <path> (for example -InstallDir ~\gramvanity)." `
             "Папка установки не указана. Запустите установщик снова и введите папку, либо передайте -InstallDir <путь> (например -InstallDir ~\gramvanity)."
    }
    $target = Resolve-InstallDir $raw
    say ("Install folder: $target") ("Папка установки: $target")
}

# ---------------------------------------------------------------- version

# EN: 3. Which release to install: -Version vX.Y.Z, or the latest stable one.
# RU: 3. Какой релиз ставить: -Version vX.Y.Z, иначе последний стабильный.
$tag = ''
if ($Version) {
    if ($Version -notmatch '^[vV]?\d+\.\d+\.\d+$') {
        fail "Version '$Version' does not look like vX.Y.Z (for example v1.2.0)." `
             "Версия '$Version' не похожа на vX.Y.Z (например v1.2.0)."
    }
    $tag = 'v' + ($Version -replace '^[vV]', '')
    say ("Version: $tag") ("Версия: $tag")
} else {
    say ("Resolving the latest release...") ("Определяем последний релиз...")
    try {
        $rel = Invoke-RestMethod -Uri $LatestApi -Headers @{ Accept = 'application/vnd.github+json' } -UseBasicParsing
        $tag = [string]$rel.tag_name
    } catch {
        fail "Cannot reach the GitHub API ($($_.Exception.Message)). Check the network and retry, or download the archive manually from https://github.com/$Repo/releases and unpack it yourself." `
             "GitHub API недоступен ($($_.Exception.Message)). Проверьте сеть и повторите, либо скачайте архив вручную с https://github.com/$Repo/releases и распакуйте сами."
    }
    if (-not ($tag -match '^v\d+\.\d+\.\d+$')) {
        fail "The latest release tag looks wrong ('$tag'); pass -Version vX.Y.Z explicitly." `
             "Тег последнего релиза выглядит некорректно ('$tag'); укажите версию явно через -Version vX.Y.Z."
    }
    say ("Latest release: $tag") ("Последний релиз: $tag")
}

$asset  = "gramvanity-$tag-windows-x64"
$zipUrl = "$ReleaseBase/$tag/$asset.zip"
$shaUrl = "$zipUrl.sha256"

if ($DryRun) {
    say ''
    say ("DRY RUN - nothing was downloaded or installed. The plan:") `
        ("DRY RUN - ничего не скачано и не установлено. План действий:")
    say ("  1. download  $zipUrl (+ .sha256)") ("  1. скачать   $zipUrl (+ .sha256)")
    say ("  2. verify    SHA-256 of the archive") ("  2. проверить SHA-256 архива")
    say ("  3. unpack    to a temporary folder") ("  3. распаковать во временную папку")
    if ($target) {
        say ("  4. install   into $target (existing results/ is kept)") `
            ("  4. установить в $target (существующий results/ сохраняется)")
    } else {
        say ("  4. install   into the folder you enter when the installer prompts (existing results/ is kept)") `
            ("  4. установить в папку, которую вы введёте по запросу установщика (существующий results/ сохраняется)")
    }
    say ("  5. run       --selftest-det and --list-devices") ("  5. запустить --selftest-det и --list-devices")
    exit 0
}

# ---------------------------------------------------------------- download and verify

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('gramvanity-install-' + [Guid]::NewGuid().ToString('N'))
try {
    # EN: 4. Download the archive and its checksum into a fresh temporary directory.
    # RU: 4. Скачиваем архив и его контрольную сумму во свежий временный каталог.
    New-Item -ItemType Directory -Path $tmp | Out-Null
    $zipPath = Join-Path $tmp "$asset.zip"
    $shaPath = "$zipPath.sha256"
    say ("Downloading $($asset).zip ...") ("Скачиваем $asset.zip ...")
    try {
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -UseBasicParsing
        Invoke-WebRequest -Uri $shaUrl -OutFile $shaPath -UseBasicParsing
    } catch {
        $code = $null
        try { $code = [int]$_.Exception.Response.StatusCode } catch {}
        if ($code -eq 404) {
            fail "Release $tag was not found (HTTP 404). Check https://github.com/$Repo/releases and pass -Version vX.Y.Z if needed." `
                 "Релиз $tag не найден (HTTP 404). Проверьте https://github.com/$Repo/releases и при необходимости укажите -Version vX.Y.Z."
        }
        fail "Download failed ($code $($_.Exception.Message)). Check the network and retry." `
             "Не удалось скачать ($code $($_.Exception.Message)). Проверьте сеть и повторите."
    }

    # EN: 5. Verify SHA-256 before unpacking.
    # RU: 5. Проверяем SHA-256 до распаковки.
    $expected = ((Get-Content $shaPath -TotalCount 1) -split '\s+')[0].ToLower()
    if ($expected -notmatch '^[0-9a-f]{64}$') {
        fail "The checksum file is malformed; refusing to install." `
             "Файл контрольной суммы повреждён; установка прервана."
    }
    $actual = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLower()
    if ($actual -ne $expected) {
        fail "SHA-256 mismatch: expected $expected, got $actual. The download is corrupt - retry; if it repeats, report it in the project channel." `
             "SHA-256 не совпал: ожидалось $expected, получено $actual. Скачанный файл повреждён - повторите; если повторится, сообщите в канале проекта."
    }
    say ("SHA-256 verified.") ("SHA-256 проверен.")

    # EN: 6. Unpack into the temporary directory and check the release layout.
    # RU: 6. Распаковываем во временную папку и проверяем раскладку релиза.
    $unpack = Join-Path $tmp 'unpack'
    Expand-Archive -Path $zipPath -DestinationPath $unpack -Force
    foreach ($f in 'opencl/gramvanity.exe', 'verify-go/w5addr.exe', 'docs/tg-wallet.md',
                   'README.md', 'CHANGELOG.md', 'LICENSE', 'NOTICE') {
        $p = Join-Path $unpack ($f -replace '/', '\')
        if (-not (Test-Path $p) -or ((Get-Item $p).Length -eq 0)) {
            fail "The archive is incomplete: '$f' is missing or empty." `
                 "Архив неполный: '$f' отсутствует или пуст."
        }
    }
    say ("Archive structure verified.") ("Структура архива проверена.")

    # EN: 7. Install into the target directory. Only program files are copied; an existing
    # RU: 7. Устанавливаем в целевой каталог. Копируются только программные файлы; существующий
    # EN:    results/ folder is never in a release archive, so user data stays untouched.
    # RU:    results/ в релизном архиве отсутствует, поэтому данные пользователя не затрагиваются.
    $updating = Test-Path $target
    if ($updating) {
        say ("Updating existing installation in $target (results/ is kept).") `
            ("Обновляем существующую установку в $target (results/ сохраняется).")
    } else {
        say ("Installing into $target ...") ("Устанавливаем в $target ...")
    }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    try {
        Copy-Item -Path (Join-Path $unpack '*') -Destination $target -Recurse -Force
    } catch {
        fail "Installing failed: $($_.Exception.Message). If gramvanity is running, close it and retry." `
             "Установка не удалась: $($_.Exception.Message). Если gramvanity запущен, закройте его и повторите."
    }
    say ("Program files installed.") ("Программные файлы установлены.")
} finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------- checks after install

# EN: Helper: run one installed tool, capture stdout/stderr and the exit code. Uses its own
# EN: temporary directory: the download one has already been removed by this point.
# RU: Вспомогательная: запускает установленный инструмент, перехватывает stdout/stderr и код
# RU: возврата. Использует свой временный каталог: каталог загрузки к этому моменту уже удалён.
function Run-Tool([string]$ExePath, [string[]]$ExeArgs) {
    $rundir = Join-Path ([IO.Path]::GetTempPath()) ('gramvanity-run-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $rundir | Out-Null
    try {
        $outF = Join-Path $rundir 'out.txt'
        $errF = Join-Path $rundir 'err.txt'
        try {
            # EN: no -Wait: touch the handle, then WaitForExit - the only reliable way to get a
            # RU: без -Wait: берём хендл, затем WaitForExit - единственный надёжный способ
            # EN: non-null ExitCode in Windows PowerShell 5.1.
            # RU: получить НЕ-null ExitCode в Windows PowerShell 5.1.
            $p = Start-Process -FilePath $ExePath -ArgumentList $ExeArgs -PassThru -NoNewWindow `
                 -RedirectStandardOutput $outF -RedirectStandardError $errF
            $null = $p.Handle
            $p.WaitForExit()
            $code = $p.ExitCode
        } catch {
            return [pscustomobject]@{ ExitCode = -1; Out = ''; Err = ('could not start ' + $ExePath + ': ' + $_.Exception.Message) }
        }
        $out = ''
        $err = ''
        try { $out = Get-Content $outF -Raw -Encoding UTF8 -ErrorAction SilentlyContinue } catch {}
        try { $err = Get-Content $errF -Raw -Encoding UTF8 -ErrorAction SilentlyContinue } catch {}
        if ($null -eq $code) { $code = -1 }
        [pscustomobject]@{ ExitCode = $code; Out = $out; Err = $err }
    } finally {
        Remove-Item -Recurse -Force $rundir -ErrorAction SilentlyContinue
    }
}

$exe = Join-Path $target 'opencl\gramvanity.exe'
$selftestOk = $false

# EN: 8a. The deterministic self-test needs no GPU and checks the computation itself.
# RU: 8a. Детерминированный самотест не требует GPU и проверяет сами вычисления.
say ''
say ('-- selftest-det --') ('-- selftest-det --')
$st = Run-Tool $exe @('--selftest-det')
if ($st.Out) { Write-Host $st.Out.TrimEnd() }
if ($st.Err) { Write-Host $st.Err.TrimEnd() }
if ($st.ExitCode -ne 0) {
    warn "SELF-TEST FAILED (exit code $($st.ExitCode)): do not search on this machine until the cause is clear (see the README)." `
         "САМОТЕСТ ПРОВАЛЕН (код возврата $($st.ExitCode)): не запускайте поиск на этой машине, пока причина не ясна (см. README)."
} else {
    $selftestOk = $true
    say ('Self-test: OK') ('Самотест: OK')
}

# EN: 8b. List the OpenCL devices; the program exits 0 even without a GPU and prints a note.
# RU: 8b. Список устройств OpenCL; программа выходит с кодом 0 даже без GPU и печатает пометку.
# EN: --lang en pins the message language so the readiness check below matches the wording even
# RU: --lang en фиксирует язык сообщений, чтобы проверка готовности ниже совпадала с формулировкой
# EN:    when the user's environment sets GRAMVANITY_LANG=ru.
# RU:    даже если в окружении пользователя задан GRAMVANITY_LANG=ru.
say ''
say ('-- list-devices --') ('-- list-devices --')
$ld = Run-Tool $exe @('--list-devices', '--lang', 'en')
if ($ld.Out) { Write-Host $ld.Out.TrimEnd() }
if ($ld.Err) { Write-Host $ld.Err.TrimEnd() }
$gpuOk = ($ld.ExitCode -eq 0) -and ($ld.Out -notmatch 'no OpenCL GPU found')

if (-not $gpuOk) {
    # EN: 9. GPU not ready: the driver is the user's job, we only explain what to install.
    # RU: 9. GPU не готов: драйвер ставит пользователь сам, мы лишь объясняем, что установить.
    warn "No OpenCL GPU was found. The program is installed, but the search needs a GPU driver with OpenCL support:" `
         "GPU с OpenCL не найден. Программа установлена, но поиску нужен драйвер GPU с поддержкой OpenCL:"
    say '  NVIDIA: install or update the driver from https://www.nvidia.com/drivers (OpenCL is included).'
    say '  AMD:    install the driver from https://www.amd.com/support (Adrenalin includes OpenCL).'
    say '  Intel:  install the graphics driver for your iGPU/dGPU from https://www.intel.com/download-center.'
    say ("  After installing the driver, run: & '$exe' --list-devices") `
        ("  После установки драйвера выполните: & '$exe' --list-devices")
}

# ---------------------------------------------------------------- summary

say ''
say ('=== Installation summary ===') ('=== Итог установки ===')
say ("files:      installed into $target") ("файлы:      установлены в $target")
if ($selftestOk) {
    say ('self-test:  OK') ('самотест:   OK')
} else {
    say ('self-test:  FAILED (do not search on this machine)') ('самотест:   ПРОВАЛЕН (не запускайте поиск на этой машине)')
}
if ($gpuOk) {
    say ('GPU:        ready for the search') ('GPU:        готов к поиску')
} else {
    say ('GPU:        not ready (see the driver instructions above)') ('GPU:        не готов (см. инструкции по драйверу выше)')
}
say ''
say ("Next step - measure the speed of your GPU:") ("Следующий шаг - измерьте скорость вашего GPU:")
say ("  cd '$(Join-Path $target 'opencl')'") ("  cd '$(Join-Path $target 'opencl')'")
say ("  .\gramvanity.exe --mode bip39-12 --bench 60") ("  .\gramvanity.exe --mode bip39-12 --bench 60")
say ''
say ("The README.md in $target explains the rest.") ("Остальное описано в README.md в $target.")

if ($selftestOk) { exit 0 } else { exit 1 }
