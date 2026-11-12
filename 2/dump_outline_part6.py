import struct

path = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
with open(path, "rb") as f:
    data = f.read()

from dump_outline import dump_fclassparams
from dump_outline_part5 import disasm_a64

print("=== 1. Full Properties of AActor (0x0F23E798) ===")
dump_fclassparams(0x0F23E798, filter_props=False)

print("=== 2. Disasm 0x0D141BE4, 0x053CE6F4, 0x074887D0 (UDBDOutlineComponent ctor), 0x0748A03C ===")
for label, rva, cnt in [
    ("0x0D141BE4 (called in RefreshOutlineComponent)", 0x0D141BE4, 30),
    ("0x053CE6F4 (called in RefreshOutlineComponent)", 0x053CE6F4, 45),
    ("0x074887D0 (UDBDOutlineComponent native Ctor)", 0x074887D0, 60),
    ("0x0748A03C (called at end of ResetToTransparent)", 0x0748A03C, 45),
]:
    print(f"\n--- {label} @ 0x{rva:08X} ---")
    for l in disasm_a64(rva, cnt):
        print("  " + l)

# Find FClassParams for ACamperPlayer, ASlasherPlayer, UActorComponent, USceneComponent
for label, p_bytes in [
    ("UActorComponent", b"ComponentTags\x00"),
    ("USceneComponent", b"AttachChildren\x00"),
    ("ACamperPlayer", b"OnCamperTriggeredTrap\x00"),
    ("ACamperPlayer_alt", b"_camperExposerInstance\x00"),
    ("ASlasherPlayer", b"_terrorNavModifierComponent\x00"),
    ("ASlasherPlayer_alt", b"_slasherLight\x00"),
]:
    s_off = data.find(p_bytes, 0x02000000, 0x03600000)
    while s_off != -1:
        pat = struct.pack("<Q", s_off)
        p_struct = data.find(pat, 0x0E000000)
        while p_struct != -1:
            pat2 = struct.pack("<Q", p_struct)
            arr_item = data.find(pat2, 0x0E000000)
            if arr_item != -1:
                arr_start = arr_item
                while arr_start >= 0x0E000000:
                    prev_q = struct.unpack_from("<Q", data, arr_start - 8)[0]
                    if 0x0E000000 <= prev_q <= 0x0F700000:
                        s_ptr = struct.unpack_from("<Q", data, prev_q)[0]
                        if 0x02000000 <= s_ptr <= 0x03600000:
                            arr_start -= 8
                            continue
                    break
                cp_ref = data.find(struct.pack("<Q", arr_start), 0x0E000000)
                if cp_ref != -1:
                    cp_addr = cp_ref - 40
                    dump_fclassparams(cp_addr, filter_props=False)
            p_struct = data.find(pat, p_struct + 8)
        s_off = data.find(p_bytes, s_off + 1, 0x03600000)
