<#
.SYNOPSIS
    Builds FileDonkey for Windows and packages it into a single setup .exe.

.DESCRIPTION
    Release build -> staging directory -> Inno Setup installer. Every step is
    reproducible from a clean tree; nothing here depends on Qt Creator having
    been run first.

    The version is not configurable: it is parsed out of app/app.pro, which is
    where that file says the version lives for the whole project. Keeping one
    source of truth is the reason the .exe's VERSIONINFO block and the status
    bar cannot drift apart, and the installer joins the same arrangement.

.PARAMETER StageOnly
    Build and stage, but do not run Inno Setup. Useful for inspecting exactly
    what would ship, and for running the machine without Inno Setup installed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\package-win.ps1
    powershell -ExecutionPolicy Bypass -File packaging\package-win.ps1 -StageOnly

    The -ExecutionPolicy flag is not superstition. Windows client machines
    default to Restricted, which refuses to run any .ps1 at all, and the
    default is per-machine rather than something this repository can carry -
    so a fresh checkout on a fresh machine hits it every time. The flag lasts
    only for that one process and changes no setting.
#>
[CmdletBinding()]
param(
    [string] $QtDir    = 'D:\Projects\Qt\6.9.2\mingw_64',
    [string] $MinGwDir = 'D:\Projects\Qt\Tools\mingw1310_64',
    [string] $IsccPath = '',
    [string] $OutDir   = '',
    [switch] $StageOnly
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Write-Stage {
    param([string] $Text)
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

# Native tools are run through here so that a non-zero exit, and only a non-zero
# exit, stops the script.
#
# The preference dance is not decoration. Windows PowerShell 5.1 wraps whatever
# a native command writes to stderr in ErrorRecords, and with the script-wide
# 'Stop' preference in force those become terminating errors - so g++ printing a
# perfectly ordinary -Wclass-memaccess warning would abort the packaging run.
# Worse, it only happens when the caller pipes or redirects this script's
# output, which is exactly what anyone automating it will do, so the failure
# looks intermittent. Exit code is the only thing here that means failure.
function Invoke-Native {
    param(
        [Parameter(Mandatory)] [string]   $Exe,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string]   $What
    )
    Write-Host "    $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments
    } finally {
        $ErrorActionPreference = $previous
    }

    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit code $LASTEXITCODE)"
    }
}

function Assert-Tool {
    param([string] $Path, [string] $Hint)
    if (-not (Test-Path $Path)) {
        throw "Not found: $Path`n  $Hint"
    }
}

