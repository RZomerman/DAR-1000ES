import math
import uuid

from pcb_v2_tools import *

SOURCE = Path(r"C:\Dev\DAR-1000ES-Production-v2")
TARGET = Path(r"C:\Dev\DAR-1000ES-Production-v3")
BASE = "DAR-1000ES-v3"


def uid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "DAR-1000ES-v3/" + name))


def prop(name, text, x=0, y=0, hidden=False):
    return ["property", q(name), q(text), ["at", str(x), str(y), "0"],
            *([["hide", "yes"]] if hidden else []), ["effects", ["font", ["size", "1", "1"]]]]


def global_label(name, x, y):
    return ["global_label", q(name), ["shape", "bidirectional"], ["at", str(round(x, 6)), str(round(y, 6)), "0"],
            ["effects", ["font", ["size", "0.8", "0.8"]], ["justify", "left"]], ["uuid", q(uid(f"{name}:{x}:{y}"))]]


def pins(definition, unit=1):
    return {json.loads(child(p, "number")[1]): p for sub in children(definition, "symbol")
            if json.loads(sub[1]).rsplit("_", 2)[1] in ("0", str(unit)) for p in children(sub, "pin")}


def pin_position(instance, definition, number):
    x, y, angle = map(float, child(instance, "at")[1:4])
    px, py = map(float, child(pins(definition, int(child(instance, "unit")[1]))[number], "at")[1:3])
    a = math.radians(angle)
    return round(x + math.cos(a) * px - math.sin(a) * py, 6), round(y - math.sin(a) * px - math.cos(a) * py, 6)
