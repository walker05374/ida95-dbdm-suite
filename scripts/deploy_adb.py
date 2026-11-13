#!/usr/bin/env python3
import os
import socket
import subprocess
import sys
import threading
import time

REMOTE_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.68"
REMOTE_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 10001
PROXY_PORT = 15555

def find_reachable_local_ip(remote_ip: str, remote_port: int) -> str:
    candidates = ["192.168.1.3", "192.168.1.188", "0.0.0.0"]
    for lip in candidates:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1.0)
        try:
            if lip != "0.0.0.0":
                s.bind((lip, 0))
            s.connect((remote_ip, remote_port))
            s.close()
            return lip
        except Exception:
            try:
                s.close()
            except Exception:
                pass
    return "0.0.0.0"

def pipe(src: socket.socket, dst: socket.socket) -> None:
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
    except Exception:
        pass
    finally:
        for sock in (src, dst):
            try:
                sock.close()
            except Exception:
                pass

def start_proxy(local_bind_ip: str, remote_ip: str, remote_port: int, proxy_port: int) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", proxy_port))
    srv.listen(32)

    def accept_loop() -> None:
        while True:
            try:
                client_sock, _ = srv.accept()
                remote_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                if local_bind_ip != "0.0.0.0":
                    remote_sock.bind((local_bind_ip, 0))
                remote_sock.settimeout(5.0)
                remote_sock.connect((remote_ip, remote_port))
                remote_sock.settimeout(None)
                threading.Thread(target=pipe, args=(client_sock, remote_sock), daemon=True).start()
                threading.Thread(target=pipe, args=(remote_sock, client_sock), daemon=True).start()
            except Exception:
                try:
                    client_sock.close()
                except Exception:
                    pass

    threading.Thread(target=accept_loop, daemon=True).start()

