import struct
import re

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

def find_ptr_refs(target_off, start=0x0DC00000, end=0x0F700000):
    pat = struct.pack('<Q', target_off)
    res = []
    pos = start
    while True:
        idx = data.find(pat, pos, end)
        if idx == -1: break
        res.append(idx)
        pos = idx + 8
    return res

def dump_table(title, start, end):
    print(f'\n=== {title} ({hex(start)}..{hex(end)}) ===')
    for off in range(start, end, 8):
        val = struct.unpack_from('<Q', data, off)[0]
        s = cstr(val)
        if s:
            nxt = struct.unpack_from('<Q', data, off + 8)[0]
            nxt2 = struct.unpack_from('<Q', data, off + 16)[0]
            nxt3 = struct.unpack_from('<Q', data, off + 24)[0]
            print(f'  {hex(off)}: "{s}" -> +8={hex(nxt)}, +16={hex(nxt2)}, +24={hex(nxt3)}')

# 1. ABearTrap around 0xef01d00..0xef02900
dump_table('ABearTrap', 0xef01d00, 0xef02900)

# 2. Search for Empathy, Windows, Plunderer, Kindred, Obsession, Outline, Observer across entire rodata (0x1800000..0x4200000)
for kw in [b'Empathy', b'WindowsOfOpportunity', b'Plunderer', b'Kindred', b'ObjectOfObsession', b'UOutlineComponent', b'DBDOutlineComponent', b'GeneratorOutline', b'RevealGenerator', b'TrapImmunity', b'BearTrapOutline']:
    pos = 0x1800000
    hits = []
    while len(hits) < 10:
        idx = data.find(kw, pos, 0x4200000)
        if idx == -1: break
        # find start of string
        s_start = data.rfind(b'\x00', idx - 64, idx) + 1
        s_end = data.find(b'\x00', idx, idx + 128)
        s = data[s_start:s_end].decode('ascii', errors='ignore')
        refs = find_ptr_refs(s_start)
        hits.append((hex(s_start), s, [hex(r) for r in refs[:3]]))
        pos = s_end + 1
    print(f'KW {kw.decode()}: {hits}')

# 3. Inspect Perk tables around 0xee40800..0xee40a00 (_revealGeneratorAurasInRangeEffectClass), 0xee9fa00..0xee9fc00 (_survivorAuraRevealEffect), 0xeef5700..0xeef5900 (_objectOfObsessionActionSpeedEffect)
dump_table('RevealGenEffect (0xee40800)', 0xee40780, 0xee40a80)
dump_table('SurvivorAuraRevealEffect (0xee9fa00)', 0xee9f980, 0xee9fc00)
dump_table('ObjectOfObsession (0xeef5700)', 0xeef5680, 0xeef5900)
dump_table('TimedRevealKillerAura (0xeebed00)', 0xeebed00, 0xeebf080)
dump_table('ADBDPlayerState Escape (0xebe4480)', 0xebe4480, 0xebe4800)
