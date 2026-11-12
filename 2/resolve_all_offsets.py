import struct

SO_PATH = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"

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

def resolve_z_construct_func(z_va):
    if z_va <= 0 or z_va + 64 > len(data):
        return 0, 0, 0
    adrp_pages = {}
    for off in range(z_va, z_va + 64, 4):
        insn = struct.unpack_from("<I", data, off)[0]
        if (insn & 0x9F000000) == 0x90000000:  # ADRP
            rd = insn & 0x1F
            immlo = (insn >> 29) & 0x3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= (1 << 21)
            page = (off & ~0xFFF) + (imm << 12)
            adrp_pages[rd] = page
        elif (insn & 0xFF800000) == 0x91000000:  # ADD Xd, Xn, #imm12
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            sh = (insn >> 22) & 0x1
            if sh:
                imm12 <<= 12
            if rn in adrp_pages:
                target = adrp_pages[rn] + imm12
                if rd == 1 and 0x0E65A000 <= target < len(data) - 64:
                    outer_f, super_f, name_u8, exec_fn, prop_arr, num_p, struct_sz = struct.unpack_from("<QQQQQIH", data, target)
                    native_fn = 0
                    if 0x04BBC000 <= exec_fn <= 0x0E65A6A0:
                        for eoff in range(exec_fn, min(exec_fn + 256, len(data)), 4):
                            einsn = struct.unpack_from("<I", data, eoff)[0]
                            if (einsn & 0xFC000000) in (0x94000000, 0x14000000):  # BL or B
                                simm26 = einsn & 0x03FFFFFF
                                if simm26 & (1 << 25):
                                    simm26 -= (1 << 26)
                                callee = eoff + (simm26 << 2)
                                if not (0x0A6F0000 <= callee <= 0x0A7A0000):
                                    native_fn = callee
                    return target, exec_fn, native_fn
    return 0, 0, 0

def decode_setbit(setbit_va):
    if not (0x04BBC000 <= setbit_va <= 0x0E65A6A0):
        return ""
    # Usually 4-8 instructions loading or storing byte/halfword at [X0, #imm]
    out = []
    for off in range(setbit_va, min(setbit_va + 32, len(data)), 4):
        insn = struct.unpack_from("<I", data, off)[0]
        # LDRB / STRB [X0, #imm12] -> (insn & 0xFFC003E0) == 0x39400000 or 0x39000000
        if (insn & 0xFFC003E0) in (0x39400000, 0x39000000):
            imm12 = (insn >> 10) & 0xFFF
            out.append(f"byte+0x{imm12:04X}")
            break
        elif (insn & 0xFFC003E0) in (0x79400000, 0x79000000):
            imm12 = ((insn >> 10) & 0xFFF) * 2
            out.append(f"half+0x{imm12:04X}")
            break
        elif (insn & 0xFFC003E0) in (0xB9400000, 0xB9000000):
            imm12 = ((insn >> 10) & 0xFFF) * 4
            out.append(f"word+0x{imm12:04X}")
            break
    return " ".join(out)

def dump_prop(p_ptr, idx, prefix="Prop"):
    p_name_ptr, rep_func, p_flags, gen_flags, obj_flags, arr_dim, p_off = struct.unpack_from(
        "<QQQIIHI", data, p_ptr
    )
    # Wait: let's read p_off explicitly at p_ptr + 0x24!
    p_off_24 = struct.unpack_from("<I", data, p_ptr + 0x24)[0]
    gen_byte = data[p_ptr + 0x18]
    p_name = read_cstr(p_name_ptr)
    rep_name = read_cstr(rep_func) if rep_func else ""
    extra = ""
    if rep_name:
        extra += f" [RepNotify={rep_name}]"
    if (gen_byte & 0x3F) == 0x0A:  # Bool
        setbit = struct.unpack_from("<Q", data, p_ptr + 0x28)[0]
        if not (0x04BBC000 <= setbit <= 0x0E65A6A0):
            setbit = struct.unpack_from("<Q", data, p_ptr + 0x30)[0]
        sb_info = decode_setbit(setbit)
        extra += f" [Bool SetBitFunc=0x{setbit:08X} ({sb_info})]"
    print(f"  {prefix}[{idx:02d}] +0x{p_off_24:04X} : {p_name} (PropStruct=0x{p_ptr:08X}, GenFlags=0x{gen_byte:02X}){extra}")

