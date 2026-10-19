"""Assemble the browser build's data directory from the built catalogue.

    uv run --python 3.12 tools/make_web_data.py [OUT]     (default: build-web-data)

The browser build preloads one data file, so it gets a smaller copy of data/:

- kernels/de440s.bsp cut to the moons' element-table span (tools/bodies.py
  MOON_SPAN, 1980-2070) by tools/trim_spk.py. DE440s covers 1849-2150 in 32 MB;
  the cut keeps JPL's own Chebyshev records for those 90 years (about 10 MB).
  Every body then has data on every date the page can show, and the page's
  date range is the kernel's (SolarSystem::firstJd/lastJd).
- catalog/: the same files, with each element table and spacecraft trajectory
  cut to the same window (plus one row either side, for interpolation).
- textures/: the planet maps, unchanged.

Nothing here is new data: every number is copied from data/ as built by
tools/fetch_data.py and tools/build_catalog.py.
"""

import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import bodies  # noqa: E402
import trim_spk  # noqa: E402

JD_J2000 = 2451545.0


def jd_of(date):
    return JD_J2000 + trim_spk.et_of(date) / 86400.0


def cut_table(src, dst, jd0, jd1):
    """Copy a catalogue CSV, keeping comments, the header and the rows from the
    last one at or before jd0 to the first one at or after jd1."""
    lines = src.read_text().splitlines()
    head = [l for l in lines if l.startswith("#")]
    rows = [l for l in lines if not l.startswith("#")]
    header, data = rows[0], rows[1:]
    jds = [float(r.split(",", 1)[0]) for r in data]
    first = max([i for i, jd in enumerate(jds) if jd <= jd0], default=0)
    last = min([i for i, jd in enumerate(jds) if jd >= jd1], default=len(jds) - 1)
    if first > last:  # a table entirely outside the window: keep it whole
        first, last = 0, len(jds) - 1
    note = "# Browser build: rows cut to JD %.1f..%.1f by tools/make_web_data.py." % (jd0, jd1)
    dst.write_text("\n".join(head + [note, header] + data[first:last + 1]) + "\n")


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else ROOT / "build-web-data"
    data = ROOT / "data"
    if not (data / "catalog" / "bodies.csv").exists():
        raise SystemExit("no data/catalog: run tools/fetch_data.py all, then tools/build_catalog.py")
    start, stop = bodies.MOON_SPAN[0], bodies.MOON_SPAN[1]
    if out.exists():
        shutil.rmtree(out)
    (out / "kernels").mkdir(parents=True)
    trim_spk.main([str(data / "kernels" / "de440s.bsp"), str(out / "kernels" / "de440s.bsp"),
                   "--from", start, "--to", stop])

    catalog = out / "catalog"
    catalog.mkdir()
    for f in sorted((data / "catalog").iterdir()):
        if f.is_file():
            shutil.copy2(f, catalog / f.name)
    jd0, jd1 = jd_of(start), jd_of(stop)
    for sub in ("elements", "trajectories"):
        (catalog / sub).mkdir()
        for f in sorted((data / "catalog" / sub).glob("*.csv")):
            cut_table(f, catalog / sub / f.name, jd0, jd1)
    shutil.copytree(data / "textures", out / "textures")

    total = sum(f.stat().st_size for f in out.rglob("*") if f.is_file())
    for part in ("kernels", "catalog", "textures"):
        size = sum(f.stat().st_size for f in (out / part).rglob("*") if f.is_file())
        print("  %-9s %6.2f MB" % (part, size / 1e6))
    print("%s: %.2f MB of web data, %s .. %s" % (out, total / 1e6, start, stop))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
