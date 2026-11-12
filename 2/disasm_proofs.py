import struct

ELF_PATH = r"E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so"
OUT_PATH = r"C:\Users\walker\.gemini\antigravity\brain\85ef3522-f44a-498e-9793-6dea70f49d57\scratch\disasm_proofs.txt"

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
    # B / BL
    if (insn & 0x7C000000) == 0x14000000:
        is_bl = (insn >> 31) & 1
        imm26 = insn & 0x3FFFFFF
        if imm26 & (1 << 25):
            imm26 -= (1 << 26)
        target = pc + (imm26 << 2)
        return f"{'BL' if is_bl else 'B'} 0x{target:08X}"
    # CBZ / CBNZ
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
    # TBZ / TBNZ
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
    # B.cond
    if (insn & 0xFF000010) == 0x54000000:
        cond = insn & 0xF
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & (1 << 18):
            imm19 -= (1 << 19)
        target = pc + (imm19 << 2)
        conds = ["EQ","NE","CS","CC","MI","PL","VS","VC","HI","LS","GE","LT","GT","LE","AL","NV"]
        return f"B.{conds[cond]} 0x{target:08X}"
    # ADRP
    if (insn & 0x9F000000) == 0x90000000:
        rd = insn & 0x1F
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= (1 << 21)
        page = (pc & ~0xFFF) + (imm << 12)
        return f"ADRP X{rd}, 0x{page:08X}"
    # ADD immediate (64-bit)
    if (insn & 0xFF800000) == 0x91000000:
        rd = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        sh = (insn >> 22) & 1
        if sh:
            imm12 <<= 12
        return f"ADD X{rd}, X{rn}, #0x{imm12:X}"
    # ADD immediate (32-bit)
    if (insn & 0xFF800000) == 0x11000000:
        rd = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        sh = (insn >> 22) & 1
        if sh:
            imm12 <<= 12
        return f"ADD W{rd}, W{rn}, #0x{imm12:X}"
    # LDR/STR unsigned offset
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
    # MOVZ / MOVK / MOVN
    if (insn & 0x1F800000) == 0x12800000:
        sf = (insn >> 31) & 1
        opc = (insn >> 29) & 3
        hw = (insn >> 21) & 3
        imm16 = (insn >> 5) & 0xFFFF
        rd = insn & 0x1F
        mnem = {0: "MOVN", 2: "MOVZ", 3: "MOVK"}.get(opc, "MOV")
        r = "X" if sf else "W"
        return f"{mnem} {r}{rd}, #0x{imm16:X}, LSL #{hw*16}"
    # ORR register (MOV reg)
    if (insn & 0x7F200000) == 0x2A000000:
        sf = (insn >> 31) & 1
        rm = (insn >> 16) & 0x1F
        rn = (insn >> 5) & 0x1F
        rd = insn & 0x1F
        r = "X" if sf else "W"
        if rn == 31:
            return f"MOV {r}{rd}, {r}{rm}"
        return f"ORR {r}{rd}, {r}{rn}, {r}{rm}"
    # BR / BLR
    if (insn & 0xFFFFFC1F) == 0xD63F0000:
        rn = (insn >> 5) & 0x1F
        return f"BLR X{rn}"
    if (insn & 0xFFFFFC1F) == 0xD61F0000:
        rn = (insn >> 5) & 0x1F
        return f"BR X{rn}"
    return f"RAW 0x{insn:08X}"

def get_class_sizeof(cva):
    noreg = u64(cva)
    for i in range(30):
        pc = noreg + i*4
        insn = u32(pc)
        dec = decode_arm64(pc, insn)
        if dec.startswith("MOVZ W4, #0x"):
            return int(dec.split("#0x")[1].split(",")[0], 16), pc
    return 0, noreg

def find_exec_funcs_by_name_va(name_va, func_Info_va=0):
    if name_va < 0x01000000 or name_va >= 0x04000000:
        return []
    packed = struct.pack("<I", name_va & 0xFFFFFFFF)
    pos = 0x0E000000
    res = []
    while True:
        idx = data.find(packed, pos, 0x10500000)
        if idx == -1:
            break
        if (idx & 7) == 0:
            fn = u64(idx + 8)
            prev = u64(idx - 8)
            # In FNameNativePtrPair: [NameUTF8, FuncPtr] where FuncPtr is in .text (0x04000000..0x0E000000)
            if 0x04000000 <= fn < 0x0E000000 and len(res) < 6:
                res.append((idx, fn))
        pos = idx + 4
    return res

def disasm_block(start_va, max_insns=65):
    lines = []
    callees = []
    for i in range(max_insns):
        pc = start_va + i*4
        insn = u32(pc)
        dec = decode_arm64(pc, insn)
        if not dec.startswith("RAW"):
            lines.append(f"    0x{pc:08X}: {dec}")
        if dec.startswith("BL 0x") or dec.startswith("B 0x"):
            t = int(dec.split("0x")[1], 16)
            if 0x04000000 <= t < 0x0E000000 and t not in callees:
                callees.append(t)
        if insn == 0xD65F03C0 and i > 4:
            break
    return lines, callees

