# 05 — Authentication, Session Management & Anti-Cheat Detection

## 1. Two-Stage Authentication Architecture

In Online mode (`active_realm == "1"`), authentication occurs across two distinct layers:

### Stage 1: `OnlineClient` Tunnel & Account Gate (`server/online.py`, `hotfix2.py:5840-6300`)
1. **Device Identity (`OnlineClient._load_device_id`, `server/online.py:89-125`)**:
   - Reads or generates a 32-hex-character random `device_id` (`os.urandom(16).hex()`) persisted at `<state_dir>/online_device.id` (`/sdcard/Android/data/com.netease.dbdena/files/.ue4cache/s/u/online_device.id`).
   - Collects `self.hwid` (`server/online.py:127-166`):
     - `aid`: SHA-256 of Android `settings get secure android_id` (`[:32]`).
     - `boot`: SHA-256 of `/proc/sys/kernel/random/boot_id` (`[:24]`).
     - `drm`: SHA-256 of `getprop ro.boot.vbmeta.digest` or `ro.build.fingerprint` (`[:32]`).
     - `model`: `ro.product.manufacturer + " " + ro.product.model` (`[:64]`).
     - `os`: `"android " + ro.build.version.release`.
   - `hotfix2.py` (`_dbdm_lg_ident` at lines 5835-5864) augments `c.hwid` and calls `acc.ident` with NetEase UniSDK properties (`udid`, `sdk_model`, and on iOS `fs` filesystem birthtime/inode fingerprint `v1;<model>;<var_birthtime>;<mobile_birthtime>;<root_birthtime>;<ino>;<blocks>`).
2. **`HELLO` / `WELCOME` Handshake (`server/online.py:195-248`)**:
   - Client sends `HELLO` with `deviceId`, `nonce`, `build = "2026.11.09.1"`, `proof = HMAC-SHA256(device_secret, "hello-v3|" + nonce_c + "|2026.11.09.1")`, and `hwid`.
   - Server responds with `WELCOME` assigning `uid` and flags `needLogin`, `unlocked`, `registered`.
3. **Interactive Nickname/Password Login (`hotfix2.py:6056-6205`)**:
   - If `needLogin == True` and `unlocked == False`, `hotfix2.py` displays a custom UMG popup (`_dbdm_lg_choice` / `_dbdm_lg_input`) and invokes `POST /dbdm/online/call` with:
     - `{"fn": "acc.auth", "args": {"nickname": "<3-20 chars>", "password": "<6-64 chars>", "mode": "login" | "register"}}`
   - If server `testMode` is active (`code == "testKeyRequired"`), the client prompts for an invite key and calls `{"fn": "acc.test_key", "args": {"key": "<key>"}}` (`hotfix2.py:6109`).
   - Account binding: The remote server binds accounts to `deviceId` / `hwid` (`error codes: deviceBound, accountBound`, `hotfix2.py:6096`), which admins can reset via `/admin/api/user/{uid}/unbind_device` (`server/admin.py:552`).

### Stage 2: Kraken HTTP API Session (`server/auth.py`, `server/storage.py`)
1. Once `OnlineClient` is unlocked (or immediately in Offline mode `realm == "100"`), UE4 calls:
   - `GET /api/v1/auth/provider/guid/login?guid=<guid>` (`server/auth.py:75`)
   - `POST /api/v1/dbd-login/sign-in` (`server/auth.py:139`)
2. **Token Generation (`Storage.create_session`, `server/storage.py:79-84`)**:
   - Generates a 64-character hex token (`new_id().replace('-', '') + new_id().replace('-', '')`, i.e. two concatenated UUIDv4s).
   - Default TTL: `session_ttl_seconds = 30 * 86400` (30 days, `server/auth.py:11`).
   - Returned in JSON body (`token`, `sessionToken`) and HTTP header `Set-Cookie: bhvrSession=<token>; Path=/; HttpOnly` (`server/auth.py:21-38`).
3. **Token Extraction on Subsequent Requests (`extract_token`, `server/auth.py:40-54`)**:
   - Checks `Cookie: bhvrSession=<tok>`, then `Authorization: Bearer <tok>`, then `X-Bhvr-Session: <tok>`, then query `?bhvrSession=<tok>`.

---

## 2. Client-Side Anti-Cheat & Tamper Detection (`libdbdmguard.so` + `hotfix2.py` + `server/acguard.py`)

