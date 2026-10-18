# Data sources

Every number the orrery draws comes from one of the sources below. Nothing is
estimated or invented, with two exceptions that are stated wherever they appear:

- the display colours of bodies, rings and small-body groups are hand-picked
  tints. They are not measured data.
- three dwarf-planet radii come from the published papers cited below, because
  the NAIF/IAU constants have no entry for them.

There are no textures. Strata has no texture path yet, so every body is a
plain-coloured sphere.

## How the data gets here

| Step | Command | Output |
|---|---|---|
| Download | `uv run --python 3.12 tools/fetch_data.py all` | `data/kernels/` (git-ignored, SHA-256 checked against `data/kernels.sha256`); `data/raw/` (committed, gzipped API responses) |
| Build the catalogue | `uv run --python 3.12 tools/build_catalog.py` | `data/catalog/` (read by the app) and `tests/data/vectors/` (accuracy references), both committed |

CI rebuilds the catalogue from the committed raw responses and fails if the
result differs from what is committed (`git diff --exit-code`). The raw
responses were fetched on 2026-10-18. A refetch (`--refresh`) gives newer orbit
solutions, so it is a deliberate change.

## Ephemerides and orbits

### JPL DE440 planetary ephemeris (DE440s kernel)

- **What:** positions of the Sun, the planetary system barycentres, Mercury, Venus, Earth and the Moon, 1849-12-26 to 2150-01-22. They are read directly from the SPK file by `src/orrery/spk.cpp`, with no SPICE toolkit involved.
- **File:** `de440s.bsp` from <https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/>, SHA-256 `c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2`. It is not committed because it is 32 MB.
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
- **Samples** (each query is in `tools/bodies.py`; the counts were fetched on 2026-10-18):

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

The SSD group's FAQ (<https://ssd.jpl.nasa.gov/faq.html>) asks people who republish information from its site to tell the group what they use and how. It also says that most site content is covered by JPL's copyright statement, and that use does not imply endorsement. This repository republishes numerical Horizons and SBDB output: the raw responses in `data/raw/` and the derived tables in `data/catalog/` and `tests/data/`. **Open item for the maintainer:** notifying SSD as the FAQ asks has not been done yet. If SSD objects, the alternative is to stop committing `data/raw/` and have CI fetch it, at the cost of reproducibility, because SBDB solutions change over time.

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

## Constants

- Astronomical unit: 149,597,870.7 km (IAU 2012 Resolution B2).
- J2000 obliquity: 84,381.448″ (IAU 1976), which matches the frame Horizons uses for its ecliptic output.
- GM of the Sun: 132,712,440,041.279419 km³/s² (DE440).
- Leap seconds: IERS Bulletin C, through the 2017-01-01 step (TAI − UTC = 37 s).