def dump_class_by_name(target_names):
    str_vas = {}
    for name in target_names:
        needle = b"\x00" + name.encode("ascii") + b"\x00"
        pos = 0
        vas = []
        while True:
            idx = data.find(needle, pos)
            if idx == -1:
                break
            vas.append(idx + 1)
            pos = idx + 1
        str_vas[name] = vas

    start_scan = 0x0E65A000
    end_scan = min(len(data) - 64, 0x10200000)

    va_to_name = {}
    for name, vas in str_vas.items():
        for v in vas:
            va_to_name[v] = name

    print("=== CLASS / STRUCT / ENUM REFLECTION DUMP ===")
    for off in range(start_scan, end_scan, 8):
        val = struct.unpack_from("<Q", data, off)[0]
        if val in va_to_name:
            name = va_to_name[val]
            cand_class = off - 0x10
            func_no_reg, dep_list, class_utf8, cpp_info, func_arr, prop_arr, num_funcs, num_props, size_of, flags = struct.unpack_from(
                "<QQQQQQIIQI", data, cand_class
            )
            if 0x04BBC000 <= func_no_reg <= 0x0E65A6A0 and num_funcs < 500 and num_props < 500 and 0 < size_of <= 0x10000:
                print(f"\n[FClassParams] {name} @ 0x{cand_class:08X} | NoRegisterFunc=0x{func_no_reg:08X} | SizeOf=0x{size_of:04X} | Props={num_props} | Funcs={num_funcs}")
                if prop_arr and num_props > 0 and start_scan <= prop_arr < end_scan:
                    for i in range(num_props):
                        p_ptr = struct.unpack_from("<Q", data, prop_arr + i * 8)[0]
                        if start_scan <= p_ptr < end_scan:
                            dump_prop(p_ptr, i, "Prop")
                if func_arr and num_funcs > 0 and start_scan <= func_arr < end_scan:
                    for i in range(num_funcs):
                        f_entry = func_arr + i * 16
                        f_ctor, f_name_ptr = struct.unpack_from("<QQ", data, f_entry)
                        f_name = read_cstr(f_name_ptr)
                        f_params, exec_fn, native_fn = resolve_z_construct_func(f_ctor)
                        print(f"  Func[{i:02d}] {f_name} -> Z_Construct=0x{f_ctor:08X} | FFunctionParams=0x{f_params:08X} | exec=0x{exec_fn:08X} | native=0x{native_fn:08X}")

            cand_struct = off - 0x10
            outer_fn, super_fn, struct_utf8, s_size, s_align, s_prop_arr, s_num_props = struct.unpack_from(
                "<QQQQQQI", data, cand_struct
            )
            if struct_utf8 == val and 0 < s_size <= 0x4000 and s_num_props < 200 and (s_align in (1, 2, 4, 8, 16)):
                print(f"\n[FStructParams] {name} @ 0x{cand_struct:08X} | SizeOf=0x{s_size:04X} | Align={s_align} | Props={s_num_props}")
                if s_prop_arr and s_num_props > 0 and start_scan <= s_prop_arr < end_scan:
                    for i in range(s_num_props):
                        p_ptr = struct.unpack_from("<Q", data, s_prop_arr + i * 8)[0]
                        if start_scan <= p_ptr < end_scan:
                            dump_prop(p_ptr, i, "StructProp")

            # Also check FEnumParams (+0x08 = EnumUTF8)
            cand_enum = off - 0x08
            e_outer, e_utf8, e_cpp_type, e_pair_arr, e_num_pairs = struct.unpack_from("<QQQQI", data, cand_enum)
            if e_utf8 == val and 0 < e_num_pairs <= 128 and start_scan <= e_pair_arr < end_scan:
                print(f"\n[FEnumParams] {name} @ 0x{cand_enum:08X} | Entries={e_num_pairs}")
                for i in range(e_num_pairs):
                    en_name_ptr, en_val = struct.unpack_from("<Qq", data, e_pair_arr + i * 16)
                    en_name = read_cstr(en_name_ptr)
                    print(f"  Enum[{i:02d}] {en_name} = {en_val}")

if __name__ == "__main__":
    targets = [
        "PlayerState",
        "DBDPlayerState",
        "PlayerStateData",
        "CameraComponent",
        "PlayerCameraManager",
        "DBDPlayerCameraManager",
        "DBDOutlineComponent",
        "OutlineUpdateStrategy",
        "GeneratorOutlineUpdateStrategy",
        "SurvivorOutlineUpdateStrategy",
        "SlasherOutlineUpdateStrategy",
        "TotemOutlineUpdateStrategy",
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
        "CharacterMovementComponent",
        "DBDCharacterMovementComponent",
        "MovementComponent",
        "SkillCheck",
        "PlayerInteractionHandler",
        "ChargeableComponent",
        "EImmobilizeState",
        "ECamperDamageState",
        "EGameState",
        "EPlayerRole",
        "EMovementMode",
        "ETotemState",
        "EPalletState",
        "EHatchState",
    ]
    dump_class_by_name(targets)
