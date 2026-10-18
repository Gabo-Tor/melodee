# 16 patterns per track, 128 steps

Continuation on `experiment/patterns-16x128`, based on nightly `71bf054`,
18 October 2026. The user chose a shared pool preserving existing dense-project
capacity, with explicit refusal when exhausted. This is an implementation in
progress: the device build still uses 8 patterns and 64 steps.

## Capacity contract

- Four expanded playing/editable patterns, with four staging patterns for
  prepared switches; inactive banks use the lossless block pool.
- The shared step budget retains at least the former 32 banks of 64 arbitrary
  dense steps. Empty and repeated blocks let material occupy more banks.
  All hidden steps count, even when beyond the current pattern length.
- Count active and pending outgoing banks as well as compressed payloads.
  Otherwise edits can appear to fit and fail only on a later bank switch.
- Reject a growing edit before changing steps, events, automation, timing,
  undo or preparation. Clearing and changes that fit remain possible.
- Pattern switching must preserve every admitted bank even at full capacity.
- New length edits will select multiples of 16 through 128. Legacy odd lengths
  need an explicit import/edit policy that preserves their existing timing.
- Existing project files, recorded onsets/releases and the four saved project
  slots must remain readable. RAM admission alone does not establish flash fit.

## Implemented foundation

`recorded_note.h` retains the ten-byte event layout. Two spare bits in the
former boolean length byte extend ownership to 64 track/bank combinations and
actual positions to 128 steps; bit zero still selects a drum gate. Duration's
three-bit exponent and the original overview-rounding/wrapping bits retain
their meanings. Old events retain their exact bytes. Extracted event types
originate in Kerem Kilic's `4a50561d` / `b565eab7` changes, verified in history.

Capture, playback, editing, undo, track removal and legacy serialization now
use accessors. The old recording serializer refuses identifiers it cannot
represent. A spare runtime flag carries the release heap's seventh overview
bit, so its 1,024-entry position array does not grow. Watch/dirty bitmaps and
the bank/length cache key cover the larger capacity.

For larger host configurations the pattern pool has 1,152 chunks. Admission
counts the exact codec cost of every logical bank, including active and
pending patterns. This preserves 32 x 64 dense steps across 64 bank slots.
Chunk payload and links together occupy 20,736 bytes. This is reserved
capacity, not spare audio RAM when compression utilization is low.

Whole-bank publication and individual-step publication reject growth before
mutation. Copy and doubling preflight this budget before touching recorded
events or automation and return a distinct storage-full result. The UI has
brief `PATTERN STORAGE FULL` action feedback. Replacements release all of the
old bank's chunks before encoding: at full capacity, freeing only the current
block fails when the new data moves earlier than the old occupied blocks.

The current device configuration retains the old dense guarantee and pool
size. The larger configuration is exercised by host core fixtures, excluding
project storage and the device UI. Direct mutations elsewhere are **not yet
all routed through admission**, so changing only the device constants is not
a supported way to enable this feature.

## Validation

- `recorded_note_test.c`: frozen legacy bytes plus mixed edits across all
  64 owners, 128 positions, eight duration exponents, and overview/gate flags.
- `recording_capacity_test.c`: actual recording sources at 16 x 128; 1,024
  releases across four tracks, all watch words, reindexing sounding events,
  bank copy, pitch-cache maintenance above step 64, cache-key collisions,
  quantized wrap at step 128, capture on track 4/bank 16, and doubling across
  step 64. A separate compile enables the capture fast path.
- `pattern_capacity_test.c`: the original dense capacity; all 64 repeated
  128-step banks; full-pool refusal including copy/double event isolation;
  block relocation without spare chunks; all four simultaneous prepared
  switches; unprepared switches; same-cost and rejected active edits; seeded
  mutations against an independent expanded bank model; free-list ownership.
- AddressSanitizer/UndefinedBehaviorSanitizer runs cover both larger core
  fixtures. These are constructed host results, not device timing evidence.

