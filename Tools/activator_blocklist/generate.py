"""Regenerate Code/client/Services/LocalOnlyActivators.h from the official masters.

Activators whose attached Papyrus script marks them as local-only (ore veins, shrines,
standing stones, crafting triggers, critters, resource objects) are listed by runtime
base form id. The official masters always load in ORDER, so the ids are stable.

Usage: python generate.py <Skyrim Data folder> <path to LocalOnlyActivators.h>
"""
import collections
import os
import re
import struct
import sys

import esm_activators as esm

ORDER = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'HearthFires.esm', 'Dragonborn.esm']
LOCAL_ONLY_SCRIPTS = re.compile(
    r'^(MineOreScript|TempleBlessingScript|powerShrineScript|DLC2TempleShrineScript|'
    r'dlc2standingstonescript|DLC2StandingStoneFX|BlacksmithForge01|CraftingActivateLinker|'
    r'BYOHHouseCraftingTriggerScript|DLC2ExpSpiderCraftingSCRIPT|critter.*|DLC1CritterFollowSCRIPT|'
    r'FXfakeCritterScript|ResourceObjectScript|BYOHHouseFishHatcheryScript)$', re.I)


def masters(path):
    with open(path, 'rb') as f:
        buf = f.read(1 << 20)
    size = struct.unpack_from('<I', buf, 4)[0]
    return [d.rstrip(b'\0').decode() for t, d in esm.subrecords(buf[24:24 + size]) if t == b'MAST']


def main(data_dir, header_path):
    final = {}  # runtime base form id -> last overriding record
    for name in ORDER:
        path = os.path.join(data_dir, name)
        file_masters = masters(path) + [name]
        for record in esm.scan(path, name):
            runtime_id = (ORDER.index(file_masters[record['hi']]) << 24) | record['id']
            final[runtime_id] = record

    local_only = {fid: r for fid, r in final.items() if any(LOCAL_ONLY_SCRIPTS.match(s) for s in r['scripts'])}
    by_script = collections.Counter(
        next(s for s in r['scripts'] if LOCAL_ONLY_SCRIPTS.match(s)) for r in local_only.values())
    print(f'{len(final)} activators, {len(local_only)} local-only')
    for script, count in by_script.most_common():
        print(f'{count:5} {script}')

    body = ''.join(f'    0x{fid:08X}, // {local_only[fid]["edid"]}\n' for fid in sorted(local_only))
    with open(header_path, encoding='utf-8') as f:
        header = f.read()
    start = header.index('kBaseFormIds{') + len('kBaseFormIds{\n')
    end = header.index('};', start)
    header = header[:start] + body + header[end:]
    header = re.sub(r'std::array<uint32_t, \d+>', f'std::array<uint32_t, {len(local_only)}>', header)
    with open(header_path, 'w', encoding='utf-8') as f:
        f.write(header)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
