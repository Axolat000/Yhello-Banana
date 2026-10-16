#!/usr/bin/env python3
"""Regenerate the pinned Astro production fixture (requires Node/npm).

Normal OS builds and browser tests use the committed output, without npm.
All npm installation and build work stays below build/test/astro-project.
"""
from pathlib import Path
import argparse
import hashlib
from html import escape
import json
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--npm', default='npm', help='npm executable (default: from PATH)')
args = parser.parse_args()
npm = shutil.which(args.npm)
if not npm:
    parser.error('npm is required only to regenerate this fixture; install Node/npm first')
npm = Path(npm).absolute()
env = dict(os.environ, ASTRO_TELEMETRY_DISABLED='1', PATH=str(npm.parent) + os.pathsep + os.environ['PATH'])
fixtures = root / 'web/fixtures'
work = root / 'build/test/astro-project'
shutil.copytree(fixtures / 'astro-src', work, dirs_exist_ok=True)
subprocess.run([str(npm), 'ci', '--no-audit', '--no-fund'], cwd=work, env=env, check=True)
subprocess.run([str(npm), 'run', 'build'], cwd=work, env=env, check=True)

# This folder contains generated output only; remove obsolete hashed chunks.
dest = fixtures / 'astro'
if dest.exists():
    shutil.rmtree(dest)
shutil.copytree(work / 'dist', dest)
entries, asm = [], []
for i, path in enumerate(sorted(p for p in dest.rglob('*') if p.is_file())):
    rel = path.relative_to(dest).as_posix()
    name = f'astro_{i}'
    mime = {'.html': 'text/html', '.css': 'text/css', '.js': 'application/javascript'}[path.suffix]
    urls = ['about:frameworks/astro/' + rel]
    if rel == 'index.html':
        urls += ['about:frameworks/astro', 'about:frameworks/astro/']
    entries += [f'ASTRO_RESOURCE({json.dumps(url)},{name},{json.dumps(mime)})' for url in urls]
    asm.append(f'FIXTURE {name}, {json.dumps(path.relative_to(root).as_posix())}')
(fixtures / 'astro_resources.inc').write_text('\n'.join(entries) + '\n')
(fixtures / 'astro_resources.asm').write_text('\n'.join(asm) + '\n')

notices = []
for pkg in ('astro', '@astrojs/preact', 'preact', '@preact/signals', '@preact/signals-core', 'vite'):
    package = work / 'node_modules' / pkg
    meta = json.loads((package / 'package.json').read_text())
    license_file = next(p for p in sorted(package.iterdir()) if p.is_file() and p.name.lower() in ('license', 'license.md', 'license.txt'))
    notices.append(f"{pkg} {meta['version']}\n\n{license_file.read_text()}")
(fixtures / 'astro-LICENSE').write_text('\n\n'.join(notices))
html = ['<!doctype html><html><head><title>Framework licenses</title></head><body><h1>Framework licenses</h1><p><a href="about:frameworks">Back to demos</a></p>']
for license_file in sorted(fixtures.glob('*-LICENSE')):
    html += ['<h2>' + escape(license_file.stem) + '</h2><pre>' + escape(license_file.read_text()) + '</pre>']
(fixtures / 'licenses.html').write_text('\n'.join(html) + '\n</body></html>\n')

resources = [fixtures / f for f in ('react.js', 'react-dom.js', 'vue.js', 'alpine.js')]
resources += sorted(p for p in dest.rglob('*') if p.is_file())
(fixtures / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(fixtures).as_posix() + '\n' for p in resources))
print(f'Generated {len(asm)} Astro resources and their offline mappings.')
