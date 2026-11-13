import struct

path = r'E:\atual\ida95-dbdm-suite\binaries\libUE4_fixed.so'
with open(path, 'rb') as f:
    data = f.read()

def cstr(off):
    if off < 0x1000000 or off >= 0x4500000: return None
    end = data.find(b'\x00', off, off + 256)
    if end == -1 or end == off: return None
    try:
        s = data[off:end].decode('ascii')
        return s if s.isprintable() else None
    except:
        return None

print('=== ADBDGameState Properties (0xebb1000..0xebb1780) ===')
for off in range(0xebb1000, 0xebb1780, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s:
        q = struct.unpack_from('<7Q', data, off)
        prop_off = (q[4] >> 32) & 0xFFFF
        print(f'  {hex(off)}: "{s}" | off={hex(prop_off)} | q4={hex(q[4])} q5={hex(q[5])} q6={hex(q[6])}')

def dump_words(name, rva, count=24):
    words = struct.unpack_from(f'<{count}I', data, rva)
    targets = []
    for i, w in enumerate(words):
        pc = rva + i*4
        if (w >> 26) in (0b000101, 0b100101): # B or BL
            imm26 = w & 0x3FFFFFF
            if imm26 & (1 << 25): imm26 -= (1 << 26)
            targets.append(f'{"BL" if (w>>31) else "B"}@{hex(pc)}->{hex(pc + imm26*4)}')
        elif (w & 0xFFC003E0) == 0xF9400000: # LDR Xd, [X0, #imm]
            imm12 = ((w >> 10) & 0xFFF) * 8
            rd = w & 0x1F
            targets.append(f'LDR_X{rd}[X0+{hex(imm12)}]@{hex(pc)}')
        elif (w & 0xFFC003E0) == 0x39400000: # LDRB Wd, [X0, #imm]
            imm12 = (w >> 10) & 0xFFF
            rd = w & 0x1F
            targets.append(f'LDRB_W{rd}[X0+{hex(imm12)}]@{hex(pc)}')
        elif (w & 0xFFC003E0) == 0x39000000: # STRB Wd, [X0, #imm]
            imm12 = (w >> 10) & 0xFFF
            rd = w & 0x1F
            targets.append(f'STRB_W{rd}[X0+{hex(imm12)}]@{hex(pc)}')
    print(f'{name} @ {hex(rva)}: {" ".join(f"{w:08x}" for w in words[:12])}\n   decoded: {targets}')

print('\n=== Thunk & Implementation Analysis ===')
for name, rva in [
    ('GetInGameGenerators', 0x77aa360),
    ('GetInGameBaseTraps', 0x77aa0a0),
    ('GetInGameSearchables', 0x77aa410),
    ('GetInGamePallets', 0x77a9ff0),
    ('GetInGameWindows', 0x77a9f40),
    ('GetInGameHatches', 0x77aa200),
    ('GetInGameEscapeDoors', 0x77aa2b0),
    ('SetGameLevelEnded', 0x77abee0),
    ('Multicast_SetGameLevelEnded', 0x77a9000),
    ('Authority_EscapeThroughHatch', 0x77ab390),
    ('ACamperPlayer::EscapeTutorial_impl', 0x6f40300),
    ('ACamperPlayer::DBD_CheatEscaped_impl', 0x6f3fa94),
    ('ACamperPlayer::SetImmobilized', 0x761cc40),
    ('ACamperPlayer::Local_SetImmobilized', 0x761cb98),
    ('ADBDPlayer::HasTrapImmunity_impl', 0x702db34),
    ('ADBDPlayer::TriggerTrapImmunity', 0x7803034),
    ('UOutlineComponent::IsAlwaysVisible_setter', 0x77f21e8),
    ('UOutlineComponent::MinimumOutlineDistance_setter', 0x77f17d8),
    ('USkillCheck::ActivateSkillCheck_impl', 0x6e166d4),
    ('USkillCheck::PatchSite_0x06E1903C', 0x6e19020),
]:
    dump_words(name, rva, 22)
