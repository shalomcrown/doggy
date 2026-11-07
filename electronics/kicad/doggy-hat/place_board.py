# Places the doggy-hat board from the current schematic netlist.
# Running this again overwrites placement. Do not run it after hand edits.

import os
import re
import subprocess

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HERE, "doggy-hat.kicad_pcb")
TEMPLATE = os.path.join(HERE, "doggy-hat-outline.kicad_pcb")
NETLIST = "/tmp/doggy-hat-place.net"
LIB_ROOT = "/usr/share/kicad/footprints"
CELL = 0.5

# Footprints the schematic left blank. Sizes are starting points.
FOOTPRINTS = {
    "F1": "Fuse:Fuse_Blade_ATO_directSolder",
    "SW1": "Button_Switch_SMD:SW_SPST_B3U-1000P",
    "SW2": "Button_Switch_SMD:SW_SPST_B3U-1000P",
    "U4": "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
    "D2": "Diode_SMD:D_SMC",
    "L1": "Inductor_SMD:L_12x12mm_H6mm",
    "D3": "Diode_SMD:D_SOD-123",
    "L2": "Inductor_SMD:L_Wuerth_HCI-1890",
    "D4": "Diode_SMD:D_SOD-123",
    "L3": "Inductor_SMD:L_12x12mm_H8mm",
    "J3": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "U10": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "J24": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "J25": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "J26": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "J27": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "J28": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "U17": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "U18": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "U19": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "U20": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "U21": "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
    "U22": "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm",
    "J29": "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
    "SW3": "Button_Switch_SMD:SW_SPST_B3U-1000P",
    "Y1": "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm",
}

SHEET_ORDER = (
    "Pack input",
    "Pi 5 V",
    "Servo rail",
    "Motor rail",
    "Servos",
    "Motors",
    "Sensors",
    "RP2040",
    "Pi header",
)

SUPPLY_VALUES = {"100nF", "1uF", "10uF", "22uF", "100uF", "10nF", "4.7nF"}

# One servo row, pin row pointing away from the Pi header. 5.08 mm leaves a gap
# between JR plugs (the housing is 2.42 mm thick).
SERVO_ROW = (
    "J6", "J7", "J8",
    "J17", "J16", "J15",
    "J9", "J10", "J11",
    "J14", "J13", "J12",
    "J19",
    "J4", "J5", "J18",
)

SCREW_ROW = ("J1", "J20", "J21", "J22", "J23")
SERVO_RESISTORS = tuple("R" + str(number) for number in range(37, 53))
WHEEL_ROW = ("J25", "J26", "J27", "J28")


# ================================================================================
def fill_footprints():
    """Write a footprint name onto schematic symbols that still have a blank one."""
    changed = []
    for name in os.listdir(HERE):
        if name.endswith(".kicad_sch") == False:
            continue
        path = os.path.join(HERE, name)
        text = open(path, encoding="utf-8").read()
        parts = re.split(r'(?=\n  \(symbol \(lib_id )', text)
        output = []
        for part in parts:
            ref_match = re.search(r'\(property "Reference" "([^"]+)"', part)
            if ref_match != None and ref_match.group(1) in FOOTPRINTS:
                part, did = write_footprint(part, FOOTPRINTS[ref_match.group(1)])
                if did:
                    changed.append(ref_match.group(1))
            output.append(part)
        open(path, "w", encoding="utf-8").write("".join(output))
    return sorted(set(changed))


# ================================================================================
def write_footprint(block, name):
    marker = '(property "Footprint" "'
    at = block.find(marker)
    if at < 0:
        return block, False
    rest = block[at + len(marker):]
    if rest.startswith('"') == False:
        return block, False
    updated = block[:at] + marker + name + '"' + rest[1:]
    return updated, True


# ================================================================================
def export_netlist():
    subprocess.check_call([
        "kicad-cli", "sch", "export", "netlist",
        "--format", "kicadsexpr",
        "-o", NETLIST,
        os.path.join(HERE, "doggy-hat.kicad_sch"),
    ])


