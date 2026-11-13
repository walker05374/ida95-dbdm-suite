# 03 — Network Surface & Endpoint Inventory

## 1. Network Topology Overview

The client uses a **two-tier architecture** injected via `const.ues` (`_bsp_main` at `<boot>:66`):
1. **Local Embedded Python Server (`127.0.0.1:21749`)**: Runs inside the game process (`server/transport.py`, `ThreadingHTTPServer`, `server_version = "bullshitproj"`). All UE4 C++ Kraken HTTP requests (`const.SERVER_URL`) and Python `hotfix2.py` requests target `http://127.0.0.1:21749/<path_key>/...` (where `<path_key>` is a 12-byte random hex token generated per boot via `os.urandom(12).hex()` at `<boot>:116`) or unprefixed `/dbdm/*` routes.
2. **Remote Private Server (`2.31.101.228:21760` TCP/TLS & `2.31.101.228:21761` UDP)**: When the player selects the Online realm (`id = "1"`, `"bullshitproj"`), `server/online_proxy.py` (`maybe_proxy` at line 105) forwards API and matchmaking requests over a persistent TLS 1.2+ binary tunnel (`server/online.py`, `OnlineClient`) to `2.31.101.228:21760`. When a match starts, `server/relay_agent.py` bridges local UE4 UDP port `127.0.0.1:7777` (or ephemeral local UDP port) to the remote UDP relay at `2.31.101.228:21761`.

---

## 2. Host, Port & Scheme Table

| Host / IP | Port | Scheme | Source (File & Line/Offset) | Inferred Purpose | Flags | Confidence |
|---|---:|---|---|---|---|---|
| `2.31.101.228` | `21760` | `tls+netproto` (TCP) | `BSP_PAYLOAD/config.json:25-26`, `BSP_PAYLOAD/data/servers.bin` (decrypted `server_dir.py:42`) | Primary remote private server (`eu-1`, `"Bullshit Project"`) handling auth, profile, progression, bloodmarket, gacha, matchmaking, and OTA hotfixes | **Hardcoded IPv4** | Confirmed (100%) |
| `2.31.101.228` | `21761` | `udp+netproto` (UDP) | `server/relay_agent.py:29` (`ticket.get('port')`), `server/matchmaking.py` (`relay_port = 21761`) | Remote UDP game traffic relay for online UE4 P2P matches (hosts & clients tunnel UE4 NetDriver packets through this port) | **Hardcoded IPv4** | Confirmed (100%) |
| `127.0.0.1` | `21749` | `http` / `ws` | `const.ues` (`BSP_PORT = 21749`, `<boot>:115`), `server/transport.py:265` | Embedded local HTTP server & WebSocket RTM emulator (`ws://127.0.0.1:21749/rtm-<path_key>`) | **Localhost** | Confirmed (100%) |
| `127.0.0.1` | `8080` | `http` / `ws` | `BSP_PAYLOAD/config.json:3`, `server/app.py:40` | Default standalone server listen port when `server/app.py` is run on the remote host or desktop dev environment | **Localhost / Dev default** | Confirmed (100%) |
| `127.0.0.1` | `7777` | `udp` | `data/hotfix/hotfix2.py:4608` (`gamePort: 7777`), `server/relay_agent.py:13` | Local UE4 `IpNetDriver` listen port when the client acts as the match host (`?listen`) | **Localhost** | Confirmed (100%) |
| `https://t.me/bullshitproj` | `443` | `https` | `data/hotfix/hotfix2.py:5936` | Private server Telegram community / update channel | Community URL | Confirmed (100%) |
| `https://discord.gg/bullshitprojectdbdm` | `443` | `https` | `data/hotfix/hotfix2.py:5937` | Private server Discord invite link | Community URL | Confirmed (100%) |
| `https://dbdm.bad.mn/p` | `443` | `https` | `patch/pconst.ues` (`SERVER_LIST_URL_RELEASE`, replaced at runtime by `_bsp_url()`) | Legacy / previous private server mirror URL in `patch/pconst.ues` before `_bsp_main()` overrides `SERVER_LIST_URL_*` to `http://127.0.0.1:21749/<pkey>/ServerList.txt` | **Legacy / Staging** | Confirmed (100%) |

