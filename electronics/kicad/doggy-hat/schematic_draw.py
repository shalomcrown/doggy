"""Draw the doggy HAT as one sheet per block.

Pin positions use KiCad's rotation-0 transform: world = (origin_x + pin_x, origin_y - pin_y).
Part symbols stay at rotation 0. Every pin gets a short straight stub. A signal stub ends in a
global label, a supply stub ends in a power symbol, and an unused pin gets a no-connect flag.
Only parts that belong together are wired: stacked dividers and filters, and the hand-laid pack
input, servo headers, and motor terminals. Everything sits on the 1.27 mm grid so the sheets can
be edited by hand in KiCad.
"""

import copy
import os

import generate_schematic as gen

# ================================================================================
POWER = {
    "GND",
    "+3V3",
    "+3V3_AON",
    "+3V3_RP",
    "+5V_PI",
    "+5V_HDR",
    "+5V_SERVO",
    "VM_MOTOR",
    "RP_5V",
    "VBUS",
}

GRID = 1.27
STUB = 2.54
BLOCK_GAP = 10.16
ROW_GAP = 12.7

# ================================================================================
PAPERS = (
    ("A4", 297, 210),
    ("A3", 420, 297),
    ("A2", 594, 420),
    ("A1", 841, 594),
    ("A0", 1189, 841),
)

# ================================================================================
PAGES = (
    ("pack", "pack.kicad_sch", "Pack input",
     "Battery screw, fuse, reverse-polarity diode, INA228, cutoff latch, and e-stop."),
    ("pi-5v", "pi-5v.kicad_sch", "Pi 5 V",
     "TPS54560B buck, ideal diode into the Pi header, and the hat 3.3 V regulator."),
    ("servo-rail", "servo-rail.kicad_sch", "Servo rail",
     "LM5176 buck-boost stuffed for 5.0 V."),
    ("motor-rail", "motor-rail.kicad_sch", "Motor rail",
     "LM5176 buck-boost stuffed for 12 V. The top resistor sets 5-12 V."),
    ("pi-header", "pi-header.kicad_sch", "Pi header",
     "40-pin stacking header, HAT ID EEPROM, and the GPS header."),
    ("servos", "servos.kicad_sch", "Servos",
     "PCA9685 at 0x40 and sixteen 3-pin servo headers, grouped by leg."),
    ("motors", "motors.kicad_sch", "Motors",
     "PCA9685 at 0x60, two TB6612 H-bridges, and four motor screw terminals."),
    ("sensors", "sensors.kicad_sch", "Sensors",
     "IMU, barometer, compass connector, and one 4-pin header per wheel."),
    ("rp2040", "rp2040.kicad_sch", "RP2040",
     "Encoder microcontroller, USB-C, flash, crystal, and the USB or 5 V power OR."),
)

# ================================================================================
SERVO_GROUPS = (
    ("Front left", (2, 3, 4)),
    ("Rear left", (5, 6, 7)),
    ("Rear right", (8, 9, 10)),
    ("Front right", (11, 12, 13)),
    ("Head and spare", (15, 0, 1, 14)),
)

# ================================================================================
def snap(value):
    return round(float(value), 2)

# ================================================================================
def grid(value):
    return snap(round(float(value) / GRID) * GRID)

# ================================================================================
def fmt(value):
    value = snap(value)
    if value == int(value):
        return str(int(value))
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return text

# ================================================================================
def text_len(text):
    return len(text) * 1.15 + 3.0

# ================================================================================
def step(point, direction, distance):
    return (
        snap(point[0] + direction[0] * distance),
        snap(point[1] + direction[1] * distance),
    )

# ================================================================================
def pin_world(part, number):
    for pin_number, _name, at in part["pins"]:
        if pin_number == number:
            return (snap(part["x"] + at[0]), snap(part["y"] - at[1]))
    raise RuntimeError(part["ref"] + " has no pin " + number)

# ================================================================================
def pin_at(part, number):
    for pin_number, _name, at in part["pins"]:
        if pin_number == number:
            return at
    raise RuntimeError(part["ref"] + " has no pin " + number)

# ================================================================================
def part_net(part, number):
    return part["nets"][(part["unit"], number)]

# ================================================================================
def outward(at):
    """Direction away from the body, from the pin angle (0 means the pin points right)."""
    angle = int(round(at[2])) % 360
    if angle == 0:
        return (-1, 0)
    if angle == 180:
        return (1, 0)
    if angle == 90:
        return (0, 1)
    return (0, -1)

# ================================================================================
def label_style(dx, dy):
    """KiCad global label angle: 0 reads right, 90 up, 180 left, 270 down."""
    if dx > 0:
        return 0, "left"
    if dx < 0:
        return 180, "right"
    if dy < 0:
        return 90, "left"
    return 270, "right"

# ================================================================================
def power_angle(net, direction):
    """Rotation that points the power glyph along `direction`, away from the pin."""
    if net == "GND":
        table = {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270}
    else:
        table = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}
    return table[direction]

# ================================================================================
def power_direction(net, angle):
    for direction in ((0, 1), (1, 0), (0, -1), (-1, 0)):
        if power_angle(net, direction) == angle:
            return direction
    raise RuntimeError("bad power angle " + str(angle))

# ================================================================================
def pin_for_net(part, net):
    for number, _name, _at in part["pins"]:
        if part_net(part, number) == net:
            return number
    raise RuntimeError(part["ref"] + " has no net " + str(net))