# ================================================================================
def parse_netlist(path):
    text = open(path, encoding="utf-8").read()
    comps = {}
    for chunk in text.split("(comp (ref ")[1:]:
        ref = chunk.split('"', 2)[1]
        value = re.search(r'\(value "([^"]*)"', chunk).group(1)
        footprint = re.search(r'\(footprint "([^"]*)"', chunk)
        sheet = re.search(r'Sheetname"\) \(value "([^"]*)"', chunk).group(1)
        sheet_path = re.search(r'\(sheetpath \(names "[^"]*"\) \(tstamps "([^"]*)"', chunk).group(1)
        stamp = re.search(r'\(tstamps "([^"]+)"\)', chunk).group(1)
        comps[ref] = {
            "ref": ref,
            "value": value,
            "footprint": footprint.group(1) if footprint != None else "",
            "sheet": sheet,
            "path": sheet_path + stamp,
        }
    pin_net = {}
    nets = {}
    for chunk in text.split("(net (code ")[1:]:
        name = re.search(r'\(name "([^"]*)"', chunk).group(1)
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', chunk)
        nets[name] = nodes
        for ref, pin in nodes:
            pin_net[(ref, pin)] = name
    return comps, pin_net, nets


# ================================================================================
PLUGIN = None
LOADED = {}


# ================================================================================
def load_footprint(lib_id):
    """Duplicate a cached footprint. Repeated FootprintLoad drops the plugin."""
    global PLUGIN
    if lib_id not in LOADED:
        if PLUGIN == None:
            PLUGIN = pcbnew.IO_MGR.PluginFind(pcbnew.IO_MGR.KICAD_SEXP)
        lib_name, item = lib_id.split(":", 1)
        folder = os.path.join(LIB_ROOT, lib_name + ".pretty")
        loaded = PLUGIN.FootprintLoad(folder, item)
        if loaded == None:
            raise RuntimeError("missing footprint " + lib_id)
        LOADED[lib_id] = loaded
    return LOADED[lib_id].Duplicate()


# ================================================================================
def mm(value):
    return pcbnew.VECTOR2I(pcbnew.FromMM(value[0]), pcbnew.FromMM(value[1]))


# ================================================================================
def ensure_net(board, name):
    found = board.FindNet(name)
    if found != None:
        return found
    board.Add(pcbnew.NETINFO_ITEM(board, name))
    return board.FindNet(name)


# ================================================================================
def world_box(footprint):
    """Courtyard only. The full bounding box includes reference text and packs nothing."""
    box = footprint.GetCourtyard(pcbnew.F_CrtYd).BBox()
    if box.GetWidth() == 0 and box.GetHeight() == 0:
        box = footprint.GetCourtyard(pcbnew.B_CrtYd).BBox()
    xs = (box.GetLeft(), box.GetRight())
    ys = (box.GetTop(), box.GetBottom())
    return min(xs) / 1e6, min(ys) / 1e6, max(xs) / 1e6, max(ys) / 1e6


# ================================================================================
def block_box(blocked, box):
    x1, y1, x2, y2 = box
    ix1 = int(min(x1, x2) / CELL) - 1
    ix2 = int(max(x1, x2) / CELL) + 2
    iy1 = int(min(y1, y2) / CELL) - 1
    iy2 = int(max(y1, y2) / CELL) + 2
    for ix in range(ix1, ix2):
        for iy in range(iy1, iy2):
            blocked.add((ix, iy))


# ================================================================================
def is_free(blocked, box):
    x1, y1, x2, y2 = box
    ix1 = int(min(x1, x2) / CELL)
    ix2 = int(max(x1, x2) / CELL) + 1
    iy1 = int(min(y1, y2) / CELL)
    iy2 = int(max(y1, y2) / CELL) + 1
    for ix in range(ix1, ix2):
        for iy in range(iy1, iy2):
            if (ix, iy) in blocked:
                return False
    return True


# ================================================================================
def add_segment(board, start, end):
    shape = pcbnew.PCB_SHAPE(board)
    shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
    shape.SetStart(mm(start))
    shape.SetEnd(mm(end))
    shape.SetLayer(pcbnew.Edge_Cuts)
    shape.SetWidth(pcbnew.FromMM(0.1))
    board.Add(shape)