# Inno Setup does not install to one predictable place. winget installs it
# per-user under %LOCALAPPDATA%\Programs when it is not running elevated, and
# per-machine under Program Files when it is, so checking only the latter finds
# nothing on a machine where winget has just reported success. The registry pass
# at the end is what keeps this working when the directory name changes with the
# major version.
function Find-Iscc {
    $onPath = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $roots = @()
    if ($env:LOCALAPPDATA)        { $roots += (Join-Path $env:LOCALAPPDATA 'Programs') }
    if (${env:ProgramFiles(x86)}) { $roots += ${env:ProgramFiles(x86)} }
    if ($env:ProgramFiles)        { $roots += $env:ProgramFiles }

    foreach ($root in $roots) {
        $candidate = Join-Path $root 'Inno Setup 6\ISCC.exe'
        if (Test-Path $candidate) { return $candidate }
    }

    $uninstallKeys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($key in $uninstallKeys) {
        $entries = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue |
                   Where-Object { $_.DisplayName -like 'Inno Setup*' -and $_.InstallLocation }
        foreach ($entry in $entries) {
            $candidate = Join-Path $entry.InstallLocation 'ISCC.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    return $null
}

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $RepoRoot 'build\package-win'
$StageDir = Join-Path $RepoRoot 'build\package-win-stage'
$IssPath  = Join-Path $PSScriptRoot 'FileDonkey.iss'

if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'dist' }

$QtBin    = Join-Path $QtDir 'bin'
$MinGwBin = Join-Path $MinGwDir 'bin'

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------

Write-Stage 'Checking the toolchain'

Assert-Tool (Join-Path $QtBin 'qmake.exe')        'Pass -QtDir <path-to-Qt>\6.x.y\mingw_64'
Assert-Tool (Join-Path $QtBin 'lrelease.exe')     'Pass -QtDir <path-to-Qt>\6.x.y\mingw_64'
Assert-Tool (Join-Path $QtBin 'windeployqt.exe')  'Pass -QtDir <path-to-Qt>\6.x.y\mingw_64'
Assert-Tool (Join-Path $MinGwBin 'mingw32-make.exe') 'Pass -MinGwDir <path-to-Qt>\Tools\mingw####_64'
Assert-Tool $IssPath 'The Inno Setup script should sit beside this one.'

if (-not $StageOnly) {
    if (-not $IsccPath) { $IsccPath = Find-Iscc }

    if (-not $IsccPath) {
        throw @'
Inno Setup 6 was not found.

Install it from https://jrsoftware.org/isdl.php (or: winget install JRSoftware.InnoSetup),
then run this script again.

If it is already installed somewhere this script did not look, point at it
directly with -IsccPath <path-to-ISCC.exe>. To build and stage without
producing an installer at all, re-run with -StageOnly.
'@
    }
    Assert-Tool $IsccPath 'Pass -IsccPath <path-to-ISCC.exe>'
}

# WinFsp is resolved through the registry rather than a hardcoded Program Files
# path, because that is where WinFsp itself looks: FspLoad() in winfsp.h falls
# back to HKLM\...\WinFsp\InstallDir when the DLL is not already on the search
# path. Its bin directory is a symlink into a versioned side-by-side directory,
# so reading InstallDir is also what keeps us pointed at the current version.
$WinFspDir = $null
foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\WinFsp', 'HKLM:\SOFTWARE\WinFsp')) {
    try {
        $WinFspDir = (Get-ItemProperty -Path $key -ErrorAction Stop).InstallDir
        if ($WinFspDir) { break }
    } catch {
        # Key absent under this view; try the next.
    }
}
if (-not $WinFspDir) {
    throw 'WinFsp is not installed on this machine, so there is no winfsp-x64.dll to ship. Install it from https://winfsp.dev and run this script again.'
}

$WinFspDll = Join-Path $WinFspDir 'bin\winfsp-x64.dll'
Assert-Tool $WinFspDll 'WinFsp is registered but its DLL is missing; try repairing the WinFsp installation.'

Write-Host "    Qt        $QtDir"
Write-Host "    MinGW     $MinGwDir"
Write-Host "    WinFsp    $WinFspDir"
if (-not $StageOnly) { Write-Host "    Inno      $IsccPath" }

# ---------------------------------------------------------------------------
# Version, from the one place that holds it
# ---------------------------------------------------------------------------

$AppPro  = Join-Path $RepoRoot 'app\app.pro'
$ProText = Get-Content -Path $AppPro -Raw

if ($ProText -match '(?m)^\s*VERSION\s*=\s*(\S+)') {
    $Version = $Matches[1]
} else {
    throw "Could not read VERSION from $AppPro"
}

$Stage = ''
if ($ProText -match '(?m)^\s*STAGE\s*=\s*(\S+)') { $Stage = $Matches[1] }

Write-Host "    Version   $Version $Stage".TrimEnd()

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

# Both toolchains go on PATH ahead of anything else: qmake and windeployqt need
# the Qt bin directory, and windeployqt's --compiler-runtime needs the MinGW one
# to find libgcc/libstdc++/libwinpthread.
$env:PATH = "$QtBin;$MinGwBin;$env:PATH"

