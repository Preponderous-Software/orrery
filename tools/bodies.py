"""What the orrery shows, and where each body's data comes from.

This file is the single list of bodies. tools/fetch_data.py reads it to know
what to download; tools/build_catalog.py reads it to write data/catalog/.

Colours are hand-picked approximations of each body's overall tint, in sRGB
0..1. They are not measured data, and the README says so.
"""

import re


def slug(name: str) -> str:
    """File-system name for a body: 'C/1995 O1 (Hale-Bopp)' -> 'c_1995_o1_hale_bopp'."""
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


# Time span of the element tables (TDB calendar dates, Horizons syntax).
MOON_SPAN = ("1980-01-01", "2070-01-01", "30 d")
HELIO_SPAN = ("1850-01-01", "2150-01-01", "30 d")
COMET_SPAN = ("1850-01-01", "2150-01-01", "60 d")

# The Sun, the planets and Pluto. Positions come from the DE440s kernel
# (NAIF ids are SPK targets); physical data from pck00011.tpc unless noted.
# "spk" is the SPK target whose position is used for the body before the
# barycentre correction from its moons (tools/build_catalog.py).
MAJOR_BODIES = [
    {"name": "Sun", "naif": 10, "spk": 10, "kind": "star", "color": [1.00, 0.85, 0.45]},
    {"name": "Mercury", "naif": 199, "spk": 199, "kind": "planet", "color": [0.55, 0.53, 0.50]},
    {"name": "Venus", "naif": 299, "spk": 299, "kind": "planet", "color": [0.90, 0.80, 0.60]},
    {"name": "Earth", "naif": 399, "spk": 399, "kind": "planet", "color": [0.25, 0.45, 0.80]},
    {"name": "Mars", "naif": 499, "spk": 4, "kind": "planet", "color": [0.80, 0.42, 0.25]},
    {"name": "Jupiter", "naif": 599, "spk": 5, "kind": "planet", "color": [0.85, 0.72, 0.55]},
    {"name": "Saturn", "naif": 699, "spk": 6, "kind": "planet", "color": [0.90, 0.80, 0.58]},
    {"name": "Uranus", "naif": 799, "spk": 7, "kind": "planet", "color": [0.60, 0.82, 0.88]},
    {"name": "Neptune", "naif": 899, "spk": 8, "kind": "planet", "color": [0.30, 0.45, 0.90]},
    {"name": "Pluto", "naif": 999, "spk": 9, "kind": "dwarf", "color": [0.80, 0.70, 0.60]},
]

# Every natural satellite whose mean radius in pck00011.tpc is at least
# 100 km, plus Phobos and Deimos. The Moon comes from DE440s; the rest from
# Horizons osculating elements relative to the planet's centre, sampled every
# MOON_SPAN[2] and interpolated (see src/orrery/element_table.cpp).
_moon_color = [0.70, 0.68, 0.65]
MOONS = [
    {"name": "Phobos", "naif": 401, "parent": 499},
    {"name": "Deimos", "naif": 402, "parent": 499},
    {"name": "Io", "naif": 501, "parent": 599, "color": [0.90, 0.82, 0.45]},
    {"name": "Europa", "naif": 502, "parent": 599, "color": [0.85, 0.80, 0.72]},
    {"name": "Ganymede", "naif": 503, "parent": 599},
    {"name": "Callisto", "naif": 504, "parent": 599, "color": [0.50, 0.45, 0.40]},
    {"name": "Mimas", "naif": 601, "parent": 699},
    {"name": "Enceladus", "naif": 602, "parent": 699, "color": [0.92, 0.92, 0.92]},
    {"name": "Tethys", "naif": 603, "parent": 699},
    {"name": "Dione", "naif": 604, "parent": 699},
    {"name": "Rhea", "naif": 605, "parent": 699},
    {"name": "Titan", "naif": 606, "parent": 699, "color": [0.85, 0.65, 0.35]},
    {"name": "Hyperion", "naif": 607, "parent": 699},
    {"name": "Iapetus", "naif": 608, "parent": 699},
    {"name": "Phoebe", "naif": 609, "parent": 699, "color": [0.40, 0.38, 0.36]},
    {"name": "Ariel", "naif": 701, "parent": 799},
    {"name": "Umbriel", "naif": 702, "parent": 799, "color": [0.45, 0.45, 0.45]},
    {"name": "Titania", "naif": 703, "parent": 799},
    {"name": "Oberon", "naif": 704, "parent": 799},
    {"name": "Miranda", "naif": 705, "parent": 799},
    {"name": "Triton", "naif": 801, "parent": 899, "color": [0.80, 0.75, 0.72]},
    {"name": "Nereid", "naif": 802, "parent": 899},
    {"name": "Proteus", "naif": 808, "parent": 899},
    {"name": "Charon", "naif": 901, "parent": 999},
]
for _m in MOONS:
    _m.setdefault("color", _moon_color)
    _m["horizons"] = str(_m["naif"])
    _m["center"] = _m["parent"]
    _m["table"] = MOON_SPAN
    _m["kind"] = "moon"

