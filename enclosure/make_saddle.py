"""
Generates the saddle-foot for PaperAir: the PaperDIY in M5Stack's own case stands in it, leaning
back 13 degrees, with the SEN66, the optional SFA40, the FPC breakout and the battery in a foot
behind the case's lower half.

M5Stack's parts are used as they are (bezel, back plate, three button caps; MIT licence, in
M5_Hardware), except that the back plate gets a slot over the board's I2S connector, so the FPC
cable can leave the case. Pass M5Stack's STL with --oem to cut it; nothing of theirs is kept here.

Parts:
  saddle.stl              one piece. Printed standing on its bottom face, as exported, no supports.
  paperdiy_back_slot.stl  M5Stack's back plate with the slot (only with --oem). Printed as M5Stack's.
  paperdiy_bezel_lip.stl  M5Stack's bezel with its lip 2 mm wider all round (--lip), so the panel's
                          black border stays covered. Printed face down, as exported.

Coordinates (mm), "local": X to the right seen from the front, Y down the screen (0 = the top edge
of the case, where the buttons are), Z into the case (0 = the bezel's front face). The saddle is then
leaned back and cut level where it meets the desk.

Usage:
  pip install trimesh manifold3d numpy
  python make_saddle.py --out stl [--oem PaperDlY_Complete_Kit.stl]
"""

import argparse
import math
import os

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold, Mesh, OpType

SEG = 48

# --- M5Stack's case, from its model: 120 x 67 x 6.3, buttons on one long edge, USB-C on one end ---
CASE = (120.0, 67.0, 6.3)
CLR = 0.3                            # around the case in the saddle
# The I2S cable leaves its connector, passes under the display flex's fold and runs down the back
# of the panel, 5-15 mm from the buttons' edge (photo with the case open). The slot is in the back
# plate over the panel, just past the board's inner edge, so the cable turns straight out the back.
SLOT = (53.9, 64.9, 5.0, 17.0)       # x0, x1, y0, y1
# The same slot in M5Stack's coordinates (their model: buttons at -Y, USB-C at +X, front at Z -0.2)
OEM_OFFSET = (84.94, 58.74, -0.2)    # local = oem + offset for X, Y; Z local = -(Z oem + 0.2)

# --- Saddle ---
WALL = 1.6
LIP_T, LIP_H = 1.2, 2.0              # the front lip: thick, and how far up the bezel's face (the window starts 3.6 up)
SIDE_H = 8.0                         # the side ears, up from the case's bottom edge
FLOOR = 2.4                          # under the case at the front (rubber feet sit in it)
TILT = math.radians(13)
T_THIN = CASE[2] + CLR + 2.4         # the thin part behind the case, above the foot: carries the cable groove
GROOVE = 1.0                         # the cable groove in the thin part's face, covered by the case's back plate

# --- Foot ---
SEN = (55.0, 26.0, 21.2)             # SEN66: length (X), width (Y), depth (Z)
SEN_X0 = 6.0
SEN_Z0 = CASE[2] + CLR + WALL + 3.0  # 3 mm in front of it for the cable and wires
T_FOOT = SEN_Z0 + SEN[2] + 0.2 + WALL
SFA = (17.3, 15.2, 3.6)              # SFA40 carrier board, 16.9 x 14.8 x 3.4 measured, plus clearance
SFA_RECESS = 0.6

X0, X1 = -CLR - WALL, CASE[0] + CLR + WALL
Y1 = CASE[1] + CLR + FLOOR           # where the saddle's front-bottom edge meets the desk


# ---------------------------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def rr(x0, x1, y0, y1, r):
    r = min(r, (x1 - x0) / 2 - 0.01, (y1 - y0) / 2 - 0.01)
    return CrossSection.square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]).translate([x0 + r, y0 + r]).offset(
        r, JoinType.Round, circular_segments=SEG)


def prism(section, z0, z1):
    return section.extrude(z1 - z0).translate([0, 0, z0])


def union(parts):
    return Manifold.batch_boolean(parts, OpType.Add)


_d = np.array([0, -math.sin(TILT), -math.cos(TILT)])   # down the face
_n = np.array([0, math.cos(TILT), -math.sin(TILT)])    # into the case
TO_WORLD = np.c_[[1, 0, 0], _d, _n, -Y1 * _d]
TO_LOCAL = np.linalg.inv(np.vstack([TO_WORLD, [0, 0, 0, 1]]))[:3]


def world(m):
    return m.transform(TO_WORLD)


def local(m):
    return m.transform(TO_LOCAL)


def above_desk(m, z=0.0):
    return local(world(m) ^ box(-1000, 1000, -1000, 1000, z, 1000))


def SEN_Y0():
    """The SEN66's top edge, as high as needed for its bottom corner to clear the floor."""
    z_back = SEN_Z0 + SEN[2]
    y_floor_back = Y1 - math.tan(TILT) * z_back - FLOOR / math.cos(TILT)
    return y_floor_back - 0.5 - SEN[1]