---

## 3. Complete HTTP / REST / WebSocket Route Table (`server/*.pyc`)

All `/api/v1/*` routes are mounted under `app.api` (`prefix = '/api/v1'`, `server/app.py:23`) and are also reachable with the realm prefix `/r/{realm}/api/v1/*` and/or `/<path_key>/...` (`server/app.py:85-100`).
In Online mode (`active_realm == "1"`), `server/online_proxy.py:105` (`maybe_proxy`) intercepts and forwards all `/api/v1/*` routes **except** `LOCAL_API = ('/api/v1/realTimeMessaging/getUrl', '/api/v1/healthcheck', '/api/v1/version', '/api/v1/p1/dedicatedServer/forwardLog', '/api/v1/utils/contentVersion/version')` and `/api/v1/config*` to `2.31.101.228:21760`.

### 3.1 Unauthenticated Local & Proxy Control Routes (`app.root`, `auth=False`)

| Method | Path | Module & Handler | Proxied to `2.31.101.228`? | Purpose |
|---|---|---|---|---|
| `GET` | `/ServerList.txt`, `/serverlist.txt`, `/ServerList_Dev.txt`, `/serverlist_dev.txt` | `server/configuration.py:8` (`server_list`) / `online_proxy.py:39` | Local (queries `OnlineClient`) | Returns realm list: Realm `"1"` (`"bullshitproj"`, online) and Realm `"100"` (`"Оффлайн (боты)"`, offline) |
| `GET` | `/hotfix/hotfix2` | `server/configuration.py:68` (`hotfix2`) | Local (fetches OTA via `hotfix.check`) | Serves `data/hotfix/hotfix2.py` (or Ed25519-signed OTA override from remote server) with `__DBDM_HOST__`, `__DBDM_PORT__`, `__DBDM_PREFIX__` templated |
| `GET` | `/dbdm/online/status` | `server/online_proxy.py:124` (`online_status`) | Local (`no_key` bypass) | Returns `OnlineClient.status_dict()` (connection state, `needLogin`, `unlocked`, `uid`, `banned`) |
| `POST` | `/dbdm/online/call` | `server/online_proxy.py:127` (`online_call`) | Yes (via `CALL` opcode `12`) | **Unauthenticated local RPC bridge**: invokes `OnlineClient.call(fn, args)` for `acc.status`, `acc.auth`, `acc.ident`, `acc.test_key`, `acc.redeem`, `acc.set_credentials`, `acc.login_credentials`, `acc.transfer_code`, `acc.use_transfer_code` |
| `POST` | `/dbdm/relay/start` | `server/online_proxy.py:165` (`relay_start`) | Local (`no_key` bypass) | Starts `HostRelay` or `ClientRelay` UDP tunnel to `2.31.101.228:21761` |
| `POST` | `/dbdm/relay/stop` | `server/online_proxy.py:194` (`relay_stop`) | Local (`no_key` bypass) | Stops active UDP relay agent |
| `GET` | `/dbdm/relay/info` | `server/online_proxy.py:201` (`relay_info`) | Local (`no_key` bypass) | Returns active UDP relay stats |
| `GET`, `POST` | `/dbdm/loadouts` | `server/characters.py:90,94` (`dbdm_loadouts_get/put`) | Yes (`PROXY_ROOT`) | Reads/writes saved custom perk/item builds (`kv['dbdm_loadouts']`) |
| `GET` | `/dbdm/content/catalog.json` | `server/configuration.py:210` (`online_catalog`) | Yes (`PROXY_ROOT`) | Returns dynamic store price catalog (`store_prices.py`) |
| `GET` | `/dbdm/perks.json` | `server/configuration.py:133` (`perk_lists`) | Local | Returns list of all camper/slasher perks and default prestige (`100`) |
| `GET` | `/dbdm/audio.json`, `/dbdm/audio.pak` | `server/configuration.py:142,154` | Local | Serves custom audio pak metadata and binary |
| `POST` | `/debug/pylog` | `server/configuration.py:103` (`pylog`) | Local (disabled if `release=true`) | Appends client Python log lines to `client_python.log` |
| `GET` | `/debug/cmd` | `server/configuration.py:120` (`debug_cmd`) | Local (disabled if `release=true`) | Executes `state_dir/debug_cmd.py` in client if present |
| `GET` | `/debug/meshmap`, `/debug/botmatch` | `server/configuration.py:161,168` | Local | Serves `customization_meshes.json` and bot match pools |
| `GET` | `/cdn/clientData/{root}/{p1..p5}` | `server/configuration.py:188` (`cdn_file`) | Top-level `.json` proxied via `content.get` | Serves `Catalog`, `tunables`, `SpecialEventsContent`, ` emblemTunable`, `ranksThresholds` |
| `WS` | `/rtm`, `/rtm-<path_key>` | `server/rtm.py:86` (`handle`) | Local WebSocket | Emulates NetEase/Kraken Real-Time Messaging WebSocket (`Connection Established`, party/chat events) |

