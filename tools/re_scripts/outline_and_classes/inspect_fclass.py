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

for addr in (0x0EBC33F8, 0x0EBD9188, 0x0EC8F120, 0x0EBDBC00, 0x0F28ACE8, 0x0F28E8F8, 0x0F39B990):
    qwords = struct.unpack_from("<8Q", data, addr)
    print(f"=== 0x{addr:08X} ===")
    for i, q in enumerate(qwords):
        s = read_cstr(q)
        print(f"  +0x{i*8:02X}: 0x{q:016X} ({s})")
