#!/usr/bin/env python3
"""Writes a placeholder PNG for every sprite the game looks for.

    python3 tools/make_placeholder_sprites.py           # only the missing ones
    python3 tools/make_placeholder_sprites.py --force   # all of them, overwriting

Needs Pillow (pip install pillow). Files go under assets/sprites/ (see the
readme's "Sprites" section):

    items/<ItemId>.png       64x64 icon for every item, placeables included
    buildings/<ItemId>.png   built look of every placeable, 64 px per cell,
                             facing North
    buildings/Bridge.png     the 6x6 bridge
    fluids/<FluidId>.png     64x64 icon for every fluid

Without --force, files that already exist are left alone, so real art is
never overwritten. The look copies the game's old procedural drawing
(lettered tiles, arrows on belts, round lumps and square plates) and its
colors (src/ui/ui_theme.h).

The tables below must match ItemKind / FluidKind (src/sim/game_types.h) and
footprint_of (src/sim/building.cpp); tests/test_sprites.cpp fails when a
sprite is missing or has the wrong shape.
"""

import argparse
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = os.path.join(ROOT, "imgui", "misc", "fonts", "Roboto-Medium.ttf")

CELL = 64     # output pixels per grid cell (and per icon side)
SS = 4        # drawn this many times larger, then scaled down (anti-aliasing)

# Colors from src/ui/ui_theme.h.
PAPER_LIGHT = (252, 249, 242)
INK = (32, 42, 68)
INK_DIM = (118, 110, 96)
RED = (196, 52, 44)
RED_ACTIVE = (164, 40, 34)
NAVY = (44, 66, 112)
NAVY_HOVER = (62, 88, 140)
GOOD = (58, 135, 88)
WARN = (214, 158, 46)
WARN_TEXT = (160, 110, 18)
TEAL = (52, 128, 140)
PLUM = (120, 72, 110)
HULL = (204, 189, 162)
REINFORCED_HULL = (168, 150, 120)
STORAGE = (140, 112, 72)

ITEM_IRON = (118, 128, 142)
ITEM_COPPER = (196, 112, 52)
ITEM_NICKEL = (146, 160, 112)
ITEM_SILICA = (224, 198, 140)
ITEM_CARBON = (62, 56, 52)
ITEM_ICE = (150, 200, 230)
ITEM_EXOTIC = PLUM
ITEM_CHEMICAL = GOOD
ITEM_COMPONENT = NAVY

# Placeables: id -> (width, height in cells, look, color, label).
# Looks: hull, belt, tunnel, sorter, reverse_sorter, junction, router, pipe,
# pump, tile.
BUILDINGS = {
    "Hull":              (1, 1, "hull", HULL, ""),
    "ReinforcedHull":    (1, 1, "hull", REINFORCED_HULL, ""),
    "TransportBelt":     (1, 1, "belt", INK_DIM, ""),
    "ExpressBelt":       (1, 1, "belt", WARN_TEXT, ""),
    "BeltTunnel":        (1, 1, "tunnel", INK_DIM, ""),
    "FilterSorter":      (1, 1, "sorter", INK_DIM, ""),
    "ReverseSorter":     (1, 1, "reverse_sorter", INK_DIM, ""),
    "Junction":          (1, 1, "junction", INK_DIM, ""),
    "Router":            (1, 1, "router", INK_DIM, ""),
    "CargoHold":         (2, 2, "tile", STORAGE, "C"),
    "Pipe":              (1, 1, "pipe", NAVY, ""),
    "Pump":              (1, 1, "pump", NAVY, ""),
    "FluidTank":         (2, 2, "tile", STORAGE, "FT"),
    "AsteroidCollector": (2, 2, "tile", PLUM, "A"),
    "TractorArray":      (3, 3, "tile", PLUM, "TA"),
    "HydrogenCollector": (2, 2, "tile", TEAL, "H"),
    "AsteroidCrusher":   (2, 2, "tile", NAVY_HOVER, "Cr"),
    "SolarFurnace":      (2, 2, "tile", NAVY_HOVER, "SF"),
    "ArcFurnace":        (3, 3, "tile", NAVY_HOVER, "AF"),
    "IceMelter":         (2, 2, "tile", NAVY_HOVER, "IM"),
    "Electrolyzer":      (2, 2, "tile", NAVY_HOVER, "El"),
    "ChemicalPlant":     (3, 3, "tile", NAVY_HOVER, "CP"),
    "FabricatorMk1":     (3, 3, "tile", NAVY_HOVER, "F1"),
    "FabricatorMk2":     (3, 3, "tile", NAVY_HOVER, "F2"),
    "FabricatorMk3":     (3, 3, "tile", NAVY_HOVER, "F3"),
    "SolarPanel":        (2, 2, "tile", GOOD, "SP"),
    "SolarArray":        (3, 3, "tile", GOOD, "SA"),
    "FuelCellGenerator": (2, 2, "tile", GOOD, "FC"),
    "BatteryBank":       (2, 2, "tile", GOOD, "BB"),
    "SurveyScanner":     (2, 2, "tile", RED_ACTIVE, "SS"),
    "IonThruster":       (2, 3, "tile", RED_ACTIVE, "IT"),
}
BRIDGE = (6, 6, "tile", RED, "BRIDGE")

