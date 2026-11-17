import collections

from pcb_v3_tools import *


path = FILES / "pcb-v3-planned.kicad_pcb"
board = parse(path.read_text())
segments = [s for s in children(board, "segment") if child(s, "layer")[1] == q("F.Cu")]
vias = children(board, "via")


def xy(node, tag):
    return tuple(round(float(c), 6) for c in child(node, tag)[1:3])


by_net = collections.defaultdict(list)
for segment in segments:
    by_net[json.loads(child(segment, "net")[1])].append((xy(segment, "start"), xy(segment, "end")))
links = []
for net, edges in sorted(by_net.items()):
    terminals = {xy(v, "at") for v in vias if json.loads(child(v, "net")[1]) == net}
    points = terminals | {p for e in edges for p in e}
    graph = collections.defaultdict(set)
    split_edges = set()
    for a, b in edges:
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length < 1e-8:
            continue
        on_line = [p for p in points
                   if abs(dx * (p[1] - a[1]) - dy * (p[0] - a[0])) < 0.00002 * length
                   and min(a[0], b[0]) - 1e-5 <= p[0] <= max(a[0], b[0]) + 1e-5
                   and min(a[1], b[1]) - 1e-5 <= p[1] <= max(a[1], b[1]) + 1e-5]
        ordered = sorted(on_line, key=lambda p: (p[0] - a[0]) * dx + (p[1] - a[1]) * dy)
        for u, v in zip(ordered, ordered[1:]):
            graph[u].add(v)
            graph[v].add(u)
            split_edges.add(tuple(sorted((u, v))))
    remaining = set(graph)
    while remaining:
        start = min(remaining)
        connected, frontier = {start}, [start]
        while frontier:
            for other in graph[frontier.pop()]:
                if other not in connected:
                    connected.add(other)
                    frontier.append(other)
        remaining -= connected
        ends = sorted(terminals & connected)
        assert len(ends) >= 2, (net, "top stub without two physical wire holes", ends)
        assert all(len(graph[p]) != 1 or p in terminals for p in connected), (net, "non-via top endpoint")
        route = [e for e in sorted(split_edges) if e[0] in connected]
        links.append({"net": net, "terminals": ends, "route": route,
                      "length_mm": round(sum(math.dist(*e) for e in route), 2)})

source_board = pcbnew.LoadBoard(str(path))
obstacles = []


def rect(item, expand=0.15):
    box = item.GetBoundingBox()
    return (pcbnew.ToMM(box.GetX()) - expand, pcbnew.ToMM(box.GetY()) - expand,
            pcbnew.ToMM(box.GetRight()) + expand, pcbnew.ToMM(box.GetBottom()) + expand)


for f in source_board.GetFootprints():
    obstacles.extend(rect(p, 0.2) for p in f.Pads())
    obstacles.extend(rect(g) for g in f.GraphicalItems() if g.GetLayer() == pcbnew.F_SilkS)
    for t in (f.Reference(), f.Value()):
        if t.IsVisible() and t.GetLayer() == pcbnew.F_SilkS:
            obstacles.append(rect(t))
for v in vias:
    x, y = xy(v, "at")
    obstacles.append((x - 1.1, y - 1.1, x + 1.1, y + 1.1))


def choose_reference(ends, text):
    width = max(2.5, len(text) * 0.95)
    for radius in (1.8, 2.5, 3.2, 4, 5, 6):
        for x, y in ends:
            for dx, dy in ((0, -radius), (0, radius), (-radius, 0), (radius, 0),
                           (-radius, -radius), (radius, radius), (-radius, radius), (radius, -radius)):
                cx, cy = x + dx, y + dy
                box = (cx - width / 2, cy - 0.6, cx + width / 2, cy + 0.6)
                if not (1 < box[0] and box[2] < 99 and 1 < box[1] and box[3] < 209):
                    continue
                if all(box[2] < r[0] or r[2] < box[0] or box[3] < r[1] or r[3] < box[1] for r in obstacles):
                    obstacles.append(box)
                    return cx, cy, "F.SilkS"
    return ends[0][0], ends[0][1] - 2, "Dwgs.User"


for index, link in enumerate(links, 1):
    ref = f"JW{index}"
    name = f"Bottom_InsulatedWire_{ref}"
    x, y = link["terminals"][0]
    tx, ty, text_layer = choose_reference(link["terminals"], ref)
    fp = ["footprint", q(name), ["version", "20260206"], ["generator", q("pcbnew")],
          ["layer", q("F.Cu")],
          ["descr", q("Physical insulated component-side wire; all numbered 1 pads are internally joined ONLY when the wire is installed.")],
          ["property", q("Reference"), q(ref), ["at", str(tx - x), str(ty - y), "0"], ["layer", q(text_layer)],
           ["effects", ["font", ["size", "0.8", "0.8"], ["thickness", "0.1"]]]],
          ["property", q("Value"), q("Insulated wire link"), ["at", "0", "0", "0"], ["layer", q("F.Fab")],
           ["hide", "yes"], ["effects", ["font", ["size", "1", "1"]]]],
          ["attr", "through_hole", "board_only", "exclude_from_pos_files"],
          ["duplicate_pad_numbers_are_jumpers", "yes"]]
    for px, py in link["terminals"]:
        fp.append(["pad", q("1"), "thru_hole", "circle", ["at", str(px - x), str(py - y)],
                   ["size", "1.8", "1.8"], ["drill", "1.0"], ["layers", q("*.Cu"), q("*.Mask")]])
    for a, b in link["route"]:
        fp.append(["fp_line", ["start", str(a[0] - x), str(a[1] - y)], ["end", str(b[0] - x), str(b[1] - y)],
                   ["stroke", ["width", "0.2"], ["type", "solid"]], ["layer", q("Dwgs.User")]])
    save(TARGET / "DAR1000ES.pretty" / (name + ".kicad_mod"), fp)
    placed = copy.deepcopy(fp)
    placed[1] = q("DAR1000ES:" + name)
    placed.extend([["at", str(x), str(y)], ["uuid", q(uid("PCB:" + ref))]])
    for p in children(placed, "pad"):
        p.append(["net", q(link["net"])])
    board.append(placed)
    link.update(ref=ref, footprint="DAR1000ES:" + name, label_layer=text_layer)
board = [n for n in board if not (isinstance(n, list) and (n[0] == "via" or
         n[0] == "segment" and child(n, "layer")[1] == q("F.Cu")))]
save(TARGET / (BASE + ".kicad_pcb"), board)
(FILES / "pcb-v3-wire-links.json").write_text(json.dumps(links, indent=2))
print("Replaced all planning-layer copper and vias with", len(links), "explicit physical bottom-pad wire-link footprints.")
print("Terminal counts:", dict(collections.Counter(len(link["terminals"]) for link in links)))
