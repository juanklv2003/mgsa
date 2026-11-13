# mgsa bring-up: build the recompiler, generate the corpus, build and run the host.
#
#   pwsh -File scripts/bringup.ps1            # full pipeline
#   pwsh -File scripts/bringup.ps1 -RunOnly   # just rebuild and run the host
#
# Requirements: MSYS2 UCRT64 (g++, cmake, ninja) at C:\msys64\ucrt64, Python 3, git.

[CmdletBinding()]
param(
    [switch]$RunOnly,
    [string]$Elf = "extract/PSP_GAME/SYSDIR/BOOT.BIN",
    [int]$Max = 20000000
)

$ErrorActionPreference = "Stop"

$repo      = Split-Path -Parent $PSScriptRoot
$toolRoot  = Join-Path $repo "work/tools/Yakumo"
$profile   = Join-Path $repo "profiles/mgsa"
$buildDir  = Join-Path $toolRoot "out/mgsa"
$ucrt      = "C:\msys64\ucrt64\bin"

if (-not (Test-Path $ucrt)) { throw "MSYS2 UCRT64 toolchain not found at $ucrt" }
$env:PATH = "$ucrt;$env:PATH"

if (-not $RunOnly) {
    if (-not (Test-Path $toolRoot)) {
        Write-Host "==> cloning Yakumo"
        git clone --depth 1 https://github.com/TeamGDB/Yakumo.git $toolRoot
    }

    # The vendored tree is patched, never forked: every local framework change lives in patches/
    # so a clean clone reaches the same build. A patch already in place fails `--check`, which is
    # the normal case on a second run.
    foreach ($patch in (Get-ChildItem (Join-Path $repo "patches") -Filter *.patch -ErrorAction SilentlyContinue | Sort-Object Name)) {
        & git -C $toolRoot apply --check $patch.FullName 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "==> applying $($patch.Name)"
            & git -C $toolRoot apply $patch.FullName
        } else {
            Write-Host "==> $($patch.Name) is already applied"
        }
    }

    $framework = Join-Path $toolRoot "out/framework"
    if (-not (Test-Path $framework)) {
        Write-Host "==> configuring framework"
        cmake -S $toolRoot -B $framework -G Ninja -DPSPRECOMP_PROFILE="" `
              -DPSPRECOMP_BUILD_TESTS=OFF -DPSPRECOMP_BUILD_PROFILE_TESTS=OFF `
              -DCMAKE_BUILD_TYPE=Release
    }
    Write-Host "==> building framework tools"
    cmake --build $framework --target psp_analyze psp_recomp

    Write-Host "==> installing profile into the framework tree"
    $target = Join-Path $toolRoot "profiles/mgsa"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item (Join-Path $profile "CMakeLists.txt") $target -Force
    $hostDir = Join-Path $target "host"
    New-Item -ItemType Directory -Force -Path $hostDir | Out-Null
    Copy-Item (Join-Path $profile "host/main.cpp") $hostDir -Force

    $elfPath = Join-Path $repo $Elf
    if (-not (Test-Path $elfPath)) { throw "missing game executable at $elfPath" }

    Write-Host "==> analysing"
    & (Join-Path $framework "psp_analyze.exe") $elfPath (Join-Path $repo "work/mgsa-report.json")

    Write-Host "==> generating AOT corpus"
    & (Join-Path $framework "psp_recomp.exe") $elfPath --auto (Join-Path $target "generated")

    if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
        Write-Host "==> configuring mgsa profile"
        cmake -S $toolRoot -B $buildDir -G Ninja -DPSPRECOMP_PROFILE=mgsa `
              -DPSPRECOMP_BUILD_TESTS=OFF -DPSPRECOMP_BUILD_PROFILE_TESTS=OFF `
              -DCMAKE_BUILD_TYPE=Release
    }
}

Write-Host "==> building mgsa_boot"
cmake --build $buildDir --target mgsa_boot

$exe = Join-Path $buildDir "bin/mgsa_boot.exe"
Write-Host "==> running $exe"
$env:PSPRECOMP_HLE_HISTOGRAM = "1"
Push-Location $repo
try {
    & $exe --elf $Elf --max $Max
} finally {
    Pop-Location
}
