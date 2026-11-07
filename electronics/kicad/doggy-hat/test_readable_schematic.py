"""The readable schematic has wires, servo headers, and motor screws.

The old one-page sheet had no wires. These checks fail on that file and pass
after generate_schematic.py draws the sheets. Pin nets are compared with the
signature saved from the last electrically checked one-page netlist.
"""

import json
import os
import subprocess
import sys

import generate_schematic as gen

# ================================================================================
DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(DIR, "doggy-hat.kicad_sch")
SIGNATURE = os.path.join(DIR, "expected_pin_nets.json")

# ================================================================================
SERVO_NAMES = (
    "spare 0", "spare 1", "FL knee", "FL hip", "FL waist",
    "RL knee", "RL hip", "RL waist", "RR waist", "RR hip", "RR knee",
    "FR waist", "FR hip", "FR knee", "spare 14", "head neck",
)

# ================================================================================
MOTOR_NAMES = ("FL screw", "FR screw", "RR screw", "RL screw")

# ================================================================================
def read(name):
    path = os.path.join(DIR, name)
    with open(path, encoding="utf-8") as handle:
        return handle.read()

# ================================================================================
def prop(node, tag):
    for item in node[1:]:
        if isinstance(item, list) and item and item[0] == tag:
            return item[1]
    return None

# ================================================================================
def pin_signature(text):
    tree = gen.parse(text)
    components = None
    nets = None
    for item in tree[1:]:
        if isinstance(item, list) and item and item[0] == "components":
            components = item
        if isinstance(item, list) and item and item[0] == "nets":
            nets = item
    info = {}
    for comp in components[1:]:
        if isinstance(comp, list) == False or comp[0] != "comp":
            continue
        ref = prop(comp, "ref")
        libsource = None
        for item in comp[1:]:
            if isinstance(item, list) and item and item[0] == "libsource":
                libsource = item
        info[ref] = (
            prop(libsource, "lib"),
            prop(libsource, "part"),
        )
    pins = {}
    for net in nets[1:]:
        if isinstance(net, list) == False or net[0] != "net":
            continue
        name = prop(net, "name")
        if name.startswith("unconnected-"):
            name = "NC"
        for node in net[1:]:
            if isinstance(node, list) == False or node[0] != "node":
                continue
            ref = prop(node, "ref")
            if ref.startswith("#"):
                continue
            pins.setdefault(ref, []).append((prop(node, "pin"), name))
    buckets = {}
    for ref, pairs in pins.items():
        key = info[ref]
        buckets.setdefault(key, []).append(tuple(sorted(pairs)))
    signature = {}
    for key, maps in buckets.items():
        signature["|".join(key)] = sorted(maps)
    return signature

# ================================================================================
def check_readable():
    upper = os.path.join(os.path.dirname(DIR), "doggy-upper", "doggy-upper.kicad_sch")
    with open(upper, encoding="utf-8") as handle:
        root = handle.read()
    if "servos.kicad_sch" not in root or "motors.kicad_sch" not in root:
        raise AssertionError("upper root does not list the servo and motor sheets")
    servos = read("servos.kicad_sch")
    motors = read("motors.kicad_sch")
    pack = read("pack.kicad_sch")
    if servos.count("(wire ") < 16:
        raise AssertionError("servo sheet has no wires")
    if motors.count("(wire ") < 4:
        raise AssertionError("motor sheet has no wires")
    if pack.count("(wire ") < 1:
        raise AssertionError("pack sheet has no wires")
    for name in SERVO_NAMES:
        if gen.dumps(name) not in servos and '"' + name + '"' not in servos:
            raise AssertionError("servo header missing: " + name)
    for name in MOTOR_NAMES:
        if '"' + name + '"' not in motors:
            raise AssertionError("motor screw missing: " + name)
    if servos.count("Conn_01x03") < 16:
        raise AssertionError("expected 16 servo headers")
    if motors.count("Screw_Terminal_01x02") < 4:
        raise AssertionError("expected 4 motor screw terminals")

# ================================================================================
def export_netlist():
    dest = os.path.join(DIR, "doggy-hat.net")
    subprocess.check_call(
        [
            "kicad-cli", "sch", "export", "netlist",
            "--format", "kicadsexpr", "-o", dest, ROOT,
        ]
    )
    with open(dest, encoding="utf-8") as handle:
        return handle.read()

# ================================================================================
def freeze(maps):
    frozen = []
    for item in maps:
        frozen.append([list(pair) for pair in item])
    return frozen

# ================================================================================
def check_nets(text):
    with open(SIGNATURE, encoding="utf-8") as handle:
        expected = json.load(handle)
    actual = pin_signature(text)
    if set(expected) != set(actual):
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        raise AssertionError("part keys changed missing " + str(missing) + " extra " + str(extra))
    for key, maps in expected.items():
        if freeze(actual[key]) != maps:
            raise AssertionError("pin nets changed for " + key)

# ================================================================================
def main():
    check_readable()
    if len(sys.argv) > 1 and sys.argv[1] == "--nets":
        check_nets(export_netlist())
    print("readable schematic ok")

# ================================================================================
if __name__ == "__main__":
    main()
