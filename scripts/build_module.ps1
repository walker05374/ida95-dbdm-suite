# ============================================================================
# Script        : build_module.ps1
# Target        : com.netease.dbdena (ELF64 AArch64 / arm64-v8a)
# Layout        : app/ | jni/ (core, telemetry, hud) | zygisk/ | scripts/ | modulos/
# Artifacts     : modulos\ue4_instrument_zygisk.zip
#                 modulos\DBDM_FloatingModMenu.apk
# ============================================================================
$ErrorActionPreference = "Stop"

# Resolve workspace root whether invoked from root or scripts/
$WorkspaceRoot = $PSScriptRoot
if ((Split-Path -Leaf $WorkspaceRoot) -eq "scripts") {
    $WorkspaceRoot = Split-Path -Parent $WorkspaceRoot
}
Set-Location $WorkspaceRoot

# 1. Configure CMake, Ninja, NDK, JDK 17, Android Build-Tools, and ADB
if (Test-Path "C:\Program Files\CMake\bin") {
    $env:PATH = "C:\Program Files\CMake\bin;$env:PATH"
}
if (Test-Path "E:\android-sdk\cmake\3.22.1\bin") {
    $env:PATH = "E:\android-sdk\cmake\3.22.1\bin;$env:PATH"
}
if (Test-Path "$WorkspaceRoot\scripts\tools_bin") {
    $env:PATH = "$WorkspaceRoot\scripts\tools_bin;$env:PATH"
} elseif (Test-Path "$WorkspaceRoot\tools_bin") {
    $env:PATH = "$WorkspaceRoot\tools_bin;$env:PATH"
}

if (-not $env:ANDROID_NDK_HOME) {
    $CandidateNdks = @(
        "C:\Android\ndk\android-ndk-r27c",
        "E:\android-sdk\ndk\27.2.12479018"
    )
    foreach ($Candidate in $CandidateNdks) {
        if (Test-Path $Candidate) {
            $env:ANDROID_NDK_HOME = $Candidate
            break
        }
    }
    if (-not $env:ANDROID_NDK_HOME) {
        throw "Variavel de ambiente ANDROID_NDK_HOME nao definida."
    }
}

$AdbExe = "adb"
if (Test-Path "C:\Users\walker\Desktop\anydesk\tools\android-sdk\platform-tools\adb.exe") {
    $AdbExe = "C:\Users\walker\Desktop\anydesk\tools\android-sdk\platform-tools\adb.exe"
} elseif (Test-Path "C:\Users\Walker\Downloads\platform-tools\adb.exe") {
    $AdbExe = "C:\Users\Walker\Downloads\platform-tools\adb.exe"
} elseif (Test-Path "E:\android-sdk\platform-tools\adb.exe") {
    $AdbExe = "E:\android-sdk\platform-tools\adb.exe"
}

$AppDir     = "app"
$JniDir     = "jni"
$ZygiskDir  = "zygisk"
$BuildDir   = "build-arm64"
$ModDir     = "modulos"
$StageDir   = "$ModDir\ue4_instrument_zygisk"
$ZipOut     = "$ModDir\ue4_instrument_zygisk.zip"
$ApkOut     = "$ModDir\DBDM_FloatingModMenu.apk"
$WdbdApkOut = "$ModDir\wdbd.apk"
$CoreSo    = Join-Path $BuildDir "libinstrument_core.so"
$DiagSo    = Join-Path $BuildDir "libue4_diagnostics.so"
$ZygiskSo  = Join-Path $BuildDir "libzygisk_loader.so"
$LlvmBin   = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin"
$LlvmNm    = Join-Path $LlvmBin "llvm-nm.exe"
$LlvmStrip = Join-Path $LlvmBin "llvm-strip.exe"