### 3.2 Authentication & Session Routes (`server/auth.py`)

| Method | Path | Handler (`server/auth.py`) | Auth Required? | Purpose |
|---|---|---|---|---|
| `GET` | `/api/v1/auth/provider/guid/login` | `guid_login` (`L75`) | `False` | Logs in or creates guest account using `guid` query param; issues `bhvrSession` cookie & token |
| `POST` | `/api/v1/auth/login` | `auth_login` (`L85`) | `False` | Logs in with `guestToken` |
| `POST` | `/api/v1/auth/loginWith Token` (`/auth/loginWithToken`) | `login_with_token` (`L103`) | `False` | Logs in via third-party provider (`provider`, `token`/`id`) |
| `POST` | `/api/v1/auth/provider/{provider}/login` | `provider_login` (`L121`) | `False` | Logs in with provider query `token` |
| `POST` | `/api/v1/auth/register` | `register_provider` (`L131`) | `True` | Links current account to `provider` + `id` |
| `POST` | `/api/v1/dbd-login/sign-in` | `dbd_signin` (`L139`) | `False` | Main DBDM sign-in (`data.loginType`, `userId`, `token`); seeds new account via `inventory.seed_account` and returns `token` + `bhvrSession` |
| `POST` | `/api/v1/dbd-login/token/refresh` | `dbd_refresh` (`L165`) | `False` | Refreshes session token |
| `POST` | `/api/v1/auth/session/ renew` (`/auth/session/renew`) | `session_renew` (`L174`) | `True` | Extends session TTL (`30 * 86400`s) |
| `POST` | `/api/v1/auth/logout` | `logout` (`L181`) | `False` | Drops `bhvrSession` token from `sessions` table |

### 3.3 Profile, Save States & Friends Routes (`server/profile.py`, `server/characters.py`)

| Method | Path | Handler | Auth Required? | Purpose |
|---|---|---|---|---|
| `GET` | `/api/v1/players/me` | `profile.players_me` (`L15`) | `True` | Returns player profile (`userId`, `playerName`, `platforms`) |
| `GET`, `PUT` | `/api/v1/playername`, `/api/v1/playername/byId/{userId}` | `profile.playername_get/set/by_id` (`L25-44`) | `True` | Reads or updates `playerName` in `accounts` table |
| `POST` | `/api/v1/players/{playerId}/states/binary` | `profile.states_binary_write` (`L48`) | `True` | **Writes `FullProfile` binary save blob** (`stateName`, `version`) and calls `bloodmarket.sync_save_characters` |
| `GET` | `/api/v1/players/{playerId}/states/{stateName}/binary` | `profile.states_binary_read` (`L75`) | `True` | Reads `FullProfile` binary save blob (or synthesizes default save via `dbdsave.encode_full_profile`) |
| `POST` | `/api/v1/players/me/states/FullProfile/binary` | `profile.me_fullprofile_write` (`L89`) | `True` | Writes `FullProfile` blob for current user |
| `GET` | `/api/v1/players/me/states/FullProfile/binary` | `profile.me_fullprofile_read` (`L95`) | `True` | Reads `FullProfile` blob for current user |
| `GET` | `/api/v1/states/{stateName}/type/{stateType}` | `profile.states_list` (`L103`) | `True` | Lists state objects (`seasonManager`, `story`, `dailyRituals`, etc.) |
| `GET`, `PUT`, `DELETE` | `/api/v1/states/{stateName}/type/{stateType}/id/{objectId}` | `profile.state_get/put/delete` (`L115-167`) | `True` | CRUD on JSON player states (`states` table) |
| `GET`, `POST` | `/api/v1/friends`, `/api/v1/players/friends`, `/api/v1/friends/richPresence` | `profile.friends_list/add/rich_presence` (`L175-225`) | `True` | Friend list & rich presence management |
| `POST` | `/api/v1/PlayerCard/Get`, `/api/v1/PlayerCard/Set` | `profile.playercard_get/set` (`L248-262`) | `True` | Reads/writes player card customization (`kv['playercard']`) |
| `GET` | `/api/v1/profile/character`, `/api/v1/profile/characterData` | `characters.profile_character` (`L4,7`) | `True` | Returns character progression data |
| `POST` | `/api/v1/profile/loadoutdata/update`, `/api/v1/character/loadout/custom/save` | `characters.loadout_update/custom_save` (`L13,22`) | `True` | Saves character loadout & perk schemes |
| `POST` | `/api/v1/perk/GetPerk`, `/api/v1/perk/SavePerk` | `characters.get_perk/save_perk` (`L57,69`) | `True` | Reads/saves character perk presets (`perk1`..`perk3`) |

