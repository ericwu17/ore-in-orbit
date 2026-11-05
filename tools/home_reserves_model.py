#!/usr/bin/env python3
"""A paper playtest of a new game's home sector, for tuning kHomeReserves
(src/sim/sector.h).

It sums what it takes, in raw resources, to reach the "Escape velocity"
milestone (travel and weapons) for three styles of player (a lean factory, a
fair-sized one and a sprawling one), using the recipe table and milestones
copied from src/sim/recipes.cpp and src/sim/milestones.cpp, then plays each
out minute by minute: collectors gather at 30 a minute (tractor arrays 120)
from home's reserves, the player builds more as they can afford them, and
each milestone is paid for in turn. It prints when each milestone completes
and when each of home's resources runs dry.

The model is rough: crafting is instant and every collector is always aimed
at what's short, so its minutes run faster than a real game's. Keep the
tables in step with the game's by hand.

    python3 tools/home_reserves_model.py                     # the game's amounts
    python3 tools/home_reserves_model.py 7000,1200,1100,1700  # others to try
"""
import sys
from collections import Counter
R = {  # item: (inputs, outputs count)
 'IronPlate':({'IronOre':1},1),'CopperPlate':({'CopperOre':1},1),'Glass':({'Silica':2},1),
 'Steel':({'IronPlate':3,'Carbon':1},1),'Plastic':({'Carbon':2},1),'Silicon':({'Silica':1,'Carbon':1},1),
 'IridiumPlate':({'IridiumOre':2},1),'FuelCell':({'Carbon':4},1),'Gear':({'IronPlate':2},1),
 'Circuit':({'IronPlate':1,'CopperPlate':1},1),'StructuralFrame':({'Steel':2,'Gear':2},1),
 'AdvancedCircuit':({'Circuit':2,'Plastic':1,'Silicon':1},1),'Processor':({'AdvancedCircuit':2,'IridiumPlate':1},1),
 'TransportBelt':({'Gear':1,'IronPlate':1},2),'AsteroidCollector':({'IronPlate':5,'Gear':3,'Circuit':2},1),
 'SolarFurnace':({'Silica':8,'IronOre':4},1),'CargoHold':({'IronPlate':8},1),
 'FilterSorter':({'IronPlate':2,'Circuit':1,'TransportBelt':2},1),'Unloader':({'IronPlate':2,'Gear':2,'Circuit':1},1),
 'Junction':({'IronPlate':2,'TransportBelt':2},1),'FabricatorMk1':({'IronPlate':8,'Gear':4,'Circuit':3},1),
 'Router':({'Circuit':2,'IronPlate':3,'TransportBelt':2},1),'BeltTunnel':({'IronPlate':10,'TransportBelt':5},2),
 'Refinery':({'Steel':10,'Circuit':10,'Glass':10},1),'FabricatorMk2':({'FabricatorMk1':1,'StructuralFrame':2,'AdvancedCircuit':3},1),
 'ExpressBelt':({'TransportBelt':1,'Gear':2,'Steel':1},1),
 'TractorArray':({'AsteroidCollector':1,'StructuralFrame':10,'AdvancedCircuit':10},1),
 'SurveyScanner':({'StructuralFrame':5,'AdvancedCircuit':5,'Glass':4},1),
 'ShipStarterPack':({'StructuralFrame':10,'AdvancedCircuit':5,'Steel':20,'Circuit':10},1),
 'CargoPod':({'Steel':2,'Circuit':1},2),
 'FocusCrystal':({'Glass':4,'CopperPlate':4,'Circuit':2},1),
}
RAW=['IronOre','CopperOre','Silica','Carbon','IridiumOre']
HOME = dict(zip(RAW, [7000,1200,1100,1700,0]))  # kHomeReserves
if len(sys.argv) > 1: HOME = dict(zip(RAW, list(map(int, sys.argv[1].split(','))) + [0]))
def raw(item,n=1.0):
    c=Counter()
    if item in RAW: c[item]+=n; return c
    ins,out=R[item]
    for k,v in ins.items(): c.update(raw(k,n*v/out))
    return c
def total(d):
    c=Counter()
    for k,v in d.items(): c.update(raw(k,v))
    return c
def show(name,c): print(f"{name:28s}"+"  ".join(f"{r[:4]} {c[r]:6.0f}" for r in RAW))

M = {
 'First light':{'IronPlate':50,'CopperPlate':20},
 'Heavy industry':{'Circuit':100,'Steel':50},
 'Deep space':{'AdvancedCircuit':50,'StructuralFrame':25},
 'Escape velocity':{'AdvancedCircuit':40,'StructuralFrame':15,'Glass':60},
}
ms=Counter()
for k,v in M.items():
    c=total(v); show(k,c); ms.update(c)
