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

print('=== ADBDGameState Properties (0xebaf800..0xebb0900) ===')
for off in range(0xebaf800, 0xebb0900, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s and (s.startswith('_') or s[0].isupper()):
        q = struct.unpack_from('<7Q', data, off)
        # Check where the offset is stored in FPropertyParams (either q[4]>>32 or q[5])
        print(f'  {hex(off)}: "{s}" | q1={hex(q[1])} q2={hex(q[2])} q3={hex(q[3])} q4={hex(q[4])} q5={hex(q[5])}')

print('\n=== ABearTrap Properties (0xef02170..0xef02680) ===')
for off in range(0xef02170, 0xef02680, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s:
        q = struct.unpack_from('<8Q', data, off)
        print(f'  {hex(off)}: "{s}" | q1={hex(q[1])} q2={hex(q[2])} q3={hex(q[3])} q4={hex(q[4])} q5={hex(q[5])} q6={hex(q[6])}')

print('\n=== UOutlineComponent Properties (0xebc2d00..0xebc3200) ===')
for off in range(0xebc2d00, 0xebc3200, 8):
    val = struct.unpack_from('<Q', data, off)[0]
    s = cstr(val)
    if s:
        q = struct.unpack_from('<7Q', data, off)
        print(f'  {hex(off)}: "{s}" | q1={hex(q[1])} q2={hex(q[2])} q3={hex(q[3])} q4={hex(q[4])} q5={hex(q[5])}')
