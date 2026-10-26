#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent WPA2/CCMP vectors from hashlib/hmac and cryptography (AESCCM/key wrap)."""
import argparse
import hashlib
import hmac
import os
from pathlib import Path
import random
import shlex
import subprocess
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from cryptography.hazmat.primitives.keywrap import aes_key_wrap

ROOT = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sanitize', action='store_true')
ap.add_argument('--32', dest='bits32', action='store_true')
a = ap.parse_args()
out = ROOT / 'build/test/wifi_crypto'
out.mkdir(parents=True, exist_ok=True)
rng = random.Random(0xbada551)
def data(n):
    return bytes(rng.randrange(256) for _ in range(n))
def array(name, value):
    # Empty arrays use one storage byte; the API still receives length zero.
    return 'static const uint8_t ' + name + '[]={' + ','.join(str(v) for v in value or b'\0') + '};\n'
cases = ['static void vector_tests(void) {\nuint8_t result[2304], mic[8];\n']
for n in [0, 1, 55, 56, 63, 64, 65, 255, 1024]:
    for kn in [0, 1, 20, 64, 65, 128]:
        key, message = data(kn), data(n)
        expected = hmac.new(key, message, 'sha1').digest()
        cases += ['{\n', array('key',key), array('message',message), array('expected',expected),
                  f'wifi_hmac_sha1(key,{kn},message,{n},result); CHECK(!memcmp(result,expected,20));\n',
                  '}\n']
for password, ssid in [(b'password',b'IEEE'), (b'ThisIsAPassword',b'ThisIsASSID'),
                       (b'x'*63, b's'*32), (b'12345678',b'embedded\0SSID')]:
    expected = hashlib.pbkdf2_hmac('sha1', password, ssid, 4096, 32)
    cases += ['{\n',array('password', password+b'\0'), array('ssid',ssid), array('expected',expected),
              f'CHECK(wifi_pmk((const char*)password,ssid,{len(ssid)},result)==0);\n',
              'CHECK(!memcmp(result,expected,32));\n}\n']
for n in [16, 24, 32, 48, 128, 504]:
    key, plain = data(16), data(n)
    wrapped = aes_key_wrap(key, plain)
    cases += ['{\n',array('key',key),array('wrapped',wrapped),array('plain',plain),
              f'CHECK(wifi_key_unwrap(key,wrapped,{len(wrapped)},result)=={n});\n',
              f'CHECK(!memcmp(result,plain,{n}));\n',
              f'uint8_t bad[{len(wrapped)}]; memcpy(bad,wrapped,sizeof(bad)); bad[0]^=1;\n',
              'CHECK(wifi_key_unwrap(key,bad,sizeof(bad),result)<0);\n',
              f'for(int i=0;i<{n};i++) CHECK(result[i]==0);\n', '}\n']
for _ in range(12):
    pmk, aa, spa, an, sn = data(32), data(6), data(6), data(32), data(32)
    seed = b'Pairwise key expansion\0' + min(aa,spa)+max(aa,spa)+min(an,sn)+max(an,sn)
    expected = b''.join(hmac.new(pmk, seed+bytes([i]), 'sha1').digest() for i in range(3))[:48]
    cases += ['{\n',array('pmk',pmk),array('aa',aa),array('spa',spa),array('an',an),array('sn',sn),
              array('expected',expected),'wifi_ptk(pmk,aa,spa,an,sn,result); CHECK(!memcmp(result,expected,48));\n}\n']
for qos in [False, True]:
    for n in [0, 1, 15, 16, 17, 1500, 2304]:
        key, plain = data(16), data(n)
        header = bytearray(data(26 if qos else 24))
        header[0] = 0x88 if qos else 8
        header[1] = 0x42 if qos else 0x41
        header[22] &= 0xf0
        tid = header[24]&15 if qos else 0
        pn = rng.randrange(1, 1<<48)
        nonce = bytes([tid])+header[10:16]+pn.to_bytes(6,'big')
        aad = bytes([header[0]&0x8f,header[1]&(0x47 if qos else 0xc7)])
        aad += header[4:22] + bytes([header[22]&15, 0])
        if qos:
            aad += bytes([tid,0])
        ciphertext = AESCCM(key, tag_length=8).encrypt(nonce,plain,aad)
        cases += ['{\n',array('key',key),array('header',bytes(header)),array('plain',plain),
                  array('ciphertext',ciphertext),f'memcpy(result,plain,{n});\n',
                  f'CHECK(wifi_ccmp_seal(key,header,sizeof(header),{pn}ULL,result,{n},mic)==0);\n',
                  f'CHECK(!memcmp(result,ciphertext,{n}) && !memcmp(mic,ciphertext+{n},8));\n',
                  f'CHECK(wifi_ccmp_open(key,header,sizeof(header),{pn}ULL,result,{n},mic)==0);\n',
                  f'CHECK(!memcmp(result,plain,{n}));\n',
                  f'memcpy(result,ciphertext,{n}); mic[0]^=1;\n',
                  f'CHECK(wifi_ccmp_open(key,header,sizeof(header),{pn}ULL,result,{n},mic)<0);\n',
                  f'for(int i=0;i<{n};i++) CHECK(result[i]==0);\n',
                  '}\n']
cases += ['}\n']
(out / 'vectors.h').write_text(''.join(cases))
flags = ['-O1','-g','-Wall','-Wextra','-Werror','-Ikernel','-Icrypto', '-I'+str(out)]
if a.bits32:
    flags += ['-m32']
if a.sanitize:
    flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
binary = out / 'test'
subprocess.run([*shlex.split(os.environ.get('CC','cc')), *flags,
                'tools/tests/wifi_crypto.c','crypto/wifi_crypto.c','crypto/aead.c','crypto/chacha20.c',
                '-o',str(binary)],cwd=ROOT,check=True)
subprocess.run([str(binary)],cwd=ROOT,check=True)