# Dwarf planets other than Pluto: heliocentric Horizons element tables.
# Physical data: Ceres from pck00011.tpc. pck00011.tpc has no entry for the
# rest: their radii come from stellar-occultation papers (LITERATURE_RADII in
# tools/build_catalog.py) and their rotation periods from SBDB's physical
# parameters ("sbdb_phys").
HELIO_TABLE_BODIES = [
    {"name": "Ceres", "naif": 2000001, "horizons": "1;", "kind": "dwarf", "color": [0.55, 0.53, 0.50]},
    {"name": "Eris", "naif": 2136199, "horizons": "136199;", "kind": "dwarf", "color": [0.90, 0.90, 0.88],
     "sbdb_phys": "136199"},
    {"name": "Haumea", "naif": 2136108, "horizons": "136108;", "kind": "dwarf", "color": [0.88, 0.88, 0.88],
     "sbdb_phys": "136108"},
    {"name": "Makemake", "naif": 2136472, "horizons": "136472;", "kind": "dwarf", "color": [0.85, 0.70, 0.60],
     "sbdb_phys": "136472"},
]
for _b in HELIO_TABLE_BODIES:
    _b["table"] = HELIO_SPAN

COMETS = [
    {"name": "1P/Halley", "naif": 1000036, "horizons": "DES=1P;CAP;NOFRAG", "kind": "comet"},
    {"name": "C/1995 O1 (Hale-Bopp)", "naif": 1003228, "horizons": "DES=C/1995 O1;NOFRAG", "kind": "comet"},
    {"name": "67P/Churyumov-Gerasimenko", "naif": 1000012, "horizons": "DES=67P;CAP;NOFRAG", "kind": "comet"},
]
for _c in COMETS:
    _c["table"] = COMET_SPAN
    _c["color"] = [0.75, 0.90, 1.00]

# Spacecraft: heliocentric state vectors from Horizons, every 10 days from
# just after launch until the end of each trajectory's ephemeris.
SPACECRAFT = [
    {"name": "Voyager 1", "horizons": "-31", "table": ("1977-09-06", "2100-01-01", "10 d")},
    {"name": "Voyager 2", "horizons": "-32", "table": ("1977-08-21", "2100-01-01", "10 d")},
    {"name": "New Horizons", "horizons": "-98", "table": ("2006-01-20", "2100-01-01", "10 d")},
]
for _s in SPACECRAFT:
    _s["kind"] = "spacecraft"
    _s["color"] = [0.95, 0.95, 0.40]

# Small-body samples from the JPL SBDB query API. Each is a documented subset;
# the README states the cutoffs and counts.
SMALL_BODY_SAMPLES = [
    {"name": "main_belt", "label": "Main-belt asteroids (numbered, H < 13)",
     "query": {"sb-class": "MBA,IMB,OMB", "sb-ns": "n", "sb-cdata": '{"AND":["H|LT|13"]}'},
     "color": [0.62, 0.60, 0.56]},
    {"name": "trojans", "label": "Jupiter trojans (numbered, H < 13)",
     "query": {"sb-class": "TJN", "sb-ns": "n", "sb-cdata": '{"AND":["H|LT|13"]}'},
     "color": [0.55, 0.75, 0.45]},
    {"name": "near_earth", "label": "Near-Earth asteroids (numbered, H < 17)",
     "query": {"sb-group": "neo", "sb-ns": "n", "sb-cdata": '{"AND":["H|LT|17"]}'},
     "color": [0.95, 0.55, 0.45]},
    {"name": "centaurs", "label": "Centaurs (orbit condition code <= 5)",
     "query": {"sb-class": "CEN", "sb-cdata": '{"AND":["condition_code|LE|5"]}'},
     "color": [0.80, 0.60, 0.85]},
    {"name": "trans_neptunian", "label": "Trans-Neptunian objects: Kuiper belt and scattered disc (condition code <= 5)",
     "query": {"sb-class": "TNO", "sb-cdata": '{"AND":["condition_code|LE|5"]}'},
     "color": [0.55, 0.70, 0.95]},
]

