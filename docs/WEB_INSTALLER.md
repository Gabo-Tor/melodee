# Web installer

The installer ports Saadet's safeguards from `keremimo/saadet` at `f358c01`
(introduced in `036b97e`), with Melodee's release selection and update protocol.
The ported MPL-2.0 files keep their original notices; see `LICENSING.md` and
`LICENSES/MPL-2.0.txt`. New session and integration code is by Kerem Kilic
(Ellic Studio), GPL-3.0-only.

`web/index_pkg.html` presents connection, version selection and installation,
followed by the existing TransitFM1 and official V15 paths. `web/make_site.py`
inlines the libraries, styles and SHA-256 metadata into one published page.
There are no runtime dependencies on Saadet or an external JavaScript service.

- `web/install/transfer.js` answers read requests from the MIDI event with
  timestamped sends. The ten millisecond response gap does not use a page
  timer. Loader identity checks and CHECK HEAD addresses remain intact.
- `keepalive.js` holds a Web Lock during connection checks and installations,
  routes timeout work through a worker, requests a screen wake lock and warns
  before leaving. Unsupported browser APIs are skipped; lock acquisition
  errors stop the attempt. Wake locks arriving after cleanup are released.
- `session.js` gates writes behind preflight confirmation, tracks the actual
  transfer phases and distinguishes safe failures, paused writes and missing
  restarts. An interrupted session retains its selected version and commit in
  local storage; a different build cannot resume that session. The loader and
  returned firmware identities are still verified by `fm1ota.js`.
- Package hashes are generated from the exact published bytes. A hash mismatch
  leaves Install disabled. Stock V15 and TransitFM1 retain their existing
  pinned checksums, automatic backup, explicit backup opt-out and separate
  loader restrictions. M-VAVE Open firmware is rejected before upgrade starts.

Saadet's optional m-bootloader writer and crash-report protocol are not offered:
Melodee does not implement those firmware commands. Existing Melodee recovery
instructions remain linked on the page. Browser and simulated-device tests
verify software behavior; they do not establish physical USB reliability.

After building firmware, run `npm ci --prefix web` and `tests/run_tests.sh`.
The focused checks are `node web/test_install_safety.mjs`,
`node web/test_installer.mjs`, `node web/test_transit.mjs` and
`python3 tests/nightly_site_test.py`. The safety suite runs the actual published
library bundle with real DOM elements, simulated MIDI, background throttling,
locks, preflight cancellation and same-build recovery. Its `jsdom` dependency
is used only for tests and is pinned in `web/package-lock.json`.
