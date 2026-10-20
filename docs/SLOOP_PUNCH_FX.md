# SLOOP master punch FX

Melodee's master effects adapt SLOOP's GPL-3.0-only `firmware/src/punch.c`
from [isod89/sloop-fm1 at 4ec1ac8](https://github.com/isod89/sloop-fm1/blob/4ec1ac829fd2bdf2c571ea215fb3d62477d01220/firmware/src/punch.c).
Source history identifies isod89's original punch implementation in `a3f9fd0`
and its 27-effect expansion in `d30d655`. The retained band-kill SVF equations
come from Felucca DSP by Leo Kuroshita. These notices are preserved in the port.
Melodee integration, ownership, arena handling, UI and Echoverb changes are
Copyright (C) 2026 Kerem Kilic (Ellic Studio). A shared licence or API was not
used as evidence of authorship.

## Controls

Tap FX: track rack. Hold FX: silent effect keys. Double tap: lock the layer;
tap again: unlock. Last pressed key wins; its release fades dry in 64 samples.
A previously held musical key remains a musical key. Closing the layer while
an effect key is still down leaves that effect active until its key releases.
Stop, project changes and modal cancellation end a request.

| White key | Effect | Black key | Effect |
| --- | --- | --- | --- |
| F3 | LOOP 4 | F#3 | KILL LOW |
| G3 | LOOP 8 | G#3 | KILL MID |
| A3 | LOOP 16 | A#3 | KILL HIGH |
| B3 | LOOP 32 | C#4 | REWIND |
| C4 | STUTTER | D#4 | OCT UP |
| D4 | REVERSE | F#4 | FLANGER |
| E4 | STOP | G#4 | AUTO-PAN |
| F4 | HALF | A#4 | FREEZE |
| G4 | LOW | C#5 | LOOP 8T |
| A4 | HIGH | D#5 | MONO |
| B4 | PHONE | F#5 | FADE OUT |
| C5 | CRUSH | | |
| D5 | ALIAS | | |
| E5 | GATE | | |
| F5 | ECHO | | |
| G5 | WOBBLE | | |

## Audio and memory

The stereo master sum, including shared returns and master gain, enters punch
DSP before the DC blocker and limiter. Stereo USB capture receives that master;
USB stems remain individual tracks. Buffered SLOOP effects intentionally use a
mono PCM16 history, as upstream; filters/kills retain independent stereo state.
Loops/gate align to local or externally clocked transport phase, and loop
regions are protected from being overwritten while held.

The 32,768-sample history (64 KiB) is optional working memory acquired when
FX is armed, not a permanent reservation taken from voices at startup. It
captures from layer entry: hold FX briefly before reverse/rewind to give them
past audio. It is released when the layer, effect keys and wet fade have ended.
Under memory pressure, buffered effects bypass and show FX MEMORY; unbuffered
effects keep working. Allocation is attempted once per layer session, with
normal retry on reopen. Existing engine allocations are never stolen. Several
large Echoverb slots and the punch history may exceed available SRAM.

FREEZE adapts the behavior to Melodee's restored Echoverb rather than reviving
SLOOP's always-on reverb bus. It reuses the history allocation for Echoverb's
native PCM history and nested allpasses, captures the master for 8,192 samples
(186 ms), then recirculates it with unity feedback, no damping and no moving
delay taps. Its tank is independent of rack slots. It does not allocate a second
tank. Echoverb's normal rack behavior retains the original PCM history, delay
crossfades and diffusion/feedback law, at stored audition ID 8.

## Validation

`tests/punch_master_test.c` covers every effect's audio, long-held captured
loops, effect transitions, exact dry bypass, key ownership, transport phase,
memory rejection, freeze sustain and the completed stereo master/return sum.
`tests/echoverb_test.c` checks exact native PCM history, delay extremes,
decaying normal tails and insert/shared integration. UI renders cover both
fonts, crowded recording headers, lock, memory status and restored Echoverb.
Physical musical feel, output sound and stressed device deadlines require
hardware listening/measurement; builds and simulated audio alone do not prove them.
