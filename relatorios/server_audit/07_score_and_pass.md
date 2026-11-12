# 07 — Score, XP, Ranking, Paid Pass & Unlockables Analysis

## 1. Score, Match Rewards, Player XP & Character XP

### 1.1 Bloodpoints & Post-Match Currency (`server/match.py`)
- **Post-Match Recording (`POST /api/v1/match/{matchId}/Killers`, `/Survivors`, `/end`, `POST /dbdm/match/PostMatch`)**:
  - `server/match.py:31-67` stores whatever JSON payload the client submits into the `matches` table (`app.db.put_match(mid, m)`). It performs **no server-side validation** of match duration, emblem scores, or Bloodpoint category caps (`Objectives`, `Survival`, `Altruism`, `Boldness`, `Brutality`, `Deviousness`, `Hunter`, `Sacrifice`).
- **Currency Grant Endpoints (`server/match.py:69-78`)**:
  - `POST /api/v1/extensions/rewards/grantCurrency` (`ext_grant_currency`) and `POST /api/v1/dedicatedServer/{playerId}/currency/grant` (`ds_grant_currency`) both call `_grant_list(app, uid, items)` (`server/match.py:55-67`):
    ```python
    def _grant_list(app, uid, items):
        granted = []
        for it in items or []:
            cur = it.get('currency') or it.get('currencyId')
            amt = int(it.get('amount') or 0)
            if cur and amt:
                app.db.wallet_add(uid, cur, amt)
                granted.append({'currency': cur, 'amount': amt})
        return {'list': _wallet_list(app, uid), 'granted': granted}
    ```
  - Because `_grant_list` does not restrict which currencies can be granted or cap `amount`, a client can grant itself arbitrary `Bloodpoints`, `Shards`, `Cells`, `DrawTicket`, or `EntityCoins` at any time without playing a match.

### 1.2 Player Account XP & Iridescent Shards (`server/progression.py:1-44`)
- **Constants (`server/progression.py:6-9`)**:
  - `XP_PER_LEVEL = 1000`
  - `LEVELS_PER_PRESTIGE = 100` (Devotion level increments every 100 levels)
  - `SHARDS_PER_LEVEL = 300` (300 Iridescent Shards granted per level-up)
  - `EMBLEM_XP = {'Iridescent': 18, 'Gold': 12, 'Silver': 6, 'Bronze': 0}`
- **Calculation vs. Override (`earn_player_xp`, `server/progression.py:35-44`)**:
  - Normal formula (`_calc_xp` at line 19): `min(int(matchTime), 600) + sum(EMBLEM_XP[q] for q in emblemQualities) + (300 if isFirstMatch else 0) + min(int(consecutiveMatch) * 30, 180)`.
  - **Weakness**: Line 39 checks `xp = int(d.get('matchXpOverride') or 0) or _calc_xp(d)`. Passing `{"data": {"matchXpOverride": 1000000}}` immediately grants `1,000` levels (`10` Devotion) and **`300,000` Iridescent Shards** (`Shards`).

### 1.3 Character XP & Prestige (`server/bloodmarket.py:248-365`)
- **Character XP (`POST /api/v1/dedicatedServer/profile/characterXp`, `server/bloodmarket.py:351`)**:
  - `_add_char_xp(app, uid, cid, add_xp)` (`line 259`) adds `baseExp + offeringExp` (`XP_PER_BLOODWEB_LEVEL = 600`), increments `bloodWebLevel` (capped at `MAX_BLOODWEB_LEVEL = 50`), and grants item rewards from `CharacterLevel.json`.
- **Prestige Up (`POST /api/v1/Character/PrestigeUp`, `server/bloodmarket.py:314-349`)**:
  - Increments `c['progressionLevel']` by `1`, resets `c['bloodWebLevel'] = 1` and `c['experience'] = 0`, unlocks the character's teachable perks at Prestige 1, 2, and 3 into `inventory` and `states['teachableRecord']`, and awards **Auric Cells (`Cells`)** from `prestige_Reward.json` (`10`, `20`, `30` Cells).
  - **Critical Flaw**: `prestige_up` **never checks whether `bloodWebLevel == 50`**! Any authenticated client can call `POST /api/v1/Character/PrestigeUp` repeatedly at level 1 to reach Prestige 100 and farm unlimited `Cells`.

### 1.4 Ranking & Pips (`server/progression.py:41-152`)
- Stored in `kv['ranks']` (`killerPips`, `survivorPips`, `highestKillerRank`, `highestSurvivorRank`).
- `PUT /api/v1/ranks/pips` (`line 46`) overwrites `killerPips` and `survivorPips` directly from the request JSON.
- `POST /api/v1/ranks/updateSeasonInfo` (`line 65`) adds `killerPips` and `survivorPips` deltas (clamped `0..85`).
- `POST /api/v1/ranks/rewards/claimAll` (`line 115`) awards all rank threshold rewards (`Bloodpoints`, `Shards`, items from `ranksThresholds.json`) up to `highestKillerRank` / `highestSurvivorRank`.

