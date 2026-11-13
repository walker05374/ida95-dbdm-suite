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

def read_wstr(addr):
    if addr <= 0 or addr >= len(data):
        return ""
    out = []
    for i in range(128):
        if addr + i * 2 + 2 > len(data):
            break
        ch = struct.unpack_from("<H", data, addr + i * 2)[0]
        if ch == 0:
            break
        out.append(chr(ch) if 32 <= ch < 127 else "?")
    return "".join(out)

PROP_TYPES = {
    0x00: "Byte", 0x01: "Int8", 0x02: "Int16", 0x03: "Int", 0x04: "Int64",
    0x05: "UInt16", 0x06: "UInt32", 0x07: "UInt64", 0x08: "UnsizedInt", 0x09: "UnsizedUInt",
    0x0A: "Float", 0x0B: "Double", 0x0C: "Bool", 0x0D: "SoftClass", 0x0E: "WeakObject",
    0x0F: "LazyObject", 0x10: "SoftObject", 0x11: "Class", 0x12: "Object", 0x13: "Interface",
    0x14: "Name", 0x15: "Str", 0x16: "Array", 0x17: "Map", 0x18: "Set", 0x19: "Struct",
    0x1A: "Delegate", 0x1B: "InlineMulticastDelegate", 0x1C: "SparseMulticastDelegate",
    0x1D: "Text", 0x1E: "Enum", 0x1F: "FieldPath",
}

def decode_arm64_logical_imm(n, imms, immr):
    if n == 0:
        s = imms & 0x1F
        r = immr & 0x1F
        welem = (1 << (s + 1)) - 1
        return ((welem >> r) | (welem << (32 - r))) & 0xFFFFFFFF
    return 0

def decode_bool_setbit(fn_rva):
    if not (0x04000000 <= fn_rva < 0x0E65A6A0):
        return f"val={hex(fn_rva)}"
    insns = struct.unpack_from("<10I", data, fn_rva)
    info = []
    for insn in insns:
        if (insn & 0xFFC00000) == 0x39400000:
            off = (insn >> 10) & 0xFFF
            info.append(f"LDRB[+0x{off:04x}]")
        elif (insn & 0xFFC00000) == 0x39000000:
            off = (insn >> 10) & 0xFFF
            info.append(f"STRB[+0x{off:04x}]")
        elif (insn & 0xFFC00000) == 0x79400000:
            off = ((insn >> 10) & 0xFFF) * 2
            info.append(f"LDRH[+0x{off:04x}]")
        elif (insn & 0xFFC00000) == 0xB9400000:
            off = ((insn >> 10) & 0xFFF) * 4
            info.append(f"LDRW[+0x{off:04x}]")
        elif (insn & 0xFF800000) == 0x52800000:
            imm16 = (insn >> 5) & 0xFFFF
            hw = (insn >> 21) & 3
            info.append(f"MOV #{hex(imm16 << (hw*16))}")
        elif (insn & 0xFF800000) == 0x32000000:
            n = (insn >> 22) & 1
            immr = (insn >> 16) & 0x3F
            imms = (insn >> 10) & 0x3F
            mask = decode_arm64_logical_imm(n, imms, immr)
            info.append(f"ORR_mask={hex(mask)}")
        elif insn == 0xD65F03C0:
            break
    return " ".join(info) + f" (SetBit@{hex(fn_rva)})"

def parse_static_class(sc_rva):
    if not (0x04000000 <= sc_rva < 0x0E65A6A0):
        return None
    # Only follow unconditional B at offset 0
    first_insn = struct.unpack_from("<I", data, sc_rva)[0]
    if (first_insn & 0xFC000000) == 0x14000000:
        imm26 = first_insn & 0x3FFFFFF
        if imm26 & (1 << 25):
            imm26 -= 1 << 26
        sc_rva = sc_rva + (imm26 << 2)
    regs = {}
    for i in range(45):
        pc = sc_rva + i * 4
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
        elif (insn & 0xFF800000) == 0x52800000:
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            hw = (insn >> 21) & 3
            regs[rd] = imm16 << (hw * 16)
        elif (insn & 0x7C000000) == 0x14000000 and (insn >> 31) == 1:
            break
    pkg = read_wstr(regs.get(0, 0))
    cls_name = read_wstr(regs.get(1, 0))
    if not cls_name or not pkg.startswith("/Script/"):
        return None
    return {
        "sc_rva": sc_rva,
        "pkg": pkg,
        "name": cls_name,
        "size": regs.get(4, 0),
        "super_sc": regs.get(9, 0),
        "ctor": regs.get(12, 0),
    }

