# 04 — Transport, Framing, Signing & Pinning

## 1. Transport Stack Overview

The client uses three distinct transport channels:
1. **Local Loopback HTTP/1.1 & WebSocket (`127.0.0.1:21749`)**: Standard plaintext HTTP/1.1 and RFC 6455 WebSocket (`server/transport.py`, `server/rtm.py`). Requests carry a path prefix `/<path_key>/` (`12`-byte random hex string generated per boot in `const.ues` `<boot>:116`), except `no_key` routes (`/dbdm/online/status`, `/dbdm/online/call`, `/dbdm/relay/start`, `/dbdm/relay/stop`, `/dbdm/relay/info`).
2. **Remote Control & API Tunnel (`netproto` v3 over TLS 1.2+ TCP to `2.31.101.228:21760`)**: Implemented in `server/online.py` (`OnlineClient`) and `server/netproto.py`. Multiplexes authentication, HTTP API proxying (`REQ`/`RESP`), RPC calls (`CALL`/`CALL_RESP`), matchmaking push events (`PUSH`), and heartbeats (`PING`/`PONG`) over a single persistent TLS socket.
3. **Remote Match UDP Relay (`netproto` UDP framing to `2.31.101.228:21761`)**: Implemented in `server/relay_agent.py` (`HostRelay`, `ClientRelay`) and `server/netproto.py` (`udp_pack`, `udp_unpack`). Wraps raw UE4 `IpNetDriver` UDP packets (local port `7777`) in a 21-byte header+MAC envelope.

---

## 2. TLS Configuration & Certificate Pinning (`server/online.py:178-193`)

In `OnlineClient._tls(self, raw)` (`server/online.pyc.txt:626-711`), the client disables standard CA chain and hostname verification (`check_hostname = False`, `verify_mode = ssl.CERT_NONE`) and enforces **custom SHA-256 leaf certificate pinning** over the DER-encoded peer certificate:

```python
# Reconstructed from server_online.pyc.txt (lines 626-711, file=<bsp>:178-193)
def _tls(self, raw):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    sock = ctx.wrap_socket(raw, server_hostname='dbdm')
    der = sock.getpeercert(binary_form=True)
    if not der:
        sock.close()
        raise ProtoError('no peer cert')
    fp = hashlib.sha256(der).hexdigest().lower()
    if self.pins and fp not in self.pins:
        sock.close()
        raise ProtoError('cert pin mismatch: %s' % fp)
    return sock
```

- **Pinned SHA-256 Digest**: `aca6b6f709234fb0a8c1877a2b97ceda2bc3bd290eed9f5b1340fa0a29294a7f` (configured in both `BSP_PAYLOAD/config.json:27` and encrypted `BSP_PAYLOAD/data/servers.bin`).
- **Bypass Condition**: Notice `if self.pins and fp not in self.pins:` — if `pins` is empty in `config.json` or patched in memory (`OnlineClient.pins = set()`), any self-signed TLS certificate is accepted.

---

## 3. `netproto` v3 TCP Framing, Key Derivation & Signing (`server/netproto.py`)

### 3.1 Constants & Opcodes (`server/netproto.pyc.txt:1-147`)

- `VERSION = 3`
- `MAX_FRAME = 8388608` (8 MB)
- `TAG_LEN = 16` (16-byte truncated HMAC-SHA256)
- `ZFLAG = 0x80` (128 — top bit of `typ` indicates `zlib.compress(payload, 6)` for payloads `>= 1024` bytes)
- `HDR = struct.Struct('>IBQ')` (13 bytes big-endian: `uint32 length`, `uint8 typ`, `uint64 seq`)
- **Pre-shared `build_key`**:
  - Literal Hex (`config.json:28`, `servers.bin`): `a757b7d84ff02772a239cb262b2e6bbb9cb9bcd1dde87066fd9b3d7f940caa08`

