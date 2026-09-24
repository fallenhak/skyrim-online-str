"""Dump interior cells whose EDID starts with a prefix, with their placed actors (ACHR).

Used to write Data/renewable_encounters.txt (docs/renewable_encounters.example.txt):
cell and ACHR form ids are printed as load-order form ids for a master at index 00.

    python Tools/Scripts/esm_encounter_cells.py <path to Skyrim.esm> <edid prefix> [...]
"""
import struct, sys, zlib

if len(sys.argv) < 3:
    sys.exit(__doc__)

ESM = sys.argv[1]
PREFIXES = [p.lower() for p in sys.argv[2:]]

data = open(ESM, "rb").read()

def fields(buf):
    out, i, big = [], 0, None
    while i + 6 <= len(buf):
        t, n = buf[i:i+4], struct.unpack_from("<H", buf, i+4)[0]
        i += 6
        if t == b"XXXX":
            big = struct.unpack_from("<I", buf, i)[0]; i += n; continue
        if big is not None:
            n, big = big, None
        out.append((t, buf[i:i+n])); i += n
    return out

def record_data(off):
    size, flags = struct.unpack_from("<II", data, off+4)
    body = data[off+24:off+24+size]
    if flags & 0x00040000:
        body = zlib.decompress(body[4:])
    return body

def edid(fs):
    for t, v in fs:
        if t == b"EDID":
            return v.split(b"\0")[0].decode("latin1")
    return ""

names = {}      # formid -> EDID for NPC_/LVLN
cells = {}      # formid -> EDID (interior)
achr_by_cell = {}
cur_cell = None

def walk(off, end):
    global cur_cell
    while off < end:
        typ = data[off:off+4]
        if typ == b"GRUP":
            gsize, label, gtype = struct.unpack_from("<I4sI", data, off+4)
            if gtype == 0 and label not in (b"CELL", b"NPC_", b"LVLN"):
                off += gsize; continue
            if gtype in (6, 8, 9, 10):
                cur_cell = struct.unpack_from("<I", label)[0] if gtype != 10 else cur_cell
            if gtype == 1:  # worldspace children: exteriors, skip
                off += gsize; continue
            walk(off+24, off+gsize); off += gsize; continue
        size, flags, fid = struct.unpack_from("<III", data, off+4)
        if typ in (b"NPC_", b"LVLN"):
            names[fid] = edid(fields(record_data(off)))
        elif typ == b"CELL":
            e = edid(fields(record_data(off)))
            if any(e.lower().startswith(p) for p in PREFIXES):
                cells[fid] = e
        elif typ == b"ACHR" and cur_cell in cells:
            base = next((struct.unpack_from("<I", v)[0] for t, v in fields(record_data(off)) if t == b"NAME"), 0)
            achr_by_cell.setdefault(cur_cell, []).append((fid, base, bool(flags & 0x400)))
        off += 24 + size

hsize = struct.unpack_from("<I", data, 4)[0]
walk(24 + hsize, len(data))

for cid, e in sorted(cells.items()):
    actors = achr_by_cell.get(cid, [])
    print(f"CELL {cid:08X} {e} actors={len(actors)}")
    for fid, base, persistent in actors:
        print(f"   ACHR {fid:08X} base={base:08X} {names.get(base, '?')}{' [persistent]' if persistent else ''}")