# ================================================================================
def replace_outline(board):
    """20 mm past the HAT on the left, and 20 mm past the edge opposite the header."""
    for drawing in list(board.GetDrawings()):
        if drawing.GetLayer() != pcbnew.Edge_Cuts:
            continue
        box = drawing.GetBoundingBox()
        cx = (box.GetLeft() + box.GetRight()) / 2e6
        cy = (box.GetTop() + box.GetBottom()) / 2e6
        if 143 < cx < 148 and 79 < cy < 98:
            continue
        board.Remove(drawing)
    points = (
        (80, 47), (100, 47), (103, 44), (162, 44), (165, 47),
        (165, 120), (80, 120), (80, 81), (100, 81),
        (100.5, 80.5), (104.5, 80.5), (105, 80), (105, 64),
        (104.5, 63.5), (100.5, 63.5), (100, 63), (80, 63), (80, 47),
    )
    for start, end in zip(points, points[1:]):
        add_segment(board, start, end)


# ================================================================================
def apply_nets(board, footprint, ref, pin_net):
    """ABM8 uses pads 1 and 3 for the crystal. Pads 2 and 4 are the lid ground."""
    if ref == "Y1":
        mapping = {
            "1": pin_net.get((ref, "1")),
            "3": pin_net.get((ref, "2")),
            "2": "GND",
            "4": "GND",
        }
        for pad in footprint.Pads():
            name = mapping.get(pad.GetNumber())
            if name != None:
                pad.SetNet(ensure_net(board, name))
        return
    for pad in footprint.Pads():
        name = pin_net.get((ref, pad.GetNumber()))
        if name != None:
            pad.SetNet(ensure_net(board, name))


# ================================================================================
def add_part(board, comp, pin_net):
    footprint = load_footprint(comp["footprint"])
    footprint.SetReference(comp["ref"])
    footprint.SetValue(comp["value"])
    footprint.Reference().SetTextSize(mm((0.6, 0.6)))
    footprint.Value().SetTextSize(mm((0.6, 0.6)))
    footprint.Reference().SetTextThickness(pcbnew.FromMM(0.1))
    footprint.Value().SetTextThickness(pcbnew.FromMM(0.1))
    footprint.SetPath(pcbnew.KIID_PATH(comp["path"]))
    board.Add(footprint)
    apply_nets(board, footprint, comp["ref"], pin_net)
    return footprint


# ================================================================================
def move_to(footprint, x, y, rotation):
    footprint.SetPosition(mm((x, y)))
    footprint.SetOrientationDegrees(rotation)


# ================================================================================
def shrink_text(footprint):
    size = mm((0.7, 0.7))
    footprint.Reference().SetTextSize(size)
    footprint.Value().SetTextSize(size)
    footprint.Value().SetVisible(False)


# ================================================================================
def place_header(footprint):
    footprint.SetPosition(mm((108.37, 48.77)))
    footprint.Flip(footprint.GetPosition(), False)
    footprint.SetOrientationDegrees(-90)


# ================================================================================
def place_connectors(footprints):
    shrink_text(footprints["J2"])
    place_header(footprints["J2"])
    first = 86.6
    for index, ref in enumerate(SERVO_ROW):
        footprint = footprints[ref]
        shrink_text(footprint)
        move_to(footprint, first + index * 5.08, 103.2, 0)
    for index, ref in enumerate(SCREW_ROW):
        move_to(footprints[ref], 92.7 + index * 15.5, 115.5, 0)
    compass = footprints["J24"]
    shrink_text(compass)
    move_to(compass, 82.0, 103.2, 0)
    for index, ref in enumerate(WHEEL_ROW):
        footprint = footprints[ref]
        shrink_text(footprint)
        move_to(footprint, 90.0, 98.74 - index * 5.08, -90)
    move_to(footprints["J29"], 84.2, 55.0, -90)
    move_to(footprints["SW3"], 100.0, 55.0, 0)
    move_to(footprints["SW2"], 112.0, 55.0, 0)
    move_to(footprints["J3"], 148.0, 60.0, 0)
    for index, ref in enumerate(SERVO_RESISTORS):
        x = 86.6 + index * 5.08
        y = 99.2
        # The first two sit over the wheel headers, so they move inboard of that row.
        if index < 2:
            x = 95.0 + index * 2.6
        if abs(x - 103.5) < 6 or abs(x - 161.5) < 6:
            y = 91.5
        move_to(footprints[ref], x, y, 90)


# ================================================================================
def is_ground(name):
    if name == "GND":
        return True
    if name.endswith("GND"):
        return True
    return False