The private server implements a multi-component anti-cheat system split across a native library (`libdbdmguard.so`), client Python hotfix (`data/hotfix/hotfix2.py`), and server verifier (`server/acguard.py`).

### 2.1 Native Watchdog Library (`lib/arm64-v8a/libdbdmguard.so`)

Loaded automatically via a patched `DT_NEEDED` entry in `libVoiceChangeCore.so`. Disassembly of `.init_array` constructor `0x11dc` and watchdog thread `0x1260` shows:
- Loops every **3 seconds** (`nanosleep` at `0x1700`) scanning:
  1. **Process Status (`/proc/self/status`)**: Checks `TracerPid:` (`0x132c`). If non-zero, sets bit `0x01` (`tracer:%d`).
  2. **Thread Comm Names (`/proc/self/task/<tid>/comm`)**: Flags threads named `gmain`, `gum-js-loop`, `pool-frida`, `gdbus` (FridaGadget / GumJS threads, bit `0x02`).
  3. **Memory Maps (`/proc/self/maps`)**: Scans mapped paths (`0x1490`) for `frida`, `gum-js`, `xposed`, `lsposed`, `edxposed`, `riru`, `zygisk`, `substrate`, `libhooker`, `substitute`, `gameguardian`, `gg_` (bits `0x04`, `0x08`, `0x10`).
  4. **Local TCP Ports (`/proc/net/tcp`, `/proc/net/tcp6`)**: Checks for listening port `0x69A2` (`27042`, default `frida-server` port) and `0x69A3` (`27043`) (bit `0x20`).
  5. **`libUE4.so` Executable Segment Integrity**: Computes a rolling FNV-1a hash over the first `r-xp` mapping of `libUE4.so` and compares it across ticks; if modified (inline hooks), sets bit `0x80` (`textmod`).
- **Output Channel**: Writes a pipe-delimited status line to `/data/data/com.netease.dbdena/files/.dbdmguard`:
  ```
  1|<seq>|<timestamp>|<flags_hex>|<findings_csv>|<fnv_hash>
  ```
- **Critical Weakness**: `libdbdmguard.so` has **no network code**. It only writes to `/data/data/com.netease.dbdena/files/.dbdmguard`, which `hotfix2.py` (`_dbdm_sh_native` at line 10953) reads in plaintext Python!

### 2.2 Python Anti-Cheat Modules in `data/hotfix/hotfix2.py`

