# Mission-records fixtures

Records files (`records.txt`, the best time per mission) written by released builds of the game,
one directory entry per file, named `v<format>-<what>.txt`. `tests/test_records.cpp` loads each one
by name. A fixture is never edited: it stands for a records file a player already has. A new
records format (`kRecordsFormatVersion` in `src/orrery/missions.hpp`) gets a new `v<N>` fixture
written by the build that introduces it, and the older fixtures must keep loading.

`.gitattributes` keeps these files byte for byte (`-text`), so no checkout changes their line
endings.

## Format 1

Written on 2026-10-24 by orrery **0.6.0** (tag `0.6.0`, commit
`44e0297320172d7e89db2a124a75e8c7a7b6d5a4`), playing in a window, not headless: headless runs and
harness-driven runs never read or write records. The game was built in an Ubuntu 24.04 environment
(GCC 13.3, CMake 3.28; Strata at `c2d7ee4c1102726c654c90d8361383d9ba3590b3`, the commit
`strata.ref` pins) after `tools/fetch_data.py all` and `tools/build_catalog.py`. It ran on an Xvfb
display with Mesa llvmpipe, under these settings: `SDL_VIDEO_FORCE_EGL=1 LIBGL_ALWAYS_SOFTWARE=1
GALLIUM_DRIVER=llvmpipe`. Each run was stopped by `timeout` after its mission had completed and
the game had written the file.

| File | How it was written | What the game printed | SHA-256 |
|---|---|---|---|
| `v1-moon-run.txt` | `ORRERY_RECORDS=v1-moon-run.txt orrery --data data --mission moon-run --land --no-small-bodies --width 320 --height 180` | `mission moon-run complete in 0:25.1 (25.06 s), a new best` | `5b7d1841065d069f2e134d33da7236ff2845ea26635179093ca70b7ead878ddd` |
| `v1-two-missions.txt` | `v1-moon-run.txt` copied, then `ORRERY_RECORDS=v1-two-missions.txt orrery --data data --mission hot-entry --land --no-small-bodies --width 320 --height 180` (the game loaded the Moon run's record and saved both) | `mission hot-entry complete in 0:32.5 (32.54 s), a new best` | `c6b7daae9621ab39d0aebfe517a704df04c5f2d1f2255c1b3df0b07693ab2750` |
