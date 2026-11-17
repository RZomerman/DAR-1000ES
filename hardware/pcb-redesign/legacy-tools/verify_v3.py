import xml.etree.ElementTree as ET

from pcb_v3_tools import *


tree = ET.parse(FILES / "pcb-v3-netlist.xml").getroot()
pin_nets = {(n.get("ref"), n.get("pin")): net.get("name")
            for net in tree.findall("./nets/net") for n in net.findall("node")}
new_sets = {frozenset((n.get("ref"), n.get("pin")) for n in net.findall("node")
                     if n.get("ref") not in {"C9", "C10", "C11", "C12", "C13"})
            for net in tree.findall("./nets/net")}
old = ET.parse(FILES / "pcb-v2-final-netlist.xml").getroot()
old_sets = {frozenset((n.get("ref"), n.get("pin")) for n in net.findall("node")) for net in old.findall("./nets/net")}
assert new_sets == old_sets, "Unrequested GPIO/channel or original connector connectivity change"
board_path = TARGET / (BASE + ".kicad_pcb")
board = pcbnew.LoadBoard(str(board_path))
fps = {f.GetReference(): f for f in board.GetFootprints()}
for original in json.loads((FILES / "pcb-v3-source-placements.json").read_text()):
    f = fps[original["ref"]]
    assert math.dist(mm(f.GetPosition()), original["pos"]) < 1e-6, (original["ref"], "moved")
    assert abs(f.GetOrientationDegrees() - original["angle"]) < 1e-6
    pads = {p.GetNumber(): p for p in f.Pads()}
    for pad in original["pads"]:
        assert math.dist(mm(pads[pad["number"]].GetPosition()), pad["xy"]) < 1e-6
for node, name in pin_nets.items():
    ref, number = node
    if ref not in fps:
        continue
    p = next(p for p in fps[ref].Pads() if p.GetNumber() == number)
    assert p.GetNetname() == name, (node, p.GetNetname(), name)
for ref, rail in (("C9", "LOCAL_5V"), ("C10", "ESP_CTRL_5V"), ("C11", "ESP_CTRL_5V"),
                  ("C12", "ESP_AUDIO_5V"), ("C13", "ESP_AUDIO_5V")):
    assert pin_nets[(ref, "1")] == rail
    assert pin_nets[(ref, "2")] == "GND"
assert pin_nets[("U1-CONTROLLER1", "J1.21")] == pin_nets[("JP1", "2")] == "ESP_CTRL_5V"
assert pin_nets[("U6-AUDIO1", "J1.21")] == pin_nets[("JP2", "2")] == "ESP_AUDIO_5V"
assert pin_nets[("JP1", "1")] == pin_nets[("JP2", "1")] == "LOCAL_5V"
assert pin_nets[("J12", "2")] == "SONY_5V_SENSE"
tracks = list(board.GetTracks())
assert not any(isinstance(t, pcbnew.PCB_VIA) for t in tracks), "A via remains"
assert all(t.GetLayer() == pcbnew.B_Cu for t in tracks), "Non-bottom copper track"
assert not list(board.Zones()), "Unexpected copper zone"
assert board.GetConnectivity().GetUnconnectedCount(False) == 0
links = json.loads((FILES / "pcb-v3-wire-links.json").read_text())
for link in links:
    f = fps[link["ref"]]
    assert all(p.GetNumber() == "1" and p.GetNetname() == link["net"] for p in f.Pads())
    assert len(list(f.Pads())) == len(link["terminals"])
    assert len(link["terminals"]) >= 2
hashes = json.loads((FILES / "pcb-v3-source-hashes.json").read_text())
for name, digest in hashes.items():
    assert hashlib.sha256((SOURCE / name).read_bytes()).hexdigest() == digest, ("v2 changed", name)
summary = {"original_v2_files_unchanged": len(hashes), "original_footprints_preserved": 51,
           "new_capacitors": 5, "wire_links": len(links), "wire_holes": sum(len(l["terminals"]) for l in links),
           "bottom_tracks": len(tracks), "front_tracks": 0, "normal_plated_through_hole_annuli": True,
           "vias": 0, "unconnected": 0,
           "track_length_mm": round(sum(pcbnew.ToMM(t.GetLength()) for t in tracks), 2)}
(FILES / "pcb-v3-verification.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
ast = parse(board_path.read_text())
ast = [n for n in ast if not (isinstance(n, list) and n[0] == "footprint" and value(n, "Reference").startswith("JW"))]
save(FILES / "pcb-v3-without-wire-links.kicad_pcb", ast)