# lrelease runs before qmake, not as part of it. app.pro carries
# CONFIG += lrelease embed_translations, and on a clean shadow build the .qm
# does not exist yet when rcc wants to embed it.
Write-Stage 'Compiling translations'
Invoke-Native (Join-Path $QtBin 'lrelease.exe') `
    @((Join-Path $RepoRoot 'app\FileDonkey_en_US.ts')) 'lrelease'

Write-Stage 'Building (release)'

if (Test-Path $BuildDir) { Remove-Item -Path $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

Push-Location $BuildDir
try {
    # CONFIG+=release and nothing more. It is tempting to also strip
    # debug_and_release, but that is what puts the build output in release/ and
    # debug/ subdirectories, and app.pro's win32 branch names
    # ../core/release/libcore.a outright. Remove it and libcore.a lands in
    # core/ instead, where the linker will not look for it.
    Invoke-Native (Join-Path $QtBin 'qmake.exe') @(
        (Join-Path $RepoRoot 'FileDonkey.pro'),
        'CONFIG+=release'
    ) 'qmake'

    $jobs = $env:NUMBER_OF_PROCESSORS
    if (-not $jobs) { $jobs = 4 }

    # sub-app, not the default target: the Makefile declares "sub-app: sub-core",
    # so this builds exactly what ships and leaves the test subproject alone.
    # That subproject's mklink step needs Developer Mode enabled, which has
    # nothing to do with packaging and should not be able to fail a release.
    Invoke-Native (Join-Path $MinGwBin 'mingw32-make.exe') @("-j$jobs", 'sub-app') 'mingw32-make'
} finally {
    Pop-Location
}

$BuiltExe = Get-ChildItem -Path (Join-Path $BuildDir 'app') -Filter 'FileDonkey.exe' -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
if (-not $BuiltExe) {
    throw "The build reported success but no FileDonkey.exe was produced under $BuildDir\app."
}
Write-Host "    Built $($BuiltExe.FullName)"

# ---------------------------------------------------------------------------
# Stage
# ---------------------------------------------------------------------------

Write-Stage 'Staging the payload'

if (Test-Path $StageDir) { Remove-Item -Path $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item -Path $BuiltExe.FullName -Destination $StageDir

# windeployqt is pointed at the staged copy so everything it pulls in lands in
# the staging directory and the build tree stays clean. Qt6Svg arrives this way
# rather than as a link-time import: the title bar's caption glyphs go through
# the qsvgicon *plugin*, which is loaded at runtime and so is invisible to the
# .exe's import table.
#
# --no-opengl-sw and --no-system-d3d-compiler drop 25 MB of the 65 MB
# windeployqt otherwise stages, which is 38% of the download for files this app
# cannot reach. Both serve OpenGL: opengl32sw.dll is the software rasteriser Qt
# falls back to when a GL context is requested and the machine has no usable
# driver, and D3Dcompiler_47.dll is left over from ANGLE, which Qt 6 removed.
# FileDonkey is Widgets-only - no QML, no QOpenGLWidget, no QSurfaceFormat
# anywhere in core/ or app/ - so nothing ever asks for a GL context and neither
# file is opened.
#
# If a tester ever reports a black or blank window, put --no-opengl-sw back
# first: that is the symptom it would cause.
#
Invoke-Native (Join-Path $QtBin 'windeployqt.exe') @(
    '--release',
    '--compiler-runtime',
    '--no-opengl-sw',
    '--no-system-d3d-compiler',
    (Join-Path $StageDir 'FileDonkey.exe')
) 'windeployqt'

# winfsp-x64.dll is a load-time import of the .exe (confirmed with objdump -p),
# so the loader resolves it before main() runs and it has to be beside the
# binary. WinFsp does not put its bin directory on PATH, and the delay-load
# route that FspLoad() exists to serve is an MSVC linker feature we do not have
# under MinGW. So: ship it.
Copy-Item -Path $WinFspDll -Destination $StageDir

$payloadBytes = (Get-ChildItem -Path $StageDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
$payloadFiles = (Get-ChildItem -Path $StageDir -Recurse -File | Measure-Object).Count
Write-Host ("    {0} files, {1:N1} MB" -f $payloadFiles, ($payloadBytes / 1MB))

if ($StageOnly) {
    Write-Stage 'Done (staged only)'
    Write-Host "Payload: $StageDir"
    return
}

# ---------------------------------------------------------------------------
# Installer
# ---------------------------------------------------------------------------

Write-Stage 'Building the installer'

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Invoke-Native $IsccPath @(
    "/DMyAppVersion=$Version",
    "/DMyAppStage=$Stage",
    "/DMyStageDir=$StageDir",
    "/DMyRepoRoot=$RepoRoot",
    "/O$OutDir",
    $IssPath
) 'Inno Setup'

Write-Stage 'Done'
Get-ChildItem -Path $OutDir -Filter '*.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | ForEach-Object {
    Write-Host ("Installer: {0} ({1:N1} MB)" -f $_.FullName, ($_.Length / 1MB)) -ForegroundColor Green
}
