import struct

SO_PATH = r"E:\atual\ida95-dbdm-suite\binaries\libUE4_fixed.so"
with open(SO_PATH, "rb") as f:
    data = f.read()

def read_cstr(va, max_len=128):
    if va <= 0 or va >= len(data):
        return ""
    end = data.find(b"\x00", va, va + max_len)
    if end == -1:
        return ""
    try:
        return data[va:end].decode("ascii")
    except Exception:
        return ""

# In UE4, Z_Construct_UClass_* functions hold a pointer to FClassParams!
# And we already know StaticClass RVAs from RELATORIO_REV7_COMPLETO.md and native_outline_controller.hpp:
# Let's check why decode_static_class missed them: in Engine classes (like ACharacter, APlayerState, APlayerCameraManager)
# StaticClass() is at 0x0C81DD30, 0x0C9A2D90, etc. Let's inspect 0x0C81DD30 and 0x0780E190 (ADBDPlayer::StaticClass)!
for name, sc_va in [
    ("ADBDPlayer", 0x0780E190),
    ("ACharacter", 0x0C81DD30),
    ("APlayerCameraManager", 0x0C9A2D90),
    ("UOutlineUpdateStrategy", 0x07A9C5C4),
    ("UPalletOutlineUpdateStrategy", 0x07AB8A60),
    ("UVaultableOutlineUpdateStrategy", 0x07D672F8),
]:
    # Find where sc_va appears as 8-byte pointer in .data.rel.ro!
    needle = struct.pack("<Q", sc_va)
    pos = 0x0E65A000
    while True:
        idx = data.find(needle, pos, 0x0F660000)
        if idx == -1:
            break
        print(f"Found {name} StaticClass (0x{sc_va:08X}) at 0x{idx:08X}:")
        qwords = struct.unpack_from("<10Q", data, idx)
        for i, q in enumerate(qwords):
            print(f"  +0x{i*8:02X}: 0x{q:016X}")
        pos = idx + 8