# ================================================================================
def net_names(part):
    found = []
    for number, _name, _at in part["pins"]:
        net = part_net(part, number)
        if net not in found:
            found.append(net)
    return found

# ================================================================================
def mark_point(drawing, part, number):
    target = None
    for pin_number, _name, at in part["pins"]:
        if pin_number == number:
            target = (round(at[0], 3), round(at[1], 3))
    for pin_number, _name, at in part["pins"]:
        if (round(at[0], 3), round(at[1], 3)) == target:
            drawing.wired.add((part["ref"], part["unit"], pin_number))

# ================================================================================
def place_pin_at(part, number, x, y):
    at = pin_at(part, number)
    part["x"] = snap(grid(x) - at[0])
    part["y"] = snap(grid(y) + at[1])

# ================================================================================
def name_on(drawing, point, net, direction=(1, 0)):
    """Put a global label on an existing wire point so KiCad keeps the net name."""
    tip = step(point, direction, 5.08)
    drawing.wire([point, tip])
    drawing.label(net, tip[0], tip[1], direction)
    drawing.placed_nets.add(net)

# ================================================================================
def elbow(a, b):
    ax, ay = snap(a[0]), snap(a[1])
    bx, by = snap(b[0]), snap(b[1])
    if ax == bx or ay == by:
        return [(ax, ay), (bx, by)]
    return [(ax, ay), (bx, ay), (bx, by)]

# ================================================================================
def ordered_groups(parts):
    order = []
    found = {}
    for part in parts:
        if part["ref"] not in found:
            order.append(part["ref"])
            found[part["ref"]] = []
        found[part["ref"]].append(part)
    groups = []
    for ref in order:
        groups.append(found[ref])
    return groups

# ================================================================================
def by_role(parts, role):
    found = []
    for part in parts:
        if part["role"] == role:
            found.append(part)
    if len(found) == 0:
        raise RuntimeError("missing " + role)
    return found

# ================================================================================
def is_vertical_two_pin(part):
    if part["kind"] != "passive" or len(part["pins"]) != 2:
        return False
    numbers = [pin[0] for pin in part["pins"]]
    if "1" not in numbers or "2" not in numbers:
        return False
    return outward(pin_at(part, "1")) == (0, -1) and outward(pin_at(part, "2")) == (0, 1)

# ================================================================================
class Drawing:
    # ================================================================================
    def __init__(self):
        self.wires = []
        self.labels = []
        self.powers = []
        self.noconn = []
        self.texts = []
        self.used = set()
        self.wired = set()
        self.placed_nets = set()

    # ================================================================================
    def wire(self, points):
        cleaned = []
        for point in points:
            snapped = (snap(point[0]), snap(point[1]))
            if len(cleaned) == 0 or cleaned[-1] != snapped:
                cleaned.append(snapped)
        if len(cleaned) >= 2:
            self.wires.append(cleaned)

    # ================================================================================
    def label(self, net, x, y, direction):
        angle, justify = label_style(direction[0], direction[1])
        self.labels.append((net, snap(x), snap(y), angle, justify))

    # ================================================================================
    def add_power(self, net, x, y, direction):
        point = (snap(x), snap(y))
        self.powers.append((net, point[0], point[1], power_angle(net, direction)))
        return point

    # ================================================================================
    def text(self, text, x, y):
        self.texts.append((text, snap(x), snap(y)))

    # ================================================================================
    def snapshot(self):
        return (
            len(self.wires),
            len(self.labels),
            len(self.powers),
            len(self.noconn),
            len(self.texts),
        )

# ================================================================================
def lib_field(symbol, name):
    """(x, y, angle, justify) of a property in the library symbol, or None."""
    for item in symbol:
        if isinstance(item, list) == False or len(item) < 3:
            continue
        if item[0] != "property" or item[1] != name:
            continue
        spot = None
        justify = ""
        for sub in item[3:]:
            if isinstance(sub, list) == False or len(sub) == 0:
                continue
            if sub[0] == "at":
                angle = 0.0
                if len(sub) > 3:
                    angle = float(sub[3])
                spot = (float(sub[1]), float(sub[2]), angle)
            if sub[0] == "effects":
                for effect in sub[1:]:
                    if isinstance(effect, list) and len(effect) > 1 and effect[0] == "justify":
                        justify = " ".join(str(value) for value in effect[1:])
        if spot == None:
            return None
        return (spot[0], spot[1], spot[2], justify)
    return None

# ================================================================================
def assign_fields(model, parts):
    for part in parts:
        if is_vertical_two_pin(part):
            part["fields"] = [
                ("Reference", part["ref"], 2.54, -1.27, 0, "left"),
                ("Value", part["value"], 2.54, 1.27, 0, "left"),
            ]
            continue
        symbol = model.symbols[part["lib_id"]]
        fields = []
        for name, text, fallback in (
            ("Reference", part["ref"], -2.54),
            ("Value", part["value"], 2.54),
        ):
            spot = lib_field(symbol, name)
            if spot == None:
                fields.append((name, text, 0, fallback, 0, ""))
            else:
                fields.append((name, text, spot[0], -spot[1], spot[2], spot[3]))
        part["fields"] = fields

