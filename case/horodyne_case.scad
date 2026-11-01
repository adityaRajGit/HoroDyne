// === DESCRIPTION ===
// Horodyne watch case: a one-piece wrist-watch case for the Waveshare
// ESP32-S3-Touch-LCD-1.69 module plus a 20x25x4 mm LiPo pouch cell, worn
// LANDSCAPE (the module's long side runs along the arm, straps leave the long
// edges). Takes standard 20 mm straps on spring bars through lug holes.
//
// Physical context: worn on the wrist (sweat, body heat, the odd knock). The
// module drops in from the front and its cover glass rests on two ledges at
// the short ends; the battery sits on the case floor under the board. The
// module's USB-C port faces -X and its three side buttons face +X, i.e. both
// sit on the strap edges, which drove most of the design.
//
// Design decisions:
//   - One part, no screws: the module is a drop-in (0.25 mm per side) centred
//     in X by four crush ribs on the glass band; fix it with dots of
//     B-7000/E6000 or thin foam tape on the ledges. Pry notches at both short
//     ends let a plastic pick under the glass to remove it.
//   - Lugs are LOW (4.5 mm tall from the back) and the spring bar sits 1.5 mm
//     above the back, so the strap end stays below a full-size USB-C plug and
//     the side buttons stay reachable above it. Strap ends up to 4 mm thick
//     (strap_end_r) clear the plug; thicker silicone may need the strap popped
//     off one side to charge. strap_style = "nato" raises the bars so a
//     1.5 mm NATO slides under them (a NATO never wraps over the bar, so the
//     plug still clears); export it with -D 'strap_style="nato"'.
//   - Buttons are conventional pushers: three small printed caps slide through
//     rounded holes in the +X wall, a flange inside stops them falling out,
//     and the module's own tactile switches push them back. Fit the pushers
//     from inside, push them fully out, then drop the module in.
//   - The USB opening is the plug-overmold size (USB-C max 12.35 x 6.5 mm,
//     rounded): the receptacle face sits 2.4 mm behind the outer wall, and a
//     fully inserted plug's overmold ends within ~0.5 mm of that face, so a
//     port-sized hole would stop most cables before they latch.
//   - The battery sits 3 mm right of the board centre: the USB receptacle,
//     battery connector and one tall part hang 1.3-2.3 mm lower on the left.
//     Centring it would add ~2.3 mm of thickness.
//   - Printed back-down: the open top needs no support, the lugs stand on
//     the plate, the horizontal holes are teardrops/chamfered.
//
// Terminology -> code:
//   "glass pocket / fit"    -> fit_clear, cavity()
//   "ledges"                -> ledge_y0, ledge_y1 (glass rests at glass_z0)
//   "crush ribs"            -> rib_*, ribs()
//   "pry notches"           -> pry_w, pry_depth, pry_cut()
//   "lugs", "lug holes"     -> lug_t, lug_h, bar_off, bar_z, bar_tip_d, lugs(), bar_holes()
//   "strap width"           -> strap_w, strap_clear
//   "USB opening"           -> plug_w, plug_h, plug_r, plug_clear, usb_cut()
//   "buttons / pushers"     -> btn_y, cap_*, flange_*, pusher_clear, pusher_shape(), pusher_holes()
//   "battery"               -> bat_*, battery_stops()
//   "thickness"             -> floor_t, glass_proud, bat_swell
//
// Common modifications:
//   Different strap width    -> strap_w (gap = strap_w + 2*strap_clear)
//   Module too tight/loose   -> fit_clear (0.2..0.3), rib_crush
//   Thicker battery          -> bat_t (case grows by the same amount)
//   Pushers loose/sticky     -> pusher_clear (0.2..0.35)
//   Pushers stick out more   -> cap_out (travel must stay < cap_out)
//   Pusher misses a button   -> btn_y entries (module Y coordinates)
//   Bigger spring-bar tips   -> bar_tip_d (hole = tip + hole_comp)
//
// Overall dimensions: ~46.4 (X, lug tip to lug tip, across the wrist)
//   x 45.2 (Y, along the arm) x 14.5 mm case + 0.6 mm glass proud.
// Coordinate system: X/Y = the module's own STEP coordinates (glass centre at
//   y = glass_cy), Z = height from build plate (Z=0 = case back).
// NOTE: Model is in print orientation: back on the plate, open top up.
//   Internally geometry is built in module Z and shifted by -zb.
// Real-mesh fit check: python ref/check_fit.py ref/module.stl

