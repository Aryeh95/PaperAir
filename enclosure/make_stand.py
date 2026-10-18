"""
Generates a desk stand for the M5Stack PaperDIY that holds the SEN66 and the optional SFA40.

The PaperDIY stands in a slot along the top of a low base, leaning back 12 degrees, upside down
compared with how it ships, so its two buttons are on the top edge and the USB-C port is high
on the left. The sensors sit in the base:
  - SEN66: against the back wall, breathing through two grilles (inlet and fan outlet)
  - SFA40: in a slide-in bay at the left end, behind a small grille
  - FPC breakout board: loose in the front-left corner (double-sided foam tape)
The FPC cable leaves the PaperDIY through a slot in its back plate, runs down behind it in a
groove at the back of the slot, and drops into the base. A slim spine behind the PaperDIY hides
the cable.

Parts (all print without supports):
  stand_base.stl         printed upside down, top face on the bed
  stand_bottom.stl       the bottom plate, 4 x M3 countersunk screws into M3 heat-set inserts
  stand_spine.stl        printed lying on its back; push it into the socket behind the slot
  back_plate_fpc.stl     only with --oem: M5Stack's back plate with a slot for the FPC cable

Coordinates (mm): X to the right seen from the front, Y away from the viewer, Z up, base
centred on X and Y with its underside at Z = 0.

Usage:
  pip install trimesh manifold3d numpy
  python make_stand.py --out stl/
  python make_stand.py --out stl/ --oem PaperDlY_Complete_Kit.stl   # also the back plate
The OEM model is M5Stack's PaperDIY enclosure (MIT licence), from
https://github.com/m5stack/M5_Hardware/tree/master/Products/K159-B_PaperDIY_Complete_Kit/Structures
"""

import argparse
import math
import os

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold, Mesh, OpType

SEG = 64

# --- PaperDIY (M5Stack's drawing and STL) ---
DEV_LENGTH = 120.0
DEV_HEIGHT = 67.0
DEV_THICK = 6.3
CAP_PROUD = 1.3            # the power button cap stands out of the left end (as it stands here)

# --- Base ---
W, D, H = 132.0, 56.0, 32.0
R = 5.0                    # corner radius
WALL = 2.2
TOP = 2.4                  # top thickness
FLOOR = 2.4                # bottom plate thickness
TILT = math.radians(12)    # lean back from vertical
SLOT_FRONT_Y = -16.0       # where the front face of the PaperDIY meets the top of the base
SLOT_W = DEV_THICK + 0.5
SLOT_DEPTH = 7.0
FRONT_LIP = 3.0            # the screen starts 3.8 mm above the PaperDIY's edge, so the base in
                           # front of it steps down to 3 mm above the slot floor
SLOT_WALL = 1.8

# FPC cable: 12-way, 0.5 mm pitch, 6.5 mm wide. Where it runs, in X.
CABLE_X = (-11.5, -0.5)

# Spine behind the PaperDIY
SPINE_X = (-13.0, 1.0)
SPINE_GAP = 0.8            # from the PaperDIY's back
SPINE_THICK = 3.0
SPINE_TOP = 58.0           # along the PaperDIY, measured from the top of the base
SOCKET_DEPTH = 6.0

# Heat-set inserts (CNC Kitchen M3 x 5.7: 4.4 mm hole as measured on your printer)
INSERT_D, INSERT_DEPTH = 4.4, 7.0
BOSS_R = 4.25
BOSSES = [(sx * (W / 2 - 6.5), sy * (D / 2 - 6.5)) for sx in (-1, 1) for sy in (-1, 1)]

# SEN66: the larger of the datasheet and a measured one; lies with its inlet/outlet face against
# the back wall, inlet end on the right
SEN = (55.0, 21.2, 26.0)   # length (X), connector face to inlet face (Y), width (Z)
SEN_X0 = -0.7
GAP = 0.3

# SFA40 module: about 17.5 x 15 mm, 4 mm thick with the sensor cap
SFA_W, SFA_H, SFA_T = 17.5, 15.0, 4.0

# Slot for the FPC cable in M5Stack's back plate, measured from the case's top-left corner seen
# from the screen side as it ships (buttons on the bottom edge). The FPC-I2S connector sits about
# 71-76 mm from the left and 49-59 mm from the top, and its cable leaves towards the left.
# Check it against your board before printing.
BACKPLATE_SLOT = dict(x=(63.5, 68.5), y=(47.5, 60.5))
OEM_LEFT_X, OEM_TOP_Y = -84.94, 8.26          # that corner in the STL's coordinates


# ---------------------------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def rr(w, d, r, cx=0.0, cy=0.0):
    return CrossSection.square([w - 2 * r, d - 2 * r]).translate([cx - w / 2 + r, cy - d / 2 + r]).offset(
        r, JoinType.Round, circular_segments=SEG)


