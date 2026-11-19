# Build WWE '13 PC (port/, ReXGlue SDK 0.10) from a plain PowerShell.
#
#   .\port\tools\build.ps1                       # configure + build port\out\build\win-amd64-release
#   .\port\tools\build.ps1 -SdkDir D:\rexglue-sdk
#
# Uses the installed SDK of WWE 2K15 PC / WWE '12 PC (ReXGlue c94f5eb + their patches, installed in
# <SdkDir>\out\install\ninja-clangcl). This script never modifies or rebuilds the SDK: it is shared.
# Always build through this script: a bare "cmake -S . -B" can cache empty release flags (slow exe).
#   .\port\tools\build.ps1 -Native               # also the native renderer (re:Blue + plume):
#       wwe13_native.exe (Vulkan), and with the game's shaders dumped in work\shaders\xsc
#       (WWE13_DUMP_SHADERS) wwe13_d3d12.exe and wwe13_d3d11.exe, their shaders built in (as WWE '12 PC).
#       First prepares work\native-deps (patched copy of plume / XenosRecomp: prepare_native_deps.ps1).
param(
  [string]$SdkDir = "C:\Dev\rexglue-sdk",
  [switch]$Native
)
$ErrorActionPreference = "Stop"
$port = Split-Path -Parent $PSScriptRoot
$install = Join-Path $SdkDir "out\install\ninja-clangcl"
if (-not (Test-Path (Join-Path $install "bin\rexruntime.dll"))) { throw "SDK install not found: $install" }

# MSVC environment (Windows SDK, linker) + LLVM clang-cl.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$env:ProgramFiles\LLVM\bin;" + (Split-Path $vswhere) + ";$env:PATH"

Push-Location $port
try {
  if (-not (Test-Path "assets\default.xex")) { throw "port\assets must link to your WWE '13 folder (default.xex + default.xexp)" }
  if ($Native) { & (Join-Path $PSScriptRoot "prepare_native_deps.ps1") }
  $i = $install -replace '\\', '/'
  cmake --preset win-amd64-release "-DCMAKE_PREFIX_PATH=$i" "-DSDL3_DIR=$i/cmake" `
    "-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG" "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG" `
    "-DWWE13_NATIVE_RENDERER=$(if ($Native) { 'ON' } else { 'OFF' })"
  if ($LASTEXITCODE) { throw "configure failed" }
  cmake --build --preset win-amd64-release; if ($LASTEXITCODE) { throw "build failed" }
  # The game build does not re-copy SDK DLLs when only the SDK changed.
  Copy-Item (Join-Path $install "bin\rexruntime.dll"), (Join-Path $install "bin\rexgpu-xenos.dll") `
    "out\build\win-amd64-release\" -Force
} finally { Pop-Location }
