# Bluetooth radio recovery

`NO CAL` concerns the radio, not the knob/button mapping. Old Melodee preset
storage overlaps stock VM sectors. This utility supports the device's own
saved calibration and a temporary SDK calibration helper. The latter is a
development tool; the calibration-generation/return cycle has been checked on
one FM-1. Radio interoperability using the regenerated trims remains unverified.

## Browser

Open **Settings → Bluetooth recovery** in the web editor, press **Find FM-1**,
then **Calibrate Bluetooth**. The page is laid out and run like the installer
(its styles come from `install/installer.css`): it shows what the FM-1 runs and
its radio status, asks for Radio OFF and a backup, then runs in a screen of its
own with progress, time left and an end screen per outcome. `?demo=<scenario>`
runs it against a simulated FM-1 (`ble-calibration-demo.js`) and never writes or
downloads anything. The page includes the helper and matching Melodee firmware;
no firmware files or old backup need to be selected. It checks both complete
images and their SHA-256 hashes before requesting MIDI access. This also works
from a locally opened HTML page because all code and images are bundled in one
classic script, with no module/network fetch. Older Melodee releases without
command 81 enter the same recovery flow and return to the included newer build.

The browser installs the helper, measures RF
calibration on the device, downloads the result, reinstalls Melodee and imports
it. Only one FM-1 may be connected. Existing different valid calibration cannot
be replaced. No full-flash restoration is performed.

Under **Use a saved calibration**, an optional import accepts this
FM-1's original 1 MiB full-flash backup or saved radio-calibration JSON. The
browser validates checksums before importing 100 bytes. Musical backup archives
do not contain radio calibration or pairing keys.

The helper uses the SDK defaults for missing production trim records 106/107/108
and generates the RF initialization cache (187) from hardware read-backs. This
does not reproduce a lost factory production alignment. The generic board
defaults are the SDK defaults used by the official firmware, not another
device's measured RF cache.

If returning to Melodee is interrupted, press **Resume**, or later **Find FM-1**
and **Return to Melodee**: the helper's own result, or the one this browser kept,
is saved after Melodee is back. Otherwise use **Reinstall Melodee** under *If a
recovery stopped*, then **Restore from file** with the downloaded calibration. Failed calibration still
attempts to return to Melodee and never imports an incomplete result.

## Build and command line

Build normal Melodee first, then the separate helper with the same toolchain:

```sh
PYTHON="$PWD/build/venv/bin/python" ./build.sh
build/venv/bin/python tools/build_ble_calibration.py
build/venv/bin/python tools/ble_recalibrate.py \
  --helper build/rf-calibration/melodee-ble-calibration.fwsc \
  --firmware build/melodee.fwsc --port FM-1 \
  --output /path/to/this-FM1-radio.json
```

The helper build also writes the ignored `web/ble-calibration-bundled.js` beside
`web/ble-calibration.html`. To refresh just the browser bundle without rebuilding
firmware, run `python3 web/make_ble_calibration.py build/melodee.fwsc
build/rf-calibration/melodee-ble-calibration.fwsc`. Stable site builds create this
bundle from the firmware pair carried by the reviewed shared release builder;
native firmware is not rebuilt during site publication.

Radio must be OFF. Already calibrated devices are left alone. `--measure` is
the developer check: it generates a result but keeps the existing calibration
and verifies its checksum after returning to Melodee. The output path must be
new. Development builds are not release artifacts; follow BUILDING.md for
attested release builds.

## Watchdog and diagnostics

IRQs stay disabled in the helper. Every SDK `delay_us` call goes through the
same polled delay service, feeding the watchdog in chunks of at most 1 ms.
TIMER4 crystal ticks enforce a **60-second total run cap**, including stage
boundaries; at the cap feeding stops and the approximately 8-second hardware
watchdog resets the helper. Busy loops that never call a delay still time out
through the hardware watchdog. Python and the browser allow **80 seconds** for
calibration, including watchdog expiry and USB enumeration margin.

Stock's `sdio_mac_wreg(0x308, 0x9407)` and second MAC reset run between SIFS
reset and CFR initialization. This matches Open's `bt_rf_init` startup before
its first PHY table load.

