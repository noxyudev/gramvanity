#requires -Version 5.1
<#
 gramvanity - developer environment setup, stage 1 (Windows).

 EN: Prepares a Windows machine for building gramvanity after the repository has been cloned:
 EN: checks the existing tools, installs ONLY what is missing (winget when available; otherwise a
 EN: portable copy into %LOCALAPPDATA%\gramvanity-dev), and then runs stage 2
 EN: (tools/setup-dev-msys2.sh) inside MSYS2 UCRT64, which installs the four pacman packages and
 EN: calls the existing build gate (build-windows.sh --check).
 RU: Готовит машину с Windows к сборке gramvanity после клонирования репозитория: проверяет
 RU: установленные инструменты, ставит ТОЛЬКО недостающее (через winget, а при его отсутствии -
 RU: переносимые копии в %LOCALAPPDATA%\gramvanity-dev), затем запускает этап 2
 RU: (tools/setup-dev-msys2.sh) внутри MSYS2 UCRT64: установка четырёх pacman-пакетов и
 RU: существующая точка сборки (build-windows.sh --check).

 EN: Coexistence policy: existing programs are never reinstalled or upgraded. In particular an
 EN: existing Go is never touched; when it is missing or too old, a portable Go is unpacked into
 EN: the tools folder and put on the SESSION PATH only (the global PATH is never modified).
 RU: Политика сосуществования: установленные программы не переустанавливаются и не обновляются.
 RU: В частности, существующий Go не трогается: если его нет или он слишком стар, переносимый Go
 RU: распаковывается в папку инструментов и подключается ТОЛЬКО через сессионный PATH
 RU: (глобальный PATH не изменяется).

 Usage / Использование:
   powershell -ExecutionPolicy Bypass -File tools\setup-dev.ps1 [-Lang en|ru] [-DryRun] [-Msys2Root C:\msys64]
#>
param(
    [string]$Lang = "",
    [switch]$DryRun,
    [string]$Msys2Root = ""
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$DevRoot = Join-Path $env:LOCALAPPDATA 'gramvanity-dev'
$ToolsDir = Join-Path $DevRoot 'tools'
$Manifest = Join-Path $DevRoot '.dev-setup-manifest'

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

try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}

