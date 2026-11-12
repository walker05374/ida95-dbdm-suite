import struct
import re

path = r'E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so'
with open(path, 'rb') as f:
    data = f.read()

def cstr(off):
    if off < 0x2000000 or off >= 0x4000000: return None
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

print('=== ACamperPlayer Function Table (0xeb3db00..0xeb3e600) ===')
for off in range(0xeb3db00, 0xeb3e600, 16):
    name_off, fn_rva = struct.unpack_from('<QQ', data, off)
    s = cstr(name_off)
    if s and 0x4000000 <= fn_rva < 0x0DC00000:
        print(f'  {hex(off)}: {s} -> {hex(fn_rva)}')

print('\n=== OnEscaped Class Table (0xe8e7b80..0xe8e7e00) ===')
for off in range(0xe8e7b80, 0xe8e7e00, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s:
        print(f'  {hex(off)}: str="{s}" (next={hex(struct.unpack_from("<Q", data, off+8)[0])})')

print('\n=== USkillCheck Function Table (0xecb3b80..0xecb3ca0) ===')
for off in range(0xecb3b80, 0xecb3ca0, 16):
    name_off, fn_rva = struct.unpack_from('<QQ', data, off)
    s = cstr(name_off)
    if s:
        print(f'  {hex(off)}: {s} -> {hex(fn_rva)}')

print('\n=== Search Perk / Aura / Trap / Cinematic Class Names ===')
for pat in [b'OutlineComponent', b'Aura', b'Obsession', b'Empathy', b'WindowsOfOpportunity', b'Plunderer', b'Kindred', b'BearTrap', b'Disarm', b'Mori', b'KillInteraction', b'Escape', b'Immobilize']:
    for m in re.finditer(b'\x00([A-Za-z0-9_]*' + pat + b'[A-Za-z0-9_]*)\x00', data[0x2c00000:0x3200000]):
        s = m.group(1).decode('ascii', errors='ignore')
        str_off = 0x2c00000 + m.start(1)
        refs = find_ptr_refs(str_off)
        if refs and len(s) < 55:
            # check if any ref looks like a Z_Construct_UClass / FClassParams or FFunctionParams
            for r in refs[:2]:
                q0, q1, q2, q3 = struct.unpack_from('<4Q', data, r - 16)
                if 0x4000000 <= q0 < 0x0DC00000 or 0x4000000 <= q3 < 0x0DC00000:
                    print(f'  {s} @ {hex(str_off)} -> ref {hex(r)} (q[-2]={hex(q0)}, q[-1]={hex(q1)}, q[+1]={hex(q3)})')