// === PRINT SETTINGS ===
// Material: PETG (sweat/body heat, tough lugs; PLA softens in a hot car).
// Parts: part="case" x1, part="plate_pushers" (3 pushers + 1 spare).
// Layer Height: 0.2mm (0.12-0.16 nicer for the glass rim)
// Walls/Perimeters: 4 (1.8mm, = the full wall, so walls print solid)
// Infill: 100% (part is mostly walls; ensures solid lugs and pushers)
// Supports: None required (teardrop lug holes; the USB and pusher holes have
//   short flat-topped bridges: USB 9.6 mm with 45-deg teardrop corners,
//   pushers 1.6 mm with small corner arcs)
// Orientation: As modeled: case back on the plate, open top up; pushers
//   flange down, cap up.
// Notes: 240C nozzle, 75-80C bed, fan 30-40%, bridges 80-100%. Dry PETG
//   first. Elephant-foot compensation 0 in the slicer (bottom edges are
//   chamfered). Pushers must slide freely: ream the holes with a 2 mm drill
//   or file if they print tight. Keep glue off the +X (button) wall: it
//   wicks into the pusher holes. Bridges: fan 100%, slow bridge speed.

// === PARAMETERS ===
// Printer settings (print service, generic FDM)
nozzle_diameter = 0.4;
layer_height = 0.2;

// Module geometry, measured from ESP32-S3-Touch-LCD-1_69.stp (module coords)
glass_w   = 33.15;   // cover glass X width
glass_y0  = -21.83;  // cover glass Y extents
glass_y1  = 19.28;
glass_r   = 7.6;     // cover glass corner radius
glass_z0  = 2.0;     // glass underside (rests on the ledges)
glass_top = 3.81;    // glass top
lcd_y0    = -20.51;  // LCD body under the glass, Y extents
lcd_y1    = 17.49;
lcd_hx    = 15.03;   // LCD body half width
parts_low = -4.6;    // lowest underside part above the battery area
deep_low  = -6.9;    // lowest part overall (battery connector)
deep_x1   = -8.41;   // deep parts all lie at x < deep_x1
usb_cy    = -1.015;  // USB-C receptacle centre Y
usb_cz    = -4.22;   // USB-C receptacle centre Z
btn_face_x = 15.57;  // side-button actuator faces (x)
btn_cz    = -3.42;   // actuator centre Z
btn_y     = [-9.77, -0.85, 8.06];  // actuator centres Y
// Envelope boxes [x0,y0,z0, x1,y1,z1, corner_r] of the module below the glass (STEP probe)
module_boxes = [
    [-lcd_hx, lcd_y0, 0,      lcd_hx, lcd_y1, glass_z0, 5.18], // LCD body
    [-14.93, -19.76, -2.5,    14.93, 17.38, -1.5, 4.63],       // board
    [-14.0,  -20.31, -1.5,    14.03, 12.0,  0, 4.63],          // FPC / shield
    [-14.03, -16.63, parts_low, 14.37, 17.24, -2.5],           // underside parts
    [11.9,   -12.05, -4.3,    btn_face_x, -7.48, -2.1],        // button 1
    [11.9,   -3.13,  -4.3,    btn_face_x, 1.43,  -2.1],        // button 2
    [11.9,   5.79,   -4.3,    btn_face_x, 10.34, -2.1],        // button 3
    [-16.23, -5.81,  -5.85,   -8.70, 3.78,  -1.69],            // USB-C
    [-11.41, -16.63, deep_low, -8.41, -12.33, -2.5],           // battery connector
    [-13.92, 4.97,   -6.0,    -8.72, 12.62, -2.6]              // deep part 2
];