# ================================================================================
def supply_caps(comps, pin_net):
    """Caps from a supply net to ground. These sit on the chip pin, not in the shelf."""
    found = []
    for comp in comps.values():
        if comp["ref"].startswith("C") == False:
            continue
        if comp["value"] not in SUPPLY_VALUES:
            continue
        nets = []
        for pin in ("1", "2"):
            net = pin_net.get((comp["ref"], pin))
            if net != None:
                nets.append(net)
        if len(nets) != 2:
            continue
        grounds = [net for net in nets if is_ground(net)]
        powers = [net for net in nets if is_ground(net) == False]
        if len(grounds) == 1 and len(powers) == 1:
            found.append((comp["ref"], powers[0], grounds[0]))
    return found


# ================================================================================
def pads_on(footprint, net_name, pin_net, ref):
    pads = []
    for pad in footprint.Pads():
        if pin_net.get((ref, pad.GetNumber())) == net_name:
            pads.append(pad)
    return pads


# ================================================================================
def outward_of(footprint, pad):
    center = footprint.GetPosition()
    pos = pad.GetPosition()
    dx = pos.x - center.x
    dy = pos.y - center.y
    if abs(dx) >= abs(dy):
        if dx >= 0:
            return (1, 0)
        return (-1, 0)
    if dy >= 0:
        return (0, 1)
    return (0, -1)


# ================================================================================
def cap_rotation(direction):
    if direction == (1, 0):
        return 0
    if direction == (-1, 0):
        return 180
    if direction == (0, 1):
        return 90
    return 270


# ================================================================================
def on_board(box):
    samples = (
        (box[0], box[1]),
        (box[2], box[1]),
        (box[0], box[3]),
        (box[2], box[3]),
        ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2),
    )
    for x, y in samples:
        if y < 44 or y > 120 or x > 165 or x < 80:
            return False
        if 63 < y < 81 and x < 105:
            return False
        if y < 47 and (x < 103 or x > 162):
            return False
        if 79 < y < 98 and 142 < x < 148:
            return False
        for hx, hy in ((103.5, 47.5), (161.5, 47.5), (103.5, 96.5), (161.5, 96.5)):
            if (x - hx) ** 2 + (y - hy) ** 2 < 2.7 ** 2:
                return False
    return True


# ================================================================================
def cap_fits(box, owner_box, others):
    """Sit against the owner. Do not cover another part, a hole, or the notch."""
    if on_board(box) == False:
        return False
    overlap_x = min(box[2], owner_box[2]) - max(box[0], owner_box[0])
    overlap_y = min(box[3], owner_box[3]) - max(box[1], owner_box[1])
    if overlap_x > 0.6 and overlap_y > 0.6:
        return False
    for other_box in others:
        overlap_x = min(box[2], other_box[2]) - max(box[0], other_box[0])
        overlap_y = min(box[3], other_box[3]) - max(box[1], other_box[1])
        if overlap_x > 0.15 and overlap_y > 0.15:
            return False
    return True


# ================================================================================
def cap_candidates(pad, direction):
    side = (-direction[1], direction[0])
    pos = pad.GetPosition()
    aims = (
        direction,
        side,
        (-side[0], -side[1]),
        (-direction[0], -direction[1]),
    )
    for aim in aims:
        rotation = cap_rotation(aim)
        across = (-aim[1], aim[0])
        for distance in (2.55, 4.75, 6.95):
            for offset in (0, 2.2, -2.2, 4.4, -4.4):
                x = pos.x / 1e6 + aim[0] * distance + across[0] * offset
                y = pos.y / 1e6 + aim[1] * distance + across[1] * offset
                yield x, y, rotation