# ================================================================================
def stub_part(drawing, part):
    """Short stub on every unwired pin, ending in a label, a power symbol, or no-connect."""
    spots = {}
    order = []
    for number, _name, at in part["pins"]:
        key = (round(at[0], 3), round(at[1], 3))
        if key not in spots:
            spots[key] = []
            order.append(key)
        spots[key].append(number)
    ends = []
    supplies = []
    for key in order:
        numbers = spots[key]
        done = False
        for number in numbers:
            if (part["ref"], part["unit"], number) in drawing.wired:
                done = True
        if done:
            continue
        net = None
        for number in numbers:
            drawing.wired.add((part["ref"], part["unit"], number))
            if net == None:
                net = part_net(part, number)
        point = pin_world(part, numbers[0])
        direction = outward(pin_at(part, numbers[0]))
        if net == None:
            drawing.noconn.append(point)
            continue
        end = step(point, direction, STUB)
        drawing.wire([point, end])
        ends.append(end)
        supplies.append((direction, net, end))
    draw_ends(drawing, supplies, ends)

# ================================================================================
def draw_ends(drawing, supplies, ends):
    """Neighbouring pins on the same net share one power symbol or label."""
    groups = {}
    order = []
    for direction, net, end in supplies:
        key = (direction, net)
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(end)
    for key in order:
        direction, net = key
        axis = 0
        if direction[0] != 0:
            axis = 1
        points = sorted(groups[key], key=lambda point: point[axis])
        run = [points[0]]
        for point in points[1:]:
            if adjacent(run[-1], point, axis, ends):
                run.append(point)
            else:
                finish_run(drawing, run, direction, net)
                run = [point]
        finish_run(drawing, run, direction, net)

# ================================================================================
def adjacent(a, b, axis, ends):
    other = 1 - axis
    if abs(a[other] - b[other]) > 0.01:
        return False
    if b[axis] - a[axis] > 2.55:
        return False
    for end in ends:
        if abs(end[other] - a[other]) > 0.01:
            continue
        if a[axis] + 0.01 < end[axis] < b[axis] - 0.01:
            return False
    return True

# ================================================================================
def finish_run(drawing, run, direction, net):
    for a, b in zip(run, run[1:]):
        drawing.wire([a, b])
    if net in POWER:
        drawing.add_power(net, run[0][0], run[0][1], direction)
    else:
        drawing.label(net, run[0][0], run[0][1], direction)

# ================================================================================
def cap_end(drawing, part, number):
    """End of a vertical passive: stub, then a power symbol or a label turned to the right."""
    net = part_net(part, number)
    point = pin_world(part, number)
    direction = outward(pin_at(part, number))
    drawing.wired.add((part["ref"], part["unit"], number))
    if net == None:
        drawing.noconn.append(point)
        return
    end = step(point, direction, STUB)
    drawing.wire([point, end])
    if net in POWER:
        drawing.add_power(net, end[0], end[1], direction)
        return
    tip = step(end, (1, 0), STUB)
    drawing.wire([end, tip])
    drawing.label(net, tip[0], tip[1], (1, 0))

# ================================================================================
def build_chains(passives):
    """Stack vertical passives where one part's bottom net is the next part's top net."""
    vertical = []
    for part in passives:
        if is_vertical_two_pin(part):
            vertical.append(part)
    following = {}
    has_prev = set()
    for upper in vertical:
        net = part_net(upper, "2")
        if net == None or net in POWER:
            continue
        for lower in vertical:
            if lower is upper or id(lower) in has_prev:
                continue
            if part_net(lower, "1") == net:
                following[id(upper)] = lower
                has_prev.add(id(lower))
                break
    chains = []
    included = set()
    for part in passives:
        if id(part) in has_prev or id(part) in included:
            continue
        chain = [part]
        included.add(id(part))
        while id(chain[-1]) in following:
            lower = following[id(chain[-1])]
            if id(lower) in included:
                break
            chain.append(lower)
            included.add(id(lower))
        chains.append(chain)
    for part in passives:
        if id(part) not in included:
            chains.append([part])
            included.add(id(part))
    return chains

# ================================================================================
def draw_chain(drawing, chain):
    if is_vertical_two_pin(chain[0]) == False:
        chain[0]["x"] = 0.0
        chain[0]["y"] = 0.0
        stub_part(drawing, chain[0])
        drawing.used.add(id(chain[0]))
        return
    place_pin_at(chain[0], "1", 0, 0)
    cap_end(drawing, chain[0], "1")
    for upper, lower in zip(chain, chain[1:]):
        bottom = pin_world(upper, "2")
        place_pin_at(lower, "1", bottom[0], bottom[1] + 5.08)
        top = pin_world(lower, "1")
        mid = step(bottom, (0, 1), STUB)
        drawing.wire([bottom, mid])
        drawing.wire([mid, top])
        tip = step(mid, (1, 0), STUB)
        drawing.wire([mid, tip])
        drawing.label(part_net(upper, "2"), tip[0], tip[1], (1, 0))
        drawing.wired.add((upper["ref"], upper["unit"], "2"))
        drawing.wired.add((lower["ref"], lower["unit"], "1"))
    cap_end(drawing, chain[-1], "2")
    for part in chain:
        drawing.used.add(id(part))

# ================================================================================
def best_anchor(chain, anchors):
    nets = set()
    for part in chain:
        for net in net_names(part):
            if net != None:
                nets.add(net)
    best = None
    best_score = 0
    for anchor in anchors:
        score = 0
        for net in net_names(anchor):
            if net in nets:
                if net in POWER:
                    score += 1
                else:
                    score += 10
        if score > best_score:
            best = anchor["ref"]
            best_score = score
    return best

