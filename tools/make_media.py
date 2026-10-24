#!/usr/bin/env python3
"""Render the README images and animation from the real app, headless.

    LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \\
        uv run --python 3.12 --with pillow==11.0.0 tools/make_media.py build/orrery

Writes docs/media/*.png (mission_complete.png is the last frame of a mission flown on the landing
autopilot; air.png puts together the last frames of four landings through air, and reentry.png is
one frame of the descent to Earth), docs/media/hero.gif, earth_day.gif, ship.gif and landing.gif. Every image is a plain
`orrery --headless` run; the command for each is printed.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
MEDIA = ROOT / "docs" / "media"

STILLS = {
    "overview_compressed": ["--date", "2026-10-18", "--focus", "Sun", "--mode", "compressed", "--distance-au", "26",
                            "--pitch", "32", "--yaw", "-70"],
    "inner_true": ["--date", "2026-10-18", "--focus", "Sun", "--enlarge", "500", "--pitch", "30", "--yaw", "-70"],
    "saturn_true": ["--date", "2032-03-01", "--focus", "Saturn", "--enlarge", "1", "--distance-au", "0.0035",
                    "--pitch", "20", "--yaw", "-40", "--no-small-bodies"],
    "jupiter_true": ["--date", "2026-10-18", "--focus", "Jupiter", "--enlarge", "1", "--distance-au", "0.022",
                     "--pitch", "30", "--yaw", "-70", "--no-small-bodies"],
    "pluto_charon_true": ["--date", "2026-10-18", "--focus", "Pluto", "--enlarge", "1", "--distance-au", "0.00025",
                          "--pitch", "35", "--yaw", "-60", "--no-small-bodies"],
    "earth_true": ["--date", "2026-10-18T12:00", "--focus", "Earth", "--enlarge", "1", "--distance-au", "0.00028",
                   "--pitch", "15", "--yaw", "-70", "--no-small-bodies"],
    "kuiper_compressed": ["--date", "2026-10-18", "--focus", "Sun", "--mode", "compressed", "--distance-au", "30",
                          "--pitch", "70", "--yaw", "-90"],
    "missions": ["--date", "2026-11-24", "--focus", "Sun", "--enlarge", "500", "--pitch", "30", "--yaw", "-70",
                 "--no-labels", "--missions"],
}


def run(binary: str, args: list[str], size: tuple[int, int] = (1280, 720)) -> None:
    cmd = [binary, "--headless", "--width", str(size[0]), "--height", str(size[1]), *args]
    print("$", " ".join(cmd))
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "orrery")
    MEDIA.mkdir(parents=True, exist_ok=True)
    for name, args in STILLS.items():
        run(binary, [*args, "--screenshot", str(MEDIA / f"{name}.png")])

    # Hero: the inner solar system at true distances (bodies x500, Sun x20)
    # through one year, eight days per frame, from a fixed camera.
    with tempfile.TemporaryDirectory() as tmp:
        run(binary, ["--date", "2026-01-01", "--focus", "Sun", "--enlarge", "500", "--pitch", "30", "--yaw", "-40",
                     "--distance-au", "4.6", "--frames", tmp, "--count", "46", "--step", "8"], size=(640, 360))
        rgb = [Image.open(p).convert("RGB") for p in sorted(Path(tmp).glob("frame_*.png"))]
        # One palette for every frame (from a strip of three frames), so the
        # encoder's frame differencing never mixes palettes.
        strip = Image.new("RGB", (rgb[0].width, rgb[0].height * 3))
        for k, im in enumerate((rgb[0], rgb[len(rgb) // 2], rgb[-1])):
            strip.paste(im, (0, k * rgb[0].height))
        palette = strip.quantize(colors=48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in rgb]
        out = MEDIA / "hero.gif"
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=110, loop=0, optimize=True)
        print(f"wrote {out} ({out.stat().st_size / 1e6:.2f} MB, {len(frames)} frames)")

    # Earth through one day, an hour per frame: the planet map turning about
    # its tilted axis (IAU pole and prime meridian from pck00011.tpc).
    with tempfile.TemporaryDirectory() as tmp:
        run(binary, ["--date", "2026-12-21", "--focus", "Earth", "--enlarge", "1", "--distance-au", "0.0002",
                     "--pitch", "10", "--yaw", "-90", "--no-small-bodies", "--no-orbits", "--no-labels", "--no-hud",
                     "--frames", tmp, "--count", "24", "--step", str(1 / 24)], size=(480, 270))
        rgb = [Image.open(p).convert("RGB") for p in sorted(Path(tmp).glob("frame_*.png"))]
        palette = rgb[0].quantize(colors=96, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in rgb]
        out = MEDIA / "earth_day.gif"
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=120, loop=0, optimize=True)
        print(f"wrote {out} ({out.stat().st_size / 1e6:.2f} MB, {len(frames)} frames)")

    # The ship on autopilot from near Earth to Mars, 1/30 s of flight per
    # rendered frame and every fifth frame kept: up through the drive tiers,
    # mass locked by Mars's moons, stopping a few Mars radii out.
    with tempfile.TemporaryDirectory() as tmp:
        run(binary, ["--date", "2026-10-18", "--focus", "Earth", "--enlarge", "1", "--ship", "--target", "Mars",
                     "--autopilot", "--no-small-bodies", "--frames", tmp, "--count", "760", "--step", "0"],
            size=(640, 360))
        rgb = [Image.open(p).convert("RGB") for p in sorted(Path(tmp).glob("frame_*.png"))[::5]]
        strip = Image.new("RGB", (rgb[0].width, rgb[0].height * 3))
        for k, im in enumerate((rgb[0], rgb[len(rgb) // 2], rgb[-1])):
            strip.paste(im, (0, k * rgb[0].height))
        palette = strip.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in rgb]
        frames += [frames[-1]] * 12  # hold on the arrival
        out = MEDIA / "ship.gif"
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=80, loop=0, optimize=True)
        print(f"wrote {out} ({out.stat().st_size / 1e6:.2f} MB, {len(frames)} frames)")

    # The landing autopilot from 20,000 km out down to the Moon's surface,
    # near full moon so the ground is lit; every sixth frame kept, and a hold
    # on the landed ship.
    with tempfile.TemporaryDirectory() as tmp:
        run(binary, ["--date", "2026-11-24", "--focus", "Moon", "--enlarge", "1", "--ship", "--target", "Moon",
                     "--land", "--no-small-bodies", "--frames", tmp, "--count", "640", "--step", "0"],
            size=(640, 360))
        rgb = [Image.open(p).convert("RGB") for p in sorted(Path(tmp).glob("frame_*.png"))[::6]]
        picks = rgb[:: max(len(rgb) // 6, 1)]  # the palette sees the whole descent, HUD colours included
        strip = Image.new("RGB", (rgb[0].width, rgb[0].height * (len(picks) + 1)))
        for k, im in enumerate(picks):
            strip.paste(im, (0, k * rgb[0].height))
        # The HUD's text colours (renderer.cpp kGo, kWarn, kAlarm) cover too few
        # pixels to win a palette entry on their own.
        hud = [(115, 255, 153), (255, 191, 89), (255, 107, 97)]
        band = rgb[0].width // len(hud)
        for k, colour in enumerate(hud):
            strip.paste(colour, (k * band, len(picks) * rgb[0].height, (k + 1) * band, strip.height))
        palette = strip.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in rgb]
        frames += [frames[-1]] * 10  # hold on the landed ship
        out = MEDIA / "landing.gif"
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=120, loop=0, optimize=True)
        print(f"wrote {out} ({out.stat().st_size / 1e6:.2f} MB, {len(frames)} frames)")

    # The Moon run flown on the landing autopilot (about 25 s, at 1/30 s per
    # frame): its last frame, landed, with the mission complete.
    with tempfile.TemporaryDirectory() as tmp:
        run(binary, ["--mission", "moon-run", "--land", "--no-small-bodies", "--frames", tmp, "--count", "790",
                     "--step", "0"])
        last = sorted(Path(tmp).glob("frame_*.png"))[-1]
        out = MEDIA / "mission_complete.png"
        Image.open(last).save(out)
        print(f"wrote {out}")

    # Landing through air: the landing autopilot to Venus, Earth, Mars and
    # Titan, each from its start pose to the ground, and the last frame of
    # each (landed, under that sky) in a 2 x 2 grid. The descent to Earth
    # gives reentry.png too: frame 1000, 57 km up at 10 km/s and 27% heat.
    landed = []
    for body, count in (("Venus", 4000), ("Earth", 1500), ("Mars", 1500), ("Titan", 4000)):
        with tempfile.TemporaryDirectory() as tmp:
            run(binary, ["--date", "2026-11-24", "--enlarge", "1", "--ship", "--target", body,
                         "--land", "--no-small-bodies", "--frames", tmp, "--count", str(count), "--step", "0"],
                size=(640, 360))
            frames = sorted(Path(tmp).glob("frame_*.png"))
            landed.append(Image.open(frames[-1]).convert("RGB"))
            if body == "Earth":
                out = MEDIA / "reentry.png"
                Image.open(frames[1000]).save(out)
                print(f"wrote {out}")
    grid = Image.new("RGB", (landed[0].width * 2, landed[0].height * 2))
    for k, im in enumerate(landed):
        grid.paste(im, ((k % 2) * im.width, (k // 2) * im.height))
    out = MEDIA / "air.png"
    grid.save(out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
