# Data sources

Every number the orrery draws comes from one of the sources below. Nothing is
estimated or invented, with three exceptions that are stated wherever they appear:

- the display colours of bodies, rings and small-body groups are hand-picked
  tints. They are not measured data.
- three dwarf-planet radii come from the published papers cited below, because
  the NAIF/IAU constants have no entry for them.
- the planet maps (see *Planet maps*) are artistic composites. Their maker
  fills unmapped areas with plausible terrain and saturates the colours, so
  they show what each body roughly looks like, not measured data.

## How the data gets here

None of the NASA/JPL data is committed to this repository. Whoever builds
the orrery downloads it from JPL, and the app's catalogue is generated from
that download:

| Step | Command | Output (all git-ignored) |
|---|---|---|
| Download | `uv run --python 3.12 tools/fetch_data.py all` | `data/kernels/` (NAIF kernels, SHA-256 checked against `data/kernels.sha256`); `data/raw/` (JPL Horizons and SBDB API responses, gzipped, plus `data/raw/manifest.json`) |
| Build the catalogue | `uv run --python 3.12 tools/build_catalog.py` | `data/catalog/` (read by the app, with `data/catalog/manifest.json`) and `tests/data/vectors/` (accuracy references) |

What is committed is the list of queries: the bodies, spans, steps and SBDB
filters in `tools/bodies.py`, and the kernel hashes in `data/kernels.sha256`.
`uv run --python 3.12 tools/fetch_data.py plan` prints every JPL request the
build makes, without touching the network.

**Being polite to JPL.** `tools/fetch_data.py` sends one request at a time, at
least two seconds apart, with a `User-Agent` naming this project. It retries
failures with exponential backoff and honours `Retry-After`. Responses are
cached in `data/raw/`, and a cached response is reused until its query
changes or `--refresh` is given, so a full download (about 66 API requests
and three kernel files) happens once per checkout. CI caches `data/raw/`
with `actions/cache`, keyed on the `plan` output. JPL is asked again only
when a query changes or the cache expires.

**Provenance.** `data/raw/manifest.json` records, for every response, the
service URL, the query parameters as sent, the UTC date and time it was
fetched, and the SHA-256 and size of the bytes JPL returned. The responses
are stored exactly as received. `tools/build_catalog.py` checks each one
against its hash before using it. It then copies the manifest, together with
the kernel hashes, to `data/catalog/manifest.json`, so every build records
exactly which data it was made from. Each element table, trajectory and test
reference also carries its fetch time in its header. CI uploads the
manifest as the `data-manifest` artifact of each run.

**Reproducibility.** The NAIF kernels are pinned by SHA-256, so the planets,
the Sun and the Moon are the same in every build. The Horizons and SBDB
answers are not pinned. JPL updates orbit solutions (SBDB most often, and
its osculation epoch moves forward), so two builds made months apart can
differ slightly in the moons, dwarf planets, comets, spacecraft and small
bodies. The small-body counts below can change too. This was a deliberate
trade-off, made so that JPL's data is not republished here (see the SSD
terms below). Two builds can be compared through their manifests. The tests
compare the orrery with Horizons within fixed tolerances, never with
exact numbers, so they hold for any later fetch.

## Ephemerides and orbits

### JPL DE440 planetary ephemeris (DE440s kernel)

