#!/usr/bin/env python3
"""Build data/catalog/ (read by the app) and tests/data/ from the fetched data.

    uv run --python 3.12 tools/fetch_data.py all      # first: download from JPL
    uv run --python 3.12 tools/build_catalog.py

Inputs (see DATA_SOURCES.md): the JPL API responses in data/raw/ and the two
small NAIF text kernels in data/kernels/, all fetched by tools/fetch_data.py.
Every response is checked against the SHA-256 in data/raw/manifest.json, and
the manifest is copied to data/catalog/manifest.json so the catalogue records
exactly which responses (query, fetch time, hash) it was built from. Output is
deterministic for given inputs. None of it is committed.

Python standard library only.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "data" / "raw"
OUT = ROOT / "data" / "catalog"
TESTS = ROOT / "tests" / "data"
sys.path.insert(0, str(ROOT / "tools"))
from bodies import (  # noqa: E402
    COMETS,
    HELIO_TABLE_BODIES,
    MAJOR_BODIES,
    MOONS,
    RINGS,
    SMALL_BODY_SAMPLES,
    SPACECRAFT,
    TEST_VECTORS,
    slug,
)

# Radii that pck00011.tpc does not have, from the literature (cited in DATA_SOURCES.md).
LITERATURE_RADII = {
    # Eris: 2326 +/- 12 km diameter, stellar occultation (Sicardy et al. 2011, Nature 478, 493).
    "Eris": (1163.0, "Sicardy et al. 2011 (occultation)"),
    # Haumea: triaxial 2322 x 1704 x 1026 km (Ortiz et al. 2017, Nature 550, 219); radius of the
    # equal-volume sphere = (1161 * 852 * 513)^(1/3).
    "Haumea": ((1161.0 * 852.0 * 513.0) ** (1.0 / 3.0), "Ortiz et al. 2017 (occultation); equal-volume sphere"),
    # Makemake: projected 1430 x 1502 km (Ortiz et al. 2012, Nature 491, 566); geometric-mean radius.
    "Makemake": (math.sqrt(1430.0 * 1502.0) / 2.0, "Ortiz et al. 2012 (occultation); geometric mean"),
}


def read_manifest() -> dict:
    path = RAW / "manifest.json"
    if not path.exists():
        raise SystemExit("data/raw/ has not been fetched yet. Run first:\n"
                         "    uv run --python 3.12 tools/fetch_data.py all")
    return json.loads(path.read_text())


MANIFEST = read_manifest()


def load(path: Path) -> dict:
    """A cached response as {"request": params sent, "response": parsed JSON}, checked against the manifest."""
    rel = path.relative_to(RAW).as_posix()
    entry = MANIFEST["responses"].get(rel)
    if entry is None or not path.exists():
        raise SystemExit(f"data/raw/{rel} is missing. Run: uv run --python 3.12 tools/fetch_data.py all")
    with gzip.open(path) as f:
        data = f.read()
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise SystemExit(f"data/raw/{rel} does not match its SHA-256 in data/raw/manifest.json; fetch it again "
                         "with: uv run --python 3.12 tools/fetch_data.py all --refresh")
    return {"request": entry["params"], "response": json.loads(data), "fetched": entry["fetched_utc"]}


def horizons_rows(payload: dict) -> list[list[str]]:
    result = payload["response"]["result"]
    body = result[result.index("$$SOE") + 5:result.index("$$EOE")]
    rows = []
    for line in body.strip().splitlines():
        cells = [c.strip() for c in line.split(",")]
        rows.append([c for c in cells if c != ""])
    return rows


def horizons_source(payload: dict) -> str:
    m = re.search(r"Target body name:\s*(.*?)\s*\{source:\s*([^}]*)\}", payload["response"]["result"])
    return f"{m.group(1).strip()} [{m.group(2).strip()}]" if m else "?"


def fmt(x: float | str) -> str:
    """12 significant digits: below every source's own precision for this use, and half the size of repr()."""
    return f"{float(x):.12g}"


