# Releasing

A release is a bare version tag on `main` (`0.6.0`, never `v0.6.0`) with a GitHub release on it.
Every orrery release so far has been a full release, not a pre-release. Publishing the release
starts [`.github/workflows/release.yml`](../.github/workflows/release.yml), which builds the desktop
orrery for Windows x64 and Linux x64 and attaches the downloads to the release. macOS is not built.
The browser build is separate: it deploys to the arcade on a push to `main` when the repository
variable `ARCADE_ENABLED` is `true`, or when its workflow is run by hand
([web/README.md](../web/README.md)).

## Steps

1. **Set the version.** On a branch, set the version in `project(orrery VERSION ...)` at the top
   of [`CMakeLists.txt`](../CMakeLists.txt) and open a pull request ("Release X.Y.Z"). The release
   workflow refuses a tag that does not match it.

2. **Create the release on the merged commit.** Pass the full SHA, not a branch name, so the tag
   lands on exactly the commit that was checked:

   ```bash
   git fetch origin
   sha=$(git rev-parse origin/main)
   gh release create 0.6.0 --target "$sha" --title "0.6.0" --notes-file notes.md
   ```

3. **Wait for the builds.** The `Release` workflow runs on the published release. When it
   finishes, the release has four assets:

   | Asset | Contents |
   | --- | --- |
   | `Orrery-<version>-win-x64.zip` | `orrery.exe`, `play-orrery.bat`, the data tools, the planet maps, `README.txt`, `LICENSE.txt`, `DATA_SOURCES.md`, `THIRD-PARTY.md` |
   | `Orrery-<version>-linux-x64.tar.gz` | `orrery`, `play-orrery.sh` and the same files |
   | `*.sha256` | The SHA-256 of each archive |

   If the workflow fails, fix the cause and re-run it (`gh run rerun <id>`); uploads replace assets
   of the same name.

Pushing a version tag without creating a release also works: the workflow builds the tag and
creates a full release for it with generated notes.

## What is in a download

No NASA/JPL data. [DATA_SOURCES.md](../DATA_SOURCES.md) records that none of it is republished
from this repository; the downloads are published from it, so they carry none. A download carries:

- the program;
- `play-orrery.sh` / `play-orrery.bat` ([`tools/release/`](../tools/release/));
- `tools/fetch_data.py`, `tools/build_catalog.py` and `tools/bodies.py`;
- `data/kernels.sha256`;
- the planet maps in `data/textures/` (CC BY 4.0, committed here);
- the licence, the data sources and the third-party notices.

On the first run, `play-orrery` fetches the data from JPL with uv, builds the catalogue next to
the program, and starts `orrery --data data`, as a build from source does (README.md, *Building
and running*). Later runs reuse it.

Strata and SDL3 are linked into the executable, and the Windows build links the C++ runtime
statically (`/MT`). The player needs an OpenGL 4.5 driver on Windows 10/11 x64, or on Linux x64
with glibc 2.34 or newer (the build runs on Ubuntu 22.04), plus uv and the network for the first
run.

What each run checks:

- **Linux:** the packaged binary links only the C and C++ system libraries. The packaged
  `play-orrery.sh` then builds the data with the packaged tools and renders the smoke test's views
  headless on Mesa llvmpipe (`--smoke-test`). CI's kernel and JPL-response caches are copied in
  first, so JPL is not asked again, and they are removed before the archive is made. The run fails
  if any kernel, response or catalogue file is left in the package.
- **Windows:** the runner has no OpenGL 4.5 driver, so the orrery cannot draw there.
  `dumpbin /dependents` checks that `orrery.exe` imports no Visual C++ runtime or SDL DLL.

## Checking the packaging without a release

A pull request that changes `release.yml`, `CMakeLists.txt`, the launchers, the data tools or the
download's README and notices runs the same builds as a dry run, keeping the archives as workflow
artifacts. So does running it by hand:

```bash
gh workflow run release.yml --ref <branch>             # dry run
gh workflow run release.yml --ref main -f tag=0.6.0    # build a tag and attach to its release
```
