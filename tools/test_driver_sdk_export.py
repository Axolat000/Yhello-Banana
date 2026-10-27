#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the driver examples with only the exported independent Driver SDK."""
import subprocess,sys,tarfile,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='banana-driver-sdk-') as temp:
    base=Path(temp);archive=base/'driver.tar.gz'
    subprocess.run([sys.executable,'tools/export_driver_sdk.py','-o',str(archive)],cwd=root,check=True)
    with tarfile.open(archive) as t:
        names=t.getnames()
        assert 'banana-driver-sdk/include/banana.h' not in names
        assert 'banana-driver-sdk/banana.mk' not in names
        assert not any('/build/' in n or n.endswith('.bpk') for n in names)
        t.extractall(base,filter='data')
    sdk=base/'banana-driver-sdk'
    assert not (base/'sdk').exists()
    for name in ['edu','bochsfb','sdk-probe','usb-serial-demo']:
        subprocess.run(['make','-j2','-C',str(sdk/'examples'/name)],cwd=base,check=True)
        subprocess.run([sys.executable,str(sdk/'tools/bpkg'),'check',
                        str(sdk/'examples'/name/(name+'.bpk'))],cwd=base,check=True)
print('Standalone Driver SDK export: all four examples built for both architectures without application SDK')
