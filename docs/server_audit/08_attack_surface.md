# 08 — Ranked Attack Surface & Weak Validation Points

All findings below are statically proven from the disassembled server and client bytecode in `E:\ida95-dbdm-suite\relatorios\server_audit\_work_bsp_payload\disasm\`. Because the remote server (`2.31.101.228:21760`) runs the exact `server/*.pyc` codebase and `server/online_proxy.py:105` (`maybe_proxy`) forwards all non-local `/api/v1/*` requests over `netproto` `REQ` (`opcode=10`), every server route vulnerability below applies to both the local server and the remote online server.

---

## Ranked Vulnerabilities (Critical -> Low)

### 1. [CRITICAL] Arbitrary Currency Minting via `ext_grant_currency`
- **Location**: `server/match.py:75-78` (`ext_grant_currency`), `_grant_list` (`server/match.py:55-67`), registered at `server/match.py:185` (`POST /api/v1/extensions/rewards/grantCurrency`, `auth=True`).
- **Root Cause**: The handler reads `req.json().get('data', {}).get('walletToGrant', [])` and passes it directly to `_grant_list(app, uid, items)`, which calls `app.db.wallet_add(uid, cur, amt)` for any currency string and integer amount without checking match state, admin privileges, or entitlement.
- **Impact**: Any authenticated player can mint unlimited paid Auric Cells (`Cells`), Sinister Stones (`DrawTicket`), Iridescent Shards (`Shards`), `EntityCoins`, and `Bloodpoints`, completely bypassing paid Battlepass (`rift_upgrade`), Gacha (`extract`), and Store (`purchase_consume`, `outfits_buy`) monetization.

### 2. [CRITICAL] Cross-Player Currency Grant & Wallet Drain via `ds_grant_currency`
- **Location**: `server/match.py:69-73` (`ds_grant_currency`), registered at `server/match.py:184` (`POST /api/v1/dedicatedServer/{playerId}/currency/grant`, `auth=True`), `server/storage.py:120-127` (`Storage.wallet_add`).
- **Root Cause**:
  1. Despite the `/dedicatedServer/` path prefix, the route is registered on `app.api` with standard player session authentication (`auth=True`) and zero dedicated-server role verification.
  2. Unlike other endpoints that read `req.user['user_id']`, `ds_grant_currency` uses `uid = req.params.get('playerId') or _uid(req)` (`server_match.pyc.txt:364-386`) — trusting the `{playerId}` URL parameter!
  3. `Storage.wallet_add` (`server_storage.pyc.txt:765-773`) adds `int(amount)` (which may be negative) and only rejects the update if the resulting balance `bal < 0`.
- **Impact**: Any player can grant arbitrary currencies to any other player's `user_id` or pass negative amounts (`amount = -cur_balance`) to drain another player's `Cells`, `Shards`, or `Bloodpoints` to `0`.

### 3. [CRITICAL] Arbitrary Item/Character/Outfit Grant & Cross-Player Inventory Deletion via `ds_update_inventories`
- **Location**: `server/match.py:79-90` (`ds_update_inventories`), registered at `server/match.py:186` (`POST /api/v1/dedicatedServer/update/inventories`, `auth=True`), `server/storage.py:100-109` (`Storage.inv_add`).
- **Root Cause**:
  1. No dedicated-server privilege check.
  2. Reads target user from the request body: `uid = str(b.get('playerId') or _uid(req))` (`server_match.pyc.txt:438-458`).
  3. Iterates over `b.get('rewardItems', [])` calling `app.db.inv_add(uid, oid, +qty)` and over `b.get('lostItems', [])` calling `app.db.inv_add(uid, oid, -qty)`. When `quantity <= 0`, `Storage.inv_add` executes `DELETE FROM inventory WHERE user_id=? AND object_id=?` (`server_storage.pyc.txt:618-629`).
- **Impact**: Any authenticated player can grant themselves any character, exclusive cosmetic, perk, item, or offering in the game, or wipe any other player's entire inventory (`lostItems`) by their `user_id`.

### 4. [HIGH] InfinitePrestige & Auric Cells Farming via Unchecked `PrestigeUp`
- **Location**: `server/bloodmarket.py:314-349` (`prestige_up`, `POST /api/v1/Character/PrestigeUp`).
- **Root Cause**: `prestige_up` increments `c['progressionLevel']` by `1` (up to `MAX_PRESTIGE = 100`), resets `c['bloodWebLevel'] = 1`, and grants Auric Cells (`Cells`) from `prestige_Reward.json` (`10`/`20`/`30` Cells per prestige) **without verifying that `c['bloodWebLevel'] >= 50`**.
- **Impact**: A player can call `POST /api/v1/Character/PrestigeUp` in a tight loop on a level-1 character to instantly reach Prestige 100, unlock all teachable perks, and farm `Cells`.

### 5. [HIGH] Client-Controlled Bloodweb Node Injection (`bm_save` + `bm_collect`)
- **Location**: `server/bloodmarket.py:88-100` (`bm_save`, `POST /api/v1/blood-market/save/persistent-data`) and `server/bloodmarket.py:128-172` (`bm_collect`, `POST /api/v1/blood-market/collect`).
- **Root Cause**: `bm_save` writes the client-supplied `persistentData` dictionary directly into `kv['blood_market_roles'][role]['persistentData']`. `bm_collect` then looks up `(ringId, nodeId)` inside that saved `persistentData`, trusts the node's `contentId` and `price`, and calls `app.db.inv_add(uid, content_id, 1)`.
- **Impact**: A client can save a custom Bloodweb ring containing any item/addon/offering with `price: 0` and immediately collect it into their server inventory.

### 6. [HIGH] Arbitrary Player XP, Character XP, Rank Pips & Free Rift Tiers
- **Locations**:
  - `POST /api/v1/extension/playerLevels/earnPlayerXp` (`server/progression.py:35-44`): Trusts `data.matchXpOverride` (`int`), granting `300 Shards` per `1000 XP`.
  - `POST /api/v1/dedicatedServer/profile/characterXp` (`server/bloodmarket.py:351-365`): Trusts `baseExp + offeringExp` for any `playerId` and `characterId` without dedicated-server auth.
  - `PUT /api/v1/ranks/pips` (`server/progression.py:46-63`): Trusts `killerPips` and `survivorPips`, enabling instant Rank 1 + `POST /api/v1/ranks/rewards/claimAll`.
  - `PUT /api/v1/states/story/type/{stateType}/id/{objectId}` (`server/profile.py:157` -> `server/progression.py:271-305`): `on_story_put` awards `10` Rift Stars (`1` Battlepass tier) per unique key in `data.nodeInfo` with `status == 3` without validating that the node ID exists in the Tome catalog or that a match was played.

### 7. [HIGH] Client-Authoritative `FullProfile` Binary Save & Upward Prestige/Level Sync
- **Location**: `server/profile.py:48-73` (`states_binary_write`), `server/bloodmarket.py:208-246` (`_save_levels`, `sync_save_characters`), `server/dbdsave.py:8` (`KEY = b'5BCC2D6A95D4DF04A005504E59A9B36E'`).
- **Root Cause**: The client encrypts `FullProfile` locally using the static AES-256-ECB key `5BCC2D6A95D4DF04A005504E59A9B36E` and uploads it via `POST /api/v1/players/me/states/FullProfile/binary`. The server decodes `characterData` from the uploaded save and overwrites `kv['characters']` whenever `(save_prestige, save_bloodweb_level) > (db_prestige, db_bloodweb_level)`.

### 8. [MEDIUM] Client-Side Anti-Cheat Key Exposure & Plaintext IPC (`_DBDM_ATT_K` & `.dbdmguard`)
- **Location**: `data/hotfix/hotfix2.py:10155, 10947-10988`, `server/acguard.py:9, 17`.
- **Root Cause**:
  1. Both `libUE4.so` byte-range attestation (`/dbdm/mm/sec/att`) and Shield heartbeat proofs (`/dbdm/mm/sec/hb`) are signed in Python using the hardcoded HMAC key `_DBDM_ATT_K = 'b7264a72b2cdb8a33afc0b8f63c22ceb024afd531ac29134ef07146a810b18d0'`.
  2. `_dbdm_att_lib_range` (`hotfix2.py:10158`) reads `libUE4.so` **from disk (`open(p, 'rb')` or APK zip)** rather than process memory, so runtime inline hooks in `libUE4.so` memory never affect `/dbdm/mm/sec/att`.
  3. `libdbdmguard.so` communicates with Python solely by writing a plaintext line `1|<seq>|<ts>|<flags>|<findings>|<hash>` to `/data/data/com.netease.dbdena/files/.dbdmguard`, which `_dbdm_sh_native()` reads without any cryptographic authenticity.
  4. All gameplay AC detections (`speed`, `fly`, `repair`, `escape`, `tamper`, `freeze`, `shield`) are self-reported by the client via `POST /dbdm/mm/ac`.

### 9. [MEDIUM] Unauthenticated Localhost RPC Bridge (`127.0.0.1:21749/dbdm/online/call`)
- **Location**: `server/online_proxy.py:217-228` (`register`), `server/app.py:85-100`.
- **Root Cause**: While normal routes require `/<path_key>/` (`12`-byte random hex), `/dbdm/online/status`, `/dbdm/online/call`, and `/dbdm/relay/*` are registered in `app.no_key` with `auth=False`. Any local process or script running on the same Android device can call `http://127.0.0.1:21749/dbdm/online/call` to execute authenticated `OnlineClient` RPCs (`acc.transfer_code`, `acc.set_credentials`, `acc.redeem`, etc.) or hijack the active `OnlineClient` session.