### 3.4 Wallet, Inventory, Store, Bloodmarket & Gacha Routes (`server/inventory.py`, `server/bloodmarket.py`, `server/gacha.py`)

| Method | Path | Handler | Auth Required? | Purpose |
|---|---|---|---|---|
| `GET` | `/api/v1/wallet/currencies`, `/api/v1/wallet/player/currencies` | `inventory.wallet_currencies` (`L173`) | `True` | Returns balances for `Bloodpoints`, `Shards`, `Cells`, `HorizontalFragments`, `PrestigePoints`, `BonusBloodpoints`, `EventCurrency`, `EntityCoins`, `DrawTicket` |
| `PUT` | `/api/v1/wallet/withdraw` | `inventory.wallet_withdraw` (`L199`) | `True` | Deducts `amount` of `currency` from caller's wallet |
| `POST` | `/api/v1/Args/migrateCurrencies` | `inventory.migrate_currencies` (`L211`) | `True` | Returns `migrationStatus: True` + current wallet list |
| `GET` | `/api/v1/inventories`, `/api/v1/inventories/designated` | `inventory.inventories_get/designated` (`L216,227`) | `True` | Returns caller's full `inventory` item list (`objectId`, `quantity`, `lastUpdateAt`) |
| `POST` | `/api/v1/inventories/consume` | `inventory.inventories_consume` (`L238`) | `True` | Consumes `quantity` of `objectId` from caller's inventory |
| `POST` | `/api/v1/Purchase/consume` | `inventory.purchase_consume` (`L255`) | `True` | Buys character/item/bundle (`objectId`, `currencyType`, `expectedAmountToPay`); checks `store_prices.authoritative_price` |
| `POST` | `/api/v1/store/outfits/buy` | `inventory.outfits_buy` (`L307`) | `True` | Buys outfit or individual cosmetic piece (`outfitId`, `pieceIds`, `currency`, `amount`) |
| `POST` | `/api/v1/pay/obtainPayOrderId`, `/api/v1/pay/depositCallback` | `inventory.obtain_pay_order` (`L356`) | `True` | Disabled IAP stub (`403 PaymentDisabled`) |
| `POST` | `/api/v1/blood-market/retrieve` | `bloodmarket.bm_retrieve` (`L75`) | `True` | Retrieves Bloodmarket / Bloodweb state for `role` |
| `POST` | `/api/v1/blood-market/save/persistent-data` | `bloodmarket.bm_save` (`L88`) | `True` | **Overwrites `blood_market_roles[role]['persistentData']` with client JSON** |
| `POST` | `/api/v1/blood-market/refresh` | `bloodmarket.bm_refresh` (`L102`) | `True` | Generates next Bloodweb ring/nodes (`level + 1`) |
| `POST` | `/api/v1/blood-market/collect` | `bloodmarket.bm_collect` (`L128`) | `True` | Buys node `(ringId, nodeId)` with `Bloodpoints` and grants `contentId` to `inventory` |
| `POST` | `/api/v1/blood-market/force-refresh` | `bloodmarket.bm_force_refresh` (`L174`) | `True` | Deducts `refreshCost` (`Bloodpoints`) and regenerates Bloodmarket |
| `POST` | `/api/v1/Character/PrestigeUp` | `bloodmarket.prestige_up` (`L314`) | `True` | Increments character prestige (`progressionLevel`), grants teachable perks at P1/P2/P3, and awards `Cells` (`prestige_Reward.json`) |
| `POST` | `/api/v1/dedicatedServer/profile/characterXp` | `bloodmarket.ds_character_xp` (`L351`) | `True` | **Adds `baseExp + offeringExp` to `characterId` for `playerId`** (no DS check!) |
| `GET` | `/api/v1/gacha/box/list` | `gacha.box_list` (`L226`) | `True` | Returns active gacha pools (`PoolIds = [31, 21]`), pity counters, and claimed milestones |
| `POST` | `/api/v1/gacha/treasure/extract`, `/api/v1/gacha/free/treasure/extract` | `gacha.extract` (`L248`) | `True` | Performs 1x or 10x gacha pull using `DrawTicket` (Sinister Stones) or `Cells` (Auric Cells) |
| `POST` | `/api/v1/gacha/claim/progress/reward` | `gacha.claim_progress` (`L332`) | `True` | Claims cumulative pull milestone reward |

