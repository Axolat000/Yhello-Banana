# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent model-access-point WPA2 fixtures, shared by station/USB tests."""
import hashlib
import hmac
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
from cryptography.hazmat.primitives.ciphers.aead import AESCCM

def write_vectors(out):
    own=bytes.fromhex('020000000001')
    bssid=bytes.fromhex('020000000002')
    ssid=b'TestNet'
    an=bytes(range(32));sn=bytes(range(64,96))
    pmk=hashlib.pbkdf2_hmac('sha1',b'12345678',ssid,4096,32)
    seed=b'Pairwise key expansion\0'+min(own,bssid)+max(own,bssid)+min(an,sn)+max(an,sn)
    ptk=b''.join(hmac.new(pmk,seed+bytes([i]),'sha1').digest() for i in range(3))[:48]
    gtk=bytes(range(16));newgtk=bytes(range(16,32))
    rsn=bytes.fromhex('30140100000fac040100000fac040100000fac020000')
    snap=bytes.fromhex('aaaa03000000')
    def hdr(subtype=8,direction=2,dst=own,src=bssid):
        return bytes([subtype,direction,0,0])+dst+bssid+src+b'\0\0'
    def elem(n,b):
        return bytes([n,len(b)])+b
    beacon=hdr(0x80,0,b'\xff'*6)+b'\0'*8+(100).to_bytes(2,'little')+(0x431).to_bytes(2,'little')
    beacon+=elem(0,ssid)+elem(1,bytes.fromhex('82848b960c121824'))+elem(3,b'\1')+rsn
    def eapol(info,counter,nonce=b'\0'*32,extra=b'',keylen=16):
        e=bytearray(99+len(extra))
        e[0:7]=b'\2\3'+(95+len(extra)).to_bytes(2,'big')+b'\2'+info.to_bytes(2,'big')
        e[7:9]=keylen.to_bytes(2,'big');e[9:17]=counter.to_bytes(8,'big');e[17:49]=nonce
        e[97:99]=len(extra).to_bytes(2,'big');e[99:]=extra
        if info&0x100:
            e[81:97]=hmac.new(ptk[:16],e,'sha1').digest()[:16]
        return bytes(e)
    def data_frame(e):
        return hdr()+snap+b'\x88\x8e'+e
    def wrapped(key,rsn_flag=False):
        d=(rsn if rsn_flag else b'')+elem(221,bytes.fromhex('000fac01')+b'\1\0'+key)
        if len(d)%8:
            d+=b'\xdd'+b'\0'*((7-len(d))%8)
        return aes_key_wrap(ptk[16:32],d)
    def ccmp(tk,pn,group=False):
        dest=b'\xff'*6 if group else own
        h=hdr(8,0x42,dest)
        nonce=b'\0'+bssid+pn.to_bytes(6,'big')
        aad=bytes([h[0]&0x8f,h[1]&0xc7])+h[4:22]+bytes([h[22]&15,0])
        plain=snap+b'\x08\0'+bytes(range(46))
        cipher=AESCCM(tk,tag_length=8).encrypt(nonce,plain,aad)
        iv=pn.to_bytes(6,'little')
        iv=iv[:2]+bytes([0,0x60 if group else 0x20])+iv[2:]
        return h+iv+cipher
    tx_header=bytes([8,0x41,0,0])+bssid+own+b'\xff'*6+b'\0\0'
    tx_aad=bytes([8,0x41])+tx_header[4:22]+b'\0\0'
    tx_cipher=AESCCM(ptk[32:48],tag_length=8).encrypt(b'\0'+own+(1).to_bytes(6,'big'),snap+b'\x08\0'+b'\0'*46,tx_aad)
    fixtures={
        'TX_CIPHER':tx_cipher,
        'BEACON':beacon,
        'AUTH':hdr(0xb0,0)+bytes.fromhex('000002000000'),
        'ASSOC':hdr(0x10,0)+bytes.fromhex('3104000001c0'),
        'M1':data_frame(eapol(0x8a,1,an)),
        'M2_EXPECTED':eapol(0x10a,1,sn,rsn,0),
        'M3':data_frame(eapol(0x13ca,2,an,wrapped(gtk,True))),
        'M4_EXPECTED':eapol(0x30a,2,keylen=0),
        'GROUP1':data_frame(eapol(0x1382,3,extra=wrapped(newgtk),keylen=0)),
        'GROUP2':data_frame(eapol(0x1382,4,extra=wrapped(newgtk),keylen=0)),
        'UNICAST8':ccmp(ptk[32:48],8),
        'UNICAST9':ccmp(ptk[32:48],9),
        'MULTICAST2':ccmp(newgtk,2,True),
        'PTK':ptk,
    }
    text=''
    for name,b in fixtures.items():
        text+='static const uint8_t '+name+'[]={'+','.join(str(v) for v in b)+'};\n'
    (out/'station_vectors.h').write_text(text)
