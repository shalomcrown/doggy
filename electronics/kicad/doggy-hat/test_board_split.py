"""The hat is two boards. Pack current crosses J30, not the GPIO header."""

import os
import re
import subprocess

# ================================================================================
DIR = os.path.dirname(os.path.abspath(__file__))
KICAD = os.path.dirname(DIR)

# ================================================================================
def export(name):
    root = os.path.join(KICAD, name, name + ".kicad_sch")
    dest = os.path.join("/tmp", name + "-split.net")
    subprocess.check_call([
        "kicad-cli", "sch", "export", "netlist",
        "--format", "kicadsexpr",
        "-o", dest, root,
    ])
    with open(dest, encoding="utf-8") as handle:
        return handle.read()

# ================================================================================
def components(text):
    return set(re.findall(r'\(comp \(ref "([^"]+)"\)', text))

# ================================================================================
def nets(text):
    found = {}
    for chunk in text.split("(net (code ")[1:]:
        name = re.search(r'\(name "([^"]*)"', chunk).group(1)
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', chunk)
        found[name] = nodes
    return found

# ================================================================================
def pins_of(netmap, ref):
    found = {}
    for name, nodes in netmap.items():
        for node_ref, pin in nodes:
            if node_ref == ref:
                found[int(pin)] = name
    return found

# ================================================================================
def require(condition, message):
    if condition == False:
        raise AssertionError(message)

# ================================================================================
def check_j30(netmap, pin19):
    pins = pins_of(netmap, "J30")
    for pin in range(1, 9):
        require(pins.get(pin) == "VBAT_LOAD", "J30 pin " + str(pin))
    for pin in (9, 10, 11, 12, 13, 14, 15, 16, 20):
        require(pins.get(pin) == "GND", "J30 ground " + str(pin))
    require(pins.get(17) == "+3V3" and pins.get(18) == "+3V3", "J30 3V3")
    require(pins.get(19) == pin19, "J30 pin 19")
    require(pins.get(21) == "I2C1_SDA" and pins.get(22) == "I2C1_SCL", "J30 I2C")
    require(pins.get(23) == "+5V_PI" and pins.get(24) == "+5V_PI", "J30 5V")

# ================================================================================
def check_lower(text):
    refs = components(text)
    for ref in ("J1", "J2", "J30", "U2", "U10", "U15", "U16"):
        require(ref in refs, "lower missing " + ref)
    for ref in ("U11", "U27", "J6", "SW2"):
        require(ref not in refs, "lower should not have " + ref)
    netmap = nets(text)
    check_j30(netmap, "ACT_DISABLE")
    arm = set(ref for ref, _pin in netmap["ARM"])
    require("U4" in arm and "J2" in arm and "SW2" not in arm, "ARM")
    require("U10" in set(ref for ref, _pin in netmap["ID_SD"]), "ID_SD")
    header = pins_of(netmap, "J2")
    require(header[3] == "I2C_SDA_PI" and header[16] == "ENC_INT", "lower header")
    require("U15" in set(ref for ref, _pin in netmap["I2C1_SDA"]), "IMU on device I2C")

# ================================================================================
def check_upper(text):
    refs = components(text)
    for ref in ("J2", "J6", "J20", "J30", "SW2", "U11", "U27", "R78"):
        require(ref in refs, "upper missing " + ref)
    for ref in ("J1", "U2", "U10", "U15"):
        require(ref not in refs, "upper should not have " + ref)
    netmap = nets(text)
    check_j30(netmap, "ACT_LATCH")
    disable = set(ref for ref, _pin in netmap["ACT_DISABLE"])
    require("SW2" in disable and "R78" in disable and "R19" in disable, "e-stop")
    require("J24" not in disable, "e-stop shorted to 3V3")
    require("ID_SD" not in netmap, "ID bus on the upper board")
    header = pins_of(netmap, "J2")
    require(header[2] == "+5V_HDR" and header[4] == "+5V_HDR", "header 5V")
    require(header[16] == "ENC_INT", "ENC_INT")
    for pin in (3, 5, 27, 28):
        require(header[pin].startswith("unconnected-"), "upper header pin " + str(pin))
    pi = set(ref for ref, _pin in netmap["+5V_PI"])
    hdr = set(ref for ref, _pin in netmap["+5V_HDR"])
    require(len(pi.intersection(hdr)) == 0, "+5V_PI joined to +5V_HDR")

# ================================================================================
def check_pack_estop():
    path = os.path.join(DIR, "pack.kicad_sch")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    require("ARM_SW" not in text, "ARM_SW still on the pack sheet")
    require('(property "Reference" "SW2"' not in text, "SW2 still on the pack sheet")
    require("U4" in text and "J1" in text, "pack sheet lost its parts")

# ================================================================================
def main():
    check_pack_estop()
    check_lower(export("doggy-lower"))
    check_upper(export("doggy-upper"))
    print("board split ok")

# ================================================================================
if __name__ == "__main__":
    main()