# EN: Run an external tool and capture stdout/stderr and the exit code. Start-Process is used
# EN: instead of the call operator so the script behaves the same in every hosting environment.
# RU: Запуск внешнего инструмента с перехватом stdout/stderr и кода возврата. Start-Process
# RU: вместо оператора вызова - чтобы скрипт вёл себя одинаково в любом окружении запуска.
function Run-Tool([string]$ExePath, [string[]]$ExeArgs, [string]$WorkingDir = $null) {
    $rundir = Join-Path ([IO.Path]::GetTempPath()) ('gramvanity-dev-run-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $rundir | Out-Null
    try {
        $outF = Join-Path $rundir 'out.txt'
        $errF = Join-Path $rundir 'err.txt'
        $psi = @{ FilePath = $ExePath; ArgumentList = $ExeArgs; PassThru = $true;
                  NoNewWindow = $true; RedirectStandardOutput = $outF; RedirectStandardError = $errF }
        if ($WorkingDir) { $psi.WorkingDirectory = $WorkingDir }
        try {
            # EN: no -Wait: touch the handle, then WaitForExit - the only reliable way to get
            # RU: без -Wait: берём хендл, затем WaitForExit - единственный надёжный способ
            # EN: a non-null ExitCode in Windows PowerShell 5.1.
            # RU: получить НЕ-null ExitCode в Windows PowerShell 5.1.
            $p = Start-Process @psi
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

# EN: Find an executable on PATH (Get-Command) or fall back to well-known locations.
# RU: Найти исполняемый файл на PATH (Get-Command) или в известных расположениях.
function Find-Tool([string]$Name, [string[]]$FallbackPaths = @()) {
    $c = Get-Command $Name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in $FallbackPaths) {
        if (Test-Path $p) { return $p }
    }
    return ''
}

function Add-ManifestEntry([string]$Line) {
    New-Item -ItemType Directory -Force -Path $DevRoot | Out-Null
    Add-Content -Path $Manifest -Value ("{0} {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm'), $Line)
}

# ---------------------------------------------------------------- checks

# EN: 1. OS and architecture, and that this really is a cloned repository checkout.
# RU: 1. ОС и архитектура, а также что это действительно склонированный репозиторий.
$IsWindows = $false
try {
    $IsWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
} catch {
    $IsWindows = ($env:OS -eq 'Windows_NT')
}
if (-not $IsWindows) {
    fail "This setup is for Windows. On Linux use tools/setup-dev-linux.sh." `
         "Этот setup - для Windows. В Linux используйте tools/setup-dev-linux.sh."
}
$OsArch = ''
try { $OsArch = [string][System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture } catch {}
if ($OsArch -ne 'X64') {
    fail "Windows x86_64 is required; this machine reports '$OsArch'." `
         "Нужна Windows x86_64; на этой машине архитектура '$OsArch'."
}
if (-not (Test-Path (Join-Path $RepoRoot 'go.mod')) -or -not (Test-Path (Join-Path $RepoRoot 'build-windows.sh'))) {
    fail "This does not look like a gramvanity checkout (go.mod / build-windows.sh not found next to tools\). Clone the repository first; the setup never clones it for you." `
         "Это не похоже на копию gramvanity (рядом с tools\ нет go.mod / build-windows.sh). Сначала склонируйте репозиторий; setup не делает клонирование сам."
}

# EN: the required Go version comes from go.mod (e.g. "go 1.26.0" -> 1.26).
# RU: требуемая версия Go берётся из go.mod ("go 1.26.0" -> 1.26).
$GoModRequired = ''
foreach ($line in Get-Content (Join-Path $RepoRoot 'go.mod')) {
    if ($line -match '^go\s+(\d+)\.(\d+)(\.(\d+))?') {
        $GoModRequired = "$($Matches[1]).$($Matches[2])"
        if ($Matches[4]) { $GoModPatch = $Matches[4] } else { $GoModPatch = '0' }
        break
    }
}
if (-not $GoModRequired) {
    fail "Could not parse the 'go' directive from go.mod." "Не удалось разобрать директиву 'go' в go.mod."
}

say ("=== gramvanity developer setup (Windows) ===") ("=== Настройка окружения разработчика gramvanity (Windows) ===")
say ("Repository: $RepoRoot") ("Репозиторий: $RepoRoot")
say ("Tools folder: $ToolsDir") ("Папка инструментов: $ToolsDir")
say ("Required Go (from go.mod): $GoModRequired") ("Требуемый Go (из go.mod): $GoModRequired")
say ''

# ---------------------------------------------------------------- git

# EN: 2. Git: detect only. The repository had to be cloned, so git must already exist; the
# RU: 2. Git: только проверка. Репозиторий должен был быть склонирован, поэтому git уже есть;
# EN:    policy is to never install or upgrade it from here.
# RU:    политика - никогда не устанавливать и не обновлять его отсюда.
$gitPath = Find-Tool 'git.exe'
if (-not $gitPath) {
    fail "git is not available. This setup does not install Git: clone the repository with git first (https://git-scm.com), then re-run." `
         "git недоступен. Этот setup не устанавливает Git: сначала склонируйте репозиторий через git (https://git-scm.com), затем запустите снова."
}
say ("git: found ($gitPath)") ("git: найден ($gitPath)")

# ---------------------------------------------------------------- go

# EN: 3. Go: use the existing one if it satisfies go.mod; otherwise unpack a portable Go from the
# RU: 3. Go: используем существующий, если он удовлетворяет go.mod; иначе распаковываем
# EN:    official zip into the tools folder and prepend it to the SESSION PATH. An existing Go is
# EN:    never upgraded or removed (an MSI/winget upgrade would break other projects).
# RU:    переносимый Go из официального zip в папку инструментов и добавляем его в начало
# RU:    СЕССИОННОГО PATH. Существующий Go никогда не обновляется и не удаляется
# RU:    (апгрейд через MSI/winget сломал бы другие проекты).
function Get-GoVersion([string]$GoExe) {
    $r = Run-Tool $GoExe @('version')
    if ($r.ExitCode -ne 0) { return '' }
    if ($r.Out -match 'go(\d+)\.(\d+)(\.(\d+))?') { return @{ Major = [int]$Matches[1]; Minor = [int]$Matches[2]; Text = $Matches[0] } }
    return ''
}

$goFallback = @()
if ($env:ProgramFiles) { $goFallback = @((Join-Path $env:ProgramFiles 'Go\bin\go.exe')) }
$goPath = Find-Tool 'go.exe' $goFallback
$goPortableDir = Join-Path $ToolsDir ("go" + $GoModRequired + "." + $GoModPatch)
$goSessionExe = ''
$goSessionSource = ''

if ($goPath) {
    $v = Get-GoVersion $goPath
    if ($v -and ($v.Major -gt [int]($GoModRequired.Split('.')[0]) -or
                 ($v.Major -eq [int]($GoModRequired.Split('.')[0]) -and $v.Minor -ge [int]($GoModRequired.Split('.')[1])))) {
        say ("go: found $($v.Text) at $goPath - OK for go.mod") ("go: найден $($v.Text) в $goPath - подходит для go.mod")
        $goSessionExe = $goPath
        $goSessionSource = 'existing'
    } else {
        warn ("go: existing installation ($($v.Text) at $goPath) is older than go.mod requires ($GoModRequired); it will NOT be touched. A portable Go will be used for this session instead.") `
             ("go: существующая установка ($($v.Text) в $goPath) старше требуемой в go.mod ($GoModRequired); она НЕ будет тронута. Для этой сессии будет использован переносимый Go.")
    }
} else {
    say ("go: not found on PATH") ("go: не найден на PATH")
}

if (-not $goSessionExe) {
    # EN: portable route: $ToolsDir\go<version>\go\bin\go.exe; reused on re-runs.
    # RU: переносимый путь: $ToolsDir\go<версия>\go\bin\go.exe; при повторном запуске переиспользуется.
    $goZipVersion = "go$GoModRequired.$GoModPatch"
    $goPortableExe = Join-Path $goPortableDir 'go\bin\go.exe'
    if (Test-Path $goPortableExe) {
        say ("go: portable $goZipVersion already present in $goPortableDir - reusing it") ("go: переносимый $goZipVersion уже есть в $goPortableDir - переиспользуем")
    } elseif ($DryRun) {
        say ("go: DRY RUN - would download https://go.dev/dl/$goZipVersion.windows-amd64.zip and unpack it into $goPortableDir") `
            ("go: DRY RUN - скачал бы https://go.dev/dl/$goZipVersion.windows-amd64.zip и распаковал в $goPortableDir")
    } else {
        say ("go: downloading https://go.dev/dl/$goZipVersion.windows-amd64.zip ...") ("go: скачиваем https://go.dev/dl/$goZipVersion.windows-amd64.zip ...")
        $tmp = Join-Path ([IO.Path]::GetTempPath()) ('gramvanity-go-' + [Guid]::NewGuid().ToString('N'))
        try {
            New-Item -ItemType Directory -Path $tmp | Out-Null
            $zipPath = Join-Path $tmp 'go.zip'
            Invoke-WebRequest -Uri "https://go.dev/dl/$goZipVersion.windows-amd64.zip" -OutFile $zipPath -UseBasicParsing
            say ("go: unpacking into $goPortableDir ...") ("go: распаковываем в $goPortableDir ...")
            New-Item -ItemType Directory -Force -Path $goPortableDir | Out-Null
            Expand-Archive -Path $zipPath -DestinationPath $goPortableDir -Force
        } catch {
            fail "Could not download or unpack the portable Go ($($_.Exception.Message)). Check the network, or install Go manually and re-run." `
                 "Не удалось скачать или распаковать переносимый Go ($($_.Exception.Message)). Проверьте сеть или установите Go вручную и запустите снова."
        } finally {
            if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
        }
        if (-not (Test-Path $goPortableExe)) {
            fail "The portable Go archive did not produce go\bin\go.exe where expected." "Архив переносимого Go не создал go\bin\go.exe там, где ожидалось."
        }
        Add-ManifestEntry "installed portable go $goZipVersion -> $goPortableDir"
    }
    $goSessionExe = $goPortableExe
    $goSessionSource = 'portable'
}

# EN: session PATH: the chosen Go (and later portable tools) go FIRST for this process only;
# RU: сессионный PATH: выбранный Go (и далее переносимые инструменты) ставится ВПЕРЕДИ только
# EN: the global PATH and the registry are never modified. Stage 2 inherits it.
# RU: для этого процесса; глобальный PATH и реестр не меняются. Этап 2 унаследует его.
$goSessionDir = Split-Path $goSessionExe
if ($env:PATH -notlike "*$goSessionDir*") { $env:PATH = "$goSessionDir;$env:PATH" }
say ("go: session PATH uses $goSessionDir ($goSessionSource)") ("go: сессионный PATH использует $goSessionDir ($goSessionSource)")

# ---------------------------------------------------------------- python 3

# EN: 4. Python 3 is needed for `make embed` and the documentation checks. Existing installations
# RU: 4. Python 3 нужен для `make embed` и проверок документации. Существующие установки
# EN:    are used as-is; only a missing Python is installed (winget when present - it safely
# EN:    coexists with other versions; otherwise a clear manual instruction).
# RU:    используются как есть; ставится только отсутствующий Python (через winget при наличии -
# RU:    он безопасно соседствует с другими версиями; иначе - понятная ручная инструкция).
$pyExe = Find-Tool 'python3.exe'
if (-not $pyExe) { $pyExe = Find-Tool 'python.exe' }
if ($pyExe) {
    $r = Run-Tool $pyExe @('--version')
    if ($r.Out -match 'Python 3') {
        say ("python: found $($r.Out.Trim()) at $pyExe - OK") ("python: найден $($r.Out.Trim()) в $pyExe - подходит")
    } else {
        warn ("python: found at $pyExe but it is not Python 3 ($($r.Out.Trim())); the documentation checks will be skipped by stage 2.") `
             ("python: найден в $pyExe, но это не Python 3 ($($r.Out.Trim())); этап 2 пропустит проверки документации.")
    }
} else {
    say ("python: Python 3 not found") ("python: Python 3 не найден")
    $wingetPath = Find-Tool 'winget.exe'
    if ($wingetPath) {
        if ($DryRun) {
            say ("python: DRY RUN - would run: winget install --id Python.Python.3.13 -e --accept-package-agreements --accept-source-agreements") `
                ("python: DRY RUN - выполнил бы: winget install --id Python.Python.3.13 -e --accept-package-agreements --accept-source-agreements")
        } else {
            say ("python: installing via winget (per-user, coexists with other versions)...") ("python: устанавливаем через winget (per-user, соседствует с другими версиями)...")
            $r = Run-Tool $wingetPath @('install', '--id', 'Python.Python.3.13', '-e', '--accept-package-agreements', '--accept-source-agreements')
            if ($r.Out) { $r.Out.TrimEnd() | Write-Host }
            if ($r.Err) { $r.Err.TrimEnd() | Write-Host }
            if ($r.ExitCode -ne 0) {
                warn ("python: winget install failed (exit code $($r.ExitCode)); install Python 3 from https://www.python.org/downloads/ yourself and re-run - the rest of the setup does not depend on it.") `
                     ("python: установка через winget не удалась (код $($r.ExitCode)); установите Python 3 с https://www.python.org/downloads/ сами и запустите снова - остальной setup от него не зависит.")
            }
        }
    } else {
        warn ("python: winget is not available; install Python 3 from https://www.python.org/downloads/ (any 3.x, per-user is fine) and re-run this setup, or continue without the documentation checks.") `
             ("python: winget недоступен; установите Python 3 с https://www.python.org/downloads/ (любой 3.x, per-user), затем запустите setup снова, либо продолжите без проверок документации.")
    }
}

# ---------------------------------------------------------------- msys2

# EN: 5. MSYS2: use the existing root; install the single standard root C:\msys64 only when it is
# RU: 5. MSYS2: используем существующий корень; ставим единственный стандартный корень C:\msys64
# EN:    missing. A second MSYS2 root is never created.
# RU:    только если его нет. Второй корень MSYS2 никогда не создаётся.
$msys2Bash = ''
if ($Msys2Root) {
    if (-not (Test-Path (Join-Path $Msys2Root 'usr\bin\bash.exe'))) {
        fail "The -Msys2Root '$Msys2Root' does not contain usr\bin\bash.exe." `
             "В указанном -Msys2Root '$Msys2Root' нет usr\bin\bash.exe."
    }
    $msys2Bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
    say ("msys2: using the requested root $Msys2Root") ("msys2: используем указанный корень $Msys2Root")
} elseif (Test-Path 'C:\msys64\usr\bin\bash.exe') {
    $msys2Bash = 'C:\msys64\usr\bin\bash.exe'
    say ("msys2: found existing root C:\msys64") ("msys2: найден существующий корень C:\msys64")
} else {
    say ("msys2: not found (expected C:\msys64)") ("msys2: не найден (ожидался C:\msys64)")
    $wingetPath = Find-Tool 'winget.exe'
    if ($wingetPath) {
        if ($DryRun) {
            say ("msys2: DRY RUN - would run: winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements") `
                ("msys2: DRY RUN - выполнил бы: winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements")
        } else {
            say ("msys2: installing via winget into the single standard root C:\msys64 ...") ("msys2: устанавливаем через winget в единственный стандартный корень C:\msys64 ...")
            $r = Run-Tool $wingetPath @('install', '--id', 'MSYS2.MSYS2', '-e', '--accept-package-agreements', '--accept-source-agreements')
            if ($r.Out) { $r.Out.TrimEnd() | Write-Host }
            if ($r.Err) { $r.Err.TrimEnd() | Write-Host }
            if ($r.ExitCode -ne 0 -or -not (Test-Path 'C:\msys64\usr\bin\bash.exe')) {
                fail "MSYS2 installation did not produce C:\msys64\usr\bin\bash.exe. Install MSYS2 manually from https://www.msys2.org/ (keep the default C:\msys64) and re-run." `
                     "Установка MSYS2 не создала C:\msys64\usr\bin\bash.exe. Установите MSYS2 вручную с https://www.msys2.org/ (оставьте путь по умолчанию C:\msys64) и запустите снова."
            }
            Add-ManifestEntry "installed msys2 via winget -> C:\msys64"
        }
    } else {
        fail "winget is not available, so MSYS2 cannot be installed automatically. Install it manually from https://www.msys2.org/ into C:\msys64 (the only supported root), then re-run." `
             "winget недоступен, поэтому MSYS2 нельзя установить автоматически. Установите его вручную с https://www.msys2.org/ в C:\msys64 (единственный поддерживаемый корень) и запустите снова."
    }
}

# ---------------------------------------------------------------- stage 2

# EN: 6. Hand over to stage 2 inside MSYS2 UCRT64. MSYSTEM=UCRT64 + CHERE_INVOKING=1 make the
# RU: 6. Передаём управление этапу 2 внутри MSYS2 UCRT64. MSYSTEM=UCRT64 + CHERE_INVOKING=1
# EN:    login shell start in the repository with the UCRT64 toolchain; the session PATH
# EN:    (portable Go) is inherited, so build-windows.sh finds the right Go.
# RU:    делают так, что login-оболочка стартует в репозитории с тулчейном UCRT64; сессионный
# RU:    PATH (переносимый Go) наследуется, поэтому build-windows.sh найдёт нужный Go.
$stage2 = './tools/setup-dev-msys2.sh --lang ' + $Lang
if ($DryRun) {
    say ''
    say ("DRY RUN - stage 2 would now run inside MSYS2 UCRT64:") ("DRY RUN - теперь этап 2 выполнился бы внутри MSYS2 UCRT64:")
    say ("  bash: $msys2Bash") ("  bash: $msys2Bash")
    say ("  command: $stage2") ("  команда: $stage2")
    say ("  it installs the four UCRT64 pacman packages and runs: bash ./build-windows.sh --check") `
        ("  он устанавливает четыре pacman-пакета UCRT64 и выполняет: bash ./build-windows.sh --check")
    exit 0
}

say ''
say ("=== Stage 2: MSYS2 UCRT64 (log is printed below when it finishes) ===") ("=== Этап 2: MSYS2 UCRT64 (лог будет напечатан ниже после завершения) ===")
$log = Join-Path ([IO.Path]::GetTempPath()) ('gramvanity-stage2-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
# EN: Start-Process joins the argument list with spaces, so the -c command string must be quoted
# RU: Start-Process соединяет аргументы пробелами, поэтому строку команды для -c нужно взять
# EN: as a whole, or bash would treat every word after the first as a separate command.
# RU: в кавычки целиком, иначе bash выполнит каждое слово как отдельную команду.
$stage2Quoted = '"' + $stage2 + '"'
try {
    $env:MSYSTEM = 'UCRT64'
    $env:CHERE_INVOKING = '1'
    Set-Location $RepoRoot
    # EN: no -Wait: touching the process handle before WaitForExit is what makes ExitCode
    # RU: без -Wait: обращение к хендлу процесса до WaitForExit - это то, что заставляет
    # EN: reliable in Windows PowerShell 5.1 (with -Wait it can come back null).
    # RU: ExitCode работать в Windows PowerShell 5.1 (при -Wait он может вернуться null).
    $p = Start-Process -FilePath $msys2Bash -ArgumentList @('-lc', $stage2Quoted) -PassThru -NoNewWindow `
         -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $null = $p.Handle
    $p.WaitForExit()
    $stage2Exit = $p.ExitCode
} finally {
    Remove-Item Env:CHERE_INVOKING -ErrorAction SilentlyContinue
}
if (Test-Path $log) { Get-Content $log -Encoding UTF8 | Write-Host }
if (Test-Path "$log.err") { Get-Content "$log.err" -Encoding UTF8 | Write-Host }
say ("Stage 2 log: $log") ("Лог этапа 2: $log")
if ($null -eq $stage2Exit) { $stage2Exit = 1 }
exit $stage2Exit