A 52-byte trace with a four-byte stable-header guard at `0x01C7C040` is excluded from startup's SRAM copy and BSS
clear, the stacks, and the boot stub's `0x01C7C000..01F` mailbox. It records the
last stage, elapsed milliseconds, delay calls, last requested delay, SDK return
address, watchdog feeds, boot count and reset reasons. It survives watchdog
resets while SRAM is retained; survival through removing power is not promised.
The guard rejects retained headers overwritten by normal firmware between
helper installs; it is not a claim of continuity across firmware changes.

| Stage | Meaning |
| --- | --- |
| 1–5 | Clock preparation, MAC reset, SIFS, CFR, PHY initialization |
| 6 | `wf_rf_init` and its ADC/RF tuning loops |
| 7–8 | Stop radio, validate result |
| 9 | Complete |
| 10 | Result invalid |
| 11 | Run reached the hard time cap |
| 12 | Finalize RF cache after the SDK's one-second deferred callback |

Use **Save recovery diagnostics** in the browser or:

```sh
build/venv/bin/python tools/ble_recalibrate.py --diagnose --port FM-1 \
  --output /path/to/radio-diagnostics.json
```

The recovery flow also tries to reopen the MIDI port and save diagnostics
before reinstalling Melodee after failure. Resolve `caller_hex` against the
matching `build/rf-calibration/helper.dis` or `helper.elf`; it points just past
the SDK call to `delay_us`. The linked image must match that run. After an
interrupted run command 82 action 1 refuses automatic repeat; action 4 explicitly
clears the run state for a deliberate retry.

USB command 82 replies `[rc, 1, state]`; actions 0/1/2/3/4 are status, run,
download checked copy, download trace, explicit retry. Actions 2 and 3 append
the editor's MSB-group encoding of 100 or 52 bytes. It accepts no host-supplied
addresses and exposes no general flash or RAM access.

## Sources and boundaries

- Helper orchestration, adapter, watchdog policy and browser/CLI tools are new
  Melodee code, Kerem Kilic (Ellic Studio), GPL-3.0-only.
- Unchanged Melodee/Felucca USB, graphics, HAL, OTA and boot-stub sources retain
  their original notices. The boot stub remains byte-identical.
- The RF implementation comes from the external JieLi AC79 SDK
  `wl_rf_common.a`, checkout `d179b4484759423312073f5fbb232501aa491047`, SHA-256
  `ec50bb31af02ee3e2db9d87726c1b18b317f2acd72f233c9ce7b79a89ea324b2`.
  The build refuses a changed library until its ABI and path are reviewed.
  The SDK's Apache-2.0 notice remains applicable; its archive is not vendored.
- The official FM1 Open SDK archive was inspected to confirm register sequencing;
  its proprietary binary was not copied or linked into this helper.
- Floating-point support comes from JieLi's `r3-large/libcompiler_rt.a`;
  retain `LICENSES/compiler-rt.txt` (LLVM compiler-rt legacy MIT/NCSA notices).

The build rejects vendor VM/flash, RTOS task and interrupt-registration symbols,
checks every linked function is in SRAM, and checks the retained trace address.
The SDK storage hooks only read/write an in-RAM result. OTA can write only its
existing `0xE0000..0xE4FFF` staging region; NOR programs are split into pages.
The normal Melodee update loader performs application replacement. Calibration
does not write VM, presets, projects, the bootloader or settings. Only the final
checked import into normal Melodee writes its A/B settings.

The SDK `tx_rx_bw_set_save` timer hook is retained as a single bounded callback.
After `wf_rf_init`, the helper waits the SDK-requested interval with watchdog
service and invokes `tx_rx_bw_set` to calculate the inner checksum and copy the
68-byte cache into RAM. Missing, duplicate or oversized timer requests fail
validation. No RTOS timer or background temperature task is started.

## Validation

The hardware measurement completed in 1,588 ms, generated a checksum-valid
100-byte copy, and returned through the helper's updater to Melodee. The
guarded trace recorded stage 9, 2,338 watchdog feeds and no retained resets.
The original saved calibration remained selected; all musical archive sizes and
checksums matched before and after. This establishes generation and recovery,
not RF range, timing or MIDI interoperability with the new calibration.

Host checks cover the actual SDK delay bridge, bounded deferred callback,
60-second watchdog deadline across timer wrap, a late deadline checkpoint,
retained-header corruption, extended host wait, trace decoding, editor packing,
protected import, browser page handlers and return after failures. The full host
suite and focused calibration cases pass. Local `build/rf-calibration/validation.json`
records exact device-tested package hashes and subsequent source checks.
