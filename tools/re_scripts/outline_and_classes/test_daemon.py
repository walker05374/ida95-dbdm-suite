import subprocess, time
from scripts.deploy_adb import find_reachable_local_ip, start_proxy

adb_exe = r"C:\Users\walker\Desktop\anydesk\tools\android-sdk\platform-tools\adb.exe"
local_ip = find_reachable_local_ip("192.168.1.8", 10001)
start_proxy(local_ip, "192.168.1.8", 10001, 15556)
target = "127.0.0.1:15556"
subprocess.run([adb_exe, "disconnect", target], capture_output=True)
subprocess.run([adb_exe, "connect", target], capture_output=True)
time.sleep(0.5)

cmd = (
    "su -c '"
    "echo === UniTrace.log_temp ===; "
    "cat /data/data/com.netease.dbdena/files/uniTrace/crashhunter/UniTrace.log_temp; "
    "echo === crashhunter.di ===; "
    "cat /data/data/com.netease.dbdena/files/uniTrace/crashhunter/crashhunter.di; "
    "echo === native_crash.dmp ===; "
    "xxd /data/data/com.netease.dbdena/files/uniTrace/crashhunter/NATIVE_DUMP_644_1794487476276/native_crash.dmp"
    "'"
)
res = subprocess.run([adb_exe, "-s", target, "shell", cmd], capture_output=True, text=True, timeout=20)
print(res.stdout)
