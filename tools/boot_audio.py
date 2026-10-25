#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Pack resident instructions for boot and verify their decoder's XIP closure."""
import re
import struct
import zlib

MAGIC = 0x31465A41


def pack(raw):
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    data = compressor.compress(raw) + compressor.flush()
    if zlib.decompress(data, -15) != raw:
        raise ValueError('resident audio compression round trip failed')
    if len(data) + 16 < len(raw):
        return struct.pack('<4I', MAGIC, len(data), len(raw), 0) + data
    # A raw image must never be mistaken for a compressed header.
    if raw[:4] == struct.pack('<I', MAGIC):
        raise ValueError('raw audio image collides with compression header')
    return raw


def check_decoder(syms, dis, xip_start, xip_end):
    addresses = {}
    for line in syms.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch('[0-9a-f]+', fields[0]):
            addresses[fields[-1]] = int(fields[0], 16)
    functions = {}
    current = None
    for line in dis.splitlines():
        label = re.fullmatch(r'([\w.]+):', line)
        if label:
            current = label[1]
            functions[current] = []
        elif current:
            functions[current].append(line)
    errors, seen, pending = [], set(), ['boot_inflate']
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        if name not in functions or not xip_start <= addresses.get(name, 0) < xip_end:
            errors.append(f'boot decoder dependency {name} must execute from XIP')
            continue
        for line in functions[name]:
            call = re.search(r'\bcall\b', line)
            target = re.search(r'(?:\bcall|\bgoto) -?\d+ <([\w.]+)(?:\+[^ :]+)? :', line)
            if call and not target:
                errors.append(f'boot decoder {name} has an unresolved indirect call: {line.strip()}')
            if target and target[1] != name:
                pending.append(target[1])
    # Constant tables must also be available before executable SRAM is copied.
    for name, address in addresses.items():
        if name.startswith('boot_inflate.') and not xip_start <= address < xip_end:
            errors.append(f'boot decoder table {name} must be in XIP')
    return errors
