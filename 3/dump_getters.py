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

print('=== ADBDGameState Properties (0xebb0900..0xebb1800) ===')
for off in range(0xebb0900, 0xebb1800, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s:
        q = struct.unpack_from('<7Q', data, off)
        prop_off = (q[4] >> 32) & 0xFFFF
        print(f'  {hex(off)}: "{s}" | off={hex(prop_off)} | q4={hex(q[4])} q5={hex(q[5])}')

print('\n=== UOutlineComponent Properties (0xebc3140..0xebc3600) ===')
for off in range(0xebc3140, 0xebc3600, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s:
        q = struct.unpack_from('<7Q', data, off)
        prop_off = (q[4] >> 32) & 0xFFFF
        print(f'  {hex(off)}: "{s}" | off={hex(prop_off)} | q4={hex(q[4])} q5={hex(q[5])} q6={hex(q[6])}')

def dump_words(name, rva, count=12):
    words = struct.unpack_from(f'<{count}I', data, rva)
    print(f'{name} @ {hex(rva)}: ' + ' '.join(f'{w:08x}' for w in words))

print('\n=== Getter / Setter Machine Code ===')
for name, rva in [
    ('GetInGameBaseTraps', 0x77aa0a0),
    ('GetInGameEscapeDoors', 0x77aa2b0),
    ('GetInGameGenerators', 0x77aa360),
    ('GetInGameHatches', 0x77aa200),
    ('GetInGameMeatHooks', 0x77aa4c0),
    ('GetInGamePallets', 0x77a9ff0),
    ('GetInGameSearchables', 0x77aa410),
    ('GetInGameWindows', 0x77a9f40),
    ('GetTraps', 0x77aae14),
    ('GetKiller', 0x77aa570),
    ('GetObsessionTarget', 0x77ab354),
    ('SetGameLevelEnded', 0x77abee0),
    ('ABearTrap::_isBroken_setter', 0x9d9a170),
    ('ABearTrap::_isBeingCollected_setter', 0x9d9a17c),
    ('ABearTrap::_containsSurvivor_setter', 0x9d9a188),
    ('ABearTrap::_isTrapSet_setter', 0x9d9a194),
    ('ABearTrap::_wasJustDisarmed_setter', 0x9d9a1a0),
    ('ACamperPlayer::EscapeTutorial', 0x761d368),
    ('ACamperPlayer::DBD_CheatEscaped', 0x761cb50),
    ('ACamperPlayer::Server_CheatEscaped', 0x761985c),
    ('ACamperPlayer::SetImmobilized', 0x761cc40),
    ('ACamperPlayer::HasTrapImmunity', 0x7802fc8),
    ('ACamperPlayer::TriggerTrapImmunity', 0x7803034),
]:
    dump_words(name, rva, 10)