### 3.5 Progression, Score, Ranks & Rift Pass Routes (`server/progression.py`, `server/match.py`)

| Method | Path | Handler | Auth Required? | Purpose |
|---|---|---|---|---|
| `GET`, `POST` | `/api/v1/extension/playerLevels/getPlayerLevel` | `progression.get_player_level` (`L28`) | `True` | Returns player XP, `levelVersion`, `level`, `prestigeLevel` |
| `POST` | `/api/v1/extension/playerLevels/earnPlayerXp` | `progression.earn_player_xp` (`L35`) | `True` | **Grants `matchXpOverride` or calculated match XP; awards `300 Shards` per level** |
| `GET` | `/api/v1/ranks/pips`, `/api/v1/ranks/season/getInfo` | `progression.ranks_get` (`L41`) | `True` | Returns `killerPips`, `survivorPips`, highest ranks, and season stats |
| `PUT` | `/api/v1/ranks/pips` | `progression.ranks_pips` (`L46`) | `True` | **Directly overwrites `killerPips` and `survivorPips` from client JSON** |
| `POST` | `/api/v1/ranks/updateSeasonInfo` | `progression.ranks_update_season` (`L65`) | `True` | **Adds `killerPips`/`survivorPips` delta and match/win counters** |
| `POST` | `/api/v1/ranks/rewards/claimAll` | `progression.ranks_claim_all` (`L115`) | `True` | Grants all unclaimed rank rewards (`Bloodpoints`, `Shards`, items) up to highest rank |
| `POST` | `/api/v1/archive/v1/combat/state/get` | `progression.rift_state` (`L154`) | `True` | Returns Rift (Battlepass) state (`stars`, `tier`, `passId`, claimed tiers, custom season overrides) |
| `POST` | `/api/v1/archive/v1/combat/pass/upgrade` | `progression.rift_upgrade` (`L160`) | `True` | Upgrades Rift Pass (`passId = 2` for `900 Cells` or `passId = 3` for `2000 Cells` + 20 tiers + `upgrade2` rewards) |
| `POST` | `/api/v1/archive/v1/combat/tier/buy` | `progression.rift_buy_tier` (`L211`) | `True` | Buys Rift tiers (`100 Cells` per tier, 10 stars/tier) |
| `POST` | `/api/v1/archive/v1/combat/tier/claim` | `progression.rift_claim` (`L255`) | `True` | Claims free & premium Rift rewards for `tier` (or `claimAll` when `tier <= 0`) |
| `POST` | `/api/v1/dedicatedServer/{playerId}/currency/grant` | `match.ds_grant_currency` (`L69`) | `True` | **Grants arbitrary `walletToGrant`/`walletToExtraAdd` to `{playerId}` (no DS check!)** |
| `POST` | `/api/v1/extensions/rewards/grantCurrency` | `match.ext_grant_currency` (`L75`) | `True` | **Grants arbitrary `data.walletToGrant` to caller (no validation!)** |
| `POST` | `/api/v1/dedicatedServer/update/inventories` | `match.ds_update_inventories` (`L79`) | `True` | **Adds `rewardItems` and removes `lostItems` for any `playerId` (no DS check!)** |
| `POST` | `/api/v1/match/{matchId}/Killers`, `/Survivors`, `/end` | `match.match_killers/survivors/end` (`L42-53`) | `True` | Records post-match results |

