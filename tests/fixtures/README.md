# Pattern compatibility fixture

`patterns8.fbkk` is a constructed 27,904-byte FBKK project generated with the
8-bank writer at `28882c3`, before enabling the new 16-bank default. It contains
no user data. `patterns8_generate.c` documents the exact inputs and explicitly
selects NPAT=8; the frozen file must not be regenerated to accommodate a decoder
change. Its FNV-1a over the complete file is `dae5ca39`.

Every track/bank has a four-note recorded overview at step 3. All 1,024 precise
notes cover the old 32 owners, with fractional onsets, exponent-3 durations,
drum gate bits and signed pitch offsets. `patterns16_test.c` checks the frozen
hash, maps each old owner to track × 16 + old bank, verifies empty new banks,
and saves/reloads the upgraded format.
