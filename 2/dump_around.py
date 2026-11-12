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

def dump_fclass_around(prop_va, label):
    # Find where prop_va appears in the pointer array, then find the start of that pointer array, then find FClassParams (+0x28 = PropertyArray)
    needle = struct.pack("<Q", prop_va)
    ptr_loc = data.find(needle, prop_va, prop_va + 0x4000)
    if ptr_loc == -1:
        print(f"Could not find pointer to 0x{prop_va:08X} for {label}")
        return
    # Scan forward from ptr_loc to find FClassParams (where +0x28 points to [prop_va .. ptr_loc])
    for cand in range(ptr_loc, ptr_loc + 0x1000, 8):
        fn_va, cfg_va, cpp_va, dep_arr, fn_arr, prop_arr, iface_arr, num_dep, num_fn, num_prop, num_iface = struct.unpack_from(
            "<QQQQQQQIIII", data, cand
        )
        if prop_va < prop_arr <= ptr_loc and 0 < num_prop <= 500:
            print(f"\n=== {label} FClassParams @ 0x{cand:08X} | NoRegisterFunc=0x{fn_va:08X} | Props={num_prop} | Funcs={num_fn} ===")
            for i in range(num_prop):
                p_ptr = struct.unpack_from("<Q", data, prop_arr + i * 8)[0]
                p_name_ptr, rep_func = struct.unpack_from("<QQ", data, p_ptr)
                p_off = struct.unpack_from("<I", data, p_ptr + 0x24)[0]
                gen_b = data[p_ptr + 0x18]
                p_name = read_cstr(p_name_ptr)
                extra = ""
                if (gen_b & 0x1F) == 0x0C:
                    setbit = struct.unpack_from("<Q", data, p_ptr + 0x28)[0]
                    extra = f" [Bool SetBitFunc=0x{setbit:08X}]"
                print(f"  Prop[{i:02d}] +0x{p_off:04X} : {p_name} (0x{p_ptr:08X}, Gen=0x{gen_b:02X}){extra}")
            if fn_arr and 0 < num_fn <= 500:
                for i in range(num_fn):
                    f_ctor, f_name_ptr = struct.unpack_from("<QQ", data, fn_arr + i * 16)
                    print(f"  Func[{i:02d}] {read_cstr(f_name_ptr)} -> Z_Construct=0x{f_ctor:08X}")
            return

dump_fclass_around(0x0EBD9188, "ADBDPlayer")
dump_fclass_around(0x0F28ACE8, "ACharacter")
dump_fclass_around(0x0F28E8F8, "UCharacterMovementComponent")
dump_fclass_around(0x0F39B990, "APlayerCameraManager")
