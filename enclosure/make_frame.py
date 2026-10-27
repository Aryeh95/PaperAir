"""
Generates the slim frame for PaperAir: the bare PaperDIY panel and board (no M5Stack case) in a
thin printed frame that leans back 13 degrees, with a foot at the bottom-back that holds the
SEN66, the optional SFA40, the FPC breakout and the battery.

Parts (all print without supports):
  frame_bezel.stl     front face down. The printed lip holds the glass; the board's buttons,
                      USB-C and power switch line up with openings in its top and right walls.
  frame_back.stl      the back plate and the sensor foot, split face (the flat ring) down.
  frame_caps.stl      three button caps (G3, G4 and power), standing on their ends.
  frame_backer.stl    a flat plate behind the panel, which is flexible: it holds it flat against the
                      lip. Printed flat; its edges clip into grooves in the bezel.
  frame_fit_test.stl  the first 2.4 mm of the bezel all round, plus its top right corner at full
                      depth: shows the lip, the pocket, the buttons and the USB-C opening in
                      about an hour.

The PaperDIY sits upside down compared with how it ships, so its buttons are on top: set
kFlipScreen = true in include/config.h.

Coordinates (mm), "local" frame: X to the right seen from the front, Y down the screen (0 = the
top edge of the glass), Z into the frame (0 = the front face). The frame is then leaned back and
cut level where it meets the desk.

Usage:
  pip install trimesh manifold3d numpy
  python make_frame.py --out stl/
"""

import argparse
import math
import os

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold, Mesh, OpType

SEG = 48

# --- Panel and board. From M5Stack's dimension drawings and case model (K159-B in M5_Hardware),
# checked against the fit tests; see enclosure/README.md ---
GLASS_W, GLASS_H = 110.5, 62.0       # ED047TC1 glass, without the flex stub at its left end (calipers; drawing 110.54 x 62.5)
ACTIVE_W, ACTIVE_H = 105.02, 59.72   # the picture
ACTIVE_X = 1.49                      # from the glass's left end: the wide border (4.0) is at the board end
ACTIVE_Y = (GLASS_H - ACTIVE_H) / 2  # centred across
STACK = 4.7                          # glass plus board, front of glass to back of board (calipers)
GLASS_T = 1.2
# The board sits behind the right-hand end of the glass and reaches past it. Along the frame it is
# located by its end, which rests against the right-hand wall; across, by its two nubs.
BOARD_END = GLASS_W + 4.46           # the board's end, past the glass's right-hand edge
BOARD_X0 = BOARD_END - 51.4          # its inner edge (fit test)
BOARD_W = 61.15                      # across, without the nubs; centred on the glass
BOARD_TOP = (GLASS_H - BOARD_W) / 2
# Board edge features, in this orientation (buttons on top), from the board's end and top edge
KEYS_X = (BOARD_END - 42.0, BOARD_END - 30.0)   # G4, G3 centres along the top edge
KEY_TIP_Y = -1.0                     # how far the button actuators reach above the glass edge
KEY_Z = 2.4                          # their centre, measured from the front face (fit test v7)
USB_Y, USB_Z = BOARD_TOP + 6.98, 3.4   # USB-C centre on the right end
USB_TIP_X = BOARD_END + 2.05         # its socket stands out past the board
PWR_Y, PWR_Z = BOARD_TOP + BOARD_W - 7.1, KEY_Z   # power switch centre on the right end (fit test v7)
PWR_TIP_X = BOARD_END - 0.6
NUBS_X = (BOARD_END - 8.4, BOARD_END - 5.4)   # two nubs, one on each long edge, near the right end
NUB_OUT = (64.2 - GLASS_H) / 2       # how far they reach beyond the glass: 64.2 mm across both
FPC_EXIT = (BOARD_X0, 7.0, 16.0)     # FPC-I2S cable leaves from under the board's inner edge, Y range
FLEX_Y = (9.0, 16.0)                 # the panel's own flex strip runs along its back, tab to board

