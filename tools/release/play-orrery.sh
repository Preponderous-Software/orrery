#!/bin/sh
# Starts the downloaded orrery. The NASA/JPL data it draws from is not in the
# download (DATA_SOURCES.md): on the first run it is fetched from JPL with
# tools/fetch_data.py and turned into the catalogue with tools/build_catalog.py,
# as a build from source does. Later runs reuse it. Arguments are passed on to
# the orrery, e.g. ./play-orrery.sh --focus Saturn --enlarge 1
set -e
cd "$(dirname "$0")"
if [ ! -f data/catalog/manifest.json ]; then
    if ! command -v uv >/dev/null 2>&1; then
        echo "The orrery needs uv to download the JPL data the first time: https://docs.astral.sh/uv/" >&2
        exit 1
    fi
    echo "Fetching the JPL data (the first run only; a few minutes) ..."
    uv run --python 3.12 tools/fetch_data.py all
    uv run --python 3.12 tools/build_catalog.py
fi
exec ./orrery --data data "$@"
