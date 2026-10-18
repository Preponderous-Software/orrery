#!/usr/bin/env python3
"""Fetch every input the orrery is built from into a local cache.

    uv run --python 3.12 tools/fetch_data.py all        # everything below, in order
    uv run --python 3.12 tools/fetch_data.py kernels    # NAIF kernels, checked against data/kernels.sha256
    uv run --python 3.12 tools/fetch_data.py horizons   # JPL Horizons element tables, vectors and test vectors
    uv run --python 3.12 tools/fetch_data.py sbdb       # JPL SBDB small-body orbital elements
    uv run --python 3.12 tools/fetch_data.py plan       # print every JPL query as JSON; no network
    (add --refresh to download again responses that are already cached)

Nothing fetched here is committed. NASA/JPL's data is downloaded from JPL
by whoever builds the orrery, and data/kernels/ and data/raw/ are git-ignored.

data/raw/ holds each JPL API response gzipped, byte for byte as JPL sent it,
plus data/raw/manifest.json, which records for every response the service,
the query parameters, the UTC date and time it was fetched and the SHA-256
of the response bytes. tools/build_catalog.py checks every response against
that hash and copies the manifest into data/catalog/, so a build can say
exactly which data it was made from.

A cached response is reused while its query is unchanged; a changed query
(tools/bodies.py) is fetched again. The `plan` output is the query manifest:
CI keys its cache on it, so JPL is only asked again when a query changes or
the cache expires.

Politeness: one request at a time, at least REQUEST_INTERVAL seconds apart,
with a User-Agent naming this project. Failed requests are retried with
exponential backoff, and a server's Retry-After is honoured.

Python standard library only.
"""
from __future__ import annotations

import datetime as dt
import gzip
import hashlib
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "data" / "raw"
MANIFEST = RAW / "manifest.json"
KERNELS = ROOT / "data" / "kernels"
NAIF = "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/"
HORIZONS = "https://ssd.jpl.nasa.gov/api/horizons.api"
SBDB_QUERY = "https://ssd-api.jpl.nasa.gov/sbdb_query.api"
SBDB = "https://ssd-api.jpl.nasa.gov/sbdb.api"
REFRESH = "--refresh" in sys.argv
USER_AGENT = "orrery-data-fetch/2.0 (+https://github.com/Preponderous-Software/orrery)"
REQUEST_INTERVAL = 2.0  # seconds between the starts of two requests
RETRIES = 6
BACKOFF = 5.0  # seconds before the first retry; doubles each time

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

_last_request = 0.0


def get(url: str, params: dict | None = None) -> bytes:
    """One polite GET: rate limited, retried with backoff, Retry-After honoured."""
    global _last_request
    if params:
        url = url + "?" + urllib.parse.urlencode(params)
    for attempt in range(RETRIES):
        wait = _last_request + REQUEST_INTERVAL - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        _last_request = time.monotonic()
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=300) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            # 4xx other than 429 means the query is wrong: retrying will not help.
            if 400 <= e.code < 500 and e.code != 429:
                raise SystemExit(f"{url}\n  HTTP {e.code}: {e.read()[:2000]!r}")
            error, retry_after = f"HTTP {e.code}", e.headers.get("Retry-After")
        except Exception as e:  # network hiccup
            error, retry_after = str(e), None
        if attempt == RETRIES - 1:
            raise SystemExit(f"{url}\n  gave up after {RETRIES} attempts: {error}")
        delay = BACKOFF * 2 ** attempt
        if retry_after and retry_after.isdigit():
            delay = max(delay, float(retry_after))
        print(f"  {error}; retrying in {delay:.0f} s", file=sys.stderr)
        time.sleep(delay)
    raise AssertionError("unreachable")


# ----------------------------------------------------------------- manifest

def load_manifest() -> dict:
    if MANIFEST.exists():
        return json.loads(MANIFEST.read_text())
    return {"responses": {}}


