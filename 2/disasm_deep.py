import struct

ELF_PATH = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
OUT_PATH = r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch\disasm_deep.txt"

with open(ELF_PATH, "rb") as f:
    data = f.read()

def u64(off):
    return struct.unpack_from("<Q", data, off)[0] & 0xFFFFFFFF

def u32(off):
    return struct.unpack_from("<I", data, off)[0]

def cstr(off):
    if off <= 0 or off >= len(data):
        return ""
    end = data.find(b'\x00', off)
    if end == -1 or end - off > 200:
        return ""
    return data[off:end].decode('ascii', errors='ignore')

def decode_arm64(pc, insn):
    if insn == 0xD65F03C0:
        return "RET"
    if (insn & 0x7C000000) == 0x14000000:
        is_bl = (insn >> 31) & 1
        imm26 = insn & 0x3FFFFFF
        if imm26 & (1 << 25):
            imm26 -= (1 << 26)
        target = pc + (imm26 << 2)
        return f"{'BL' if is_bl else 'B'} 0x{target:08X}"
    if (insn & 0x7E000000) == 0x34000000:
        sf = (insn >> 31) & 1
        op = (insn >> 24) & 1
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & (1 << 18):
            imm19 -= (1 << 19)
        target = pc + (imm19 << 2)
        rt = insn & 0x1F
        r = "X" if sf else "W"
        return f"{'CBNZ' if op else 'CBZ'} {r}{rt}, 0x{target:08X}"
    if (insn & 0x7E000000) == 0x36000000:
        op = (insn >> 24) & 1
        b5 = (insn >> 31) & 1
        b40 = (insn >> 19) & 0x1F
        bit = (b5 << 5) | b40
        imm14 = (insn >> 5) & 0x3FFF
        if imm14 & (1 << 13):
            imm14 -= (1 << 14)
        target = pc + (imm14 << 2)
        rt = insn & 0x1F
        return f"{'TBNZ' if op else 'TBZ'} R{rt}, #{bit}, 0x{target:08X}"
    if (insn & 0xFF000010) == 0x54000000:
        cond = insn & 0xF
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & (1 << 18):
            imm19 -= (1 << 19)
        target = pc + (imm19 << 2)
        conds = ["EQ","NE","CS","CC","MI","PL","VS","VC","HI","LS","GE","LT","GT","LE","AL","NV"]
        return f"B.{conds[cond]} 0x{target:08X}"
    if (insn & 0x9F000000) == 0x90000000:
        rd = insn & 0x1F
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= (1 << 21)
        page = (pc & ~0xFFF) + (imm << 12)
        return f"ADRP X{rd}, 0x{page:08X}"
    if (insn & 0xFF800000) == 0x91000000:
        rd = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        sh = (insn >> 22) & 1
        if sh:
            imm12 <<= 12
        return f"ADD X{rd}, X{rn}, #0x{imm12:X}"
    if (insn & 0xFF800000) == 0x11000000:
        rd = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        sh = (insn >> 22) & 1
        if sh:
            imm12 <<= 12
        return f"ADD W{rd}, W{rn}, #0x{imm12:X}"
    if (insn & 0x3B000000) == 0x39000000:
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
        mnem = "LDR" if (opc & 1) else "STR"
        reg_prefix = ("B", "H", "W", "X")[size] if not v else ("B", "H", "S", "D", "Q")[scale]
        if not v and size < 2:
            mnem += reg_prefix
            reg_prefix = "W"
        return f"{mnem} {reg_prefix}{rt}, [X{rn}, #0x{off:X}]"
    if (insn & 0x1F800000) == 0x12800000:
        sf = (insn >> 31) & 1
        opc = (insn >> 29) & 3
        hw = (insn >> 21) & 3
        imm16 = (insn >> 5) & 0xFFFF
        rd = insn & 0x1F
        mnem = {0: "MOVN", 2: "MOVZ", 3: "MOVK"}.get(opc, "MOV")
        r = "X" if sf else "W"
        return f"{mnem} {r}{rd}, #0x{imm16:X}, LSL #{hw*16}"
    if (insn & 0x7F200000) == 0x2A000000:
        sf = (insn >> 31) & 1
        rm = (insn >> 16) & 0x1F
        rn = (insn >> 5) & 0x1F
        rd = insn & 0x1F
        r = "X" if sf else "W"
        if rn == 31:
            return f"MOV {r}{rd}, {r}{rm}"
        return f"ORR {r}{rd}, {r}{rn}, {r}{rm}"
    if (insn & 0xFFFFFC1F) == 0xD63F0000:
        rn = (insn >> 5) & 0x1F
        return f"BLR X{rn}"
    if (insn & 0xFFFFFC1F) == 0xD61F0000:
        rn = (insn >> 5) & 0x1F
        return f"BR X{rn}"
    return f"RAW 0x{insn:08X}"

out = []

# Also check all functions of USkillCheck from FClassParams @ 0x0ECB5B38
cva = 0x0ECB5B38
func_arr = u64(cva + 0x20)
num_funcs = u32(cva + 0x2C)
out.append(f"=== USkillCheck All {num_funcs} Functions ===")
for i in range(num_funcs):
    finfo = func_arr + i * 0x10
    z_va = u64(finfo)
    fname_va = u64(finfo + 8)
    fname = cstr(fname_va)
    # Find FNameNativePtrPair
    packed = struct.pack("<I", fname_va)
    idx = data.find(packed, 0x0ECB3A00, 0x0ECB4000)
    exec_va = u64(idx + 8) if (idx != -1 and (idx & 7) == 0) else 0
    out.append(f"  Func[{i:02d}] {fname:38s} | Z_Construct=0x{z_va:08X} | ExecFunc=0x{exec_va:08X}")
    if 0x04000000 <= exec_va < 0x0E000000:
        for j in range(25):
            pc = exec_va + j*4
            insn = u32(pc)
            dec = decode_arm64(pc, insn)
            if not dec.startswith("RAW"):
                out.append(f"      0x{pc:08X}: {dec}")
            if insn == 0xD65F03C0:
                break

funcs_to_dump = [
    ("USkillCheck::IsDisplayed_impl_area", 0x06E16D00, 120),
    ("USkillCheck::Local_OnSkillCheckSuccessTrigger_impl", 0x06E16E2C, 150),
    ("USkillCheck::Local_ActivateCustomSkillCheck_impl", 0x06E17384, 280),
    ("USkillCheck::Local_ActivateSkillCheck_impl", 0x06E18208, 120),
    ("USkillCheck::OnSkillCheckInput_impl", 0x06E18D54, 450),
    ("ACamperPlayer::Escape_impl", 0x06F3FABC, 160),
    ("ACamperPlayer::SetImmobilized_impl", 0x06F400D8, 150),
    ("ACamperPlayer::Local_SetImmobilized_impl", 0x06F44FC0, 120),
]

for label, start_va, count in funcs_to_dump:
    out.append(f"\n=== {label} @ 0x{start_va:08X} ({count} insns) ===")
    for i in range(count):
        pc = start_va + i*4
        insn = u32(pc)
        dec = decode_arm64(pc, insn)
        if not dec.startswith("RAW"):
            out.append(f"  0x{pc:08X}: {dec}")

with open(OUT_PATH, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
print("Wrote", len(out), "lines")