def text(s: str) -> str:
    """Catalogue text fields never contain the delimiter."""
    return s.replace(",", ";")


# ----------------------------------------------------------------- kernels

def parse_kernel_vars(path: Path) -> dict[str, list[float]]:
    text = path.read_text()
    data = "\n".join(re.findall(r"\\begindata(.*?)(?:\\begintext|\Z)", text, re.S))
    out: dict[str, list[float]] = {}
    for m in re.finditer(r"(BODY\w+)\s*=\s*\(([^)]*)\)", data):
        out[m.group(1)] = [float(v.replace("D", "E")) for v in m.group(2).split()]
    for m in re.finditer(r"(BODY\w+)\s*=\s*([-+0-9.EeDd]+)\s*$", data, re.M):
        out.setdefault(m.group(1), [float(m.group(2).replace("D", "E"))])
    return out


def write_csv(path: Path, header: list[str], rows: list[list[str]], comment: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    for c in comment.strip().splitlines():
        lines.append("# " + c if c else "#")
    lines.append(",".join(header))
    lines += [",".join(r) for r in rows]
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    pck = parse_kernel_vars(ROOT / "data" / "kernels" / "pck00011.tpc")
    gm = parse_kernel_vars(ROOT / "data" / "kernels" / "gm_de440.tpc")

    def radius(naif: int) -> tuple[float, str]:
        r = pck.get(f"BODY{naif}_RADII")
        if not r:
            raise SystemExit(f"no radii for {naif} in pck00011.tpc")
        return (r[0] * r[1] * r[2]) ** (1.0 / 3.0), "pck00011.tpc (IAU WGCCRE 2015); equal-volume sphere"

    def rotation(naif: int) -> list[str]:
        ra = pck.get(f"BODY{naif}_POLE_RA")
        dec = pck.get(f"BODY{naif}_POLE_DEC")
        pm = pck.get(f"BODY{naif}_PM")
        if not (ra and dec and pm):
            return ["", "", "", "", "", ""]
        return [fmt(ra[0]), fmt(ra[1]), fmt(dec[0]), fmt(dec[1]), fmt(pm[0]), fmt(pm[1])]

    def gm_of(naif: int) -> str:
        v = gm.get(f"BODY{naif}_GM")
        return fmt(v[0]) if v else ""

    header = ["name", "naif", "kind", "parent", "source", "spk", "table", "radius_km", "radius_source",
              "pole_ra0", "pole_ra_rate", "pole_dec0", "pole_dec_rate", "pm_w0", "pm_rate", "gm",
              "system_gm", "r", "g", "b", "rotation_period_h", "rotation_source"]
    rows: list[list[str]] = []

    def color(c: list[float]) -> list[str]:
        return [f"{v:.3f}" for v in c]

    for b in MAJOR_BODIES:
        r, rs = radius(b["naif"])
        system_gm = gm_of(b["spk"]) if b["spk"] < 10 else ""
        rows.append([b["name"], str(b["naif"]), b["kind"], "0" if b["naif"] == 10 else "10", "spk",
                     str(b["spk"]), "", f"{r:.3f}", rs, *rotation(b["naif"]), gm_of(b["naif"]), system_gm,
                     *color(b["color"]), "", "pck00011.tpc"])
    r, rs = radius(301)
    rows.append(["Moon", "301", "moon", "399", "spk", "301", "", f"{r:.3f}", rs, *rotation(301), gm_of(301), "",
                 *color([0.62, 0.61, 0.60]), "", "pck00011.tpc"])
    for m in MOONS:
        r, rs = radius(m["naif"])
        rows.append([m["name"], str(m["naif"]), "moon", str(m["parent"]), "table", "", f"elements/{slug(m['name'])}.csv",
                     f"{r:.3f}", rs, *rotation(m["naif"]), gm_of(m["naif"]), "", *color(m["color"]), "",
                     "pck00011.tpc"])
    for b in HELIO_TABLE_BODIES:
        rot_h, rot_src = "", ""
        if b["name"] in LITERATURE_RADII:
            r, rs = LITERATURE_RADII[b["name"]]
            rot = ["", "", "", "", "", ""]
            phys = load(RAW / "sbdb" / "phys" / f"{slug(b['name'])}.json.gz")["response"]
            for p in phys.get("phys_par", []):
                if p["name"] == "rot_per":
                    rot_h, rot_src = p["value"], text("JPL SBDB: " + (p.get("ref") or ""))
        else:
            r, rs = radius(b["naif"])
            rot = rotation(b["naif"])
            rot_src = "pck00011.tpc"
        rows.append([b["name"], str(b["naif"]), b["kind"], "10", "table", "", f"elements/{slug(b['name'])}.csv",
                     f"{r:.3f}", text(rs), *rot, gm_of(b["naif"]), "", *color(b["color"]), rot_h, rot_src])
    for c in COMETS:
        rows.append([c["name"], str(c["naif"]), "comet", "10", "table", "", f"elements/{slug(c['name'])}.csv",
                     "", "", "", "", "", "", "", "", "", "", *color(c["color"]), "", ""])
    for s in SPACECRAFT:
        rows.append([s["name"], s["horizons"], "spacecraft", "10", "trajectory", "",
                     f"trajectories/{slug(s['name'])}.csv", "", "", "", "", "", "", "", "", "", "",
                     *color(s["color"]), "", ""])
    write_csv(OUT / "bodies.csv", header, rows, """Every named body the orrery draws. Generated by tools/build_catalog.py; do not edit.
source: spk = DE440s segment `spk` (barycentre for Mars..Pluto, corrected to the body centre with
        its moons' GMs when system_gm is set); table = osculating elements in `table`
        relative to `parent`; trajectory = heliocentric state vectors in `table`.
radius_km: equal-volume sphere unless radius_source says otherwise.
pole_ra0/pole_ra_rate/pole_dec0/pole_dec_rate: IAU pole, degrees and degrees per Julian century (ICRF).
pm_w0/pm_rate: prime meridian, degrees and degrees per day. Secular terms only.
gm, system_gm: km^3/s^2 from gm_de440.tpc. r,g,b: approximate display colour (sRGB), not data.""")

    # ---- element tables
    el_header = ["jd_tdb", "a_km", "e", "i_deg", "node_deg", "peri_deg", "m_deg", "n_deg_per_day"]
    for b in [*MOONS, *HELIO_TABLE_BODIES, *COMETS]:
        payload = load(RAW / "horizons" / "elements" / f"{slug(b['name'])}.json.gz")
        out_rows = []
        for r in horizons_rows(payload):
            # JDTDB, Calendar, EC, QR, IN, OM, W, Tp, N (deg/s), MA, TA, A, AD, PR
            jd, ec, qr, inc, om, w, tp, n, ma, a = (r[0], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[11])
            out_rows.append([fmt(jd), fmt(a), fmt(ec), fmt(inc), fmt(om), fmt(w), fmt(ma), fmt(float(n) * 86400.0)])
        center = payload["request"]["CENTER"]
        write_csv(OUT / "elements" / f"{slug(b['name'])}.csv", el_header, out_rows,
                  f"""Osculating elements of {horizons_source(payload)} relative to {center} (Horizons),
ecliptic and mean equinox of J2000 (IAU76 obliquity), TDB. Step {payload['request']['STEP_SIZE']}.
Fetched {payload['fetched']}; data/raw/horizons/elements/{slug(b['name'])}.json.gz via tools/build_catalog.py.""")

    # ---- spacecraft trajectories
    for s in SPACECRAFT:
        payload = load(RAW / "horizons" / "vectors" / f"{slug(s['name'])}.json.gz")
        out_rows = [[fmt(v) for v in (r[0], *r[2:8])] for r in horizons_rows(payload)]
        write_csv(OUT / "trajectories" / f"{slug(s['name'])}.csv",
                  ["jd_tdb", "x_km", "y_km", "z_km", "vx_km_s", "vy_km_s", "vz_km_s"], out_rows,
                  f"""Heliocentric state of {horizons_source(payload)} (Horizons), ecliptic J2000, TDB.
Fetched {payload['fetched']}; data/raw/horizons/vectors/{slug(s['name'])}.json.gz via tools/build_catalog.py.""")

    # ---- small bodies
    sb_rows = []
    group_rows = []
    for sample in SMALL_BODY_SAMPLES:
        resp = load(RAW / "sbdb" / f"{sample['name']}.json.gz")["response"]
        f = {k: i for i, k in enumerate(resp["fields"])}
        count = 0
        for row in resp["data"]:
            try:
                vals = [float(row[f[k]]) for k in ("a", "e", "i", "om", "w", "ma", "epoch")]
            except (TypeError, ValueError):
                continue  # incomplete orbit: skipped and not counted
            n = row[f["n"]]
            h = row[f["H"]] or ""
            name = " ".join(str(row[f["full_name"]]).split())
            sb_rows.append([sample["name"], text(name), *[fmt(v) for v in vals],
                            fmt(float(n)) if n not in (None, "") else "", h])
            count += 1
        group_rows.append([sample["name"], text(sample["label"]), str(count), *color(sample["color"])])
    write_csv(OUT / "small_bodies.csv",
              ["group", "name", "a_au", "e", "i_deg", "node_deg", "peri_deg", "m_deg", "epoch_jd_tdb",
               "n_deg_per_day", "H"], sb_rows,
              """Heliocentric osculating elements from the JPL SBDB query API (full precision),
ecliptic J2000, one epoch per object. Propagated two-body by the app.
From data/raw/sbdb/*.json.gz via tools/build_catalog.py.""")
    write_csv(OUT / "groups.csv", ["group", "label", "count", "r", "g", "b"], group_rows,
              "Small-body samples: the SBDB query in tools/bodies.py for each, and how many objects it gave.")

    # ---- rings
    write_csv(OUT / "rings.csv", ["planet", "name", "inner_km", "outer_km", "r", "g", "b", "alpha"],
              [[str(r["planet"]), text(r["name"]), str(r["inner"]), str(r["outer"]), *color(r["color"]), str(r["alpha"])]
               for r in RINGS],
              "Ring radii from the NASA NSSDCA ring fact sheets (see DATA_SOURCES.md); colour and alpha are display choices.")

    # ---- test vectors
    for name, (command, center, _epochs) in TEST_VECTORS.items():
        payload = load(RAW / "horizons" / "test_vectors" / f"{name}.json.gz")
        out_rows = [[r[0], *r[2:8]] for r in horizons_rows(payload)]  # full precision: these are the reference
        write_csv(TESTS / "vectors" / f"{name}.csv",
                  ["jd_tdb", "x_km", "y_km", "z_km", "vx_km_s", "vy_km_s", "vz_km_s"], out_rows,
                  f"""Horizons state of {horizons_source(payload)} (COMMAND {command}) relative to {center},
ecliptic J2000, TDB, fetched {payload['fetched']}. Reference values for tests/test_ephemeris.cpp.""")
    # ---- provenance: exactly which responses (and kernels) this catalogue was built from
    kernels = {}
    for line in (ROOT / "data" / "kernels.sha256").read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            digest, rel = line.split()
            kernels["https://naif.jpl.nasa.gov/pub/naif/generic_kernels/" + rel] = digest
    dates = sorted(e["fetched_utc"] for e in MANIFEST["responses"].values())
    (OUT / "manifest.json").write_text(json.dumps({
        "about": "The inputs data/catalog/ and tests/data/ were built from. Generated by tools/build_catalog.py.",
        "fetched_utc_range": [dates[0], dates[-1]],
        "naif_kernels_sha256": kernels,
        "jpl_api_responses": MANIFEST["responses"],
    }, indent=1) + "\n")
    print(f"built from {len(dates)} JPL API responses fetched {dates[0]} .. {dates[-1]} (data/catalog/manifest.json)")
    print(f"wrote {len(rows)} bodies, {len(sb_rows)} small bodies in {len(group_rows)} groups, "
          f"{len(TEST_VECTORS)} test vector files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
