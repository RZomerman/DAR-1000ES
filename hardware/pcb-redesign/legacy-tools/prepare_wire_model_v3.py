import shutil

from pcb_v3_tools import *


shutil.copyfile(TARGET / (BASE + ".kicad_pro"), FILES / "pcb-v3-placement-base.kicad_pro")
board = pcbnew.LoadBoard(str(FILES / "pcb-v3-placement-base.kicad_pcb"))
count = 0
for f in board.GetFootprints():
    rectangles = []
    for g in f.GraphicalItems():
        if g.GetLayer() == pcbnew.F_CrtYd:
            box = g.GetBoundingBox()
            rectangles.append((box.GetX(), box.GetY(), box.GetRight(), box.GetBottom()))
    for text in (f.Reference(), f.Value()):
        if text.GetLayer() == pcbnew.F_SilkS and text.IsVisible():
            box = text.GetBoundingBox()
            rectangles.append((box.GetX(), box.GetY(), box.GetRight(), box.GetBottom()))
    if f.GetReference().startswith("H"):
        x, y = f.GetPosition().x, f.GetPosition().y
        r = pcbnew.FromMM(3.75)
        rectangles.append((x - r, y - r, x + r, y + r))
    if not rectangles:
        continue
    margin = pcbnew.FromMM(0.15)
    x1, y1 = min(r[0] for r in rectangles) - margin, min(r[1] for r in rectangles) - margin
    x2, y2 = max(r[2] for r in rectangles) + margin, max(r[3] for r in rectangles) + margin
    zone = pcbnew.ZONE(board)
    zone.SetLayer(pcbnew.F_Cu)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowTracks(False)
    zone.SetDoNotAllowPads(False)
    zone.SetDoNotAllowZoneFills(False)
    zone.SetDoNotAllowFootprints(False)
    poly = zone.Outline()
    poly.NewOutline()
    for x, y in ((x1, y1), (x2, y1), (x2, y2), (x1, y2)):
        poly.Append(x, y)
    board.Add(zone)
    count += 1
original = FILES / "pcb-v3-mechanical-original.dsn"
assert pcbnew.ExportSpecctraDSN(board, str(original))
text = original.read_text()


def end_scope(start):
    depth, quoted, escaped, end = 0, False, False, start
    while True:
        c = text[end]
        if escaped:
            escaped = False
        elif c == "\\" and quoted:
            escaped = True
        elif c == '"':
            quoted = not quoted
        elif not quoted:
            depth += (c == "(") - (c == ")")
        end += 1
        if depth == 0:
            return end


replacements = []
for match in re.finditer(r"(?m)^    \(padstack ", text):
    start = text.index("(", match.start())
    end = end_scope(start)
    block = text[start:end]
    if "Via[" not in block.splitlines()[0]:
        replacements.append((start, end, re.sub(r'(\((?:circle|rect|polygon|path)\s+)(?:"?signal"?|F\.Cu)', r"\1B.Cu", block)))
for start, end, block in reversed(replacements):
    text = text[:start] + block + text[end:]
spans = []
for match in re.finditer(r'(?m)^    \(net "unconnected-', text):
    start = text.index("(", match.start())
    spans.append((start, end_scope(start)))
for start, end in reversed(spans):
    text = text[:start] + text[end:]
text = re.sub(r'"unconnected-[^"]*"', "", text)
settings = ["autoroute_settings", ["fanout", "on"], ["app.freerouting.autoroute", "on"],
            ["postroute", "on"], ["vias", "on"], ["via_costs", "50"], ["plane_via_costs", "50"],
            ["start_ripup_costs", "100"], ["start_pass_no", "1"],
            ["layer_rule", "F.Cu", ["active", "on"], ["preferred_direction", "horizontal"],
             ["preferred_direction_trace_costs", "2"], ["against_preferred_direction_trace_costs", "2"]],
            ["layer_rule", "B.Cu", ["active", "on"], ["preferred_direction", "horizontal"],
             ["preferred_direction_trace_costs", "1"], ["against_preferred_direction_trace_costs", "1"]]]
text = text.replace("    (boundary", "    " + emit(settings) + "\n    (boundary", 1)
(FILES / "pcb-v3-mechanical.dsn").write_text(text)
print("Protected", count, "component/screwhead regions and their printed labels from new wire-link holes.")
