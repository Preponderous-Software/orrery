"""Cut a JPL SPK kernel (DAF, type 2 Chebyshev segments) down to a date window.

    uv run --python 3.12 tools/trim_spk.py data/kernels/de440s.bsp out.bsp --from 1970-01-01 --to 2100-01-01

The browser build ships the planetary kernel inside its preloaded data file,
and DE440s spans 1849-2150 (32 MB). This keeps, for every type 2 segment, only
the Chebyshev records that overlap the window, so the numbers that remain are
JPL's own, unchanged: inside the window the cut kernel gives the same
positions bit for bit (tests/check_trim_spk.py compares them).

The output is a plain DAF/SPK file: one file record, one summary record, one
name record and the segment data, little-endian, no comment area. Only type 2
segments are kept (the only type src/orrery/spk.cpp reads, and the only type
in DE440s).
"""

import argparse
import datetime
import struct
import sys

RECORD = 1024
WORDS = RECORD // 8
J2000 = datetime.datetime(2000, 1, 1, 12, 0, 0)


def et_of(date):
    """TDB seconds past J2000 for a calendar date (TDB-UTC, about a minute, is ignored:
    the window is padded to whole records anyway)."""
    d = datetime.datetime.fromisoformat(date)
    return (d - J2000).total_seconds()


def read_segments(data):
    if data[:8] != b"DAF/SPK ":
        raise SystemExit("not an SPK file")
    if data[88:96] != b"LTL-IEEE":
        raise SystemExit("only little-endian SPK files are supported")
    nd, ni = struct.unpack_from("<ii", data, 8)
    fward = struct.unpack_from("<i", data, 76)[0]
    if (nd, ni) != (2, 6):
        raise SystemExit("unexpected summary format")
    words = memoryview(data).cast("d")
    segments = []
    record = fward
    while record > 0:
        base = (record - 1) * WORDS
        nxt, _prev, count = int(words[base]), int(words[base + 1]), int(words[base + 2])
        for i in range(count):
            at = base + 3 + i * 5
            start, end = words[at], words[at + 1]
            ints = struct.unpack_from("<6i", data, (at + 2) * 8)
            name = data[record * RECORD + i * 40: record * RECORD + (i + 1) * 40]
            segments.append((start, end, ints, name))
        record = nxt
    return words, segments


def trim(words, segment, et0, et1):
    start, end, ints, name = segment
    target, center, frame, kind, begin, last = ints
    if kind != 2:
        return None
    init, length, rsize, n = (words[last - 4], words[last - 3], int(words[last - 2]), int(words[last - 1]))
    lo = max(start, et0)
    hi = min(end, et1)
    if lo >= hi:
        return None
    first = max(0, min(n - 1, int((lo - init) // length)))
    final = max(0, min(n - 1, int((hi - init) // length)))
    count = final - first + 1
    new_init = init + first * length
    new_start = max(start, new_init)
    new_end = min(end, new_init + count * length)
    body = list(words[begin - 1 + first * rsize: begin - 1 + (final + 1) * rsize])
    body += [new_init, length, float(rsize), float(count)]
    return new_start, new_end, (target, center, frame, kind), name, body


def write(path, kept, source_header):
    if len(kept) > (WORDS - 3) // 5:
        raise SystemExit("too many segments for one summary record")
    out = bytearray(RECORD * 3)
    # File record: keep the id word, ND/NI, the internal file name and the
    # FTP validation string of the source; point at our own records.
    out[0:RECORD] = source_header
    data_words = []
    summaries = bytearray(RECORD)
    names = bytearray(b" " * RECORD)
    address = 3 * WORDS + 1  # first word after the file, summary and name records
    struct.pack_into("<ddd", summaries, 0, 0.0, 0.0, float(len(kept)))
    for i, (start, end, (target, center, frame, kind), name, body) in enumerate(kept):
        first, last = address, address + len(body) - 1
        struct.pack_into("<dd6i", summaries, 24 + i * 40, start, end, target, center, frame, kind, first, last)
        names[i * 40:(i + 1) * 40] = name
        data_words += body
        address = last + 1
    struct.pack_into("<iii", out, 76, 2, 2, address)  # FWARD, BWARD, FREE
    out[RECORD:2 * RECORD] = summaries
    out[2 * RECORD:3 * RECORD] = names
    payload = struct.pack("<%dd" % len(data_words), *data_words)
    payload += b"\0" * (-len(payload) % RECORD)
    with open(path, "wb") as f:
        f.write(out)
        f.write(payload)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("source")
    p.add_argument("output")
    p.add_argument("--from", dest="start", required=True, help="first date kept (YYYY-MM-DD, TDB)")
    p.add_argument("--to", dest="stop", required=True, help="last date kept (YYYY-MM-DD, TDB)")
    a = p.parse_args(argv)
    with open(a.source, "rb") as f:
        data = f.read()
    words, segments = read_segments(data)
    et0, et1 = et_of(a.start), et_of(a.stop)
    kept = [t for t in (trim(words, s, et0, et1) for s in segments) if t]
    write(a.output, kept, data[:RECORD])
    size = sum(len(t[4]) for t in kept) * 8
    print("%s: %d segments, %.1f MB of %s kept (%s .. %s)" % (a.output, len(kept), size / 1e6, a.source, a.start,
                                                             a.stop))
    return 0


if __name__ == "__main__":
    sys.exit(main())
