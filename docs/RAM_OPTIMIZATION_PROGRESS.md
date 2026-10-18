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
| Reduce display canvas | Pending | Exact rendering with a safe worst-case path; keep shared project scratch |
| Stream project/archive staging | Pending | Preserve legacy files, four slots and interrupted-write recovery |
| Allocate USB capture by channel count | Pending | Preserve four-stem mode and ring/jitter capacity |
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
