"""Split the hat into the lower and upper boards of option 3.

Running this again rewrites the generated sheets and both project roots.
It also rewrites the e-stop move on pack.kicad_sch. The original sensors
sheet is left in place and used as the source of the two sensor sheets.
"""

import os
import re
import shutil
import uuid

# ================================================================================
HERE = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.dirname(HERE)
LIB_CONN = "/usr/share/kicad/symbols/Connector_Generic.kicad_sym"
BODY_REFS = ("U15", "U16", "C33", "C34")

# ================================================================================
def uid():
    return str(uuid.uuid4())


# ================================================================================
def extract_form(text, start):
    depth = 0
    for index in range(start, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[start:index + 1], index + 1
    raise RuntimeError("unbalanced sexp")


# ================================================================================
def top_children(text):
    start = text.find("(")
    _root, end = extract_form(text, start)
    body = text[start + 1:end - 1]
    # Drop the head token "kicad_sch".
    body = body[body.find("("):]
    children = []
    index = 0
    while True:
        nxt = body.find("(", index)
        if nxt < 0:
            break
        form, stop = extract_form(body, nxt)
        children.append(form)
        index = stop
    return children


# ================================================================================
def form_head(form):
    match = re.match(r"\((\S+)", form)
    return match.group(1)


# ================================================================================
def symbol_ref(form):
    match = re.search(r'\(property "Reference" "([^"]*)"', form)
    if match == None:
        return ""
    return match.group(1)


# ================================================================================
def symbol_at(form):
    match = re.search(r'\(lib_id "[^"]+"\) \(at ([-\d.]+) ([-\d.]+)', form)
    if match == None:
        match = re.search(r'\(at ([-\d.]+) ([-\d.]+)', form)
    if match == None:
        return None
    return float(match.group(1)), float(match.group(2))


# ================================================================================
def form_point(form):
    head = form_head(form)
    if head == "symbol":
        return symbol_at(form)
    if head == "wire":
        pts = re.findall(r'\(xy ([-\d.]+) ([-\d.]+)\)', form)
        if len(pts) < 2:
            return None
        x1, y1 = float(pts[0][0]), float(pts[0][1])
        x2, y2 = float(pts[1][0]), float(pts[1][1])
        return (x1 + x2) / 2, (y1 + y2) / 2
    match = re.search(r'\(at ([-\d.]+) ([-\d.]+)', form)
    if match == None:
        return None
    return float(match.group(1)), float(match.group(2))


# ================================================================================
def nearest(point, places):
    best = None
    best_d = 1e9
    for ref, origin in places:
        dist = (point[0] - origin[0]) ** 2 + (point[1] - origin[1]) ** 2
        if dist < best_d:
            best_d = dist
            best = ref
    return best, best_d ** 0.5


# ================================================================================
def split_sensors():
    text = open(os.path.join(HERE, "sensors.kicad_sch"), encoding="utf-8").read()
    children = top_children(text)
    places = []
    for form in children:
        if form_head(form) != "symbol":
            continue
        ref = symbol_ref(form)
        if ref.startswith("#") or ref == "":
            continue
        origin = symbol_at(form)
        if origin == None:
            continue
        places.append((ref, origin))
    cap_places = [(ref, origin) for ref, origin in places if ref in ("C35", "C36")]
    imu = [origin for ref, origin in places if ref == "U15"][0]
    extra = ""
    if len(cap_places) != 0:
        extra = min(cap_places, key=lambda item: (item[1][0] - imu[0]) ** 2 + (item[1][1] - imu[1]) ** 2)[0]
    body_refs = set(BODY_REFS)
    if extra != "":
        body_refs.add(extra)
    body_parts = [(ref, origin) for ref, origin in places if ref in body_refs]
    wheel_parts = [(ref, origin) for ref, origin in places if ref not in body_refs]
    preamble_heads = ("version", "generator", "uuid", "paper", "title_block", "lib_symbols")
    preamble = []
    body_forms = []
    wheel_forms = []
    for form in children:
        head = form_head(form)
        if head in preamble_heads:
            if head == "uuid":
                continue
            preamble.append(form)
            continue
        if head == "symbol" and symbol_ref(form) in body_refs:
            body_forms.append(form)
            continue
        if head == "symbol" and symbol_ref(form) != "" and symbol_ref(form).startswith("#") == False:
            wheel_forms.append(form)
            continue
        point = form_point(form)
        if point == None:
            continue
        body_ref, body_d = nearest(point, body_parts)
        wheel_ref, wheel_d = nearest(point, wheel_parts)
        if body_d <= wheel_d and body_d < 25:
            body_forms.append(form)
        elif wheel_d < 25:
            wheel_forms.append(form)
    write_sheet("sensors-body.kicad_sch", preamble, body_forms, "IMU and barometer. The lower board.")
    write_sheet("sensors-wheels.kicad_sch", preamble, wheel_forms, "Wheel comparators, compass, and wheel headers. The upper board.")
    return extra


# ================================================================================
def write_sheet(name, preamble, forms, comment):
    order = ("version", "generator", "paper", "title_block", "lib_symbols")
    ranked = sorted(preamble, key=lambda form: order.index(form_head(form)) if form_head(form) in order else 99)
    lines = ["(kicad_sch"]
    for form in ranked:
        if form_head(form) == "generator":
            lines.append(form)
            lines.append("  (uuid " + uid() + ")")
            continue
        if form_head(form) == "title_block":
            form = re.sub(r'\(comment 2 "[^"]*"\)', '(comment 2 "' + comment + '")', form)
            if "(comment 2 " not in form:
                form = form[:-1] + '    (comment 2 "' + comment + '")\n  )'
        lines.append(form)
        lines.append("")
    for form in forms:
        lines.append(form)
        lines.append("")
    lines.append(")")
    lines.append("")
    path = os.path.join(HERE, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


# ================================================================================
def release_estop():
    path = os.path.join(HERE, "pack.kicad_sch")
    text = open(path, encoding="utf-8").read()
    text = text.replace('(global_label "ARM_SW"', '(global_label "ARM"')
    children = top_children(text)
    kept = []
    for form in children:
        if form_head(form) == "symbol" and symbol_ref(form) == "SW2":
            continue
        kept.append(form)
    lines = ["(kicad_sch"]
    for form in kept:
        lines.append(form)
    lines.append(")")
    lines.append("")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


# ================================================================================
def library_symbol(path, name):
    text = open(path, encoding="utf-8").read()
    needle = '(symbol "' + name + '"'
    start = text.find(needle)
    if start < 0:
        raise RuntimeError("missing symbol " + name)
    form, _end = extract_form(text, start)
    return form


# ================================================================================
def embedded_symbol(sch_path, qualified):
    text = open(sch_path, encoding="utf-8").read()
    needle = '(symbol "' + qualified + '"'
    start = text.find(needle)
    if start < 0:
        raise RuntimeError("missing embedded " + qualified)
    form, _end = extract_form(text, start)
    return form


# ================================================================================
def pin_table(symbol_text):
    pins = {}
    for match in re.finditer(
        r'\(pin\s+\S+\s+\S+\s+\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)[\s\S]{0,400}?\(number "([^"]+)"',
        symbol_text,
    ):
        pins[match.group(4)] = (float(match.group(1)), float(match.group(2)), float(match.group(3)))
    return pins


# ================================================================================
def rename_symbol(form, qualified):
    return re.sub(r'^\(symbol "[^"]+"', '(symbol "' + qualified + '"', form, count=1)


# ================================================================================
def stub(wx, wy, angle):
    """Pin (at) is the tip. Angle 0 leaves to the left and 180 to the right.
    A vertical resistor points 90 toward the bottom and 270 toward the top.
    """
    if angle == 0:
        return wx - 7.62, wy, 180
    if angle == 180:
        return wx + 7.62, wy, 0
    if angle == 90:
        return wx, wy + 7.62, 270
    return wx, wy - 7.62, 90


# ================================================================================
def world_pin(origin, pin):
    px, py, angle = pin
    # Library Y is up. Schematic Y is down.
    return origin[0] + px, origin[1] - py, angle


# ================================================================================
def wire(x1, y1, x2, y2):
    return (
        "  (wire (pts (xy {0} {1}) (xy {2} {3}))\n"
        "    (stroke (width 0) (type default))\n"
        "    (uuid {4})\n"
        "  )"
    ).format(round(x1, 4), round(y1, 4), round(x2, 4), round(y2, 4), uid())


# ================================================================================
def global_label(name, x, y, rot):
    return (
        '  (global_label "{0}" (shape passive) (at {1} {2} {3})\n'
        "    (effects (font (size 1.27 1.27)) (justify left))\n"
        "    (uuid {4})\n"
        "  )"
    ).format(name, round(x, 4), round(y, 4), rot, uid())


# ================================================================================
def power_at(lib_id, value, x, y, rot, ref):
    return (
        '  (symbol (lib_id "{0}") (at {1} {2} {3}) (unit 1)\n'
        "    (in_bom yes) (on_board yes) (dnp no)\n"
        "    (uuid {4})\n"
        '    (property "Reference" "{5}" (at {1} {2} 0)\n'
        "      (effects (font (size 1.27 1.27)) hide)\n"
        "    )\n"
        '    (property "Value" "{6}" (at {1} {2} 0)\n'
        "      (effects (font (size 1.27 1.27)) hide)\n"
        "    )\n"
        '    (property "Footprint" "" (at {1} {2} 0)\n'
        "      (effects (font (size 1.27 1.27)) hide)\n"
        "    )\n"
        '    (property "Datasheet" "" (at {1} {2} 0)\n'
        "      (effects (font (size 1.27 1.27)) hide)\n"
        "    )\n"
        '    (pin "1" (uuid {7}))\n'
        "  )"
    ).format(lib_id, round(x, 4), round(y, 4), rot, uid(), ref, value, uid())


# ================================================================================
def placed_symbol(lib_id, x, y, rot, ref, value, footprint, pins):
    pin_lines = "\n".join('    (pin "{0}" (uuid {1}))'.format(number, uid()) for number in pins)
    return (
        '  (symbol (lib_id "{0}") (at {1} {2} {3}) (unit 1)\n'
        "    (in_bom yes) (on_board yes) (dnp no)\n"
        "    (uuid {4})\n"
        '    (property "Reference" "{5}" (at {1} {6} 0)\n'
        "      (effects (font (size 1.27 1.27)))\n"
        "    )\n"
        '    (property "Value" "{7}" (at {1} {8} 0)\n'
        "      (effects (font (size 1.27 1.27)))\n"
        "    )\n"
        '    (property "Footprint" "{9}" (at {1} {2} 0)\n'
        "      (effects (font (size 1.27 1.27)) hide)\n"
        "    )\n"
        '    (property "Datasheet" "" (at {1} {2} 0)\n'
        "      (effects (font (size 1.27 1.27)) hide)\n"
        "    )\n"
        "{10}\n"
        "  )"
    ).format(
        lib_id, round(x, 4), round(y, 4), rot, uid(), ref,
        round(y - 2, 4), value, round(y + 2, 4), footprint, pin_lines,
    )


# ================================================================================
def connect_pin(chunks, origin, pins, number, kind, name, ref_counter):
    wx, wy, angle = world_pin(origin, pins[number])
    ox, oy, label_rot = stub(wx, wy, angle)
    chunks.append(wire(wx, wy, ox, oy))
    if kind == "global":
        chunks.append(global_label(name, ox, oy, label_rot))
        return
    ref_counter[0] += 1
    chunks.append(power_at("power:" + name, name, ox, oy, 0, "#PWR" + str(ref_counter[0])))


# ================================================================================
def sheet_document(title, comment, lib_forms, body):
    libs = "\n".join(lib_forms)
    return (
        "(kicad_sch (version 20230121) (generator eeschema)\n"
        "  (uuid {0})\n"
        '  (paper "A3")\n'
        "  (title_block\n"
        '    (title "{1}")\n'
        '    (comment 1 "{2}")\n'
        "  )\n"
        "  (lib_symbols\n"
        "{3}\n"
        "  )\n"
        "{4}\n"
        ")\n"
    ).format(uid(), title, comment, libs, "\n".join(body))


# ================================================================================
def build_link_sheets():
    conn = rename_symbol(
        library_symbol(LIB_CONN, "Conn_02x12_Odd_Even"),
        "Connector_Generic:Conn_02x12_Odd_Even",
    )
    pins = pin_table(conn)
    resistor = embedded_symbol(os.path.join(HERE, "pi-header.kicad_sch"), "Device:R")
    switch = embedded_symbol(os.path.join(HERE, "pack.kicad_sch"), "Switch:SW_SPST")
    gnd = embedded_symbol(os.path.join(HERE, "pi-header.kicad_sch"), "power:GND")
    v3 = embedded_symbol(os.path.join(HERE, "pi-header.kicad_sch"), "power:+3V3")
    v5 = embedded_symbol(os.path.join(HERE, "pi-5v.kicad_sch"), "power:+5V_PI")
    libs = [conn, resistor, switch, gnd, v3, v5]
    r_pins = pin_table(resistor)
    sw_pins = pin_table(switch)
    footprint = "Connector_PinSocket_2.54mm:PinSocket_2x12_P2.54mm_Vertical"
    nets = {}
    for number in range(1, 9):
        nets[str(number)] = ("global", "VBAT_LOAD")
    for number in (9, 10, 11, 12, 13, 14, 15, 16, 20):
        nets[str(number)] = ("power", "GND")
    nets["17"] = ("power", "+3V3")
    nets["18"] = ("power", "+3V3")
    nets["21"] = ("global", "I2C1_SDA")
    nets["22"] = ("global", "I2C1_SCL")
    nets["23"] = ("power", "+5V_PI")
    nets["24"] = ("power", "+5V_PI")
    origin = (160.0, 120.0)
    lower = []
    upper = []
    lower.append(placed_symbol(
        "Connector_Generic:Conn_02x12_Odd_Even", origin[0], origin[1], 0,
        "J30", "STACK_PWR", footprint, sorted(pins, key=lambda n: int(n)),
    ))
    upper.append(placed_symbol(
        "Connector_Generic:Conn_02x12_Odd_Even", origin[0], origin[1], 0,
        "J30", "STACK_PWR", footprint, sorted(pins, key=lambda n: int(n)),
    ))
    low_count = [0]
    up_count = [0]
    for number, kind_name in nets.items():
        connect_pin(lower, origin, pins, number, kind_name[0], kind_name[1], low_count)
        connect_pin(upper, origin, pins, number, kind_name[0], kind_name[1], up_count)
    connect_pin(lower, origin, pins, "19", "global", "ACT_DISABLE", low_count)
    connect_pin(upper, origin, pins, "19", "global", "ACT_LATCH", up_count)
    # NC e-stop between the latch output and the actuator-disable net.
    sw_at = (100.0, 150.0)
    upper.append(placed_symbol(
        "Switch:SW_SPST", sw_at[0], sw_at[1], 0,
        "SW2", "ESTOP_NC", "Button_Switch_SMD:SW_SPST_B3U-1000P", ("1", "2"),
    ))
    connect_pin(upper, sw_at, sw_pins, "1", "global", "ACT_LATCH", up_count)
    connect_pin(upper, sw_at, sw_pins, "2", "global", "ACT_DISABLE", up_count)
    r_at = (100.0, 165.0)
    upper.append(placed_symbol(
        "Device:R", r_at[0], r_at[1], 0,
        "R78", "100k", "Resistor_SMD:R_0805_2012Metric", ("1", "2"),
    ))
    connect_pin(upper, r_at, r_pins, "1", "global", "ACT_DISABLE", up_count)
    connect_pin(upper, r_at, r_pins, "2", "power", "+3V3", up_count)
    note = (
        '  (text "J30 mates with the other board. Pins 1-8 are pack power, '
        '9-16 and 20 are ground, 17-18 are hat 3.3 V, 19 is the latch, '
        '21-22 are I2C1 after the series resistors, 23-24 are +5V_PI. '
        'The paralleled pack pins must be rated 15 A together."\n'
        "    (at 25.4 25.4 0)\n"
        "    (effects (font (size 1.27 1.27)) (justify left))\n"
        "    (uuid {0})\n"
        "  )"
    ).format(uid())
    lower.insert(0, note)
    upper.insert(0, note)
    path_l = os.path.join(HERE, "link-lower.kicad_sch")
    path_u = os.path.join(HERE, "link-upper.kicad_sch")
    with open(path_l, "w", encoding="utf-8") as handle:
        handle.write(sheet_document(
            "Lower stack link",
            "Pack power, 3.3 V, Pi 5 V, I2C, and the latch output leave through J30.",
            libs, lower,
        ))
    with open(path_u, "w", encoding="utf-8") as handle:
        handle.write(sheet_document(
            "Upper stack link",
            "J30 enters. The e-stop opens the actuator disable and leaves the Pi rail up.",
            libs, upper,
        ))
    return pins


# ================================================================================
def build_upper_header():
    conn = embedded_symbol(os.path.join(HERE, "pi-header.kicad_sch"), "Connector_Generic:Conn_02x20_Odd_Even")
    gnd = embedded_symbol(os.path.join(HERE, "pi-header.kicad_sch"), "power:GND")
    v5 = embedded_symbol(os.path.join(HERE, "pi-header.kicad_sch"), "power:+5V_HDR")
    pins = pin_table(conn)
    origin = (80.0, 140.0)
    body = []
    body.append(placed_symbol(
        "Connector_Generic:Conn_02x20_Odd_Even", origin[0], origin[1], 0,
        "J2", "PI_GPIO", "Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical",
        sorted(pins, key=lambda n: int(n)),
    ))
    count = [0]
    for number in ("2", "4"):
        connect_pin(body, origin, pins, number, "power", "+5V_HDR", count)
    for number in ("6", "9", "14", "20", "25", "30", "34", "39"):
        connect_pin(body, origin, pins, number, "power", "GND", count)
    connect_pin(body, origin, pins, "16", "global", "ENC_INT", count)
    body.insert(0, (
        '  (text "Stacking header. I2C arrives on J30, after the series resistors '
        'on the lower board. ID_SD and ID_SC are not connected on this board. '
        'Pins 2 and 4 are the Pi 5 V feed and are not tied to +5V_PI."\n'
        "    (at 25.4 25.4 0)\n"
        "    (effects (font (size 1.27 1.27)) (justify left))\n"
        "    (uuid {0})\n"
        "  )"
    ).format(uid()))
    path = os.path.join(HERE, "header-upper.kicad_sch")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(sheet_document(
            "Upper Pi header",
            "Pass-through header. No ID EEPROM and no I2C pull-ups.",
            [conn, gnd, v5], body,
        ))


# ================================================================================
def sheet_block(name, filename, x, y, page, project, root_uuid):
    return (
        "  (sheet (at {0} {1}) (size 110 28)\n"
        "    (stroke (width 0.1524) (type solid))\n"
        "    (fill (color 0 0 0 0.0000))\n"
        "    (uuid {2})\n"
        '    (property "Sheetname" "{3}" (at {0} {4} 0)\n'
        "      (effects (font (size 1.27 1.27)) (justify left bottom))\n"
        "    )\n"
        '    (property "Sheetfile" "{5}" (at {0} {6} 0)\n'
        "      (effects (font (size 1.27 1.27)) (justify left top))\n"
        "    )\n"
        "    (instances\n"
        '      (project "{7}"\n'
        '        (path "/{8}" (page "{9}"))\n'
        "      )\n"
        "    )\n"
        "  )"
    ).format(x, y, uid(), name, y - 1, filename, y + 29, project, root_uuid, page)


# ================================================================================
def write_root(project, title, comment, sheets):
    directory = os.path.join(KICAD, project)
    os.makedirs(directory, exist_ok=True)
    root_uuid = uid()
    blocks = []
    for index, (name, filename) in enumerate(sheets):
        col = index % 3
        row = index // 3
        blocks.append(sheet_block(
            name, "../doggy-hat/" + filename,
            20 + col * 120, 20 + row * 40,
            str(index + 2), project, root_uuid,
        ))
    text = (
        "(kicad_sch (version 20230121) (generator eeschema)\n"
        "  (uuid {0})\n"
        '  (paper "A3")\n'
        "  (title_block\n"
        '    (title "{1}")\n'
        '    (comment 1 "{2}")\n'
        "  )\n"
        "  (lib_symbols)\n"
        '  (text "{2}" (at 20 150 0)\n'
        "    (effects (font (size 1.27 1.27)) (justify left))\n"
        "    (uuid {3})\n"
        "  )\n"
        "{4}\n"
        "  (sheet_instances\n"
        '    (path "/" (page "1"))\n'
        "  )\n"
        ")\n"
    ).format(root_uuid, title, comment, uid(), "\n".join(blocks))
    with open(os.path.join(directory, project + ".kicad_sch"), "w", encoding="utf-8") as handle:
        handle.write(text)
    pro_src = os.path.join(HERE, "doggy-hat.kicad_pro")
    pro = open(pro_src, encoding="utf-8").read().replace(
        '"filename": "doggy-hat.kicad_pro"',
        '"filename": "' + project + '.kicad_pro"',
    )
    with open(os.path.join(directory, project + ".kicad_pro"), "w", encoding="utf-8") as handle:
        handle.write(pro)


# ================================================================================
def retire_old_root():
    text = (
        "(kicad_sch (version 20230121) (generator eeschema)\n"
        "  (uuid {0})\n"
        '  (paper "A4")\n'
        "  (title_block\n"
        '    (title "doggy hat moved")\n'
        '    (comment 1 "Open doggy-lower or doggy-upper. This root is not the circuit.")\n'
        "  )\n"
        "  (lib_symbols)\n"
        '  (text "The hat is two boards. Lower: electronics/kicad/doggy-lower. '
        'Upper: electronics/kicad/doggy-upper. Sheet files still live in doggy-hat."\n'
        "    (at 20 20 0)\n"
        "    (effects (font (size 1.27 1.27)) (justify left))\n"
        "    (uuid {1})\n"
        "  )\n"
        ")\n"
    ).format(uid(), uid())
    with open(os.path.join(HERE, "doggy-hat.kicad_sch"), "w", encoding="utf-8") as handle:
        handle.write(text)


# ================================================================================
def main():
    extra = split_sensors()
    release_estop()
    build_link_sheets()
    build_upper_header()
    write_root("doggy-lower", "doggy lower HAT", "Pack, Pi 5 V, ID EEPROM, IMU, and barometer.", (
        ("Pack input", "pack.kicad_sch"),
        ("Pi 5 V", "pi-5v.kicad_sch"),
        ("Pi header", "pi-header.kicad_sch"),
        ("Body sensors", "sensors-body.kicad_sch"),
        ("Stack link", "link-lower.kicad_sch"),
    ))
    write_root("doggy-upper", "doggy upper board", "Actuators, encoders, and the e-stop.", (
        ("Servo rail", "servo-rail.kicad_sch"),
        ("Motor rail", "motor-rail.kicad_sch"),
        ("Servos", "servos.kicad_sch"),
        ("Motors", "motors.kicad_sch"),
        ("RP2040", "rp2040.kicad_sch"),
        ("Wheel sensors", "sensors-wheels.kicad_sch"),
        ("Stack link", "link-upper.kicad_sch"),
        ("Pi header", "header-upper.kicad_sch"),
    ))
    retire_old_root()
    print("split ok, body decoupling cap", extra)


# ================================================================================
if __name__ == "__main__":
    main()
