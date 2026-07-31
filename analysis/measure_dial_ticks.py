#!/usr/bin/env python3
"""Measures the tubecomp VU dial's dB -> needle-angle tick table directly
from brand/mocks/tubecomp/master-03-clean-base.png (the production
faceplate background).

Not part of the build - a one-off measurement tool. Its output (the
`ticks` table printed at the end) is hand-transcribed, with a citation back
to this script, into src/gui/HubNeedle.cpp's own `ticks` table. Re-run this
whenever the tubecomp master render is replaced.

Method
------
1. The needle hub/pivot is taken from components/needle.json's own
   pivotXInMaster/pivotYInMaster (693.51, 364.0) - already the measured
   master-diff HUB CENTRE (not the rod end, see that file's own
   `pivotNote`), so this script does not re-derive the pivot itself.
2. For each of the dial's 9 printed labels (-20,-10,-7,-5,-3,0,+1,+2,+3),
   this script scans a per-label angular search window (found once, by
   inspecting a rendered polar-grid overlay against the actual artwork -
   see dial_grid.png/dial_grid_redzone.png, generated during development)
   and finds the angle whose radial sample line - integrated across a
   colour-distance-from-background metric over the tick-mark radius band
   (116-132 master px from the pivot, itself found by profiling darkness
   vs. radius along a known tick's own ray, see the r-profile printed
   during development) - is darkest. This is the tick mark's own line, not
   the printed digit glyph (which sits at a smaller radius, ~60-116px, and
   is excluded by the radius band).
3. The measured angles are verified visually by overlaying them back onto
   a cropped render of the dial (dial_verify.png) - every tick line lands
   exactly on its printed number's own tick mark.

Convention: 0deg = straight up (-y from the pivot), positive = clockwise.
Matches components/needle.json's own `bakedAngleConvention` field, so the
tick table and the needle sprite's bakedAngleDeg are directly comparable
without any sign/offset conversion.
"""

import math

import numpy as np
from PIL import Image

MASTER_PATH = "/Users/yves/Development/Audio/brand/mocks/tubecomp/master-03-clean-base.png"
PIVOT_X, PIVOT_Y = 693.51, 364.0  # components/needle.json pivotXInMaster/pivotYInMaster
BACKGROUND_RGB = np.array([182.0, 165.0, 132.0])  # sampled cream dial-face colour, away from any ticks/text
TICK_RADII = np.arange(116.0, 132.0, 0.2)  # master px from the pivot; see this file's docstring for how this band was found

# Per-label search windows (degrees), found by visual cross-check against
# dial_grid.png / dial_grid_redzone.png during development - wide enough to
# contain the whole tick mark plus feathered AA edge, narrow enough to never
# capture a neighbouring tick.
SEARCH_WINDOWS = {
    -20: (-52.0, -46.0),
    -10: (-41.0, -35.0),
    -7: (-28.0, -22.0),
    -5: (-18.5, -12.5),
    -3: (-9.3, -3.3),
    0: (0.5, 6.5),
    1: (15.0, 21.0),
    2: (20.5, 25.5),
    3: (26.0, 31.5),
}


def bilinear_sample(arr: np.ndarray, px: float, py: float) -> np.ndarray:
    x0, y0 = int(math.floor(px)), int(math.floor(py))
    fx, fy = px - x0, py - y0
    if x0 < 0 or y0 < 0 or x0 + 1 >= arr.shape[1] or y0 + 1 >= arr.shape[0]:
        return np.array([255.0, 255.0, 255.0])
    c00, c10 = arr[y0, x0], arr[y0, x0 + 1]
    c01, c11 = arr[y0 + 1, x0], arr[y0 + 1, x0 + 1]
    return (c00 * (1 - fx) + c10 * fx) * (1 - fy) + (c01 * (1 - fx) + c11 * fx) * fy


def darkness_at_angle(arr: np.ndarray, deg: float) -> float:
    """Sum of colour-distance-from-background across TICK_RADII at this angle -
    high where a tick line crosses this ray, low (background) elsewhere."""
    rad = math.radians(deg)
    total = 0.0
    for r in TICK_RADII:
        px = PIVOT_X + r * math.sin(rad)
        py = PIVOT_Y - r * math.cos(rad)
        v = bilinear_sample(arr, px, py)
        dist = float(np.linalg.norm(v - BACKGROUND_RGB))
        if dist > 40.0:  # ignore near-background noise
            total += dist
    return total


def measure_tick_angle(arr: np.ndarray, lo: float, hi: float, step: float = 0.05) -> float:
    degs = np.arange(lo, hi, step)
    vals = np.array([darkness_at_angle(arr, d) for d in degs])
    return float(degs[int(np.argmax(vals))])


def main() -> None:
    img = Image.open(MASTER_PATH).convert("RGB")
    arr = np.array(img).astype(float)

    print("Measured tubecomp VU dial dB -> angle table")
    print("(0deg = straight up, positive = clockwise; pivot = "
          f"({PIVOT_X}, {PIVOT_Y}), from components/needle.json)\n")

    table = {}
    for db, (lo, hi) in SEARCH_WINDOWS.items():
        deg = measure_tick_angle(arr, lo, hi)
        table[db] = deg
        print(f"  {db:+3d} dB -> {deg:+7.2f} deg")

    print("\nC++ table (src/gui/HubNeedle.cpp):")
    for db in sorted(table):
        sign = "+" if db >= 0 else ""
        print(f"    Tick {{ {sign}{db}.0f, {table[db]:+.2f}f }},")


if __name__ == "__main__":
    main()
