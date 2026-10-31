#!/usr/bin/env python3
"""Writes a placeholder PNG for every sprite the game looks for.

    python3 tools/make_placeholder_sprites.py           # only the missing ones
    python3 tools/make_placeholder_sprites.py --force   # all of them, overwriting

Needs Pillow (pip install pillow). Files go under assets/sprites/ (see the
readme's "Sprites" section):

    items/<ItemId>.png       64x64 icon for every item, buildings included
    buildings/<ItemId>.png   built look of every building, 64 px per cell,
                             facing North
    buildings/Bridge.png     the 6x6 bridge
    buildings/Hull.png       one cell of hull

Without --force, files that already exist are left alone, so real art is
never overwritten. The look follows the stand-ins the game draws for a
sprite it doesn't have (src/ui/ui_draw.cpp: lettered tiles, arrows on belts,
round lumps and square plates) in its industrial style and colors
(src/ui/ui_theme.h): steel plating, bevelled machine housings, rivets and
hazard stripes.

The tables below must match ItemKind (src/sim/game_types.h) and
footprint_of (src/sim/building.cpp); tests/test_sprites.cpp fails when a
sprite is missing or has the wrong shape.
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = os.path.join(ROOT, "imgui", "misc", "fonts", "Cousine-Regular.ttf")

CELL = 64     # output pixels per grid cell (and per icon side)
SS = 4        # drawn this many times larger, then scaled down (anti-aliasing)

# Colors from src/ui/ui_theme.h.
GLYPH = (236, 238, 241)          # kTileGlyph: letters and arrows
BEVEL_DARK = (11, 12, 14)        # kBevelDark: outlines
ACCENT = (242, 163, 43)          # kAccent: hazard amber
BAD = (224, 69, 44)              # kBad
GOOD = (95, 174, 110)            # kGood
WARN = (242, 193, 46)            # kWarn
PLUM = (160, 108, 150)           # kPlum
STEEL_HOVER = (112, 145, 178)    # kSteelHover
HULL = (90, 97, 107)             # kTileHull: steel deck plating
BELT = (64, 70, 79)              # kTileBelt
EXPRESS_BELT = (150, 112, 40)    # kTileExpressBelt
STORAGE = (112, 98, 72)          # kTileStorage
COLLECTOR = (150, 92, 58)        # kTileCollector: oxidized copper
MACHINE = (70, 92, 116)          # kTileMachine
POWER = (98, 120, 64)            # kTilePower
SYSTEM = (150, 62, 48)           # kTileSystem
TURRET = (48, 52, 58)            # kTileTurret
BRIDGE_COLOR = (201, 133, 22)    # kTileBridge

ITEM_IRON = (150, 160, 174)
ITEM_COPPER = (212, 124, 62)
ITEM_SILICA = (228, 204, 150)
ITEM_CARBON = (52, 48, 46)
ITEM_EXOTIC = PLUM
ITEM_CHEMICAL = GOOD
ITEM_COMPONENT = STEEL_HOVER

# Buildings: id -> (width, height in cells, look, color, label).
# Looks: hull, belt, tunnel, sorter, reverse_sorter, junction, router, tile,
# turret, bridge.
BUILDINGS = {
    "TransportBelt":     (1, 1, "belt", BELT, ""),
    "ExpressBelt":       (1, 1, "belt", EXPRESS_BELT, ""),
    "BeltTunnel":        (1, 1, "tunnel", BELT, ""),
    "FilterSorter":      (1, 1, "sorter", BELT, ""),
    "ReverseSorter":     (1, 1, "reverse_sorter", BELT, ""),
    "Junction":          (1, 1, "junction", BELT, ""),
    "Router":            (1, 1, "router", BELT, ""),
    "CargoHold":         (2, 2, "tile", STORAGE, "C"),
    "AsteroidCollector": (2, 2, "tile", COLLECTOR, "A"),
    "TractorArray":      (3, 3, "tile", COLLECTOR, "TA"),
    "SolarFurnace":      (2, 2, "tile", MACHINE, "SF"),
    "Refinery":          (3, 3, "tile", MACHINE, "R"),
    "FabricatorMk1":     (3, 3, "tile", MACHINE, "F1"),
    "FabricatorMk2":     (3, 3, "tile", MACHINE, "F2"),
    "FabricatorMk3":     (3, 3, "tile", MACHINE, "F3"),
    "SolarPanel":        (2, 2, "tile", POWER, "SP"),
    "SolarArray":        (3, 3, "tile", POWER, "SA"),
    "BatteryBank":       (2, 2, "tile", POWER, "BB"),
    "SurveyScanner":     (2, 2, "tile", SYSTEM, "SS"),
    "IonThruster":       (2, 3, "tile", SYSTEM, "IT"),
    "Autocannon":        (2, 2, "turret", TURRET, "AC"),
    "Railgun":           (3, 3, "turret", TURRET, "RG"),
}
BRIDGE = (6, 6, "bridge", BRIDGE_COLOR, "BRIDGE")
HULL_TILE = (1, 1, "hull", HULL, "")

# Everything else: id -> (shape, color, label). Shapes: lump (round: raw
# resources), plate (square: made things).
ITEMS = {
    "IronOre":           ("lump", ITEM_IRON, "Fe"),
    "CopperOre":         ("lump", ITEM_COPPER, "Cu"),
    "Silica":            ("lump", ITEM_SILICA, "SiO"),
    "Carbon":            ("lump", ITEM_CARBON, "C"),
    "IridiumOre":        ("lump", ITEM_EXOTIC, "Ir"),
    "IronPlate":         ("plate", ITEM_IRON, "Fe"),
    "CopperPlate":       ("plate", ITEM_COPPER, "Cu"),
    "Glass":             ("plate", ITEM_SILICA, "Gl"),
    "Steel":             ("plate", ITEM_IRON, "St"),
    "Silicon":           ("plate", ITEM_SILICA, "Si"),
    "IridiumPlate":      ("plate", ITEM_EXOTIC, "Ir"),
    "Plastic":           ("plate", ITEM_CHEMICAL, "Pl"),
    "FuelCell":          ("plate", WARN, "Fu"),
    "Gear":              ("plate", ITEM_COMPONENT, "Gr"),
    "Circuit":           ("plate", ITEM_COMPONENT, "Ci"),
    "StructuralFrame":   ("plate", ITEM_COMPONENT, "Fr"),
    "AdvancedCircuit":   ("plate", ITEM_COMPONENT, "AC"),
    "Processor":         ("plate", ITEM_COMPONENT, "CPU"),
    "ShipStarterPack":   ("plate", ACCENT, "SHIP"),
    "CargoPod":          ("plate", STORAGE, "Pod"),
    "SlugMagazine":      ("plate", ITEM_IRON, "Mag"),
    "RailSlug":          ("plate", ITEM_EXOTIC, "RS"),
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


def bevel(draw, box, color, edge):
    """A flat plate over box, lit along its top and left, shaded along its
    bottom and right, outlined dark."""
    x0, y0, x1, y1 = box
    draw.rectangle(box, fill=color)
    draw.line((x0, y0 + edge / 2, x1, y0 + edge / 2), fill=lighter(color, 0.3), width=edge)
    draw.line((x0 + edge / 2, y0, x0 + edge / 2, y1), fill=lighter(color, 0.3), width=edge)
    draw.line((x0, y1 - edge / 2, x1, y1 - edge / 2), fill=darker(color, 0.6), width=edge)
    draw.line((x1 - edge / 2, y0, x1 - edge / 2, y1), fill=darker(color, 0.6), width=edge)
    draw.rectangle(box, outline=BEVEL_DARK, width=max(1, edge // 2))


def rivets(draw, box, r, inset, color):
    """A rivet near each corner of box, with a glint."""
    x0, y0, x1, y1 = box
    for x in (x0 + inset, x1 - inset):
        for y in (y0 + inset, y1 - inset):
            draw.ellipse((x - r, y - r, x + r, y + r), fill=darker(color, 0.55))
            draw.ellipse((x - r * 0.45 - r * 0.3, y - r * 0.45 - r * 0.3, x - r * 0.3 + r * 0.2, y - r * 0.3 + r * 0.2),
                         fill=lighter(color, 0.45))


def hazard(img, box, color, stripe):
    """Diagonal stripes in color (on black) over box."""
    x0, y0, x1, y1 = (int(v) for v in box)
    band = Image.new("RGBA", (x1 - x0, y1 - y0), BEVEL_DARK + (255,))
    d = ImageDraw.Draw(band)
    h = y1 - y0
    x = -h
    while x < x1 - x0:
        d.polygon([(x, h), (x + h, 0), (x + h + stripe, 0), (x + stripe, h)], fill=color)
        x += stripe * 2
    img.alpha_composite(band, (x0, y0))


def finish(img):
    w, h = img.size
    return img.resize((w // SS, h // SS), Image.LANCZOS)


def building_image(w, h, look, color, label):
    s = CELL * SS
    img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    W, H = w * s, h * s
    inset = s * 0.06
    box = (inset, inset, W - inset, H - inset)
    edge = max(2, int(s * 0.05))

    if look == "hull":
        # A steel deck plate: bevelled, a seam inset all round, a rivet in
        # each corner.
        bevel(d, (0, 0, W - 1, H - 1), color, edge)
        seam = s * 0.14
        d.rectangle((seam, seam, W - seam, H - seam), outline=darker(color, 0.8), width=max(1, edge // 2))
        rivets(d, (0, 0, W, H), s * 0.05, s * 0.08, color)
    elif look in ("belt", "tunnel", "sorter", "reverse_sorter"):
        # A dark rubber belt between steel rails.
        d.rectangle(box, fill=darker(BELT, 0.7), outline=BEVEL_DARK, width=max(1, edge // 2))
        rail = s * 0.09
        for x0 in (box[0], box[2] - rail):
            d.rectangle((x0, box[1], x0 + rail, box[3]), fill=lighter(BELT, 0.15))
        if look == "sorter":
            d.rectangle(box, outline=WARN, width=int(s * 0.08))
        elif look == "reverse_sorter":
            d.rectangle(box, outline=BAD, width=int(s * 0.08))
        else:
            # An arrow on every cell: light on a plain belt, amber on an
            # express one.
            arrow = ACCENT if color == EXPRESS_BELT else GLYPH
            for cy in range(h):
                for cx in range(w):
                    arrow_up(d, (cx + 0.5) * s, (cy + 0.5) * s, s * 0.26, arrow)
        if look == "tunnel":
            # The tunnel mouth: a hazard-striped bar across the back (south)
            # edge.
            y = H / 2 + s * 0.3
            hazard(img, (W / 2 - s * 0.38, y - s * 0.07, W / 2 + s * 0.38, y + s * 0.07), ACCENT, s * 0.06)
    elif look == "junction":
        # A cross: the two ways over it.
        d.rectangle(box, fill=darker(BELT, 0.7), outline=BEVEL_DARK, width=max(1, edge // 2))
        bar = s * 0.07
        d.rectangle((W / 2 - bar, box[1] + edge, W / 2 + bar, box[3] - edge), fill=GLYPH)
        d.rectangle((box[0] + edge, H / 2 - bar, box[2] - edge, H / 2 + bar), fill=GLYPH)
    elif look == "router":
        # An arrow out of every side.
        bevel(d, box, color, edge)
        for i in range(4):
            arrow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            arrow_up(ImageDraw.Draw(arrow), W / 2, H / 2 - s * 0.25, s * 0.13, GLYPH)
            img.alpha_composite(arrow.rotate(-90 * i))
    else:  # tile, turret, bridge: a machine housing
        bevel(d, box, color, edge)
        # A recessed panel inside, where the label goes.
        panel = s * 0.22 if min(w, h) > 1 else s * 0.16
        inner = (box[0] + panel, box[1] + panel, box[2] - panel, box[3] - panel)
        d.rectangle(inner, fill=darker(color, 0.72), outline=darker(color, 0.5), width=max(1, edge // 2))
        rivets(d, box, s * 0.045, s * 0.1, color)
        if look == "bridge":
            # Hazard stripes along the top and bottom of the bridge.
            band = s * 0.35
            hazard(img, (box[0] + edge, box[1] + edge, box[2] - edge, box[1] + band), ACCENT, s * 0.18)
            hazard(img, (box[0] + edge, box[3] - band, box[2] - edge, box[3] - edge), ACCENT, s * 0.18)
            d = ImageDraw.Draw(img)
        if look == "turret":
            # A barrel ring round the label.
            r = min(inner[2] - inner[0], inner[3] - inner[1]) * 0.42
            cx, cy = (inner[0] + inner[2]) / 2, (inner[1] + inner[3]) / 2
            d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=ACCENT, width=edge)
        centered_text(d, inner, label, ACCENT if look == "turret" else GLYPH, int(s * 0.55))
    return finish(img)


def placeable_icon(item_id):
    """A placeable's item icon: its building, shrunk to fit a square."""
    w, h, look, color, label = BUILDINGS[item_id]
    if look in ("tile", "turret"):
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
    m = s * 0.12
    if shape == "lump":
        d.ellipse((m, m, s - m, s - m), fill=color, outline=BEVEL_DARK, width=edge)
        d.arc((m + edge * 2, m + edge * 2, s - m - edge * 2, s - m - edge * 2), 190, 260, fill=lighter(color),
              width=edge)
    else:
        bevel(d, (m, m, s - m, s - m), color, edge)
    text = BEVEL_DARK if sum(color) > 450 else GLYPH
    centered_text(d, (s * 0.15, s * 0.15, s * 0.85, s * 0.85), label, text, int(s * 0.4))
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
    write("buildings/Hull.png", lambda: building_image(*HULL_TILE))
    for item_id, spec in ITEMS.items():
        write(f"items/{item_id}.png", lambda spec=spec: item_image(*spec))

    print(f"{written} written, {skipped} already there (kept; --force to overwrite)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
