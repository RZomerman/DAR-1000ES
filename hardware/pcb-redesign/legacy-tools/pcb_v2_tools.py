import copy
import hashlib
import json
import re
import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).parent
FILES = ROOT / "files"
SOURCE = Path(r"C:\Dev\DAR-1000ES-Production")
TARGET = Path(r"C:\Dev\DAR-1000ES-Production-v2")
BASE = "DAR-1000ES-v2"
KICAD = Path(r"C:\Program Files\KiCad\10.0\share\kicad")


def q(value):
    return json.dumps(str(value), ensure_ascii=True)


def parse(text):
    tokens = iter(re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text))

    def read(token):
        if token != "(":
            return token
        out = []
        for token in tokens:
            if token == ")":
                return out
            out.append(read(token))
        raise ValueError("Unclosed S-expression")

    return read(next(tokens))


def emit(node, depth=0):
    if not isinstance(node, list):
        return node
    parts = [emit(child, depth + 1) for child in node]
    flat = "(" + " ".join(parts) + ")"
    if "\n" not in flat and len(flat) < 110:
        return flat
    return "(" + parts[0] + "\n" + "\n".join("\t" * (depth + 1) + p for p in parts[1:]) + "\n" + "\t" * depth + ")"


def save(path, node):
    path.write_text(emit(node) + "\n", encoding="utf-8")


def children(node, key):
    return [n for n in node if isinstance(n, list) and n[0] == key]


def child(node, key):
    return next(n for n in node if isinstance(n, list) and n[0] == key)


def value(node, name):
    return json.loads(next(n[2] for n in children(node, "property") if n[1] == q(name)))


def set_value(node, name, text):
    next(n for n in children(node, "property") if n[1] == q(name))[2] = q(text)


def vec(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))


def mm(pos):
    return (pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y))


def prepare():
    assert not list(TARGET.glob("*.lck"))
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCE.iterdir() if p.is_file()}
    (FILES / "pcb-v2-original-hashes.json").write_text(json.dumps(hashes, indent=2))
    sch_path = TARGET / (BASE + ".kicad_sch")
    sch = parse(sch_path.read_text(encoding="utf-8"))
    for symbol in children(sch, "symbol"):
        for instances in children(symbol, "instances"):
            for project in children(instances, "project"):
                if project[1] == q("DAR-1000ES-blank"):
                    project[1] = q(BASE)
    board = pcbnew.LoadBoard(str(TARGET / (BASE + ".kicad_pcb")))
    connector_changes = []
    added_footprints = []
    for old in list(board.GetFootprints()):
        ref = old.GetReference()
        if not re.fullmatch(r"J\d+", ref):
            continue
        count = len(list(old.Pads()))
        fp_name = f"JST_XH_S{count}B-XH-A_1x{count:02d}_P2.50mm_Horizontal"
        new = pcbnew.FootprintLoad(str(KICAD / "footprints" / "Connector_JST.pretty"), fp_name)
        assert new
        new.SetReference(ref)
        new.SetValue(old.GetValue())
        new.SetFPID(pcbnew.LIB_ID("Connector_JST", fp_name))
        new.SetPath(old.GetPath())
        old_x, old_y = mm(old.GetPosition())
        if old_y < old_x:
            position, angle, edge = (old_x, 9.7), 180, "top"
        else:
            position, angle, edge = (9.7, old_y), -90, "left"
        new.SetOrientationDegrees(angle)
        new.SetPosition(vec(*position))
        pads = {p.GetNumber(): p for p in new.Pads()}
        for pad in old.Pads():
            pads[pad.GetNumber()].SetNet(pad.GetNet())
        midpoint = 1.25 * (count - 1)
        if edge == "top":
            new.Reference().SetPosition(vec(position[0] - midpoint, 13.5))
            new.Reference().SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
            new.Value().SetPosition(vec(position[0] - midpoint, 15))
            new.Value().SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
        else:
            new.Reference().SetPosition(vec(13.5, position[1] + midpoint))
            new.Reference().SetTextAngle(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T))
            new.Value().SetPosition(vec(15, position[1] + midpoint))
            new.Value().SetTextAngle(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T))
        new.Reference().SetVisible(old.Reference().IsVisible())
        new.Value().SetVisible(old.Value().IsVisible())
        for symbol in children(sch, "symbol"):
            if value(symbol, "Reference") == ref:
                set_value(symbol, "Footprint", "Connector_JST:" + fp_name)
        connector_changes.append({"ref": ref, "old_position": [old_x, old_y], "new_position": position, "edge": edge, "footprint": fp_name, "label": old.GetValue()})
        board.Remove(old)
        board.Add(new)
        added_footprints.append(new)
    assert len(connector_changes) == 10
    intermediate = FILES / "pcb-v2-edge-connectors.kicad_pcb"
    pcbnew.SaveBoard(str(intermediate), board)
    save(FILES / "pcb-v2-edge-connectors.kicad_sch", sch)
    (FILES / "pcb-v2-connector-changes.json").write_text(json.dumps(connector_changes, indent=2))
    print("Prepared edge-connector replacements.")


def finish_prepare():
    board = parse((FILES / "pcb-v2-edge-connectors.kicad_pcb").read_text(encoding="utf-8"))
    sch_path = TARGET / (BASE + ".kicad_sch")
    sch = parse((FILES / "pcb-v2-edge-connectors.kicad_sch").read_text(encoding="utf-8"))
    seen = set()
    holes = []
    for f in children(board, "footprint"):
        if not value(f, "Reference").startswith("H"):
            continue
        key = tuple(child(f, "at")[1:3])
        if key in seen:
            board.remove(f)
        else:
            seen.add(key)
            holes.append(f)
    assert len(holes) == 6
    for n, f in enumerate(sorted(holes, key=lambda f: (float(child(f, "at")[2]), float(child(f, "at")[1]))), 1):
        set_value(f, "Reference", "H" + str(n))
    board = [n for n in board if not (isinstance(n, list) and n[0] in ("segment", "via", "arc"))]
    edges = [n for n in children(board, "gr_rect") if child(n, "layer")[1] == q("Edge.Cuts")]
    assert len(edges) == 1
    child(edges[0], "end")[1:] = ["100", "210"]
    child(child(board, "title_block"), "title")[1] = q("DAR-1000ES v2 - optimized connections")
    project_path = TARGET / (BASE + ".kicad_pro")
    project = json.loads(project_path.read_text(encoding="utf-8"))
    project.setdefault("meta", {})["filename"] = project_path.name
    default = project["net_settings"]["classes"][0]
    default["track_width"] = 0.25
    power = copy.deepcopy(default)
    power.update(name="Power", priority=0, track_width=0.8, via_diameter=0.8, via_drill=0.4)
    project["net_settings"]["classes"] = [default, power]
    project["net_settings"]["netclass_patterns"] = [
        {"netclass": "Power", "pattern": name} for name in ("GND", "/+5V", "Net-(JP1-B)", "Net-(JP2-B)")
    ]
    project_path.write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")
    save(sch_path, sch)
    save(TARGET / (BASE + ".kicad_pcb"), board)
    print("Prepared v2: 100 x 210 mm; 10 outward-facing edge connectors; non-connector placements preserved; six distinct mounting holes; removed old tracks for rerouting.")


if __name__ == "__main__":
    finish_prepare() if "--finish" in sys.argv else prepare()
