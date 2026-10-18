# RAM optimization implementation

All changes stay on `experiment/patterns-16x128`, in separate commits.
`f1be889` checkpoints the previously tested pattern-capacity foundation, RAM
savings, global Take Taker pause and investigation. Device limits remain
8 x 64 until the complete 16 x 128 integration is validated.

| Change | State | Evidence / constraint |
| --- | --- | --- |
| Share mutually exclusive FX histories | Implemented | 1,500 B fixed state removed; 1,184 B usable SRAM gained; frozen PCM, full FX tests, Chorus cost/oracle and target budgets pass |
| Compact exact recording skip index | Implemented | 2,560 B fixed pool removed; 2,464 B usable SRAM gained after code alignment; exact arbitrary-query fallback |
| Pack precise notes without changing wire data | Implemented | Nine bytes/event; 896 B additional fixed usable RAM, 1,024 B per full history copy; frozen wire bytes pass |
| Sparse recording/manual undo | Implemented | 9,792 B less per 32-note snapshot; full-density fallback, redo and slot ownership preserved |
| Reduce display canvas | Implemented | 28,832 B usable SRAM gained; exact RGB565 bands, 1,100 image and four rolling-frame comparisons; project scratch retained |
| Stream project/archive staging | Implemented | Wire scratch 27,904 -> 4,104 B; unpublished A/B staging, exact bytes and interrupted-write recovery |
| Allocate USB capture by channel count | Implemented | Stereo keeps fixed 1,024-frame ring; extra two stem channels use 4,096 B arena only when configured; 3,968 B fixed usable gain |
| Balance data/pool placement and final budgets | Pending | Check each region; placement gains are not total RAM savings |

FX validation: `build/ram-implementation/fx/`. The actual firmware uses all
19 nightly workflow options. The Chorus cost test uses its prescribed `-O2`
configuration, and all wet-output reference hashes remain unchanged.
No push or flash is part of these commits.

Index validation: `build/ram-implementation/index/`. Target build and budgets,
640,000 randomized scanner comparisons, exhaustive byte-mask comparisons,
alternate overview queries, runtime/scheduler and 16 x 128 capacity fixtures
pass. Dense steady query cost drops from 182 to 126 host instructions; a full
1,024-event rebuild rises from 37,589 to 45,446 (21%). This is bounded edit
maintenance, not a per-sample cost. Combined usable SRAM gain so far: 3,648 B.

Sparse history validation: `build/ram-implementation/sparse/`. Every occupancy
count from 0 through 1,024 round trips with holes and original slot bytes.
Recording undo, manual tools, allocator relocation/failure, capture parity/cost
and target budgets pass. Recording undo also passes ASan/UBSan with the serial
audio path (the batched host harness has a separate existing alignment issue).
Snapshots allocate their packed size immediately; expansion failures leave a
retryable undo and unchanged musical state. Full snapshots cost no bitmap.
Extra resident code/placement costs 448 B of fixed free SRAM; the quiescent
gain with one 32-note history is therefore 9,344 B. The two history handles
remain independent so a later manual undo can precede recording undo.

Note packing validation: `build/ram-implementation/notes-final/` and
`notes-aligned/`. Hot owner/step bytes stay byte aligned, while the compiler
handles odd-stride timing fields. Every 64-owner/128-step identifier and all
duration/rounding/gate bits pass exhaustive edits; 168 FBKK/FUN22 workloads
retain the frozen pre-change wire hash. Prior ten-byte raw runtime snapshots
still import with validation before narrowing. Dense/sparse history, capture
cost and target budgets pass; serial ASan/UBSan passes. The complete host suite
passes (`notes-host.log`, 166 s). Fixed usable SRAM is now 69,776 B, a 4,096 B
gain over the checkpoint build, before any sparse-history savings.

Display validation: `build/ram-implementation/display-final/` and
`display-host.log`. Primary canvas falls from 59,520 to 30,720 B. Logical
layouts replay into bounded RGB565 bands; glyph clips, shared scope snapshots,
DMA ownership, partial wheel coverage, dialogs and FM6 diagram masks retain
their original semantics. Fourteen smaller diff-cache entries use less RAM
than the old nine, avoiding band-driven cache eviction. All 1,100 images and
four rolling frames exactly match the whole-canvas oracle, including its
layout and FM6 graph checks. The target budgets and project/resource/recording
undo checks pass. Host About rendering decodes roughly twice as many glyph
pixels; steady unchanged frames and audio rendering retain their paths.
The largest measured knob redraw sends 199,840 versus 199,296 pixels (0.27%
more); ARP redraw falls from 58,720 to 57,280 pixels. These are host/display
workload measurements, not physical input-latency measurements.
Fixed usable SRAM is now 98,608 B: 32,928 B more than the checkpoint.
The complete rendered UI also passes ASan/UBSan with serial audio enabled
(`display-sanitize.log`); band bounds and the original layout checks remain
active in both reference and candidate runs.

USB validation: `build/ram-implementation/usb-final/` and `usb-host-final.log`.
The complete host suite passes in 167 s. Stereo/four-track PCM16 and PCM24,
clock drift, packet deadlines, reset/replug, volume and muted-master routing
pass. Both mock and real-arena exhaustion preserve running stereo and allow
retry; returning to stereo or disabling capture releases the extra bank.
Stream red zones and ASan/UBSan pass. Allocation happens only during a guarded
main-loop mode change/startup, never inside sample or USB service loops.
Target audio budgets pass; this is not a device cycle measurement.
Fixed usable SRAM is now 102,576 B: 36,896 B more than the checkpoint in stereo
mode. Four-track mode reserves 4,096 B from that arena while configured.

Streaming validation: `build/ram-implementation/stream-final/`,
`stream-host.log`, `stream-sanitize.log` and `stream-profile.log`.
The full host suite passes in 180 s. The target build and budgets pass;
168 generated images match the complete RAM codec byte for byte, including
hash/dirty checks, saved-slot load/rename, legacy ten-byte raw notes and track
removal. Sequential and tail-first staging survive 223 program cutoffs and
391 selected page/header byte cutoffs, seven erase failures, stale publication
and write protection. Serial ASan/UBSan passes. Guarded profiling sessions
retain the full wire buffer and pass RAM-only LIST/GET/runtime restore with
zero erases; normal nightly builds use the smaller buffer.

Normal LIST/runtime restores and project PUT begin/data stage an unpublished
inactive flash copy. This adds flash erases/readbacks, including begin/abort
preflight; only a verified final header replaces a saved object. The current
committed project survives interruption. No per-sample work is added. The
resident build pass also handles lowered memory intrinsics and places libc
entry points at the high end of resident code so implicit compiler calls stay
in range after fixed buffers shrink.

Measured combined fixed resources are 126,016 B (main 119,808; data 5,016;
low 344; retained 848), 60,336 B above the checkpoint. This measured build
also contains concurrent uncommitted live-ARP clock changes in `seq.c`, which
this RAM work does not stage or commit; its code cost is included conservatively.
Optional cache RAM is excluded. Shipping limits remain 8 x 64; the new larger
project schema, capacity UI and complete 16 x 128 integration remain separate.
