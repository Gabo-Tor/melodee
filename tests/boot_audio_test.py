#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Independent zlib fixtures, malformed decoder inputs and real packed image."""
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import boot_audio


def elf_symbol(path, wanted):
    """Independent ELF32 reader: verify the load address the startup uses."""
    data = path.read_bytes()
    assert data[:6] == b'\x7fELF\x01\x01'
    offset = struct.unpack_from('<I', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, offset + i * stride) for i in range(count)]
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, value = struct.unpack_from('<II', data, pos)
            if strings[name:strings.find(b'\0', name)].decode() == wanted:
                return value
    raise AssertionError('missing ELF symbol ' + wanted)


class BootAudio(unittest.TestCase):
    def test_raw_reference_and_collision(self):
        raw = bytes(random.Random(741).randbytes(512))
        self.assertEqual(boot_audio.pack(raw), raw)
        with self.assertRaisesRegex(ValueError, 'collides'):
            boot_audio.pack(b'AZF1')

    def test_dependency_gate(self):
        syms = '02001000 l F .text 00000020 boot_inflate\n02001040 l F .text 00000020 helper\n'
        dis = 'boot_inflate:\n 2001000: 00 00 \tcall 64 <helper : 2001040 >\nhelper:\n 2001040: 00 00 \trts\n'
        self.assertEqual(boot_audio.check_decoder(syms, dis, 0x2001000, 0x208d000), [])
        self.assertTrue(boot_audio.check_decoder(syms.replace('02001040', '01c01040'), dis, 0x2001000, 0x208d000))
        self.assertTrue(boot_audio.check_decoder(syms, dis.replace('call 64 <helper : 2001040 >', 'call r3'), 0x2001000, 0x208d000))
        self.assertTrue(boot_audio.check_decoder(syms + '01c01000 l O .data 00000020 boot_inflate.table\n', dis, 0x2001000, 0x208d000))

    def test_decoder(self):
        rng = random.Random(9025)
        payloads = [b'', b'A', bytes(range(256)), b'ABCD' * 32768,
                    bytes(rng.randrange(256) for _ in range(8192))]
        resident = Path('build/audiotext.bin')
        if resident.exists() and resident.stat().st_size:
            payloads.append(resident.read_bytes())
        with tempfile.TemporaryDirectory(prefix='melodee-inflate-') as temp:
            root = Path(temp)
            exe = root / 'inflate'
            flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), *flags, 'tests/boot_inflate_test.c', '-o', str(exe)], check=True)
            for raw in payloads:
                for level, strategy in [(0, 0), (9, 0), (9, zlib.Z_FIXED), (9, zlib.Z_HUFFMAN_ONLY)]:
                    compressor = zlib.compressobj(level, zlib.DEFLATED, -15, 8, strategy)
                    data = compressor.compress(raw) + compressor.flush()
                    (root / 'packed').write_bytes(data)
                    (root / 'raw').write_bytes(raw)
                    subprocess.run([str(exe), str(root / 'packed'), str(root / 'raw'), str(len(raw))], check=True)
                data = boot_audio.pack(raw)
                if data[:4] == struct.pack('<I', boot_audio.MAGIC):
                    magic, packed, size, reserved = struct.unpack('<4I', data[:16])
                    self.assertEqual((packed, size, reserved), (len(data) - 16, len(raw), 0))
                    self.assertEqual(zlib.decompress(data[16:], -15), raw)
                    # Exercise the firmware decoder against the chosen build
                    # compressor for every payload, including small streams.
                    (root / 'packed').write_bytes(data[16:])
                    (root / 'raw').write_bytes(raw)
                    subprocess.run([str(exe), str(root / 'packed'), str(root / 'raw'), str(len(raw))], check=True)
            # Check bytes actually emitted by the packager, not another compressor invocation.
            if resident.exists() and resident.stat().st_size:
                image = Path('build/melodee.bin').read_bytes()
                # ROM entry maps app-file offset zero to 0x02000120.
                offset = elf_symbol(Path('build/melodee.elf'), '_audio_load') - 0x02000120
                self.assertGreaterEqual(offset, 3808)
                if image[offset:offset+4] != struct.pack('<I', boot_audio.MAGIC):
                    # MELODEE_COMPRESS_AUDIO=0 is the raw-copy reference profile.
                    self.assertEqual(image[offset:offset+resident.stat().st_size], resident.read_bytes())
                    return
                magic, n, size, reserved = struct.unpack_from('<4I', image, offset)
                self.assertLessEqual(offset + 16 + n, len(image))
                self.assertEqual((size, reserved), (resident.stat().st_size, 0))
                data = image[offset + 16:offset + 16 + n]
                self.assertEqual(zlib.decompress(data, -15), resident.read_bytes())
                (root / 'packed').write_bytes(data)
                (root / 'raw').write_bytes(resident.read_bytes())
                subprocess.run([str(exe), str(root / 'packed'), str(root / 'raw'), str(size)], check=True)


if __name__ == '__main__':
    unittest.main()
