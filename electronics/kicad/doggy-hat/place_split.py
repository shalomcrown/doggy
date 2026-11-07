"""Place connectors on the two boards. Copper is not routed.

The lower board keeps the official HAT outline. The upper board adds the
left ear and the motor-screw strip. Running this again overwrites both PCBs.
"""

import os

import pcbnew

import place_board as pb

# ================================================================================
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.dirname(HERE)
SERVO_ROW = pb.SERVO_ROW
MOTOR_ROW = ("J20", "J21", "J22", "J23")
WHEEL_ROW = pb.WHEEL_ROW

# ================================================================================
def cache_footprints(comps, refs):
    """Load each footprint once before the board. A later Remove breaks the plugin."""
    pb.PLUGIN = None
    pb.LOADED = {}
    seen = []
    for ref in refs:
        lib_id = comps[ref]["footprint"]
        if lib_id == "":
            raise RuntimeError(ref + " has no footprint")
        if lib_id in seen:
            continue
        seen.append(lib_id)
        pb.load_footprint(lib_id)

# ================================================================================
def drop_template_header(board):
    for footprint in list(board.GetFootprints()):
        lib_id = str(footprint.GetFPID().GetLibItemName())
        if lib_id == "PinSocket_2x20_P2.54mm_Vertical" and footprint.GetReference() != "J2":
            board.Remove(footprint)

# ================================================================================
def add_placed(board, comps, pin_net, ref, x, y, rotation):
    footprint = pb.add_part(board, comps[ref], pin_net)
    pb.shrink_text(footprint)
    pb.move_to(footprint, x, y, rotation)
    return footprint

# ================================================================================
def place_shared(board, comps, pin_net):
    header = pb.add_part(board, comps["J2"], pin_net)
    pb.shrink_text(header)
    pb.place_header(header)
    add_placed(board, comps, pin_net, "J30", 132.0, 64.0, 90)

# ================================================================================
def far_edge(drawing):
    box = drawing.GetBoundingBox()
    cx = (box.GetLeft() + box.GetRight()) / 2e6
    cy = (box.GetTop() + box.GetBottom()) / 2e6
    if abs(cy - 100) < 1.5 and 100 < cx < 165:
        return True
    if abs(cx - 100) < 0.6 and 85 < cy < 98:
        return True
    if 159 < cx < 166 and 96 < cy < 101:
        return True
    if 99 < cx < 106 and 96 < cy < 101:
        return True
    return False

# ================================================================================
def extend_upper(board):
    """Left ear from the notch to the far edge, and a screw strip past y = 100."""
    for drawing in list(board.GetDrawings()):
        if drawing.GetLayer() != pcbnew.Edge_Cuts:
            continue
        if far_edge(drawing):
            board.Remove(drawing)
    points = (
        (100, 81), (80, 81), (80, 100), (118, 100),
        (118, 110), (165, 110), (165, 97),
    )
    for start, end in zip(points, points[1:]):
        pb.add_segment(board, start, end)

# ================================================================================
def place_upper_connectors(board, comps, pin_net):
    # Indices 3, 4, 14 and 15 sit on the mounting holes if they stay in the row.
    clear_of_holes = {3, 4, 14, 15}
    first = 86.5
    for index, ref in enumerate(SERVO_ROW):
        y = 85.5 if index in clear_of_holes else 93.0
        add_placed(board, comps, pin_net, ref, first + index * 5.08, y, 0)
    for index, ref in enumerate(MOTOR_ROW):
        add_placed(board, comps, pin_net, ref, 121.0 + index * 11.6, 105.0, 0)
    add_placed(board, comps, pin_net, "J24", 82.0, 94.4, 0)
    # Two plugs on the left ear. The other pair sits inboard; their cables run left.
    add_placed(board, comps, pin_net, WHEEL_ROW[0], 90.0, 83.0, -90)
    add_placed(board, comps, pin_net, WHEEL_ROW[1], 90.0, 87.2, -90)
    add_placed(board, comps, pin_net, WHEEL_ROW[2], 122.0, 83.0, -90)
    add_placed(board, comps, pin_net, WHEEL_ROW[3], 122.0, 87.2, -90)
    add_placed(board, comps, pin_net, "J29", 104.2, 57.2, -90)

# ================================================================================
def overlaps(board):
    boxes = []
    for footprint in board.GetFootprints():
        boxes.append((footprint.GetReference(), pb.world_box(footprint)))
    hits = []
    for index, (ref, box) in enumerate(boxes):
        for other, other_box in boxes[index + 1:]:
            gap_x = min(box[2], other_box[2]) - max(box[0], other_box[0])
            gap_y = min(box[3], other_box[3]) - max(box[1], other_box[1])
            if gap_x > 0.2 and gap_y > 0.2:
                hits.append((ref, other, round(gap_x, 2), round(gap_y, 2)))
    return hits

# ================================================================================
def save_board(board, name):
    path = os.path.join(KICAD, name, name + ".kicad_pcb")
    pcbnew.SaveBoard(path, board)
    print(name, path)

# ================================================================================
def build(name, extra):
    net_path = os.path.join("/tmp", name + "-place.net")
    root = os.path.join(KICAD, name, name + ".kicad_sch")
    pb.subprocess.check_call([
        "kicad-cli", "sch", "export", "netlist",
        "--format", "kicadsexpr", "-o", net_path, root,
    ])
    comps, pin_net, _nets = pb.parse_netlist(net_path)
    refs = ["J2", "J30"] + extra
    cache_footprints(comps, refs)
    board = pcbnew.LoadBoard(pb.TEMPLATE)
    place_shared(board, comps, pin_net)
    if name == "doggy-lower":
        add_placed(board, comps, pin_net, "J1", 112.0, 94.0, 0)
    else:
        place_upper_connectors(board, comps, pin_net)
        extend_upper(board)
    drop_template_header(board)
    print(name, "overlaps", overlaps(board))
    save_board(board, name)

# ================================================================================
def main():
    build("doggy-lower", ["J1"])
    build(
        "doggy-upper",
        list(SERVO_ROW) + list(MOTOR_ROW) + ["J24", "J29"] + list(WHEEL_ROW),
    )

# ================================================================================
if __name__ == "__main__":
    main()