# Rings: inner and outer radii in km in the planet's equatorial plane.
# Saturn: NASA NSSDCA Saturnian Rings Fact Sheet (inner/outer edges; F ring by its radius). Uranus: NASA NSSDCA
# Uranian Rings Fact Sheet (the epsilon ring). Opacity values are a rendering
# choice, not data.
RINGS = [
    {"planet": 699, "name": "D ring", "inner": 66900, "outer": 74510, "color": [0.50, 0.45, 0.40], "alpha": 0.15},
    {"planet": 699, "name": "C ring", "inner": 74658, "outer": 91975, "color": [0.62, 0.56, 0.48], "alpha": 0.35},
    {"planet": 699, "name": "B ring", "inner": 91975, "outer": 117507, "color": [0.92, 0.85, 0.70], "alpha": 0.85},
    {"planet": 699, "name": "A ring", "inner": 122340, "outer": 136780, "color": [0.82, 0.76, 0.64], "alpha": 0.6},
    {"planet": 699, "name": "F ring", "inner": 139826 - 25, "outer": 139826 + 25, "color": [0.85, 0.80, 0.70],
     "alpha": 0.6},
    {"planet": 799, "name": "epsilon ring", "inner": 51149 - 30, "outer": 51149 + 30, "color": [0.55, 0.55, 0.55],
     "alpha": 0.6},
]

# Reference state vectors for the accuracy tests (tests/test_ephemeris.cpp),
# fetched from Horizons: name -> (COMMAND, CENTER, [JD TDB epochs]).
_planet_epochs = [2433282.5, 2451545.0, 2461331.5, 2488069.5]  # 1950-01-01, J2000, 2026-10-18, 2100-01-01
_moon_epochs = [2452701.7916666665, 2461331.5, 2469800.0]       # 2003-03-03 07h, 2026-10-18, 2049-12-24 12h
TEST_VECTORS = {
    "sun": ("10", "500@0", _planet_epochs),
    "mercury": ("199", "500@0", _planet_epochs),
    "venus": ("299", "500@0", _planet_epochs),
    "earth": ("399", "500@0", _planet_epochs),
    "mars_bary": ("4", "500@0", _planet_epochs),
    "jupiter_bary": ("5", "500@0", _planet_epochs),
    "saturn_bary": ("6", "500@0", _planet_epochs),
    "uranus_bary": ("7", "500@0", _planet_epochs),
    "neptune_bary": ("8", "500@0", _planet_epochs),
    "pluto_bary": ("9", "500@0", _planet_epochs),
    "moon_geo": ("301", "500@399", _planet_epochs),
    "jupiter_center": ("599", "500@0", _moon_epochs),
    "pluto_center": ("999", "500@0", _moon_epochs),
    "phobos": ("401", "500@499", _moon_epochs),
    "io": ("501", "500@599", _moon_epochs),
    "titan": ("606", "500@699", _moon_epochs),
    "hyperion": ("607", "500@699", _moon_epochs),
    "triton": ("801", "500@899", _moon_epochs),
    "charon": ("901", "500@999", _moon_epochs),
    "ceres": ("1;", "500@10", _moon_epochs),
    "eris": ("136199;", "500@10", _moon_epochs),
    "halley": ("DES=1P;CAP;NOFRAG", "500@10", [2446470.5, 2461331.5, 2474033.5]),
    "voyager1": ("-31", "500@10", [2447763.5, 2456164.5, 2461331.5]),
    # Vesta at its SBDB osculation epoch (2461200.5), one year later and ten
    # years earlier: two-body propagation of the SBDB elements is checked
    # against Horizons' perturbed integration.
    "vesta": ("4;", "500@10", [2461200.5, 2461565.75, 2457547.0]),
}