# Mirror structured jni/ sources into modulos/ue4_instrument_zygisk and app/ into modulos/floating_mod_menu_apk
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
Copy-Item -Force "CMakeLists.txt" "$StageDir\CMakeLists.txt"
foreach ($SubDir in @("$JniDir\core", "$JniDir\telemetry", "$JniDir\hud")) {
    if (Test-Path $SubDir) {
        Get-ChildItem -Path $SubDir -File | ForEach-Object {
            Copy-Item -Force $_.FullName (Join-Path $StageDir $_.Name)
        }
    }
}
if (Test-Path $AppDir) {
    New-Item -ItemType Directory -Force -Path "$ModDir\floating_mod_menu_apk\src\com\dbdm\modmenu" | Out-Null
    New-Item -ItemType Directory -Force -Path "$ModDir\floating_mod_menu_apk\res\layout" | Out-Null
    Copy-Item -Force "$AppDir\AndroidManifest.xml" "$ModDir\floating_mod_menu_apk\AndroidManifest.xml" -ErrorAction SilentlyContinue
    Copy-Item -Force "$AppDir\src\com\dbdm\modmenu\*.java" "$ModDir\floating_mod_menu_apk\src\com\dbdm\modmenu\" -ErrorAction SilentlyContinue
    Copy-Item -Force "$AppDir\res\layout\*.xml" "$ModDir\floating_mod_menu_apk\res\layout\" -ErrorAction SilentlyContinue
}
if ((Test-Path "$JniDir\third_party\imgui") -and -not (Test-Path "$StageDir\third_party\imgui\imgui.h")) {
    New-Item -ItemType Directory -Force -Path "$StageDir\third_party\imgui\backends" | Out-Null
    Copy-Item -Force "$JniDir\third_party\imgui\*" "$StageDir\third_party\imgui\" -ErrorAction SilentlyContinue
    Copy-Item -Force "$JniDir\third_party\imgui\backends\*" "$StageDir\third_party\imgui\backends\" -ErrorAction SilentlyContinue
}

Write-Host "[0/6] Executando limpeza profunda de caches de build (build-arm64/ e app/build/)..." -ForegroundColor Cyan
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
foreach ($CleanDir in @("$AppDir\build", "$AppDir\.gradle", "$ModDir\floating_mod_menu_apk\build")) {
    if (Test-Path $CleanDir) {
        Remove-Item -Recurse -Force $CleanDir
    }
}
if (Test-Path ".\gradlew.bat") {
    & .\gradlew.bat clean
} elseif (Test-Path "$AppDir\gradlew.bat") {
    Push-Location $AppDir
    try { & .\gradlew.bat clean } finally { Pop-Location }
}

