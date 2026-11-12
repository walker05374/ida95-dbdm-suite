import struct

path = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
with open(path, "rb") as f:
    data = f.read()

from dump_outline_part5 import disasm_a64, read_cstr

# 1. Find NativeExec for GetComponentByClass and GetFirstComponentByClass
for fn_str in [b"GetComponentByClass\x00", b"GetFirstComponentByClass\x00", b"SetRenderCustomDepth\x00"]:
    s_off = data.find(fn_str, 0x02000000, 0x03600000)
    while s_off != -1:
        pat = struct.pack("<Q", s_off)
        ref = data.find(pat, 0x0E000000)
        while ref != -1:
            q0, q1 = struct.unpack_from("<2Q", data, ref)
            if 0x05000000 <= q1 < 0x0D600000:
                print(f"\nFOUND NativeExec for {read_cstr(s_off)} @ 0x{ref:08X} -> exec=0x{q1:08X}")
                el = disasm_a64(q1, 35)
                bls = [int(x.split("  BL 0x")[1], 16) for x in el if "  BL 0x" in x]
                if bls:
                    impl = bls[-1]
                    print(f"  Impl @ 0x{impl:08X}:")
                    for l in disasm_a64(impl, 45):
                        print("    " + l)
            ref = data.find(pat, ref + 8)
        s_off = data.find(fn_str, s_off + 1, 0x03600000)

# 2. Dump UDBDOutlineComponent VTable (0x0EAF5390) entries and disassemble Update/Tick methods around 0x07488000..0x0748B000
print("\n=== UDBDOutlineComponent VTable @ 0x0EAF5390 ===")
vt = struct.unpack_from("<100Q", data, 0x0EAF5390)
for idx, fn in enumerate(vt):
    if 0x07480000 <= fn <= 0x07490000 or 0x0D120000 <= fn <= 0x0D140000:
        print(f"  vt[{idx:3d}] (+0x{idx*8:03X}) = 0x{fn:08X}")

# 3. Disassemble UOutlineUpdateStrategy Ctor (0x07A9C684) and its VTable
print("\n=== UOutlineUpdateStrategy Ctor @ 0x07A9C684 ===")
for l in disasm_a64(0x07A9C684, 20):
    print("  " + l)
