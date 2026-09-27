import pcbnew, collections
b=pcbnew.LoadBoard("ttgo_xpa_routed.kicad_pcb")
got=collections.defaultdict(set)
for fp in b.GetFootprints():
    for p in fp.Pads():
        n=p.GetNetname()
        if n: got[n].add(f"{fp.GetReference()}.{p.GetNumber()}")
expected={
 "GND":{"C1.2","C2.2","C3.2","C5.2","Q1.2","R3.2","U1.4","D2.2","J1.4","J2.R2"},
 "+12V_RAW":{"J1.1","D1.2","D2.1"},
 "+12V":{"D1.1","C1.1","U1.2","U1.3"},
 "3V3":{"U1.1","L1.2","C2.1","C3.1","J2.L1"},
 "SW":{"U1.5","L1.1","C4.2"},
 "BST":{"U1.6","C4.1"},
 "PTT":{"Q1.3","J1.2"},
 "BAND":{"R1.1","C5.1","J1.3"},
 "G26":{"R2.1","J2.R4"},
 "G27":{"R1.2","J2.R3"},
 "GATE":{"Q1.1","R2.2","R3.1"},
}
ok=True
for n,exp in expected.items():
    g=got.get(n,set())
    st="OK " if g==exp else "FEHLER"
    if g!=exp: ok=False
    print(st,n.ljust(9),sorted(g), "" if g==exp else f"erwartet {sorted(exp)}")
extra=set(got)-set(expected)
print("weitere Netze:",extra or "keine")
# 5V-Pad und alle uebrigen TTGO-Pads ohne Netz?
for fp in b.GetFootprints():
    if fp.GetReference()=="J2":
        conn=[p.GetNumber() for p in fp.Pads() if p.GetNetname()]
        print("J2 verbundene Pads:",sorted(conn),"| R1 (5V) Netz:",[p.GetNetname() for p in fp.Pads() if p.GetNumber()=="R1"])
print("GESAMT:", "ALLES OK" if ok else "PROBLEM")