show('MILESTONES to EV',ms)
# factories at the moment EV completes, by player style (built on top of the starting kit)
styles = {
 'lean':   {'AsteroidCollector':10,'SolarFurnace':5,'FabricatorMk1':4,'Refinery':2,'TransportBelt':200,'FilterSorter':6,'Unloader':4,'Router':2,'CargoHold':1,'CargoPod':0},
 'typical':{'AsteroidCollector':18,'SolarFurnace':8,'FabricatorMk1':6,'Refinery':3,'FabricatorMk2':2,'TractorArray':2,'TransportBelt':400,'FilterSorter':12,'Unloader':6,'Router':4,'Junction':8,'BeltTunnel':10,'ExpressBelt':60,'CargoHold':3,'SurveyScanner':1},
 'sprawl': {'AsteroidCollector':26,'SolarFurnace':12,'FabricatorMk1':8,'Refinery':5,'FabricatorMk2':4,'TractorArray':4,'TransportBelt':600,'FilterSorter':20,'Unloader':10,'Router':8,'Junction':16,'BeltTunnel':20,'ExpressBelt':150,'CargoHold':6,'SurveyScanner':1,'ShipStarterPack':1,'CargoPod':20},
}
waste={'lean':0.10,'typical':0.25,'sprawl':0.40}  # extra intermediates made and left unused
AST, TRACTOR = 30.0, 120.0   # catches / min at 100%
ORDER = list(M) + ['Jump+fight', 'Frontier']
GOALS = dict(M)
GOALS['Jump+fight'] = {'FuelCell': 40, 'FocusCrystal': 20}  # a skiff's jump (2 thrusters x 10), weapon fuel, a fight's crystals
GOALS['Frontier'] = {'FuelCell': 100, 'FocusCrystal': 30}
# How fast each style builds out, and how big it gets by EV (from model.py's factories).
PROFILE = {  # collectors/min added, cap of asteroid collectors, tractors after Deep space, minutes of "thinking" per milestone
  'lean':    (0.5, 12, 0, 4.0),
  'typical': (1.0, 20, 2, 3.0),
  'sprawl':  (1.5, 28, 4, 2.0),
}
def run(style):
    add_rate, cap, tractors_max, think = PROFILE[style]
    w = 1 + waste[style]
    reserve = dict(HOME); stock = Counter(); coll = 2.0; tractors = 0
    # non-collector buildings paid as the game goes, spread over milestones before EV
    f = dict(styles[style]); f.pop('AsteroidCollector', None); tr = f.pop('TractorArray', 0)
    other = total(f)
    goal_i = 0; need = Counter()
    def load(i):
        c = Counter(total(GOALS[ORDER[i]]))
        for r in RAW: c[r] *= w   # intermediates made and left unused
        if i < 4:                 # buildings: refundable, so no waste
            for r in RAW: c[r] += other[r] / 4
        return c
    need = load(0); t = 0.0; dt = 1/60; log = []; dry = {}; pause = 0.0
    while t < 120 and goal_i < len(ORDER):
        t += dt
        # gather, split by what's still needed (a player aims collectors at what they're short of)
        rate = coll * AST + tractors * TRACTOR
        short = {r: max(need[r] - stock[r], 0) for r in RAW if reserve[r] > 0}
        tot = sum(short.values())
        for r in short:
            share = short[r] / tot if tot > 0 else 1 / len(short)
            got = min(rate * share * dt, reserve[r]); reserve[r] -= got; stock[r] += got
            if reserve[r] <= 0 and r not in dry: dry[r] = t
        for r in RAW:
            if HOME[r] and reserve[r] <= 0 and r not in dry: dry[r] = t
        # grow the factory: build collectors while there's stock for them
        if coll < cap:
            c = raw('AsteroidCollector', add_rate * dt)
            if all(stock[r] >= c[r] for r in RAW):
                for r in RAW: stock[r] -= c[r]
                coll += add_rate * dt
        if goal_i >= 3 and tractors < tractors_max:
            c = raw('TractorArray')
            if all(stock[r] >= c[r] for r in RAW):
                for r in RAW: stock[r] -= c[r]
                tractors += 1
        if pause > 0: pause -= dt; continue
        if all(stock[r] >= need[r] for r in RAW):
            for r in RAW: stock[r] -= need[r]
            log.append((ORDER[goal_i], t)); goal_i += 1; pause = think
            if goal_i < len(ORDER): need = load(goal_i)
        elif all(reserve[r] <= 0 for r in RAW if stock[r] < need[r]):
            log.append((ORDER[goal_i] + ' STUCK', t)); break
    ev = dict(log).get('Escape velocity')
    print(f"--- {style}: {coll:.0f} collectors, {tractors} tractors")
    for name, when in log: print(f"   {name:24s} at {when:5.1f} min")
    for r, when in sorted(dry.items(), key=lambda x: x[1]):
        print(f"   home {r:10s} dry at {when:5.1f} min" + (f"  ({when-ev:+.1f} after Escape velocity)" if ev else ""))
    left = {r: round(reserve[r]) for r in RAW if HOME[r]}
    print("   still in the ground:", left, " in the bridge:", {r: round(stock[r]) for r in RAW if HOME[r]})

for s in PROFILE: run(s)