// Battery (LiPo pouch, user-measured)
bat_w     = 20;      // along X
bat_l     = 25;      // along Y
bat_t     = 4;
bat_swell = 0.4;     // allowance for pouch swell
bat_top_gap = 0.3;   // min gap battery(swollen) -> parts above
bat_cx    = 3.0;
bat_cy    = -1.3;
bat_clear = 0.4;     // locating-stop clearance
stop_h    = 1.2;     // locating-stop height
stop_leg  = 3.0;     // locating-stop leg length

// Strap / lugs
strap_w     = 20;    // strap / spring-bar width
strap_clear = 0.15;  // per side
lug_t       = 3.5;   // lug thickness (Y)
lug_h       = 4.5;   // lug height from the back
bar_off     = 3.0;   // spring-bar axis, out from the case wall
lug_tip     = 1.6;   // lug material beyond the bar axis
strap_style = "standard"; // "standard" (two-piece strap) or "nato" (one strap under the case)
nato_t      = 1.5;   // NATO strap thickness it must slide under the bars
nato_clear  = 0.1;
bar_d       = 1.8;   // spring-bar body diameter
bar_z       = strap_style == "nato" ? nato_t + nato_clear + bar_d / 2
                                    : 1.5;   // spring-bar axis above the back
bar_tip_d   = 0.9;   // spring-bar tip diameter
strap_end_r = 2.0;   // strap end radius around the bar (<= 4 mm thick strap)
lug_flare   = 0.8;   // 45-deg root flare (fillet) on both lug sides
lug_round   = 0.8;   // lug tip corner radius

// USB-C plug (spec max overmold 12.35 x 6.5)
plug_w      = 12.35;
plug_h      = 6.5;
plug_clear  = 0.25;
plug_r      = 2.0;   // overmold corner radius
usb_r       = 2.75;  // opening corner radius (fits plug_r + plug_clear, shortens the top bridge)

// Case fit
fit_clear    = 0.25; // module/glass clearance per side
glass_proud  = 0.6;  // glass top above the rim
floor_t      = 2.0;  // case back thickness
back_chamfer = 1.0;  // bottom outer edge (also elephant foot)
top_chamfer  = 0.6;  // top outer edge
lead_in      = 0.4;  // top inner edge chamfer
floor_fillet = 0.8;  // floor/wall internal chamfer
rib_d        = 0.8;  // crush rib diameter
rib_crush    = 0.05; // rib interference with the glass edge, per side
rib_dy       = 8.0;  // rib offset from glass centre along Y
pry_w        = 6.0;  // pry notch width
pry_depth    = 1.0;  // pry notch depth below the rim

// Button pushers (separate caps through the +X wall)
cap_w        = 3.2;  // cap width (Y)
cap_h        = 2.0;  // cap height (Z)
cap_r        = 0.8;  // cap corner radius
cap_out      = 1.0;  // cap stands out of the wall at rest
cap_chamfer  = 0.3;  // cap outer edge
flange_t     = 1.0;  // flange thickness (X), inside the wall
cap_root     = 0.2;  // 45-deg root fillet, cap to flange (< pusher_clear)
flange_m     = 0.7;  // flange overlap beyond the hole, each side
pusher_clear = 0.25; // sliding clearance per side (PETG)
btn_travel   = 0.25; // tactile switch travel

// === DERIVED CONSTANTS ===
extrusion_width = nozzle_diameter * 1.125;
wall_thickness = extrusion_width * 4;  // 4 perimeters
fudge = 0.01;
hole_comp = 0.30;    // FDM hole undersize compensation
ef_chamfer = 0.4;
$fn = $preview ? 32 : 64;

glass_cy = (glass_y0 + glass_y1) / 2;
cav_hx = glass_w / 2 + fit_clear;
cav_y0 = glass_y0 - fit_clear;
cav_y1 = glass_y1 + fit_clear;
cav_r  = glass_r + fit_clear;
out_hx = cav_hx + wall_thickness;
out_y0 = cav_y0 - wall_thickness;
out_y1 = cav_y1 + wall_thickness;
out_r  = cav_r + wall_thickness;
ledge_y0 = lcd_y0 - fit_clear;
ledge_y1 = lcd_y1 + fit_clear;
rib_proud = fit_clear + rib_crush;