def main() -> int:
    adb_candidates = [
        r"C:\Users\Walker\Downloads\platform-tools\adb.exe",
        r"C:\Users\walker\Desktop\anydesk\tools\android-sdk\platform-tools\adb.exe",
        r"E:\android-sdk\platform-tools\adb.exe",
        r"C:\Android\sdk\platform-tools\adb.exe",
    ]
    adb_exe = next((p for p in adb_candidates if os.path.exists(p)), "adb.exe")

    local_ip = find_reachable_local_ip(REMOTE_IP, REMOTE_PORT)
    print(f"[ADB-DEPLOY] Conectando na VM {REMOTE_IP}:{REMOTE_PORT} via adb connect direto...", flush=True)

    target = f"{REMOTE_IP}:{REMOTE_PORT}"

    def run_adb(*args: str, timeout: int = 45) -> int:
        cmd = [adb_exe] + list(args)
        print(f"  $ {' '.join(cmd)}", flush=True)
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
            if res.stdout and res.stdout.strip():
                print(res.stdout.strip(), flush=True)
            if res.stderr and res.stderr.strip():
                print(res.stderr.strip(), flush=True)
            return res.returncode
        except subprocess.TimeoutExpired:
            print(f"  [TIMEOUT] {' '.join(cmd)}", flush=True)
            return 124

    run_adb("disconnect")
    run_adb("connect", target)
    run_adb("-s", target, "wait-for-device", timeout=15)
    time.sleep(0.5)
    run_adb("devices", "-l")

    # 0. Clean old APKs and old module archives/directory on device first
    cleanup_cmd = (
        "rm -f /sdcard/Download/ue4_instrument_zygisk.zip "
        "/sdcard/Download/wdbd.apk "
        "/sdcard/Download/DBDM_FloatingModMenu.apk "
        "/data/local/tmp/ue4_instrument_zygisk.zip "
        "/data/local/tmp/wdbd.apk "
        "/data/local/tmp/libinstrument_core.so; "
        "pm uninstall com.dbdm.modmenu >/dev/null 2>&1; "
        "su -c 'rm -rf /data/adb/modules/ue4_instrument_core /data/adb/modules_update/ue4_instrument_core "
        "/sdcard/Download/ue4_instrument_zygisk.zip /sdcard/Download/wdbd.apk /sdcard/Download/DBDM_FloatingModMenu.apk'"
    )
    run_adb("-s", target, "shell", cleanup_cmd, timeout=20)

    # 1. Push Zygisk module & native libraries
    run_adb("-s", target, "push", r"modulos\ue4_instrument_zygisk.zip", "/sdcard/Download/ue4_instrument_zygisk.zip")
    run_adb("-s", target, "push", r"modulos\ue4_instrument_zygisk.zip", "/data/local/tmp/ue4_instrument_zygisk.zip")
    run_adb("-s", target, "push", r"build-arm64\libinstrument_core.so", "/data/local/tmp/libinstrument_core.so")
    run_adb("-s", target, "push", r"zygisk\service.sh", "/data/local/tmp/service.sh")
    run_adb("-s", target, "push", r"zygisk\module.prop", "/data/local/tmp/module.prop")
    if os.path.exists(r"build-arm64\dbdm_telemetry_daemon"):
        run_adb("-s", target, "push", r"build-arm64\dbdm_telemetry_daemon", "/data/local/tmp/dbdm_telemetry_daemon_new")

    # 2. Push Wdbd APK
    apk_local = r"modulos\wdbd.apk" if os.path.exists(r"modulos\wdbd.apk") else "wdbd.apk"
    run_adb("-s", target, "push", apk_local, "/sdcard/Download/wdbd.apk")
    run_adb("-s", target, "push", apk_local, "/sdcard/Download/DBDM_FloatingModMenu.apk")
    run_adb("-s", target, "push", apk_local, "/data/local/tmp/wdbd.apk")

    # 3. Install Magisk/Zygisk module & restart daemon (preserving +x on dbdm_telemetry_daemon!)
    su_cmd = (
        "su -c '"
        "mkdir -p /data/adb/modules/ue4_instrument_core/zygisk /data/adb/modules/ue4_instrument_core/system/lib64 && "
        "cp -f /data/local/tmp/ue4_instrument_zygisk.zip /data/adb/modules/ue4_instrument_core/ 2>/dev/null; "
        "magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip >/dev/null 2>&1; "
        "cp -f /data/local/tmp/libinstrument_core.so /data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so && "
        "cp -f /data/local/tmp/libinstrument_core.so /data/adb/modules/ue4_instrument_core/system/lib64/libinstrument_core.so && "
        "cp -f /data/local/tmp/service.sh /data/adb/modules/ue4_instrument_core/service.sh && "
        "cp -f /data/local/tmp/module.prop /data/adb/modules/ue4_instrument_core/module.prop && "
        "killall -9 dbdm_telemetry_daemon dbdm_telemetry_ >/dev/null 2>&1; pkill -9 -x dbdm_telemetry_ >/dev/null 2>&1; "
        "if [ -f /data/local/tmp/dbdm_telemetry_daemon_new ]; then "
        "  mv -f /data/local/tmp/dbdm_telemetry_daemon_new /data/local/tmp/dbdm_telemetry_daemon; "
        "  cp -f /data/local/tmp/dbdm_telemetry_daemon /data/adb/modules/ue4_instrument_core/dbdm_telemetry_daemon 2>/dev/null; "
        "fi; "
        "chmod 0777 /data/local/tmp && "
        "/sbin/magiskpolicy --live \"allow * * unix_stream_socket { connectto read write getattr getopt }\" \"allow * app_data_file file { open read write getattr }\" \"allow * shell_data_file file { open read write getattr }\" 2>/dev/null; "
        "rm -f /data/local/tmp/dbdm_mod_config.cfg && "
        "touch /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null && "
        "chmod 0666 /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null && "
        "chmod 0755 /data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so "
        "/data/adb/modules/ue4_instrument_core/system/lib64/libinstrument_core.so "
        "/data/adb/modules/ue4_instrument_core/service.sh "
        "/data/adb/modules/ue4_instrument_core/dbdm_telemetry_daemon "
        "/data/local/tmp/dbdm_telemetry_daemon 2>/dev/null; "
        "rm -f /data/adb/modules/ue4_instrument_core/update && rm -rf /data/adb/modules_update/ue4_instrument_core; "
        "/data/local/tmp/dbdm_telemetry_daemon --daemon && "
        "echo DAEMON_PID=$(pidof dbdm_telemetry_daemon)"
        "'"
    )
    run_adb("-s", target, "shell", su_cmd, timeout=25)

    # 4. Install and launch Wdbd APK (com.dbdm.modmenu)
    apk_cmd = (
        "pm uninstall com.dbdm.modmenu >/dev/null 2>&1; "
        "pm install -r /data/local/tmp/wdbd.apk && "
        "appops set com.dbdm.modmenu SYSTEM_ALERT_WINDOW allow && "
        "appops set com.dbdm.modmenu MANAGE_EXTERNAL_STORAGE allow >/dev/null 2>&1; "
        "pm grant com.dbdm.modmenu android.permission.WRITE_EXTERNAL_STORAGE >/dev/null 2>&1; "
        "pm grant com.dbdm.modmenu android.permission.READ_EXTERNAL_STORAGE >/dev/null 2>&1; "
        "mkdir -p /sdcard/DBDMod/logs && chmod 0777 /sdcard/DBDMod /sdcard/DBDMod/logs 2>/dev/null; "
        "am start -n com.dbdm.modmenu/.MainActivity"
    )
    run_adb("-s", target, "shell", apk_cmd, timeout=30)

    time.sleep(1.5)
    run_adb("-s", target, "logcat", "-d", "-t", "30", "-s", "UE4Diag:V", "FloatingModService:V", "AndroidRuntime:E", timeout=15)
    print(f"[OK] Deploy concluido para a VM {REMOTE_IP}:{REMOTE_PORT}!", flush=True)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