# --- Backing plate: the panel is flexible, so a flat plate behind it (left of the board) keeps it
# flat against the lip. Its top and bottom edges clip into grooves in the bezel. ---
BACKER_T = 1.2
BACKER_X = (0.5, BOARD_X0 - 1.5)     # stops short of the board's connectors
GROOVE = 0.6

# --- Frame ---
LIP = 1.0                            # thickness of the printed lip in front of the glass
CLEAR = 0.25                         # around the glass in its pocket
WINDOW_MARGIN = 0.15                 # window opening beyond the picture on each side, at the back of the lip
CHAMFER = 0.5                        # the lip's front edge, so the window doesn't shadow the picture
X0, X1 = -5.0, BOARD_END + 3.2       # outer: room for the folded flex tab on the left, USB-C on the right
STOP = 4.0                           # stops at the glass's corners hold it in place along X; the flex
                                     # tab and its fold are between the stops at the left end
Y0, Y1 = -4.0, GLASS_H + 4.0
RADIUS = 3.0
WALL = 1.6
ZS = 7.4                             # where the bezel and the back meet
T_THIN = ZS + 1.6                    # thickness of the screen part
TILT = math.radians(13)

# --- Foot (part of the back) ---
SEN = (55.0, 26.0, 21.2)             # SEN66: length (X), width (Y), depth (Z)
SEN_X0 = 4.0
SEN_Z0 = GLASS_T + LIP + 3.3         # in front of it: the glass, the flex and 3 mm for its wires
FOOT_X0, FOOT_X1 = 0.0, X1 - 5.0        # inset from the frame's sides, leaving room for the corner screws
FLOOR = 2.4                          # foot floor thickness (rubber feet sit in it)
T_FOOT = SEN_Z0 + SEN[2] + 0.2 + WALL
SFA = (17.5, 15.0, 4.0)              # SFA40 module: X, Y, Z
SFA_X0 = FOOT_X1 - WALL - 0.4 - SFA[0]

# --- Screws: 4 x M2 x 6 (thread-forming / PT, or plain M2 into the printed hole) ---
SCREW_PILOT = 1.7
SCREW_CLEAR = 2.4
SCREWS = [(X0 + 2.5, Y0 + 2.3), (X1 - 2.5, Y0 + 2.3), (X0 + 2.5, GLASS_H - 1.5), (X1 - 2.5, GLASS_H - 0.5)]
COLUMN_R = 2.3

# --- Button caps ---
CAP_FACE = (5.0, 2.4)                # the part you press: length along the edge x height (Z)
CAP_FLANGE = (5.0, 3.6, 0.8)       # same length as the face, so a cap prints standing on its end
CAP_PROUD = 0.6                      # how far it stands out of the frame


# ---------------------------------------------------------------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def rr(x0, x1, y0, y1, r):
    r = min(r, (x1 - x0) / 2 - 0.01, (y1 - y0) / 2 - 0.01)
    return CrossSection.square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]).translate([x0 + r, y0 + r]).offset(
        r, JoinType.Round, circular_segments=SEG)


def prism(section, z0, z1):
    return section.extrude(z1 - z0).translate([0, 0, z0])


def zcyl(x, y, z0, z1, r):
    return Manifold.cylinder(z1 - z0, r, circular_segments=SEG).translate([x, y, z0])


def union(parts):
    return Manifold.batch_boolean(parts, OpType.Add)


# local -> world: lean back, then put the bottom front edge on the desk at Y = 0
_d = np.array([0, -math.sin(TILT), -math.cos(TILT)])   # down the face
_n = np.array([0, math.cos(TILT), -math.sin(TILT)])    # into the frame
_O = -Y1 * _d
TO_WORLD = np.c_[[1, 0, 0], _d, _n, _O]
TO_LOCAL = np.linalg.inv(np.vstack([TO_WORLD, [0, 0, 0, 1]]))[:3]