floor_top = parts_low - bat_top_gap - bat_swell - bat_t;
zb        = floor_top - floor_t;          // case back, in module Z
wall_top  = glass_top - glass_proud;
case_h    = wall_top - zb;

lug_in   = strap_w / 2 + strap_clear;
lug_out  = lug_in + lug_t;
bar_x    = out_hx + bar_off;
lug_x1   = bar_x + lug_tip;
bar_hole_d = bar_tip_d + hole_comp;

usb_w = plug_w + 2 * plug_clear;
usb_h = plug_h + 2 * plug_clear;
usb_bridge = usb_w - 2 * usb_r + 2 * usb_r * (sqrt(2) - 1);   // flat top incl. 45-deg teardrop corners
// a plug_r overmold corner, plug_clear off the edges, stays inside the usb_r corner
usb_corner_fit = usb_r - (sqrt(2) * (usb_r - plug_r - plug_clear) + plug_r);
plug_bottom = usb_cz - plug_h / 2;
// highest strap material near the USB plug: wrapped strap end, or (NATO) the bar itself
strap_top   = zb + bar_z + (strap_style == "nato" ? bar_d / 2 : strap_end_r);

hole_w   = cap_w + 2 * pusher_clear;          // pusher hole through the wall
hole_h   = cap_h + 2 * pusher_clear;
hole_r   = cap_r + pusher_clear;
flange_w = hole_w + 2 * flange_m;
flange_h = hole_h + 2 * flange_m;
cap_len  = wall_thickness + cap_out;         // cap length beyond the flange
pusher_in_x = cav_hx - flange_t;             // pusher inner face at rest
pusher_gap  = pusher_in_x - btn_face_x;      // pusher -> button at rest
pusher_travel = pusher_gap + btn_travel;     // press depth to actuate

// === CONTRACTS ===
assert(abs(wall_thickness / extrusion_width - round(wall_thickness / extrusion_width)) < 1e-6, "wall not whole perimeters");
assert(2 * lug_in >= strap_w + 0.3 - 1e-6, "lug gap too tight for the strap");
assert(strap_top < plug_bottom, "strap end would hit a full-size USB-C plug");
assert(strap_style == "standard" || strap_style == "nato", "strap_style must be standard or nato");
assert(strap_style == "nato" || lug_h > bar_z + strap_end_r, "lug lower than the strap end");
assert(bar_z + bar_hole_d / sqrt(2) + 1.0 < lug_h, "too little lug above the bar hole");
assert(strap_style == "standard" || bar_z - bar_d / 2 >= nato_t + nato_clear - 1e-6, "NATO strap can't pass under the bars");
assert(usb_cz - usb_h / 2 > floor_top + 1, "USB opening reaches the floor");
assert(usb_bridge <= 10, "USB opening top bridge too long for PETG");
assert(usb_corner_fit >= 0, "USB opening corners clip the plug overmold");
assert(usb_w / 2 < lug_in - 1, "USB opening runs into the lugs");
assert(deep_low > floor_top + bat_clear, "deep parts touch the floor");
assert(bat_cx - bat_w / 2 > deep_x1, "battery under the deep parts");
assert(floor_top + stop_h < deep_low, "battery stops hit the deep parts");
assert(pusher_gap >= 0.2, "pushers rest on the buttons");
assert(cap_root < pusher_clear, "cap root fillet hits the hole edge");
assert(pusher_in_x > lcd_hx + fit_clear, "pusher flanges block module insertion");
assert(pusher_travel < cap_out - 0.2, "cap sinks into the wall before the switch clicks");
assert(btn_cz - flange_h / 2 > floor_top + 1, "flange hits the floor");
assert(btn_cz + flange_h / 2 < glass_z0, "flange reaches the glass");
assert(btn_cz - hole_h / 2 > zb + lug_h, "pusher hole runs into the lugs");
assert(min([for (i = [1 : len(btn_y) - 1]) btn_y[i] - btn_y[i - 1]]) > flange_w + 0.5, "flanges overlap");
assert(hole_w - 2 * hole_r <= 10, "pusher hole bridge too long");
assert(wall_top - pry_depth > glass_z0, "pry notch below the glass");
assert(rib_dy < (glass_y1 - glass_y0) / 2 - glass_r, "ribs not on the straight glass edge");
assert(case_h + glass_proud <= 15.5, "watch thicker than 15.5 mm");
echo(case_h = case_h, total_h = case_h + glass_proud, lug_to_lug = 2 * lug_x1,
     length_y = out_y1 - out_y0, plug_clear_above_strap = plug_bottom - strap_top,
     usb_bridge = usb_bridge, pusher_gap = pusher_gap, pusher_travel = pusher_travel,
     battery_gap = parts_low - (floor_top + bat_t + bat_swell));

