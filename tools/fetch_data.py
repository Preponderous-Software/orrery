#!/usr/bin/env python3
"""Fetch every input the orrery is built from, and cache the raw responses.

    uv run --python 3.12 tools/fetch_data.py kernels    # NAIF kernels, checked against data/kernels.sha256
    uv run --python 3.12 tools/fetch_data.py horizons   # JPL Horizons element tables, vectors and test vectors
    uv run --python 3.12 tools/fetch_data.py sbdb       # JPL SBDB small-body orbital elements
    uv run --python 3.12 tools/fetch_data.py all
    (add --refresh to re-download responses that are already cached)

Raw API responses are stored gzipped under data/raw/ and committed, so the
catalogue (tools/build_catalog.py) can be rebuilt byte for byte without the
network. Only the DE440s kernel (32 MB) is not committed: it is fetched by URL
and must match the SHA-256 pinned in data/kernels.sha256.

Python standard library only.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "data" / "raw"
KERNELS = ROOT / "data" / "kernels"
NAIF = "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/"
HORIZONS = "https://ssd.jpl.nasa.gov/api/horizons.api"
SBDB_QUERY = "https://ssd-api.jpl.nasa.gov/sbdb_query.api"
SBDB = "https://ssd-api.jpl.nasa.gov/sbdb.api"
# Cached raw responses are kept unless --refresh is given.
REFRESH = "--refresh" in sys.argv
USER_AGENT = "orrery-data-fetch/1.0 (+https://github.com/Preponderous-Software/orrery)"

sys.path.insert(0, str(ROOT / "tools"))
from bodies import (  # noqa: E402
    slug,
    COMETS,
    HELIO_TABLE_BODIES,
    MOONS,
    SMALL_BODY_SAMPLES,
    SPACECRAFT,
    TEST_VECTORS,
)


def get(url: str, params: dict | None = None, retries: int = 4) -> bytes:
    if params:
        url = url + "?" + urllib.parse.urlencode(params)
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=300) as r:
                return r.read()
        except Exception as e:  # network hiccup: back off and retry
            if attempt == retries - 1:
                raise
            print(f"  retry after {e}", file=sys.stderr)
            time.sleep(5 * (attempt + 1))
    raise AssertionError("unreachable")


def save_raw(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(payload, indent=1, sort_keys=True).encode()
    # mtime=0 keeps the gzip bytes reproducible.
    with open(path, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", mtime=0, filename="") as g:
        g.write(data)


# ------------------------------------------------------------------ kernels

def fetch_kernels() -> None:
    pins = {}
    for line in (ROOT / "data" / "kernels.sha256").read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            digest, rel = line.split()
            pins[rel] = digest
    KERNELS.mkdir(parents=True, exist_ok=True)
    for rel, digest in pins.items():
        dest = KERNELS / Path(rel).name
        if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() == digest:
            print(f"ok    {dest.relative_to(ROOT)}")
            continue
        print(f"fetch {NAIF}{rel}")
        data = get(NAIF + rel)
        actual = hashlib.sha256(data).hexdigest()
        if actual != digest:
            raise SystemExit(f"{rel}: SHA-256 {actual} does not match the pinned {digest}")
        dest.write_bytes(data)
        print(f"ok    {dest.relative_to(ROOT)} ({len(data)} bytes)")


# ----------------------------------------------------------------- horizons

def horizons(params: dict) -> dict:
    base = {"format": "json", "MAKE_EPHEM": "YES", "OBJ_DATA": "YES", "CSV_FORMAT": "YES",
            "REF_SYSTEM": "ICRF", "REF_PLANE": "ECLIPTIC", "OUT_UNITS": "KM-S"}
    base.update(params)
    quoted = {k: (f"'{v}'" if k not in ("format",) else v) for k, v in base.items()}
    body = json.loads(get(HORIZONS, quoted))
    time.sleep(1.0)  # be gentle with the service
    return {"request": base, "response": body}


def stop_before_gap(result: str) -> str | None:
    """Horizons reports the end of a target's ephemeris as 'No ephemeris ... after A.D. 2030-JAN-01 ...'."""
    m = re.search(r"after A\.D\. (\d{4}-[A-Z]{3}-\d{2})", result)
    return m.group(1) if m else None


