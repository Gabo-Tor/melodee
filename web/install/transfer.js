// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Portions Copyright (C) 2026 Charles Vestal
// Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// fm1-base's Updater (fm1ota.js, unchanged beside this file) with a read loop that does not depend on the page's
// timers. Upstream answers each of the device's read requests after `await sleep(10)`. A browser throttles the timers
// of a page in the background (Chrome: once a second at once, once a minute after five minutes), so a switched tab
// turned every 10 ms gap into a second or a minute. The device waits 3 x 5 s for an answer and its first step gives
// up after 2 min: the install stopped halfway. Here an answer is sent from the MIDI message's own event, with the
// same 10 ms gap given to the browser's MIDI service as the send's timestamp; no page timer is between a request
// and its answer. response() and parseRequest() are fm1ota.js's (module-private there), kept byte for byte.

import { Updater, pack7, unpack7 } from "../fm1ota.js";

const MAXDATA = 512;
export const GAP_MS = 10;                          // upstream's pause before each answer
const FINISH = [0xE0000000, 0xF0000000];           // step 1's and step 2's "success" questions

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

// a send the MIDI service delivers `delay` ms from now; false when the port is gone
function sendLater(link, bytes, delay) {
  try { link.output.send(bytes, performance.now() + delay); return true; } catch (_) { return false; }
}

function fail(code, msg) { const e = new Error(msg); e.code = code; return e; }

export class SteadyUpdater extends Updater {
  // Updater.serve's contract: answer reads until the device asks finishAddr -> { served, finished, lost }
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, bytes = 0, last = Date.now(), lastRead = -1;
    for (;;) {
      const pkt = await link.read(1000);           // resolves from the message event; its timeout only measures idle
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true, lastRead };
        if (Date.now() - last > idleMs) return { served, finished: false, lastRead };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (FINISH.includes(r.addr)) {
        if (!sendLater(link, response(r.addr, [...new TextEncoder().encode("success"), 0]), 0))
          return { served, finished: false, lost: true, lastRead };
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      lastRead = r.addr;
      if (!sendLater(link, response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl), GAP_MS)) {
        return { served, finished: false, lost: true, lastRead };
      }
      served++;
      bytes += r.len;
      if (onProgress) onProgress(served, lastRead, bytes);
    }
  }
}
