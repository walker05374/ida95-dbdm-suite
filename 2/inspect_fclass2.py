import struct

SO_PATH = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
with open(SO_PATH, "rb") as f:
    data = f.read()

qwords = struct.unpack_from("<12Q", data, 0x0EBC33F8)
for i, q in enumerate(qwords):
    print(f"0x0EBC33F8 +0x{i*8:02X}: 0x{q:016X}")

print("\nStaticClass 0x077F1C04:")
for off in range(0x077F1C04, 0x077F1C04 + 96, 4):
    insn = struct.unpack_from("<I", data, off)[0]
    print(f"  0x{off:08X}: 0x{insn:08X}")
