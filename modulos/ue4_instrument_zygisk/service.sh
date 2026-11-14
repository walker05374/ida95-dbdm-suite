#!/system/bin/sh
MODDIR=${0%/*}
chmod 1777 /data/local/tmp 2>/dev/null
touch /data/local/tmp/dbdm_mod_config.cfg /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null
chmod 0666 /data/local/tmp/dbdm_mod_config.cfg /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null
killall -9 dbdm_telemetry_daemon dbdm_telemetry_ 2>/dev/null
pkill -9 -f dbdm_telemetry_daemon 2>/dev/null
if [ -f "$MODDIR/system/lib64/libinstrument_core.so" ]; then
  cp -f "$MODDIR/system/lib64/libinstrument_core.so" /data/local/tmp/libinstrument_core_live.so 2>/dev/null
  chmod 0755 /data/local/tmp/libinstrument_core_live.so 2>/dev/null
  chcon u:object_r:system_file:s0 /data/local/tmp/libinstrument_core_live.so 2>/dev/null
fi