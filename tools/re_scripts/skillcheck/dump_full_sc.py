import struct

path = r'E:\atual\ida95-dbdm-suite\binaries\libUE4_fixed.so'
with open(path, 'rb') as f:
    data = f.read()

start, end = 0x6e18e90, 0x6e193c0
words = struct.unpack_from(f'<{(end-start)//4}I', data, start)
for i, w in enumerate(words):
    pc = start + i*4
    print(f'{hex(pc)}: {w:08x}')
