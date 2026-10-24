@echo off
setlocal
rem Starts the downloaded orrery. The NASA/JPL data it draws from is not in the
rem download (DATA_SOURCES.md): on the first run it is fetched from JPL with
rem tools\fetch_data.py and turned into the catalogue with tools\build_catalog.py,
rem as a build from source does. Later runs reuse it. Arguments are passed on to
rem the orrery, e.g. play-orrery.bat --focus Saturn --enlarge 1
pushd "%~dp0"
if not exist "data\catalog\manifest.json" (
    where uv >nul 2>nul || (echo The orrery needs uv to download the JPL data the first time: https://docs.astral.sh/uv/ - for example: winget install astral-sh.uv & popd & pause & exit /b 1)
    echo Fetching the JPL data ^(the first run only; a few minutes^) ...
    uv run --python 3.12 tools\fetch_data.py all || (popd & pause & exit /b 1)
    uv run --python 3.12 tools\build_catalog.py || (popd & pause & exit /b 1)
)
orrery.exe --data data %*
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" pause
exit /b %RC%