# ================================================================================
def label_box(net, x, y, angle):
    length = text_len(net)
    if angle == 0:
        return (x, y - 1.6, x + length, y + 1.6)
    if angle == 180:
        return (x - length, y - 1.6, x, y + 1.6)
    if angle == 90:
        return (x - 1.6, y - length, x + 1.6, y)
    return (x - 1.6, y, x + 1.6, y + length)

# ================================================================================
def power_value_spot(net, x, y, angle):
    """Centre of the power symbol's value text, past the glyph."""
    dx, dy = power_direction(net, angle)
    half = len(net) * 0.6
    if dx > 0:
        return (x + 4.5 + half, y)
    if dx < 0:
        return (x - 4.5 - half, y)
    if dy < 0:
        return (x, y - 5.08)
    return (x, y + 5.08)

# ================================================================================
def power_box(net, x, y, angle):
    vx, vy = power_value_spot(net, x, y, angle)
    half = len(net) * 0.6 + 0.5
    return (
        min(x - 1.5, vx - half),
        min(y - 1.5, vy - 1.2),
        max(x + 1.5, vx + half),
        max(y + 1.5, vy + 1.2),
    )

# ================================================================================
def field_box(text, x, y, angle, justify):
    length = len(text) * 1.15
    vertical = int(round(angle)) % 180 == 90
    if "left" in justify:
        low, high = 0.0, length
    elif "right" in justify:
        low, high = -length, 0.0
    else:
        low, high = -length / 2.0, length / 2.0
    if vertical:
        return (x - 1.0, y - high, x + 1.0, y - low)
    return (x + low, y - 1.0, x + high, y + 1.0)

# ================================================================================
def part_boxes(part):
    boxes = []
    xs = []
    ys = []
    for number, _name, _at in part["pins"]:
        point = pin_world(part, number)
        xs.append(point[0])
        ys.append(point[1])
    margin = 1.5
    if part["kind"] != "passive":
        margin = 3.0
    boxes.append((min(xs) - margin, min(ys) - margin, max(xs) + margin, max(ys) + margin))
    for _name, text, fx, fy, angle, justify in part.get("fields", []):
        boxes.append(field_box(text, part["x"] + fx, part["y"] + fy, angle, justify))
    return boxes

# ================================================================================
def items_box(drawing, start, end, parts):
    boxes = []
    for wire in drawing.wires[start[0]:end[0]]:
        for point in wire:
            boxes.append((point[0], point[1], point[0], point[1]))
    for net, x, y, angle, _justify in drawing.labels[start[1]:end[1]]:
        boxes.append(label_box(net, x, y, angle))
    for net, x, y, angle in drawing.powers[start[2]:end[2]]:
        boxes.append(power_box(net, x, y, angle))
    for x, y in drawing.noconn[start[3]:end[3]]:
        boxes.append((x - 1.0, y - 1.0, x + 1.0, y + 1.0))
    for text, x, y in drawing.texts[start[4]:end[4]]:
        boxes.append((x, y - 1.0, x + len(text) * 1.15, y + 1.0))
    for part in parts:
        boxes.extend(part_boxes(part))
    if len(boxes) == 0:
        return None
    return (
        min(box[0] for box in boxes),
        min(box[1] for box in boxes),
        max(box[2] for box in boxes),
        max(box[3] for box in boxes),
    )

# ================================================================================
def move_items(drawing, start, end, parts, dx, dy):
    for index in range(start[0], end[0]):
        drawing.wires[index] = [(snap(x + dx), snap(y + dy)) for x, y in drawing.wires[index]]
    for index in range(start[1], end[1]):
        net, x, y, angle, justify = drawing.labels[index]
        drawing.labels[index] = (net, snap(x + dx), snap(y + dy), angle, justify)
    for index in range(start[2], end[2]):
        net, x, y, angle = drawing.powers[index]
        drawing.powers[index] = (net, snap(x + dx), snap(y + dy), angle)
    for index in range(start[3], end[3]):
        x, y = drawing.noconn[index]
        drawing.noconn[index] = (snap(x + dx), snap(y + dy))
    for index in range(start[4], end[4]):
        text, x, y = drawing.texts[index]
        drawing.texts[index] = (text, snap(x + dx), snap(y + dy))
    for part in parts:
        part["x"] = snap(part["x"] + dx)
        part["y"] = snap(part["y"] + dy)

# ================================================================================
def make_block(drawing, parts, draw):
    start = drawing.snapshot()
    draw()
    end = drawing.snapshot()
    return {"start": start, "end": end, "parts": parts}

# ================================================================================
def anchor_block(drawing, part):
    part["x"] = 0.0
    part["y"] = 0.0
    return make_block(drawing, [part], lambda: stub_part(drawing, part))

# ================================================================================
def chain_block(drawing, chain):
    return make_block(drawing, list(chain), lambda: draw_chain(drawing, chain))

# ================================================================================
def pack_blocks(drawing, blocks, left, top, min_width):
    """Shelf-pack blocks into rows, left to right, top to bottom."""
    boxes = []
    area = 0.0
    widest = 0.0
    for block in blocks:
        box = items_box(drawing, block["start"], block["end"], block["parts"])
        boxes.append(box)
        width = box[2] - box[0]
        height = box[3] - box[1]
        area += (width + BLOCK_GAP) * (height + ROW_GAP)
        widest = max(widest, width)
    limit = max(widest, min_width, (area * 3.0) ** 0.5)
    x = left
    y = top
    row_height = 0.0
    for block, box in zip(blocks, boxes):
        width = box[2] - box[0]
        height = box[3] - box[1]
        if x > left and x + width > left + limit:
            x = left
            y += row_height + ROW_GAP
            row_height = 0.0
        dx = grid(x - box[0])
        dy = grid(y - box[1])
        move_items(drawing, block["start"], block["end"], block["parts"], dx, dy)
        x += width + BLOCK_GAP
        row_height = max(row_height, height)