| Opcode Constant | Value | Direction | JSON Payload Format & Purpose |
|---|---:|---|---|
| `HELLO` | `1` | Client -> Server | `{"v": 3, "deviceId": "<id>", "nonce": "<32-hex>", "build": "2026.11.09.1", "proof": "<64-hex>", "hwid": {...}, "resume": "<sid>"}` |
| `WELCOME` | `2` | Server -> Client | `{"sessionId": "<sid>", "uid": "<uid>", "nonce": "<32-hex>", "needLogin": bool, "unlocked": bool, "registered": bool, "resume": bool}` |
| `PING` | `3` | Bidirectional | `{"t": <ms>}` — Keepalive ping every 15s (`PING_EVERY = 15.0`) |
| `PONG` | `4` | Bidirectional | `{"t": <ms>}` — Keepalive response |
| `KICK` | `5` | Server -> Client | `{"reason": "banned"|"outdated"|"kicked"|"serverFull", "message": "<str>"}` |
| `REQ` | `10` | Client -> Server | `{"id": <int>, "m": "<HTTP_METHOD>", "p": "<path>", "q": "<query>", "h": {<headers>}, "b": "<b64_body>"}` |
| `RESP` | `11` | Server -> Client | `{"id": <int>, "s": <http_status>, "ct": "<content_type>", "b": "<b64_body>", "j": <parsed_json>}` |
| `CALL` | `12` | Client -> Server | `{"id": <int>, "fn": "<rpc_name>", "args": {<dict>}}` |
| `CALL_RESP` | `13` | Server -> Client | `{"id": <int>, "ok": bool, "data": {...}, "code": "<err>", "message": "<msg>"}` |
| `PUSH` | `20` | Server -> Client | `{"topic": "<str>", "data": {...}}` — Async matchmaking/party/broadcast events |

### 3.2 Key Derivation & Handshake (`server/netproto.py:25-45`, `server/online.py:195-248`)

```python
# Reconstructed from server_netproto.pyc.txt (lines 149-294, file=<bsp>:25-45)
def hkdf(ikm, salt, info, length=32):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out, prev, i = b'', b'', 1
    while len(out) < length:
        prev = hmac.new(prk, prev + info + bytes([i]), hashlib.sha256).digest()
        out += prev
        i += 1
    return out[:length]

def device_secret(build_key, device_id):
    return hkdf(build_key, b'dbdm-dev-v3', device_id.encode('utf-8'), 32)

def hello_proof(dev_secret, nonce_c, build_id):
    msg = b'hello-v3|' + nonce_c + b'|' + build_id.encode('utf-8')
    return hmac.new(dev_secret, msg, hashlib.sha256).hexdigest()

def session_keys(dev_secret, nonce_c, nonce_s, session_id):
    raw = hkdf(dev_secret, nonce_c + nonce_s, b'dbdm-sess-v3|' + session_id.encode('utf-8'), 96)
    # Returns (c2s_key, s2c_key, relay_key) — three 32-byte keys
    return (raw[:32], raw[32:64], raw[64:96])
```

**Handshake Sequence (`OnlineClient._handshake`, `server/online.py:195`)**:
1. Both sides initialize `Sealer(build_key, build_key)` where `tx_key = rx_key = build_key`.
2. Client generates `nonce_c = os.urandom(16)` and sends `HELLO` (`typ=1, seq=1`) sealed under `build_key` (`dir = b'c2s'`), containing `proof = hello_proof(device_secret(build_key, device_id), nonce_c, "2026.11.09.1")`.
3. Server responds with `WELCOME` (`typ=2, seq=1`) sealed under `build_key` (`dir = b's2c'`), containing `nonce_s = bytes.fromhex(welcome['nonce'])` and `session_id`.
4. Both sides switch to `c2s_key, s2c_key, _ = session_keys(dev_secret, nonce_c, nonce_s, session_id)` and create a fresh `Sealer(c2s_key, s2c_key)` (resetting `tx_seq` and `rx_seq` to `0`).

### 3.3 Frame Wire Format & `Sealer` (`server/netproto.py:47-105`)

**Important Finding**: `Sealer` **does not encrypt** frame payloads; it only optionally compresses payloads (`>= 1024` bytes) with `zlib` and authenticates them with a 16-byte truncated HMAC-SHA256 tag:

```
+-------------------+---------------+-------------------+-----------------------+-------------------------+
| Length n (4B BE)  | Type (1B)     | Seq (8B BE)       | Payload (var)         | HMAC-SHA256[:16] (16B)  |
| 9 + len(p) + 16   | opcode | 0x80 | strictly > rx_seq | UTF-8 JSON (or zlib)  | over (dir||typ||seq||p) |
+-------------------+---------------+-------------------+-----------------------+-------------------------+
```

