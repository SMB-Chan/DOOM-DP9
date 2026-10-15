# Generates embedded_wad.h for DOOM_cy700:
# extracts an E1M1 + sprites/flats subset of doom1.wad, LZSS-compresses it,
# and emits a C header with the compressed image.
# Run inside WSL:  python3 /mnt/d/EXword/scripts/gen_embedded_wad.py
import struct
from collections import defaultdict

data = open('/tmp/doom1.wad', 'rb').read()
ident, numlumps, infotableofs = struct.unpack('<4sII', data[:12])
lumps = []
lump_map = {}
for i in range(numlumps):
    ofs = infotableofs + i * 16
    filepos, size, name = struct.unpack('<II8s', data[ofs:ofs+16])
    name_s = name.split(b'\x00', 1)[0].decode('ascii', errors='replace')
    lumps.append((name_s, filepos, size))
    lump_map[name_s] = (filepos, size)

keep = [('PLAYPAL', data[lump_map['PLAYPAL'][0]:lump_map['PLAYPAL'][0]+768]),
        ('COLORMAP', data[lump_map['COLORMAP'][0]:lump_map['COLORMAP'][0]+8192])]
for idx, (n, p, s) in enumerate(lumps):
    if n == 'E1M1':
        keep.append(('E1M1', b''))
        for k in range(1, 9):
            kn, kp, ks = lumps[idx+k]
            keep.append((kn, data[kp:kp+ks]))
        break

sprites = ['PISGA0', 'PISGB0', 'PISFA0', 'SHTGA0', 'SHTGB0', 'SHTFA0', 'CHGGA0', 'MISGA0', 'PUNGA0',
           'POSSA1', 'POSSB1', 'POSSH0', 'POSSL0',
           'SPOSA1', 'SPOSB1', 'SPOSH0', 'SPOSL0',
           'TROOA1', 'TROOB1', 'TROOH0', 'TROOM0',
           'SARGA1', 'SARGB1', 'SARGN0',
           'BAR1A0', 'BEXPA0',
           'STIMA0', 'MEDIA0', 'ARM1A0', 'ARM2A0', 'CLIPA0', 'SHELA0', 'SHOTA0',
           'BKEYA0', 'YKEYA0', 'RKEYA0', 'BON1A0', 'BON2A0',
           'STFST01', 'STFDEAD0', 'COLUA0']
keep.append(('S_START', b''))
for sp in sprites:
    if sp in lump_map:
        kp, ks = lump_map[sp]
        keep.append((sp, data[kp:kp+ks]))
keep.append(('S_END', b''))

e1m1_flats = ['FLOOR4_8', 'FLOOR7_1', 'FLAT14', 'CEIL3_5', 'NUKAGE3']
keep.append(('F_START', b''))
for fl in e1m1_flats:
    if fl in lump_map:
        kp, ks = lump_map[fl]
        keep.append((fl, data[kp:kp+ks]))
keep.append(('F_END', b''))

out = bytearray(b'IWAD' + struct.pack('<II', len(keep), 0))
dir_entries = bytearray()
for name, blob in keep:
    pos = len(out)
    out.extend(blob)
    dir_entries.extend(struct.pack('<II8s', pos, len(blob), name.encode('ascii').ljust(8, b'\x00')))
dir_ofs = len(out)
out[8:12] = struct.pack('<I', dir_ofs)
out.extend(dir_entries)

src = bytes(out)
n = len(src)
comp = bytearray()
i = 0
ht = defaultdict(list)

while i < n:
    flag_pos = len(comp)
    comp.append(0)
    flags = 0
    for bit in range(8):
        if i >= n:
            break
        best_len = 2
        best_off = 0
        if i + 3 <= n:
            key = src[i:i+3]
            for pos in reversed(ht[key][-48:]):
                off = i - pos
                if off > 4096:
                    break
                l = 3
                max_l = min(18, n - i)
                while l < max_l and src[pos + l] == src[i + l]:
                    l += 1
                if l > best_len:
                    best_len = l
                    best_off = off
                    if l == 18:
                        break
        if best_len >= 3:
            flags |= (1 << bit)
            val = ((best_off - 1) << 4) | (best_len - 3)
            comp.append(val & 0xff)
            comp.append((val >> 8) & 0xff)
            for k in range(best_len):
                if i + k + 3 <= n:
                    ht[src[i+k:i+k+3]].append(i + k)
            i += best_len
        else:
            comp.append(src[i])
            if i + 3 <= n:
                ht[src[i:i+3]].append(i)
            i += 1
    comp[flag_pos] = flags

with open('/mnt/d/EXword/addons_src/DOOM_cy700/src/embedded_wad.h', 'w') as f:
    f.write('#ifndef EMBEDDED_WAD_H\n#define EMBEDDED_WAD_H\n\n')
    f.write('#include <stdint.h>\n\n')
    f.write(f'#define EMBEDDED_WAD_RAW_SIZE {n}U\n')
    f.write(f'#define EMBEDDED_WAD_COMP_SIZE {len(comp)}U\n\n')
    f.write('static const uint8_t g_embedded_wad_lzss[EMBEDDED_WAD_COMP_SIZE] = {\n')
    for row_start in range(0, len(comp), 16):
        chunk = comp[row_start:row_start+16]
        f.write('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
    f.write('};\n\n#endif\n')
print('Generated embedded_wad.h successfully!')