def dump_fclassparams(addr, filter_props=False):
    q = struct.unpack_from("<10Q", data, addr)
    info = parse_static_class(q[0])
    cname = info["name"] if info else f"Unknown@{hex(q[0])}"
    super_info = parse_static_class(info["super_sc"]) if (info and info["super_sc"]) else None
    super_name = super_info["name"] if super_info else (hex(info["super_sc"]) if info else "?")
    num_deps = q[7] & 0xFFFFFFFF
    num_funcs = (q[7] >> 32) & 0xFFFFFFFF
    num_props = q[8] & 0xFFFFFFFF
    sz = info["size"] if info else 0
    sc_rva = info["sc_rva"] if info else q[0]
    ctor_rva = info["ctor"] if info else 0
    print("=" * 95)
    print(f"CLASS {cname} : {super_name} | Size={hex(sz)} | StaticClass={hex(sc_rva)} | Ctor={hex(ctor_rva)} | FClassParams={hex(addr)}")
    prop_arr = q[5]
    if 0x0E000000 <= prop_arr <= 0x0F700000 and 0 < num_props < 1000:
        ptrs = struct.unpack_from(f"<{num_props}Q", data, prop_arr)
        for p in ptrs:
            if 0x0E000000 <= p <= 0x0F700000:
                pq = struct.unpack_from("<7Q", data, p)
                pd = struct.unpack_from("<14I", data, p)
                pname = read_cstr(pq[0])
                raw_type = pd[6] & 0xFF
                base_type = raw_type & 0x1F
                ptype = PROP_TYPES.get(base_type, f"Type_{hex(raw_type)}")
                if raw_type == 0x2C:
                    ptype = "NativeBool"
                poff = pd[9]
                extra = ""
                if base_type == 0x0C:
                    extra = decode_bool_setbit(pq[6])
                elif base_type in (0x12, 0x11, 0x19, 0x1E) and 0x04000000 <= pq[5] < 0x0E65A6A0:
                    tinfo = parse_static_class(pq[5])
                    extra = f"-> {tinfo['name'] if tinfo else hex(pq[5])}"
                if filter_props:
                    low = pname.lower()
                    if not any(k in low for k in ("outline", "depth", "stencil", "mesh", "color", "colour", "visible", "visibility", "cull", "dist", "state", "custom", "render", "aura", "reveal", "blind", "strategy")):
                        continue
                print(f"  +0x{poff:04x} : {pname:42s} [{ptype}] {extra}")
    func_arr = q[4]
    if 0x0E000000 <= func_arr <= 0x0F700000 and 0 < num_funcs < 1000:
        for fi in range(num_funcs):
            f_zcon, f_name_ptr = struct.unpack_from("<2Q", data, func_arr + fi * 16)
            fname = read_cstr(f_name_ptr)
            if not filter_props or any(k in fname.lower() for k in ("outline", "customdepth", "stencil", "color", "colour", "visible", "visibility", "reveal")):
                print(f"  FUNC {fname:42s} (ZConstruct={hex(f_zcon)})")

# 1. Dump known FClassParams and trace all _outlineComponent / _outlineUpdateStrategy owners!
prop_refs = [
    0xF3B4030, # bRenderCustomDepth
    0xE8AF810, 0xE8E9860, 0xE9AF188, 0xE9B0358, 0xE9B3418,
    0xEC77768, 0xEDD98A8, 0xEDEC598, 0xEDEEC08, 0xEDF2388,
    0xEDF5460, 0xEE136E0, 0xEE85430, 0xEEB80C8, 0xF565A00,
    0xECE28F8, # _totemOutlineUpdateStrategy
]

seen_cp = set()
for p_addr in prop_refs:
    pat = struct.pack("<Q", p_addr)
    idx = data.find(pat, 0x0E000000)
    if idx != -1:
        arr_start = idx
        while arr_start >= 0x0E000000:
            prev_q = struct.unpack_from("<Q", data, arr_start - 8)[0]
            if 0x0E000000 <= prev_q <= 0x0F700000:
                s_ptr = struct.unpack_from("<Q", data, prev_q)[0]
                if 0x02000000 <= s_ptr <= 0x03500000:
                    arr_start -= 8
                    continue
            break
        cp_ref = data.find(struct.pack("<Q", arr_start), 0x0E000000)
        if cp_ref != -1:
            cp_addr = cp_ref - 40
            if cp_addr not in seen_cp:
                seen_cp.add(cp_addr)
                dump_fclassparams(cp_addr, filter_props=(cp_addr == 0xF3B4B48))

# 2. Also dump core Outline and Gameplay classes
core_cps = [
    0xEBC33F8, # DBDOutlineComponent
    0xEC777A0, # OutlineUpdateStrategy
    0xECC1E48, # SourceBasedOutlineUpdateStrategy
    0xEC00000, # DefaultOutlineUpdateStrategy
    0xECE32E0, # TotemOutlineUpdateStrategy
    0xEC2B038, # GeneratorOutlineUpdateStrategy
    0xEC676E8, # MeatHookOutlineUpdateStrategy
    0xECD7C10, # SurvivorOutlineUpdateStrategy
    0xECB8528, # SlasherOutlineUpdateStrategy
    0xEC7D3C0, # PalletOutlineUpdateStrategy
    0xED19EF8, # VaultableOutlineUpdateStrategy
    0xEC0FF28, # EscapeOutlineUpdateStrategy
    0xEC2EDE0, # HatchOutlineUpdateStrategy
    0xECB2AB0, # SearchableOutlineUpdateStrategy
]
for cp_addr in core_cps:
    if cp_addr not in seen_cp:
        seen_cp.add(cp_addr)
        dump_fclassparams(cp_addr, filter_props=False)
