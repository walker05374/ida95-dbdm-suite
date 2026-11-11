#!/system/bin/sh
MODDIR=${0%/*}
chmod 1777 /data/local/tmp 2>/dev/null
touch /data/local/tmp/dbdm_mod_config.cfg /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null
chmod 0666 /data/local/tmp/dbdm_mod_config.cfg /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null
if [ -x /data/local/tmp/dbdm_telemetry_daemon ]; then
  killall -9 dbdm_telemetry_daemon 2>/dev/null
  /data/local/tmp/dbdm_telemetry_daemon --daemon
fi