def save_manifest(manifest: dict) -> None:
    manifest["about"] = ("JPL API responses cached by tools/fetch_data.py. Each entry: service URL, query "
                         "parameters as sent, UTC fetch time, SHA-256 and size of the response bytes.")
    manifest["responses"] = dict(sorted(manifest["responses"].items()))
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    tmp = MANIFEST.with_suffix(".tmp")
    tmp.write_text(json.dumps(manifest, indent=1) + "\n")
    tmp.replace(MANIFEST)


def cached(manifest: dict, rel: str, query: dict) -> bool:
    """True when rel is cached for exactly this query and its bytes still match the manifest."""
    entry = manifest["responses"].get(rel)
    path = RAW / rel
    if REFRESH or not entry or entry.get("query") != query or not path.exists():
        return False
    with gzip.open(path) as f:
        return hashlib.sha256(f.read()).hexdigest() == entry["sha256"]


def store(manifest: dict, rel: str, service: str, query: dict, params: dict, data: bytes) -> None:
    path = RAW / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    # mtime=0: the same response always gives the same file.
    with open(path, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", mtime=0, filename="") as g:
        g.write(data)
    manifest["responses"][rel] = {
        "service": service,
        "query": query,  # what tools/bodies.py asks for (the cache key)
        "params": params,  # what was sent (differs only when a stop time was cut to the ephemeris end)
        "fetched_utc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
    }
    save_manifest(manifest)


# -------------------------------------------------------------- the queries

def horizons_query(params: dict) -> dict:
    base = {"format": "json", "MAKE_EPHEM": "YES", "OBJ_DATA": "YES", "CSV_FORMAT": "YES",
            "REF_SYSTEM": "ICRF", "REF_PLANE": "ECLIPTIC", "OUT_UNITS": "KM-S"}
    base.update(params)
    return base


SBDB_FIELDS = "spkid,full_name,pdes,a,e,i,om,w,ma,n,epoch,H,class,condition_code"


def plan() -> list[dict]:
    """Every JPL API request, in fetch order: {rel, service, kind, query}."""
    jobs = []
    for m in MOONS:
        jobs.append(element_job(m["name"], m["horizons"], f"500@{m['center']}", *m["table"]))
    for b in [*HELIO_TABLE_BODIES, *COMETS]:
        jobs.append(element_job(b["name"], b["horizons"], "500@10", *b["table"]))
    for s in SPACECRAFT:
        start, stop, step = s["table"]
        jobs.append({"rel": f"horizons/vectors/{slug(s['name'])}.json.gz", "service": HORIZONS, "kind": "vectors",
                     "query": horizons_query({"COMMAND": s["horizons"], "EPHEM_TYPE": "VECTORS", "CENTER": "500@10",
                                              "START_TIME": start, "STOP_TIME": stop, "STEP_SIZE": step,
                                              "VEC_TABLE": "2"})})
    for name, (command, center, epochs) in TEST_VECTORS.items():
        jobs.append({"rel": f"horizons/test_vectors/{name}.json.gz", "service": HORIZONS, "kind": "horizons",
                     "query": horizons_query({"COMMAND": command, "EPHEM_TYPE": "VECTORS", "CENTER": center,
                                              "TLIST": " ".join(f"{jd:.6f}" for jd in epochs), "TLIST_TYPE": "JD",
                                              "TIME_TYPE": "TDB", "VEC_TABLE": "2"})})
    for sample in SMALL_BODY_SAMPLES:
        jobs.append({"rel": f"sbdb/{sample['name']}.json.gz", "service": SBDB_QUERY, "kind": "sbdb_query",
                     "query": {"fields": SBDB_FIELDS, "sb-kind": "a", "full-prec": "1", **sample["query"]}})
    # Physical parameters for the dwarf planets the IAU/NAIF PCK does not cover.
    for b in HELIO_TABLE_BODIES:
        if b.get("sbdb_phys"):
            jobs.append({"rel": f"sbdb/phys/{slug(b['name'])}.json.gz", "service": SBDB, "kind": "sbdb",
                         "query": {"sstr": b["sbdb_phys"], "phys-par": "1"}})
    return jobs


def element_job(name: str, command: str, center: str, start: str, stop: str, step: str) -> dict:
    return {"rel": f"horizons/elements/{slug(name)}.json.gz", "service": HORIZONS, "kind": "horizons",
            "query": horizons_query({"COMMAND": command, "EPHEM_TYPE": "ELEMENTS", "CENTER": center,
                                     "START_TIME": start, "STOP_TIME": stop, "STEP_SIZE": step,
                                     "TP_TYPE": "ABSOLUTE"})}


# ----------------------------------------------------------------- fetching

def horizons_get(params: dict) -> tuple[bytes, str]:
    # Horizons wants every value but `format` in single quotes.
    quoted = {k: (v if k == "format" else f"'{v}'") for k, v in params.items()}
    data = get(HORIZONS, quoted)
    return data, json.loads(data).get("result", "")


def stop_before_gap(result: str) -> str | None:
    """Horizons reports the end of a target's ephemeris as 'No ephemeris ... after A.D. 2030-JAN-01 ...'."""
    m = re.search(r"after A\.D\. (\d{4}-[A-Z]{3}-\d{2})", result)
    return m.group(1) if m else None


def fetch_job(manifest: dict, job: dict) -> None:
    rel, query = job["rel"], job["query"]
    if cached(manifest, rel, query):
        print(f"cached {rel}")
        return
    print(f"fetch  {rel}")
    params = dict(query)
    if job["kind"] in ("horizons", "vectors"):
        data, result = horizons_get(params)
        if "$$SOE" not in result and job["kind"] == "vectors":
            # A spacecraft trajectory file can end before STOP_TIME: cut there and ask again.
            cut = stop_before_gap(result)
            if cut:
                print(f"  ephemeris ends {cut}; asking again with that stop time")
                params["STOP_TIME"] = cut
                data, result = horizons_get(params)
        if "$$SOE" not in result:
            raise SystemExit(f"{rel}: Horizons returned no table:\n{result[:2000] or data[:2000]!r}")
    else:
        data = get(job["service"], params)
        body = json.loads(data)
        if job["kind"] == "sbdb_query":
            if "data" not in body:
                raise SystemExit(f"{rel}: {str(body)[:2000]}")
            print(f"  {body['count']} objects")
    store(manifest, rel, job["service"], query, params, data)


def fetch_api(kinds: tuple[str, ...]) -> None:
    manifest = load_manifest()
    for job in plan():
        if job["service"] in kinds:
            fetch_job(manifest, job)
    # Drop entries for queries that no longer exist (a body removed from tools/bodies.py).
    planned = {j["rel"] for j in plan()}
    stale = [rel for rel in manifest["responses"] if rel not in planned]
    for rel in stale:
        del manifest["responses"][rel]
        (RAW / rel).unlink(missing_ok=True)
    save_manifest(manifest)


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
            print(f"ok     {dest.relative_to(ROOT)}")
            continue
        print(f"fetch  {NAIF}{rel}")
        data = get(NAIF + rel)
        actual = hashlib.sha256(data).hexdigest()
        if actual != digest:
            raise SystemExit(f"{rel}: SHA-256 {actual} does not match the pinned {digest}")
        dest.write_bytes(data)
        print(f"ok     {dest.relative_to(ROOT)} ({len(data)} bytes)")


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if a != "--refresh"]
    what = args[0] if args else "all"
    if what == "plan":
        print(json.dumps([{k: j[k] for k in ("rel", "service", "query")} for j in plan()], indent=1))
        return 0
    if what not in ("kernels", "horizons", "sbdb", "all"):
        print(__doc__)
        return 2
    if what in ("kernels", "all"):
        fetch_kernels()
    if what in ("horizons", "all"):
        fetch_api((HORIZONS,))
    if what in ("sbdb", "all"):
        fetch_api((SBDB_QUERY, SBDB))
    if what == "all":
        print("next: uv run --python 3.12 tools/build_catalog.py")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
