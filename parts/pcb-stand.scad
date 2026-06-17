$fn = 60;

pcb_width = 36; // adjust depending on manufacturing tolerances for the PCB
pcb_thickness = 1.8;
component_clearance = 1.2; // adjust if that hits components

wall_thickness = 3.0; // thickness of the plastic walls of the groove
slot_depth = 15.0;    // how high up the sides the U-profile grips
base_height = 5.0;    // thickness of the bottom mounting plate
base_extension = 12.0; // how far the base extends out for screws

// screw holes (for standard wood screws)
screw_shanks_dia = 4.0; // diameter of the screw thread
screw_head_dia = 8.0;  // diameter of the recessed screw head
screw_head_depth = 3.0; // how deep the countersink goes

// total width of the plastic tower block before cutting the grooves and middle out
total_width = pcb_width + (2 * wall_thickness);
total_depth = pcb_thickness + (2 * wall_thickness);
total_length = total_width + (2 * base_extension);

difference() {
    union() {
        // base plate for screwing into wood
        cube([total_length, total_depth, base_height]);

        // vertical block across the center (will be hollowed out into towers)
        translate([base_extension, 0, base_height])
            cube([total_width, total_depth, slot_depth]);
    }

    // the pcb slot (cuts the groove depth into the towers)
    // positioned so the PCB edges sit exactly at the right width apart
    translate([base_extension + wall_thickness, wall_thickness, base_height])
        cube([pcb_width, pcb_thickness, slot_depth + 1]);

    // the middle clearance cutout
    // this removes the entire middle section to clear components,
    // leaving behind two towers with inward-facing U-grooves.
    translate([base_extension + wall_thickness + component_clearance, -1, base_height])
        cube([pcb_width - (2 * component_clearance), total_depth + 2, slot_depth + 1]);

    // left screw hole (countersunk)
    translate([base_extension / 2, total_depth / 2, -0.5]) {
        cylinder(d = screw_shanks_dia, h = base_height + 1);
        translate([0, 0, base_height - screw_head_depth + 0.5])
            cylinder(d1 = screw_shanks_dia, d2 = screw_head_dia, h = screw_head_depth);
    }

    // right screw hole (countersunk)
    translate([total_length - (base_extension / 2), total_depth / 2, -0.5]) {
        cylinder(d = screw_shanks_dia, h = base_height + 1);
        translate([0, 0, base_height - screw_head_depth + 0.5])
            cylinder(d1 = screw_shanks_dia, d2 = screw_head_dia, h = screw_head_depth);
    }
}
