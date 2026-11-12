import struct
import sys

sys.path.insert(0, r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch")
import dump_outline as do

data = do.data

def disasm_simple(rva, count=32):
    if not (0x04000000 <= rva < 0x0E65A6A0):
        return []
    insns = struct.unpack_from(f"<{count}I", data, rva)
    lines = []
    for i, insn in enumerate(insns):
        pc = rva + i * 4
        desc = f"0x{pc:08X}: {insn:08X}"
        # Check B / BL
        if (insn & 0x7C000000) == 0x14000000:
            is_bl = (insn >> 31) & 1
            imm26 = insn & 0x3FFFFFF
            if imm26 & (1 << 25):
                imm26 -= 1 << 26
            target = pc + (imm26 << 2)
            desc += f"  {'BL' if is_bl else 'B'} 0x{target:08X}"
        # Check LDR/STR unsigned offset (32/64-bit / float / byte)
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

print("\n" + "#" * 95)
print("# 1. UDBDOutlineComponent UFunction Exec & Native Implementations")
print("#" * 95)
q = struct.unpack_from("<10Q", data, 0x0EBC33F8)
func_arr = q[4]
num_funcs = (q[7] >> 32) & 0xFFFFFFFF
for fi in range(num_funcs):
    f_zcon, f_name_ptr = struct.unpack_from("<2Q", data, func_arr + fi * 16)
    fname = do.read_cstr(f_name_ptr)
    # Parse f_zcon for ADRP/ADD of FFunctionParams
    regs = {}
    for i in range(25):
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
    fparams = regs.get(0, 0) or regs.get(1, 0)
    if 0x0E000000 <= fparams <= 0x0F700000:
        fq = struct.unpack_from("<10Q", data, fparams)
        # Find exec thunk in fq[5..9]
        exec_rva = 0
        for val in fq[4:]:
            if 0x04000000 <= val < 0x0E65A6A0:
                exec_rva = val
                break
        print(f"\n--- {fname} (ZConstruct=0x{f_zcon:08X}, FFuncParams=0x{fparams:08X}, Exec=0x{exec_rva:08X}) ---")
        if exec_rva:
            exec_lines = disasm_simple(exec_rva, 40)
            # Find last BL in exec_lines before RET
            bl_targets = []
            for l in exec_lines:
                if "  BL 0x" in l:
                    t = int(l.split("  BL 0x")[1], 16)
                    bl_targets.append(t)
            print(f"  Exec BL targets: {[hex(x) for x in bl_targets]}")
            if bl_targets:
                native_rva = bl_targets[-1]
                print(f"  Native candidate 0x{native_rva:08X}:")
                for nl in disasm_simple(native_rva, 32):
                    print("    " + nl)

print("\n" + "#" * 95)
print("# 2. Find FClassParams by StaticClass RVA and Dump Relevant Properties")
print("#" * 95)

def find_fclassparams_by_sc(sc_rva):
    # Follow unconditional B at start if thunk
    first_insn = struct.unpack_from("<I", data, sc_rva)[0]
    real_sc = sc_rva
    if (first_insn & 0xFC000000) == 0x14000000:
        imm26 = first_insn & 0x3FFFFFF
        if imm26 & (1 << 25):
            imm26 -= 1 << 26
        real_sc = sc_rva + (imm26 << 2)
    for target in (sc_rva, real_sc):
        pat = struct.pack("<Q", target)
        idx = data.find(pat, 0x0E000000)
        while idx != -1:
            # Check if idx looks like FClassParams (q[0] == target, q[7] small ints, q[8] small ints)
            q = struct.unpack_from("<10Q", data, idx)
            num_deps = q[7] & 0xFFFFFFFF
            num_funcs = (q[7] >> 32) & 0xFFFFFFFF
            num_props = q[8] & 0xFFFFFFFF
            if num_deps < 50 and num_funcs < 1000 and num_props < 1000 and (q[5] == 0 or 0x0E000000 <= q[5] <= 0x0F700000):
                return idx
            idx = data.find(pat, idx + 8)
    return None

target_classes = [
    ("AActor", 0x0C7ABD84),
    ("APawn", 0x0C9A47F8),
    ("ACharacter", 0x0C7DD0AC),
    ("ADBDBasePlayer", 0x0770F5EC),
    ("ADBDPlayer", 0x0780E190),
    ("ACamperPlayer", 0x07741D98),
    ("ASlasherPlayer", 0x07B89AD0),
    ("AInteractable", 0x07991A34),
    ("AGenerator", 0x078C5CA4),
    ("APallet", 0x07AA7E14),
    ("AWindow", 0x07CA2E18),
    ("ATotem", 0x07C2EB58),
    ("AEscapeDoor", 0x0787860C),
    ("AHatch", 0x078FF74C),
    ("ASearchable", 0x07B69FC0),
    ("AMeatHook", 0x07A2AD08),
]

for cname, sc in target_classes:
    cp = find_fclassparams_by_sc(sc)
    if cp:
        do.dump_fclassparams(cp, filter_props=(cname in ("AActor", "ADBDPlayer", "ACamperPlayer", "ASlasherPlayer")))
    else:
        print(f"Could not find FClassParams for {cname} (0x{sc:08X})")

# Also dump UBatchMeshCommands and UBaseOutlineRenderStrategy
for sc_sub in (0x077F22CC,):
    info = do.parse_static_class(0x077F1C04)
    if info and info["super_sc"]:
        cp_super = find_fclassparams_by_sc(info["super_sc"])
        if cp_super:
            do.dump_fclassparams(cp_super, filter_props=False)