```python
# Reconstructed from server_netproto.pyc.txt (lines 296-548, file=<bsp>:47-93)
class Sealer:
    def __init__(self, tx_key, rx_key, server=False):
        self.tx_key = tx_key
        self.rx_key = rx_key
        self.tx_dir = b's2c' if server else b'c2s'
        self.rx_dir = b'c2s' if server else b's2c'
        self.tx_seq = 0
        self.rx_seq = 0
        self.tx_lock = threading.Lock()

    def pack(self, typ, obj):
        raw = obj if isinstance(obj, (bytes, bytearray)) else json.dumps(obj, separators=(',', ':')).encode('utf-8')
        if len(raw) >= 1024:
            comp = zlib.compress(raw, 6)
            if len(comp) < len(raw):
                raw = comp
                typ |= ZFLAG  # 0x80
        with self.tx_lock:
            self.tx_seq += 1
            seq = self.tx_seq
            tag = self._mac(self.tx_key, self.tx_dir, typ, seq, raw)
            n = 9 + len(raw) + TAG_LEN
            return HDR.pack(n, typ, seq) + raw + tag

    def _mac(self, key, d, typ, seq, payload):
        h = hmac.new(key, d, hashlib.sha256)
        h.update(bytes([typ]))
        h.update(struct.pack('>Q', seq))
        h.update(payload)
        return h.digest()[:TAG_LEN]
```

---

## 4. UDP Relay Framing (`server/netproto.py:119-144`, `server/relay_agent.py`)

When an online match begins, matchmaking issues a relay ticket (`{"host": "2.31.101.228", "port": 21761, "allocId": <uint32>, "key": "<64-hex>"}`). Both the match host (`HostRelay`, bridging `127.0.0.1:7777`) and survivor clients (`ClientRelay`, listening on a local ephemeral port) exchange UDP datagrams with `2.31.101.228:21761`:

```
+---------------+------------------+-----------------+----------------+------------------------+----------------------+
| Kind (1B)     | AllocId (4B BE)  | PeerId (4B BE)  | Seq (4B BE)    | HMAC-SHA256[:8] (8B)   | Raw UE4 UDP Payload  |
+---------------+------------------+-----------------+----------------+------------------------+----------------------+
```

- **UDP Kinds**: `UDP_BIND = 1`, `UDP_DATA = 2`, `UDP_PING = 3`, `UDP_BYE = 4`
- **MAC Calculation (`udp_pack` at `server/netproto.py:127`)**:
  ```python
  head = UDP_HDR.pack(kind, alloc_id, peer_id, seq & 0xFFFFFFFF)
  tag = hmac.new(key, head + payload, hashlib.sha256).digest()[:8]
  return head + tag + payload
  ```

---

## 5. `FullProfile` Save State Encryption (`server/dbdsave.py`)

Player save files sent to `POST /api/v1/players/{playerId}/states/binary` use a two-layer custom envelope (`server/dbdsave.pyc.txt:1-215`):
1. **Inner Layer (`INNER = 'DbdDAQEB'`)**:
   - UTF-16LE encode the JSON string: `raw = text.encode('utf-16-le')`.
   - Prepend 4-byte little-endian uncompressed byte length and compress with `zlib`: `z = struct.pack('<I', len(raw)) + zlib.compress(raw)`.
   - Prepend `'DbdDAQEB'` to Base64(`z`): `inner = ('DbdDAQEB' + base64.b64encode(z).decode('ascii')).encode('ascii')`.
2. **Outer Layer (`OUTER = 'DbdDAgAC'`)**:
   - Pad `inner` with `b'\x00'` to a 16-byte multiple.
   - Subtract `1` (`mod 256`) from every byte: `shifted = bytes(((b - 1) & 0xFF for b in padded))`.
   - Encrypt with **AES-256-ECB** using hardcoded key `KEY = b'5BCC2D6A95D4DF04A005504E59A9B36E'`.
   - Prepend `'DbdDAgAC'` to Base64(ciphertext).

---

## 6. Server Directory (`servers.bin`) & OTA Hotfix Signing (`server/server_dir.py`, `server/hotfix_ota.py`)

- **Ed25519 Public Key**: `e316661037589f083f825844af31df157860f1bc09a38f8482a112205ca606df` (`server/server_dir.py:13`, `server/hotfix_ota.py:16`).
- **`servers.bin` Encryption (`server/server_dir.py:12-40`)**:
  - Header: `MAGIC = b'DBDMSL01'` (8 bytes) + `nonce` (16 bytes) + `sig` (64-byte Ed25519 signature over `MAGIC + nonce + ct`) + `ct`.
  - Decryption: `pt = ct ^ SHA256_CTR(key=6cd0edf80b272ffe282e9635c6d9fc13cb3e0e6fdb99f3f6a54ce54d66287bce, nonce)` followed by `zlib.decompress(pt)`.
- **OTA Hotfix Verification (`server/hotfix_ota.py:56-77`)**:
  - Remote RPC `hotfix.check` returns `{"code": "<py_src>", "sig": "<128-hex>", "version": <int>, "targetBuild": "<str>"}`.
  - Verified via Ed25519 over `b'DBDM-HOTFIX-v1\n' + version.encode() + b'\n' + target_build.encode() + b'\n' + sha256(code).digest()`.

