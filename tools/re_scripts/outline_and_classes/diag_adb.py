import subprocess
import time
from scripts.deploy_adb import find_reachable_local_ip, start_proxy

adb_exe = r"C:\Users\walker\Desktop\anydesk\tools\android-sdk\platform-tools\adb.exe"
lip = find_reachable_local_ip("192.168.1.8", 10001)
start_proxy(lip, "192.168.1.8", 10001, 15555)
target = "127.0.0.1:15555"
subprocess.run([adb_exe, "connect", target], capture_output=True)
time.sleep(0.5)

def sh(cmd: str) -> None:
    res = subprocess.run([adb_exe, "-s", target, "shell", cmd], capture_output=True, text=True, timeout=20)
    print(f"=== {cmd} ===")
    if res.stdout:
        print(res.stdout.strip())
    if res.stderr:
        print("STDERR:", res.stderr.strip())

sh("su -c 'ps -A | grep -E \"dbdena|dbdm|modmenu\"'")
sh("su -c 'ls -laZ /data/local/tmp/dbdm* /data/adb/modules/ue4_instrument_core/zygisk/ /data/adb/modules/ue4_instrument_core/system/lib64/'")
sh("su -c 'md5sum /data/local/tmp/libinstrument_core.so /data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so /data/local/tmp/dbdm_telemetry_daemon'")
sh("su -c 'cat /proc/net/unix | grep dbdm'")
sh("su -c 'cat /data/local/tmp/dbdm_mod_config.cfg'")
sh("su -c 'for p in $(pidof com.netease.dbdena com.dbdm.modmenu dbdm_telemetry_daemon); do echo \"--- PID $p ---\"; cat /proc/$p/cmdline; echo \"\"; grep -E \"instrument_core|arm64-v8a|dbdm\" /proc/$p/maps | head -n 10; done'")
sh("su -c 'logcat -d -t 300 | grep -E \"UE4Diag|FloatingModService|dbdm|avc:|instrument_core|FATAL\"'")
