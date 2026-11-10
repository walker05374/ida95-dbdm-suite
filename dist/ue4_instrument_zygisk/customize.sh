SKIPUNZIP=0

if [ "$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755