def sensor_box():
    y0 = SEN_Y0()
    return (SEN_X0, SEN_X0 + SEN[0], y0, y0 + SEN[1], SEN_Z0, SEN_Z0 + SEN[2])


def sfa_box():
    x1 = X1 - WALL - 1.5
    y1 = SEN_Y0() + SEN[1] - 2
    return (x1 - SFA[0], x1, y1 - SFA[1], y1, T_FOOT - WALL - SFA[2] - 0.2, T_FOOT - WALL - 0.2)


def grille(xs, y0, y1, w=1.8):
    holes = []
    for x in xs:
        sec = CrossSection.square([w, y1 - y0 - w]).translate([x - w / 2, y0 + w / 2])
        sec = sec + CrossSection.circle(w / 2, SEG).translate([x, y0 + w / 2]) + CrossSection.circle(w / 2, SEG).translate([x, y1 - w / 2])
        holes.append(prism(sec, T_FOOT - WALL - 1, T_FOOT + 1))
    return union(holes)


def build():
    zf = CASE[2] + CLR                                   # the case's back face, plus clearance
    step_y = SEN_Y0() - WALL - 0.3                       # the foot's top, at the back
    rise = T_FOOT - T_THIN
    # The foot, with a 45 degree shoulder up to the thin part, and the thin part itself up to the slot
    lower = prism(rr(X0, X1, step_y, Y1 + 30, 2.0), zf, T_FOOT)
    upper = prism(rr(X0, X1, step_y - rise, step_y - rise + 0.5, 0.2), zf, T_THIN)
    s = Manifold.batch_hull([lower, upper])
    s += box(SLOT[0] - 4, SLOT[1] + 4, SLOT[2] - 2, step_y, zf, T_THIN)
    # The floor under the case, the front lip and the side ears
    s += box(X0, X1, CASE[1] + CLR, Y1 + 30, -CLR - LIP_T, T_FOOT)
    s += box(X0, X1, CASE[1] - LIP_H, Y1 + 30, -CLR - LIP_T, -CLR)
    for x0 in (X0, X1 - WALL):
        s += box(x0, x0 + WALL, CASE[1] - SIDE_H, Y1 + 30, -CLR - LIP_T, zf + WALL)
    # The cavity, with the same shoulder inside
    step_i = SEN_Y0() - 0.3
    rise_i = (T_FOOT - WALL) - (zf + WALL)
    inner_lower = prism(rr(X0 + WALL, X1 - WALL, step_i, Y1 + 30, 0.8), zf + WALL, T_FOOT - WALL)
    inner_upper = prism(rr(X0 + WALL, X1 - WALL, step_i - rise_i, step_i - rise_i + 0.5, 0.2), zf + WALL, zf + WALL + 0.01)
    inner = Manifold.batch_hull([inner_lower, inner_upper])
    s -= local(world(inner) ^ box(-1000, 1000, -1000, 1000, FLOOR, 1000))
    # The cable: a groove in the face behind the case, from the slot down, and an opening into the
    # cavity through the front wall
    s -= box(SLOT[0] - 1, SLOT[1] + 1, SLOT[2] - 1, step_i + 4, zf - 0.01, zf + GROOVE)
    s -= box(SLOT[0] - 1, SLOT[1] + 1, step_i - 4, step_i + 6, zf - 0.01, zf + WALL + 1)
    # SEN66: inlet grille at its left end, fan outlet in the middle, fins that hold it
    x0, x1, y0, y1, z0, z1 = sensor_box()
    s -= grille([x0 + 3.2 + 1.0, x0 + 6.7, x0 + 10.2 - 1.0], y0 + 2.8, y1 - 2.8)
    xc, yc, r = x0 + 42.4, y0 + 12.8, 10.6
    slots = []
    for k in range(-3, 4):
        x = xc + 3.0 * k
        h = math.sqrt(max(0.0, r * r - (abs(x - xc) + 0.9) ** 2))
        if h > 2:
            slots.append(grille([x], yc - h, yc + h))
    s -= union(slots)
    zw = T_FOOT - WALL + 0.5
    fins = [box(x0 + 8, x0 + 10, y1 - 0.15, y1 + 6, zf + 1, zw),
            box(x1 - 10, x1 - 8, y1 - 0.15, y1 + 6, zf + 1, zw),
            box(X0 + WALL - 0.5, x0 + 0.15, y0 + 14, y1 + 6, zf + 1, zw),
            box(x1 - 0.15, x1 + 1.85, y0 + 14, y1 + 6, zf + 1, zw)]
    ledge = Manifold.batch_hull([box(x0 + 3, x1 - 3, y0 - 2.5, y0 + 0.15, zw - 0.01, zw),   # along the back wall, above it
                                 box(x0 + 3, x1 - 3, y0 - 2.5, y0 - 2.35, zw - 2.65, zw)])
    s += above_desk(union(fins) + ledge, FLOOR - 0.1)
    # SFA40: a shallow recess in the back wall and its grille
    sx0, sx1, sy0, sy1, sz0, sz1 = sfa_box()
    s -= box(sx0, sx1, sy0, sy1, sz1 - 0.01, sz1 + SFA_RECESS)
    s -= grille([sx0 + 4 + 2.8 * k for k in range(4)], sy0 + 3, sy1 - 3, 1.5)
    # Level with the desk, and recesses for 10 mm rubber feet (2 mm thick, 0.6 mm proud)
    w = world(s) ^ box(-1000, 1000, -1000, 1000, 0, 1000)
    for fx in (X0 + 9, X1 - 9):
        for fy in (12.0, 26.0):
            w -= Manifold.cylinder(1.4, 5.25, circular_segments=SEG).translate([fx, fy, -0.01])
    return local(w)


