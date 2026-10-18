#!/usr/bin/env python3
"""Opens a layer written by `orrery --export-usda` with OpenUSD and checks it.

    uv run --python 3.12 --with usd-core==25.8 tests/check_usda.py out.usda
"""
import math
import sys

from pxr import Usd, UsdGeom

AU_KM = 149597870.7


def main(path: str) -> int:
    stage = Usd.Stage.Open(path)
    assert stage, f"OpenUSD could not open {path}"
    assert UsdGeom.GetStageUpAxis(stage) == "Z"
    assert UsdGeom.GetStageMetersPerUnit(stage) == 1000
    root = stage.GetDefaultPrim()
    assert root and root.GetName() == "SolarSystem"
    spheres = [p for p in stage.Traverse() if p.IsA(UsdGeom.Sphere)]
    points = [p for p in stage.Traverse() if p.IsA(UsdGeom.Points)]
    earth = stage.GetPrimAtPath("/SolarSystem/Earth")
    sun = stage.GetPrimAtPath("/SolarSystem/Sun")
    radius = UsdGeom.Sphere(earth).GetRadiusAttr().Get()
    e = earth.GetAttribute("xformOp:translate").Get()
    s = sun.GetAttribute("xformOp:translate").Get()
    d_au = math.dist(e, s) / AU_KM
    n_points = sum(len(UsdGeom.Points(p).GetPointsAttr().Get()) for p in points)
    print(f"{len(spheres)} spheres, {len(points)} point sets ({n_points} points); Earth radius {radius} km, "
          f"{d_au:.4f} au from the Sun")
    assert len(spheres) >= 38, "every body with a known radius is a Sphere"
    assert abs(radius - 6371.0) < 1.0
    assert 0.98 < d_au < 1.02
    assert n_points > 12000
    assert earth.GetCustomDataByKey("orrery:positionSource").startswith("JPL DE440s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
