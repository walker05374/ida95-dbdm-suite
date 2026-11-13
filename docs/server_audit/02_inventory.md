# 02 — App Bundle Inventory, Protections & Architecture

## 1. Bundle Metadata (`info.json` & `AndroidManifest.xml`)

- **Source Bundle Path**: `C:\Users\walker\Desktop\dbdmonlin`
- **Merged Extraction Path**: `E:\ida95-dbdm-suite\relatorios\server_audit\_work`
- **Package Name**: `com.netease.dbdena` (`info.json`, `AndroidManifest.xml`)
- **App Label**: `Dead by Daylight Mobile` (`split_config.pt.apk` / `base.apk`: `Dead by Daylight M`)
- **Version Name**: `1.292949.292949` (`versionCode = 292949`)
- **SDK Levels**: `minSdkVersion = 24` (Android 7.0), `targetSdkVersion = 33` (Android 13), `compileSdkVersion = 33`
- **Application Class**: `com.netease.ntunisdk.base.ApplicationNetease`
- **Main Launch Activity**: `com.bhvr.deadbydaylight.DBDActivity` (subclasses `com.epicgames.ue4.GameActivity`)

## 2. Engine Identification (Unreal Engine 4.27 + Embedded CPython 3.8 — Not Unity/IL2CPP)

Contrary to initial Unity/IL2CPP assumptions, static inspection proves this build is **Unreal Engine 4.27 (`++UE4+Release-4.27`)** coupled with **NetEase's embedded CPython 3.8 (`PakImporter`)** scripting runtime:
- **Zero Unity artifacts**: Neither `libil2cpp.so` nor `global-metadata.dat` exists in any of the 35 split APKs.
- **Core Native Engine**: `split_config.arm64_v8a.apk -> lib/arm64-v8a/libUE4.so` (263,651,496 bytes, SHA-256 `1ca51f84065cf096ad51c1f346684bf97d2dfe0afd2f1b64b98018562311c38a`).
- **UE4 Asset Containers**: `split_obbassets.apk -> assets/main.292949.com.netease.dbdena.obb` (3,701,372,499 bytes) and `split_packchunks.apk -> assets/pakchunk0-Android_ASTC-*.pak` (7 `.pak` files totaling 436,053,131 bytes).
- **Encrypted Python Script Archive**: `split_packchunks.apk -> assets/pakchunk0-Android_ASTC-Script.pak` (40,466,851 bytes) contains 2,618 `.ues` files (AES-128-ECB encrypted Python 3.8 `marshal` code objects).

## 3. Complete Split-APK Map (35 Splits)

| Split File | Size (Bytes) | Category | Key Contents & Role |
|---|---:|---|---|
| `base.apk` | 29,213,540 | Base APK | `AndroidManifest.xml`, `classes.dex`–`classes4.dex`, `assets/main.obb.png`, `res/`, `okhttp3/internal/publicsuffix/` |
| `split_config.arm64_v8a.apk` | 292,508,993 | Native ABI (`arm64-v8a`) | 19 native `.so` libraries under `lib/arm64-v8a/` including `libUE4.so`, `libdbdmguard.so`, `libVoiceChangeCore.so` |
| `split_obbassets.apk` | 3,701,377,090 | Install-time Asset Pack | `assets/main.292949.com.netease.dbdena.obb` (3.70 GB main UE4 Pak archive) |
| `split_packchunks.apk` | 436,062,566 | Install-time Asset Pack | `assets/Build.ach`, `assets/PatchVersion.txt`, and 7 `pakchunk0-Android_ASTC-*.pak` archives (`Script.pak`, `Logic.pak`, `UI.pak`, `Startup.pak`, `Material.pak`, `WwiseAudio.pak`, base `.pak`) |
| `split_config.mdpi.apk` | 238,242 | Density (`mdpi`) | Launcher icons & notification drawables |
| `split_config.hdpi.apk` | 341,668 | Density (`hdpi`) | Launcher icons & notification drawables |
| `split_config.xhdpi.apk` | 407,341 | Density (`xhdpi`) | Launcher icons & notification drawables |
| `split_config.xxhdpi.apk` | 599,517 | Density (`xxhdpi`) | Launcher icons & notification drawables |
| `split_config.xxxhdpi.apk` | 735,691 | Density (`xxxhdpi`) | Launcher icons & notification drawables |
| `split_config.ar.apk` .. `split_config.zh.apk` (26 files) | 22,588 – 42,676 each | Locale Splits (26 langs) | `ar`, `bg`, `cs`, `da`, `de`, `el`, `en`, `es`, `fi`, `fr`, `hi`, `hr`, `in`, `it`, `iw`, `ja`, `ko`, `ms`, `nl`, `pl`, `pt`, `ro`, `ru`, `sv`, `th`, `tr`, `uk`, `vi`, `zh` string resources |

## 4. `split_packchunks.apk` Payload Breakdown

