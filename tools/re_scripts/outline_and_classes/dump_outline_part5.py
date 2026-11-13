import struct

path = r"E:\atual\ida95-dbdm-suite\binaries\libUE4_fixed.so"
with open(path, "rb") as f:
    data = f.read()

def read_cstr(addr):
    if addr <= 0 or addr >= len(data):
        return ""
    end = data.find(b"\x00", addr, addr + 256)
    if end == -1:
        return ""
    return data[addr:end].decode("latin1", "ignore")

def disasm_a64(rva, count=60):
    if not (0x04000000 <= rva < 0x0E65A6A0):
        return []
    insns = struct.unpack_from(f"<{count}I", data, rva)
    lines = []
    regs = {}
    for i, insn in enumerate(insns):
        pc = rva + i * 4
        desc = f"0x{pc:08X}: {insn:08X}"
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= 1 << 21
            regs[rd] = (pc & ~0xFFF) + (imm << 12)
            desc += f"  ADRP X{rd}, 0x{regs[rd]:08X}"
        elif (insn & 0xFF800000) == 0x91000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            if rn in regs:
                regs[rd] = regs[rn] + imm12
                desc += f"  ADD X{rd}, X{rn}, #0x{imm12:X} (-> 0x{regs[rd]:08X})"
            else:
                desc += f"  ADD X{rd}, X{rn}, #0x{imm12:X}"
        elif (insn & 0x7C000000) == 0x14000000:
            is_bl = (insn >> 31) & 1
            imm26 = insn & 0x3FFFFFF
            if imm26 & (1 << 25):
                imm26 -= 1 << 26
            target = pc + (imm26 << 2)
            desc += f"  {'BL' if is_bl else 'B'} 0x{target:08X}"
        elif (insn & 0x3B000000) == 0x39000000:
            size = (insn >> 30) & 3
            v = (insn >> 26) & 1
            opc = (insn >> 22) & 3
            imm12 = (insn >> 10) & 0xFFF
            rn = (insn >> 5) & 0x1F
            rt = insn & 0x1F
            scale = size
            if v and (opc & 2):
                scale = 4
            off = imm12 << scale
            op_str = "LDR" if (opc & 1) else "STR"
            reg_p = "Q" if (v and scale == 4) else ("S" if (v and scale == 2) else ("D" if (v and scale == 3) else ("X" if scale == 3 else ("W" if scale == 2 else "B"))))
            desc += f"  {op_str} {reg_p}{rt}, [X{rn}, #0x{off:04X}]"
        elif insn == 0xD65F03C0:
            desc += "  RET"
            lines.append(desc)
            break
        lines.append(desc)
    return lines

for label, rva, cnt in [
    ("UDBDOutlineComponent::SetTargetColor (part 2)", 0x0748A500, 45),
    ("UDBDOutlineComponent::RefreshOutlineComponent", 0x07488C4C, 50),
    ("UDBDOutlineComponent::ResetToTransparent", 0x0748A8AC, 45),
    ("UPrimitiveComponent::SetRenderCustomDepth", 0x0C063874, 40),
    ("UDBDOutlineComponent::Ctor", 0x077F22CC, 50),
]:
    print(f"\n=== {label} @ 0x{rva:08X} ===")
    for l in disasm_a64(rva, cnt):
        print("  " + l)

# Find FClassParams by searching for known property names of AActor, ADBDPlayer, AInteractable, AGenerator, APallet, AWindow, AEscapeDoor, AHatch, ASearchable
prop_Owner_map = [
    ("AActor", b"InstanceComponents\x00"),
    ("APawn", b"PlayerState\x00"),
    ("ACharacter", b"CharacterMovement\x00"),
    ("ADBDPlayer", b"_characterInventoryComponent\x00"),
    ("ADBDPlayer_alt", b"_interactionHandler\x00"),
    ("AInteractable", b"_interactors\x00"),
    ("AGenerator", b"NativePercentComplete\x00"),
    ("APallet", b"_pushable\x00"),
    ("APallet_alt", b"_state\x00"),
    ("AWindow", b"_windowCollider\x00"),
    ("AEscapeDoor", b"_openChargeable\x00"),
    ("AHatch", b"_hatchState\x00"),
    ("ASearchable", b"_hasBeenSearched\x00"),
    ("AMeatHook", b"_isSacrificed\x00"),
]

from dump_outline import dump_fclassparams

print("\n=== Find FClassParams via Property Name ===")
seen = set()
for label, p_bytes in prop_Owner_map:
    s_off = data.find(p_bytes, 0x02000000, 0x03600000)
    while s_off != -1:
        pat = struct.pack("<Q", s_off)
        p_struct = data.find(pat, 0x0E000000)
        while p_struct != -1:
            # Find pointer to p_struct in property pointer array
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
                    if cp_addr not in seen:
                        seen.add(cp_addr)
                        dump_fclassparams(cp_addr, filter_props=(label in ("AActor",)))
            p_struct = data.find(pat, p_struct + 8)
        s_off = data.find(p_bytes, s_off + 1, 0x03600000)
