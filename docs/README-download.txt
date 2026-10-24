Orrery
======

The whole solar system from real JPL data: the Sun, the planets, the dwarf
planets, 25 moons, Saturn's rings, 12,584 small bodies, three comets and
three spacecraft, placed from JPL ephemerides at any instant from 1850 to
2149. There is a ship to fly, and missions to race.

The NASA/JPL data is not in this download. The first run fetches it from
JPL (about 3 minutes; the NAIF kernels are 32 MB) and builds the catalogue
the orrery reads, into the data folder next to it. That needs the network
and uv (https://docs.astral.sh/uv/), which runs the two Python scripts in
tools/. Later runs reuse the data.

Play
----

Windows:  double-click play-orrery.bat. Windows may say it protected your
          PC, because the orrery is not signed: choose "More info", then
          "Run anyway". uv can be installed with: winget install astral-sh.uv
Linux:    ./play-orrery.sh   (needs OpenGL 4.5, which any current Mesa or
          vendor driver has, and glibc 2.34 or newer: Ubuntu 22.04, Debian
          12, Fedora 35 and later)

Arguments are passed on, for example:

  play-orrery --focus Saturn --enlarge 1 --distance-au 0.004
  play-orrery --date 1986-02-09 --focus Halley --enlarge 1
  play-orrery --mission moon-run

orrery --help lists every option. Mission best times are kept in
%APPDATA%\orrery\records.txt on Windows and ~/.local/share/orrery/records.txt
on Linux.

More: https://github.com/Preponderous-Software/orrery
In a browser: https://orrery.play.danielstephenson.dev

The orrery's code is MIT-licensed (LICENSE.txt). DATA_SOURCES.md credits the
data and gives its terms. It is built on the Strata engine; THIRD-PARTY.md
lists what that links in, and their licences.
