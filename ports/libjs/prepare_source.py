#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch pinned source archives and publish only verified, complete trees."""
from pathlib import Path
import hashlib,json,os,shutil,tarfile,tempfile,urllib.request
here=Path(__file__).resolve().parent
sources=json.loads((here/'sources.json').read_text())
cache=here/'src';cache.mkdir(exist_ok=True)
reuse=Path(os.environ.get('LIBJS_DOWNLOAD_CACHE',str(cache)))
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
for name,entry in sources.items():
    archive=cache/entry['archive']
    if not archive.exists():
        with tempfile.NamedTemporaryFile(dir=cache,delete=False) as f:
            temporary=Path(f.name)
            try:
                if (reuse/entry['archive']).is_file():
                    with (reuse/entry['archive']).open('rb') as source:shutil.copyfileobj(source,f)
                else:
                    req=urllib.request.Request(entry['url'],headers={'User-Agent':'BananaOS-LibJS-prototype'})
                    with urllib.request.urlopen(req,timeout=120) as source:shutil.copyfileobj(source,f)
                f.flush()
                if digest(temporary)!=entry['sha256']:raise RuntimeError('source checksum mismatch: '+name)
                os.replace(temporary,archive)
            finally:
                temporary.unlink(missing_ok=True)
    if digest(archive)!=entry['sha256']:raise RuntimeError('source checksum mismatch: '+name)
    if 'directory' not in entry:continue
    target=cache/entry['directory']
    marker=target/'.banana-source-sha256'
    if target.exists():
        if not marker.exists() or marker.read_text().strip()!=entry['sha256']:
            raise RuntimeError('unverified existing source directory: '+str(target))
        continue
    with tempfile.TemporaryDirectory(dir=cache) as temporary:
        with tarfile.open(archive) as tar:tar.extractall(temporary,filter='data')
        tree=Path(temporary)/entry['directory']
        if not tree.is_dir():raise RuntimeError('missing archive root: '+name)
        (tree/'.banana-source-sha256').write_text(entry['sha256']+'\n')
        if name=='ladybird':
            path=tree/'AK/StackInfo.cpp';text=path.read_text()
            old='#if defined(AK_OS_LINUX) && !defined(AK_OS_ANDROID) && !defined(AK_LIBC_GLIBC)'
            if text.count(old)!=1:raise RuntimeError('StackInfo patch no longer applies')
            # SDK pthread_getattr_np already returns the actual usable stack.
            text=text.replace(old,old+' && !defined(__BANANA_OS__)')
            path.write_text(text)
        os.replace(tree,target)
    print('verified source:',name,entry['revision'],flush=True)
