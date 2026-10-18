# 16 patterns per track, 64 steps

The first device milestone is 16 patterns per track at the existing 64-step
maximum, on `experiment/patterns-16x128`. The larger 128-step milestone remains
future work. The user chose a shared pool preserving the previous dense-project
capacity and an explicit refusal when it is exhausted.

## Capacity and editing

Four playing/editable banks and four switch staging banks stay expanded;
inactive banks use the existing lossless block pool. Admission counts every
logical bank, including active and pending outgoing banks, hidden steps and
block padding. The 1,152-chunk RAM budget preserves 32 arbitrary dense 64-step
patterns across the 64 bank slots. Empty and repeated material can occupy all
64 banks. The retained/RAM payload and links reserve 20,736 bytes regardless of
compression utilization. This storage is independent of the audio allocator.

The new saved format additionally charges every logical bank against a
16,384-byte step payload budget. Switching an admitted project therefore cannot
make it unsaveable. Copy, doubling, direct entry, recording, event edits,
quantization, deletion, capture and history restoration check capacity before
publication. Whole stopped-note transforms roll back their bounded snapshot
when the resulting representation is too large. Refused actions report
`PATTERN STORAGE FULL`; the existing data stays usable. A single-cell deletion
can grow a repeated block's representation; clearing an entire unused bank
always frees its storage.

Ordinary live note entry uses a conservative constant-time bound: if the cold
pool leaves room for all expanded/staged banks to become dense, exact scans are
unnecessary. Near exhaustion the bounded exact codec costs decide admission.
A captured note checks its final overview once before changing event ownership.
The audio render paths continue to read the four expanded active banks.

New LEN edits choose 16, 32, 48 or 64. Legacy odd/short lengths load unchanged;
an explicit length write rounds upward to that grid. Half stops at 16; doubling
retains the original material and rounds the resulting length to the grid.
Recorded note onset, release, velocity, pitch and gate semantics stay precise.
The project-wide limits remain 1,024 precise notes and 64 automation events.

## Storage and compatibility

FBKL stores all 64 banks in a compressed tail after an 11,068-byte compact
FUN23 controls/events/native prefix. It fits the existing 27,904-byte A/B saved
record, retaining all four project slots and protected flash sector tails.
Empty, dense and sparse/repeated bank modes share the payload budget.

FUN23 retains the 13,212-byte standalone extent and previous field offsets;
spare header/control bits hold the wider active bank IDs and owner counts.
FBKL omits its active step arrays and 96 unused bytes because every bank lives
in the tail. Saves and renames keep the existing verified header-last flash
transaction. Main-loop project operations borrow a 32,768-byte canvas work
union after its DMA transfer finishes; the rendering bands remain 240 × 64.

FBKK/FUN22 and earlier supported inputs remain readable. Each old track's
banks map to its first eight new banks; banks 9–16 start empty. Old recording
owners map explicitly rather than changing the meaning of a saved track ID.
Saving upgrades to FBKL, which older firmware cannot read. The frozen,
constructed `tests/fixtures/patterns8.fbkk` checks this independently of the
new encoder; its provenance and generator are beside it.

The device exposes all 16 white keys while SEQ is held. The first eight keep
their existing mapping; the upper eight select 9–16. Holding/copying, key LEDs,
queued edits, SONG assignments and two-digit labels support the full range.
PATTERNS shows two rows of eight cells per track. Editor INFO advertises the
bank count; the web editor supports both old eight-bank and new sixteen-bank
firmware.

## Target RAM and evidence

The completed RAM optimization pass at `28882c3` had 126,336 bytes of guaranteed
resources across four independent SRAM regions. The completed 16-bank target
measures:

| Region | Eight-bank optimized baseline | Sixteen-bank target |
| --- | ---: | ---: |
| Main audio arena | 114,560 B | 108,768 B |
| Data SRAM tail | 10,392 B | 9,192 B |
| Low SRAM tail | 344 B | 344 B |
| Retained SRAM tail | 1,040 B | 1,040 B |
| Guaranteed resources, separate regions | 126,336 B | 119,344 B |

The unchanged main-arena minimum is 49,152 bytes. Optional cache RAM is 28,672
bytes and is excluded from these guarantees. The original RAM-work checkpoint
had 65,680 bytes of guaranteed resources, so the feature retains 53,664 bytes
of the optimization gains. The reserved cold step pool is already included.
These figures use all 19 nightly workflow options and the linker boundaries;
final build details are in `build/patterns-16x64/` and
`build/ram-implementation/patterns16-complete/`.

Validation includes complete save/load/rename/backup/track removal, all 64
owners with 1,024 precise events, frozen old-format import, all 64 repeated
banks, the exact former dense capacity, full-store refusal, high bank keys and
LEDs, queued bit 15, recording/overdub, history, band-render pixel parity,
layout lint, web protocol, target instruction budgets and sanitizers. The
16 × 128 core fixtures remain useful experimental checks, but do not establish
128-step UI, storage or device support. Host and static target results do not
establish physical audio/MIDI/control timing; no device flash is part of this
milestone.

The full host suite ends with `ALL HOST TESTS PASSED in 179 s` in
`build/patterns-16x64/host-verified.log`; the final target and requested checks
pass in `target-complete.log`. Serial ASan/UBSan passes the four format/UI/note/
recording fixtures in `sanitizers-complete.log`. The separate profiling build
passes with 93,696 bytes of guaranteed resources and a full runtime guard.
Build source hashes include the pre-existing local MIDI changes, which remain
outside the feature commits.