def prism(section, z0, z1):
    return section.extrude(z1 - z0).translate([0, 0, z0])


def cyl(x, y, z0, z1, r):
    return Manifold.cylinder(z1 - z0, r, circular_segments=SEG).translate([x, y, z0])


# The slot frame: X, s (across the slot, towards the back), t (along the PaperDIY, upwards),
# with s = t = 0 where the PaperDIY's front face meets the top of the base.
SLOT_M = np.array([[1, 0, 0, 0],
                   [0, math.cos(TILT), math.sin(TILT), SLOT_FRONT_Y],
                   [0, -math.sin(TILT), math.cos(TILT), H]], dtype=float)


def sbox(x0, x1, s0, s1, t0, t1):
    return box(x0, x1, s0, s1, t0, t1).transform(SLOT_M)


def slot_point(x, s, t):
    return SLOT_M @ np.array([x, s, t, 1.0])


def union(parts):
    return Manifold.batch_boolean(parts, OpType.Add)


def grille_slots(x_centres, z0, z1, width, y0, y1, clip=None):
    """Vertical slots with rounded ends through a wall that runs along X."""
    holes = []
    for xc in x_centres:
        lo, hi = z0, z1
        if clip:  # clip to a circle (xc0, zc0, r)
            xc0, zc0, r = clip
            dx = abs(xc - xc0) + width / 2
            if dx >= r - 0.8:
                continue
            h = math.sqrt(r * r - dx * dx)
            lo, hi = zc0 - h, zc0 + h
        if hi - lo < width + 1:
            continue
        sec = CrossSection.square([width, hi - lo - width]).translate([xc - width / 2, lo + width / 2])
        sec = sec + CrossSection.circle(width / 2, SEG).translate([xc, lo + width / 2])
        sec = sec + CrossSection.circle(width / 2, SEG).translate([xc, hi - width / 2])
        # sketch is X/Z; extrude along Y
        holes.append(sec.extrude(y1 - y0).transform(np.array([[1, 0, 0, 0], [0, 0, 1, y0], [0, 1, 0, 0]], float)))
    return union(holes)


def side_grille(y_centres, z0, z1, width, x0, x1):
    """Horizontal-ish slots through the left end wall (runs along Y): slots run along Z."""
    holes = []
    for yc in y_centres:
        sec = CrossSection.square([width, z1 - z0 - width]).translate([yc - width / 2, z0 + width / 2])
        sec = sec + CrossSection.circle(width / 2, SEG).translate([yc, z0 + width / 2])
        sec = sec + CrossSection.circle(width / 2, SEG).translate([yc, z1 - width / 2])
        # sketch is Y/Z; extrude along X
        holes.append(sec.extrude(x1 - x0).transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], float)))
    return union(holes)


# ---------------------------------------------------------------------------------------------
def sensor_box():
    sx, sy, sz = SEN
    y1 = D / 2 - WALL - GAP
    return (SEN_X0, SEN_X0 + sx, y1 - sy, y1, FLOOR, FLOOR + sz)


