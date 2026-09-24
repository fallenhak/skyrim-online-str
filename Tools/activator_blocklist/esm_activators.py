"""Scan ESM masters for ACTI records and group them by attached Papyrus scripts."""
import collections, os, struct, sys, zlib

FILES = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'HearthFires.esm', 'Dragonborn.esm']


def wstr(b, o):
    n = struct.unpack_from('<H', b, o)[0]
    return b[o + 2:o + 2 + n].decode('cp1252', 'replace'), o + 2 + n


def parse_vmad(b):
    ver, fmt, cnt = struct.unpack_from('<hhH', b, 0)
    o = 6
    names = []
    for _ in range(cnt):
        name, o = wstr(b, o)
        names.append(name)
        if ver >= 4:
            o += 1
        pc = struct.unpack_from('<H', b, o)[0]; o += 2
        for _ in range(pc):
            _, o = wstr(b, o)
            t = b[o]; o += 1
            if ver >= 4:
                o += 1
            if t == 1: o += 8
            elif t == 2: _, o = wstr(b, o)
            elif t in (3, 4): o += 4
            elif t == 5: o += 1
            elif t in (11, 12, 13, 14, 15):
                n = struct.unpack_from('<I', b, o)[0]; o += 4
                for _ in range(n):
                    if t == 11: o += 8
                    elif t == 12: _, o = wstr(b, o)
                    elif t in (13, 14): o += 4
                    else: o += 1
            else:
                return names  # unknown type: keep what we have
    return names


def subrecords(data):
    o = 0; big = None
    while o + 6 <= len(data):
        t = data[o:o + 4]; n = struct.unpack_from('<H', data, o + 4)[0]; o += 6
        if t == b'XXXX':
            big = struct.unpack_from('<I', data, o)[0]; o += n; continue
        if big is not None:
            n = big; big = None
        yield t, data[o:o + n]; o += n


def scan(path, fname):
    out = []
    with open(path, 'rb') as f:
        buf = f.read()
    # TES4 header -> master count
    hsize = struct.unpack_from('<I', buf, 4)[0]
    masters = sum(1 for t, _ in subrecords(buf[24:24 + hsize]) if t == b'MAST')
    o = 24 + hsize
    while o < len(buf):
        gsize = struct.unpack_from('<I', buf, o + 4)[0]
        label = buf[o + 8:o + 12]
        if label == b'ACTI':
            p = o + 24; end = o + gsize
            while p < end:
                rt = buf[p:p + 4]; size, flags, fid = struct.unpack_from('<III', buf, p + 4)
                body = buf[p + 24:p + 24 + size]
                if flags & 0x40000:
                    body = zlib.decompress(body[4:])
                edid = ''; scripts = []
                for t, d in subrecords(body):
                    if t == b'EDID': edid = d.rstrip(b'\0').decode('cp1252', 'replace')
                    elif t == b'VMAD':
                        try: scripts = parse_vmad(d)
                        except Exception: scripts = ['<vmad-parse-error>']
                owner = fname if (fid >> 24) == masters else f'master#{fid >> 24}'
                out.append({'file': fname, 'owner': owner, 'hi': fid >> 24, 'id': fid & 0xFFFFFF, 'edid': edid, 'scripts': scripts})
                p += 24 + size
        o += gsize
    return out


if __name__ == '__main__':
    # Usage: python esm_activators.py <Skyrim Data folder>  -> histogram of attached scripts
    allr = []
    for fn in FILES:
        allr += scan(os.path.join(sys.argv[1], fn), fn)
    c =collections.Counter(s for r in allr for s in (r['scripts'] or ['<none>']))
    print(len(allr), 'ACTI records')
    for s, n in c.most_common(60):
        print(f'{n:5} {s}')
