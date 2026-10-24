# The orrery in a browser

The same app, compiled with Emscripten and drawn by Strata's WebGL 2 device
(Strata's [docs/WEB.md](https://github.com/Preponderous-Software/strata/blob/main/docs/WEB.md)).
`.github/workflows/browser.yml` builds it, smoke-tests it in headless
Chromium and deploys it to arcade as <https://orrery.play.danielstephenson.dev>,
either on a push to `main` when the repository variable `ARCADE_ENABLED` is
`true`, or whenever the workflow is run by hand.

## What differs from the desktop build

- **Dates: 1980 to 2070.** The page preloads one data file, so it gets a
  smaller copy of `data/` made by `tools/make_web_data.py`:
  - the DE440s kernel cut to the moons' element-table span (`MOON_SPAN` in `tools/bodies.py`) by `tools/trim_spk.py`. The cut keeps JPL's own Chebyshev records, so positions inside the span are bit-for-bit the same (`tests/check_trim_spk.py` checks this). That takes the kernel from 32 MB to 9.8 MB.
  - the element tables and spacecraft trajectories cut to the same span.
  - the planet maps, unchanged.

  This comes to 18 MB (about 14 MB gzipped). Arcade serves files as they are, so the first visit downloads all of it. The page's date range is the cut kernel's (about 1979-12-29 to 2070-01-03).
- **Input.** The page handles the pointer itself:
  - one finger or the mouse orbits.
  - two fingers pinch to zoom, and the wheel zooms.
  - the toolbar (`web/shell.html`) does what the keys do: pick or step through bodies, pause, slower or faster, now, a date, log scale, labels, orbits, the key list.
  - **Ship** in the toolbar boards the ship (`V`). On a touch screen its controls show over the canvas:
    - a thumb stick on the left steers (pitch and yaw) while it is held off centre, gently near the centre, and springs back when let go.
    - a throttle lever on the right runs from 25% reverse at the bottom to full at the top, with a detent at zero, and stays where it is left. Moving it takes control back from the autopilot.
    - buttons above the lever: drive tier up and down, autopilot, roll left and right (held), land on the target, stop, the cockpit view and exit.

    On a desktop these are hidden, and the keyboard and mouse fly the ship. The **Joystick** toolbar toggle shows them, for a touchscreen laptop for example; the browser remembers the choice. Dragging the canvas also steers, and in the ship the wheel steps the drive tier instead of zooming.

  - **Missions** in the toolbar opens the mission list (`I` on a keyboard): a panel at the top right, a bottom sheet on a phone. Each mission shows its brief, this browser's best time and, for a race, its par; picking one starts it in the ship. **Abandon** ends the current one. Best times are kept in `localStorage` (`orrery.records`): the page loads them into the app (`orrery_load_records`) and saves them again whenever a new best is set.

  Keys still work while the canvas has focus. `P` (screenshot), `U` (USD export) and `Esc` (quit) do nothing in a browser.
- **Text** is drawn at the screen's pixel density (`orrery_ui_scale`), a
  little smaller below 440 CSS pixels so that the HUD fits a phone. The key-hint
  line is hidden on touch screens.
- **Frames** come from the browser (`emscripten_set_main_loop_arg`); main()
  returns and the session lives on.

## Building it locally

You need emsdk 6.0.11 (Strata's version) and CMake 3.24 or newer. Fetch the
data and build the catalogue first (the main README), then:

```sh
uv run --python 3.12 tools/make_web_data.py build-web-data
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j4
cd build-web && python3 -m http.server 8000      # open http://localhost:8000/index.html
```

The build writes `index.html`, `index.js`, `index.wasm` and `index.data`. These
four files are the whole site. C++ exceptions use Strata's mode
(`STRATA_WASM_EXCEPTIONS`, `wasm` by default) for the whole module.

## Smoke test

```sh
npm install --no-save --prefix web/smoke playwright@1.61.1
npx --prefix web/smoke playwright install chromium
node web/smoke/smoke.cjs http://127.0.0.1:8000/ out/desktop
node web/smoke/smoke.cjs http://127.0.0.1:8000/ out/phone --phone     # iPhone 13 profile
```

It checks that frames keep coming and prints the frame rate. It checks that the
frame holds the solar system and is not blank. Then, with time paused, it
checks that a drag orbits, that the wheel or a two-finger pinch zooms, and that
the toolbar changes the focus. It then boards the ship from the toolbar and checks
that its controls show at once on the phone, but only after the Joystick toggle on
the desktop. It checks that the throttle lever opens the throttle and stays put, that
the stick steers and springs back, that a drive button steps the drive, that Land (aimed at Titan) starts the landing autopilot and Stop ends it, that out of any air the ship reports no heat, and that Exit
leaves the ship. Last, it loads a best time as the page would from `localStorage`, and
checks that **Missions** lists the ten missions with that best time and the race's par,
that picking the Moon run starts it in the ship at its date aimed at the Moon and closes
the list, and that **Abandon** ends it. Screenshots are written to the output directory.
Headless Chromium draws with SwiftShader, which is software WebGL. It runs at
about 7 to 9 frames a second at 1280 × 747, so its frame rate is a lower bound,
not what a GPU gives.