# ================================================================================
def finish_page(drawing, parts):
    """Stub every pin the page layout left open, then pack the unplaced parts as blocks."""
    anchors = []
    loose = []
    for part in parts:
        if part["kind"] == "passive":
            if part["x"] == None and (id(part) in drawing.used) == False:
                loose.append(part)
        else:
            anchors.append(part)
    for part in anchors:
        if part["x"] != None:
            stub_part(drawing, part)
    placed = [part for part in parts if part["x"] != None]
    before = drawing.snapshot()
    existing = items_box(drawing, (0, 0, 0, 0, 0), before, placed)
    owners = {}
    orphans = []
    for chain in build_chains(loose):
        ref = best_anchor(chain, anchors)
        if ref == None:
            orphans.append(chain)
        else:
            owners.setdefault(ref, []).append(chain)
    blocks = []
    for group in ordered_groups(anchors):
        for part in group:
            if part["x"] == None:
                blocks.append(anchor_block(drawing, part))
        for chain in owners.get(group[0]["ref"], []):
            blocks.append(chain_block(drawing, chain))
    for chain in orphans:
        blocks.append(chain_block(drawing, chain))
    if len(blocks) == 0:
        return
    left = 0.0
    top = 0.0
    min_width = 0.0
    if existing != None:
        left = existing[0]
        top = existing[3] + 25.4
        min_width = existing[2] - existing[0]
    pack_blocks(drawing, blocks, left, top, min_width)

# ================================================================================
def layout_pack(drawing, parts):
    battery = by_role(parts, "J1")[0]
    fuse = by_role(parts, "F1")[0]
    tvs = by_role(parts, "D1")[0]
    ideal = by_role(parts, "U1")[0]
    fet = by_role(parts, "Q1")[0]
    shunt = by_role(parts, "R1")[0]
    ina = by_role(parts, "U2")[0]
    y = 190.5
    place_pin_at(battery, "1", 40.64, y)
    place_pin_at(fuse, "1", 78.74, y)
    raw = pin_world(battery, "1")
    fused_in = pin_world(fuse, "1")
    corner = (snap(raw[0] - 12.7), snap(y - 15.24))
    drawing.wire([
        raw,
        (corner[0], raw[1]),
        corner,
        (fused_in[0], corner[1]),
        fused_in,
    ])
    name_on(drawing, corner, "VBAT_RAW", (0, -1))
    mark_point(drawing, battery, "1")
    mark_point(drawing, fuse, "1")
    drawing.used.add(id(fuse))
    fused_out = pin_world(fuse, "2")
    node = (snap(fused_out[0] + 15.24), fused_out[1])
    drawing.wire([fused_out, node])
    place_pin_at(tvs, "1", node[0], node[1] - 12.7)
    cathode = pin_world(tvs, "1")
    drawing.wire([node, (node[0], cathode[1]), cathode])
    anode = pin_world(tvs, "2")
    ground = drawing.add_power("GND", anode[0] + 10.16, anode[1], (1, 0))
    drawing.wire([anode, ground])
    mark_point(drawing, tvs, "1")
    mark_point(drawing, tvs, "2")
    drawing.used.add(id(tvs))
    place_pin_at(ideal, "6", node[0] + 27.94, node[1])
    drawing.wire([node, pin_world(ideal, "6")])
    mark_point(drawing, ideal, "6")
    place_pin_at(fet, "1", node[0] + 58.42, node[1] + 10.16)
    below = (node[0], snap(node[1] + 10.16))
    drawing.wire([node, below, pin_world(fet, "1")])
    name_on(drawing, below, "VBAT_FUSED", (0, 1))
    mark_point(drawing, fet, "1")
    drawing.wire(elbow(pin_world(fet, "4"), pin_world(ideal, "5")))
    name_on(drawing, pin_world(fet, "4"), "GATE_IN", (0, -1))
    mark_point(drawing, fet, "4")
    mark_point(drawing, ideal, "5")
    drain = pin_world(fet, "5")
    place_pin_at(shunt, "1", drain[0] + 17.78, drain[1])
    drawing.wire([drain, pin_world(shunt, "1")])
    drawing.wire(elbow(pin_world(ideal, "4"), pin_world(shunt, "1")))
    mark_point(drawing, fet, "5")
    mark_point(drawing, ideal, "4")
    mark_point(drawing, shunt, "1")
    drawing.used.add(id(shunt))
    load = pin_world(shunt, "2")
    load_node = (snap(load[0] + 13.97), load[1])
    load_label = (load_node[0], snap(load_node[1] + 6.35))
    drawing.wire([load, load_node, load_label])
    drawing.label("VBAT_LOAD", load_label[0], load_label[1], (0, 1))
    drawing.placed_nets.add("VBAT_LOAD")
    vbat_point = pin_world(shunt, "1")
    vbat_label = (vbat_point[0], snap(vbat_point[1] - 6.35))
    drawing.wire([vbat_point, vbat_label])
    drawing.label("VBAT", vbat_label[0], vbat_label[1], (0, -1))
    drawing.placed_nets.add("VBAT")
    place_pin_at(ina, "10", load_node[0] + 30.48, pin_world(shunt, "1")[1])
    drawing.wire(elbow(pin_world(shunt, "1"), pin_world(ina, "10")))
    mark_point(drawing, ina, "10")
    mark_point(drawing, shunt, "2")