---

## 2. Paid Pass (The Rift / Battlepass), Entitlement & Unlockables

### 2.1 How the Paid Pass Is Implemented (`server/progression.py:154-305`, `hotfix2.py:11430-11640`)
- **Rift Configuration & Custom Seasons**:
  - Base season data is loaded from `BSP_PAYLOAD/data/content/SeasonManager.json` (`_season_table` at `server/progression.py:154`).
  - Server admins can define custom seasons (`dbdmSeason`, `dbdmArt`, `dbdmRoleKey`, `dbdmRolePicture`, `dbdmShowcase`, `dbdmUpgrade2`, `dbdmValue1`, `dbdmValue2`) via `/admin/api/seasons` (`server/admin.py:345-415`), which `hotfix2.py` (`_dbdm_bp_patch` at `lines 11439-11638`) renders in the client UI.
- **Pass Entitlement State (`kv['rift']`)**:
  - `passId = 1`: Free Track only (default).
  - `passId = 2`: Premium Pass (`UPGRADE_COST_CELLS = 900` `Cells`).
  - `passId = 3`: Premium Pass Plus (`UPGRADE_PLUS_COST_CELLS = 2000` `Cells`, immediately adds `200` Rift Stars = `20` tiers and grants `upgrade2Items`).
  - `stars`: `10` Rift Stars per tier (`STARS_PER_TIER = 10`, `TIER_COST_CELLS = 100` `Cells`).
- **How Pass Entitlement & Tiers AreValidated (`server/progression.py:160-269`)**:
  - `POST /api/v1/archive/v1/combat/pass/upgrade` (`rift_upgrade`) checks the user's `Cells` balance in `wallet`, deducts `900` or `2000` `Cells`, and sets `passId = 2` or `3`.
  - `POST /api/v1/archive/v1/combat/tier/buy` (`rift_buy_tier`) deducts `100 * tiers` `Cells` from `wallet` and increases `stars` and `tier`.
  - `POST /api/v1/archive/v1/combat/tier/claim` (`rift_claim`) checks `tier <= state['tier']` and `passId >= 2` (for premium rewards), then grants all characters, outfits, currencies, and items for those tiers.
- **How Pass Progression & Entitlement Are Bypassed**:
  1. **Free Premium Pass & Tier 80 via `grantCurrency`**: Call `POST /api/v1/extensions/rewards/grantCurrency` to mint `10,000 Cells`, then call `rift_upgrade` (`passId: 3`) and `rift_buy_tier` (`tiers: 80`), then `rift_claim` (`claimAll: true`).
  2. **Free Rift Stars via Tome Story State (`server/progression.py:271-305`)**: When `PUT /api/v1/states/story/type/{stateType}/id/{objectId}` (`server/profile.py:157`) is called, `progression.on_story_put(app, uid, data)` awards **10 Rift Stars (1 full tier)** for every unique node key in `data['nodeInfo']` whose `status == 3`! Submitting synthetic node IDs with `status: 3` advances the Rift to max tier for free.
  3. **Promo Codes (`acc.redeem`)**: Remote server supports promo codes created via `/admin/api/codes` (`server/admin.py:570`) that grant currencies, items, or pass entitlements via `POST /dbdm/online/call` (`{"fn": "acc.redeem", "args": {"code": "..."}}`).

### 2.2 New Account Seeding & Unlockables (`server/inventory.py:85-171`, `BSP_PAYLOAD/config.json:6-20`)
- When a new account is created (`seed_account` at `server/inventory.py:85`), it is seeded from `config.json -> new_account`:
  - **Offline Mode (`127.0.0.1:21749`)**: `all_characters: true`, `all_perks: true`, `all_items: true`, `all_outfits: true`, `Bloodpoints: 2,000,000`, `Shards: 100,000`, `Cells: 50,000`, `DrawTicket: 100`.
  - **Online Mode (`2.31.101.228:21760`)**: Controlled by the remote server's `config.json` (where `Cells`, `outfits`, `characters`, and `gacha` items are monetized/restricted).
- **Direct Unlockable Bypass in Online Mode**:
  - Even when the remote server restricts `new_account` defaults, `POST /api/v1/dedicatedServer/update/inventories` (`server/match.py:79`), `POST /api/v1/extensions/rewards/grantCurrency` (`server/match.py:75`), and `POST /api/v1/blood-market/save/persistent-data` (`server/bloodmarket.py:88`) remain active on `app.api` and are proxied directly to `2.31.101.228:21760` by `online_proxy.maybe_proxy` (`server/online_proxy.py:105`).