# Everything else: id -> (shape, color, label). Shapes: chunk (a rough
# rock), lump (round: raw resources), plate (square: made things).
ITEMS = {
    "MetallicChunk":     ("chunk", ITEM_IRON, "Me"),
    "SilicateChunk":     ("chunk", ITEM_SILICA, "Si"),
    "CarbonaceousChunk": ("chunk", ITEM_CARBON, "Ca"),
    "IceChunk":          ("chunk", ITEM_ICE, "Ic"),
    "ExoticChunk":       ("chunk", ITEM_EXOTIC, "Ex"),
    "IronOre":           ("lump", ITEM_IRON, "Fe"),
    "NickelOre":         ("lump", ITEM_NICKEL, "Ni"),
    "CopperOre":         ("lump", ITEM_COPPER, "Cu"),
    "Silica":            ("lump", ITEM_SILICA, "SiO"),
    "Carbon":            ("lump", ITEM_CARBON, "C"),
    "WaterIce":          ("lump", ITEM_ICE, "H2O"),
    "IridiumOre":        ("lump", ITEM_EXOTIC, "Ir"),
    "IronPlate":         ("plate", ITEM_IRON, "Fe"),
    "CopperPlate":       ("plate", ITEM_COPPER, "Cu"),
    "NickelPlate":       ("plate", ITEM_NICKEL, "Ni"),
    "Glass":             ("plate", ITEM_SILICA, "Gl"),
    "Steel":             ("plate", ITEM_IRON, "St"),
    "Silicon":           ("plate", ITEM_SILICA, "Si"),
    "IridiumPlate":      ("plate", ITEM_EXOTIC, "Ir"),
    "Plastic":           ("plate", ITEM_CHEMICAL, "Pl"),
    "Battery":           ("plate", ITEM_CHEMICAL, "Bt"),
    "PropellantCell":    ("plate", ITEM_CHEMICAL, "PC"),
    "Gear":              ("plate", ITEM_COMPONENT, "Gr"),
    "CopperWire":        ("plate", ITEM_COPPER, "Wi"),
    "Circuit":           ("plate", ITEM_COMPONENT, "Ci"),
    "ServoMotor":        ("plate", ITEM_COMPONENT, "SM"),
    "StructuralFrame":   ("plate", ITEM_COMPONENT, "Fr"),
    "PhotovoltaicCell":  ("plate", ITEM_COMPONENT, "PV"),
    "AdvancedCircuit":   ("plate", ITEM_COMPONENT, "AC"),
    "Processor":         ("plate", ITEM_COMPONENT, "CPU"),
}

FLUIDS = {
    "Water":    ((70, 140, 210), "H2O"),
    "Hydrogen": (TEAL, "H2"),
    "Oxygen":   (RED, "O2"),
}


def darker(color, k=0.65):
    return tuple(int(c * k) for c in color)


def lighter(color, k=0.35):
    return tuple(int(c + (255 - c) * k) for c in color)


_fonts = {}


def font(px):
    if px not in _fonts:
        try:
            _fonts[px] = ImageFont.truetype(FONT_PATH, px)
        except OSError:
            _fonts[px] = ImageFont.load_default(px)
    return _fonts[px]