// === MODULES ===
module rrect(hx, y0, y1, r)
    translate([0, (y0 + y1) / 2])
        offset(r = r) square([2 * hx - 2 * r, (y1 - y0) - 2 * r], center = true);

module outer2d() rrect(out_hx, out_y0, out_y1, out_r);
module cav2d()   rrect(cav_hx, cav_y0, cav_y1, cav_r);

// 2D polygon in (y, z), extruded along X from x0 to x1
module yz_prism(x0, x1) translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children();
// 2D polygon in (x, z), extruded along Y from y0 to y1
module xz_prism(y0, y1) translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(y1 - y0) children();

// Teardrop hole (point faces +Z), from the skill reference
module teardrop_hole(d, h, axis = "y") {
    r = d / 2;
    rotate([0, 0, axis == "x" ? 90 : 0]) rotate([90, 0, 0])
        linear_extrude(height = h, center = true)
            union() { circle(r = r); rotate(45) square(r); }
}

module case_shell()
    hull() {
        translate([0, 0, zb]) linear_extrude(fudge) offset(delta = -back_chamfer) outer2d();
        translate([0, 0, zb + back_chamfer]) linear_extrude(case_h - back_chamfer - top_chamfer) outer2d();
        translate([0, 0, wall_top - fudge]) linear_extrude(fudge) offset(delta = -top_chamfer) outer2d();
    }

// One lug footprint (convex): root buried in the wall, 45-deg flare both sides, rounded tip
module lug2d(sx, sy)
    hull() {
        translate([sx * (out_hx - back_chamfer - ef_chamfer) - (sx > 0 ? 0 : fudge),
                   glass_cy + sy * (lug_in - lug_flare)])
            mirror([sx < 0 ? 1 : 0, sy < 0 ? 1 : 0])
                square([back_chamfer + ef_chamfer + fudge, lug_t + 2 * lug_flare]);
        for (x = [out_hx + lug_flare, lug_x1 - lug_round], y = [lug_in + lug_round, lug_out - lug_round])
            translate([sx * x, glass_cy + sy * y]) circle(r = x < lug_x1 - lug_round - fudge ? fudge : lug_round);
        for (y = [lug_in, lug_out]) translate([sx * (out_hx + lug_flare), glass_cy + sy * y]) circle(r = fudge);
    }

module lugs()
    for (sx = [-1, 1], sy = [-1, 1])
        hull() {
            translate([0, 0, zb]) linear_extrude(fudge) offset(delta = -ef_chamfer) lug2d(sx, sy);
            translate([0, 0, zb + ef_chamfer]) linear_extrude(lug_h - ef_chamfer) lug2d(sx, sy);
        }

module bar_holes()
    for (sx = [-1, 1])
        translate([sx * bar_x, glass_cy, zb + bar_z]) teardrop_hole(bar_hole_d, 2 * lug_out + 2, "y");