Write-Host "[1/6] Compilando artefatos ELF64 AArch64 (Release) a partir de jni/..." -ForegroundColor Cyan
cmake -S . -B $BuildDir `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

cmake --build $BuildDir --config Release --clean-first --parallel

Write-Host "[2/6] Auditando e limpando simbolos dinamicos via llvm-strip / llvm-nm..." -ForegroundColor Cyan
& $LlvmStrip --strip-unneeded $CoreSo
& $LlvmNm -gD --defined-only $CoreSo

Write-Host "[3/6] Montando estrutura de diretorios do modulo Magisk/Zygisk a partir de zygisk/..." -ForegroundColor Cyan
if (Test-Path $ZipOut) { Remove-Item -Force $ZipOut }

New-Item -ItemType Directory -Force -Path "$StageDir\META-INF\com\google\android" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\system\lib64"                | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\zygisk"                      | Out-Null
New-Item -ItemType Directory -Force -Path "$ZygiskDir\META-INF\com\google\android"| Out-Null

$ModuleProp = @"
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev9
versionCode=109
author=SystemsEngineering
description=Headless UI-free ELF64 AArch64 telemetry & IPC backend for com.netease.dbdena (UE 4.27)
"@
[System.IO.File]::WriteAllText("$ZygiskDir\module.prop", ($ModuleProp -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
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
set_perm "`$MODPATH/service.sh" 0 0 0755
"@
[System.IO.File]::WriteAllText("$ZygiskDir\customize.sh", ($CustomizeSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText("$StageDir\customize.sh", ($CustomizeSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

$ServiceSh = @"
#!/system/bin/sh
MODDIR=`${0%/*}
chmod 1777 /data/local/tmp 2>/dev/null
touch /data/local/tmp/dbdm_mod_config.cfg /data/local/tmp/dbdm_telemetry_shm.bin 2>/dev/null
chmod 0666 /data/local/tmp/dbdm_mod_config.cfg /data/local/tmp/dbdm_telemetry_shm.bin 2>/dev/null
if [ -x /data/local/tmp/dbdm_telemetry_daemon ]; then
  killall -9 dbdm_telemetry_daemon 2>/dev/null
  /data/local/tmp/dbdm_telemetry_daemon --daemon
fi
"@
[System.IO.File]::WriteAllText("$ZygiskDir\service.sh", ($ServiceSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText("$StageDir\service.sh", ($ServiceSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

[System.IO.File]::WriteAllText("$ZygiskDir\META-INF\com\google\android\updater-script", "#MAGISK`n", [System.Text.UTF8Encoding]::new($false))
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
[System.IO.File]::WriteAllText("$ZygiskDir\META-INF\com\google\android\update-binary", ($UpdateBinary -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
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
$PkgFiles = @(
    "module.prop",
    "customize.sh",
    "service.sh",
    "META-INF/com/google/android/updater-script",
    "META-INF/com/google/android/update-binary",
    "system/lib64/libinstrument_core.so",
    "zygisk/arm64-v8a.so"
)
try {
    $ZipArchive = New-Object System.IO.Compression.ZipArchive($ZipStream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        foreach ($RelPath in $PkgFiles) {
            $FullPath = Join-Path $ResolvedStage ($RelPath -replace '/', '\')
            if (Test-Path $FullPath) {
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $ZipArchive,
                    $FullPath,
                    $RelPath,
                    [System.IO.Compression.CompressionLevel]::Optimal
                ) | Out-Null
            }
        }
    } finally {
        $ZipArchive.Dispose()
    }
} finally {
    $ZipStream.Dispose()
}

Write-Host "[5/6] Compilando e assinando Standalone Floating Mod Menu APK a partir de app/ ($WdbdApkOut / $ApkOut)..." -ForegroundColor Cyan
$JavaHomeCandidates = @(
    "C:\Program Files\Java\jdk-21.0.12.1",
    "C:\Program Files\Microsoft\jdk-17.0.20.101-hotspot"
)
$ResolvedJavaHome = $env:JAVA_HOME
foreach ($Cand in $JavaHomeCandidates) {
    if (Test-Path "$Cand\bin\javac.exe") {
        $ResolvedJavaHome = $Cand
        break
    }
}
$JavacExe   = Join-Path $ResolvedJavaHome "bin\javac.exe"
$KeytoolExe = Join-Path $ResolvedJavaHome "bin\keytool.exe"

$BuildToolsCandidates = @(
    "C:\Users\walker\Desktop\anydesk\tools\android-sdk\build-tools\35.0.0",
    "E:\android-sdk\build-tools\34.0.0"
)
$BuildTools = $BuildToolsCandidates[0]
foreach ($Cand in $BuildToolsCandidates) {
    if (Test-Path $Cand) {
        $BuildTools = $Cand
        break
    }
}

$AndroidJarCandidates = @(
    "C:\Users\walker\Desktop\anydesk\tools\android-sdk\platforms\android-35\android.jar",
    "E:\android-sdk\platforms\android-34\android.jar"
)
$AndroidJar = $AndroidJarCandidates[0]
foreach ($Cand in $AndroidJarCandidates) {
    if (Test-Path $Cand) {
        $AndroidJar = $Cand
        break
    }
}

if ((Test-Path $JavacExe) -and (Test-Path $BuildTools) -and (Test-Path $AndroidJar)) {
    $env:JAVA_HOME = $ResolvedJavaHome
    $env:PATH      = "$env:JAVA_HOME\bin;$env:PATH"

    $ApkSrcDir   = $AppDir
    $ApkBuildDir = "$BuildDir\apk_build"
    if (Test-Path $ApkBuildDir) { Remove-Item -Recurse -Force $ApkBuildDir }
    New-Item -ItemType Directory -Force -Path "$ApkBuildDir\classes" | Out-Null
    New-Item -ItemType Directory -Force -Path "$ApkBuildDir\dex"     | Out-Null

    & $JavacExe -encoding UTF-8 --release 11 -cp $AndroidJar -d "$ApkBuildDir\classes" `
        "$ApkSrcDir\src\com\dbdm\modmenu\MainActivity.java" `
        "$ApkSrcDir\src\com\dbdm\modmenu\FloatingModService.java"

    $ClassFiles = (Get-ChildItem -Path "$ApkBuildDir\classes" -Recurse -Filter "*.class").FullName
    & "$BuildTools\d8.bat" --lib $AndroidJar --min-api 26 --output "$ApkBuildDir\dex" $ClassFiles

    $ResZip = "$ApkBuildDir\res.zip"
    if (Test-Path "$ApkSrcDir\res") {
        & "$BuildTools\aapt2.exe" compile --dir "$ApkSrcDir\res" -o $ResZip
    }

    $UnalignedApk = "$ApkBuildDir\unaligned.apk"
    if (Test-Path $ResZip) {
        & "$BuildTools\aapt2.exe" link -o $UnalignedApk `
            -I $AndroidJar `
            --manifest "$ApkSrcDir\AndroidManifest.xml" `
            --auto-add-overlay `
            -R $ResZip
    } else {
        & "$BuildTools\aapt2.exe" link -o $UnalignedApk `
            -I $AndroidJar `
            --manifest "$ApkSrcDir\AndroidManifest.xml"
    }

    # Inject classes.dex, assets/wdbd_logo_circle.png, and lib/arm64-v8a/libinstrument_core.so into APK
    $UnalignedFullPath = (Resolve-Path $UnalignedApk).Path
    $ApkArchive = [System.IO.Compression.ZipFile]::Open($UnalignedFullPath, [System.IO.Compression.ZipArchiveMode]::Update)
    try {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $ApkArchive,
            (Resolve-Path "$ApkBuildDir\dex\classes.dex").Path,
            "classes.dex",
            [System.IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
        foreach ($AssetFile in @("wdbd_logo_circle.png", "logo.jpg")) {
            $AssetPath = "$ApkSrcDir\assets\$AssetFile"
            if (Test-Path $AssetPath) {
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $ApkArchive,
                    (Resolve-Path $AssetPath).Path,
                    "assets/$AssetFile",
                    [System.IO.Compression.CompressionLevel]::NoCompression
                ) | Out-Null
            }
        }
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $ApkArchive,
            (Resolve-Path $CoreSo).Path,
            "lib/arm64-v8a/libinstrument_core.so",
            [System.IO.Compression.CompressionLevel]::NoCompression
        ) | Out-Null
        $OverlayUiSo = Join-Path $BuildDir "libdbdm_overlay_ui.so"
        if (Test-Path $OverlayUiSo) {
            & $LlvmStrip --strip-unneeded $OverlayUiSo
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $ApkArchive,
                (Resolve-Path $OverlayUiSo).Path,
                "lib/arm64-v8a/libdbdm_overlay_ui.so",
                [System.IO.Compression.CompressionLevel]::NoCompression
            ) | Out-Null
        }
    } finally {
        $ApkArchive.Dispose()
    }

    $AlignedApk = "$ApkBuildDir\aligned.apk"
    & "$BuildTools\zipalign.exe" -f -p 4 $UnalignedApk $AlignedApk

    $KeystorePath = "$ModDir\debug.keystore"
    if (-not (Test-Path $KeystorePath)) {
        $PrevEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $KeytoolExe -genkeypair -keystore $KeystorePath -storepass android -alias androiddebugkey `
            -keypass android -keyalg RSA -keysize 2048 -validity 10000 `
            -dname "CN=Android Debug,O=Android,C=US" *> $null
        $ErrorActionPreference = $PrevEap
    }

    if (Test-Path $ApkOut)     { Remove-Item -Force $ApkOut }
    if (Test-Path $WdbdApkOut) { Remove-Item -Force $WdbdApkOut }
    & "$BuildTools\apksigner.bat" sign --ks $KeystorePath --ks-pass pass:android --key-pass pass:android `
        --out $WdbdApkOut $AlignedApk
    Copy-Item -Force $WdbdApkOut $ApkOut
    Copy-Item -Force $WdbdApkOut "$WorkspaceRoot\wdbd.apk"
    Write-Host "    -> WDBD Floating Mod Menu APK: $WdbdApkOut (e espelhado em $ApkOut)" -ForegroundColor Green
} else {
    throw "Ferramentas de build do APK nao encontradas (Javac=$JavacExe, BuildTools=$BuildTools, AndroidJar=$AndroidJar)"
}

Write-Host "[6/6] Enviando artefatos via ADB para a VM 192.168.1.8:10001..." -ForegroundColor Cyan
if (Test-Path "$WorkspaceRoot\scripts\deploy_adb.py") {
    python "$WorkspaceRoot\scripts\deploy_adb.py" 192.168.1.8 10001
} else {
    Write-Host "[OK] Artefatos gerados em $ZipOut e $ApkOut." -ForegroundColor Yellow
}
