"""Regenerate Code/client/Services/SyncedWorldForms.h from the official masters.

Two allowlists of base forms the object sync would otherwise skip because of their type:
- lever furniture (FURN with the TrapLever / defaultPillarPuzzleLever script): pulling one
  opens gates and doors by script, so it is relayed like an activator;
- harvestable trees (TREE with an ingredient, PFIG): lavender, mountain flowers, cabbages
  and the like, harvested exactly like flora.
Runtime base form ids; the official masters always load in ORDER, so the ids are stable.

Usage: python generate_synced.py <Skyrim Data folder> <path to SyncedWorldForms.h>
"""
import os
import re
import struct
import sys
import zlib

import esm_activators as esm
from generate import ORDER, masters

LEVER_SCRIPTS = re.compile(r'^(TrapLever|defaultPillarPuzzleLever)$', re.I)


def scan(path, label):
    with open(path, 'rb') as f:
        buf = f.read()
    o = 24 + struct.unpack_from('<I', buf, 4)[0]
    while o < len(buf):
        gsize = struct.unpack_from('<I', buf, o + 4)[0]
        if buf[o + 8:o + 12] == label:
            p, end = o + 24, o + gsize
            while p < end:
                size, flags, fid = struct.unpack_from('<III', buf, p + 4)
                body = buf[p + 24:p + 24 + size]
                if flags & 0x40000:
                    body = zlib.decompress(body[4:])
                fields = {}
                for t, d in esm.subrecords(body):
                    fields.setdefault(t, d)
                yield fid, fields
                p += 24 + size
        o += gsize


def collect(data_dir, label):
    final = {}  # runtime base form id -> fields of the last overriding record
    for name in ORDER:
        path = os.path.join(data_dir, name)
        file_masters = masters(path) + [name]
        for fid, fields in scan(path, label):
            final[(ORDER.index(file_masters[fid >> 24]) << 24) | (fid & 0xFFFFFF)] = fields
    return final


def edid(fields):
    return fields.get(b'EDID', b'').rstrip(b'\0').decode('cp1252', 'replace')


def is_lever(fields):
    if b'VMAD' not in fields:
        return False
    try:
        return any(LEVER_SCRIPTS.match(s) for s in esm.parse_vmad(fields[b'VMAD']))
    except Exception:
        return False


def is_harvestable_tree(fields):
    return b'PFIG' in fields and struct.unpack_from('<I', fields[b'PFIG'])[0] != 0


def replace_array(header, name, forms):
    body = ''.join(f'    0x{fid:08X}, // {edid(forms[fid])}\n' for fid in sorted(forms))
    start = header.index(name + '{') + len(name + '{\n')
    end = header.index('};', start)
    header = header[:start] + body + header[end:]
    return re.sub(r'std::array<uint32_t, \d+> ' + name, f'std::array<uint32_t, {len(forms)}> {name}', header)


def main(data_dir, header_path):
    levers = {fid: f for fid, f in collect(data_dir, b'FURN').items() if is_lever(f)}
    trees = {fid: f for fid, f in collect(data_dir, b'TREE').items() if is_harvestable_tree(f)}
    print(f'{len(levers)} lever furniture, {len(trees)} harvestable trees')
    with open(header_path, encoding='utf-8') as f:
        header = f.read()
    header = replace_array(header, 'kLeverFurniture', levers)
    header = replace_array(header, 'kHarvestableTrees', trees)
    with open(header_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(header)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