| Asset Path | Size (Bytes) | Encryption / Format | Purpose |
|---|---:|---|---|
| `assets/Build.ach` | 53 | Plaintext INI (`Version=292949`) | Build version marker |
| `assets/PatchVersion.txt` | 6 | Plaintext (`292949`) | Patch version marker |
| `assets/pakchunk0-Android_ASTC.pak` | 224,251 | UE4 Pak v11 (`0x5A6F12E1`) | Core engine/config INI files (`DefaultEngine.ini`, `DefaultGame.ini`) |
| `assets/pakchunk0-Android_ASTC-Logic.pak` | 31,422,068 | UE4 Pak v11 | Gameplay Blueprints & Tunables (`TunableValue` DataTables) |
| `assets/pakchunk0-Android_ASTC-Material.pak` | 48,147,534 | UE4 Pak v11 | Shader libraries & materials |
| `assets/pakchunk0-Android_ASTC-Script.pak` | 40,466,851 | UE4 Pak v11 + `.ues` AES-128-ECB | 2,618 Python `.ues` bytecode modules + injected `const.ues` (`0x021a7ce6`) & `patch/pconst.ues` (`0x0223d32f`) |
| `assets/pakchunk0-Android_ASTC-Startup.pak` | 15,729,820 | UE4 Pak v11 | Startup UI / splash assets |
| `assets/pakchunk0-Android_ASTC-UI.pak` | 153,886,089 | UE4 Pak v11 | UMG widgets & textures |
| `assets/pakchunk0-Android_ASTC-WwiseAudio.pak` | 146,176,518 | UE4 Pak v11 | Wwise soundbanks |

## 5. Native Libraries (`split_config.arm64_v8a.apk -> lib/arm64-v8a/`)

| Library | Size (Bytes) | SHA-256 | Role & Security Significance |
|---|---:|---|---|
| `libUE4.so` | 263,651,496 | `1ca51f84065cf096ad51c1f346684bf97d2dfe0afd2f1b64b98018562311c38a` | Unreal Engine 4.27 + CPython 3.8 + `PakImporter` (`0x0f6892de`) + `render_imgs` (`0x051b4b1c`) with AES key at `0x028c5e10` |
| `libdbdmguard.so` | 11,336 | `0ee0dc1409a89a870ce817e766b0267f0d22d37c735a8a7820be4e3f64f8a41c` | **Custom Private Server Native Anti-Cheat Guard**; constructor (`0x11dc`) spawns watchdog thread (`0x1260`) writing `/data/data/com.netease.dbdena/files/.dbdmguard` |
| `libVoiceChangeCore.so` | 357,384 | `8197a2f15578f69f64935455a11cc27bc602fa42cc48536d2d37855fd4839d42` | Patched by private server authors: its `DT_NEEDED` entry was overwritten with `libdbdmguard.so` so `System.loadLibrary("VoiceChangeCore")` loads the guard |
| `libGameSdk.so` | 16,650,960 | `81c89598d7cd2d3bd260e7291bc5ff4c44b4874ee0931654bb3d7f3e4839cf5b` | NetEase / third-party game SDK |
| `libImmersionPlugin.so` | 2,400,248 | `5967e4cfbe8821d335c3721b56761775c82bd4a2b3461a7bf78deefcd7fd40be` | Haptics plugin |
| `libTouchSenseSDK.so` | 2,265,648 | `92b7e6fa714223a3f039d9222ec785b027b1b30f22f5bbec1726658059656603` | Haptics SDK |
| `libijksdl.so` | 1,599,512 | `8fb4e5814fb56960fb7abaa52580b4d1682325517a4d2219cd28e4eb9a1d5cb6` | IJK media player SDL |
| `libijkffmpeg.so` | 1,355,656 | `063bbad262bb355cbdb79d0d6e43a040083dc9326ef349ddae28e3461d7eb18b` | IJK media player FFmpeg |
| `libijkplayer.so` | 817,224 | `9a9ef3265d6390b7d1ffeb2358b4e386bd42670a6c3086da8d02581ca98cb63e` | IJK media player core |
| `libcc.so` | 1,039,584 | `c216e1fc3f093c57232c191477274eb11805fd54e2bc544fa17bd9f6d4f28881` | NetEase CC voice/streaming |
| `libaudio_Process.so` | 800,728 | `8f6b532f41cf3fb45fb4545d42ee4c9770e1be1c7224529dd6997a2de513d8c4` | Audio processing |
| `libopus.so` | 482,664 | `cf433ae63259ff6370cb7b719ca13275a3df66fb7d7d006684f6f17cf6ee3f28` | Opus audio codec |
| `libc++_shared.so` | 1,015,248 | `9f9f9efde8ad28ee6fd1fc5beccbe7422dc28aa4bc11c7273b219239991173a9` | Android NDK C++ runtime |
| `liborbit.so` | 190,600 | `53f88d6833c425f4ebcf02f4b86a93f94bfef7d342b2914bb702c79d52ebef63` | NetEase Orbit downloader |
| `libngp2p.so` | 145,272 | `2a30bf11a34b60e0a8d7e8d8b0ab2ee2572fa2fa85e4c2a44c3c1b3ff3cf18fc` | NetEase P2P downloader |
| `libLDLib.so` | 73,776 | `c2db431188a556f3cb4e725cd9c41beabfa0ff8be88386299d9996d544c92647` | NetEase network diagnostics |
| `libNetHTProtect.so` | 52,072 | `991246537b23a0d179fcbf04f3622db2278488c13ab996966603363b83a476bf` | NetEase HTProtect stub |
| `libgetuiext3.so` | 35,880 | `32dc56aa03e39402e28608135b8a427ec08ed0df43c882da5a9b43635be51888` | Getui push SDK |
| `libantitrace.so` | 22,520 | `32bd8095a9ab6ee6087da9cd42eb5c09b32ec29085686b8685c8cb6decfedde1` | Legacy NetEase anti-trace library |

