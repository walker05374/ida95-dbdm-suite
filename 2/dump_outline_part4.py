import struct

path = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
with open(path, "rb") as f:
    data = f.read()

def read_cstr(addr):
    if addr <= 0 or addr >= len(data):
        return ""
    end = data.find(b"\x00", addr, addr + 256)
    if end == -1:
        return ""
    return data[addr:end].decode("latin1", "ignore")

def disasm_a64(rva, count=45):
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
        elif (insn & 0xFFC00000) in (0xA9000000, 0xA9400000, 0x29000000, 0x29400000, 0x2D000000, 0x2D400000):
            is_load = (insn >> 22) & 1
            imm7 = (insn >> 15) & 0x7F
            if imm7 & 0x40:
                imm7 -= 0x80
            rn = (insn >> 5) & 0x1F
            rt = insn & 0x1F
            rt2 = (insn >> 10) & 0x1F
            scale = 2 + ((insn >> 31) & 1)
            off = imm7 << scale
            desc += f"  {'LDP' if is_load else 'STP'} r{rt}, r{rt2}, [X{rn}, #{hex(off)}]"
        elif (insn & 0xFFE003E0) == 0xAA0003E0:
            rd = insn & 0x1F
            rm = (insn >> 16) & 0x1F
            desc += f"  MOV X{rd}, X{rm}"
        elif insn == 0xD65F03C0:
            desc += "  RET"
            lines.append(desc)
            break
        lines.append(desc)
    return lines

print("=== Disasm around UDBDOutlineComponent::StaticRegisterNatives (0x077F1BC0..0x077F1D10) ===")
for l in disasm_a64(0x077F1BC0, 60):
    print("  " + l)

# Also search FName / C-string table in .data.rel.ro for FNameNativePtrPair ("SetTargetColor", exec_ptr)!
for fn_str in [b"SetTargetColor\x00", b"SetMinimumOutlineDistance\x00", b"RefreshOutlineComponent\x00", b"ResetToTransparent\x00", b"SetOutlineIntensity\x00", b"SetFadeIntensity\x00", b"SetRenderInCustomDepth\x00", b"SetCustomDepthStencilValue\x00", b"AddReveal\x00", b"AddRangedReveal\x00"]:
    s_off = data.find(fn_str, 0x02000000, 0x03600000)
    while s_off != -1:
        # Find pointers to s_off in 0x0E000000..0x0F700000
        pat = struct.pack("<Q", s_off)
        ref = data.find(pat, 0x0E000000)
        while ref != -1:
            q0, q1 = struct.unpack_from("<2Q", data, ref)
            # In FNameNativePtrPair, q0 is NameUTF8 and q1 is Pointer (execThunk RVA in 0x05000000..0x0D600000)!
            if 0x05000000 <= q1 < 0x0D600000:
                print(f"\nFOUND NativeExec for {read_cstr(s_off)} @ table 0x{ref:08X} -> exec=0x{q1:08X}")
                exec_lines = disasm_a64(q1, 35)
                for el in exec_lines:
                    print("  " + el)
                # Find last BL before RET
                bls = [int(el.split("  BL 0x")[1], 16) for el in exec_lines if "  BL 0x" in el]
                if bls:
                    impl = bls[-1]
                    print(f"  >>> Native C++ Implementation @ 0x{impl:08X}:")
                    for il in disasm_a64(impl, 45):
                        print("    " + il)
            ref = data.find(pat, ref + 8)
        s_off = data.find(fn_str, s_off + 1, 0x03600000)
