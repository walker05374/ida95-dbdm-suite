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
        elif (insn & 0xFF800000) == 0x91000000:
            rn = (insn >> 5) & 0x1F
            if rn == 0:
                imm12 = (insn >> 10) & 0xFFF
                if (insn >> 22) & 1:
                    imm12 <<= 12
                out.append(f"base+0x{imm12:04X}")
    return " ".join(out)

def dump_fclass_by_prop_name(target_prop_name, label):
    needle = b"\x00" + target_prop_name.encode("ascii") + b"\x00"
    pos = 0
    str_vas = []
    while True:
        idx = data.find(needle, pos)
        if idx == -1:
            break
        str_vas.append(idx + 1)
        pos = idx + 1

    start_scan = 0x0E65A000
    end_scan = 0x0F660000
    for sv in str_vas:
        sv_bytes = struct.pack("<Q", sv)
        p_pos = start_scan
        while True:
            prop_va = data.find(sv_bytes, p_pos, end_scan)
            if prop_va == -1:
                break
            p_pos = prop_va + 8
            # Now find pointer to prop_va in PropertyArray
            ptr_loc = data.find(struct.pack("<Q", prop_va), prop_va, min(prop_va + 0x8000, end_scan))
            if ptr_loc == -1:
                continue
            for cand in range(ptr_loc, min(ptr_loc + 0x2000, end_scan), 8):
                fn_va, cfg_va, cpp_va, dep_arr, fn_arr, prop_arr, iface_arr, num_dep, num_fn, num_prop, num_iface = struct.unpack_from(
                    "<QQQQQQQIIII", data, cand
                )
                if 0x04BBC000 <= fn_va <= 0x0E65A6A0 and prop_va < prop_arr <= ptr_loc and 0 < num_prop <= 500:
                    print(f"\n=== {label} (via {target_prop_name}) FClassParams @ 0x{cand:08X} | NoRegisterFunc=0x{fn_va:08X} | Props={num_prop} | Funcs={num_fn} ===")
                    for i in range(num_prop):
                        p_ptr = struct.unpack_from("<Q", data, prop_arr + i * 8)[0]
                        if not (start_scan <= p_ptr < end_scan):
                            continue
                        p_name_ptr, rep_func = struct.unpack_from("<QQ", data, p_ptr)
                        p_off = struct.unpack_from("<I", data, p_ptr + 0x24)[0]
                        gen_b = data[p_ptr + 0x18]
                        p_name = read_cstr(p_name_ptr)
                        extra = ""
                        if (gen_b & 0x1F) == 0x0C:
                            # Notice in BoolProperty: +0x20 is ArrayDim/ElementSize, +0x28 is SetBitFunc! Wait: earlier SetBitFunc=0x000014D0 meant +0x30 is SetBitFunc!
                            sb28 = struct.unpack_from("<Q", data, p_ptr + 0x28)[0]
                            sb30 = struct.unpack_from("<Q", data, p_ptr + 0x30)[0]
                            sb = sb30 if (0x04BBC000 <= sb30 <= 0x0E65A6A0) else sb28
                            extra = f" [Bool SetBitFunc=0x{sb:08X} ({decode_setbit(sb)})]"
                        print(f"  Prop[{i:02d}] +0x{p_off:04X} : {p_name} (0x{p_ptr:08X}, Gen=0x{gen_b:02X}){extra}")
                    if fn_arr and 0 < num_fn <= 500 and start_scan <= fn_arr < end_scan:
                        for i in range(num_fn):
                            f_ctor, f_name_ptr = struct.unpack_from("<QQ", data, fn_arr + i * 16)
                            print(f"  Func[{i:02d}] {read_cstr(f_name_ptr)} -> Z_Construct=0x{f_ctor:08X}")
                    return

dump_fclass_by_prop_name("PlayerNamePrivate", "APlayerState")
dump_fclass_by_prop_name("DefaultFOV", "APlayerCameraManager")
dump_fclass_by_prop_name("FieldOfView", "UCameraComponent")
dump_fclass_by_prop_name("MaxWalkSpeedCrouched", "UCharacterMovementComponent")
dump_fclass_by_prop_name("UpdatedComponent", "UMovementComponent")
dump_fclass_by_prop_name("_outlineComponent", "UOutlineUpdateStrategy")
dump_fclass_by_prop_name("_revealedByPowerColor", "UPalletOutlineUpdateStrategy")
dump_fclass_by_prop_name("_canBeRevealedToLocalPlayer", "UVaultableOutlineUpdateStrategy")