# ================================================================================
def place_supply_caps(footprints, caps, pin_net, blocked):
    """0805 across the supply pin: pad 1 toward the chip, pad 2 toward the outside."""
    used = {}
    placed = 0
    missed = []
    cap_refs = set(ref for ref, _power, _ground in caps)
    others = {}
    for other_ref, other in footprints.items():
        if other_ref in cap_refs:
            continue
        if other.GetPosition().x / 1e6 < 1:
            continue
        others[other_ref] = world_box(other)
    for ref, power, _ground in caps:
        owner = choose_owner(ref, power, footprints, pin_net)
        if owner == None:
            missed.append(ref)
            continue
        owner_fp = footprints[owner]
        pads = pads_on(owner_fp, power, pin_net, owner)
        if len(pads) == 0:
            missed.append(ref)
            continue
        if owner_fp.GetPosition().x / 1e6 < 75:
            missed.append(ref)
            continue
        slot = used.get(owner, 0)
        used[owner] = slot + 1
        pad = pads[slot % len(pads)]
        direction = outward_of(owner_fp, pad)
        footprint = footprints[ref]
        found = False
        for x, y, rotation in cap_candidates(pad, direction):
            move_to(footprint, x, y, rotation)
            neighbors = [
                other_box for other_ref, other_box in others.items()
                if other_ref != owner
            ]
            if cap_fits(world_box(footprint), others[owner], neighbors):
                found = True
                break
        if found == False:
            move_to(footprint, 0, 0, 0)
            missed.append(ref)
            continue
        box = world_box(footprint)
        others[ref] = box
        block_box(blocked, box)
        placed += 1
    return placed, missed


# ================================================================================
def choose_owner(cap_ref, power, footprints, pin_net):
    sheet = None
    for ref, footprint in footprints.items():
        if ref == cap_ref:
            sheet = footprint.GetProperty("Sheetname") if False else None
    # Sheet is not on the footprint. Match from the closure stored on the function.
    return choose_owner.pick(cap_ref, power)


# ================================================================================
def shelf_place(footprints, order, blocked, after_place=None):
    """Pack remaining parts into the HAT area, skipping the connector band."""
    overflow = []
    park_y = 48.0
    for ref in order:
        footprint = footprints[ref]
        footprint.SetPosition(mm((0, 0)))
        footprint.SetOrientationDegrees(0)
        box = world_box(footprint)
        width = box[2] - box[0]
        height = box[3] - box[1]
        slot = find_slot(blocked, width, height, 100, 52, 163, 100.5)
        if slot == None:
            slot = find_slot(blocked, width, height, 84, 83, 100, 100)
        if slot == None:
            overflow.append(ref)
            move_to(footprint, 40 - box[0], park_y - box[1], 0)
            block_box(blocked, world_box(footprint))
            park_y += height + 1.0
            continue
        move_to(footprint, slot[0] - box[0], slot[1] - box[1], 0)
        block_box(blocked, world_box(footprint))
        if after_place != None:
            after_place(ref)
    return overflow


# ================================================================================
def find_slot(blocked, width, height, x0, y0, x1, y1):
    y = y0
    while y + height <= y1 + 0.01:
        x = x0
        while x + width <= x1 + 0.01:
            if is_free(blocked, (x, y, x + width, y + height)):
                return (x, y)
            x += CELL
        y += CELL
    return None


# ================================================================================
def block_keepouts(board, blocked, footprints):
    for _ref, footprint in footprints.items():
        block_box(blocked, world_box(footprint))
    for footprint in board.GetFootprints():
        if "MountingHole" in str(footprint.GetFPID().GetLibItemName()):
            pos = footprint.GetPosition()
            x = pos.x / 1e6
            y = pos.y / 1e6
            block_box(blocked, (x - 3.4, y - 3.4, x + 3.4, y + 3.4))
    # Display connector notch, and the bay beside it, are not board.
    block_box(blocked, (80, 63, 105, 81))
    # Camera ribbon slot.
    block_box(blocked, (142, 79, 148, 98))


