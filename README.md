# orrery

The whole solar system from real JPL data, drawn with
[Strata](https://github.com/Preponderous-Software/strata):

- the Sun, the 8 planets and 5 dwarf planets.
- 25 moons: every moon with a mean radius of 100 km or more, plus Phobos and Deimos.
- Saturn's main rings at their measured radii.
- 12,584 asteroids, Jupiter trojans, centaurs and trans-Neptunian objects.
- three comets: Halley, Hale-Bopp and 67P.
- Voyager 1, Voyager 2 and New Horizons.

Bodies are placed from JPL ephemerides at any instant from 1850 to 2149.

**In a browser:** <https://orrery.play.danielstephenson.dev> (1980 to 2070;
drag to orbit, pinch or scroll to zoom; **Ship** to fly, with a thumb stick and a
throttle lever on a touch screen). See [web/README.md](web/README.md).

<p align="center">
  <img src="docs/media/hero.gif" width="640" alt="The inner solar system through 2026 at true distances, planets enlarged 500 times and the Sun 20 times: the planets move along their orbits, the asteroid belt and Jupiter trojans turn, labels follow the bodies">
</p>

*2026 at **true distances**, with bodies enlarged ×500 and the Sun ×20 so they
can be seen. Each frame is 8 days. It was rendered headless by the real app:
`tools/make_media.py`.*

| Saturn, 2032 (true scale, ×1) | Jupiter and the Galilean moons (true scale, ×1) |
|---|---|
| ![Saturn at true scale with its D, C, B, A and F rings, the Cassini Division and nearby moons, 1 March 2032, with the rings sunlit](docs/media/saturn_true.png) | ![Jupiter at true scale with Io, Europa, Ganymede and Callisto on their orbits, 18 October 2026](docs/media/jupiter_true.png) |
| **Pluto and Charon** (true scale, ×1) | **Everything, COMPRESSED (log) distances: NOT TO SCALE** |
| ![Pluto and Charon at true scale, 18 October 2026](docs/media/pluto_charon_true.png) | ![The whole solar system with distances compressed logarithmically: planets, dwarf planets, comets, spacecraft, the asteroid belt, trojans and the Kuiper belt](docs/media/overview_compressed.png) |
| **Earth at 12:00 UTC, 18 October 2026** (true scale, ×1) | **Earth through 21 December 2026, an hour a frame** |
| ![Earth at true scale, lit from the left by the Sun: Africa, Europe and Arabia on the day side, the terminator over India, the axis tilted 23.4 degrees](docs/media/earth_true.png) | ![Earth turning once about its tilted axis over one day: Australia and the Pacific, then Asia, Africa and the Americas pass by](docs/media/earth_day.gif) |

## Scale: what you are looking at

The real solar system cannot be shown at one scale. At true scale, Earth is
1/23,000 of its distance from the Sun, so at any zoom that shows several
orbits every planet is far smaller than a pixel. The orrery has two modes, and
the second line of the on-screen text always says which one is showing.

- **TRUE DISTANCES** (default): every position is the real one. Body radii are multiplied by a factor *k*, shown on screen as "bodies enlarged ×*k*, Sun ×min(*k*, 20)". The Sun is capped so it does not swallow Mercury.
  - Cycle *k* through 1, 10, 100, 500 and 2,000 with `[` and `]`.
  - At *k* = 1 everything is exactly to scale, and each body stays findable as a marker a few pixels wide.
  - Moons keep their true distances. At large *k* a moon can sit inside its enlarged planet: at ×500 the Moon is inside Earth. Use ×1 or ×10 to look at moon systems.
