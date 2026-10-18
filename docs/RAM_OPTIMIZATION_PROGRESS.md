# RAM optimization implementation

All changes stay on `experiment/patterns-16x128`, in separate commits.
`f1be889` checkpoints the previously tested pattern-capacity foundation, RAM
savings, global Take Taker pause and investigation. Device limits remain
8 x 64 until the complete 16 x 128 integration is validated.

| Change | State | Evidence / constraint |
| --- | --- | --- |
| Share mutually exclusive FX histories | Implemented | 1,500 B fixed state removed; 1,184 B usable SRAM gained; frozen PCM, full FX tests, Chorus cost/oracle and target budgets pass |
| Compact exact recording skip index | Next | Keep arbitrary-query fallback and validate dense rebuild cost |
| Pack precise notes without changing wire data | Pending | Preserve every supported owner, position and duration |
| Sparse recording/manual undo | Pending | Preserve full-density fallback, redo and slot ownership |
| Reduce display canvas | Pending | Exact rendering with a safe worst-case path; keep shared project scratch |
| Stream project/archive staging | Pending | Preserve legacy files, four slots and interrupted-write recovery |
| Allocate USB capture by channel count | Pending | Preserve four-stem mode and ring/jitter capacity |
| Balance data/pool placement and final budgets | Pending | Check each region; placement gains are not total RAM savings |

FX validation: `build/ram-implementation/fx/`. The actual firmware uses all
19 nightly workflow options. The Chorus cost test uses its prescribed `-O2`
configuration, and all wet-output reference hashes remain unchanged.
No push or flash is part of these commits.