def oem_back_with_slot(path):
    """M5Stack's back plate (the second-largest part of their kit model) with the cable slot."""
    kit = trimesh.load(path)
    parts = sorted(kit.split(only_watertight=False), key=lambda p: -p.volume)
    back = next(p for p in parts if p.bounds[1][2] < -2 and (p.bounds[1][0] - p.bounds[0][0]) > 100)
    m = Manifold(Mesh(vert_properties=np.asarray(back.vertices, np.float32), tri_verts=np.asarray(back.faces, np.uint32)))
    x0, x1 = SLOT[0] - OEM_OFFSET[0], SLOT[1] - OEM_OFFSET[0]
    y0, y1 = SLOT[2] - OEM_OFFSET[1], SLOT[3] - OEM_OFFSET[1]
    return m - box(x0, x1, y0, y1, -20, 20)


def oem_bezel_with_lip(path, extra):
    """M5Stack's bezel with its window lip reaching `extra` mm further in on all four sides, so the
    panel's black border stays covered. The lip's 45 degree chamfer is kept. Returned face down."""
    kit = trimesh.load(path)
    parts = sorted(kit.split(only_watertight=False), key=lambda p: -p.volume)
    bez = next(p for p in parts if p.bounds[1][2] > -1 and (p.bounds[1][0] - p.bounds[0][0]) > 100)
    m = Manifold(Mesh(vert_properties=np.asarray(bez.vertices, np.float32), tri_verts=np.asarray(bez.faces, np.uint32)))
    # The stock window, in M5Stack's coordinates (front face at z -0.2, lip back at z -1.8)
    wx0, wx1, wy0, wy1 = -81.45, 23.56, -55.10, 4.62
    cham, z_front, z_back = 0.6, -0.2, -1.8
    outer = box(wx0 - 1, wx1 + 1, wy0 - 1, wy1 + 1, z_back, z_front)
    hole = Manifold.batch_hull([box(wx0 + extra, wx1 - extra, wy0 + extra, wy1 - extra, z_back - 1, z_front - cham),
                                box(wx0 + extra - cham, wx1 - extra + cham, wy0 + extra - cham, wy1 - extra + cham, z_front - 0.01, z_front + 1)])
    ring = (outer - hole) ^ box(wx0 - 0.01, wx1 + 0.01, wy0 - 0.01, wy1 + 0.01, z_back, z_front)
    return (m + ring).rotate([180, 0, 0])


# ---------------------------------------------------------------------------------------------
def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="stl")
    ap.add_argument("--oem", help="M5Stack's PaperDlY_Complete_Kit.stl, to cut the slot into its back plate")
    ap.add_argument("--lip", type=float, default=2.0, help="with --oem: also make a bezel whose lip reaches this much further in (mm)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    s = build()
    print(f"saddle: {s.volume():.0f} mm^3, {s.status()}, genus {s.genus()}")
    w = world(s)
    t = to_trimesh(w)
    t.apply_translation(-t.bounds[0])
    t.export(os.path.join(args.out, "saddle.stl"))
    bb = w.bounding_box()
    print(f"saddle: {bb[3] - bb[0]:.1f} wide, {bb[4] - bb[1]:.1f} deep, {bb[5] - bb[2]:.1f} tall")
    if args.oem:
        b = oem_back_with_slot(args.oem)
        t = to_trimesh(b)
        t.export(os.path.join(args.out, "paperdiy_back_slot.stl"))
        print(f"back plate with slot: {b.volume():.0f} mm^3, {b.status()}")
        if args.lip > 0:
            z = oem_bezel_with_lip(args.oem, args.lip)
            t = to_trimesh(z)
            t.apply_translation(-t.bounds[0])
            t.export(os.path.join(args.out, "paperdiy_bezel_lip.stl"))
            print(f"bezel with a {args.lip} mm wider lip: {z.volume():.0f} mm^3, {z.status()}")


if __name__ == "__main__":
    main()