# ================================================================================
def layout_servos(drawing, parts):
    pca = None
    headers = {}
    resistors = {}
    for part in parts:
        if part["name"] == "PCA9685PW":
            pca = part
        elif part["name"] == "Conn_01x03":
            headers[int(part["role"][2:])] = part
        elif part["role"].startswith("Rs"):
            resistors[int(part["role"][2:])] = part
    if pca == None or len(headers) != 16 or len(resistors) != 16:
        raise RuntimeError("servo sheet is missing the PCA9685, a header, or a series resistor")
    pca["x"] = grid(76.2)
    pca["y"] = grid(165.1)
    y = 38.1
    for title, channels in SERVO_GROUPS:
        drawing.text(title, 128.27, y)
        y += 15.24
        for channel in channels:
            header = headers[channel]
            resistor = resistors[channel]
            signal = "SERVO_SIG_" + str(channel)
            led = "LED" + str(channel)
            place_pin_at(header, "1", 210.82, y)
            signal_pin = pin_for_net(resistor, signal)
            led_pin = pin_for_net(resistor, led)
            place_pin_at(resistor, signal_pin, 139.7, y)
            start = pin_world(resistor, signal_pin)
            mid = (grid((start[0] + pin_world(header, "1")[0]) / 2.0), start[1])
            drawing.wire([start, mid])
            drawing.wire([mid, pin_world(header, "1")])
            away = outward(pin_at(resistor, signal_pin))
            hang = step(mid, away, STUB)
            tip = step(hang, (-1, 0), STUB)
            drawing.wire([mid, hang, tip])
            drawing.label(signal, tip[0], tip[1], (-1, 0))
            cap_end(drawing, resistor, led_pin)
            drawing.wired.add((resistor["ref"], resistor["unit"], signal_pin))
            mark_point(drawing, header, "1")
            drawing.used.add(id(resistor))
            y += 12.7
        y += 2.54

# ================================================================================
def layout_motors(drawing, parts):
    pca = None
    drivers = {}
    screws = {}
    for part in parts:
        if part["name"] == "PCA9685PW":
            pca = part
        elif part["name"] == "TB6612FNG":
            drivers[part["role"]] = part
        elif part["role"].startswith("JM_"):
            screws[part["role"][3:]] = part
    if pca == None or len(drivers) != 2 or len(screws) != 4:
        raise RuntimeError("motor sheet is missing the PCA9685, a TB6612, or a screw terminal")
    pca["x"] = grid(50.8)
    pca["y"] = grid(50.8)
    drivers["U16"]["x"] = grid(152.4)
    drivers["U16"]["y"] = grid(50.8)
    drivers["U17"]["x"] = grid(152.4)
    drivers["U17"]["y"] = grid(127.0)
    rows = {"FL": 33.02, "FR": 58.42, "RR": 109.22, "RL": 134.62}
    for tag, row in rows.items():
        place_pin_at(screws[tag], "1", 233.68, row)

# ================================================================================
def layout_page(key, parts, model):
    assign_fields(model, parts)
    drawing = Drawing()
    if key == "pack":
        layout_pack(drawing, parts)
    elif key == "servos":
        layout_servos(drawing, parts)
    elif key == "motors":
        layout_motors(drawing, parts)
    finish_page(drawing, parts)
    missing = []
    for part in parts:
        if part["x"] == None:
            missing.append(part["role"])
    if len(missing) != 0:
        raise RuntimeError(key + " left unplaced: " + ", ".join(missing))
    return drawing

# ================================================================================
def shift_page(drawing, parts, dx, dy):
    move_items(drawing, (0, 0, 0, 0, 0), drawing.snapshot(), parts, dx, dy)

# ================================================================================
def junctions_of(wires):
    count = {}
    for wire in wires:
        for index in range(len(wire) - 1):
            for point in (wire[index], wire[index + 1]):
                count[point] = count.get(point, 0) + 1
    found = []
    for point, hits in count.items():
        if hits >= 3:
            found.append(point)
    return found

# ================================================================================
def wrap_note(text):
    words = text.split()
    lines = []
    current = ""
    for word in words:
        if len(current) + len(word) + 1 > 110:
            lines.append(current)
            current = word
        elif current == "":
            current = word
        else:
            current = current + " " + word
    if current != "":
        lines.append(current)
    return lines

# ================================================================================
def fit_paper(drawing, parts, note_lines):
    box = items_box(drawing, (0, 0, 0, 0, 0), drawing.snapshot(), parts)
    note_h = 6 + 4.5 * len(note_lines)
    need_w = (box[2] - box[0]) + 40
    need_h = (box[3] - box[1]) + note_h + 55
    paper = PAPERS[-1]
    for candidate in PAPERS:
        if candidate[1] >= need_w and candidate[2] >= need_h:
            paper = candidate
            break
    shift_page(drawing, parts, grid(20.32 - box[0]), grid(20.32 - box[1]))
    return paper

# ================================================================================
def power_symbol(net, cache):
    if net in cache:
        return cache[net]
    if cache.get("_lib") == None:
        cache["_lib"] = gen.load_library_path(os.path.join(gen.LIB_DIR, "power.kicad_sym"))
    base = "GND" if net == "GND" else "+5VA"
    node = gen.rename_for_sheet(copy.deepcopy(cache["_lib"][base]), "power:" + net)
    retarget_power(node, base, net)
    cache[net] = node
    return node

