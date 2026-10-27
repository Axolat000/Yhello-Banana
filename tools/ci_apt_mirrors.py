#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Avoid the slow Azure mirror selected by current GitHub Ubuntu runners."""
import argparse
from pathlib import Path
import re

def normalize(text):
    text = text.replace('mirror+file:/etc/apt/apt-mirrors-security.txt',
                        'https://security.ubuntu.com/ubuntu')
    text = text.replace('mirror+file:/etc/apt/apt-mirrors.txt',
                        'https://archive.ubuntu.com/ubuntu')
    return re.sub(r'https?://azure\.archive\.ubuntu\.com/ubuntu',
                  'https://archive.ubuntu.com/ubuntu', text)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('/'))
    args = parser.parse_args()
    apt = args.root / 'etc/apt'
    files = [apt / 'sources.list']
    files += sorted((apt / 'sources.list.d').glob('*.list'))
    files += sorted((apt / 'sources.list.d').glob('*.sources'))
    changed = 0
    for path in files:
        if not path.is_file():
            continue
        original = path.read_text()
        updated = normalize(original)
        if updated != original:
            path.write_text(updated)
            changed += 1
    print(f'APT source normalization: {changed} files updated')

if __name__ == '__main__':
    main()