# ================================================================================
def main():
    filled = fill_footprints()
    export_netlist()
    comps, pin_net, _nets = parse_netlist(NETLIST)
    missing = [ref for ref, comp in comps.items() if comp["footprint"] == ""]
    if len(missing) != 0:
        raise RuntimeError("still no footprint: " + " ".join(missing))
    # Load every footprint before the board. Removing footprints afterwards
    # invalidates KiCad's footprint plugin.
    seen = []
    for comp in comps.values():
        if comp["footprint"] not in seen:
            seen.append(comp["footprint"])
            load_footprint(comp["footprint"])
    board = pcbnew.LoadBoard(TEMPLATE)
    for footprint in list(board.GetFootprints()):
        name = str(footprint.GetFPID().GetLibItemName())
        if "MountingHole" in name:
            continue
        board.Remove(footprint)
    footprints = {}
    for ref in sorted(comps):
        footprints[ref] = add_part(board, comps[ref], pin_net)
    place_connectors(footprints)
    blocked = set()
    fixed = set(SERVO_ROW) | set(SCREW_ROW) | set(WHEEL_ROW)
    fixed.update(["J2", "J3", "J24", "J29", "SW2", "SW3"])
    fixed.update(SERVO_RESISTORS)
    block_keepouts(board, blocked, {ref: footprints[ref] for ref in fixed})
    caps = supply_caps(comps, pin_net)
    cap_refs = set(ref for ref, _power, _ground in caps)

    def pick(cap_ref, power):
        sheet = comps[cap_ref]["sheet"]
        for prefixes in (("U",), ("J",), ("Q", "L", "D")):
            found = []
            for ref, comp in comps.items():
                if ref == cap_ref or ref in cap_refs:
                    continue
                if comp["sheet"] != sheet:
                    continue
                if ref[0] not in prefixes:
                    continue
                if any(pin_net.get((ref, pin)) == power for pin in pins_of(ref)):
                    found.append(ref)
            if len(found) != 0:
                return sorted(found)[0]
        return None

    def pins_of(ref):
        pins = []
        for key in pin_net:
            if key[0] == ref and key[1] not in pins:
                pins.append(key[1])
        return pins

    choose_owner.pick = pick

    # ================================================================================
    def reserve(owner_ref):
        owner_fp = footprints[owner_ref]
        if owner_fp.GetPosition().x / 1e6 < 75:
            return
        slot = 0
        held = []
        owner_box = world_box(owner_fp)
        for cap_ref, power, _ground in caps:
            if pick(cap_ref, power) != owner_ref:
                continue
            pads = pads_on(owner_fp, power, pin_net, owner_ref)
            if len(pads) == 0:
                continue
            pad = pads[slot % len(pads)]
            slot += 1
            direction = outward_of(owner_fp, pad)
            cap = footprints[cap_ref]
            placed_boxes = list(held)
            for other_ref, other in footprints.items():
                if other_ref == owner_ref or other_ref == cap_ref:
                    continue
                if other.GetPosition().x / 1e6 < 1:
                    continue
                placed_boxes.append(world_box(other))
            found = False
            for x, y, rotation in cap_candidates(pad, direction):
                move_to(cap, x, y, rotation)
                if cap_fits(world_box(cap), owner_box, placed_boxes):
                    found = True
                    break
            if found:
                held.append(world_box(cap))
                block_box(blocked, world_box(cap))
            move_to(cap, 0, 0, 0)

    order = []
    for sheet in SHEET_ORDER:
        group = []
        for ref, comp in comps.items():
            if comp["sheet"] == sheet and ref not in fixed and ref not in cap_refs:
                group.append(ref)
        group.sort(key=lambda ref: part_area(footprints[ref]), reverse=True)
        order.extend(group)
    overflow = shelf_place(footprints, order, blocked, reserve)
    placed, missed = place_supply_caps(footprints, caps, pin_net, blocked)
    overflow.extend(shelf_place(footprints, missed, blocked))
    replace_outline(board)
    note_orientation(board)
    pcbnew.SaveBoard(BOARD, board)
    print("footprints filled", " ".join(filled))
    print("supply caps placed", placed, "missed", " ".join(missed))
    print("shelf overflow", len(overflow))
    print(" ".join(overflow))


# ================================================================================
def part_area(footprint):
    footprint.SetPosition(mm((0, 0)))
    footprint.SetOrientationDegrees(0)
    box = world_box(footprint)
    return (box[2] - box[0]) * (box[3] - box[1])


# ================================================================================
def note_orientation(board):
    for drawing in board.GetDrawings():
        if drawing.GetClass() != "PCB_TEXT":
            continue
        text = drawing.GetText()
        if "outline only" not in text and "Header is the low-Y edge" not in text:
            continue
        drawing.SetText(
            "Header is the low-Y edge (bottom of the default KiCad view). "
            "USB and Ethernet are off the high-X edge. "
            "Screws, the servo row, and the compass are on the high-Y edge, 20 mm past the HAT. "
            "The left edge is 20 mm past the HAT, away from USB. "
            "Parts in the column left of the board did not fit in that outline. "
            "Copper is not routed."
        )


# ================================================================================
if __name__ == "__main__":
    main()