- **What:** positions of the Sun, the planetary system barycentres, Mercury, Venus, Earth and the Moon, 1849-12-26 to 2150-01-22. They are read directly from the SPK file by `src/orrery/spk.cpp`, with no SPICE toolkit involved.
- **File:** `de440s.bsp` from <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/>, SHA-256 `c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2`. Like all the JPL data, it is downloaded at build time and not committed.
- **Reference:** Park, R. S., Folkner, W. M., Williams, J. G., Boggs, D. H. (2021), *The JPL Planetary and Lunar Ephemerides DE440 and DE441*, AJ 161:105, doi:10.3847/1538-3881/abd414.
- **Terms:** distributed by NASA/JPL's Navigation and Ancillary Information Facility (NAIF) under NAIF's [rules for SPICE use](https://naif.jpl.nasa.gov/naif/rules.html). Use is free, and NAIF asks that it and the data producers be acknowledged; this file is that acknowledgement.

### JPL Horizons

- **What:**
  - osculating orbital elements every 30 days for the 24 moons other than the Moon, relative to each planet's centre, 1980–2070.
  - the same every 30 days for Ceres, Eris, Haumea and Makemake (heliocentric, 1850–2150).
  - the same every 60 days for the three comets (1850–2150).
  - heliocentric state vectors every 10 days for Voyager 1 and 2 and New Horizons, from launch to the end of each trajectory file (Voyagers 2099, New Horizons 2050).
  - the reference state vectors used by the accuracy tests.
- **Frame:** ecliptic and mean equinox of J2000 (IAU 1976 obliquity), TDB, geometric (no light-time or aberration).
- **Service:** <https://ssd.jpl.nasa.gov/api/horizons.api>. Each body's source ephemeris (for example `jup365_merged`, `sat441`, `mar099`, `plu060`, `Voyager_1_ST+refit2022_m`) is recorded in the header of its catalogue file.
- **Reference:** Giorgini, J. D. et al. (1996), *JPL's On-Line Solar System Data Service*, BAAS 28(3), 1158. JPL Solar System Dynamics group, <https://ssd.jpl.nasa.gov/horizons/>.
- **Terms:** see the JPL Solar System Dynamics note below.

### JPL Small-Body Database (SBDB)

- **What:** heliocentric osculating elements (ecliptic J2000, full precision, one epoch per object) for the small-body samples below. It also supplies rotation periods for Eris, Haumea and Makemake, which SBDB credits to the Asteroid Lightcurve Database (LCDB, Warner et al. 2009).
- **Service:** <https://ssd-api.jpl.nasa.gov/sbdb_query.api> and <https://ssd-api.jpl.nasa.gov/sbdb.api>.
- **Samples** (each query is in `tools/bodies.py`; the counts are from a fetch on 2026-10-18 and change as SBDB grows):

| Group | SBDB query | Objects |
|---|---|---|
| Main-belt asteroids | classes MBA, IMB, OMB; numbered; H < 13 | 6,061 |
| Jupiter trojans | class TJN; numbered; H < 13 | 1,558 |
| Near-Earth asteroids | group NEO; numbered; H < 17 | 436 |
| Centaurs | class CEN; orbit condition code ≤ 5 | 671 |
| Trans-Neptunian objects (Kuiper belt and scattered disc) | class TNO; orbit condition code ≤ 5 | 3,858 |
| **Total** | | **12,584** |

- **Terms:** see the note below.

### JPL Solar System Dynamics terms

The SSD group's FAQ (<https://ssd.jpl.nasa.gov/faq.html>) asks people who
republish information from its site to tell the group what they use and how.
It also says that most site content is covered by JPL's copyright statement,
and that use does not imply endorsement.

This repository does not republish Horizons or SBDB output. No API
response and no table derived from one is committed.

The browser build is different. Its data file (`index.data`, built in CI by
`tools/make_web_data.py`) holds the derived tables for 1980 to 2070. It is
served publicly at <https://orrery.play.danielstephenson.dev>, so that site
does republish them, with the credit below. Each person who builds
the orrery fetches the data from JPL for their own use, with
`tools/fetch_data.py`. Earlier commits did contain the raw responses and
the derived tables (in `data/raw/`, `data/catalog/` and `tests/data/`), and
they are still in the git history.

**Credit.** The orbits, trajectories and small-body elements come from the
JPL Solar System Dynamics group's Horizons system and Small-Body Database,
and the planetary ephemeris and physical constants come from NASA/JPL's
NAIF. The orrery is not endorsed by NASA or JPL.

## Physical data

### IAU WGCCRE 2015 via NAIF `pck00011.tpc`

- **What:** radii (triaxial; the orrery draws the equal-volume sphere), pole right ascension and declination, and prime-meridian angle and rate for the Sun, the planets, the Moon, the catalogued moons, Pluto and Ceres. Only the secular terms are used; the nutation and precession series are ignored.
- **File:** <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc>, SHA-256 `3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1`.
- **Reference:** Archinal, B. A. et al. (2018), *Report of the IAU Working Group on Cartographic Coordinates and Rotational Elements: 2015*, Celest. Mech. Dyn. Astr. 130:22, doi:10.1007/s10569-017-9805-5.
- **Terms:** NAIF rules, as for DE440s.

### Gravitational parameters via NAIF `gm_de440.tpc`

- **What:** GM of the planetary systems and moons. Two things use it: the step that moves a planet from its system barycentre to its centre, and the osculating orbits drawn for DE bodies.
- **File:** <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/gm_de440.tpc>, SHA-256 `924ddf4fb9ead9fe8a1aa55780bcabde40b09d00065d58226e24b68d8092f140`.

### Dwarf-planet radii from the literature

| Body | Radius used | Source |
|---|---|---|
| Eris | 1,163 km | Sicardy, B. et al. (2011), *A Pluto-like radius and a high albedo for the dwarf planet Eris from an occultation*, Nature 478, 493. Diameter 2,326 ± 12 km. |
| Haumea | 797.6 km (equal-volume sphere) | Ortiz, J. L. et al. (2017), *The size, shape, density and ring of the dwarf planet Haumea from a stellar occultation*, Nature 550, 219. Semi-axes 1,161 × 852 × 513 km. |
| Makemake | 732.8 km (geometric mean) | Ortiz, J. L. et al. (2012), *Albedo and atmospheric constraints of dwarf planet Makemake from a stellar occultation*, Nature 491, 566. Projected 1,430 × 1,502 km. |

### Air

The ship flies through the air of Venus, Earth, Mars, Titan, Pluto and Triton (`src/orrery/atmosphere.cpp`). Each air is drawn as one exponential layer, density = surface density × exp(−altitude / scale height). It is cut off where it thins to 10⁻⁶ kg/m³: about 286 km up on Venus, 119 km on Earth, 107 km on Mars, 325 km on Titan, 80 km on Pluto and 70 km on Triton.

| Body | Surface density | Scale height | Source |
|---|---|---|---|
| Venus | 65 kg/m³ | 15.9 km | NASA NSSDCA *Venus Fact Sheet* |
| Earth | 1.217 kg/m³ | 8.5 km | NASA NSSDCA *Earth Fact Sheet* |
| Mars | 0.016 kg/m³ | 11.0 km | NASA NSSDCA *Mars Fact Sheet* (~0.016 ± 0.006 kg/m³) |
| Titan | 5.24 kg/m³ | 21 km | **Derived, not quoted.** From a surface pressure of 1.5 bar (Lindal et al. 1983, *Icarus* 53, 348), a surface temperature of 94 K (Flasar et al. 2005, *Science* 308, 975), and 94.2% N₂ and 5.65% CH₄ (mean molar mass 27.3 g/mol), by the ideal gas law. The scale height is RT/(Mg) at the surface (g = 1.352 m/s²), inside the 15–50 km range usually quoted for Titan's air. |
| Pluto | 9.1 × 10⁻⁵ kg/m³ | 17.8 km | **Derived, not quoted.** From a surface pressure of about 1 Pa and a near-surface temperature of 37 K measured by New Horizons (Gladstone et al. 2016, *Science* 351, aad8866), in nitrogen (28.0 g/mol), by the ideal gas law. The scale height is RT/(Mg) at the surface (g = 0.616 m/s² from the catalogue's GM and radius), within the 17–19 km quoted for the lowest layer. |
| Triton | 1.24 × 10⁻⁴ kg/m³ | 14.5 km | **Derived, not quoted.** From a surface pressure of 1.4 Pa measured by Voyager 2 in 1989 (McKinnon & Kirk 2014, *Encyclopedia of the Solar System*, 3rd ed.) and 38 K, the low end of the 37.9–46.1 K range (Nobis & Chevrier 2025, *Planetary Science Journal* 6, 132), in nitrogen, by the ideal gas law (g = 0.780 m/s²). |

- **Sources:** NASA NSSDCA planetary fact sheets (<https://nssdc.gsfc.nasa.gov/planetary/factsheet/>). These are NASA works and are not subject to copyright in the United States.
- **Game choices, not data:** the single layer, the cut-off, the burn speed (5 km/s at a density of 1.217 kg/m³, scaling as density^(−1/6), the shape of the Sutton–Graves heating law), the ship's ballistic coefficient (10,000 kg/m²), the heat's 3 s time constant, the wing loading (400 kg/m²) and glide ratio (5) for gliding, and the sky colours. A glide uses each body's surface gravity, GM / r², from the catalogue's NAIF GM and radius.
- **Left out:** the giant planets' air. The ship is lost at a giant's drawn radius before its air would matter.

### Ring radii

The rings are drawn as flat annuli in the planet's equatorial plane, which comes from the IAU pole.

| Ring | Inner–outer (km from the planet's centre) |
|---|---|
| Saturn D | 66,900–74,510 |
| Saturn C | 74,658–91,975 |
| Saturn B | 91,975–117,507 |
| Saturn A | 122,340–136,780 |
| Saturn F | 139,826 (drawn 50 km wide) |
| Uranus ε | 51,149 (drawn 60 km wide) |

- **Sources:** NASA NSSDCA *Saturnian Rings Fact Sheet* and *Uranian Rings Fact Sheet* (<https://nssdc.gsfc.nasa.gov/planetary/factsheet/>). These are NASA works and are not subject to copyright in the United States.
- The gap between B and A is the Cassini Division.
- Opacity and colour are display choices.

## Planet maps

The Sun, Mercury, Venus, Earth, the Moon, Mars, Jupiter, Saturn, Uranus and
Neptune are drawn with the maps in [`data/textures/`](data/textures/). Every
other body is a plain-coloured sphere. None of these maps comes from the JPL
APIs, and they are committed because they are small (1.9 MB in all).

- **Source:** Solar System Scope, *Solar Textures* (INOVE), <https://www.solarsystemscope.com/textures/>, the "2K" files downloaded 2026-10-18.
- **Licence:** [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/) (CC BY 4.0). Attribution: *Textures © Solar System Scope (www.solarsystemscope.com), CC BY 4.0.* The maps are licensed separately from the orrery's MIT code.
- **Their provenance:** by Solar System Scope's description, they are based on NASA elevation and imagery data (the Earth map largely on NASA's Blue Marble). Unmapped areas are filled with fictional terrain that matches the rest, and the colours are slightly more saturated than true.
- **Changes made here:** each file was decoded and re-encoded as a baseline JPEG at quality 85 with its metadata stripped. The Sun, Mercury, Venus, Uranus and Neptune maps were also downsampled from 2048 × 1024 to 1024 × 512 (Lanczos). No other changes were made.
- **Projection:** equirectangular, with planetocentric longitude 0 at the centre, east to the right and north at the top. The orrery maps it onto a 96 × 48 latitude-longitude sphere in the body's own frame.

| File | Body | Size | Original file |
|---|---|---|---|
| `sun.jpg` | Sun | 1024 × 512 | `2k_sun.jpg` |
| `mercury.jpg` | Mercury | 1024 × 512 | `2k_mercury.jpg` |
| `venus_atmosphere.jpg` | Venus (cloud tops) | 1024 × 512 | `2k_venus_atmosphere.jpg` |
| `earth_daymap.jpg` | Earth | 2048 × 1024 | `2k_earth_daymap.jpg` |
| `moon.jpg` | Moon | 2048 × 1024 | `2k_moon.jpg` |
| `mars.jpg` | Mars | 2048 × 1024 | `2k_mars.jpg` |
| `jupiter.jpg` | Jupiter | 2048 × 1024 | `2k_jupiter.jpg` |
| `saturn.jpg` | Saturn | 2048 × 1024 | `2k_saturn.jpg` |
| `uranus.jpg` | Uranus | 1024 × 512 | `2k_uranus.jpg` |
| `neptune.jpg` | Neptune | 1024 × 512 | `2k_neptune.jpg` |

**Orientation:** each map turns with its body. The pole and the prime meridian
come from the IAU elements in `pck00011.tpc` (above): the pole's right
ascension and declination, and W = W0 + Ẇ·d, with secular terms only. Each body
therefore shows its real axial tilt and sidereal rotation. A gas giant's map
is a cloud pattern, and its "longitude" is only the IAU System III frame,
because its clouds drift. The Sun's map is drawn unlit.

The maps are decoded by `stb_image` v2.30 (public domain), vendored in
[`third_party/stb/`](third_party/stb/README.md).

## Constants

- Astronomical unit: 149,597,870.7 km (IAU 2012 Resolution B2).
- J2000 obliquity: 84,381.448″ (IAU 1976), which matches the frame Horizons uses for its ecliptic output.
- GM of the Sun: 132,712,440,041.279419 km³/s² (DE440).
- Leap seconds: IERS Bulletin C, through the 2017-01-01 step (TAI − UTC = 37 s).