def world(m):
    return m.transform(TO_WORLD)


def local(m):
    return m.transform(TO_LOCAL)


def above_desk(m, z=0.0):
    """Cuts a local-frame part level with the desk (world Z >= z), returned in the local frame."""
    return local(world(m) ^ box(-1000, 1000, -1000, 1000, z, 1000))


# ---------------------------------------------------------------------------------------------
def outline(z0, z1):
    return prism(rr(X0, X1, Y0, Y1, RADIUS), z0, z1)


def roofed(x0, x1, y0, y1, z0, z1, span_x=True):
    """A box with a 45 degree roof over its span (X or Y), so its top prints with a short bridge
    when the bezel is face down. The roof stops 1 mm short of the back of the bezel."""
    rise = max(0.0, min((x1 - x0 if span_x else y1 - y0) / 2 - 1.0, ZS - 1.0 - z1))
    if span_x:
        top = box(x0 + rise, x1 - rise, y0, y1, z1, z1 + rise)
    else:
        top = box(x0, x1, y0 + rise, y1 - rise, z1, z1 + rise)
    return Manifold.batch_hull([box(x0, x1, y0, y1, z0, z1), top])


def key_cap_cut(x):
    """Opening and flange recess in the top wall for one cap. The recess is open at the back,
    so it needs no bridge; the hole holds the cap in Z."""
    fx, fy, ft = CAP_FLANGE
    hole = roofed(x - CAP_FACE[0] / 2 - 0.2, x + CAP_FACE[0] / 2 + 0.2, Y0 - 1, Y0 + 2.2,
                  KEY_Z - CAP_FACE[1] / 2 - 0.2, KEY_Z + CAP_FACE[1] / 2 + 0.2)
    recess = box(x - fx / 2 - 0.3, x + fx / 2 + 0.3, Y0 + 1.7, 0, KEY_Z - fy / 2 - 0.3, ZS + 1)
    notch = box(x - 2.5, x + 2.5, KEY_TIP_Y - 0.3, 0, KEY_Z - 1.8, ZS + 1)
    return hole + recess + notch


def pwr_cap_cut():
    fx, fy, ft = CAP_FLANGE
    inner = X1 - 2.8
    hole = roofed(X1 - 1.8, X1 + 1, PWR_Y - CAP_FACE[0] / 2 - 0.2, PWR_Y + CAP_FACE[0] / 2 + 0.2,
                  PWR_Z - CAP_FACE[1] / 2 - 0.2, PWR_Z + CAP_FACE[1] / 2 + 0.2, span_x=False)
    recess = box(PWR_TIP_X + 0.2, X1 - 1.7, PWR_Y - fx / 2 - 0.3, PWR_Y + fx / 2 + 0.3,
                 PWR_Z - fy / 2 - 0.3, ZS + 1)
    return hole + recess