The initial current-limit firmware build used all 19 nightly workflow options.
Its linked image was 534,736 bytes, with a 56,288-byte contiguous audio arena
against the unchanged 49,152-byte minimum. Other SRAM tails total 3,856 bytes;
the optional audio cache partition is separate. Log:
`build/pattern-capacity-nightly-build.log`. These figures do not establish the
RAM fit or execution deadlines of a complete 16 x 128 device build.

The subsequent [RAM audit](RAM_AUDIT.md), including the requested global
Take Taker pause during recording, reclaims 5,536 bytes of usable SRAM,
including 4,128 bytes in the contiguous main audio arena. With the same
nightly options, that arena is now 60,416 bytes, or 11,264 bytes above the
unchanged minimum. The image is 534,692 bytes. Queue-mode capture durations
and playback deadlines share one word per event; retired voice-state tails,
preset metadata and backup transfer scratch account for the other savings.
The audit distinguishes separate SRAM tails, placement changes and potential
future savings from this measured result.
The complete host suite passed again after the RAM changes in 322 seconds,
and after the global Take Taker pause in 323 seconds, including the new
1,024-event runtime-sharing and pause/resume fixtures. Sanitizers also pass
the recording handoffs and backup operations that borrow main-loop scratch.
Logs are retained in `build/ram-audit-candidate/` and `build/take-taker-pause/`.

The complete host suite, compiled with nightly's twelve fast-path options,
finished with `ALL HOST TESTS PASSED in 322 s` (`PARALLEL=2`). The existing
audio, UI, persistence, web and target-budget suites passed alongside the new
capacity fixtures. Log: `build/pattern-capacity-host.log`. Sanitizer logs:
`build/pattern-capacity-sanitize.log` and
`build/recording-capacity-sanitize.log`.

## Remaining integration

1. Rework the growing pitch cache and step-history storage, then measure the
   complete target configuration. Expanding the existing arrays directly adds
   about 20 KiB before other UI/project staging growth, more than the current
   main arena's 11,264-byte margin after the RAM audit. Preserve the existing
   audio reserve; do not count separate SRAM tails as contiguous arena space.
   The eight sound/pattern-load undo levels also grow: their current cache-RAM
   extent assertion must continue to protect that partition. Preserve the
   SRAM fallback and handle unavailable cache RAM explicitly.
   The [deeper RAM audit](RAM_DEEP_AUDIT.md) identifies at least 10,208 bytes
   of ordinary data-region growth independently of this main-arena budget.
   Its compact skip-index and FX layouts are isolated prototypes, not applied
   changes. Rebalance data/pool placement on the complete expanded build;
   moving the preset cache into data space at today's limits competes with
   those growing arrays.
2. Widen motion positions while freezing the existing four-byte disk records.
   Carry the extra bit through copy, delete, doubling, snapshots and undo.
3. Route every active edit, recording overview/tie change, bulk transform,
   preset/pattern load, undo and import through atomic admission. A preflight
   and its publication must share one sequencer guard. Do not reject a switch
   because an earlier accepted edit was not charged.
4. Introduce a versioned compact project container. Freeze the old 8-bank,
   64-step offsets and owner numbering explicitly; never derive old on-disk
   extents from the new runtime constants. Preflight complete imports before
   publishing any bank. Retain exact recorded timing and high identifiers.
5. Establish persistent capacity and power-loss behavior. A shared project
   sector pool is a candidate for retaining four slots plus a spare save copy;
   it needs explicit bounds and fault injection. Preserve user presets,
   update staging, settings and the loader's reserved sector tails.
6. Extend pattern selection, two-digit labels, SONG, step pages, length edits,
   deferred-bank masks and editor/web capabilities together. Apply the agreed
   16-step length grid without silently retiming legacy material.
7. Enable the device constants only after full integration passes host, web,
   format compatibility, storage fault injection, UI layout and target budget
   checks. Device profiling must then measure simultaneous switches and dense
   recording with audio running; host timings are not device CPU percentages.

No commit, push, flash, or physical audition is part of this continuation yet.
