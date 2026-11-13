#!/usr/bin/env python3
import os
import socket
import subprocess
import sys
import threading
import time
from typing import Tuple

DEFAULT_CANDIDATE_IPS = ["192.168.1.68", "192.168.1.8", "192.168.1.191"]
CANDIDATE_IPS = [sys.argv[1]] if len(sys.argv) > 1 else list(DEFAULT_CANDIDATE_IPS)
for _fb_ip in DEFAULT_CANDIDATE_IPS:
    if _fb_ip not in CANDIDATE_IPS:
        CANDIDATE_IPS.append(_fb_ip)
REMOTE_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 10001
PROXY_PORT = 15555


def is_tcp_reachable(remote_ip: str, remote_port: int, timeout: float = 2.0) -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((remote_ip, remote_port))
        s.close()
        return True
    except Exception:
        try:
            s.close()
        except Exception:
            pass
        return False

def find_reachable_local_ip(remote_ip: str, remote_port: int) -> str:
    candidates = ["0.0.0.0", "192.168.1.3", "192.168.1.188"]
    for lip in candidates:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.8)
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
    return ""

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

    def run_adb(*args: str, timeout: int = 45) -> Tuple[int, str]:
        cmd = [adb_exe] + list(args)
        print(f"  $ {' '.join(cmd)}", flush=True)
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
            out_str = (res.stdout or "") + "\n" + (res.stderr or "")
            if res.stdout and res.stdout.strip():
                print(res.stdout.strip(), flush=True)
            if res.stderr and res.stderr.strip():
                print(res.stderr.strip(), flush=True)
            return res.returncode, out_str
        except subprocess.TimeoutExpired:
            print(f"  [TIMEOUT] {' '.join(cmd)}", flush=True)
            return 124, ""

    run_adb("disconnect")

    selected_ip = None
    selected_target = None
    proxy_started = False
    candidate_ports = [REMOTE_PORT, 5555] if REMOTE_PORT != 5555 else [5555, 10001]

    for cand_ip in CANDIDATE_IPS:
        for cand_port in candidate_ports:
            cand_target = f"{cand_ip}:{cand_port}"
            lip = find_reachable_local_ip(cand_ip, cand_port)
            if not lip:
                print(f"[ADB-DEPLOY] {cand_target} indisponivel; tentando proximo...", flush=True)
                continue

            print(f"[ADB-DEPLOY] Conectando via adb connect {cand_target}...", flush=True)
            run_adb("connect", cand_target, timeout=6)
            rc_wait, _ = run_adb("-s", cand_target, "wait-for-device", timeout=4)
            time.sleep(0.3)
            _, devs_out = run_adb("devices", "-l")
            if rc_wait == 0 and cand_target in devs_out and "offline" not in devs_out.split(cand_target)[1].splitlines()[0]:
                selected_ip = cand_ip
                selected_target = cand_target
                break

            if lip != "0.0.0.0":
                print(f"[ADB-DEPLOY] Rota multi-NIC detectada ({lip} -> {cand_target}); usando bridge local 127.0.0.1:{PROXY_PORT}...", flush=True)
                if not proxy_started:
                    start_proxy(lip, cand_ip, cand_port, PROXY_PORT)
                    proxy_started = True
                proxy_target = f"127.0.0.1:{PROXY_PORT}"
                run_adb("connect", proxy_target, timeout=6)
                rc_wait, _ = run_adb("-s", proxy_target, "wait-for-device", timeout=5)
                time.sleep(0.3)
                _, devs_out = run_adb("devices", "-l")
                if rc_wait == 0 and proxy_target in devs_out and "offline" not in devs_out.split(proxy_target)[1].splitlines()[0]:
                    selected_ip = cand_ip
                    selected_target = proxy_target
                    break
        if selected_target:
            break

    if not selected_target:
        print(f"[ADB-DEPLOY] Nenhum dispositivo ADB online encontrado em {CANDIDATE_IPS}.", flush=True)
        return 1

    REMOTE_IP = selected_ip
    target = selected_target

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

    # 2. Push Wdbd APK
    apk_local = r"modulos\wdbd.apk" if os.path.exists(r"modulos\wdbd.apk") else "wdbd.apk"
    run_adb("-s", target, "push", apk_local, "/sdcard/Download/wdbd.apk")
    run_adb("-s", target, "push", apk_local, "/sdcard/Download/DBDM_FloatingModMenu.apk")
    run_adb("-s", target, "push", apk_local, "/data/local/tmp/wdbd.apk")

    # 3. Install Magisk/Zygisk module, stage live .so, and kill standalone daemon (in-process Zygisk only)
    su_cmd = (
        "su -c '"
        "killall -9 dbdm_telemetry_daemon dbdm_telemetry_ >/dev/null 2>&1; "
        "pkill -9 -f dbdm_telemetry_daemon >/dev/null 2>&1; "
        "rm -f /data/local/tmp/dbdm_telemetry_daemon /data/local/tmp/dbdm_telemetry_daemon_new "
        "/data/adb/modules/ue4_instrument_core/dbdm_telemetry_daemon 2>/dev/null; "
        "mkdir -p /data/adb/modules/ue4_instrument_core/zygisk /data/adb/modules/ue4_instrument_core/system/lib64 && "
        "cp -f /data/local/tmp/ue4_instrument_zygisk.zip /data/adb/modules/ue4_instrument_core/ 2>/dev/null; "
        "magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip >/dev/null 2>&1; "
        "cp -f /data/local/tmp/libinstrument_core.so /data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so && "
        "cp -f /data/local/tmp/libinstrument_core.so /data/adb/modules/ue4_instrument_core/system/lib64/libinstrument_core.so && "
        "cp -f /data/local/tmp/libinstrument_core.so /data/local/tmp/libinstrument_core_live.so && "
        "cp -f /data/local/tmp/service.sh /data/adb/modules/ue4_instrument_core/service.sh && "
        "cp -f /data/local/tmp/module.prop /data/adb/modules/ue4_instrument_core/module.prop && "
        "chmod 0777 /data/local/tmp && "
        "/sbin/magiskpolicy --live "
        "\"allow * * unix_stream_socket { connectto read write getattr getopt }\" "
        "\"allow * app_data_file file { open read write getattr execute execute_no_trans map }\" "
        "\"allow * shell_data_file file { open read write getattr execute execute_no_trans map }\" "
        "\"allow * system_file file { open read getattr execute execute_no_trans map }\" 2>/dev/null; "
        "touch /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null && "
        "chmod 0666 /data/local/tmp/dbdm_telemetry_shm.bin /data/local/tmp/dbdm_telemetry_shm_v2.bin 2>/dev/null && "
        "chmod 0755 /data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so "
        "/data/adb/modules/ue4_instrument_core/system/lib64/libinstrument_core.so "
        "/data/adb/modules/ue4_instrument_core/service.sh "
        "/data/local/tmp/libinstrument_core_live.so 2>/dev/null; "
        "chcon u:object_r:system_file:s0 /data/local/tmp/libinstrument_core_live.so "
        "/data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so "
        "/data/adb/modules/ue4_instrument_core/system/lib64/libinstrument_core.so 2>/dev/null; "
        "rm -f /data/adb/modules/ue4_instrument_core/update && rm -rf /data/adb/modules_update/ue4_instrument_core; "
        "am force-stop com.netease.dbdena >/dev/null 2>&1; "
        "killall -9 com.netease.dbdena >/dev/null 2>&1; "
        "echo ZYGISK_LIVE_SO_READY"
        "'"
    )
    run_adb("-s", target, "shell", su_cmd, timeout=25)

    # 3b. Ensure zygote64 has loaded the dlopen-capable Zygisk module stub at least once
    _, z_out = run_adb(
        "-s",
        target,
        "shell",
        "su -c 'cur_z=$(pidof zygote64); saved_z=$(cat /data/local/tmp/.zygisk_live_loader_pid 2>/dev/null); "
        "if [ -z \"$saved_z\" ] || [ \"$cur_z\" != \"$saved_z\" ]; then echo NEED_ZYGOTE_FLUSH; else echo ZYGOTE_OK:$cur_z; fi'",
        timeout=10,
    )
    if "NEED_ZYGOTE_FLUSH" in z_out:
        print("[ADB-DEPLOY] Flushing zygote64 cached module .so so live dlopen loader takes effect...", flush=True)
        run_adb("-s", target, "shell", "su -c 'setprop ctl.restart zygote'", timeout=10)
        for _ in range(25):
            time.sleep(1.0)
            rc_b, b_out = run_adb(
                "-s",
                target,
                "shell",
                "su -c 'if [ \"$(getprop sys.boot_completed)\" = \"1\" ] && [ -n \"$(pidof zygote64)\" ] && [ -n \"$(pidof com.android.systemui)\" ]; then "
                "pidof zygote64 > /data/local/tmp/.zygisk_live_loader_pid && chmod 0666 /data/local/tmp/.zygisk_live_loader_pid && echo READY; fi'",
                timeout=8,
            )
            if rc_b == 0 and "READY" in b_out:
                time.sleep(2.0)
                break

    # Re-apply magiskpolicy & kill any stray standalone daemon after zygote flush
    run_adb(
        "-s",
        target,
        "shell",
        "su -c 'killall -9 dbdm_telemetry_daemon dbdm_telemetry_ >/dev/null 2>&1; "
        "pkill -9 -f dbdm_telemetry_daemon >/dev/null 2>&1; "
        "/sbin/magiskpolicy --live "
        "\"allow * * unix_stream_socket { connectto read write getattr getopt }\" "
        "\"allow * app_data_file file { open read write getattr execute execute_no_trans map }\" "
        "\"allow * shell_data_file file { open read write getattr execute execute_no_trans map }\" "
        "\"allow * system_file file { open read getattr execute execute_no_trans map }\" 2>/dev/null'",
        timeout=15,
    )

    # 4. Install and launch Wdbd APK (com.dbdm.modmenu) and restart target game process (com.netease.dbdena)
    apk_cmd = (
        "pm uninstall com.dbdm.modmenu >/dev/null 2>&1; "
        "pm install -r /data/local/tmp/wdbd.apk && "
        "appops set com.dbdm.modmenu SYSTEM_ALERT_WINDOW allow && "
        "appops set com.dbdm.modmenu MANAGE_EXTERNAL_STORAGE allow >/dev/null 2>&1; "
        "pm grant com.dbdm.modmenu android.permission.WRITE_EXTERNAL_STORAGE >/dev/null 2>&1; "
        "pm grant com.dbdm.modmenu android.permission.READ_EXTERNAL_STORAGE >/dev/null 2>&1; "
        "mkdir -p /sdcard/DBDMod/logs && chmod 0777 /sdcard/DBDMod /sdcard/DBDMod/logs 2>/dev/null; "
        "am start -n com.dbdm.modmenu/.MainActivity && "
        "monkey -p com.netease.dbdena -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"
    )
    run_adb("-s", target, "shell", apk_cmd, timeout=30)

    time.sleep(3.0)
    run_adb("-s", target, "logcat", "-d", "-t", "40", "-s", "UE4Diag:V", "FloatingModService:V", "AndroidRuntime:E", timeout=15)
    print(f"[OK] Deploy concluido para a VM {REMOTE_IP}:{REMOTE_PORT}!", flush=True)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