# ================================================================================
def retarget_power(node, base, net):
    if isinstance(node, list) == False or len(node) == 0:
        return
    if node[0] == "property" and len(node) > 2 and node[1] == "Value":
        node[2] = net
    if node[0] == "name" and len(node) > 1 and node[1] == base:
        node[1] = net
    for item in node:
        if isinstance(item, list):
            retarget_power(item, base, net)

# ================================================================================
def symbol_block(part, root_uuid, sheet_uuid):
    x = fmt(part["x"])
    y = fmt(part["y"])
    lines = [
        "  (symbol (lib_id " + gen.dumps(part["lib_id"]) + ") (at " + x + " " + y + " 0) (unit " + str(part["unit"]) + ")",
        "    (in_bom yes) (on_board yes) (dnp no)",
        "    (uuid " + gen.new_uuid() + ")",
    ]
    for name, text, fx, fy, angle, justify in part["fields"]:
        effects = "(effects (font (size 1.27 1.27))"
        if justify != "":
            effects += " (justify " + justify + ")"
        effects += ")"
        lines.append(
            "    (property " + gen.dumps(name) + " " + gen.dumps(text) + " (at "
            + fmt(part["x"] + fx) + " " + fmt(part["y"] + fy) + " " + fmt(angle) + ")"
        )
        lines.append("      " + effects + ")")
    lines.extend([
        '    (property "Footprint" ' + gen.dumps(part["footprint"]) + " (at " + x + " " + y + " 0)",
        "      (effects (font (size 1.27 1.27)) hide))",
        '    (property "Datasheet" "" (at ' + x + " " + y + " 0)",
        "      (effects (font (size 1.27 1.27)) hide))",
    ])
    for number, _name, _at in part["pins"]:
        lines.append("    (pin " + gen.dumps(number) + " (uuid " + gen.new_uuid() + "))")
    path = "/" + root_uuid + "/" + sheet_uuid
    lines.append(
        "    (instances (project " + gen.dumps(gen.PROJECT)
        + " (path " + gen.dumps(path)
        + " (reference " + gen.dumps(part["ref"])
        + ") (unit " + str(part["unit"]) + "))))"
    )
    lines.append("  )")
    return lines

# ================================================================================
def power_block(net, x, y, angle, root_uuid, sheet_uuid, number):
    ref = "#PWR" + str(number)
    vx, vy = power_value_spot(net, x, y, angle)
    # Field angles are relative to the symbol; this keeps the value text horizontal.
    value_angle = angle
    if angle == 180:
        value_angle = 0
    lines = [
        "  (symbol (lib_id " + gen.dumps("power:" + net) + ") (at " + fmt(x) + " " + fmt(y) + " " + str(angle) + ") (unit 1)",
        "    (in_bom yes) (on_board yes) (dnp no)",
        "    (uuid " + gen.new_uuid() + ")",
        '    (property "Reference" ' + gen.dumps(ref) + " (at " + fmt(x) + " " + fmt(y) + " 0)",
        "      (effects (font (size 1.27 1.27)) hide))",
        '    (property "Value" ' + gen.dumps(net) + " (at " + fmt(vx) + " " + fmt(vy) + " " + str(value_angle) + ")",
        "      (effects (font (size 1.27 1.27))))",
        '    (property "Footprint" "" (at ' + fmt(x) + " " + fmt(y) + " 0)",
        "      (effects (font (size 1.27 1.27)) hide))",
        '    (property "Datasheet" "" (at ' + fmt(x) + " " + fmt(y) + " 0)",
        "      (effects (font (size 1.27 1.27)) hide))",
        "    (pin \"1\" (uuid " + gen.new_uuid() + "))",
        "    (instances (project " + gen.dumps(gen.PROJECT)
        + " (path " + gen.dumps("/" + root_uuid + "/" + sheet_uuid)
        + " (reference " + gen.dumps(ref) + ") (unit 1))))",
        "  )",
    ]
    return lines