def build_bezel():
    bez = outline(0, ZS)
    # The window, and behind it the pocket for the glass and board
    wx0, wx1 = ACTIVE_X - WINDOW_MARGIN, ACTIVE_X + ACTIVE_W + WINDOW_MARGIN
    wy0, wy1 = ACTIVE_Y - WINDOW_MARGIN, ACTIVE_Y + ACTIVE_H + WINDOW_MARGIN
    bez -= Manifold.batch_hull([box(wx0 - CHAMFER, wx1 + CHAMFER, wy0 - CHAMFER, wy1 + CHAMFER, -1, 0),
                                box(wx0, wx1, wy0, wy1, CHAMFER, LIP + 0.01)])
    bez -= box(X0 + WALL, X1 - 2.8, -CLEAR, GLASS_H + CLEAR, LIP, ZS + 1)
    # Corner columns for the screws that hold the back on
    cols = [zcyl(x, y, LIP, ZS, COLUMN_R) for x, y in SCREWS]
    bez += union(cols)
    bez -= box(-CLEAR, GLASS_W + CLEAR, -CLEAR, GLASS_H + CLEAR, LIP, ZS + 1)   # keep the pocket clear
    # The nubs locate the board across; their recesses are long, since the end wall locates it along
    bez -= box(NUBS_X[0] - 1.0, NUBS_X[1] + 1.0, -NUB_OUT - 0.2, GLASS_H + NUB_OUT + 0.2, LIP, ZS + 1)
    # Stops at the glass's four corners, so it can't slide along X. At the right-hand end they are
    # only as thick as the glass, in front of the board.
    for y0, y1 in ((-CLEAR, STOP), (GLASS_H - STOP, GLASS_H + CLEAR)):
        bez += box(X0 + WALL - 0.1, -CLEAR, y0, y1, LIP - 0.01, ZS)
    # Short at the right-hand end: the USB-C socket is 2.5 mm from the top edge
    for y0, y1 in ((-CLEAR, 1.5), (GLASS_H - 1.5, GLASS_H + CLEAR)):
        bez += box(GLASS_W + CLEAR, GLASS_W + 1.5, y0, y1, LIP - 0.01, LIP + GLASS_T - 0.1)
    bez -= union([zcyl(x, y, ZS - 5.5, ZS + 1, SCREW_PILOT / 2) for x, y in SCREWS])
    # Grooves for the backing plate's edges
    zb0 = LIP + GLASS_T
    for y0, y1 in ((-CLEAR - GROOVE, -CLEAR + 0.01), (GLASS_H + CLEAR - 0.01, GLASS_H + CLEAR + GROOVE)):
        bez -= box(BACKER_X[0] - 0.5, BACKER_X[1] + 0.5, y0, y1, zb0 - 0.05, zb0 + BACKER_T + 0.3)
    # Buttons on top, power on the right end, USB-C on the right end
    bez -= union([key_cap_cut(x) for x in KEYS_X])
    bez -= pwr_cap_cut()
    bez -= usb_cut()
    return above_desk(bez)


def usb_cut():
    front = 0.6   # keep a strip of wall along the front face
    usb = box(X1 - 3.0, X1 + 1, USB_Y - 4.8, USB_Y + 4.8, max(USB_Z - 3.5, front), ZS + 1.0)   # open at the back, so the board drops in
    plug = box(X1 - 1.2, X1 + 1, USB_Y - 6.3, USB_Y + 6.3, max(USB_Z - 4.3, front), USB_Z + 4.3)   # room for the plug's moulding
    return usb + plug


def foot_outer():
    step_y = SEN_Y0() - WALL - 0.3
    lower = prism(rr(FOOT_X0, FOOT_X1, step_y, Y1 + 30, 2.0), ZS, T_FOOT)
    upper = prism(rr(FOOT_X0, FOOT_X1, step_y - 4.5, step_y - 4.0, 0.2), ZS, T_THIN)
    return Manifold.batch_hull([lower, upper])


def SEN_Y0():
    """The SEN66's top edge, as high as needed for its bottom corner to clear the floor."""
    z_back = SEN_Z0 + SEN[2]
    # the desk cut in the local frame: y = Y1 - tan(tilt) * z
    y_floor_back = Y1 - math.tan(TILT) * z_back - FLOOR / math.cos(TILT)
    return y_floor_back - 0.5 - SEN[1]


def sensor_box():
    y0 = SEN_Y0()
    return (SEN_X0, SEN_X0 + SEN[0], y0, y0 + SEN[1], SEN_Z0, SEN_Z0 + SEN[2])


def sfa_box():
    y1 = SEN_Y0() + SEN[1] - 2
    return (SFA_X0, SFA_X0 + SFA[0], y1 - SFA[1], y1, T_FOOT - WALL - SFA[2] - 0.2, T_FOOT - WALL - 0.2)


