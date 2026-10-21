#!/usr/bin/env python3
"""Write the fuzz seed corpora: well-formed synthetic ZMDB databases, the
review's T1.4 crash inputs, and T1.5 MTP replies with wrapping counts.
Usage: make-seeds.py <output-dir>   (creates <dir>/zmdb and <dir>/mtp).
Never put real device dumps here: they hold personal library data."""
import os, struct, sys

def zmdb(hd=True, mutate=None):
    blob = bytearray(4096)
    blob[0:4] = b'ZMDB'
    blob[0x20:0x24] = b'ZMed'
    struct.pack_into('<H', blob, 0x24, 5 if hd else 2)
    zarr = 0x30
    for i in range(96):
        blob[zarr + i * 20: zarr + i * 20 + 4] = b'ZArr'
    def desc(i, size, count, offset):
        struct.pack_into('<HII', blob, zarr + i * 20 + 6, size, count, 0)
        struct.pack_into('<I', blob, zarr + i * 20 + 16, offset)
    records, index = 0xC00, []
    def record(atom, fixed, text):
        nonlocal records
        body = fixed + (text.encode() + b'\0' if text is not None else b'')
        struct.pack_into('<I', blob, records, len(body))
        blob[records + 4: records + 4 + len(body)] = body
        index.append((atom, records + 4))
        records += 4 + len(body)
    record(0x08000001, bytes(4 if hd else 1), 'Artist')
    record(0x06000001, struct.pack('<I', 0x08000001) + bytes((20 if hd else 12) - 4), 'Album')
    record(0x09000001, bytes(1), 'Rock')
    track = struct.pack('<IIIIiiH', 0x06000001, 0x08000001, 0x09000001, 0, 215000, 4000000, 7)
    track += bytes((32 if hd else 28) - len(track))
    record(0x01000001, track, 'Song')
    for k, (atom, offset) in enumerate(index):
        struct.pack_into('<II', blob, 0x800 + k * 8, atom, offset)
    desc(0, 8, len(index), 0x800)
    struct.pack_into('<I', blob, 0x900, 0x01000001); desc(1, 4, 1, 0x900)
    struct.pack_into('<I', blob, 0x940, 0x06000001); desc(3, 4, 1, 0x940)
    struct.pack_into('<I', blob, 0x980, 0x08000001); desc(4, 4, 1, 0x980)
    if mutate: mutate(blob, desc)
    return bytes(blob)

def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)

out = sys.argv[1]
os.makedirs(os.path.join(out, 'zmdb'), exist_ok=True)
os.makedirs(os.path.join(out, 'mtp'), exist_ok=True)
z = lambda name, data: write(os.path.join(out, 'zmdb', name), data)
z('valid-hd', zmdb(True))
z('valid-classic', zmdb(False))
# The review's proof input: descriptor-0 data_offset 0xFFFFFFF8.
z('crash-wrapped-index-offset', zmdb(True, lambda b, d: d(0, 8, 1, 0xFFFFFFF8)))
z('crash-huge-index-count', zmdb(True, lambda b, d: d(0, 8, 0xFFFFFFFF, 0x800)))
z('crash-wrapped-music-offset', zmdb(True, lambda b, d: d(1, 4, 1, 0xFFFFFFFC)))
z('crash-zero-entry-size', zmdb(True, lambda b, d: d(1, 0, 0xFFFFFFFF, 0x900)))
z('truncated-header', zmdb(True)[:0x3F])

m = lambda name, selector, body: write(os.path.join(out, 'mtp', name), bytes([selector]) + body)
m('storage-ids', 0, struct.pack('<II', 1, 0x00010001))
m('storage-ids-wrapping', 0, struct.pack('<II', 0x40000000, 0x00010001))
m('references-wrapping', 1, struct.pack('<II', 0x40000000, 42))
m('handles', 2, struct.pack('<III', 2, 5, 6))
m('props-supported', 3, struct.pack('<IHH', 2, 0xDC44, 0xDC46))
name = 'track.mp3\0'
m('object-info', 4, bytes(52) + bytes([len(name)]) + name.encode('utf-16-le'))
m('representative-sample', 5, struct.pack('<I', 4) + b'\xff\xd8\xff\xd9')
m('ucs2-string', 6, bytes([3]) + 'Hi\0'.encode('utf-16-le'))
def mtp_string(text):
    text += '\0'
    return bytes([len(text)]) + text.encode('utf-16-le')
info = bytes(8) + mtp_string('microsoft.com: 1.0') + bytes(2)
info += struct.pack('<IHH', 2, 0x1001, 0x1002) + struct.pack('<I', 0) * 4
info += mtp_string('Microsoft') + mtp_string('Zune') + mtp_string('1.0') + mtp_string('SERIAL')
m('device-info', 7, info)
m('device-info-wrapping', 7, bytes(8) + mtp_string('x') + bytes(2) + struct.pack('<I', 0x80000000))