# ================================================================================
def render_child(model, parts, drawing, title, note, root_uuid, sheet_uuid, paper):
    lib_ids = []
    for part in parts:
        if part["lib_id"] not in lib_ids:
            lib_ids.append(part["lib_id"])
    cache = {}
    lines = [
        "(kicad_sch (version 20230121) (generator eeschema)",
        "  (uuid " + gen.new_uuid() + ")",
        "  (paper \"" + paper[0] + "\")",
        "  (title_block",
        "    (title " + gen.dumps("doggy Pi HAT — " + title) + ")",
        '    (comment 1 "Schematic generated from electronics/README.md.")',
        "    (comment 2 " + gen.dumps(note) + ")",
        "  )",
        "  (lib_symbols",
    ]
    for lib_id in lib_ids:
        lines.append("    " + gen.dumps(model.symbols[lib_id]))
    seen_power = []
    for net, _x, _y, _angle in drawing.powers:
        if net not in seen_power:
            seen_power.append(net)
            lines.append("    " + gen.dumps(power_symbol(net, cache)))
    lines.append("  )")
    for part in parts:
        lines.extend(symbol_block(part, root_uuid, sheet_uuid))
    for index, (net, x, y, angle) in enumerate(drawing.powers):
        lines.extend(power_block(net, x, y, angle, root_uuid, sheet_uuid, index + 1))
    for wire in drawing.wires:
        for start, end in zip(wire, wire[1:]):
            if start == end:
                continue
            points = "(xy " + fmt(start[0]) + " " + fmt(start[1]) + ") (xy " + fmt(end[0]) + " " + fmt(end[1]) + ")"
            lines.append(
                "  (wire (pts " + points + ") (stroke (width 0) (type default)) (uuid "
                + gen.new_uuid() + "))"
            )
    for point in junctions_of(drawing.wires):
        lines.append(
            "  (junction (at " + fmt(point[0]) + " " + fmt(point[1])
            + ") (diameter 0) (color 0 0 0 0) (uuid " + gen.new_uuid() + "))"
        )
    for net, x, y, angle, justify in drawing.labels:
        lines.append(
            "  (global_label " + gen.dumps(net) + " (shape passive) (at "
            + fmt(x) + " " + fmt(y) + " " + str(angle) + ")"
        )
        lines.append(
            "    (effects (font (size 1.27 1.27)) (justify " + justify + ")) (uuid "
            + gen.new_uuid() + ")"
        )
        lines.append(
            '    (property "Intersheetrefs" "${INTERSHEET_REFS}" (at '
            + fmt(x) + " " + fmt(y) + " 0)"
        )
        lines.append("      (effects (font (size 1.27 1.27)) hide))")
        lines.append("  )")
    for x, y in drawing.noconn:
        lines.append(
            "  (no_connect (at " + fmt(x) + " " + fmt(y) + ") (uuid " + gen.new_uuid() + "))"
        )
    for text, x, y in drawing.texts:
        lines.append(
            "  (text " + gen.dumps(text) + " (at " + fmt(x) + " " + fmt(y)
            + " 0) (effects (font (size 1.27 1.27)) (justify left)) (uuid "
            + gen.new_uuid() + "))"
        )
    lines.append(")")
    return "\n".join(lines) + "\n"

# ================================================================================
def render_root(root_uuid, sheets):
    lines = [
        "(kicad_sch (version 20230121) (generator eeschema)",
        "  (uuid " + root_uuid + ")",
        '  (paper "A3")',
        "  (title_block",
        '    (title "doggy Pi HAT")',
        '    (comment 1 "Schematic generated from electronics/README.md.")',
        '    (comment 2 "Servo headers are on the Servos sheet. Motor screws are on the Motors sheet.")',
        "  )",
        "  (lib_symbols)",
        "  (text \"Sixteen servo headers and four motor screw terminals are drawn on their own sheets.\" (at 20.32 269.24 0)",
        "    (effects (font (size 1.27 1.27)) (justify left)) (uuid " + gen.new_uuid() + "))",
    ]
    for index, (filename, title, desc, sheet_uuid) in enumerate(sheets):
        column = index % 3
        row = index // 3
        x = 20.32 + column * 129.54
        y = 20.32 + row * 63.5
        lines.append("  (sheet (at " + fmt(x) + " " + fmt(y) + ") (size 118.11 40.64)")
        lines.append("    (stroke (width 0.1524) (type solid))")
        lines.append("    (fill (color 0 0 0 0.0000))")
        lines.append("    (uuid " + sheet_uuid + ")")
        lines.append(
            '    (property "Sheetname" ' + gen.dumps(title) + " (at " + fmt(x) + " "
            + fmt(y - 0.8) + " 0)"
        )
        lines.append("      (effects (font (size 1.27 1.27)) (justify left bottom)))")
        lines.append(
            '    (property "Sheetfile" ' + gen.dumps(filename) + " (at " + fmt(x) + " "
            + fmt(y + 41.4) + " 0)"
        )
        lines.append("      (effects (font (size 1.27 1.27)) (justify left top)))")
        lines.append(
            "    (instances (project " + gen.dumps(gen.PROJECT)
            + " (path " + gen.dumps("/" + root_uuid)
            + " (page \"" + str(index + 2) + "\"))))"
        )
        lines.append("  )")
        lines.append(
            "  (text " + gen.dumps(desc) + " (at " + fmt(x + 2.54) + " " + fmt(y + 20.32)
            + " 0) (effects (font (size 1.27 1.27)) (justify left)) (uuid "
            + gen.new_uuid() + "))"
        )
    lines.append("  (sheet_instances (path \"/\" (page \"1\")))")
    lines.append(")")
    return "\n".join(lines) + "\n"

# ================================================================================
def render_project(model):
    known = []
    for key, _filename, _title, _desc in PAGES:
        known.append(key)
    for part in model.parts:
        if part["page"] not in known:
            raise RuntimeError(part["role"] + " is on an unknown page " + part["page"])
    root_uuid = model.root_uuid
    sheets = []
    for key, filename, title, desc in PAGES:
        parts = []
        for part in model.parts:
            if part["page"] == key:
                parts.append(part)
        notes = []
        for page, text in model.notes:
            if page == key:
                notes.extend(wrap_note(text))
        drawing = layout_page(key, parts, model)
        paper = fit_paper(drawing, parts, notes)
        note_y = paper[2] - 12.7
        for line in reversed(notes):
            drawing.texts.append((line, 15.24, note_y))
            note_y -= 4.45
        sheet_uuid = gen.new_uuid()
        path = os.path.join(gen.OUT_DIR, filename)
        text = render_child(model, parts, drawing, title, desc, root_uuid, sheet_uuid, paper)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        print(path, paper[0], "parts", len(parts), "wires", len(drawing.wires))
        sheets.append((filename, title, desc, sheet_uuid))
    root = os.path.join(gen.OUT_DIR, gen.PROJECT + ".kicad_sch")
    with open(root, "w", encoding="utf-8") as handle:
        handle.write(render_root(root_uuid, sheets))
    print(root)