def fetch_element_table(name: str, command: str, center: str, start: str, stop: str, step: str) -> None:
    out = RAW / "horizons" / "elements" / f"{slug(name)}.json.gz"
    if out.exists() and not REFRESH:
        print(f"cached {out.relative_to(ROOT)}")
        return
    print(f"elements {name}: {command} @ {center}, {start}..{stop} every {step}")
    payload = horizons({"COMMAND": command, "EPHEM_TYPE": "ELEMENTS", "CENTER": center, "START_TIME": start,
                        "STOP_TIME": stop, "STEP_SIZE": step, "TP_TYPE": "ABSOLUTE"})
    if "$$SOE" not in payload["response"].get("result", ""):
        raise SystemExit(f"{name}: Horizons returned no table:\n{payload['response'].get('result', payload)[:2000]}")
    save_raw(out, payload)


def fetch_vectors(name: str, command: str, center: str, start: str, stop: str, step: str) -> None:
    out = RAW / "horizons" / "vectors" / f"{slug(name)}.json.gz"
    if out.exists() and not REFRESH:
        print(f"cached {out.relative_to(ROOT)}")
        return
    print(f"vectors {name}: {command} @ {center}, {start}..{stop} every {step}")
    params = {"COMMAND": command, "EPHEM_TYPE": "VECTORS", "CENTER": center, "START_TIME": start,
              "STOP_TIME": stop, "STEP_SIZE": step, "VEC_TABLE": "2"}
    payload = horizons(params)
    result = payload["response"].get("result", "")
    if "$$SOE" not in result:
        cut = stop_before_gap(result)
        if not cut:
            raise SystemExit(f"{name}: Horizons returned no table:\n{result[:2000]}")
        print(f"  ephemeris ends {cut}; retrying with that stop time")
        params["STOP_TIME"] = cut
        payload = horizons(params)
        if "$$SOE" not in payload["response"].get("result", ""):
            raise SystemExit(f"{name}: still no table:\n{payload['response'].get('result', '')[:2000]}")
    save_raw(out, payload)


def fetch_test_vectors() -> None:
    for name, (command, center, epochs) in TEST_VECTORS.items():
        out = RAW / "horizons" / "test_vectors" / f"{name}.json.gz"
        if out.exists() and not REFRESH:
            print(f"cached {out.relative_to(ROOT)}")
            continue
        print(f"test vectors {name}: {command} @ {center} at {len(epochs)} epochs")
        payload = horizons({"COMMAND": command, "EPHEM_TYPE": "VECTORS", "CENTER": center,
                            "TLIST": " ".join(f"{jd:.6f}" for jd in epochs), "TLIST_TYPE": "JD",
                            "TIME_TYPE": "TDB", "VEC_TABLE": "2"})
        if "$$SOE" not in payload["response"].get("result", ""):
            raise SystemExit(f"{name}: {payload['response'].get('result', '')[:2000]}")
        save_raw(out, payload)


def fetch_horizons() -> None:
    for m in MOONS:
        fetch_element_table(m["name"], m["horizons"], f"500@{m['center']}", *m["table"])
    for b in HELIO_TABLE_BODIES:
        fetch_element_table(b["name"], b["horizons"], "500@10", *b["table"])
    for c in COMETS:
        fetch_element_table(c["name"], c["horizons"], "500@10", *c["table"])
    for s in SPACECRAFT:
        fetch_vectors(s["name"], s["horizons"], "500@10", *s["table"])
    fetch_test_vectors()


# --------------------------------------------------------------------- sbdb

SBDB_FIELDS = "spkid,full_name,pdes,a,e,i,om,w,ma,n,epoch,H,class,condition_code"


def fetch_sbdb() -> None:
    for sample in SMALL_BODY_SAMPLES:
        params = {"fields": SBDB_FIELDS, "sb-kind": "a", "full-prec": "1", **sample["query"]}
        print(f"sbdb {sample['name']}: {params}")
        body = json.loads(get(SBDB_QUERY, params))
        if "data" not in body:
            raise SystemExit(f"{sample['name']}: {body}")
        print(f"  {body['count']} objects")
        save_raw(RAW / "sbdb" / f"{sample['name']}.json.gz", {"request": params, "response": body})
        time.sleep(1.0)
    # Physical parameters for the dwarf planets the IAU/NAIF PCK does not cover.
    for b in HELIO_TABLE_BODIES:
        if b.get("sbdb_phys"):
            params = {"sstr": b["sbdb_phys"], "phys-par": "1"}
            print(f"sbdb phys {b['name']}")
            body = json.loads(get(SBDB, params))
            save_raw(RAW / "sbdb" / "phys" / f"{slug(b['name'])}.json.gz", {"request": params, "response": body})
            time.sleep(1.0)


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if a != "--refresh"]
    what = args[0] if args else "all"
    if what in ("kernels", "all"):
        fetch_kernels()
    if what in ("horizons", "all"):
        fetch_horizons()
    if what in ("sbdb", "all"):
        fetch_sbdb()
    if what not in ("kernels", "horizons", "sbdb", "all"):
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
