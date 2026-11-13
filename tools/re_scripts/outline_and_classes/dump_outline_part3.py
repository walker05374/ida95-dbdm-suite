import struct
import sys

sys.path.insert(0, r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch")
import dump_outline as do

data = do.data

def get_fclassparams_from_sc(sc_rva):
    # Follow B or BL to Z_Construct_UClass_...
    insns = struct.unpack_from("<24I", data, sc_rva)
    candidates = [sc_rva]
    for i, insn in enumerate(insns):
        pc = sc_rva + i * 4
        if (insn & 0x7C000000) == 0x14000000:
            imm26 = insn & 0x3FFFFFF
            if imm26 & (1 << 25):
                imm26 -= 1 << 26
            candidates.append(pc + (imm26 << 2))
    for cand in candidates:
        c_insns = struct.unpack_from("<24I", data, cand)
        regs = {}
        for i, insn in enumerate(c_insns):
            pc = cand + i * 4
            if (insn & 0x9F000000) == 0x90000000:
                rd = insn & 0x1F
                immlo = (insn >> 29) & 3
                immhi = (insn >> 5) & 0x7FFFF
                imm = (immhi << 2) | immlo
                if imm & (1 << 20):
                    imm -= 1 << 21
                regs[rd] = (pc & ~0xFFF) + (imm << 12)
            elif (insn & 0xFF800000) == 0x91000000:
                rd = insn & 0x1F
                rn = (insn >> 5) & 0x1F
                imm12 = (insn >> 10) & 0xFFF
                if (insn >> 22) & 1:
                    imm12 <<= 12
                if rn in regs:
                    regs[rd] = regs[rn] + imm12
        x1 = regs.get(1, 0)
        if 0x0E000000 <= x1 <= 0x0F700000:
            q = struct.unpack_from("<10Q", data, x1)
            if 0x04000000 <= q[0] < 0x0E65A6A0:
                return x1
    return None

def disasm_a64(rva, count=40):
    if not (0x04000000 <= rva < 0x0E65A6A0):
        return []
    insns = struct.unpack_from(f"<{count}I", data, rva)
    lines = []
    for i, insn in enumerate(insns):
        pc = rva + i * 4
        desc = f"0x{pc:08X}: {insn:08X}"
        if (insn & 0x7C000000) == 0x14000000:
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
        elif (insn & 0xFFC00000) in (0xA9000000, 0xA9400000, 0x29000000, 0x29400000, 0x2D000000, 0x2D400000):
            # STP/LDP
            is_load = (insn >> 22) & 1
            imm7 = (insn >> 15) & 0x7F
            if imm7 & 0x40:
                imm7 -= 0x80
            rn = (insn >> 5) & 0x1F
            rt = insn & 0x1F
            rt2 = (insn >> 10) & 0x1F
            v = (insn >> 26) & 1
            scale = 2 + ((insn >> 31) & 1)
            off = imm7 << scale
            desc += f"  {'LDP' if is_load else 'STP'} r{rt}, r{rt2}, [X{rn}, #{hex(off)}]"
        elif insn == 0xD65F03C0:
            desc += "  RET"
            lines.append(desc)
            break
        lines.append(desc)
    return lines

print("=== 1. Inspect FFunctionParams of UDBDOutlineComponent & UBatchMeshCommands ===")
for cls_label, fcp in [("UDBDOutlineComponent", 0x0EBC33F8), ("UBatchMeshCommands", 0x0F52BF70), ("USourceBasedOutlineUpdateStrategy", 0x0ECC1E48)]:
    print(f"\n*** {cls_label} ***")
    q = struct.unpack_from("<10Q", data, fcp)
    func_arr = q[4]
    num_funcs = (q[7] >> 32) & 0xFFFFFFFF
    for fi in range(num_funcs):
        f_zcon, f_name_ptr = struct.unpack_from("<2Q", data, func_arr + fi * 16)
        fname = do.read_cstr(f_name_ptr)
        fparams = get_fclassparams_from_sc(f_zcon)
        # Or parse ADRP/ADD in f_zcon
        regs = {}
        for i in range(20):
            pc = f_zcon + i * 4
            insn = struct.unpack_from("<I", data, pc)[0]
            if (insn & 0x9F000000) == 0x90000000:
                rd = insn & 0x1F
                immlo = (insn >> 29) & 3
                immhi = (insn >> 5) & 0x7FFFF
                imm = (immhi << 2) | immlo
                if imm & (1 << 20):
                    imm -= 1 << 21
                regs[rd] = (pc & ~0xFFF) + (imm << 12)
            elif (insn & 0xFF800000) == 0x91000000:
                rd = insn & 0x1F
                rn = (insn >> 5) & 0x1F
                imm12 = (insn >> 10) & 0xFFF
                if (insn >> 22) & 1:
                    imm12 <<= 12
                if rn in regs:
                    regs[rd] = regs[rn] + imm12
        fparams = regs.get(1, 0) or regs.get(0, 0)
        if 0x0E000000 <= fparams <= 0x0F700000:
            fq = struct.unpack_from("<10Q", data, fparams)
            # Print all text RVAs in fq[1..9]
            text_slots = [(idx, val) for idx, val in enumerate(fq) if 0x04500000 <= val < 0x0D600000]
            print(f"  FUNC {fname:<34} FParams=0x{fparams:08X} TextSlots={[(i, hex(v)) for i,v in text_slots]}")
            for idx, exec_rva in text_slots:
                if idx >= 4:
                    lines = disasm_a64(exec_rva, 36)
                    bls = [int(l.split("  BL 0x")[1], 16) for l in lines if "  BL 0x" in l]
                    print(f"    Exec[{idx}]@0x{exec_rva:08X} -> BLs={[hex(x) for x in bls]}")
                    if bls:
                        impl = bls[-1]
                        print(f"    Impl@0x{impl:08X}:")
                        for il in disasm_a64(impl, 30):
                            print("      " + il)

print("\n=== 2. Dump Target Classes via get_fclassparams_from_sc ===")
target_classes = [
    ("AActor", 0x0C7ABD84),
    ("APawn", 0x0C9A47F8),
    ("ACharacter", 0x0C7DD0AC),
    ("ADBDPlayer", 0x0780E190),
    ("ACamperPlayer", 0x07741D98),
    ("ASlasherPlayer", 0x07B89AD0),
    ("AInteractable", 0x07991A34),
    ("AGenerator", 0x078C5CA4),
    ("APallet", 0x07AA7E14),
    ("AWindow", 0x07CA2E18),
    ("AEscapeDoor", 0x0787860C),
    ("AHatch", 0x078FF74C),
    ("ASearchable", 0x07B69FC0),
    ("AMeatHook", 0x07A2AD08),
    ("UBaseOutlineRenderStrategy", 0x077F5DF8),
]

for cname, sc in target_classes:
    cp = get_fclassparams_from_sc(sc)
    if cp:
        do.dump_fclassparams(cp, filter_props=(cname in ("AActor", "ADBDPlayer", "ACamperPlayer", "ASlasherPlayer")))
    else:
        print(f"Still no FClassParams for {cname} (0x{sc:08X})")
