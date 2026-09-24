#!/usr/bin/env python3
"""Strip sections from DossierExt profile INIs.

Use to drop records that came from a known-bad key (e.g. every map recorded as
"spawnmap" before the UIMapName fix) without losing the rest of a profile.

    python3 tools/clean_profiles.py <profile-dir> [substring ...]

Any section whose name contains one of the substrings (default: "spawnmap") is
removed, including its sub-sections. A .bak-<timestamp> copy is written first.
"""
import sys
import os
import time


def clean(path, needles):
    with open(path, 'r', encoding='latin-1') as f:
        lines = f.readlines()

    out, section, dropped, dropping = [], '', set(), False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('['):
            end = stripped.find(']')
            if end != -1:
                section = stripped[1:end]
                dropping = any(n.lower() in section.lower() for n in needles)
                if dropping:
                    dropped.add(section)
        if not dropping:
            out.append(line)

    if not dropped:
        return []

    backup = '%s.bak-%s' % (path, time.strftime('%Y%m%d-%H%M%S'))
    os.replace(path, backup)
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='latin-1') as f:
        f.writelines(out)
    os.replace(tmp, path)
    return sorted(dropped)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    directory = sys.argv[1]
    needles = sys.argv[2:] or ['spawnmap']
    for name in sorted(os.listdir(directory)):
        if not name.endswith('.ini'):
            continue
        path = os.path.join(directory, name)
        dropped = clean(path, needles)
        if dropped:
            print('%s: removed %d section(s)' % (name, len(dropped)))
            for s in dropped:
                print('    [%s]' % s)
        else:
            print('%s: nothing to remove' % name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
