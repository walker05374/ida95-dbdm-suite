import struct
import re

path = r'E:\atual\ida95-dbdm-suite\binaries\libUE4_fixed.so'
with open(path, 'rb') as f:
    data = f.read()

def dump_disasm_lite(name, start, end):
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
        elif (w & 0xFFC00000) == 0xBD400000:
            imm12 = ((w >> 10) & 0xFFF) * 4
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'LDR S{rt}, [X{rn}, #{hex(imm12)}]'
        elif (w & 0xFFC00000) == 0x39400000:
            imm12 = (w >> 10) & 0xFFF
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'LDRB W{rt}, [X{rn}, #{hex(imm12)}]'
        elif (w & 0xFFC00000) == 0x39000000:
            imm12 = (w >> 10) & 0xFFF
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'STRB W{rt}, [X{rn}, #{hex(imm12)}]'
        elif (w & 0xFFC00000) == 0xB9000000:
            imm12 = ((w >> 10) & 0xFFF) * 4
            rn = (w >> 5) & 0x1F
            rt = w & 0x1F
            note = f'STR W{rt}, [X{rn}, #{hex(imm12)}]'
        elif w == 0xD65F03C0:
            note = 'RET'
        elif (w & 0xFFFFFC1F) == 0xD63F0000:
            rn = (w >> 5) & 0x1F
            note = f'BLR X{rn}'
        if note or (0x6e19000 <= pc <= 0x6e19280):
            print(f'  {hex(pc)}: {w:08x}  {note}')

dump_disasm_lite('USkillCheck::OnSkillCheckInput_impl', 0x6e18f80, 0x6e19340)

# Also check case-insensitive string search for Empathy, WindowsOfOpportunity, Plunderer, Kindred across entire binary
for kw in [b'empathy', b'windowsofopportunity', b'plunderer', b'kindred', b'obsession']:
    matches = [m.start() for m in re.finditer(re.escape(kw), data, re.IGNORECASE)]
    samples = []
    for off in matches[:8]:
        s_start = data.rfind(b'\x00', max(0, off - 64), off) + 1
        s_end = data.find(b'\x00', off, off + 96)
        samples.append((hex(s_start), data[s_start:s_end].decode('ascii', errors='ignore')))
    print(f'CI {kw.decode()}: total={len(matches)}, samples={samples}')