def grille(xs, y0, y1, w=1.8):
    holes = []
    for x in xs:
        sec = CrossSection.square([w, y1 - y0 - w]).translate([x - w / 2, y0 + w / 2])
        sec = sec + CrossSection.circle(w / 2, SEG).translate([x, y0 + w / 2]) + CrossSection.circle(w / 2, SEG).translate([x, y1 - w / 2])
        holes.append(prism(sec, T_FOOT - WALL - 1, T_FOOT + 1))
    return union(holes)


def build_back():
    plate = outline(ZS, T_THIN)
    back = plate + foot_outer()
    step_y = SEN_Y0() - 0.3
    inner_lower = prism(rr(FOOT_X0 + WALL, FOOT_X1 - WALL, step_y, Y1 + 30, 0.8), ZS - 1, T_FOOT - WALL)
    inner_upper = prism(rr(FOOT_X0 + WALL, FOOT_X1 - WALL, step_y - 4.5, step_y - 4.0, 0.2), ZS - 1, T_THIN - WALL)
    inner = Manifold.batch_hull([inner_lower, inner_upper])
    inner = local(world(inner) ^ box(-1000, 1000, -1000, 1000, FLOOR, 1000))
    back -= inner
    # Screw holes, counterbored for the heads
    for x, y in SCREWS:
        back -= zcyl(x, y, ZS - 1, T_THIN + 1, SCREW_CLEAR / 2)
        back -= zcyl(x, y, T_THIN - 1.2, T_THIN + 1, 2.1)
    # SEN66: inlet (datasheet u 3.2-10.2) at its left end, fan outlet (20.5 mm circle at u 42.4)
    x0, x1, y0, y1, z0, z1 = sensor_box()
    back -= grille([x0 + 3.2 + 1.0, x0 + 6.7, x0 + 10.2 - 1.0], y0 + 2.8, y1 - 2.8)
    xc, yc, r = x0 + 42.4, y0 + 12.8, 10.6
    slots = []
    for k in range(-3, 4):
        x = xc + 3.0 * k
        h = math.sqrt(max(0.0, r * r - (abs(x - xc) + 0.9) ** 2))
        if h > 2:
            slots.append(grille([x], yc - h, yc + h))
    back -= union(slots)
    # Crush ribs that hold the SEN66: two above it, two below, one at its left end
    ribs = [box(x0 + 8, x0 + 10, y0 - 1.0, y0 + 0.15, ZS, z1),
            box(x1 - 10, x1 - 8, y0 - 1.0, y0 + 0.15, ZS, z1),
            box(FOOT_X0 + WALL - 0.5, x0 + 0.15, y0 + 8, y0 + 10, ZS, z1),
            box(x0 + 8, x0 + 10, y1 - 0.15, y1 + 2.0, ZS, z1),
            box(x1 - 10, x1 - 8, y1 - 0.15, y1 + 2.0, ZS, z1)]
    back += above_desk(union(ribs), FLOOR - 0.1)
    # SFA40 grille and the fins that locate it (it's stuck to the inside of its grille with foam tape)
    sx0, sx1, sy0, sy1, sz0, sz1 = sfa_box()
    back -= grille([sx0 + 4 + 2.8 * k for k in range(4)], sy0 + 3, sy1 - 3, 1.5)
    fins = box(sx0 - 1.6, sx0 - 0.3, sy0, sy1, ZS, T_FOOT - WALL)
    back += fins
    # The USB-C opening reaches into the back plate
    back -= usb_cut()
    # Level with the desk, and recesses for 10 mm rubber feet (2 mm thick, they stand 0.6 mm proud)
    w = world(back) ^ box(-1000, 1000, -1000, 1000, 0, 1000)
    for fx in (FOOT_X0 + 8, FOOT_X1 - 8):
        for fy in (12.0, 23.0):
            w -= Manifold.cylinder(1.4, 5.25, circular_segments=SEG).translate([fx, fy, -0.01])
    return local(w)