## 6. Manifest Security Flags (`_work_manifest.txt`)

- `android:debuggable`: **Not set** (defaults to `false`).
- `android:allowBackup`: **`false`** (`AndroidManifest.xml:44`).
- `android:usesCleartextTraffic`: **`true`** (`AndroidManifest.xml:46`) — required so UE4 and Python `http.client` can talk to the embedded local HTTP proxy on `http://127.0.0.1:21749`.
- `android:networkSecurityConfig`: `@xml/network_security_config` (`AndroidManifest.xml:49`).
- `android:extractNativeLibs`: **`true`** (`AndroidManifest.xml:45`).

## 7. Multi-Layer Protection & Encryption Stack

| Layer | File / Location | Algorithm | Literal Key / Parameters |
|---|---|---|---|
| **1. UE4 `.ues` Script Encryption** | `libUE4.so @ 0x051b4b1c` (`render_imgs`) | `AES-128-ECB` (4B LE original size prefix + 16B block aligned) | Key at `libUE4.so @ 0x028c5e10`: `773571365e43303453572140657d6164` (ASCII: `w5q6^C04SW!@e}ad`) |
| **2. Split Key Material (`k1`)** | `const.ues @ 0x12bd` & `patch/pconst.ues @ 0x1539` | 24-byte XOR (`_bsp_k1()`) | `BSP_KA = 1fca4630b9f0d6ff5a40511cac76a401e3b597d640624516`<br>`kb = f025c21a0e5c40a4e51b18d31a6dc55e6e7985fe1e98a069`<br>`k1 = BSP_KA ^ kb = efef842ab7ac965bbf5b49cfb61b615f8dcc12285efae57f` |
| **3. `BSP_PAYLOAD` Container (`BSP1`)** | `patch/pconst.ues` (`587,012` bytes) | `SHA256(key + nonce + uint32_be(ctr))` XOR stream + `HMAC-SHA256` | `key = sha256(b"bullshitproj:" + k1) = 22e34ab48106bba81271b867277babbacb990e9b08875b82c362ffbe68659b6a` |
| **4. `BSP_PAYLOAD` Integrity Table** | `server/integrity.pyc` | SHA-256 verification of `data/*.json` & `data/hotfix/hotfix2.py` | `EXPECTED` dictionary with 9 SHA-256 digests (`server_integrity.pyc.txt:15-26`) |
| **5. `servers.bin` Directory** | `data/servers.bin` (`server/server_dir.pyc`) | Ed25519 signature + SHA-256 CTR stream cipher | Ed25519 PubKey: `e316661037589f083f825844af31df157860f1bc09a38f8482a112205ca606df`<br>Stream Key: `6cd0edf80b272ffe282e9635c6d9fc13cb3e0e6fdb99f3f6a54ce54d66287bce` |
| **6. `netproto` TCP/UDP Framing** | `server/netproto.pyc` | TLS 1.2+ + `HMAC-SHA256[:16]` (TCP) / `[:8]` (UDP) + `HKDF-SHA256` | `build_key = a757b7d84ff02772a239cb262b2e6bbb9cb9bcd1dde87066fd9b3d7f940caa08`<br>TLS Cert Pin: `aca6b6f709234fb0a8c1877a2b97ceda2bc3bd290eed9f5b1340fa0a29294a7f` |
| **7. `FullProfile` Save Blob** | `server/dbdsave.pyc` | `DbdDAgAC` + Base64(AES-256-ECB(byte_sub_1(`DbdDAQEB` + Base64(len + zlib(UTF-16LE))))) | AES-256-ECB Key: `5BCC2D6A95D4DF04A005504E59A9B36E` |
| **8. Anti-Cheat Attestation** | `data/hotfix/hotfix2.py:10155`, `server/acguard.pyc:9` | `HMAC-SHA256[:32]` over nonce + `libUE4.so` slice or guard state | `_DBDM_ATT_K = b7264a72b2cdb8a33afc0b8f63c22ceb024afd531ac29134ef07146a810b18d0` |

