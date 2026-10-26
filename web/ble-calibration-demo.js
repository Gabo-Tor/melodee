// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
//
// The recovery page's demo (?demo=<scenario>) and tests: install/fakefm1.js's simulated FM-1, which here also
// answers command 81 as Melodee does and command 82 as the helper does. Small images of its own; nothing is
// fetched or written. Scenarios:
//   nocal (default)  Melodee showing NO CAL, radio off    ok         calibrated and saved
//   radio            the radio is on                      unsaved    calibrated, not saved yet
//   legacy           a Melodee before command 81          helper     the helper is running and ready
//   result           the helper holds a result            update     waiting in update mode
//   fail             the measurement fails                interrupt  the cable goes during the return write
//   refuse           Melodee won't confirm the saved calibration

import { fakeAccess } from "./install/fakefm1.js";
import { bkPack, bkUnpack } from "./fm1backup.js";
import { crc16, HELPER_PRODUCT } from "./blecalibration.js";
import { MELODEE_LOADER } from "./fm1ota.js";

const LEGACY = "FM-1_91043";
const image = (n, seed) => { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = (i * 131 + seed * 7 + (i >> 8)) & 0xFF; return b; };

// a complete copy, as the helper builds it (blecalibration.js checkCalibration)
export function demoCalibration() {
  const b = new Uint8Array(100);
  b[0] = 0xad;
  for (let i = 1; i < 94; i++) b[i] = (i * 37 + 11) & 0xFF;
  let c = crc16(b.subarray(30, 94)); b[94] = c & 255; b[95] = c >> 8;
  c = crc16(b.subarray(0, 98)); b[98] = c & 255; b[99] = c >> 8;
  return b;
}

// -> { access, device, packages: { helper, firmware }, radio, helper }
export function demoSetup(scenario = "nocal", { product = "FM-1_91050", measureMs = 7000, stride = 2, timers } = {}) {
  const packages = {
    helper: { image: image(0x30000, 3), product: HELPER_PRODUCT },
    firmware: { image: image(0x30000, 1), product },
  };
  const copy = demoCalibration(), crc = copy[98] | copy[99] << 8;
  const radio = { calibrated: ["ok", "unsaved"].includes(scenario), on: scenario === "radio", saved: scenario === "ok", crc: scenario === "ok" ? crc : 0 };
  const helper = { state: scenario === "result" ? 2 : 0, stage: scenario === "result" ? 9 : 0, elapsed: 0 };
  const identity = { helper: HELPER_PRODUCT, result: HELPER_PRODUCT, legacy: LEGACY }[scenario] || product;
  const { access, device } = fakeAccess({ images: { firmware: packages.firmware.image }, identity, after: product,
    loader: MELODEE_LOADER, state: scenario === "update" ? "loader" : "app", stride, timers });
  let interrupt = scenario === "interrupt";

  // which package was served, known by its header; it decides what the FM-1 runs after the write
  device.which = (head) => {
    for (const pkg of [packages.helper, packages.firmware]) {
      if (!head.every((b, i) => pkg.image[i] === b)) continue;
      device.after = pkg.product;
      if (pkg === packages.helper) Object.assign(helper, { state: 0, stage: 0, elapsed: 0 });
      else if (interrupt) { interrupt = false; device.interruptAt = 0.45; }
      return { size: pkg.image.length, boot: false };
    }
    return null;
  };

  const status = (rc = 0) => [rc, 1, +radio.calibrated, +radio.on, 1, +radio.saved, radio.crc & 127, (radio.crc >> 7) & 127, radio.crc >> 14];
  const trace = () => {
    const raw = new Uint8Array(52), v = new DataView(raw.buffer);
    [0x52464342, 1, helper.stage, helper.elapsed, 41210, 1000, 0x1c0bbcc, helper.elapsed, helper.stage === 11 ? 1 : 0, 32, 4, 60000, 1]
      .forEach((x, i) => v.setUint32(i * 4, x, true));
    return raw;
  };
  const receive = device.receive.bind(device);
  device.receive = (bytes) => {
    if (!(bytes[0] === 0xF0 && bytes[1] === 0x7D && bytes[2] === 0x46 && bytes[3] === 0x4C)) return receive(bytes);
    if (!device.online || device.state !== "app") return;
    const cmd = bytes[4], args = bytes.slice(5, -1);
    const reply = (out, ms = 30) => device.clock.set(() => device.emit([0xF0, 0x7D, 0x46, 0x4C, cmd, ...out, 0xF7]), ms);
    if (cmd === 81 && device.identity !== HELPER_PRODUCT && device.identity !== LEGACY) {
      if (args[0] !== 1) return reply(status());
      const got = bkUnpack(args.slice(1), 100), gotCrc = got[98] | got[99] << 8;
      if (radio.on) return reply(status(1));
      if (radio.calibrated && radio.crc !== gotCrc) return reply(status(2));
      Object.assign(radio, { calibrated: true, saved: false, crc: gotCrc });
      if (scenario !== "refuse") device.clock.set(() => { radio.saved = true; }, 600);
      return reply(status(4));
    }
    if (cmd !== 82 || device.identity !== HELPER_PRODUCT) return;
    const action = args[0];
    if (action === 0) return reply([0, 1, helper.state]);
    if (action === 1) {
      if (helper.state) return reply([0, 1, helper.state]);        // one attempt per boot
      helper.state = 1; helper.stage = 6;
      const failed = scenario === "fail";
      return device.clock.set(() => {
        Object.assign(helper, failed ? { state: 3, stage: 11, elapsed: 60000 } : { state: 2, stage: 9, elapsed: measureMs });
        reply([0, 1, helper.state], 0);
      }, failed ? measureMs * 1.4 : measureMs);
    }
    if (action === 2) return reply(helper.state === 2 ? [0, 1, 2, ...bkPack(copy)] : [2, 1, helper.state]);
    if (action === 3) return reply([0, 1, helper.state, ...bkPack(trace())]);
    if (action === 4) { Object.assign(helper, { state: 0, stage: 0 }); return reply([0, 1, 0]); }
  };
  return { access, device, packages, radio, helper, copy };
}
