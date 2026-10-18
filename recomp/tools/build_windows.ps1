# Bygger Mario Party 2: Recompiled på Windows från din egen ROM.
# Builds Mario Party 2: Recompiled on Windows from your own ROM.
#
#   powershell -ExecutionPolicy Bypass -File recomp\tools\build_windows.ps1 -Rom "C:\Spel\Mario Party 2 (USA).z64"
#
# Enklast: dra ROM-filen och släpp den på recomp\BYGG-WINDOWS.bat.
#
# Kräver: Git, Python 3, Visual Studio 2022 (eller Build Tools) med
# "Desktop development with C++" och "C++ Clang Compiler for Windows".
# Se recomp\BYGGA.md.

param(
    [Parameter(Mandatory = $true)][string]$Rom
)

$ErrorActionPreference = "Stop"

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Green }
function Fail($text) { Write-Host ""; Write-Host "FEL: $text" -ForegroundColor Red; exit 1 }
function Run($exe, [string[]]$arguments) {
    # Programs like pip, git and splat write progress to stderr. Windows
    # PowerShell turns that into errors when output is redirected, so only the
    # exit code decides success here.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & $exe @arguments 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $previous }
    if ($LASTEXITCODE -ne 0) { Fail "$exe avslutades med kod $LASTEXITCODE" }
}

$RecompDir  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepoDir    = (Resolve-Path (Join-Path $RecompDir "..")).Path
$GenDir     = Join-Path $RecompDir "generated"
$ToolsBuild = Join-Path $RecompDir "build-tools"
$GameBuild  = Join-Path $RecompDir "build"

if (-not (Test-Path -LiteralPath $Rom)) { Fail "Hittar inte ROM-filen: $Rom" }
$Rom = (Resolve-Path -LiteralPath $Rom).Path

# --- Verktyg -----------------------------------------------------------------

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Git saknas. Installera från https://git-scm.com" }

$Python = $null
foreach ($candidate in @("py", "python")) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) { $Python = $candidate; break }
}
if (-not $Python) { Fail "Python 3 saknas. Installera från https://www.python.org (kryssa i 'Add to PATH')." }

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "Visual Studio 2022 hittades inte. Se recomp\BYGGA.md." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
if (-not $vsPath) {
    Fail "Visual Studio saknar 'C++ Clang Compiler for Windows'. Öppna Visual Studio Installer -> Ändra -> Enskilda komponenter och kryssa i den."
}

# Hämta Visual Studios utvecklarmiljö (clang-cl, cmake, ninja, Windows SDK) in i den här sessionen.
$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
$envDump = & cmd.exe /s /c "`"$devCmd`" -arch=amd64 -host_arch=amd64 -no_logo >nul && set"
foreach ($line in $envDump) {
    $i = $line.IndexOf("=")
    if ($i -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1), "Process") }
}
foreach ($tool in @("clang-cl", "cmake", "ninja")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "'$tool' hittades inte i Visual Studio-miljön." }
}

# --- 1. Submoduler -------------------------------------------------------------

Step "1/6 Hämtar submoduler (första gången tar det en stund)"
Run git @("-C", $RepoDir, "submodule", "update", "--init", "--recursive", "recomp/lib")

# --- 2. Python ---------------------------------------------------------------

Step "2/6 Python-miljö"
$VenvPy = Join-Path $RecompDir ".venv\Scripts\python.exe"
if (-not (Test-Path $VenvPy)) {
    if ($Python -eq "py") { Run py @("-3", "-m", "venv", (Join-Path $RecompDir ".venv")) }
    else { Run python @("-m", "venv", (Join-Path $RecompDir ".venv")) }
}
Run $VenvPy @("-m", "pip", "install", "--quiet", "--upgrade", "pip")
Run $VenvPy @("-m", "pip", "install", "--quiet", "-r", (Join-Path $RecompDir "requirements.txt"))

# --- 3. Symboler ---------------------------------------------------------------

Step "3/6 Läser funktioner ur ROM:en med splat"
$symArgs = @((Join-Path $RecompDir "tools\gen_syms.py"), "--rom", $Rom, "--out", $GenDir)
if ($env:MP2_TEST_ROM -eq "1") { $symArgs += "--allow-wrong-rom" }  # CI: fake ROM
Run $VenvPy $symArgs

# --- 4. N64Recomp --------------------------------------------------------------

Step "4/6 Bygger och kör N64Recomp"
Run cmake @("-S", (Join-Path $RecompDir "lib\N64ModernRuntime\N64Recomp"), "-B", $ToolsBuild, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl")
Run cmake @("--build", $ToolsBuild, "--target", "N64Recomp", "RSPRecomp")
$funcsDir = Join-Path $GenDir "RecompiledFuncs"
if (Test-Path $funcsDir) { Remove-Item -Recurse -Force $funcsDir }
Run (Join-Path $ToolsBuild "N64Recomp.exe") @((Join-Path $RecompDir "config\mp2.us.toml"))
Run (Join-Path $ToolsBuild "RSPRecomp.exe") @((Join-Path $RecompDir "config\aspMain.us.toml"))

# --- 5. Deklarationer ----------------------------------------------------------

Step "5/6 Deklarationer"
Run $VenvPy @((Join-Path $RecompDir "tools\gen_decls.py"), "--funcs", $funcsDir)

# --- 6. Spelet -----------------------------------------------------------------

Step "6/6 Bygger spelet (det här tar längst tid)"
Run cmake @("-S", $RecompDir, "-B", $GameBuild, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl")
Run cmake @("--build", $GameBuild, "--target", "mp2-recomp")

Write-Host ""
Write-Host "Klart!" -ForegroundColor Green
Write-Host "Spelet ligger i: $GameBuild\mp2-recomp.exe"
Write-Host "Första gången: välj din ROM i startmenyn ('Välj ROM')."
