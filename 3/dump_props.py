import struct

path = r'E:\ida95-dbdm-suite\DBD_Binarios_Analise-20261110T010227Z-1-001\DBD_Binarios_Analise\libUE4_fixed.so'
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

def dump_props(title, start, end):
    print(f'=== {title} ({hex(start)}..{hex(end)}) ===')
    for off in range(start, end, 8):
        val = struct.unpack_from('<Q', data, off)[0]
        s = cstr(val)
        if s:
            q = struct.unpack_from('<7Q', data, off)
            # In UE4.27 FGenericPropertyParams: [0]=Name, [1]=RepNotifyFunc, [2]=PropertyFlags, [3]=Flags, [4]=(Offset<<32)|ArrayDim, ...
            prop_off = (q[4] >> 32) & 0xFFFF
            print(f'  {hex(off)}: "{s}" | +8={hex(q[1])} | +16={hex(q[2])} | +24={hex(q[3])} | +32={hex(q[4])} (off={hex(prop_off)}) | +40={hex(q[5])}')

dump_props('ABearTrap Props', 0xef02170, 0xef02500)
dump_props('TrapImmunity (ACamperPlayer / UCamperInteractable)', 0xebc9700, 0xebca400)
dump_props('UOutlineComponent (0xebc2980..0xebc2d00)', 0xebc2980, 0xebc2d00)
dump_props('SurvivorAuraRevealEffect (0xee9f980..0xee9fc00)', 0xee9f980, 0xee9fc00)
dump_props('ObjectOfObsession (0xeef5680..0xeef5900)', 0xeef5680, 0xeef5900)
dump_props('TimedRevealKillerAura (0xeebed00..0xeebf080)', 0xeebed00, 0xeebf080)
dump_props('ADBDGameState (0xebab500..0xebac000)', 0xebab500, 0xebac000)
