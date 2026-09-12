#Requires -Version 5.1
<#
Windows build assistant for the supplied Shipwright-MP Direct-IP 1.0.0 SOURCE PREVIEW.
No game source is replaced; no ROMs, saves or credentials are read or uploaded.
Setup installs missing tools only after a local confirmation. Build uses this tree.
Helper 1.2 retains the 1.1 SDK fix and adds opt-in cache-preserving resume.
Resume requires an existing matching VS2022 x64 build and generated soh.o2r.
This script has not been executed on Windows or used to certify a full game build.
#>
[CmdletBinding()]
param([ValidateSet('Setup','Build','Check')][string]$Mode = 'Build', [switch]$Resume)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent $PSScriptRoot
$script:Stage = 'Starting'
$script:TranscriptOn = $false
$script:ExitStatus = 1
$script:Logs = Join-Path $script:Root 'BUILD_LOGS'
$script:Log = $null
$script:Mutex = $null
$script:RebootRequired = $false
$script:SdkDiagnostics = @()

function Say([string]$Text) { Write-Host $Text }
function Need-Yes([string]$Question) {
    return ((Read-Host ($Question + ' [Y/N]')) -match '^(?i:y|yes)$')
}
function Refresh-Path {
    # Process only: never overwrite the user's persistent PATH.
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user;$env:Path"
}
function Find-Exe([string]$Name, [string[]]$Extras = @()) {
    $found = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { return $found.Source }
    foreach ($path in $Extras) {
        if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) { return $path }
    }
    return $null
}
function Probe([string]$Exe, [string[]]$Arguments) {
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $out = @(& $Exe @Arguments 2>$null)
        $code = $LASTEXITCODE
        if ($code -eq 0) { return ($out -join "`n") }
        return $null
    } finally { $ErrorActionPreference = $old }
}
function Invoke-Checked([string]$Exe, [string[]]$Arguments, [string]$Label, [int[]]$AllowedExitCodes = @(0)) {
    $script:Stage = $Label
    Say "`n========== $Label =========="
    Say ('Command: "' + $Exe + '" ' + (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '))
    $old = $ErrorActionPreference
    $code = -1
    try {
        # Windows PowerShell converts redirected native stderr into ErrorRecord objects.
        # Stream both outputs to the transcript; use EXIT STATUS, not stderr, for failure.
        $ErrorActionPreference = 'Continue'
        & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($AllowedExitCodes -notcontains $code) { throw "$Label stopped (exit code $code). See the command output above." }
    if ($code -eq 3010) { $script:RebootRequired = $true }
    if ($code -eq -1978335189) {
        Say 'WinGet found no applicable package update. This is not a component installation; prerequisites will be checked again.'
    }
}
function Get-SdkRoots {
    $roots = @()
    if ($env:WindowsSdkDir) { $roots += $env:WindowsSdkDir }
    # The CMD launcher selects 64-bit PowerShell; check both registry views anyway.
    foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32)) {
        $base = $null; $key = $null
        try {
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
            $key = $base.OpenSubKey('SOFTWARE\Microsoft\Windows Kits\Installed Roots')
            if ($key) {
                $value = $key.GetValue('KitsRoot10')
                if ($value -is [string] -and $value.Trim()) { $roots += $value }
            }
        } catch {
            # A registry view may be unavailable. Still examine other roots and files.
        } finally {
            if ($key) { $key.Dispose() }
            if ($base) { $base.Dispose() }
        }
    }
    foreach ($programFiles in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($programFiles) { $roots += (Join-Path $programFiles 'Windows Kits\10') }
    }
    return @($roots | Where-Object { $_ } | ForEach-Object { $_.Trim().TrimEnd('\') } | Select-Object -Unique)
}
function Find-WindowsSdk([string[]]$Roots) {
    $script:SdkDiagnostics = @()
    $found = @()
    foreach ($root in $Roots) {
        $libRoot = Join-Path $root 'Lib'
        if (-not (Test-Path -LiteralPath $libRoot -PathType Container)) {
            $script:SdkDiagnostics += "No SDK library directory: $libRoot"
            continue
        }
        # Enumerate actual version directories instead of an intermediate wildcard path.
        $directories = @(Get-ChildItem -LiteralPath $libRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' })
        if ($directories.Count -eq 0) { $script:SdkDiagnostics += "No Windows SDK versions found under $libRoot" }
        foreach ($directory in $directories) {
            $version = $directory.Name
            $required = @(
                "Include\$version\um\Windows.h",
                "Include\$version\shared\sdkddkver.h",
                "Include\$version\ucrt\stdio.h",
                "Lib\$version\um\x64\kernel32.lib",
                "Lib\$version\um\x64\user32.lib",
                "Lib\$version\um\x64\ws2_32.lib",
                "Lib\$version\ucrt\x64\ucrt.lib",
                "bin\$version\x64\rc.exe",
                "bin\$version\x64\mt.exe"
            )
            $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf) })
            if ($missing.Count -eq 0) {
                $found += [pscustomobject]@{ Version = [version]$version; Root = $root }
            } else {
                $script:SdkDiagnostics += "Incomplete SDK $version at ${root}: missing $($missing -join ', ')"
            }
        }
    }
    return ($found | Sort-Object -Property Version -Descending | Select-Object -First 1)
}
function Get-VsModifyArguments($Tools) {
    if (-not $Tools.VSExisting) { throw 'No existing Visual Studio 2022 instance was found to modify.' }
    $installPath = $Tools.VSExisting.TrimEnd('\')
    if ($installPath -match '["\r\n]') { throw 'Invalid Visual Studio installation path.' }
    # A channel belongs to the existing instance: do not change it to a different release.
    $channel = $Tools.VSChannel
    if (-not $channel) { $channel = 'VisualStudio.17.Release' }
    if ($channel -notmatch '^[A-Za-z0-9._-]+$') { throw 'Unexpected Visual Studio channel identifier.' }
    # Start-Process joins ArgumentList into a native command line. Quote the path explicitly.
    $arguments = @('modify', '--installPath', ('"' + $installPath + '"'), '--channelId', $channel,
        '--passive', '--norestart')
    if (-not $Tools.VS) {
        # These component IDs work for both Build Tools and the full VS 2022 IDE.
        $arguments += @('--add', 'Microsoft.Component.MSBuild',
            '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
    }
    if (-not $Tools.SDK) {
        # 22621 is an explicitly listed VS 2022 SDK. Retain compatibility with early 17.x catalogs.
        $sdkComponent = 'Microsoft.VisualStudio.Component.Windows11SDK.22621'
        if ($Tools.VSVersion -and ([version]$Tools.VSVersion -lt [version]'17.3')) {
            $sdkComponent = 'Microsoft.VisualStudio.Component.Windows10SDK.19041'
        }
        $arguments += @('--add', $sdkComponent, '--add', 'Microsoft.Component.VC.Runtime.UCRTSDK')
    }
    if (-not $Tools.CMake) { $arguments += @('--add', 'Microsoft.VisualStudio.Component.VC.CMake.Project') }
    return $arguments
}
function Modify-VisualStudio($Tools) {
    # Do not trust an unrelated setup.exe from PATH.
    $setup = $null
    foreach ($candidate in @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\setup.exe"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $setup = $candidate; break }
    }
    if (-not $setup) {
        throw 'Visual Studio 2022 is installed, but its Microsoft installer was not found. Open Visual Studio Installer from Start and select Modify on your 2022 installation. Do not uninstall it.'
    }
    $arguments = @(Get-VsModifyArguments $Tools)
    $script:Stage = 'Adding missing components to the existing Visual Studio 2022 installation'
    Say "`n========== $script:Stage =========="
    Say ('Command: "' + $setup + '" ' + ($arguments -join ' '))
    Say 'Approve the Microsoft Visual Studio Installer administrator prompt. This does not uninstall your tools.'
    Say 'Wait for the installer to finish. No automatic restart is requested.'
    try {
        # Elevate only the Microsoft installer. Never pass --wait to setup.exe (unsupported).
        $process = Start-Process -FilePath $setup -ArgumentList $arguments -WorkingDirectory $script:Root `
            -Verb RunAs -Wait -PassThru
        $code = $process.ExitCode
    } catch [System.ComponentModel.Win32Exception] {
        if ($_.Exception.NativeErrorCode -eq 1223) { throw 'The administrator prompt was cancelled. No successful component installation is claimed. Run setup again when ready.' }
        throw
    }
    Say "Visual Studio Installer exit code: $code"
    if ($null -eq $code) { throw 'The installer did not report an exit code. Check Visual Studio Installer before retrying; setup is not being marked complete.' }
    if ($code -eq 3010 -or $code -eq 1641) {
        $script:RebootRequired = $true
        Say 'The installer completed but reported that a Windows restart is required.'
        return
    }
    if ($code -eq 1001 -or $code -eq 1618) {
        throw 'Another installer is already running. Let it finish, close the Visual Studio Installer window, then run 1_SETUP_TOOLS.cmd again. Do not uninstall anything.'
    }
    if ($code -eq 1003 -or $code -eq 8006) {
        throw 'Visual Studio is in use. Save your work, close Visual Studio, then run 1_SETUP_TOOLS.cmd again. The helper will not force-close your work.'
    }
    if ($code -ne 0) {
        throw "Visual Studio Installer stopped with exit code $code. Setup is not complete. Its detailed logs are the dd_setup/dd_client files in your Windows temporary folder. The helper log records this command and status."
    }
}
function Install-WingetTool([string]$Id, [string]$Label, [string]$Override = '') {
    $winget = Find-Exe 'winget.exe'
    if (-not $winget) {
        throw "Microsoft WinGet is needed to install $Id, but was not found. Install/update Microsoft App Installer from Microsoft Store, then reopen 1_SETUP_TOOLS.cmd. Existing Visual Studio component repair itself does not require WinGet."
    }
    $arguments = @('install', '--exact', '--source', 'winget', '--id', $Id)
    if ($Override) { $arguments += @('--override', $Override) }
    # No-applicable-update is NOT proof of a usable install. Setup always re-probes real prerequisites.
    Invoke-Checked $winget $arguments $Label @(0, 3010, -1978335189)
}

function Get-Tools {
    Refresh-Path
    $vswhere = Find-Exe 'vswhere.exe' @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe")
    $vs = $null
    if ($vswhere) {
        $vs = Probe $vswhere @('-latest','-products','*','-version','[17.0,18.0)',
            '-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath')
        if ($vs) { $vs = $vs.Trim() }
    }
    $vsExisting = $vs
    if (-not $vsExisting -and $vswhere) {
        # Do not require C++ components when locating an instance that needs modification.
        $vsExisting = Probe $vswhere @('-latest','-all','-products','*','-version','[17.0,18.0)',
            '-property','installationPath')
        if ($vsExisting) { $vsExisting = $vsExisting.Trim() }
    }
    $vsChannel = $null; $vsVersion = $null
    if ($vswhere -and $vsExisting) {
        $json = Probe $vswhere @('-all','-products','*','-version','[17.0,18.0)','-format','json')
        if ($json) {
            $instances = ConvertFrom-Json -InputObject $json
            foreach ($instance in $instances) {
                if ($instance.PSObject.Properties['installationPath'] -and $instance.installationPath -ieq $vsExisting) {
                    if ($instance.PSObject.Properties['channelId']) { $vsChannel = $instance.channelId }
                    if ($instance.PSObject.Properties['installationVersion']) { $vsVersion = $instance.installationVersion }
                    break
                }
            }
        }
    }
    $sdkInfo = Find-WindowsSdk @(Get-SdkRoots)
    $sdk = $null; $sdkRoot = $null
    if ($sdkInfo) { $sdk = $sdkInfo.Version.ToString(); $sdkRoot = $sdkInfo.Root }
    $git = Find-Exe 'git.exe' @("$env:ProgramFiles\Git\cmd\git.exe", "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe")
    if ($git -and -not (Probe $git @('--version'))) { $git = $null }
    $cmakeCandidates = @()
    $pathCmake = Find-Exe 'cmake.exe'
    if ($pathCmake) { $cmakeCandidates += $pathCmake }
    if ($vs) { $cmakeCandidates += "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
    $cmakeCandidates += "$env:ProgramFiles\CMake\bin\cmake.exe"
    $cmakeCandidates += "$env:LOCALAPPDATA\Programs\CMake\bin\cmake.exe"
    $cmake = $null; $cmakeVersion = $null
    foreach ($candidate in ($cmakeCandidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $text = Probe $candidate @('--version')
        if ($text -and $text -match 'cmake version (\d+\.\d+\.\d+)') {
            $version = [version]$Matches[1]
            $bin = Split-Path $candidate -Parent
            if ($version -ge [version]'3.26.0' -and (Test-Path "$bin\cpack.exe") -and (Test-Path "$bin\ctest.exe")) {
                $cmake = $candidate; $cmakeVersion = $version; break
            }
        }
    }
    $pythonCandidates = @()
    $pathPython = Find-Exe 'python.exe'
    if ($pathPython -and $pathPython -notmatch '\\WindowsApps\\') { $pythonCandidates += $pathPython }
    $pythonCandidates += @(Get-ChildItem -Path "$env:LOCALAPPDATA\Programs\Python\Python*\python.exe","$env:ProgramFiles\Python*\python.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
    foreach ($key in @('HKCU:\Software\Python\PythonCore','HKLM:\Software\Python\PythonCore')) {
        foreach ($versionKey in @(Get-ChildItem $key -ErrorAction SilentlyContinue)) {
            $installKey = Get-Item ($versionKey.PSPath + '\InstallPath') -ErrorAction SilentlyContinue
            if ($installKey) {
                $installPath = $installKey.GetValue('')
                if ($installPath) { $pythonCandidates += (Join-Path $installPath 'python.exe') }
            }
        }
    }
    $python = $null
    foreach ($candidate in ($pythonCandidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $text = Probe $candidate @('-c','import sys; print(sys.executable); sys.exit(0 if sys.version_info >= (3, 8) else 1)')
        if ($text) { $python = $text.Trim(); break }
    }
    $curl = Find-Exe 'curl.exe' @("$env:SystemRoot\System32\curl.exe","$env:ProgramFiles\Git\mingw64\bin\curl.exe")
    return [pscustomobject]@{ VS=$vs; VSExisting=$vsExisting; VSChannel=$vsChannel; VSVersion=$vsVersion; SDK=$sdk; SDKRoot=$sdkRoot; Git=$git; CMake=$cmake; CMakeVersion=$cmakeVersion; Python=$python; Curl=$curl }
}
function Show-Tools($Tools) {
    Say "`nTool check:"
    foreach ($name in @('VS','SDK','Git','CMake','Python','Curl')) {
        $value = $Tools.$name
        if ($value) { Say "  OK       ${name}: $value" } else { Say "  MISSING  $name" }
    }
    if ($Tools.SDKRoot) { Say "  SDK root: $($Tools.SDKRoot)" }
    if (-not $Tools.VS -and $Tools.VSExisting) { Say "  Existing VS 2022 needing components: $($Tools.VSExisting)" }
    if (-not $Tools.SDK) {
        Say '  SDK detection details:'
        foreach ($detail in $script:SdkDiagnostics) { Say "    $detail" }
    }
}
function Missing-Tools($Tools) {
    $missing = @()
    if (-not $Tools.VS) { $missing += 'Visual Studio 2022 C++ v143 tools' }
    if (-not $Tools.SDK) { $missing += 'Windows SDK' }
    if (-not $Tools.Git) { $missing += 'Git for Windows' }
    if (-not $Tools.CMake) { $missing += 'CMake 3.26 or newer, with CPack and CTest' }
    if (-not $Tools.Python) { $missing += 'Python 3' }
    if (-not $Tools.Curl) { $missing += 'curl.exe (normally included with Windows or Git)' }
    return $missing
}
function Setup-Tools($Tools) {
    $missing = @(Missing-Tools $Tools)
    if ($missing.Count -eq 0) {
        Say "`nAll required tools were found. Nothing needs installing. Next, double-click 2_BUILD_GAME.cmd."
        return
    }
    Say "`nMissing or incomplete: $($missing -join ', ')"
    if ((-not $Tools.VS -or -not $Tools.SDK) -and $Tools.VSExisting) {
        Say "Your existing Visual Studio 2022 installation will be MODIFIED: $($Tools.VSExisting)"
        Say 'Only the requested missing build components will be added. It will not be reinstalled through WinGet.'
    }
    Say 'Setup checks the installed files again afterwards; an installer success message alone is not sufficient.'
    Say 'Review the expected licence/administrator prompts. No unrelated software is upgraded; no automatic reboot is requested.'
    if (-not (Need-Yes 'Proceed with setting up the missing build components?')) { Say 'No installation requested.'; return }
    # Repair an existing VS instance without depending on WinGet being present.
    if (-not $Tools.VS -or -not $Tools.SDK) {
        if ($Tools.VSExisting) {
            Modify-VisualStudio $Tools
        } else {
            $override = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --add Microsoft.Component.VC.Runtime.UCRTSDK --add Microsoft.VisualStudio.Component.VC.CMake.Project'
            Install-WingetTool 'Microsoft.VisualStudio.2022.BuildTools' 'Installing Visual Studio 2022 C++ build tools' $override
            $Tools = Get-Tools
            # Handles a newly discovered/partial instance, including WinGet's "no available upgrade" case.
            if (-not $script:RebootRequired -and (-not $Tools.VS -or -not $Tools.SDK)) {
                if ($Tools.VSExisting) {
                    Modify-VisualStudio $Tools
                } else {
                    throw 'WinGet did not provide a detectable Visual Studio 2022 installation. No repeated install or success claim is being made. Check the installer output above.'
                }
            }
        }
        if ($script:RebootRequired) {
            Say "`nRESTART REQUIRED: Save your work and restart Windows when ready."
            Say 'After the restart, run 1_SETUP_TOOLS.cmd again. Do not start the game build yet.'
            return
        }
        $Tools = Get-Tools
        if (-not $Tools.VS -or -not $Tools.SDK) {
            Show-Tools $Tools
            throw 'The Visual Studio component operation finished, but the C++ tools or complete x64 Windows SDK are still not detectable. Nothing has been marked ready. The SDK paths and missing files are listed above; send BUILD_LOGS\LATEST.txt.'
        }
    }
    if (-not $Tools.Git) { Install-WingetTool 'Git.Git' 'Installing Git' }
    if (-not $Tools.Python) { Install-WingetTool 'Python.Python.3.13' 'Installing Python' }
    if (-not $Tools.CMake) { Install-WingetTool 'Kitware.CMake' 'Installing CMake' }
    $Tools = Get-Tools
    Show-Tools $Tools
    if ($script:RebootRequired) {
        Say "`nRESTART REQUIRED: Save your work and restart Windows when ready, then run 1_SETUP_TOOLS.cmd again."
        return
    }
    $missing = @(Missing-Tools $Tools)
    if ($missing.Count) {
        throw "Still missing or incomplete: $($missing -join ', '). Setup is not complete. Send BUILD_LOGS\LATEST.txt from this run; it includes the detected paths."
    }
    Say "`nSetup complete. Next, double-click 2_BUILD_GAME.cmd."
}

function Check-Source {
    foreach ($relative in @('CMakeLists.txt','libultraship\CMakeLists.txt','torch\CMakeLists.txt',
        'soh\soh\Network\Direct\DirectMultiplayer.cpp','tests\direct_multiplayer\CMakeLists.txt',
        'tests\direct_multiplayer\source_contract_tests.py')) {
        if (-not (Test-Path -LiteralPath (Join-Path $script:Root $relative) -PathType Leaf)) {
            throw "Missing source file: $relative. Use the complete GitHub source snapshot, with soh, torch, libultraship and build-helper beside CMakeLists.txt. Do not replace or delete an existing build folder."
        }
    }
    if ($script:Root.StartsWith('\\') -or $script:Root.Length -gt 70 -or $script:Root -match '[;!%\r\n]') {
        throw 'Move this entire extracted folder to a short, local path such as C:\ShipMP\Shipwright-MP, then try again. Do not use a network path or special characters such as ; ! %.'
    }
    if ($script:Root.StartsWith($env:TEMP, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'This is running from a temporary extraction folder. Right-click the ZIP > Extract All to C:\ShipMP, then run the helper inside the extracted Shipwright-MP folder.'
    }
}
function Verify-And-OpenPackage([string]$PackageDir) {
    $script:Stage = 'Checking the generated package'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zips = @(Get-ChildItem -LiteralPath $PackageDir -Filter '*.zip' -File)
    if ($zips.Count -ne 1) { throw "Expected one fresh ZIP in $PackageDir; found $($zips.Count). No ready-to-play claim is being made." }
    $zip = [IO.Compression.ZipFile]::OpenRead($zips[0].FullName)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
        foreach ($name in $names) {
            if ($name -match '(^/|^[A-Za-z]:|(^|/)\.\.(/|$))') { throw "Unsafe archive entry: $name" }
        }
        foreach ($leaf in @('soh.exe','soh.o2r','gamecontrollerdb.txt')) {
            $match = @($zip.Entries | Where-Object { $_.Name -ieq $leaf })
            if ($match.Count -ne 1 -or $match[0].Length -eq 0) { throw "Package is missing one unambiguous, nonempty $leaf." }
        }
        if (-not ($names -match '(^|/)assets/.+\.ya?ml$')) { throw 'Package is missing the ROM extraction definitions (assets YAML files).' }
    } finally { $zip.Dispose() }
    $unpacked = Join-Path $PackageDir 'Game'
    [IO.Compression.ZipFile]::ExtractToDirectory($zips[0].FullName, $unpacked)
    $exes = @(Get-ChildItem -LiteralPath $unpacked -Recurse -File -Filter 'soh.exe')
    if ($exes.Count -ne 1) { throw 'Could not find one game executable after extracting the package.' }
    $gameDir = $exes[0].DirectoryName
    foreach ($leaf in @('soh.o2r','gamecontrollerdb.txt','assets')) {
        if (-not (Test-Path -LiteralPath (Join-Path $gameDir $leaf))) { throw "Missing runtime neighbour: $leaf" }
    }
    $hash = (Get-FileHash -LiteralPath $zips[0].FullName -Algorithm SHA256).Hash
    $result = @"
BUILD AND PACKAGE COMMANDS COMPLETED ON THIS PC
ZIP: $($zips[0].FullName)
ZIP SHA256: $hash
Game executable: $($exes[0].FullName)
Build log: $script:Log

This is still the Direct-IP source PREVIEW, now built locally.
Compilation and transport tests are not end-to-end multiplayer validation.
Enemy AI, shared boss health, projectiles and cutscenes remain locally simulated.

No ROM or extracted original-game data is included. Supply your own supported
Ocarina of Time game data using this version's normal first-launch extraction.
Do not overwrite your accepted installation or move your only copy of a save.
The helper does not start the game, read saves, or import saves automatically.
See docs/multiplayer/ACCEPTANCE_CHECKLIST.md before using real progress.
"@
    Set-Content -LiteralPath (Join-Path $PackageDir 'READ_BEFORE_PLAYING.txt') -Value $result -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $script:Root 'LAST_SUCCESSFUL_BUILD.txt') -Value $result -Encoding UTF8
    Say "`nBUILD AND PACKAGE COMPLETED ON YOUR PC."
    Say "ZIP: $($zips[0].FullName)"
    Say "Game folder: $gameDir"
    Say 'Your own supported game data is still required. Keep the original game and saves separate.'
    Say 'Multiplayer runtime behaviour has NOT been certified by this helper.'
    try { Start-Process explorer.exe -ArgumentList ('"' + $PackageDir + '"') | Out-Null } catch { Say "Open this folder manually: $PackageDir" }
}
function Confirm-ResumeBuild([string]$Build, $Tools) {
    $script:Stage = 'Checking the existing build before resuming'
    $cachePath = Join-Path $Build 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        throw 'The existing CMakeCache.txt is missing. Select the SAME working folder used for the failed compile. Do not delete caches or prepare a new copy.'
    }
    $cache = @{}
    foreach ($line in [IO.File]::ReadAllLines($cachePath)) {
        if ($line -match '^([^#/:][^:=]*):[^=]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    foreach ($key in @('CMAKE_HOME_DIRECTORY','CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_INSTANCE','CMAKE_COMMAND')) {
        if (-not $cache.ContainsKey($key)) { throw "Existing build cache is missing $key. No fresh build or tool installation was started." }
    }
    $root = [IO.Path]::GetFullPath($script:Root).TrimEnd([char[]]'\/')
    $cachedRoot = [IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY']).TrimEnd([char[]]'\/')
    if (-not [String]::Equals($root, $cachedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'This cache belongs to another source folder. Select the original, unmoved working folder.'
    }
    if ($cache['CMAKE_GENERATOR'] -ne 'Visual Studio 17 2022' -or $cache['CMAKE_GENERATOR_PLATFORM'] -ne 'x64') {
        throw 'Resume requires the existing Visual Studio 2022 x64 build; the cached generator is different.'
    }
    $cachedVs = [IO.Path]::GetFullPath($cache['CMAKE_GENERATOR_INSTANCE']).TrimEnd([char[]]'\/')
    $vs = [IO.Path]::GetFullPath($Tools.VS).TrimEnd([char[]]'\/')
    if (-not [String]::Equals($vs, $cachedVs, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The detected Visual Studio installation differs from this cache. No rebuild or reinstall was started.'
    }
    foreach ($relative in @('soh\soh.vcxproj','soh\soh.o2r','CPackConfig.cmake')) {
        $path = Join-Path $Build $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Resume requires the previously generated $relative. Select the working folder from the failed compile."
        }
    }
    # Keep the compiler/generator pair and CMake executable used by this cache.
    if (-not (Test-Path -LiteralPath $cache['CMAKE_COMMAND'] -PathType Leaf)) {
        throw 'The cached CMake executable is no longer present. No fresh build was started.'
    }
    $Tools.CMake = $cache['CMAKE_COMMAND']
    Say 'RESUME: using the existing configured build, dependency libraries, and soh.o2r.'
    Say 'No explicit configure, dependency update, asset-generation or clean step is requested.'
    Say 'CMake may still run its normal dependency checks. Remaining game files must still compile and link.'
}
function Build-Game($Tools) {
    Check-Source
    $missing = @(Missing-Tools $Tools)
    if ($missing.Count) { throw "Missing: $($missing -join ', '). Double-click 1_SETUP_TOOLS.cmd first. No build was attempted." }
    Say "`nThis builds the INCLUDED multiplayer preview, not an upstream replacement."
    Say 'Internet access is needed for third-party dependencies. Tools and build caches consume disk space.'
    Say 'Your original installation and saves must stay in a separate folder.'
    Say 'The source is not yet full-game compilation-verified; real compiler errors may need source fixes.'
    if (-not (Need-Yes 'Start the local build and tests?')) { Say 'Build cancelled before starting.'; return }
    if ($Resume) { Confirm-ResumeBuild (Join-Path $script:Root '_beginner_build\windows-x64') $Tools }
    $bin = Split-Path $Tools.CMake -Parent
    $ctest = Join-Path $bin 'ctest.exe'; $cpack = Join-Path $bin 'cpack.exe'
    $env:Path = "$bin;$(Split-Path $Tools.Git -Parent);$(Split-Path $Tools.Python -Parent);$(Split-Path $Tools.Curl -Parent);$env:Path"
    # Dedicated dependency root: do not update a vcpkg installation used by other projects.
    $work = Join-Path $script:Root '_beginner_build'
    $build = Join-Path $work 'windows-x64'
    $tests = Join-Path $work 'network-tests-x64'
    $env:VCPKG_ROOT = Join-Path $work 'vcpkg'
    $env:VCPKG_VISUAL_STUDIO_PATH = $Tools.VS
    $env:VCPKG_MAX_CONCURRENCY = '2'
    $env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
    $env:_CL_ = "$env:_CL_ /MP2"
    # Git settings are inherited by child processes only; never change global Git config.
    $count = 0
    if ($env:GIT_CONFIG_COUNT) { $count = [int]$env:GIT_CONFIG_COUNT }
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_KEY_$count", 'core.longpaths', 'Process')
    [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$count", 'true', 'Process')
    $env:GIT_CONFIG_COUNT = [string]($count + 1)
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    $generator = @('-G','Visual Studio 17 2022','-T','v143','-A','x64',"-DCMAKE_GENERATOR_INSTANCE=$($Tools.VS)")
    $configure = @('-S',$script:Root,'-B',$build) + $generator + @('-DCMAKE_BUILD_TYPE=Release',
        "-DVCPKG_ROOT=$env:VCPKG_ROOT", "-DPython3_EXECUTABLE=$($Tools.Python)", '-DSOH_TOOLS_ONLY=OFF')
    if ($Tools.CMakeVersion -ge [version]'4.0.0') { $configure += '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' }
    if ($Resume) {
        Say '1/6 Reusing existing configuration and dependencies.'
        Say '2/6 Reusing the already-generated soh.o2r.'
        Invoke-Checked $Tools.Python @((Join-Path $script:Root 'tests\direct_multiplayer\source_contract_tests.py')) 'Early source-contract check (not a compiler test)'
    } else {
        Invoke-Checked $Tools.CMake $configure '1/6 Configure game and fetch dependencies'
        Invoke-Checked $Tools.CMake @('--build',$build,'--config','Release','--target','GenerateSohOtr','--parallel','2') '2/6 Generate the matching soh.o2r assets'
    }
    Invoke-Checked $Tools.CMake @('--build',$build,'--config','Release','--parallel','2') '3/6 Compile the complete game'
    Invoke-Checked $Tools.Python @((Join-Path $script:Root 'tests\direct_multiplayer\source_contract_tests.py')) '4/6 Check source integration contracts'
    Invoke-Checked $Tools.CMake (@('-S',(Join-Path $script:Root 'tests\direct_multiplayer'),'-B',$tests) + $generator) '5/6 Configure network tests'
    Invoke-Checked $Tools.CMake @('--build',$tests,'--config','Release','--parallel','2') '5/6 Compile network tests'
    Invoke-Checked $ctest @('--test-dir',$tests,'-C','Release','--output-on-failure') '5/6 Run network tests'
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $out = Join-Path $script:Root "OUTPUT\$stamp"
    New-Item -ItemType Directory -Path $out | Out-Null
    Invoke-Checked $cpack @('--config',(Join-Path $build 'CPackConfig.cmake'),'-G','ZIP','-C','Release','-B',$out,
        '-D',"CPACK_OUTPUT_FILE_PREFIX=$out",'-D','CPACK_ARCHIVE_SHIP_FILE_NAME=Shipwright-MP-DirectIP-Preview-Windows-x64') '6/6 Package the compiled game'
    Verify-And-OpenPackage $out
}

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This helper is for Windows, not Linux or macOS.' }
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'A 64-bit Windows PC is required for this x64 build helper.' }
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { throw 'This helper targets Intel/AMD x64 Windows, not Windows on ARM.' }
    New-Item -ItemType Directory -Path $script:Logs -Force | Out-Null
    $script:Log = Join-Path $script:Logs ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $Mode + '.txt')
    Start-Transcript -LiteralPath $script:Log -Force | Out-Null
    $script:TranscriptOn = $true
    # Exclusive lock file, released even on error; no process-wide or persistent mutex.
    $script:Mutex = [IO.File]::Open((Join-Path $script:Root 'build-helper\.running.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    Say 'SHIPWRIGHT-MP | WINDOWS BUILD HELPER 1.2 | COMPILE FIX / CACHE-PRESERVING RESUME'
    Say "Mode: $Mode | Source: $script:Root"
    Say "Log: $script:Log"
    Say 'Source preview only. No precompiled game is supplied in this download.'
    $script:Stage = 'Checking build prerequisites'
    $tools = Get-Tools
    Show-Tools $tools
    switch ($Mode) {
        'Setup' { Setup-Tools $tools }
        'Build' { Build-Game $tools }
        'Check' {
            $missing = @(Missing-Tools $tools)
            if ($missing.Count) { throw "Missing: $($missing -join ', '). Run 1_SETUP_TOOLS.cmd." }
            Say 'Tool check passed. This is not a game build or a gameplay test.'
        }
    }
    $script:ExitStatus = 0
} catch {
    Say "`nSTOPPED AT: $script:Stage"
    Say $_.Exception.Message
    Say "`nYou do not need to interpret compiler output yourself."
    Say 'Send BUILD_LOGS\LATEST.txt back in this chat. It may contain local folder/user names; review before sharing.'
    Say 'Do not send your ROM, saves, passwords, or the entire build folder.'
    Say 'No successful full-game build is being claimed by this stopped run.'
    if ($script:Log) { Say "Detailed log: $script:Log" }
    Say ('Script location: ' + $_.InvocationInfo.PositionMessage)
} finally {
    if ($script:Mutex) { $script:Mutex.Dispose() }
    if ($script:TranscriptOn) {
        try { Stop-Transcript | Out-Null } catch {}
        try { Copy-Item -LiteralPath $script:Log -Destination (Join-Path $script:Logs 'LATEST.txt') -Force } catch {}
    }
}
exit $script:ExitStatus
