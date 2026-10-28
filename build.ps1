# 一键构建脚本：从 whisper.cpp 源码编译 main64.exe
#
# 用法：
#   1. 将 whisper.cpp 源码（含 ggml 子模块）放到本目录下的 whisper.cpp/ 中
#   2. 将 w64devkit 解压到 tools/w64devkit/ 中，或通过 -W64Devkit 参数指定路径
#   3. 执行： powershell -ExecutionPolicy Bypass -File build.ps1
#
# 参数：
#   -WhisperDir    whisper.cpp 源码目录，默认 .\whisper.cpp
#   -W64Devkit     w64devkit 的 bin 目录，默认 .\tools\w64devkit\bin
#   -Arch          目标指令集，默认 ivybridge（SSE4.2 + AVX + F16C）

param(
    [string]$WhisperDir = "$PSScriptRoot\whisper.cpp",
    [string]$W64Devkit  = "$PSScriptRoot\tools\w64devkit\bin",
    [string]$Arch       = "ivybridge",
    [string]$OutDir     = "$PSScriptRoot\build"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path "$W64Devkit\g++.exe")) {
    throw "g++.exe not found. Check -W64Devkit: $W64Devkit"
}
if (-not (Test-Path "$WhisperDir\src\whisper.cpp")) {
    throw "whisper.cpp source not found. Check -WhisperDir: $WhisperDir"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$GXX = "$W64Devkit\g++.exe"
$GCC = "$W64Devkit\gcc.exe"
if (-not (Test-Path $GCC)) { throw "gcc.exe not found in $W64Devkit" }

$COMMON = @(
    "-O2", "-march=$Arch", "-mavx", "-mf16c", "-DNDEBUG",
    "-DGGML_USE_CPU=1",
    "-UWHISPER_USE_COREML", "-UWHISPER_USE_VULKAN", "-UWHISPER_USE_CUDA",
    "-include", "$PSScriptRoot\include\whisper-version.h",
    "-I$WhisperDir\include",
    "-I$WhisperDir\ggml\include",
    "-I$WhisperDir\ggml\src",
    "-I$WhisperDir\ggml\src\ggml-cpu",
    "-I$WhisperDir\ggml\src\ggml-cpu\tiled",
    "-I$WhisperDir\src",
    "-I$PSScriptRoot\include",
    "-I$PSScriptRoot\include\ggml"
)

$SOURCES = [ordered]@{
    "whisper"        = "$WhisperDir\src\whisper.cpp"
    "ggml"           = "$WhisperDir\ggml\src\ggml.c"
    "ggml-cpp"       = "$WhisperDir\ggml\src\ggml.cpp"
    "ggml-alloc"     = "$WhisperDir\ggml\src\ggml-alloc.c"
    "ggml-quants"    = "$WhisperDir\ggml\src\ggml-quants.c"
    "ggml-backend"   = "$WhisperDir\ggml\src\ggml-backend.cpp"
    "ggml-backend-meta" = "$WhisperDir\ggml\src\ggml-backend-meta.cpp"
    "ggml-backend-reg"  = "$WhisperDir\ggml\src\ggml-backend-reg.cpp"
    "ggml-backend-dl"   = "$WhisperDir\ggml\src\ggml-backend-dl.cpp"
    "ggml-opt"       = "$WhisperDir\ggml\src\ggml-opt.cpp"
    "ggml-threading" = "$WhisperDir\ggml\src\ggml-threading.cpp"
    "gguf"           = "$WhisperDir\ggml\src\gguf.cpp"
    "cpu-entry"      = "$WhisperDir\ggml\src\ggml-cpu\ggml-cpu.cpp"
    "cpu-core"       = "$WhisperDir\ggml\src\ggml-cpu\ggml-cpu.c"
    "cpu-ops"        = "$WhisperDir\ggml\src\ggml-cpu\ops.cpp"
    "cpu-bin"        = "$WhisperDir\ggml\src\ggml-cpu\binary-ops.cpp"
    "cpu-uni"        = "$WhisperDir\ggml\src\ggml-cpu\unary-ops.cpp"
    "cpu-vec"        = "$WhisperDir\ggml\src\ggml-cpu\vec.cpp"
    "cpu-quants"     = "$WhisperDir\ggml\src\ggml-cpu\quants.c"
    "cpu-traits"     = "$WhisperDir\ggml\src\ggml-cpu\traits.cpp"
    "cpu-hbm"        = "$WhisperDir\ggml\src\ggml-cpu\hbm.cpp"
    "arch-x86-quants"= "$WhisperDir\ggml\src\ggml-cpu\arch\x86\quants.c"
    "arch-x86-feats" = "$WhisperDir\ggml\src\ggml-cpu\arch\x86\cpu-feats.cpp"
    "tiled"          = "$WhisperDir\ggml\src\ggml-cpu\tiled\tiled.cpp"
    "tiled-kernel"   = "$WhisperDir\ggml\src\ggml-cpu\tiled\tiled-kernel.cpp"
    "aneforge"       = "$WhisperDir\src\aneforge\whisper-aneforge.cpp"
}

Write-Host "[1/3] Compiling objects" -ForegroundColor Cyan
foreach ($name in $SOURCES.Keys) {
    $src = $SOURCES[$name]
    if (-not (Test-Path $src)) {
        Write-Host "  skip (source not found): $name" -ForegroundColor Yellow
        continue
    }
    $obj = "$OutDir\$name.o"
    $compiler = if ($src.EndsWith(".c")) { $GCC } else { $GXX }
    Write-Host "  $name"
    & $compiler @COMMON -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $name" }
}

Write-Host "[2/3] Collecting objects" -ForegroundColor Cyan
$allObjs = @()
foreach ($name in $SOURCES.Keys) {
    $o = "$OutDir\$name.o"
    if (Test-Path $o) { $allObjs += $o }
}
Write-Host "  $($allObjs.Count) objects"

Write-Host "[3/3] Linking main64.exe" -ForegroundColor Cyan
$exe = "$OutDir\main64.exe"
Remove-Item $exe -Force -EA SilentlyContinue
$preLink = @(
    "-O2", "-march=$Arch", "-mavx", "-mf16c", "-DNDEBUG",
    "-DGGML_USE_CPU=1",
    "-UWHISPER_USE_COREML", "-UWHISPER_USE_VULKAN", "-UWHISPER_USE_CUDA",
    "-include", "$PSScriptRoot\include\whisper-version.h",
    "-I$WhisperDir\include", "-I$WhisperDir\ggml\include",
    "-I$PSScriptRoot\include", "-I$PSScriptRoot\include\ggml", "-I$PSScriptRoot\src",
    "$PSScriptRoot\src\cpu_engine.cpp",
    "-Wl,--start-group"
)
$postLink = @(
    "-Wl,--end-group",
    "-static", "-o", $exe,
    "-lshlwapi", "-lole32", "-lrpcrt4", "-lws2_32"
)
$LINK = $preLink + $allObjs + $postLink
& $GXX @LINK
if ($LASTEXITCODE -ne 0) { throw "Link failed" }

$size = (Get-Item $exe).Length
Write-Host ""
Write-Host "BUILD OK" -ForegroundColor Green
Write-Host "  output: $exe"
Write-Host "  size  : $size bytes ($([math]::Round($size/1MB,2)) MB)"