module cavity() {
    difference() {
        union() {
            hull() {
                translate([0, 0, floor_top]) linear_extrude(fudge) offset(delta = -floor_fillet) cav2d();
                translate([0, 0, floor_top + floor_fillet]) linear_extrude(fudge) cav2d();
            }
            translate([0, 0, floor_top + floor_fillet - fudge])
                linear_extrude(wall_top - floor_top) cav2d();
            hull() {
                translate([0, 0, wall_top - lead_in]) linear_extrude(fudge) cav2d();
                translate([0, 0, wall_top + fudge]) linear_extrude(fudge) offset(delta = lead_in) cav2d();
            }
        }
        // ledges at the short ends, glass rests on their tops
        for (e = [[ledge_y1, out_y1 + 1], [out_y0 - 1, ledge_y0]])
            translate([-out_hx - 1, e[0], zb - 1]) cube([2 * out_hx + 2, e[1] - e[0], glass_z0 - zb + 1]);
    }
}

module pry_cut()
    for (e = [[cav_y1 - 1, out_y1 + 1], [out_y0 - 1, cav_y0 + 1]])
        translate([-pry_w / 2, e[0], wall_top - pry_depth]) cube([pry_w, e[1] - e[0], pry_depth + 1]);

module usb_cut()
    yz_prism(-out_hx - 1, -cav_hx + 1)
        translate([usb_cy, usb_cz]) {
            rrect_c(usb_w, usb_h, usb_r);
            // 45-deg teardrop fill at the two top corners (support-free ceiling)
            for (s = [-1, 1]) let(cx = s * (usb_w / 2 - usb_r), cz = usb_h / 2 - usb_r, t = usb_r / sqrt(2))
                polygon([[cx, cz], [cx + s * t, cz + t], [cx + s * usb_r * (sqrt(2) - 1), cz + usb_r], [cx, cz + usb_r]]);
        }

// Rounded rectangle centred on the origin
module rrect_c(w, h, r) offset(r = r) square([w - 2 * r, h - 2 * r], center = true);

module pusher_holes()
    for (y = btn_y)
        yz_prism(cav_hx - 1, out_hx + 1) translate([y, btn_cz]) rrect_c(hole_w, hole_h, hole_r);

// One pusher, built along +Z: flange (inside the wall) then the cap
module pusher_shape() {
    hull() {   // flange; small elephant-foot chamfer (this face presses the button)
        linear_extrude(fudge) offset(delta = -ef_chamfer / 4) rrect_c(flange_w, flange_h, cap_r);
        translate([0, 0, ef_chamfer / 4]) linear_extrude(flange_t - ef_chamfer / 4) rrect_c(flange_w, flange_h, cap_r);
    }
    hull() {   // root fillet, inside the hole clearance
        translate([0, 0, flange_t - fudge]) linear_extrude(fudge) offset(delta = cap_root) rrect_c(cap_w, cap_h, cap_r);
        translate([0, 0, flange_t + cap_root]) linear_extrude(fudge) rrect_c(cap_w, cap_h, cap_r);
    }
    hull() {   // cap, chamfered outer edge
        translate([0, 0, flange_t - fudge]) linear_extrude(cap_len - cap_chamfer) rrect_c(cap_w, cap_h, cap_r);
        translate([0, 0, flange_t + cap_len - fudge]) linear_extrude(fudge)
            offset(delta = -cap_chamfer) rrect_c(cap_w, cap_h, cap_r);
    }
}

// Pusher i at rest in module coords (flange against the wall's inner face)
module pusher_mod(i, gap = 0)
    translate([pusher_in_x - gap, btn_y[i], btn_cz]) rotate([90, 0, 90]) pusher_shape();

// Vertical crush ribs on the long walls, on the glass band, 45-deg bottom
module ribs()
    for (sx = [-1, 1], sy = [-1, 1])
        translate([0, glass_cy + sy * rib_dy, 0])
            hull() {
                translate([sx * (cav_hx + rib_d / 2 - rib_proud), 0, glass_z0 + rib_proud])
                    cylinder(d = rib_d, h = wall_top - lead_in - glass_z0 - rib_proud);
                translate([sx * (cav_hx + rib_d / 2), 0, glass_z0]) cylinder(d = rib_d, h = fudge);
            }