def build_base():
    inner_top = H - TOP
    outer = prism(rr(W, D, R), FLOOR, H - 1.0) + Manifold.batch_hull([
        prism(rr(W, D, R), H - 1.0, H - 0.99), prism(rr(W - 2, D - 2, R - 1), H - 0.01, H)])
    cavity = prism(rr(W - 2 * WALL, D - 2 * WALL, R - WALL), FLOOR - 1, inner_top)
    base = outer - cavity
    envelope = box(-W, W, -D, D, FLOOR, H)

    # Corner bosses for the bottom plate's screws
    base += union([cyl(x, y, FLOOR, inner_top + 0.01, BOSS_R) for x, y in BOSSES])

    # Trough around the slot, and a block behind it for the spine's socket
    x_lo = -DEV_LENGTH / 2 - CAP_PROUD - 0.7
    trough = sbox(x_lo - SLOT_WALL, DEV_LENGTH / 2 + 0.4 + SLOT_WALL, -SLOT_WALL, SLOT_W + SLOT_WALL,
                  -SLOT_DEPTH - SLOT_WALL, 1.0)
    spine_s0 = DEV_THICK + 0.25 + SPINE_GAP
    socket_block = sbox(SPINE_X[0] - 2.5, SPINE_X[1] + 2.5, SLOT_W, spine_s0 + SPINE_THICK + 2.0,
                        -SOCKET_DEPTH - 2.0, 1.0)
    base += (trough + socket_block) ^ envelope

    # Step the top down in front of the PaperDIY so the base doesn't hide the bottom of the screen
    in_front = sbox(-W, W, -100, 0.5, -100, 100)  # overlaps the slot so no film is left on its face
    z_lip = slot_point(0, 0, -SLOT_DEPTH + FRONT_LIP)[2]
    base -= in_front ^ box(-W, W, -D, D, z_lip, H + 5)
    base += in_front ^ prism(rr(W, D, R), z_lip - TOP, z_lip)


    # --- SEN66 ---
    x0, x1, y0, y1, z0, z1 = sensor_box()
    sx = lambda u: x1 - u        # along the sensor from its inlet end
    sz = lambda v: z0 + v        # across its width
    yb0, yb1 = D / 2 - WALL - 0.5, D / 2 + 0.5
    # Inlet (datasheet u 3.2-10.2, v 2.8-21.2) and fan outlet (20.5 mm circle at u 42.4, v 12.8)
    base -= grille_slots([sx(10.2) + 1.2, sx(6.7), sx(3.2) - 1.2], sz(1.8), sz(23.8), 1.8, yb0, yb1)
    xc, zc = sx(42.4), sz(12.8)
    base -= grille_slots([xc + k * 3.0 for k in range(-4, 5)], 0, 0, 1.8, yb0, yb1, clip=(xc, zc, 10.6))
    # Keeps it against the back wall and off the top
    ribs = [box(x0 + 2, x0 + 5, y0 - 1.6, y0 - 0.2, z0 + 8, inner_top + 0.01),
            box(x1 - 5, x1 - 2, y0 - 1.6, y0 - 0.2, z0 + 8, inner_top + 0.01),
            box(x0 - 1.9, x0 - 0.3, y0 + 2, y1 - 2, z0 + 8, inner_top + 0.01),
            box(x0 + 6, x0 + 16, y0 + 3, y1 - 3, z1 + 0.3, inner_top + 0.01),
            box(x1 - 16, x1 - 6, y0 + 3, y1 - 3, z1 + 0.3, inner_top + 0.01)]
    base += union(ribs)

    # --- SFA40 bay at the left end ---
    xw = -W / 2 + WALL
    half = SFA_W / 2 + 0.3
    lip_x = xw + SFA_T + 0.4
    rails = []
    for sgn in (-1, 1):
        rails.append(box(xw - 0.01, lip_x + 1.2, *sorted((sgn * half, sgn * (half + 1.6))), FLOOR + 0.3, inner_top + 0.01))
        rails.append(box(lip_x, lip_x + 1.2, *sorted((sgn * (half - 1.2), sgn * half)), FLOOR + 0.3, inner_top + 0.01))
    base += union(rails)
    zc = FLOOR + SFA_H / 2
    base -= side_grille([k * 2.8 for k in range(-2, 3)], zc - 5.5, zc + 5.5, 1.6, -W / 2 - 0.5, xw + 0.5)

    # The slot (with room at the left end for the power button cap)
    base -= sbox(-DEV_LENGTH / 2 - 0.4, DEV_LENGTH / 2 + 0.4, 0, SLOT_W, -SLOT_DEPTH, 80)
    base -= sbox(x_lo, -DEV_LENGTH / 2, 0, SLOT_W, -SLOT_DEPTH, 80)
    # Spine socket, open to the slot (a wall between them would be too thin to print)
    base -= sbox(SPINE_X[0] - 0.15, SPINE_X[1] + 0.15, SLOT_W - 0.3, spine_s0 + SPINE_THICK + 0.15,
                 -SOCKET_DEPTH, 80)
    # Cable: groove behind the PaperDIY, then a hole down into the base
    base -= sbox(*CABLE_X, SLOT_W - 0.5, spine_s0 + 1.05, -SLOT_DEPTH, 80)
    base -= sbox(*CABLE_X, 3.5, spine_s0 + 1.05, -SLOT_DEPTH - SLOT_WALL - 1.0, -SLOT_DEPTH + 0.01)
    return base


def build_bottom():
    plate = prism(rr(W, D, R), 0, FLOOR)
    # Locating lips just inside the walls (kept clear of the sensors and bosses)
    xi, yi = W / 2 - WALL - 0.3, D / 2 - WALL - 0.3
    lips = [box(-45, 45, -yi, -yi + 1.2, FLOOR, FLOOR + 2),
            box(xi - 1.2, xi, -12, 12, FLOOR, FLOOR + 2),
            box(-50, -6, yi - 1.2, yi, FLOOR, FLOOR + 2)]
    plate += union(lips)
    for x, y in BOSSES:
        plate -= cyl(x, y, -1, FLOOR + 3, 1.7)
        plate -= Manifold.cylinder(1.65, 3.3, 1.7, circular_segments=SEG).translate([x, y, -0.001])  # countersink
    for x, y in [(sx * (W / 2 - 18), sy * (D / 2 - 12)) for sx in (-1, 1) for sy in (-1, 1)]:
        plate -= cyl(x, y, -1, 0.8, 5.25)  # recesses for 10 mm rubber feet
    return plate


