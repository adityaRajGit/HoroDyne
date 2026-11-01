"""Check the real module mesh (ref/module.stl, module coords) against the case pocket.

Reads the pocket numbers from the .scad echo-free parameters by re-deriving them here;
keep in sync with horodyne_case.scad. Prints the worst clearance per zone; exits 1 on a clash.
Usage: python check_fit.py ref/module.stl
"""
import math
import struct
import sys

glass_w, glass_y0, glass_y1, glass_r = 33.15, -21.83, 19.28, 7.6
glass_z0, lcd_y0, lcd_y1 = 2.0, -20.51, 17.49
fit_clear, wall = 0.25, 1.8
btn_face_x, btn_cz, flange_t = 15.57, -3.42, 1.0
flange_w, flange_h = 3.7 + 1.4, 2.5 + 1.4   # hole + 2*flange_m
btn_y = [-9.77, -0.85, 8.06]
parts_low, bat_top_gap, bat_swell, bat_t = -4.6, 0.3, 0.4, 4

cav_hx = glass_w / 2 + fit_clear
cav_y0, cav_y1, cav_r = glass_y0 - fit_clear, glass_y1 + fit_clear, glass_r + fit_clear
ledge_y0, ledge_y1 = lcd_y0 - fit_clear, lcd_y1 + fit_clear
floor_top = parts_low - bat_top_gap - bat_swell - bat_t
pusher_in_x = cav_hx - flange_t


def rrect_margin(x, y, hx, y0, y1, r):
    """Signed distance inside a rounded rect (positive = inside)."""
    cy = (y0 + y1) / 2
    hy = (y1 - y0) / 2
    qx, qy = abs(x) - (hx - r), abs(y - cy) - (hy - r)
    if qx > 0 and qy > 0:
        return r - math.hypot(qx, qy)
    return min(hx - abs(x), hy - abs(y - cy))


def verts(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    for i in range(n):
        o = 84 + i * 50 + 12
        for k in range(3):
            yield struct.unpack("<3f", d[o + 12 * k:o + 12 * k + 12])


worst = {"pocket": 1e9, "ledges": 1e9, "pushers": 1e9, "floor": 1e9}
for x, y, z in verts(sys.argv[1]):
    worst["pocket"] = min(worst["pocket"], rrect_margin(x, y, cav_hx, cav_y0, cav_y1, cav_r))
    if z < glass_z0 - 0.05:   # below the glass: must clear the end ledges
        worst["ledges"] = min(worst["ledges"], y - ledge_y0, ledge_y1 - y)
    for by in btn_y:          # pusher flange zone, swept along the insertion path
        if abs(y - by) < flange_w / 2 and z < btn_cz + flange_h / 2:
            worst["pushers"] = min(worst["pushers"], pusher_in_x - x)
    worst["floor"] = min(worst["floor"], z - floor_top)

bad = False
for k, v in worst.items():
    print(f"{k:7s} min clearance {v:6.3f} mm")
    bad |= v < 0
sys.exit(1 if bad else 0)
