# 09 — Hypotheses & Runtime Verification Plan

Because the embedded `BSP_PAYLOAD` archive contains the complete server and client Python bytecode (`server/*.pyc` and `data/hotfix/hotfix2.py`), the protocol framing, cryptographic keys, route handlers, and local/shared validation logic are **statically proven** rather than inferred.

However, four aspects depend on **live state or configuration on the remote host `2.31.101.228:21760`** that cannot be proven from the client bundle alone. Each is listed below as an explicit `HYPOTHESIS` along with the exact runtime test or capture required to confirm it.

---

## Hypothesis 1 — Remote Server Parity on `server/match.py` & `server/bloodmarket.py` Endpoints

- **Tag**: `HYPOTHESIS`
- **Statement**: The remote server at `2.31.101.228:21760` runs the exact same `server/match.py`, `server/progression.py`, and `server/bloodmarket.py` route handlers shipped in `BSP_PAYLOAD` v`2026.11.09.1` (where `ext_grant_currency`, `ds_grant_currency`, `ds_update_inventories`, `earn_player_xp`, and `prestige_up` lack server-side privilege/precondition checks).
- **Static Evidence Supporting Hypothesis**:
  - `server/online_proxy.py:105` (`maybe_proxy`) explicitly proxies `/api/v1/*` requests over `netproto` `REQ` (`opcode=10`) to `2.31.101.228:21760`.
  - `server/admin.py`, `server/acguard.py`, and `server/matchmaking.py` are bundled in the same `server/*.pyc` package inside `BSP_PAYLOAD` alongside `server/match.py`.
- **Runtime Verification Test**:
  1. Launch the client in Online mode (Realm `1`, `"bullshitproj"`) and complete login.
  2. From a local shell on the device (or by reading the `<path_key>` from `const.SERVER_URL`), send a single benign `POST` request through the local proxy:
     - Path: `POST http://127.0.0.1:21749/<path_key>/r/1/api/v1/extensions/rewards/grantCurrency`
     - Header: `Cookie: bhvrSession=<session_token>`
     - Body: `{"data": {"walletToGrant": [{"currency": "Bloodpoints", "amount": 1}]}}`
  3. **Confirmation Criteria**: HTTP `200 OK` returning `{"list": [...], "granted": [{"currency": "Bloodpoints", "amount": 1}]}` and an incremented `Bloodpoints` balance on subsequent `GET /api/v1/wallet/currencies` confirms the remote server runs the unpatched `ext_grant_currency` handler.

---

## Hypothesis 2 — Remote OTA Hotfix Override (`hotfix.check` RPC)

- **Tag**: `HYPOTHESIS`
- **Statement**: At runtime, the remote server at `2.31.101.228:21760` may serve a newer Ed25519-signed `hotfix2.py` script via the `hotfix.check` RPC (`server/hotfix_ota.py:86`) than the `data/hotfix/hotfix2.py` (11,640 lines, SHA-256 `618ff137dc0b93657023c84cc73e8b55264ff31f4ef83a966cc01b57dc7120c9`) bundled inside `BSP_PAYLOAD`.
- **Static Evidence Supporting Hypothesis**:
  - `server/configuration.py:68-77` calls `hotfix_ota.refresh(app)` and `hotfix_ota.code_for(app)` before falling back to `app.vfs.get('data/hotfix/hotfix2.py')`.
  - `server/hotfix_ota.py:86-128` calls `app.online.call('hotfix.check', {'version': cur_ver, 'build': BUILD})` every `OTA_POLL_S = 300` seconds and caches any signed update to `<state_dir>/ota_hotfix.json`.
- **Runtime Verification Test**:
  1. After connecting to the online realm on a test device, inspect `/sdcard/Android/data/com.netease.dbdena/files/.ue4cache/s/u/ota_hotfix.json` (or request `GET http://127.0.0.1:21749/dbdm/online/status` and check the `hotfix` field).
  2. **Confirmation Criteria**: If `ota_hotfix.json` exists and `version > 0`, diff its `"code"` string against `E:\ida95-dbdm-suite\relatorios\server_audit\_work_bsp_payload\data\hotfix\hotfix2.py` to see if `SHIELD_DIGEST` or any `_dbdm_sh_*` / `_dbdm_ac_*` checks were updated.

---

## Hypothesis 3 — Server-Side Handler for `acc.*` RPCs (`CALL` Opcode `12`)

- **Tag**: `HYPOTHESIS`
- **Statement**: While the client-side `OnlineClient.call` dispatcher (`server/online.py:306`) and `online_proxy.ALLOWED_CALLS` (`server/online_proxy.py:13`) enumerate the 9 `acc.*` RPCs (`acc.status`, `acc.auth`, `acc.ident`, `acc.test_key`, `acc.redeem`, `acc.set_credentials`, `acc.login_credentials`, `acc.transfer_code`, `acc.use_transfer_code`), the remote TCP server listener that terminates `netproto` `CALL` (`opcode=12`) frames on port `21760` is a separate daemon module not included in `BSP_PAYLOAD`.
- **Static Evidence Supporting Hypothesis**:
  - `server/online.py` implements the `OnlineClient` side of `netproto`, and `server/admin.py` reads/writes the server-side tables (`online_accounts`, `test_keys`, `promo_codes`, `hwid_bans`), proving the schema and business logic of those tables, while the TCP socket accept loop for port `21760` resides on the server host.
- **Runtime Verification Test**:
  1. Capture the `CALL` (`opcode=12`) and `CALL_RESP` (`opcode=13`) JSON payloads by logging inside `OnlineClient.call` (`server/online.py:306`) or via `POST http://127.0.0.1:21749/dbdm/online/call` with `{"fn": "acc.status", "args": {}}`.
  2. **Confirmation Criteria**: Observe the exact `data` keys returned by `acc.status` (`registered`, `nickname`, `testMode`, `testPending`, `testKeyNeeded`).

---

## Hypothesis 4 — Remote Server Enforcement of `acguard.py` Flags vs. Auto-Ban

- **Tag**: `HYPOTHESIS`
- **Statement**: In `server/acguard.py`, `verify_att` (`line 74`) and `verify_hb` (`line 114`) record attestation failures (`att_bad`, `att_nolib`, `hb_bad_proof`, `hb_bad_digest`, `hb_guard_stale`, `hb_native_dead`, `hb_native_hit`) via `_flag(app, uid, kind, detail)` into `app._ac_log` (viewed by admins at `/admin/api/ac`), rather than immediately invoking an automatic database ban (`ban_user`).
- **Static Evidence Supporting Hypothesis**:
  - Disassembly of `server_acguard.pyc.txt` (`lines 150-240`, `file=<bsp>:32-48`) shows `_flag` appends to `app._ac_log` and increments `st['flags'][kind]` without calling `admin.ban_user`.
- **Runtime Verification Test**:
  1. On a disposable test account, observe `GET http://127.0.0.1:21749/dbdm/online/status` after delaying `/dbdm/mm/sec/hb` by `> 65` seconds (`GUARD_STALE = 60`).
  2. **Confirmation Criteria**: If the session remains `connected: True` and `status != 'banned'` / `'kicked'`, anti-cheat flags are logged for manual admin review in `/admin/api/ac` rather than triggering an automated kick/ban.