def build_spine():
    """In the slot frame (X, s, t); transform with SLOT_M to put it in place."""
    s0 = DEV_THICK + 0.25 + SPINE_GAP
    spine = box(*SPINE_X, s0, s0 + SPINE_THICK, -SOCKET_DEPTH, SPINE_TOP)
    # Groove for the cable on its front face, deeper where the cable folds out of the case
    spine -= box(*CABLE_X, s0 - 0.1, s0 + 1.05, -SOCKET_DEPTH - 1, SPINE_TOP + 1)
    spine -= box(CABLE_X[0] - 0.5, CABLE_X[1] + 0.5, s0 - 0.1, s0 + 2.0, 38, SPINE_TOP + 1)
    return spine


def oem_to_stand():
    """4x4 transform from M5Stack's STL coordinates to the PaperDIY's place in the stand."""
    # STL: x along the length (left end -84.94), y up the screen (top edge 8.26), z towards the
    # viewer (screen side -0.2, back -6.5). Here it's turned upside down (180 degrees in the screen
    # plane), so X = -x + c, t = -SLOT_DEPTH + 8.26 - y, s = 0.05 - z.
    local = np.array([[-1, 0, 0, OEM_LEFT_X + DEV_LENGTH / 2],
                      [0, 0, -1, 0.05],
                      [0, -1, 0, -SLOT_DEPTH + OEM_TOP_Y],
                      [0, 0, 0, 1]], dtype=float)
    return np.vstack([SLOT_M, [0, 0, 0, 1]]) @ local


def oem_parts(path):
    """M5Stack's assembled case: (bezel, back plate, button caps) in the STL's coordinates."""
    kit = trimesh.load(path)
    parts = [p for p in kit.split(only_watertight=False) if p.bounds[1][1] < 10 and p.bounds[0][1] > -61]
    bezel = [p for p in parts if abs(p.volume - 4238) < 50][0]
    back = [p for p in parts if abs(p.volume - 9049) < 50][0]
    caps = [p for p in parts if p.volume < 100]
    return bezel, back, caps


def backplate_slot_cut():
    x0 = OEM_LEFT_X + BACKPLATE_SLOT['x'][0]
    x1 = OEM_LEFT_X + BACKPLATE_SLOT['x'][1]
    y0 = OEM_TOP_Y - BACKPLATE_SLOT['y'][1]
    y1 = OEM_TOP_Y - BACKPLATE_SLOT['y'][0]
    sec = CrossSection.square([x1 - x0 - 2, y1 - y0 - 2]).translate([x0 + 1, y0 + 1]).offset(1, JoinType.Round, circular_segments=SEG)
    return prism(sec, -7.5, -4.0)


def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))


def from_trimesh(t):
    return Manifold(Mesh(vert_properties=np.asarray(t.vertices, dtype=np.float32),
                         tri_verts=np.asarray(t.faces, dtype=np.uint32)))


def on_bed(t, flip=None):
    t = t.copy()
    if flip is not None:
        t.apply_transform(trimesh.transformations.rotation_matrix(math.pi, flip))
    t.apply_translation(-t.bounds[0])
    return t


def spine_on_bed(spine):
    """Lying on its back face, front (grooved) face up."""
    t = to_trimesh(spine)
    t.apply_transform(np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]], float))  # (X, s, t) -> (X, t, -s)
    t.apply_translation(-t.bounds[0])
    return t


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="stl")
    ap.add_argument("--oem", help="M5Stack's PaperDlY_Complete_Kit.stl: also writes the back plate with the FPC slot")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    base, bottom, spine = build_base(), build_bottom(), build_spine()
    for name, part in (("base", base), ("bottom", bottom), ("spine", spine.transform(SLOT_M))):
        status = part.status()
        print(f"{name}: {part.volume():.0f} mm^3, {part.num_tri()} triangles, {status}, genus {part.genus()}")
    on_bed(to_trimesh(base), flip=[1, 0, 0]).export(os.path.join(args.out, "stand_base.stl"))
    on_bed(to_trimesh(bottom)).export(os.path.join(args.out, "stand_bottom.stl"))
    spine_on_bed(spine).export(os.path.join(args.out, "stand_spine.stl"))

    if args.oem:
        _, back, _ = oem_parts(args.oem)
        plate = from_trimesh(back) - backplate_slot_cut()
        to_trimesh(plate).export(os.path.join(args.out, "back_plate_fpc.stl"))
        print("back plate: slot cut,", f"{back.volume - plate.volume():.0f} mm^3 removed")


if __name__ == "__main__":
    main()
