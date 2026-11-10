# ============================================================================
# Script        : build_module.ps1
# Target        : com.netease.dbdena (ELF64 AArch64 / arm64-v8a)
# Artifact      : modulos\ue4_instrument_zygisk.zip
# ============================================================================
$ErrorActionPreference = "Stop"

# Adiciona CMake e Ninja locais ao PATH se necessario
if (Test-Path "C:\Program Files\CMake\bin") {
    $env:PATH = "C:\Program Files\CMake\bin;$env:PATH"
}
if (Test-Path "$PSScriptRoot\tools_bin") {
    $env:PATH = "$PSScriptRoot\tools_bin;$env:PATH"
}

if (-not $env:ANDROID_NDK_HOME) {
    $DefaultNdk = "C:\Android\ndk\android-ndk-r27c"
    if (Test-Path $DefaultNdk) {
        $env:ANDROID_NDK_HOME = $DefaultNdk
    } else {
        throw "Variavel de ambiente ANDROID_NDK_HOME nao definida."
    }
}

$BuildDir  = "build-arm64"
$ModDir    = "modulos"
$StageDir  = "$ModDir\ue4_instrument_zygisk"
$ZipOut    = "$ModDir\ue4_instrument_zygisk.zip"
$CoreSo    = Join-Path $BuildDir "libinstrument_core.so"
$DiagSo    = Join-Path $BuildDir "libue4_diagnostics.so"
$ZygiskSo  = Join-Path $BuildDir "libzygisk_loader.so"
$LlvmBin   = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin"
$LlvmNm    = Join-Path $LlvmBin "llvm-nm.exe"
$LlvmStrip = Join-Path $LlvmBin "llvm-strip.exe"

Write-Host "[1/6] Compilando artefatos ELF64 AArch64 (Release)..." -ForegroundColor Cyan
cmake -S . -B $BuildDir `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

cmake --build $BuildDir --config Release --parallel

Write-Host "[2/6] Auditando e limpando simbolos dinamicos via llvm-strip / llvm-nm..." -ForegroundColor Cyan
& $LlvmStrip --strip-unneeded $CoreSo
& $LlvmNm -gD --defined-only $CoreSo

Write-Host "[3/6] Montando estrutura de diretorios do modulo Magisk/Zygisk..." -ForegroundColor Cyan
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
if (Test-Path $ZipOut)   { Remove-Item -Force $ZipOut }

New-Item -ItemType Directory -Force -Path "$StageDir\META-INF\com\google\android" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\system\lib64"                | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\zygisk"                      | Out-Null

$ModuleProp = @"
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
"@
[System.IO.File]::WriteAllText("$StageDir\module.prop", ($ModuleProp -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

$CustomizeSh = @"
SKIPUNZIP=0

if [ "`$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "`$MODPATH" 0 0 0755 0644
set_perm "`$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "`$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
"@
[System.IO.File]::WriteAllText("$StageDir\customize.sh", ($CustomizeSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\updater-script", "#MAGISK`n", [System.Text.UTF8Encoding]::new($false))

$UpdateBinary = @"
#!/sbin/sh
umask 022
ui_print() { echo "`$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=`$2
ZIPFILE=`$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ `$MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
"@
[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\update-binary", ($UpdateBinary -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

Copy-Item -Force $CoreSo "$ModDir\libinstrument_core.so"
if (Test-Path $DiagSo) {
    & $LlvmStrip --strip-unneeded $DiagSo
    Copy-Item -Force $DiagSo "$ModDir\libue4_diagnostics.so"
}
Copy-Item -Force $CoreSo "$StageDir\system\lib64\libinstrument_core.so"
if (Test-Path $ZygiskSo) {
    & $LlvmStrip --strip-unneeded $ZygiskSo
    Copy-Item -Force $ZygiskSo "$StageDir\zygisk\arm64-v8a.so"
} else {
    Copy-Item -Force $CoreSo "$StageDir\zygisk\arm64-v8a.so"
}

Write-Host "[4/6] Empacotando arquivo ZIP com separadores POSIX (/)..." -ForegroundColor Cyan
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ResolvedStage = (Resolve-Path $StageDir).Path
$ZipFullPath   = Join-Path (Get-Location).Path $ZipOut
$ZipStream     = [System.IO.File]::Open($ZipFullPath, [System.IO.FileMode]::Create)
try {
    $ZipArchive = New-Object System.IO.Compression.ZipArchive($ZipStream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        Get-ChildItem -Path $ResolvedStage -Recurse -File | ForEach-Object {
            $RelPath = $_.FullName.Substring($ResolvedStage.Length).TrimStart('\', '/') -replace '\\', '/'
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $ZipArchive,
                $_.FullName,
                $RelPath,
                [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    } finally {
        $ZipArchive.Dispose()
    }
} finally {
    $ZipStream.Dispose()
}

Write-Host "[5/6] Verificando conexao ADB para deploy..." -ForegroundColor Cyan
$AdbDevices = (& adb devices) -join "`n"
if ($AdbDevices -match "\bdevice\b") {
    & adb push $ZipOut /data/local/tmp/ue4_instrument_zygisk.zip
    & adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
    Write-Host "[6/6] Modulo instalado com sucesso. Execute 'adb reboot' para ativar." -ForegroundColor Green
} else {
    Write-Host "[6/6] Arquivo gerado em $ZipOut (nenhum dispositivo ADB online)." -ForegroundColor Yellow
}