| Module Name | `hotfix2.py` Lines | Guard Stamp Key | What It Checks | Reporting Endpoint |
|---|---|---|---|---|
| **Movement & File AC (`_dbdm_ac_*`)** | `9614–9876` | `_dbdm_guard['ac']` | - Speed hack check (`_AC_LIMIT = {2: 850.0, 1: 1600.0}` cm/s + haste allowance)<br>- Fly hack check (`MovementMode == 5` and Z-rise `> 250.0` over `3.0`s)<br>- Host rubberbanding (`pawn.set_actor_location(back)`)<br>- Scans `UE4Game` for extra `.pak`/`.utoc`/`.ucas`/`.sig`, modified `.ini` (`maxwalkspeed`, `slomo`, `timedilation`, `gravityscale`), and `ue4commandline.txt` | `POST /dbdm/mm/ac` (`kind`: `speed`, `fly`, `files`) |
| **Generator & Escape Guard (`_dbdm_gg_*`)** | `9877–10096` | N/A (Host-only) | - Hooks `ChargeAppliedAuthorityDelegate` on all `Generator` actors; enforces token bucket (`_GG_RATE = 6.0`/s, `_GG_BURST = 30.0`) and reverts excess charge via `cc.Authority_AddCharge(-over, None, True)`<br>- Checks survivor escapes (`s == 2`) when `gens > 0` and neither hatch nor door is open, or distance to exit `> 2000` cm | `POST /dbdm/mm/ac` (`kind`: `repair`, `escape`) |
| **Attestation (`_dbdm_att_*`)** | `10153–10223` | N/A | - Polls `POST /dbdm/mm/sec/chal` every 10s–120s for `{n, o, l}`<br>- Reads `l` bytes at offset `o` from `libUE4.so` on disk (not memory!) and replies to `POST /dbdm/mm/sec/att` with `HMAC-SHA256(_DBDM_ATT_K, nonce + sha256(chunk))[:32]` | `POST /dbdm/mm/sec/chal`, `POST /dbdm/mm/sec/att` |
| **Tunable Guard (`_dbdm_tp_*`)** | `10224–10297` | `_dbdm_guard['tp']` | - Compares `TunableValue` DataTable rows in UE4 memory against compressed reference table `_TP_REF_Z` and initial baseline | `POST /dbdm/mm/ac` (`kind`: `tamper`) |
| **Memory & Freeze Guard (`_dbdm_mg_*`)** | `10298–10424` | `_dbdm_guard['mg']` | - Parses `/proc/self/smaps` every 20s for `Private_Dirty`/`Shared_Dirty`/`Swap` pages on read-only mappings (`'w' not in p[1]`) of `com.netease.dbdena`<br>- Parses `/proc/self/maps` for foreign `/data/` libs<br>- Detects process freeze (`gap >= 2.5`s with low CPU delta)<br>- Checks `TracerPid` in `/proc/self/status` | `POST /dbdm/mm/ac` (`kind`: `tamper`, `freeze`, `env`) |
| **Self-Scale Guard (`_dbdm_sg_*`)** | `10425–10486` | `_dbdm_guard['sg']` | - Monitors `RelativeScale3D` on pawn hitbox components (`AttackZone`, `InteractionDetector`, `CollisionCylinder`, etc.) for scale `> 20.0` or `3x` growth | `POST /dbdm/mm/ac` (`kind`: `tamper`) |
| **Shield & Heartbeat (`_dbdm_sh_*`)** | `10487–11081` | `_dbdm_guard['sh']`, `_dbdm_guard['nat']` | - **Virtual Space / Dual App check**: Compares `os.getuid()` with `/data/user/<u_id>/com.netease.dbdena` owner UID<br>- **Emulator check**: `libhoudini`, `libndk_translation`, `/libnb.so`, `x86` ABI, `ro.dalvik.vm.native.bridge`<br>- **Root / Magisk / KernelSU check**: `su` paths, `/proc/self/mountinfo` (`magisk`, `/data/adb/`, `zygisk`, `kernelsu`, `apatch`)<br>- **GameGuardian / Memory Scanner Trap (`_dbdm_sh_trap_probe`)**: Maps an anonymous `4 GB` / `1 GB` / `256 MB` region (`mmap`) and monitors `/proc/self/status` `VmPTE:` growth (`>= 128 KB`) caused by external memory scanners touching unallocated pages!<br>- **Speedhack Clock Skew (`_dbdm_sh_clock_step`)**: Compares filesystem `st_mtime` deltas (`os.utime`) against `time.time()` and `time.monotonic()` (`> 10%` drift over 3 runs)<br>- **UE4 CDO Sweep (`_dbdm_sh_cdo_step`, `_dbdm_sh_wide_step`)**: Sweeps Class Default Objects (`DBDCharacterMovementComponent`, `SlasherPlayer`, `CamperPlayer`, etc.) for modified float/bool properties<br>- **Code Object Digest (`_dbdm_sh_digest`)**: Computes SHA-256(`[:24]`) over `co_code` + `co_names` of the 22 `_SH_FUNCS` functions | `POST /dbdm/mm/ac` (`kind`: `shield`, `tamper`), `POST /dbdm/mm/sec/hb` |

### 2.3 Server-Side Verification (`server/acguard.py`)
- **Hardcoded HMAC Key (`KEY` at `server/acguard.py:9` & `_DBDM_ATT_K` at `hotfix2.py:10155`)**:
  - Literal Hex: `b7264a72b2cdb8a33afc0b8f63c22ceb024afd531ac29134ef07146a810b18d0`
- **Expected Python Digest (`SHIELD_DIGEST` at `server/acguard.py:17`)**:
  - Literal Value: `'89626c0576b4de952bbbc879'`
- **Heartbeat Proof Formula (`verify_hb`, `server/acguard.py:114-200`)**:
  - `msg = bytes.fromhex(nonce) + f"{seq}|{digest}|{plat}|{guards_csv}|{nat_line}".encode("utf-8")`
  - `proof = hmac.new(KEY, msg, hashlib.sha256).hexdigest()[:32]`
  - Checks that `guards` contains `('ac', 'tp', 'mg', 'sg', 'sh')` with ages `<= 60`s (`GUARD_STALE = 60`), `digest == '89626c0576b4de952bbbc879'`, and on Android `nat_line` starts with `'1|'` with flag field `'0'`.