### 3.6 Matchmaking, Custom Match & Anti-Cheat Telemetry Routes (`server/matchmaking.py`, `server/match.py`)

| Method | Path | Handler | Auth Required? | Purpose |
|---|---|---|---|---|
| `POST` | `/dbdm/match/plan` | `match.dbdm_match_plan` (`L104`) | `False` | Returns bot match plan (`127.0.0.1:7777`) or online match roster |
| `POST`, `GET` | `/dbdm/match/PostMatch`, `/dbdm/match/result` | `match.dbdm_post_match / result` (`L153,167`) | `False` | Records/reads post-match stats |
| `POST` | `/dbdm/mm/queue`, `/dbdm/mm/cancel` | `matchmaking.mm_queue / mm_cancel` | `False` (uses `OnlineClient` session) | Enters/leaves online survivor/killer matchmaking queue |
| `GET` | `/dbdm/mm/status` | `matchmaking.mm_status` | `False` | Polls online queue & active match state |
| `POST` | `/dbdm/mm/host_ready`, `/dbdm/mm/end`, `/dbdm/mm/EndGame` | `matchmaking.mm_host_ready / mm_end` | `False` | Match lifecycle transitions |
| `POST` | `/dbdm/mm/info`, `/dbdm/mm/select`, `/dbdm/mm/ready` | `matchmaking.mm_info / select / ready` | `False` | Lobby loadout sync & ready state |
| `POST` | `/dbdm/mm/custom/create`, `/join`, `/leave`, `/role`, `/bots`, `/map`, `/start` | `matchmaking.mm_custom_*` | `False` | Custom lobby creation & management |
| `POST` | `/dbdm/mm/ac` | `matchmaking.mm_ac` | `False` | **Client anti-cheat self-report sink** (`kind`: `speed`, `fly`, `repair`, `escape`, `tamper`, `freeze`, `env`, `files`, `shield`) |
| `POST` | `/dbdm/mm/sec/chal`, `/dbdm/mm/sec/att` | `matchmaking.mm_sec_chal / att` (`acguard.py`) | `False` | `libUE4.so` byte-range HMAC-SHA256 attestation challenge & response |
| `POST` | `/dbdm/mm/sec/hb` | `matchmaking.mm_sec_hb` (`acguard.py`) | `False` | Periodic Python/native guard heartbeat proof (`_dbdm_sh_pulse`) |
| `POST` | `/dbdm/mm/kdiag` | `matchmaking.mm_kdiag` | `False` | Killer power diagnostic telemetry |

### 3.7 Remote Admin Console Routes (`server/admin.py` — Server-Side Only unless `admin_token` Set)

Mounted on the server at `/admin/*` (`server/admin.py:678-733`), protected by `X-Admin-Token` / `Bearer <admin_token>` (`hmac.compare_digest` at `server/admin.py:39`). Includes `/admin/api/overview`, `/admin/api/online`, `/admin/api/Update`, `/admin/api/matches`, `/admin/api/custom`, `/admin/api/queues`, `/admin/api/ac`, `/admin/api/hotfix`, `/admin/api/test` (test mode & key generator), `/admin/api/seasons` (custom Rift pass builder), `/admin/api/prices`, `/admin/api/users`, `/admin/api/user/{uid}/{action}` (`ban`, `unban`, `kick`, `grant_currency`, `grant_item`, `reset_password`, `unbind_device`, `delete`), `/admin/api/codes`, `/admin/api/broadcast`, `/admin/api/audit`.

