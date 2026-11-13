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

# Disassemble 0x6e16e2c (Local_OnSkillCheckFailureTrigger_impl) and 0x6e1700c (Local_OnSkillCheckSuccessTrigger_impl) and 0x6e19300..0x6e19450
for name, start, end in [
    ('Local_OnSkillCheckFailureTrigger_impl', 0x6e16e2c, 0x6e1700c),
    ('Local_OnSkillCheckSuccessTrigger_impl', 0x6e1700c, 0x6e17350),
    ('OnSkillCheckInput_tail', 0x6e192f0, 0x6e19460),
    ('Server_OnSkillCheckSuccess_thunk', 0x7b89bb4, 0x7b89c40),
    ('Multicast_SkillCheckResponse_thunk', 0x7b89a24, 0x7b89aec),
]:
    print(f'=== {name} ({hex(start)}..{hex(end)}) ===')
    words = struct.unpack_from(f'<{(end-start)//4}I', data, start)
    for i, w in enumerate(words):
        pc = start + i*4
        note = ''
        if (w >> 26) in (0b000101, 0b100101):
            imm26 = w & 0x3FFFFFF
            if imm26 & (1 << 25): imm26 -= (1 << 26)
            note = f'{"BL" if (w>>31) else "B"} {hex(pc + imm26*4)}'
        elif (w & 0xFF000010) == 0x54000000:
            imm19 = (w >> 5) & 0x7FFFF
            if imm19 & (1 << 18): imm19 -= (1 << 19)
            cond = ['eq','ne','cs','cc','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','al','nv'][w & 0xF]
            note = f'B.{cond} {hex(pc + imm19*4)}'
        elif (w & 0x7E000000) in (0x34000000, 0x36000000):
            imm19 = (w >> 5) & 0x7FFFF
            if imm19 & (1 << 18): imm19 -= (1 << 19)
            note = f'CBZ/TBZ -> {hex(pc + imm19*4)}'
        elif (w & 0xFFC00000) == 0xF9400000:
            imm12 = ((w >> 10) & 0xFFF) * 8
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'LDR X{rt}, [X{rn}, #{hex(imm12)}]'
        elif (w & 0xFFC00000) == 0xB9400000:
            imm12 = ((w >> 10) & 0xFFF) * 4
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'LDR W{rt}, [X{rn}, #{hex(imm12)}]'
        elif (w & 0xFFC00000) == 0x39400000:
            imm12 = (w >> 10) & 0xFFF
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'LDRB W{rt}, [X{rn}, #{hex(imm12)}]'
        elif (w & 0xFFFFFC1F) == 0xD63F0000:
            rn = (w >> 5) & 0x1F
            note = f'BLR X{rn}'
        elif w == 0xD65F03C0:
            note = 'RET'
        if note:
            print(f'  {hex(pc)}: {w:08x}  {note}')