def centered_text(draw, box, text, fill, max_px):
    """Text as large as fits (up to max_px tall) in the middle of box."""
    if not text:
        return
    x0, y0, x1, y1 = box
    px = max_px
    while px > 8:
        l, t, r, b = draw.textbbox((0, 0), text, font=font(px))
        if r - l <= (x1 - x0) * 0.8 and b - t <= (y1 - y0) * 0.8:
            break
        px -= 2
    l, t, r, b = draw.textbbox((0, 0), text, font=font(px))
    pos = ((x0 + x1 - (r - l)) / 2 - l, (y0 + y1 - (b - t)) / 2 - t)
    draw.text(pos, text, font=font(px), fill=fill)


def arrow_up(draw, cx, cy, h, fill):
    """A filled arrowhead pointing up (North), centered on (cx, cy)."""
    draw.polygon([(cx, cy - h), (cx - h * 0.8, cy + h * 0.5), (cx + h * 0.8, cy + h * 0.5)], fill=fill)


def finish(img):
    w, h = img.size
    return img.resize((w // SS, h // SS), Image.LANCZOS)


def building_image(w, h, look, color, label):
    s = CELL * SS
    img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    W, H = w * s, h * s
    inset = s * 0.08
    box = (inset, inset, W - inset, H - inset)
    edge = max(2, int(s * 0.05))

    if look == "hull":
        d.rectangle((0, 0, W, H), fill=color)
        d.rectangle((0, 0, W - 1, H - 1), outline=darker(color, 0.85), width=edge)
        r = s * 0.06
        for fx in (0.17, 0.83):
            for fy in (0.17, 0.83):
                d.ellipse((W * fx - r, H * fy - r, W * fx + r, H * fy + r), fill=darker(color, 0.75))
    elif look in ("belt", "tunnel", "sorter", "reverse_sorter"):
        d.rounded_rectangle(box, radius=s * 0.08, fill=color, outline=darker(color), width=edge)
        if look == "sorter":
            d.rounded_rectangle(box, radius=s * 0.08, outline=WARN, width=int(s * 0.08))
        elif look == "reverse_sorter":
            d.rounded_rectangle(box, radius=s * 0.08, outline=RED, width=int(s * 0.08))
        else:
            # Rails down both sides, and an arrow on every cell.
            for x in (box[0] + s * 0.1, box[2] - s * 0.1):
                d.line((x, box[1] + s * 0.06, x, box[3] - s * 0.06), fill=darker(color), width=edge)
            for cy in range(h):
                for cx in range(w):
                    arrow_up(d, (cx + 0.5) * s, (cy + 0.5) * s, s * 0.28, PAPER_LIGHT)
        if look == "tunnel":
            # The tunnel mouth: a bar across the back (south) edge.
            y = H / 2 + s * 0.3
            d.line((W / 2 - s * 0.38, y, W / 2 + s * 0.38, y), fill=INK, width=int(s * 0.12))
    elif look == "junction":
        # A cross: the two ways over it.
        d.rounded_rectangle(box, radius=s * 0.08, fill=color, outline=darker(color), width=edge)
        bar = s * 0.07
        d.rectangle((W / 2 - bar, box[1] + edge, W / 2 + bar, box[3] - edge), fill=PAPER_LIGHT)
        d.rectangle((box[0] + edge, H / 2 - bar, box[2] - edge, H / 2 + bar), fill=PAPER_LIGHT)
    elif look == "router":
        # An arrow out of every side.
        d.rounded_rectangle(box, radius=s * 0.15, fill=color, outline=darker(color), width=edge)
        for i in range(4):
            arrow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            arrow_up(ImageDraw.Draw(arrow), W / 2, H / 2 - s * 0.25, s * 0.13, PAPER_LIGHT)
            img.alpha_composite(arrow.rotate(-90 * i))
    elif look == "pipe":
        # Just the hub: the game draws the joins to neighboring pipes.
        half = s * 0.14
        d.rectangle((W / 2 - half, H / 2 - half, W / 2 + half, H / 2 + half), fill=color,
                    outline=darker(color), width=edge // 2)
    elif look == "pump":
        d.rounded_rectangle(box, radius=s * 0.15, fill=color, outline=darker(color), width=edge)
        arrow_up(d, W / 2, H / 2, s * 0.28, PAPER_LIGHT)
    else:  # tile
        d.rounded_rectangle(box, radius=s * 0.15, fill=color, outline=darker(color), width=edge)
        d.rounded_rectangle((box[0] + edge * 2, box[1] + edge * 2, box[2] - edge * 2, box[3] - edge * 2),
                            radius=s * 0.1, outline=lighter(color, 0.25), width=edge // 2)
        centered_text(d, box, label, PAPER_LIGHT, int(s * 0.55))
    return finish(img)


def placeable_icon(item_id):
    """A placeable's item icon: its building, shrunk to fit a square."""
    w, h, look, color, label = BUILDINGS[item_id]
    if look == "pipe":
        # A lone hub reads as nothing at icon size: draw a pipe cross.
        s = CELL * SS
        img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        half = s * 0.12
        for rect in ((s * 0.1, s / 2 - half, s * 0.9, s / 2 + half), (s / 2 - half, s * 0.1, s / 2 + half, s * 0.9)):
            d.rectangle(rect, fill=color, outline=darker(color), width=SS * 2)
        return finish(img)
    if look == "tile":
        # Shrunk, a big building's label is unreadable: one tile instead.
        return building_image(1, 1, look, color, label)
    built = building_image(w, h, look, color, label)
    k = CELL / max(built.size)
    small = built.resize((max(1, round(built.width * k)), max(1, round(built.height * k))), Image.LANCZOS)
    icon = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    icon.paste(small, ((CELL - small.width) // 2, (CELL - small.height) // 2), small)
    return icon


def item_image(shape, color, label):
    s = CELL * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    edge = SS * 2
    if shape == "chunk":
        # A rough rock: a lumpy polygon with fixed "random" radii.
        radii = [0.40, 0.44, 0.36, 0.43, 0.38, 0.45, 0.35, 0.42, 0.39]
        pts = []
        for i, r in enumerate(radii):
            a = 2 * math.pi * i / len(radii) - math.pi / 2
            pts.append((s / 2 + math.cos(a) * r * s, s / 2 + math.sin(a) * r * s))
        d.polygon(pts, fill=color, outline=INK, width=edge)
    elif shape == "lump":
        m = s * 0.12
        d.ellipse((m, m, s - m, s - m), fill=color, outline=INK, width=edge)
    else:
        m = s * 0.12
        d.rounded_rectangle((m, m, s - m, s - m), radius=s * 0.1, fill=color, outline=INK, width=edge)
        d.line((m + edge * 2, m + edge * 2, s - m - edge * 2, m + edge * 2), fill=lighter(color), width=edge)
    text = INK if sum(color) > 450 else PAPER_LIGHT
    centered_text(d, (s * 0.15, s * 0.15, s * 0.85, s * 0.85), label, text, int(s * 0.4))
    return finish(img)


def fluid_image(color, label):
    s = CELL * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # A droplet: a circle with a point on top.
    cx, cy, r = s / 2, s * 0.6, s * 0.3
    d.polygon([(cx, s * 0.08), (cx - r * 0.92, cy - r * 0.4), (cx + r * 0.92, cy - r * 0.4)], fill=color)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)
    text = INK if sum(color) > 450 else PAPER_LIGHT
    centered_text(d, (cx - r, cy - r * 0.8, cx + r, cy + r * 0.8), label, text, int(s * 0.3))
    return finish(img)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--force", action="store_true", help="overwrite sprites that already exist")
    parser.add_argument("--out", default=os.path.join(ROOT, "assets", "sprites"), help="sprites folder")
    args = parser.parse_args()

    written = skipped = 0

    def write(rel, make):
        nonlocal written, skipped
        path = os.path.join(args.out, rel)
        if os.path.exists(path) and not args.force:
            skipped += 1
            return
        os.makedirs(os.path.dirname(path), exist_ok=True)
        make().save(path, optimize=True)
        written += 1

    for item_id, spec in BUILDINGS.items():
        write(f"buildings/{item_id}.png", lambda spec=spec: building_image(*spec))
        write(f"items/{item_id}.png", lambda item_id=item_id: placeable_icon(item_id))
    write("buildings/Bridge.png", lambda: building_image(*BRIDGE))
    for item_id, spec in ITEMS.items():
        write(f"items/{item_id}.png", lambda spec=spec: item_image(*spec))
    for fluid_id, spec in FLUIDS.items():
        write(f"fluids/{fluid_id}.png", lambda spec=spec: fluid_image(*spec))

    print(f"{written} written, {skipped} already there (kept; --force to overwrite)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
