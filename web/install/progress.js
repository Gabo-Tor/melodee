/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Charles Vestal
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Ported from keremimo/saadet web/install/flow.js and fm1pkg.js at f358c01. */

function ufwEntries(image) {
  const dec = (off, n) => {
    const b = image.slice(off, off + n);
    for (let i = 0, k = 0xFFFF; i < n; i++) { b[i] ^= k & 0xFF; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; }
    return b;
  };
  const rd16 = (b, o) => b[o] | (b[o + 1] << 8);
  const rd32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  const out = {};
  const n = Math.min(rd16(dec(0, 0x40), 8), 11);
  for (let i = 0; i < n; i++) {
    const e = dec(0x40 + i * 0x50, 0x50);
    out[rd16(e, 0)] = { off: rd32(e, 8), len: rd32(e, 12) };
  }
  return out;
}

// the bytes the device reads, for the bar. Step 1 (the running firmware): the UFW header, the loader's head twice,
// its body. Step 2 (the loader): the header, the chip-key check, the whole flash.bin (checked before anything is
// erased), then the app area again as it writes it. A resume has step 2 only. Without a readable flash.bin entry
// the whole image stands for it.
export function progressModel(image, resume = false) {
  let ent = {};
  try { ent = ufwEntries(image); } catch (_) { /* not a UFW image */ }
  const fl = ent[0], ldr = ent[100];
  const flen = fl && fl.len > 0x4000 && fl.len <= 0x100000 ? fl.len : image.length;
  const check = resume ? 0 : 0x400 + 0x400 + (ldr && ldr.len > 0x20 && ldr.len < image.length ? ldr.len - 0x20 : 0);
  const write = 0x400 + 0x20 + flen + Math.max(0, flen - 0x4000);
  const total = check + write;
  return {
    check, write, total,
    verify: (b) => (Math.min(b, check) / total) * 100,
    writing: (b) => Math.min(99, ((check + Math.min(b, write)) / total) * 100),
  };
}

// minutes and seconds left at the rate so far, or null while it is too early to say
export function remaining(done, total, ms) {
  if (done < total * 0.04 || ms < 3000) return null;
  return Math.max(0, Math.round(((total - done) / done) * ms / 1000));
}

// what a failure leaves: "safe" (nothing written), "paused" (in update mode: resume), "restart" (written; the FM-1
// did not come back as expected), "partial" (the firmware installed, m-bootloader not)
export function afterFailure(code, phase) {
  if (phase === "boot") return code === "noreturn" ? "restart" : "partial";
  if (phase === "done") return "restart";
  if (phase === "staged") return code === "noreturn" || code === "mismatch" ? "restart" : "paused";
  return "safe";
}