def dump_ufunc(label, fparams_va):
    name_va = u64(fparams_va + 0x10)
    name = cstr(name_va)
    flags = u32(fparams_va + 0x40)
    lines = [f"\n=== {label} | Name='{name}' (0x{name_va:08X}) | FFunctionParams @ 0x{fparams_va:08X} | Flags=0x{flags:08X} ==="]
    pairs = find_exec_funcs_by_name_va(name_va)
    for pair_va, exec_va in pairs:
        lines.append(f"  [FNameNativePtrPair @ 0x{pair_va:08X}] -> ExecFunc = 0x{exec_va:08X}")
        blines, callees = disasm_block(exec_va, 65)
        lines.extend(blines)
        for c in callees:
            # Skip common engine parameter helpers like FFrame::StepCompiledIn
            lines.append(f"  --- Callee 0x{c:08X} ---")
            clines, ccallees = disasm_block(c, 55)
            lines.extend(clines)
            for cc in ccallees[:2]:
                lines.append(f"    --- SubCallee 0x{cc:08X} ---")
                cclines, _ = disasm_block(cc, 40)
                lines.extend(cclines)
    return "\n".join(lines)

def find_fparams_from_zconstruct(z_va):
    regs = {}
    for i in range(20):
        pc = z_va + i*4
        insn = u32(pc)
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= (1 << 21)
            regs[rd] = (pc & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            if rn in regs:
                regs[rd] = regs[rn] + imm12
                if rd == 1:
                    return regs[1]
    return 0

out_lines = []
for cname, cva in [
    ("APlayerState", 0x0F3A5B88),
    ("ADBDPlayerState", 0x0EBE7BC8),
    ("APlayerCameraManager", 0x0F39C238),
    ("ADBDPlayerCameraManager", 0x0EBCFD58),
    ("UCameraComponent", 0x0E7D8470),
    ("UCharacterMovementComponent", 0x0F290888),
    ("UMovementComponent", 0x0F36F6D0),
    ("ADBDPlayer", 0x0EBDA6C0),
    ("ACamperPlayer", 0x0EB46148),
    ("ASlasherPlayer", 0x0ECC0160),
    ("AGenerator", 0x0EC28808),
    ("AHatch", 0x0EC2EA68),
    ("USkillCheck", 0x0ECB5B38),
    ("UDBDOutlineComponent", 0x0EBA97B8),
]:
    sz, pc = get_class_sizeof(cva)
    out_lines.append(f"Class {cname:28s} (FClassParams @ 0x{cva:08X}) -> SizeOf=0x{sz:04X} (insn @ 0x{pc:08X})")

targets = [
    ("APlayerState::GetPlayerName", find_fparams_from_zconstruct(0x0C9B2F60)),
    ("APlayerState::OnRep_PlayerName", find_fparams_from_zconstruct(0x0C9B309C)),
    ("APlayerCameraManager::GetFOVAngle", find_fparams_from_zconstruct(0x0C9A2398)),
    ("APlayerCameraManager::GetCameraLocation", find_fparams_from_zconstruct(0x0C9A2300)),
    ("APlayerCameraManager::GetCameraRotation", find_fparams_from_zconstruct(0x0C9A234C)),
    ("UCharacterMovementComponent::SetMovementMode", find_fparams_from_zconstruct(0x0C8226EC)),
    ("UMovementComponent::StopMovementImmediately", find_fparams_from_zconstruct(0x0C9622F0)),
    ("ACamperPlayer::GetImmobilizedState", 0x0EB3FE98),
    ("ACamperPlayer::SetImmobilized", 0x0EB43B08),
    ("ACamperPlayer::Local_SetImmobilized", 0x0EB41EA8),
    ("ACamperPlayer::Server_SetImmobilized", 0x0EB43500),
    ("ACamperPlayer::Authority_SetImmobilized", 0x0EB3EA38),
    ("ACamperPlayer::Escape", 0x0EB3F540),
    ("ACamperPlayer::DBD_CheatEscaped", 0x0EB3F220),
    ("ACamperPlayer::HatchEscapeZoneEntered", 0x0EB40C00),
    ("AHatch::GetHatchState", 0x0EC2DB58),
    ("AHatch::Local_SetHatchState", 0x0EC2DD38),
    ("AHatch::Authority_SetHatchState", 0x0EC2D808),
    ("AHatch::OnSurvivorEscaped", 0x0EC2E1A0),
    ("ADBDPlayerState::GetGameRole", 0x0EBE5850),
    ("ADBDPlayerState::Client_SetGameRole", 0x0EBE4EF8),
    ("ADBDPlayerState::Server_SetGameRole", 0x0EBE6740),
    ("ADBDPlayerState::SetGameRolePy", 0x0EBE6AD0),
    ("ADBDPlayerState::GetPlayerGameState", 0x0EBE5988),
    ("ADBDPlayerState::Server_SetPlayerGameState", 0x0EBE6838),
    ("ADBDPlayerState::Multicast_SetPlayerGameState", 0x0EBE6170),
    ("USkillCheck::IsDisplayed", 0x0ECB40B8),
    ("USkillCheck::CanShowSkillCheck", 0x0ECB3DA0),
    ("USkillCheck::Local_ActivateSkillCheck", 0x0ECB41E0),
    ("USkillCheck::Local_ActivateCustomSkillCheck", 0x0ECB4130),
    ("USkillCheck::Local_OnSkillCheckSuccessTrigger", 0x0ECB4490),
    ("USkillCheck::Local_OnSkillCheckFailureTrigger", 0x0ECB43B8),
    ("USkillCheck::OnSkillCheckInput", 0x0ECB4960),
    ("UPlayerInteractionHandler::StartCustomSkillCheck", 0x0EC8E878),
    ("AGenerator::TriggerSkillCheck", 0x0EC27580),
]

for label, fva in targets:
    if fva:
        out_lines.append(dump_ufunc(label, fva))

with open(OUT_PATH, "w", encoding="utf-8") as f:
    f.write("\n".join(out_lines))
print("Wrote", len(out_lines), "blocks to", OUT_PATH)
