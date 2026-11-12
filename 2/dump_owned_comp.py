import struct

path = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
with open(path, "rb") as f:
    data = f.read()

for name, rva in [("0x05B542A4", 0x05B542A4), ("0x05B544FC", 0x05B544FC), ("0x06D1886C", 0x06D1886C), ("0x0EAF53A0 (UDBDOutlineComponent VTable)", 0x0EAF53A0)]:
    print(f"=== {name} ===")
    if name.startswith("0x0EAF"):
        q = struct.unpack_from("<90Q", data, rva)
        for idx, v in enumerate(q):
            print(f"  vt[{idx:2d}] (+0x{idx*8:03X}) = 0x{v:08X}")
    else:
        insns = struct.unpack_from("<25I", data, rva)
        for i, insn in enumerate(insns):
            pc = rva + i * 4
            print(f"  0x{pc:08X}: {insn:08X}")
            if insn == 0xD65F03C0:
                break
