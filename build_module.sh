#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# Script        : build_module.sh
# Target        : com.netease.dbdena (ELF64 AArch64 / arm64-v8a)
# Artifact      : dist/ue4_instrument_zygisk.zip
# ============================================================================

: "${ANDROID_NDK_HOME:?Defina a variavel de ambiente ANDROID_NDK_HOME}"

BUILD_DIR="build-arm64"
MOD_DIR="modulos"
STAGE_DIR="${MOD_DIR}/ue4_instrument_zygisk"
ZIP_OUT="${MOD_DIR}/ue4_instrument_zygisk.zip"
CORE_SO="${BUILD_DIR}/libinstrument_core.so"
DIAG_SO="${BUILD_DIR}/libue4_diagnostics.so"
ZYGISK_SO="${BUILD_DIR}/libzygisk_loader.so"

echo "[1/6] Compilando artefatos ELF64 AArch64 (Release)..."
cmake -S . -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static

cmake --build "${BUILD_DIR}" --config Release --parallel

echo "[2/6] Auditando tabela de simbolos dinamicos via llvm-nm..."
LLVM_NM=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-nm" | head -n 1)
LLVM_STRIP=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-strip" | head -n 1)

"${LLVM_STRIP}" --strip-unneeded "${CORE_SO}"
"${LLVM_NM}" -gD --defined-only "${CORE_SO}"

echo "[3/6] Montando arvore de diretorios do modulo Magisk/Zygisk..."
rm -rf "${STAGE_DIR}" "${ZIP_OUT}"
mkdir -p \
  "${STAGE_DIR}/META-INF/com/google/android" \
  "${STAGE_DIR}/system/lib64" \
  "${STAGE_DIR}/zygisk"

cat > "${STAGE_DIR}/module.prop" << 'EOF'
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
EOF

cat > "${STAGE_DIR}/customize.sh" << 'EOF'
SKIPUNZIP=0

if [ "$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/updater-script" << 'EOF'
#MAGISK
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/update-binary" << 'EOF'
#!/sbin/sh
umask 022
ui_print() { echo "$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=$2
ZIPFILE=$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ $MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
EOF

cp -f "${CORE_SO}" "${MOD_DIR}/libinstrument_core.so"
if [ -f "${DIAG_SO}" ]; then
  "${LLVM_STRIP}" --strip-unneeded "${DIAG_SO}"
  cp -f "${DIAG_SO}" "${MOD_DIR}/libue4_diagnostics.so"
fi
cp -f "${CORE_SO}" "${STAGE_DIR}/system/lib64/libinstrument_core.so"
if [ -f "${ZYGISK_SO}" ]; then
  "${LLVM_STRIP}" --strip-unneeded "${ZYGISK_SO}"
  cp -f "${ZYGISK_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
else
  cp -f "${CORE_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
fi

echo "[4/6] Aplicando permissoes locais (0755/0644) e gerando pacote ZIP..."
find "${STAGE_DIR}" -type d -exec chmod 0755 {} +
find "${STAGE_DIR}" -type f -exec chmod 0644 {} +
chmod 0755 \
  "${STAGE_DIR}/META-INF/com/google/android/update-binary" \
  "${STAGE_DIR}/customize.sh" \
  "${STAGE_DIR}/system/lib64/libinstrument_core.so" \
  "${STAGE_DIR}/zygisk/arm64-v8a.so"

(
  cd "${STAGE_DIR}"
  zip -r9 -q "../$(basename "${ZIP_OUT}")" \
    META-INF \
    module.prop \
    customize.sh \
    system \
    zygisk
)

echo "[5/6] Enviando ${ZIP_OUT} via ADB e executando instalacao no Magisk..."
if adb get-state >/dev/null 2>&1; then
  adb push "${ZIP_OUT}" /data/local/tmp/ue4_instrument_zygisk.zip
  adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
  echo "[6/6] Modulo instalado com sucesso. Reinicie o runtime com: adb reboot"
else
  echo "[6/6] Pacote gerado em ${ZIP_OUT} (nenhum dispositivo ADB conectado no momento)."
fi

