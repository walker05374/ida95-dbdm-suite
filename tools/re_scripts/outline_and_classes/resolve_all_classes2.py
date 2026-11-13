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

def read_utf16le(va, max_chars=128):
    if va <= 0 or va + 2 > len(data):
        return ""
    chars = []
    for i in range(max_chars):
        if va + i * 2 + 2 > len(data):
            break
        ch = struct.unpack_from("<H", data, va + i * 2)[0]
        if ch == 0:
            break
        if 32 <= ch < 127:
            chars.append(chr(ch))
        else:
            return ""
    return "".join(chars)

def decode_static_class(fn_va):
    if not (0x04BBC000 <= fn_va <= 0x0E65A6A0):
        return "", 0, 0
    regs = {}
    adrp = {}
    singleton_bss = 0
    for off in range(fn_va, min(fn_va + 256, len(data)), 4):
        insn = struct.unpack_from("<I", data, off)[0]
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 0x3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= (1 << 21)
            adrp[rd] = (off & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            if rn in adrp:
                val = adrp[rn] + imm12
                regs[rd] = val
                if val >= 0x10B00000 and singleton_bss == 0:
                    singleton_bss = val
        elif (insn & 0xFF800000) == 0x52800000:
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            sh = ((insn >> 21) & 0x3) * 16
            regs[rd] = imm16 << sh
        elif (insn & 0xFC000000) == 0x94000000:
            break
    cls_name = read_utf16le(regs.get(1, 0))
    cls_size = regs.get(4, 0)
    return cls_name, cls_size, singleton_bss

def decode_setbit(setbit_va):
    if not (0x04BBC000 <= setbit_va <= 0x0E65A6A0):
        return ""
    out = []
    for off in range(setbit_va, min(setbit_va + 40, len(data)), 4):
        insn = struct.unpack_from("<I", data, off)[0]
        if (insn & 0xFFC003E0) in (0x39400000, 0x39000000):
            imm12 = (insn >> 10) & 0xFFF
            out.append(f"+0x{imm12:04X}(u8)")
        elif (insn & 0xFFC003E0) in (0x79400000, 0x79000000):
            imm12 = ((insn >> 10) & 0xFFF) * 2
            out.append(f"+0x{imm12:04X}(u16)")
        elif (insn & 0xFFC003E0) in (0xB9400000, 0xB9000000):
            imm12 = ((insn >> 10) & 0xFFF) * 4
            out.append(f"+0x{imm12:04X}(u32)")
        elif (insn & 0xFF800000) == 0x91000000:  # ADD Xd, X0, #imm12
            rn = (insn >> 5) & 0x1F
            if rn == 0:
                imm12 = (insn >> 10) & 0xFFF
                if (insn >> 22) & 1:
                    imm12 <<= 12
                out.append(f"base+0x{imm12:04X}")
    return " ".join(out)

def resolve_func_details(f_params):
    if not (0x0E65A000 <= f_params < len(data) - 64):
        return 0, 0, 0
    # Let's inspect qwords of FFunctionParams
    # +0x00: OuterFunc, +0x08: SuperFunc, +0x10: NameUTF8, +0x18: PropArray, +0x20: NumProps/Size, +0x28 or +0x30: ExecFunc!
    qwords = struct.unpack_from("<8Q", data, f_params)
    exec_fn = 0
    for q in qwords[3:]:
        if 0x04BBC000 <= q <= 0x0E65A6A0:
            exec_fn = q
            break
    native_fn = 0
    vt_off = 0
    if exec_fn:
        for eoff in range(exec_fn, min(exec_fn + 320, len(data)), 4):
            einsn = struct.unpack_from("<I", data, eoff)[0]
            if (einsn & 0xFC000000) in (0x94000000, 0x14000000):
                simm26 = einsn & 0x03FFFFFF
                if simm26 & (1 << 25):
                    simm26 -= (1 << 26)
                callee = eoff + (simm26 << 2)
                if not (0x0A6F0000 <= callee <= 0x0A7A0000):
                    native_fn = callee
            elif (einsn & 0xFFC00000) == 0xF9400000:
                imm12_l = ((einsn >> 10) & 0xFFF) * 8
                if imm12_l >= 0x40:
                    vt_off = imm12_l
            elif einsn == 0xD65F03C0:  # RET
                break
    return exec_fn, native_fn, vt_off

def resolve_z_construct_func(z_va):
    if z_va <= 0 or z_va + 64 > len(data):
        return 0, 0, 0, 0
    adrp_pages = {}
    for off in range(z_va, z_va + 64, 4):
        insn = struct.unpack_from("<I", data, off)[0]
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 0x3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= (1 << 21)
            adrp_pages[rd] = (off & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            if rn in adrp_pages:
                target = adrp_pages[rn] + imm12
                if rd == 1 and 0x0E65A000 <= target < len(data) - 64:
                    exec_fn, native_fn, vt_off = resolve_func_details(target)
                    return target, exec_fn, native_fn, vt_off
    return 0, 0, 0, 0

def dump_prop(p_ptr, idx, prefix="Prop", out_lines=None):
    p_name_ptr, rep_func = struct.unpack_from("<QQ", data, p_ptr)
    p_off_24 = struct.unpack_from("<I", data, p_ptr + 0x24)[0]
    gen_byte = data[p_ptr + 0x18]
    p_name = read_cstr(p_name_ptr)
    rep_name = read_cstr(rep_func) if rep_func else ""
    extra = ""
    if rep_name:
        extra += f" [RepNotify={rep_name}]"
    if (gen_byte & 0x3F) == 0x0C:  # BoolProperty is 0x0C (0x2C & 0x3F == 0x2C) -> wait: 0x2C & 0x1F == 0x0C!
        setbit = struct.unpack_from("<Q", data, p_ptr + 0x28)[0]
        if not (0x04BBC000 <= setbit <= 0x0E65A6A0):
            setbit = struct.unpack_from("<Q", data, p_ptr + 0x30)[0]
        sb_info = decode_setbit(setbit)
        extra += f" [Bool SetBitFunc=0x{setbit:08X} ({sb_info})]"
    line = f"  {prefix}[{idx:02d}] +0x{p_off_24:04X} : {p_name} (PropStruct=0x{p_ptr:08X}, GenFlags=0x{gen_byte:02X}){extra}"
    if out_lines is not None:
        out_lines.append(line)

targets = {
    "PlayerState",
    "DBDPlayerState",
    "CameraComponent",
    "PlayerCameraManager",
    "DBDPlayerCameraManager",
    "DBDOutlineComponent",
    "OutlineUpdateStrategy",
    "SourceBasedOutlineUpdateStrategy",
    "DefaultOutlineUpdateStrategy",
    "GeneratorOutlineUpdateStrategy",
    "SurvivorOutlineUpdateStrategy",
    "SlasherOutlineUpdateStrategy",
    "TotemOutlineUpdateStrategy",
    "PalletOutlineUpdateStrategy",
    "VaultableOutlineUpdateStrategy",
    "Generator",
    "Totem",
    "Pallet",
    "Window",
    "Hatch",
    "Searchable",
    "EscapeDoor",
    "SlasherPlayer",
    "CamperPlayer",
    "DBDPlayer",
    "Character",
    "Pawn",
    "Actor",
    "PlayerController",
    "DBDPlayerController",
    "CharacterMovementComponent",
    "DBDCharacterMovementComponent",
    "MovementComponent",
    "SkillCheck",
    "PlayerInteractionHandler",
    "ChargeableComponent",
    "CamperHealthComponent",
}

start_scan = 0x0E65A000
end_scan = 0x0F660000

out_lines = []
for off in range(start_scan, end_scan, 8):
    fn_va = struct.unpack_from("<Q", data, off)[0]
    if 0x04BBC000 <= fn_va <= 0x0E65A6A0:
        cfg_va, cpp_va, dep_arr, fn_arr, prop_arr, iface_arr, num_dep, num_fn, num_prop, num_iface, cls_flags = struct.unpack_from(
            "<QQQQQQIIIII", data, off + 8
        )
        if 0 < num_dep <= 64 and num_fn <= 600 and num_prop <= 600 and num_iface <= 64 and start_scan <= dep_arr < end_scan:
            cls_name, cls_size, bss_singleton = decode_static_class(fn_va)
            if cls_name in targets:
                out_lines.append(f"\n[FClassParams] {cls_name} @ 0x{off:08X} | StaticClass=0x{fn_va:08X} (Singleton=0x{bss_singleton:08X}) | SizeOf=0x{cls_size:04X} | Props={num_prop} | Funcs={num_fn}")
                if prop_arr and num_prop > 0 and start_scan <= prop_arr < end_scan:
                    for i in range(num_prop):
                        p_ptr = struct.unpack_from("<Q", data, prop_arr + i * 8)[0]
                        if start_scan <= p_ptr < end_scan:
                            dump_prop(p_ptr, i, "Prop", out_lines)
                if fn_arr and num_fn > 0 and start_scan <= fn_arr < end_scan:
                    for i in range(num_fn):
                        f_ctor, f_name_ptr = struct.unpack_from("<QQ", data, fn_arr + i * 16)
                        f_name = read_cstr(f_name_ptr)
                        f_params, exec_fn, native_fn, vt_off = resolve_z_construct_func(f_ctor)
                        vt_str = f" [VTable+0x{vt_off:03X}]" if vt_off else ""
                        out_lines.append(f"  Func[{i:02d}] {f_name} -> Z_Construct=0x{f_ctor:08X} | FFunctionParams=0x{f_params:08X} | exec=0x{exec_fn:08X} | native=0x{native_fn:08X}{vt_str}")

# Also scan all FEnumParams for Immobilize / Guided / Camper / Escape / SkillCheck enums
for off in range(start_scan, end_scan, 8):
    e_outer, e_utf8, e_cpp_type, e_pair_arr, e_num_pairs = struct.unpack_from("<QQQQI", data, off)
    if 0x04BBC000 <= e_outer <= 0x0E65A6A0 and 0 < e_num_pairs <= 64 and start_scan <= e_pair_arr < end_scan:
        e_name = read_cstr(e_utf8)
        if any(k in e_name for k in ("Immobilize", "Guided", "Camper", "GameState", "PlayerRole", "MovementMode", "TotemState", "PalletState", "HatchState", "SkillCheck")):
            out_lines.append(f"\n[FEnumParams] {e_name} @ 0x{off:08X} | Entries={e_num_pairs}")
            for i in range(e_num_pairs):
                en_name_ptr, en_val = struct.unpack_from("<Qq", data, e_pair_arr + i * 16)
                en_name = read_cstr(en_name_ptr)
                out_lines.append(f"  Enum[{i:02d}] {en_name} = {en_val}")

with open(r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch\full_resolved_utf8.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(out_lines))

print(f"Wrote {len(out_lines)} lines.")
for line in out_lines:
    if line.startswith("\n[FClassParams]") or line.startswith("\n[FEnumParams]"):
        print(line.strip())
