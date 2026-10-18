#!/usr/bin/env python3
"""Render the README images and animation from the real app, headless.

    LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \\
        uv run --python 3.12 --with pillow==11.0.0 tools/make_media.py build/orrery

Writes docs/media/*.png and docs/media/hero.gif. Every image is a plain
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