module battery_stops()
    for (sx = [-1, 1], sy = [-1, 1])
        translate([bat_cx + sx * (bat_w / 2 + bat_clear), bat_cy + sy * (bat_l / 2 + bat_clear), floor_top - fudge])
            mirror([sx < 0 ? 1 : 0, 0, 0]) mirror([0, sy < 0 ? 1 : 0, 0]) {
                translate([0, -stop_leg, 0]) cube([stop_h, stop_leg + stop_h, stop_h + fudge]);
                translate([-stop_leg, 0, 0]) cube([stop_leg + stop_h, stop_h, stop_h + fudge]);
            }

module case_mod(with_ribs = true) {
    difference() {
        union() { case_shell(); lugs(); }
        cavity();
        usb_cut();
        pry_cut();
        bar_holes();
        pusher_holes();
    }
    battery_stops();
    if (with_ribs) ribs();
}

module box(b)
    if (len(b) > 6) translate([0, 0, b[2]]) linear_extrude(b[5] - b[2])
        translate([(b[0] + b[3]) / 2, 0]) rrect((b[3] - b[0]) / 2, b[1], b[4], b[6]);
    else translate([b[0], b[1], b[2]]) cube([b[3] - b[0], b[4] - b[1], b[5] - b[2]]);

module glass_mod() translate([0, 0, glass_z0]) linear_extrude(glass_top - glass_z0)
    rrect(glass_w / 2, glass_y0, glass_y1, glass_r);
module battery_mod() translate([bat_cx - bat_w / 2, bat_cy - bat_l / 2, floor_top]) cube([bat_w, bat_l, bat_t]);
module module_env() { glass_mod(); for (b = module_boxes) box(b); }
module sweep_up(b) box(len(b) > 6 ? [b[0], b[1], b[2], b[3], b[4], wall_top + 5, b[6]]
                                   : [b[0], b[1], b[2], b[3], b[4], wall_top + 5]);

module to_plate() translate([0, 0, -zb]) children();

// === ASSEMBLY / RENDER ===
// The clash parts use box envelopes (crush ribs left out: their 0.05 mm
// interference is intended); ref/check_fit.py checks the real mesh.
part = "case"; // "case", "pusher", "plate_pushers", "all", "clash_module", "clash_insert", "clash_battery", "clash_pushers", "clash_pusher_btn"
if (part == "case") to_plate() case_mod();
if (part == "pusher") pusher_shape();
if (part == "plate_pushers")   // 3 + 1 spare, flange down
    for (k = [0 : 3]) translate([k * (flange_w + 4), 0, 0]) pusher_shape();
if (part == "all") to_plate() {
    case_mod();
    color("Tomato") for (i = [0 : len(btn_y) - 1]) pusher_mod(i);
    color("SteelBlue", 0.9) import("ref/module.stl");
    color("Gold") battery_mod();
}
// Pushers at rest, flange fudge off the wall: must not touch the case
if (part == "clash_pushers") to_plate() intersection() {
    case_mod();
    for (i = [0 : len(btn_y) - 1]) pusher_mod(i, fudge);
}
// Pushers at rest vs the seated module: must not touch the buttons
if (part == "clash_pusher_btn") to_plate() intersection() {
    union() for (i = [0 : len(btn_y) - 1]) pusher_mod(i);
    translate([0, 0, fudge]) module_env();
}
// Module in its seated pose, lifted by fudge off the ledges: must be empty
if (part == "clash_module") to_plate() intersection() {
    case_mod(with_ribs = false);
    translate([0, 0, fudge]) module_env();
}
// Everything below the glass swept straight up (the insertion path): must be empty
if (part == "clash_insert") to_plate() intersection() {
    case_mod(with_ribs = false);
    translate([0, 0, fudge]) for (b = module_boxes) sweep_up(b);
}
if (part == "clash_battery") to_plate() intersection() {
    case_mod();
    translate([0, 0, fudge]) battery_mod();
}
