#!/usr/bin/env python3
"""Generate the doggy HAT schematic for KiCad 7.

Values that set a voltage or a current limit are calculated in this file from
the datasheets in /datasheets. Connectivity uses the KiCad 7 symbol pin names.
"""

import copy
import os
import re
import uuid

# ================================================================================
LIB_DIR = "/usr/share/kicad/symbols"
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT = "doggy-hat"

# ================================================================================
def tokenize(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c in "()":
            out.append(c)
            i += 1
            continue
        if c == '"':
            i += 1
            buf = []
            while i < n:
                if text[i] == "\\":
                    buf.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                buf.append(text[i])
                i += 1
            out.append("".join(buf))
            continue
        j = i
        while i < n and (not text[i].isspace()) and text[i] not in '()\"':
            i += 1
        out.append(text[j:i])
    return out

# ================================================================================
def parse(text):
    toks = tokenize(text)

    # ================================================================================
    def read(i):
        if toks[i] != "(":
            return toks[i], i + 1
        i += 1
        items = []
        while toks[i] != ")":
            item, i = read(i)
            items.append(item)
        return items, i + 1

    tree, _ = read(0)
    return tree

# ================================================================================
ATOMS = {
    "yes", "no", "hide", "default", "line", "input", "output", "bidirectional",
    "passive", "power_in", "power_out", "open_collector", "no_connect",
    "tri_state", "free", "left", "right", "top", "bottom", "center",
    "background", "outline", "none", "solid", "dash", "dot", "inverted",
    "clock", "italic", "bold", "unspecified",
}

# Parents whose numeric children are coordinates or sizes, not pin numbers.
NUMERIC_PARENTS = {
    "at", "xy", "start", "end", "mid", "center", "radius", "length", "width",
    "size", "offset", "unit", "version", "comment",
}

# ================================================================================
def dumps(node, parent=None):
    if isinstance(node, list):
        key = node[0] if node and isinstance(node[0], str) else None
        parts = []
        for index, part in enumerate(node):
            if index == 0 and isinstance(part, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", part):
                parts.append(part)
            else:
                parts.append(dumps(part, key))
        return "(" + " ".join(parts) + ")"
    if parent in NUMERIC_PARENTS and re.fullmatch(r"-?\d+(\.\d+)?", node):
        return node
    if node in ATOMS:
        return node
    escaped = node.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + escaped + '"'

# ================================================================================
def load_library_path(path):
    tree = parse(open(path, encoding="utf-8").read())
    found = {}
    for node in tree[1:]:
        if isinstance(node, list) and node and node[0] == "symbol":
            found[node[1]] = node
    return found

# ================================================================================
def child(node, tag):
    for item in node[1:]:
        if isinstance(item, list) and item and item[0] == tag:
            return item
    return None

# ================================================================================
def resolve(symbols, name):
    node = copy.deepcopy(symbols[name])
    ext = child(node, "extends")
    if ext == None:
        return node
    parent = resolve(symbols, ext[1])
    merged = copy.deepcopy(parent)
    merged[1] = name
    replaced = set()
    for item in node[1:]:
        if isinstance(item, list) and item and item[0] == "property":
            replaced.add(item[1])
    merged = [
        part
        for part in merged
        if not (
            isinstance(part, list)
            and part
            and part[0] == "property"
            and part[1] in replaced
        )
    ]
    for item in node[1:]:
        if isinstance(item, list) and item and item[0] == "property":
            merged.append(copy.deepcopy(item))
    return merged

# ================================================================================
def short_name(name):
    return name.split(":")[-1]

# ================================================================================
def property_of(node, key):
    for item in node:
        if isinstance(item, list) and item and item[0] == "property" and item[1] == key:
            return item[2]
    return ""

# ================================================================================
def rename_for_sheet(node, lib_id):
    renamed = copy.deepcopy(node)
    renamed[1] = lib_id
    new = short_name(lib_id)
    for item in renamed:
        if isinstance(item, list) and item and item[0] == "symbol":
            match = re.fullmatch(r".*_(\d+)_(\d+)", item[1])
            if match != None:
                item[1] = new + "_" + match.group(1) + "_" + match.group(2)
    return renamed

# ================================================================================
def pin_fields(pin):
    number = None
    name = None
    at = None
    for item in pin[1:]:
        if isinstance(item, list) and item and item[0] == "number":
            number = item[1]
        elif isinstance(item, list) and item and item[0] == "name":
            name = item[1]
        elif isinstance(item, list) and item and item[0] == "at":
            at = (float(item[1]), float(item[2]), int(float(item[3])))
    return number, name, at

# ================================================================================
def units_of(node):
    """Electrical units, one body style each. Style 1 wins over style 0."""
    short = short_name(node[1])
    found = {}
    for item in node:
        if not (isinstance(item, list) and item and item[0] == "symbol"):
            continue
        match = re.fullmatch(re.escape(short) + r"_(\d+)_(\d+)", item[1])
        if match == None:
            continue
        unit = int(match.group(1))
        style = int(match.group(2))
        pins = []
        for part in item:
            if isinstance(part, list) and part and part[0] == "pin":
                number, name, at = pin_fields(part)
                if at != None:
                    pins.append((number, name, at))
        if len(pins) == 0:
            continue
        previous = found.get(unit)
        if previous == None or style == 1:
            found[unit] = pins
    common = found.pop(0, None)
    if common != None and len(found) == 0:
        found[1] = common
    elif common != None:
        found.setdefault(1, [])
        found[1] = found[1] + common
    if len(found) == 0:
        raise RuntimeError("no pins in " + node[1])
    return found

# ================================================================================
def new_uuid():
    return str(uuid.uuid4())

# ================================================================================
def classify(name, pin_count):
    if name.startswith("Conn") or name.startswith("Screw") or "USB" in name:
        return "connector"
    if name.startswith("SW"):
        return "switch"
    if pin_count <= 2 and name in ("R", "C", "L", "D", "D_Zener", "Fuse", "Crystal"):
        return "passive"
    return "ic"

# ================================================================================
class Sheet:
    def __init__(self):
        self.root_uuid = new_uuid()
        self.symbols = {}
        self.placements = []
        self.labels = []
        self.notes = []
        self.occupied = set()
        self.libraries = {}
        self.ref_count = {}
        self.page = "pack"
        self.parts = []

    # ================================================================================
    def library(self, filename):
        if filename not in self.libraries:
            path = filename if os.path.isabs(filename) else os.path.join(LIB_DIR, filename)
            self.libraries[filename] = load_library_path(path)
        return self.libraries[filename]

    # ================================================================================
    def define(self, filename, name):
        lib_id = os.path.splitext(os.path.basename(filename))[0] + ":" + name
        if lib_id in self.symbols:
            return lib_id
        resolved = resolve(self.library(filename), name)
        self.symbols[lib_id] = rename_for_sheet(resolved, lib_id)
        return lib_id

    # ================================================================================
    def claim(self, points):
        for point in points:
            key = (round(point[0], 4), round(point[1], 4))
            if key in self.occupied:
                return False
        for point in points:
            self.occupied.add((round(point[0], 4), round(point[1], 4)))
        return True

    # ================================================================================
    def add(self, filename, name, ref, value, nets, footprint=""):
        lib_id = self.define(filename, name)
        if footprint == "":
            footprint = property_of(self.symbols[lib_id], "Footprint")
        unit_pins = units_of(self.symbols[lib_id])
        normalized = {}
        for key, net in nets.items():
            if isinstance(key, tuple):
                normalized[key] = net
            else:
                normalized[(1, str(key))] = net
        missing = []
        for unit, pins in sorted(unit_pins.items()):
            for number, _name, _at in pins:
                if (unit, number) not in normalized:
                    missing.append("unit " + str(unit) + " pin " + number + " " + _name)
        if len(missing) != 0:
            raise RuntimeError(ref + " " + name + " missing " + ", ".join(missing))
        prefix = property_of(self.symbols[lib_id], "Reference")
        if prefix == "":
            prefix = "U"
        self.ref_count[prefix] = self.ref_count.get(prefix, 0) + 1
        assigned = prefix + str(self.ref_count[prefix])
        role = ref
        ref = assigned
        for unit, pins in sorted(unit_pins.items()):
            self.parts.append(
                {
                    "page": self.page,
                    "lib_id": lib_id,
                    "name": name,
                    "role": role,
                    "ref": ref,
                    "value": value,
                    "footprint": footprint,
                    "unit": unit,
                    "pins": pins,
                    "nets": normalized,
                    "kind": classify(name, len(pins)),
                    "x": None,
                    "y": None,
                }
            )

    # ================================================================================
    def note(self, text):
        self.notes.append((self.page, text))

    # ================================================================================
    def render(self):
        chunks = [
            "(kicad_sch (version 20230121) (generator eeschema)",
            "  (uuid " + self.root_uuid + ")",
            '  (paper "A0")',
            "  (title_block",
            '    (title "doggy Pi HAT")',
            '    (comment 1 "Schematic generated from electronics/README.md.")',
            '    (comment 2 "Feedback and sense values are calculated in generate_schematic.py.")',
            "  )",
            "  (lib_symbols",
        ]
        for node in self.symbols.values():
            chunks.append("    " + dumps(node))
        chunks.append("  )")
        for text in self.notes:
            chunks.append(
                "  (text "
                + dumps(text)
                + " (at 25 20 0) (effects (font (size 1.27 1.27)) (justify left))"
                + " (uuid "
                + new_uuid()
                + "))"
            )
        for place in self.placements:
            uid = new_uuid()
            x, y = place["at"]
            chunks.append(
                '  (symbol (lib_id '
                + dumps(place["lib_id"])
                + ") (at "
                + str(x)
                + " "
                + str(y)
                + " 0) (unit "
                + str(place["unit"])
                + ")"
            )
            chunks.append("    (in_bom yes) (on_board yes) (dnp no)")
            chunks.append("    (uuid " + uid + ")")
            chunks.append(
                '    (property "Reference" '
                + dumps(place["ref"])
                + " (at "
                + str(x)
                + " "
                + str(y + 5)
                + " 0) (effects (font (size 1.27 1.27))))"
            )
            chunks.append(
                '    (property "Value" '
                + dumps(place["value"])
                + " (at "
                + str(x)
                + " "
                + str(y - 5)
                + " 0) (effects (font (size 1.27 1.27))))"
            )
            chunks.append(
                '    (property "Footprint" '
                + dumps(place["footprint"])
                + " (at "
                + str(x)
                + " "
                + str(y)
                + " 0) (effects (font (size 1.27 1.27)) hide))"
            )
            chunks.append(
                '    (property "Datasheet" "" (at '
                + str(x)
                + " "
                + str(y)
                + " 0) (effects (font (size 1.27 1.27)) hide))"
            )
            for number, _name, _at in place["pins"]:
                chunks.append(
                    '    (pin ' + dumps(number) + " (uuid " + new_uuid() + "))"
                )
            chunks.append("    (instances (project " + dumps(PROJECT) + " (path " + dumps("/" + self.root_uuid))
            chunks.append(
                '      (reference ' + dumps(place["ref"]) + ") (unit " + str(place["unit"]) + "))))"
            )
            chunks.append("  )")
            for number, _name, at in place["pins"]:
                net = place["nets"][(place["unit"], number)]
                wx = x + at[0]
                wy = y - at[1]
                if net == None:
                    chunks.append(
                        "  (no_connect (at "
                        + str(wx)
                        + " "
                        + str(wy)
                        + ") (uuid "
                        + new_uuid()
                        + "))"
                    )
                else:
                    angle = at[2]
                    justify = "left" if angle in (0, 90) else "right"
                    chunks.append(
                        "  (global_label "
                        + dumps(net)
                        + " (shape passive) (at "
                        + str(wx)
                        + " "
                        + str(wy)
                        + " "
                        + str(angle)
                        + ")"
                    )
                    chunks.append(
                        "    (effects (font (size 1.27 1.27)) (justify "
                        + justify
                        + ")) (uuid "
                        + new_uuid()
                        + "))"
                    )
        chunks.append("  (sheet_instances (path \"/\" (page \"1\")))")
        chunks.append(")")
        return "\n".join(chunks) + "\n"

# ================================================================================
def r(sheet, ref, value, a, b):
    sheet.add("Device.kicad_sym", "R", ref, value, {"1": a, "2": b},
              "Resistor_SMD:R_0805_2012Metric")

# ================================================================================
def c(sheet, ref, value, a, b):
    sheet.add("Device.kicad_sym", "C", ref, value, {"1": a, "2": b},
              "Capacitor_SMD:C_0805_2012Metric")

# ================================================================================
def build():
    sheet = Sheet()
    sheet.page = "pack"
    sheet.note(
        "Pack input. INA228 at 0x45 (A1 and A0 tied to VS). "
        "LR8 makes the always-on 3.3 V that holds the cutoff latch. "
        "Shunt is 10 mohm: 15 A is 150 mV."
    )

    # Battery input, reverse-polarity ideal diode, pack monitor.
    sheet.add("Connector.kicad_sym", "Screw_Terminal_01x02", "J1", "BAT",
              {"1": "VBAT_RAW", "2": "GND"},
              "TerminalBlock:TerminalBlock_bornier-2_P5.08mm")
    sheet.add("Device.kicad_sym", "Fuse", "F1", "20A",
              {"1": "VBAT_RAW", "2": "VBAT_FUSED"})
    sheet.add("Device.kicad_sym", "D_Zener", "D1", "SMBJ36A",
              {"1": "VBAT_FUSED", "2": "GND"},
              "Diode_SMD:D_SMB")
    sheet.add("Power_Management.kicad_sym", "LM74700", "U1", "LM74700-Q1",
              {"1": "VCAP_IN", "2": "GND", "3": "VBAT_FUSED", "4": "VBAT",
               "5": "GATE_IN", "6": "VBAT_FUSED"})
    c(sheet, "C1", "100nF", "VCAP_IN", "VBAT_FUSED")
    sheet.add("Transistor_FET.kicad_sym", "CSD18540Q5B", "Q1", "CSD18540Q5B",
              {"1": "VBAT_FUSED", "2": "VBAT_FUSED", "3": "VBAT_FUSED",
               "4": "GATE_IN", "5": "VBAT"})
    # High-side shunt, 10 mohm. 15 A gives 150 mV, inside the INA228 163.84 mV range.
    sheet.add("Device.kicad_sym", "R", "R1", "10mohm 3W",
              {"1": "VBAT", "2": "VBAT_LOAD"},
              "Resistor_SMD:R_2512_6332Metric")
    local = os.path.join(OUT_DIR, "doggy-hat.kicad_sym")
    sheet.add(local, "INA228", "U2", "INA228",
              {"1": "+3V3_AON", "2": "+3V3_AON", "3": "ALERT_N",
               "4": "I2C1_SDA", "5": "I2C1_SCL", "6": "+3V3_AON",
               "7": "GND", "8": "VBAT_LOAD", "9": "VBAT_LOAD", "10": "VBAT"})
    c(sheet, "C2", "100nF", "+3V3_AON", "GND")
    r(sheet, "R2", "10k", "ALERT_N", "+3V3_AON")

    # Always-on 3.3 V from the pack. LR8: Vout = 1.20*(1+R1/R2) + 10uA*R1.
    # R1 = 16.2k top, R2 = 10.0k bottom gives about 3.31 V.
    sheet.add("Regulator_Linear.kicad_sym", "LR8K4-G", "U3", "LR8K4-G",
              {"1": "VBAT_LOAD", "2": "+3V3_AON", "3": "LR8_ADJ"})
    r(sheet, "R3", "16.2k", "+3V3_AON", "LR8_ADJ")
    r(sheet, "R4", "10.0k", "LR8_ADJ", "GND")
    c(sheet, "C3", "100nF", "+3V3_AON", "GND")

    # Fault latch. Power-on capacitor holds reset low so the Pi buck starts enabled.
    sheet.add("74xx.kicad_sym", "74HC00", "U4", "74HC00", {
        (1, "1"): "ALERT_N", (1, "2"): "FAULT_N", (1, "3"): "FAULT",
        (2, "4"): "RESET_N", (2, "5"): "FAULT", (2, "6"): "FAULT_N",
        (3, "9"): "ARM_SW", (3, "10"): "FAULT_N", (3, "8"): "ACT_DISABLE",
        (4, "12"): "GND", (4, "13"): "GND", (4, "11"): None,
        (5, "14"): "+3V3_AON", (5, "7"): "GND",
    })
    r(sheet, "R5", "10k", "RESET_N", "+3V3_AON")
    c(sheet, "C4", "100nF", "RESET_N", "GND")
    sheet.add("Switch.kicad_sym", "SW_Push", "SW1", "RESET",
              {"1": "RESET_N", "2": "GND"})
    r(sheet, "R6", "100k", "ARM_SW", "GND")
    sheet.add("Switch.kicad_sym", "SW_SPST", "SW2", "ESTOP_NC",
              {"1": "ARM", "2": "ARM_SW"})

    sheet.page = "pi-5v"
    sheet.note(
        "Pi buck. TPS54560B reference is 0.8 V, so 105k over 20.0k is 5.00 V. "
        "RT 243k, the 10 uH inductor, and the compensation network are starting values."
    )
    add_pi_buck(sheet)
    add_ideal_or(sheet, "U5", "Q2", "C5", "+5V_PI", "+5V_HDR", "VCAP_PI", "GATE_PI")
    sheet.add("Regulator_Linear.kicad_sym", "AMS1117CD-3.3", "U8", "AMS1117-3.3",
              {"1": "GND", "2": "+3V3", "3": "+5V_PI"})
    c(sheet, "C20", "10uF", "+5V_PI", "GND")
    c(sheet, "C21", "10uF", "+3V3", "GND")

    sheet.page = "servo-rail"
    sheet.note(
        "Servo rail, LM5176, stuffed for 5.00 V (105k/20.0k). "
        "Sense resistor 5 mohm is about 16 A at the 80 mV buck limit. "
        "Slope 220 pF is a starting value."
    )
    add_lm5176(sheet, "servo", "U6", "+5V_SERVO", "105k", "20.0k", "5mohm", "ACT_DISABLE")

    sheet.page = "motor-rail"
    sheet.note(
        "Motor rail, LM5176, stuffed for 12.0 V (140k/10.0k). "
        "Change the top resistor to set 5-12 V. Sense resistor 12 mohm is about 6.7 A. "
        "Slope 220 pF is a starting value."
    )
    add_lm5176(sheet, "motor", "U7", "VM_MOTOR", "140k", "10.0k", "12mohm", "ACT_DISABLE")

    sheet.page = "pi-header"
    sheet.note("Pi header, ID EEPROM, and the GPS header left for later.")
    add_pi_header(sheet)
    add_eeprom(sheet)

    sheet.page = "servos"
    sheet.note(
        "Sixteen 3-pin servo headers: signal, servo 5 V, ground. "
        "Names are the firmware's default leg map. 220 ohm in series with each signal."
    )
    add_servos(sheet)

    sheet.page = "motors"
    sheet.note(
        "Four motor screw terminals, one per wheel. "
        "Each TB6612 channel is PWM, IN1, and IN2 from the motor PCA9685."
    )
    add_motors(sheet)

    sheet.page = "sensors"
    sheet.note("IMU, barometer, compass connector, and one 4-pin header per wheel.")
    add_sensors(sheet)

    sheet.page = "rp2040"
    sheet.note(
        "RP2040 encoder microcontroller. Crystal load caps are 15 pF and the USB "
        "series resistors are 27 ohm, from the RP2040 hardware design guide."
    )
    add_rp_power(sheet)
    add_rp2040(sheet)
    return sheet

# ================================================================================
def add_pi_buck(sheet):
    sheet.add("Regulator_Switching.kicad_sym", "TPS54560BDDA", "U9", "TPS54560B",
              {"1": "BOOT_PI", "2": "VBAT_LOAD", "3": "EN_PI", "4": "RT_PI",
               "5": "FB_PI", "6": "COMP_PI", "7": "GND", "8": "SW_PI", "9": "GND"})
    c(sheet, "C6", "100nF", "BOOT_PI", "SW_PI")
    r(sheet, "R7", "243k", "RT_PI", "GND")
    r(sheet, "R8", "105k", "+5V_PI", "FB_PI")
    r(sheet, "R9", "20.0k", "FB_PI", "GND")
    r(sheet, "R10", "10k", "COMP_PI", "COMP_PI_MID")
    c(sheet, "C7", "4.7nF", "COMP_PI_MID", "GND")
    sheet.add("Device.kicad_sym", "L", "L1", "10uH", {"1": "SW_PI", "2": "+5V_PI"})
    sheet.add("Device.kicad_sym", "D", "D2", "PDS760", {"1": "SW_PI", "2": "GND"})
    c(sheet, "C8", "10uF", "VBAT_LOAD", "GND")
    c(sheet, "C9", "22uF", "+5V_PI", "GND")
    # FAULT high turns the buck off. The chip's internal pull-up enables it otherwise.
    r(sheet, "R11", "10k", "FAULT", "EN_PI_BASE")
    sheet.add("Transistor_BJT.kicad_sym", "2N3904", "Q3", "2N3904",
              {"1": "GND", "2": "EN_PI_BASE", "3": "EN_PI"})

# ================================================================================
def add_ideal_or(sheet, ref_ic, ref_fet, ref_c, anode, cathode, vcap, gate):
    sheet.add("Power_Management.kicad_sym", "LM74700", ref_ic, "LM74700-Q1",
              {"1": vcap, "2": "GND", "3": anode, "4": cathode, "5": gate, "6": anode})
    c(sheet, ref_c, "100nF", vcap, anode)
    sheet.add("Transistor_FET.kicad_sym", "CSD18540Q5B", ref_fet, "CSD18540Q5B",
              {"1": anode, "2": anode, "3": anode, "4": gate, "5": cathode})

# ================================================================================
def add_lm5176(sheet, tag, ref, vout, r_top, r_bot, r_sense, disable):
    vin = "VBAT_LOAD"
    sw1 = "SW1_" + tag
    sw2 = "SW2_" + tag
    sense = "ISNS_" + tag
    en = "EN_" + tag
    sheet.add("Regulator_Switching.kicad_sym", "LM5176PWP", ref, "LM5176", {
        "1": en, "2": vin, "3": vin, "4": "MODE_" + tag, "5": "GND",
        "6": "RT_" + tag, "7": "SLOPE_" + tag, "8": "SS_" + tag,
        "9": "COMP_" + tag, "10": "GND", "11": "FB_" + tag, "12": vout,
        "13": "GND", "14": sense, "15": "GND", "16": sense,
        "17": None, "18": sw2, "19": "HDRV2_" + tag, "20": "BOOT2_" + tag,
        "21": "LDRV2_" + tag, "22": "GND", "23": "VCC_" + tag, "24": vout,
        "25": "LDRV1_" + tag, "26": "BOOT1_" + tag, "27": "HDRV1_" + tag,
        "28": sw1, "29": "GND",
    })
    c(sheet, "Cboot1_" + tag, "100nF", "BOOT1_" + tag, sw1)
    c(sheet, "Cboot2_" + tag, "100nF", "BOOT2_" + tag, sw2)
    c(sheet, "Cvcc_" + tag, "1uF", "VCC_" + tag, "GND")
    c(sheet, "Css_" + tag, "100nF", "SS_" + tag, "GND")
    c(sheet, "Cslope_" + tag, "220pF", "SLOPE_" + tag, "GND")
    r(sheet, "Rrt_" + tag, "27.0k", "RT_" + tag, "GND")
    r(sheet, "Rmode_" + tag, "200k", "MODE_" + tag, "GND")
    r(sheet, "Rcomp_" + tag, "10k", "COMP_" + tag, "COMPM_" + tag)
    c(sheet, "Ccomp_" + tag, "10nF", "COMPM_" + tag, "GND")
    r(sheet, "Rfb_t_" + tag, r_top, vout, "FB_" + tag)
    r(sheet, "Rfb_b_" + tag, r_bot, "FB_" + tag, "GND")
    r(sheet, "Ruv_t_" + tag, "100k", vin, en)
    r(sheet, "Ruv_b_" + tag, "14.0k", en, "GND")
    sheet.add("Device.kicad_sym", "D_Zener", "Dz_" + tag, "5.1V",
              {"1": en, "2": "GND"})
    r(sheet, "Rdis_" + tag, "10k", disable, "DISB_" + tag)
    sheet.add("Transistor_BJT.kicad_sym", "2N3904", "Qdis_" + tag, "2N3904",
              {"1": "GND", "2": "DISB_" + tag, "3": en})
    sheet.add("Device.kicad_sym", "L", "L_" + tag, "4.7uH", {"1": sw1, "2": sw2})
    sheet.add("Device.kicad_sym", "R", "Rs_" + tag, r_sense,
              {"1": sense, "2": "GND"}, "Resistor_SMD:R_1206_3216Metric")
    c(sheet, "Cin_" + tag, "22uF", vin, "GND")
    c(sheet, "Cout_" + tag, "22uF", vout, "GND")
    for half, drain, gate, source in (
        ("QH1", vin, "HDRV1_" + tag, sw1),
        ("QL1", sw1, "LDRV1_" + tag, sense),
        ("QH2", vout, "HDRV2_" + tag, sw2),
        ("QL2", sw2, "LDRV2_" + tag, sense),
    ):
        sheet.add("Transistor_FET.kicad_sym", "CSD18540Q5B", half + "_" + tag, "CSD18540Q5B",
                  {"1": source, "2": source, "3": source, "4": gate, "5": drain})

# ================================================================================
def add_rp_power(sheet):
    add_ideal_or(sheet, "U10", "Q4", "C22", "VBUS", "RP_5V", "VCAP_USB", "GATE_USB")
    add_ideal_or(sheet, "U11", "Q5", "C23", "+5V_PI", "RP_5V", "VCAP_RP", "GATE_RP")
    sheet.add("Regulator_Linear.kicad_sym", "AMS1117CD-3.3", "U12", "AMS1117-3.3",
              {"1": "GND", "2": "+3V3_RP", "3": "RP_5V"})
    c(sheet, "C24", "10uF", "RP_5V", "GND")
    c(sheet, "C25", "10uF", "+3V3_RP", "GND")

# ================================================================================
def add_pi_header(sheet):
    nets = {str(pin): None for pin in range(1, 41)}
    for pin in (6, 9, 14, 20, 25, 30, 34, 39):
        nets[str(pin)] = "GND"
    nets["2"] = "+5V_HDR"
    nets["4"] = "+5V_HDR"
    nets["3"] = "I2C_SDA_PI"
    nets["5"] = "I2C_SCL_PI"
    nets["8"] = "UART_TX"
    nets["10"] = "UART_RX"
    nets["11"] = "ARM"
    nets["12"] = "PPS"
    nets["13"] = "ALERT_N"
    nets["15"] = "IMU_INT"
    nets["16"] = "ENC_INT"
    nets["27"] = "ID_SD"
    nets["28"] = "ID_SC"
    sheet.add("Connector_Generic.kicad_sym", "Conn_02x20_Odd_Even", "J2", "PI_GPIO",
              nets, "Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical")
    r(sheet, "R12", "47", "I2C_SDA_PI", "I2C1_SDA")
    r(sheet, "R13", "47", "I2C_SCL_PI", "I2C1_SCL")
    r(sheet, "R14", "100k", "ARM", "GND")
    sheet.add("Connector_Generic.kicad_sym", "Conn_01x04", "J3", "GPS_LATER",
              {"1": "UART_TX", "2": "UART_RX", "3": "PPS", "4": "GND"})

# ================================================================================
def add_eeprom(sheet):
    sheet.add("Memory_EEPROM.kicad_sym", "24LC32", "U13", "24LC32A",
              {"1": "GND", "2": "GND", "3": "GND", "4": "GND",
               "5": "ID_SD", "6": "ID_SC", "7": "WP", "8": "+3V3"})
    r(sheet, "R15", "3.9k", "ID_SD", "+3V3")
    r(sheet, "R16", "3.9k", "ID_SC", "+3V3")
    r(sheet, "R17", "1k", "WP", "+3V3")
    c(sheet, "C26", "100nF", "+3V3", "GND")

# ================================================================================
def add_pca(sheet, ref, a5, leds, oe):
    nets = {
        "1": "GND", "2": "GND", "3": "GND", "4": "GND", "5": "GND",
        "14": "GND", "23": oe, "24": a5, "25": "GND",
        "26": "I2C1_SCL", "27": "I2C1_SDA", "28": "+3V3",
    }
    # LED0 is pin 6. Pins 6..13 are LED0..LED7. Pins 15..22 are LED8..LED15.
    order = ["6", "7", "8", "9", "10", "11", "12", "13",
             "15", "16", "17", "18", "19", "20", "21", "22"]
    for index, pin in enumerate(order):
        nets[pin] = leds[index]
    sheet.add("Driver_LED.kicad_sym", "PCA9685PW", ref, "PCA9685PW", nets)
    c(sheet, "Cpca_" + ref, "100nF", "+3V3", "GND")

# ================================================================================
def add_servos(sheet):
    r(sheet, "R18", "10k", "SERVO_OE", "+3V3")
    leds = []
    for index in range(16):
        chip = "LED" + str(index)
        sig = "SERVO_SIG_" + str(index)
        leds.append(chip)
        r(sheet, "Rs" + str(index), "220", chip, sig)
        sheet.add("Connector_Generic.kicad_sym", "Conn_01x03", "JS" + str(index),
                  SERVO_LABELS[index],
                  {"1": sig, "2": "+5V_SERVO", "3": "GND"},
                  "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical")
    add_pca(sheet, "U14", "GND", leds, "SERVO_OE")

# ================================================================================
def add_motors(sheet):
    # Rover firmware channels: FL 2/3/4, FR 5/6/7, RR 8/9/10, RL 11/12/13.
    names = [None] * 16
    wiring = {
        2: "FL_PWM", 3: "FL_IN2", 4: "FL_IN1",
        5: "FR_PWM", 6: "FR_IN2", 7: "FR_IN1",
        8: "RR_PWM", 9: "RR_IN2", 10: "RR_IN1",
        11: "RL_PWM", 12: "RL_IN2", 13: "RL_IN1",
    }
    for index in range(16):
        names[index] = wiring.get(index)
    add_pca(sheet, "U15", "+3V3", names, "MOTOR_OE")
    r(sheet, "R27", "10k", "MOTOR_OE", "+3V3")
    r(sheet, "R19", "100k", "MOTOR_STBY", "GND")
    for ref, tag_a, tag_b in (("U16", "FL", "FR"), ("U17", "RR", "RL")):
        sheet.add("Driver_Motor.kicad_sym", "TB6612FNG", ref, "TB6612FNG", {
            "1": tag_a + "_P", "2": tag_a + "_P", "3": "GND", "4": "GND",
            "5": tag_a + "_N", "6": tag_a + "_N", "7": tag_b + "_N", "8": tag_b + "_N",
            "9": "GND", "10": "GND", "11": tag_b + "_P", "12": tag_b + "_P",
            "13": "VM_MOTOR", "14": "VM_MOTOR", "15": tag_b + "_PWM",
            "16": tag_b + "_IN2", "17": tag_b + "_IN1", "18": "GND",
            "19": "MOTOR_STBY", "20": "+3V3", "21": tag_a + "_IN1",
            "22": tag_a + "_IN2", "23": tag_a + "_PWM", "24": "VM_MOTOR",
        })
        c(sheet, "Cvm_" + ref, "100uF", "VM_MOTOR", "GND")
    for tag in ("FL", "FR", "RR", "RL"):
        sheet.add("Connector.kicad_sym", "Screw_Terminal_01x02", "JM_" + tag, tag + " screw",
                  {"1": tag + "_P", "2": tag + "_N"},
                  "TerminalBlock:TerminalBlock_bornier-2_P5.08mm")

# ================================================================================
def add_sensors(sheet):
    sheet.add("Sensor_Motion.kicad_sym", "MPU-6050", "U18", "MPU-6050", {
        "1": "GND", "2": None, "3": None, "4": None, "5": None,
        "6": None, "7": None, "8": "+3V3", "9": "GND", "10": "MPU_REG",
        "11": "GND", "12": "IMU_INT", "13": "+3V3", "14": None, "15": None,
        "16": None, "17": None, "18": "GND", "19": None, "20": "MPU_CP",
        "21": None, "22": None, "23": "I2C1_SCL", "24": "I2C1_SDA",
    })
    c(sheet, "C27", "100nF", "MPU_REG", "GND")
    c(sheet, "C28", "10nF", "MPU_CP", "GND")
    c(sheet, "C29", "100nF", "+3V3", "GND")
    sheet.add("Sensor_Pressure.kicad_sym", "BMP280", "U19", "BMP280",
              {"1": "GND", "2": "+3V3", "3": "I2C1_SDA", "4": "I2C1_SCL",
               "5": "GND", "6": "+3V3", "7": "GND", "8": "+3V3"})
    c(sheet, "C30", "100nF", "+3V3", "GND")
    sheet.add("Connector_Generic.kicad_sym", "Conn_01x04", "J4", "COMPASS",
              {"1": "+3V3", "2": "GND", "3": "I2C1_SDA", "4": "I2C1_SCL"})
    # 1.65 V threshold. Both channels inverted by the Schmitt; quadrature direction is unchanged.
    r(sheet, "R20", "10k", "+3V3", "CMP_REF")
    r(sheet, "R21", "10k", "CMP_REF", "GND")
    channels = ["FL_A", "FL_B", "FR_A", "FR_B", "RR_A", "RR_B", "RL_A", "RL_B"]
    for index in range(4):
        sheet.add("Comparator.kicad_sym", "LM393", "Ucmp" + str(index), "LM393", {
            (1, "1"): "CMP_" + channels[index * 2],
            (1, "2"): "CMP_REF",
            (1, "3"): "SNS_" + channels[index * 2],
            (2, "5"): "SNS_" + channels[index * 2 + 1],
            (2, "6"): "CMP_REF",
            (2, "7"): "CMP_" + channels[index * 2 + 1],
            (3, "4"): "GND",
            (3, "8"): "+3V3",
        })
    for name in channels:
        r(sheet, "Rpu_" + name, "10k", "CMP_" + name, "+3V3")
        r(sheet, "Rpd_" + name, "100k", "SNS_" + name, "GND")
    gates = [(1, "1", "2"), (2, "3", "4"), (3, "5", "6"),
             (4, "9", "8"), (5, "11", "10"), (6, "13", "12")]
    for chip in range(2):
        nets = {(7, "14"): "+3V3", (7, "7"): "GND"}
        for gate_index, (unit, pin_in, pin_out) in enumerate(gates):
            channel = chip * 6 + gate_index
            if channel < len(channels):
                nets[(unit, pin_in)] = "CMP_" + channels[channel]
                nets[(unit, pin_out)] = "ENC_" + channels[channel]
            else:
                nets[(unit, pin_in)] = "GND"
                nets[(unit, pin_out)] = None
        sheet.add("74xx.kicad_sym", "74HC14", "Usch" + str(chip), "74HC14", nets)
    for index, wheel in enumerate(("FL", "FR", "RR", "RL")):
        sheet.add("Connector_Generic.kicad_sym", "Conn_01x04", "JW" + str(index), wheel,
                  {"1": "SNS_" + wheel + "_A", "2": "SNS_" + wheel + "_B",
                   "3": "+3V3", "4": "GND"})

# ================================================================================
def add_rp2040(sheet):
    nets = {str(pin): None for pin in range(1, 58)}
    for pin in (1, 10, 22, 33, 42, 49, 23, 50, 43, 44, 48):
        nets[str(pin)] = "+3V3_RP"
        c(sheet, "Crp" + str(pin), "100nF", "+3V3_RP", "GND")
    nets["45"] = "VREG_1V1"
    c(sheet, "C31", "1uF", "VREG_1V1", "GND")
    nets["57"] = "GND"
    nets["19"] = "GND"
    nets["26"] = "RUN"
    r(sheet, "R22", "10k", "RUN", "+3V3_RP")
    nets["20"] = "XIN"
    nets["21"] = "XOUT"
    sheet.add("Device.kicad_sym", "Crystal", "Y1", "ABM8-272-T3",
              {"1": "XIN", "2": "XOUT"})
    c(sheet, "C32", "15pF", "XIN", "GND")
    c(sheet, "C33", "15pF", "XOUT", "GND")
    nets["46"] = "USB_DM_C"
    nets["47"] = "USB_DP_C"
    r(sheet, "R23", "27", "USB_DM_C", "USB_DM")
    r(sheet, "R24", "27", "USB_DP_C", "USB_DP")
    nets["2"] = "SERVO_OE"
    nets["3"] = "MOTOR_STBY"
    nets["16"] = "MOTOR_OE"
    gpio_enc = ["4", "5", "6", "7", "8", "9", "11", "12"]
    enc = ["FL_A", "FL_B", "FR_A", "FR_B", "RR_A", "RR_B", "RL_A", "RL_B"]
    for pin, name in zip(gpio_enc, enc):
        nets[pin] = "ENC_" + name
    nets["13"] = "I2C1_SDA"
    nets["14"] = "I2C1_SCL"
    nets["15"] = "ENC_INT"
    # QSPI. BOOTSEL button grounds QSPI_SS, as in the RP2040 hardware design guide.
    nets["56"] = "QSPI_SS"
    nets["52"] = "QSPI_SCK"
    nets["53"] = "QSPI_SD0"
    nets["55"] = "QSPI_SD1"
    nets["54"] = "QSPI_SD2"
    nets["51"] = "QSPI_SD3"
    sheet.add("Switch.kicad_sym", "SW_Push", "SW3", "BOOTSEL",
              {"1": "QSPI_SS", "2": "GND"})
    sheet.add("Memory_Flash.kicad_sym", "W25Q32JVSS", "U20", "W25Q32JVSS",
              {"1": "QSPI_SS", "2": "QSPI_SD1", "3": "QSPI_SD2", "4": "GND",
               "5": "QSPI_SD0", "6": "QSPI_SCK", "7": "QSPI_SD3", "8": "+3V3_RP"})
    c(sheet, "C34", "100nF", "+3V3_RP", "GND")
    sheet.add("Connector.kicad_sym", "USB_C_Receptacle_USB2.0_16P", "J5", "RP2040_USB",
              {"A1": "GND", "A12": "GND", "A4": "VBUS", "A5": "CC1",
               "A6": "USB_DP", "A7": "USB_DM", "A8": None, "A9": "VBUS",
               "B1": "GND", "B12": "GND", "B4": "VBUS", "B5": "CC2",
               "B6": "USB_DP", "B7": "USB_DM", "B8": None, "B9": "VBUS",
               "S1": "GND"})
    r(sheet, "R25", "5.1k", "CC1", "GND")
    r(sheet, "R26", "5.1k", "CC2", "GND")
    sheet.add("MCU_RaspberryPi.kicad_sym", "RP2040", "U21", "RP2040", nets)

# ================================================================================
SERVO_LABELS = {
    0: "spare 0",
    1: "spare 1",
    2: "FL knee",
    3: "FL hip",
    4: "FL waist",
    5: "RL knee",
    6: "RL hip",
    7: "RL waist",
    8: "RR waist",
    9: "RR hip",
    10: "RR knee",
    11: "FR waist",
    12: "FR hip",
    13: "FR knee",
    14: "spare 14",
    15: "head neck",
}

# ================================================================================
def main():
    sheet = build()
    from schematic_draw import render_project
    render_project(sheet)

# ================================================================================
if __name__ == "__main__":
    main()