def cap(stem_len, nub_len):
    """A button cap: flange at z 0..ft, the stem you press above it, a ridge below that presses the
    switch. The same cross-section along its whole length (x), so it prints standing on its end
    with no overhangs. The flange catches above and below the hole (y), which keeps it in."""
    fx, fy, ft = CAP_FLANGE
    cw, ch = CAP_FACE
    flange = box(-cw / 2, cw / 2, -fy / 2, fy / 2, 0, ft)
    stem = box(-cw / 2, cw / 2, -ch / 2, ch / 2, ft, ft + stem_len)
    ridge = box(-cw / 2, cw / 2, -1.0, 1.0, -nub_len, 0)
    return flange + stem + ridge


# The top wall: outer face Y0, flange recess from Y0 + 1.7. The right wall: outer X1, recess to X1 - 1.7.
KEY_NUB = (KEY_TIP_Y - 0.15) - (Y0 + 1.7 + CAP_FLANGE[2])     # flange inner face to 0.15 mm short of the actuator
PWR_NUB = (X1 - 1.7 - CAP_FLANGE[2]) - (PWR_TIP_X + 0.15)


def build_caps():
    """Two for the top buttons (G4, G3) and one, with the longer ridge, for the power switch, each
    standing on its end for printing."""
    parts = [cap(1.7 + CAP_PROUD, KEY_NUB), cap(1.7 + CAP_PROUD, KEY_NUB), cap(1.7 + CAP_PROUD, PWR_NUB)]
    upright = [p.rotate([0, 90, 0]).translate([0, i * (CAP_FLANGE[1] + 5), 0]) for i, p in enumerate(parts)]
    return union(upright)


def build_backer():
    """In place behind the glass; printed flat (front face down)."""
    z0 = LIP + GLASS_T + 0.05
    # Its edges reach 0.3 mm into the grooves, with a bevel on the side that goes in first, so it
    # flexes past the pocket's edge and clicks in.
    y0, y1 = -CLEAR - 0.3, GLASS_H + CLEAR + 0.3
    plate = Manifold.batch_hull([box(BACKER_X[0], BACKER_X[1], y0 + 0.5, y1 - 0.5, z0, z0 + 0.01),
                                 box(BACKER_X[0], BACKER_X[1], y0, y1, z0 + 0.5, z0 + BACKER_T)])
    plate -= box(BACKER_X[0] - 1, BACKER_X[1] + 1, FLEX_Y[0] - 1, FLEX_Y[1] + 1, z0 - 1, z0 + 0.5)   # clear of the flex strip
    return plate


def build_fit_test(bezel):
    ring = bezel ^ box(-1000, 1000, -1000, 1000, -1, 2.4)
    corner = bezel ^ (box(KEYS_X[0] - 8, 1000, -1000, 12, -1, 100) + box(X1 - 12, 1000, -1000, 1000, -1, 100))
    return ring + corner


# ---------------------------------------------------------------------------------------------
def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))


def on_bed(m, flip=False):
    t = to_trimesh(m)
    if flip:
        t.apply_transform(trimesh.transformations.rotation_matrix(math.pi, [1, 0, 0]))
    t.apply_translation(-t.bounds[0])
    return t


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="stl")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    bezel, back, caps, backer = build_bezel(), build_back(), build_caps(), build_backer()
    fit = build_fit_test(bezel)
    for name, part, flip in (("frame_bezel", bezel, False), ("frame_back", back, False),
                             ("frame_caps", caps, False), ("frame_backer", backer, False), ("frame_fit_test", fit, False)):
        print(f"{name}: {part.volume():.0f} mm^3, {part.status()}, genus {part.genus()}")
        on_bed(part, flip).export(os.path.join(args.out, name + ".stl"))
    w = world(bezel + back)
    bb = w.bounding_box()
    print(f"assembled: {bb[3] - bb[0]:.1f} wide, {bb[4] - bb[1]:.1f} deep, {bb[5] - bb[2]:.1f} tall")


if __name__ == "__main__":
    main()