- **COMPRESSED (LOG) DISTANCES AND SIZES: NOT TO SCALE** (key `M`): every direction is kept, but distances and sizes are compressed.
  - Distance from the Sun becomes `5 au × log10(1 + r / 0.3 au)`.
  - Radii become proportional to √radius.
  - A moon at distance *d* from a planet of radius *R* is drawn at *R′*(1 + 2 log₁₀(*d*/*R*)), where *R′* is the planet's drawn radius.
  - Order is preserved: whatever is farther is drawn farther. The mode is for seeing the Kuiper belt and Mercury in one picture, never for judging distances.

## Building and running

You need CMake 3.24 or newer, a C++17 compiler and OpenGL 4.5. Mesa's
llvmpipe is enough, so no GPU is needed. On Linux you also need the X11
development headers that SDL3 builds against (`libx11-dev libxext-dev
libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev`). CMake
fetches Strata at the commit in [`strata.ref`](strata.ref).

The JPL data is not in the repository. On a fresh clone, download it from
JPL and build the catalogue once before anything else. This needs
[uv](https://docs.astral.sh/uv/) and the network; the scripts use only the
Python standard library.

```sh
uv run --python 3.12 tools/fetch_data.py all       # NAIF kernels (32 MB, SHA-256 checked) + ~66 JPL API requests, ~3 min
uv run --python 3.12 tools/build_catalog.py        # data/catalog/ for the app, tests/data/ for the tests
```

The download is rate limited and cached in `data/raw/`, so running it again
only fetches what is missing or whose query changed (`--refresh` fetches it
all again). `data/catalog/manifest.json` records what each build was made
from: every query, when it was fetched and the SHA-256 of each response.
[DATA_SOURCES.md](DATA_SOURCES.md) explains the pipeline and credits the
sources. Then build and run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/orrery                                      # a window at the current date and time
```

```sh
./build/orrery --date 1986-02-09 --focus Halley --enlarge 1                 # Halley at perihelion
./build/orrery --focus Saturn --enlarge 1 --distance-au 0.004              # Saturn's rings up close
./build/orrery --mode compressed --distance-au 30 --pitch 70              # the Kuiper belt from above
./build/orrery --headless --date 2026-10-18 --screenshot out.png          # one frame, no window
./build/orrery --headless --frames out/ --count 120 --step 3              # frames for a video
./build/orrery --date 2026-10-18 --export-usda solar-system.usda           # an OpenUSD layer
./build/orrery --focus Earth --enlarge 1 --ship --target Mars --autopilot   # fly from Earth to Mars
```

The browser build (Emscripten, WebGL 2) is described in
[web/README.md](web/README.md).

Headless rendering uses an EGL surfaceless context, so it needs no X server.
On a machine with no GPU, set `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`.
`orrery --help` lists every option.

## Controls

| Key | Action |
|---|---|
| `Space` | play / pause |
| `,` / `.` | slower / faster, from −1 yr/s through real time to +1 yr/s (negative runs backwards) |
| `N` | now, at real time |
| `J` | jump to a date: type `YYYY-MM-DD` or `YYYY-MM-DDTHH:MM` (UTC), then `Enter` |
| `Tab` / `Shift+Tab` | focus the next / previous body (every named body in turn) |
| `0`–`9` | Sun, Mercury … Neptune, Pluto |
| `Home` | the Sun |
| left-drag | orbit the focused body |
| wheel, `PgUp` / `PgDn` | zoom |
| `F` | free flight: `W A S D` to move, `Q` / `E` down / up, drag to look; `F` again to return to orbit |
| `V` | fly the ship ([below](#the-ship)); `V` again to return to orbit around its target |
| `M` | TRUE / COMPRESSED scale |
| `[` / `]` | body enlargement in TRUE mode |
| `O` `L` `B` `X` | orbits, labels, small bodies, spin axes |
| `P` | screenshot (`orrery-NNN.png`) |
| `U` | export the current instant as `orrery.usda` |
| `H` | help |
| `Esc` | quit |

The text at the top left shows the following:

- the date and the time rate.
- the scale mode.
- facts about the focused body: radius, rotation period, axial tilt (from the IAU pole and the current orbit), distance from the Sun and from its parent, and where its position comes from.
- a warning when a body is outside its data, either extrapolated or with no trajectory at that date.

## The ship

<p align="center">
  <img src="docs/media/ship.gif" width="640" alt="The ship on autopilot from near Earth to Mars at true scale: it climbs to warp 3, is mass locked by Mars's moons and drops back to warp 1, and stops a few Mars radii out as Mars fills the view">
</p>

*On autopilot from near Earth to Mars at true scale, about 25 seconds of
flight: `--focus Earth --enlarge 1 --ship --target Mars --autopilot`.*

`V` puts you in a ship where the camera is, as in Rodina: one continuous
space from a moon's surface to the Kuiper belt, crossed by stepping the drive
up through speed tiers.

| Drive | Top speed | Earth to Mars (0.5 au) |
|---|---|---|
| thrusters | 2 km/s | 1 year |
| cruise | 200 km/s | 4 days |
| warp 1 | 1 c (299,792 km/s) | 4 minutes |
| warp 2 | 50 c | 5 seconds |
| warp 3 | 2,000 c | a moment; Neptune in 8 seconds |

- **Mass lock.** Heading toward a body, the drive covers at most 60% of the remaining distance to its surface each second. The ship slows down by itself on approach and never flies through a planet. The HUD names the body holding it back. Thrusters are never locked, so you can always set down on a surface.
- **Moving with a body.** Near a planet or moon, the ship moves with it as time runs, so a planet does not leave you behind at 30 km/s. The HUD shows which body you are moving with.
- **Size.** The ship is 1/100 of its altitude long, never under 10 m. It and its camera fit at every scale, from Phobos's surface to the outer planets. The space dust streaking past is sized the same way, so it always shows how fast you are moving relative to what is near.
- **The target** is the focused body: `Tab` and `0`–`9` choose it. Green brackets mark it, an arrow at the edge of the screen points to it when it is off screen, and the HUD gives its distance and an ETA at the current speed.
- **Autopilot** (`T`) turns the ship to the target, picks the drive and throttle, and stops a few radii out. Any stick or throttle input takes back control.
- In TRUE mode, speeds and distances are real (the drawn kilometres are kilometres). In COMPRESSED mode they are of the compressed drawing, and the HUD says so. Time keeps running at the chosen rate; `Space` pauses it while you keep flying.

| Key | Action |
|---|---|
| `W` / `S` | throttle up / down (down past zero is reverse, up to 25%) |
| wheel, `PgUp` / `PgDn` | drive tier up / down |
| left-drag, arrows, `A` / `D` | pitch and yaw |
| `Q` / `E` | roll left / right |
| `Tab`, `0`–`9`, `Home` | choose the target |
| `T` | autopilot to the target, on / off |
| `Z` | all stop |
| `C` | chase camera / cockpit |
| `V` | leave the ship |

In the browser, the toolbar's **Ship** button boards the ship. On a touch screen, a thumb stick steers, a throttle lever sets the throttle, and buttons do the rest. On a desktop they are hidden unless the **Joystick** toggle is on ([web/README.md](web/README.md)).

## What is drawn, and from what

| Class | Count | Position source |
|---|---|---|
| Sun, 8 planets, Pluto | 10 | JPL **DE440s** (read directly from the SPK file). Mars to Pluto come from their system barycentres, moved to the planet's centre by subtracting the catalogued moons' GM-weighted offsets |
| The Moon | 1 | DE440s |
| Moons with radius ≥ 100 km, plus Phobos and Deimos | 24 | JPL **Horizons** osculating elements relative to the planet's centre, every 30 days for 1980–2070, interpolated (below) |
| Ceres, Eris, Haumea, Makemake | 4 | Horizons heliocentric elements, every 30 days for 1850–2150, interpolated |
| Comets: 1P/Halley, C/1995 O1 (Hale-Bopp), 67P | 3 | Horizons heliocentric elements, every 60 days for 1850–2150, interpolated |
| Voyager 1, Voyager 2, New Horizons | 3 | Horizons state vectors every 10 days, cubic Hermite interpolation; trajectory drawn up to the current date |
| Main-belt asteroids (numbered, H < 13) | 6,061 | JPL **SBDB** elements at one epoch, propagated two-body (Kepler) |
| Jupiter trojans (numbered, H < 13) | 1,558 | SBDB, two-body |
| Near-Earth asteroids (numbered, H < 17) | 436 | SBDB, two-body |
| Centaurs (orbit condition code ≤ 5) | 671 | SBDB, two-body |
| Trans-Neptunian objects: Kuiper belt and scattered disc (condition code ≤ 5) | 3,858 | SBDB, two-body |
| Saturn's D, C, B, A and F rings; Uranus's ε ring | 6 | NSSDCA ring radii, in the equatorial plane given by the IAU pole |

Further details:

- **Radii, poles and rotation periods** are the IAU WGCCRE 2015 values from NAIF's `pck00011.tpc`. Bodies are drawn as equal-volume spheres. The radii of Eris, Haumea and Makemake come from stellar-occultation papers.
- **Surfaces and rotation:** the Sun, the eight planets and the Moon carry planet maps (Solar System Scope, CC BY 4.0; artistic composites based on NASA data, see [DATA_SOURCES.md](DATA_SOURCES.md#planet-maps)). Each map turns about the body's IAU pole at its IAU sidereal rate, so axial tilt and the time of day on Earth are real. Every other body is a plain hand-picked tint.
- **Orbits** are drawn as the current osculating ellipse of each planet, dwarf planet and comet. The focused planet's moons get their ellipses too.
- **Lighting:** the Sun is the only light, a point light at its position. Night sides are dark except for a faint ambient term.
- **Interpolating element tables:** between two samples, *a*, *e* and *i* are linear, the node and periapsis follow the shorter arc, and the mean longitude is unwrapped by the revolution count the first sample's mean motion predicts. Positions are therefore exact at every sample and follow the real mean motion in between. A single osculating set would drift by tens of degrees a month for Phobos.

Every source, licence and citation is in [DATA_SOURCES.md](DATA_SOURCES.md).

## Accuracy

`tests/test_ephemeris.cpp` compares the orrery with reference state vectors
fetched from JPL Horizons at build time. Every check has a fixed tolerance,
so it holds for any later fetch, even as JPL updates its orbit solutions.
The values below are from a build with data fetched on 2026-10-18.

| Check | Epochs | Worst error | Tolerance |
|---|---|---|---|
| Sun, Mercury, Venus, Earth, Mars–Pluto barycentres (our SPK reader on DE440s vs Horizons on DE441), SSB-relative | 1950, 2000, 2026, 2100 | < 0.001 km | 2–50 km |
| Moon, geocentric | same | 0.010 km | 1 km |
| Jupiter's centre (barycentre minus the Galilean moons' pull) | 2003, 2026, 2049 | 0.09 km | 25 km |
| Pluto's centre (barycentre minus Charon's pull) | same | 4.8 km | 25 km |
| Phobos / Triton / Charon (interpolated element tables) | same | 9.5 / 17 / 0.02 km | 15 / 30 / 15 km |
| Io / Titan / Hyperion | same | 644 / 185 / 2,936 km | 1,000 / 300 / 5,000 km |
| Ceres / Eris (heliocentric tables) | same | 1,611 / 318 km | 5,000 km |
| 1P/Halley | 1986 perihelion, 2026, 2061 perihelion | 6,558 km | 20,000 km |
| Voyager 1 (Hermite on 10-day vectors) | 1989, 2012, 2026 | 0.05 km | 50 km |
| Vesta, two-body from SBDB elements: at the epoch / +1 yr / −10 yr | 2026-06-09 ± | 0.2 / 42,246 / 1,351,693 km | 1e3 / 1e5 / 3e6 km |

For scale:

- 644 km at Io is 0.09° of its orbit.
- 6,558 km at Halley's perihelion is 0.00004 au.
- The Vesta figures show the main limit of the small-body sample. Two-body orbits from a 2026 epoch drift by about 0.0003 au a year, and by about 0.01 au after a decade. That is invisible at belt scale, but these are not precise positions far from 2026.

The other tests:

- `test_kepler` covers Kepler's equation (elliptic and hyperbolic) to 1e-12, circular and eccentric orbits (by mean and by eccentric anomaly), the state ↔ elements round trip, and table interpolation that is exact against a synthetic Phobos-like orbit.
- `test_time` covers Julian Dates, leap seconds and TDB − UTC, and ISO parsing.
- `test_scale` checks that TRUE mode leaves positions untouched and multiplies radii by exactly the stated factor, and that COMPRESSED mode keeps direction and order. It also checks unit conversions (au, obliquity).
- `test_rotation` checks the body frames built from the IAU rotation elements: each frame is right-handed and orthonormal with +Z on the pole, W = 0 puts the prime meridian on the ascending node, and Earth's pole is 23.44° from ecliptic north with its prime meridian back in place after one sidereal day.
- `test_csv` covers the catalogue's CSV reader: comment and blank lines, CRLF line ends, empty cells, the fallback for an empty number, and errors that name the file for a ragged row, a missing header, an unknown column or a cell that is not a number.
- `render_smoke` renders three views headless and checks that each frame is lit, with the expected numbers of spheres, points, orbit vertices and labels.
- CI also opens the exported USD layer with OpenUSD (`tests/check_usda.py`).
- The browser build's CI checks that the DE440s kernel cut for the web gives the same positions as the full kernel, bit for bit (`tests/check_trim_spk.py`; see [web/README.md](web/README.md)).

## Engine features this needed

Four generic features were added to Strata for this project in
[Strata #11](https://github.com/Preponderous-Software/strata/pull/11) (ABI 1.5):

- unlit **lines** (orbits).
- **points** with a pixel and/or world size. These draw 12,584 small bodies in one call, the Sun's disc and glow, and the markers that keep sub-pixel bodies visible.
- screen-space **text** in a public-domain 8×8 font (labels and HUD).
- a **space lighting** mode lit only by point lights.

The rings use Strata's static meshes (ABI 1.4). The planet maps use Strata's
textures and per-mesh model matrices
([Strata #16](https://github.com/Preponderous-Software/strata/pull/16), ABI 1.7):
each mapped body is one textured sphere mesh built once, and every frame only
its model matrix changes (pole, prime meridian, drawn radius and position
relative to the eye). The Sun's sphere is unlit. The browser build uses Strata's WebGL 2 device and
Emscripten build
([Strata #18](https://github.com/Preponderous-Software/strata/pull/18)).

**Precision** is handled in the orrery, not in Strata:

- Every position is a double in km.
- Each frame subtracts the camera position before converting to float (a floating origin), so floats only ever hold distances relative to the eye.
- The near plane follows the closest surface. Lines and points use depth clamp, so the far plane never cuts them.

## OpenUSD

`--export-usda` (or `U` in the window) writes the current instant as a USD
text layer:

- one `Sphere` per body, at true position and radius, in km in the J2000 ecliptic frame with `upAxis = "Z"`.
- one `Points` prim per small-body group.
- `customData` on each prim naming its data sources.

The layer is plain text written by the app. Strata's optional `modules/usd`
was not used because it needs a full OpenUSD build, which this program does
not otherwise need. The layer is meant for the other USD tools, which can open
it directly; CI checks it with OpenUSD (`usd-core`).

## Known gaps

- **Maps for ten bodies only.**
  - The Sun, the planets and the Moon have maps. These are artistic composites, not measured imagery (see [DATA_SOURCES.md](DATA_SOURCES.md#planet-maps)).
  - Other moons, the dwarf planets, comets and spacecraft are hand-picked tints, not measured data, and they do not visibly rotate.
  - Rotation uses the secular IAU terms only (no nutation or libration series), and gas-giant maps turn with System III rather than with their clouds.
- **Spheres only.** Bodies are equal-volume spheres. Jupiter's and Saturn's oblateness and Haumea's elongated shape are not drawn. Strata's mesh transforms could now flatten a sphere along its pole; that has not been done.
- **Missing moons.** Moons of dwarf planets other than Charon (Dysnomia, Hiʻiaka, Namaka, MK2) are not included: Horizons has no ephemeris for them. Moons smaller than 100 km (Amalthea, Janus, Larissa and many more) are left out by the size cutoff.
- **Limited time ranges.**
  - The time range is DE440s's coverage, 1849-12-26 to 2150-01-22.
  - Moon tables cover 1980–2070. Outside that span a moon is propagated two-body from the nearest sample and the HUD says "extrapolated".
  - New Horizons' trajectory file ends in 2050.
- **Geometric positions.** There is no light-time correction or aberration, so these are where bodies are, not where they appear from Earth. There are no eclipses and no shadows: Strata's point-light shadows are not used.
- **Two-body small bodies.** Small bodies are propagated two-body from a single 2026 epoch (see Accuracy).
- **UTC before 1972** uses TAI − UTC = 10 s, which is an approximation.
- **Plain labels.** Labels are skipped where they would overlap, and the font is ASCII only.
- **An arcade ship.** The ship's velocity follows its nose; there is no gravity, orbit or inertia. It stops at the surfaces of the Sun, planets, moons and dwarf planets, and flies through asteroids, comets and spacecraft. Autopilot flies straight at the target and does not steer around a body in the way: mass lock slows it, and it stops at that body's surface.
- **Depth precision.** A deep near-to-far range is unavoidable. Far spheres can fall beyond the far plane, but their point markers still show them.

## Licence

MIT; see [LICENSE](LICENSE). The data is not the code's: see
[DATA_SOURCES.md](DATA_SOURCES.md) for its terms. The planet maps in
`data/textures/` are © Solar System Scope (www.solarsystemscope.com), CC BY
4.0; `third_party/stb/stb_image.h` is public domain.
