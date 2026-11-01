// ui_draw.cpp — drawing buildings, items and icons into a draw list, from
// their sprites or, without one, stand-in shapes (see ui_internal.h).
#include "ui/ui_internal.h"

#include <algorithm>

namespace ui {

namespace {

// Unit vector for a direction in screen space (y grows down).
ImVec2 DirectionVector(Direction d) {
    switch (d) {
    case Direction::North: return {0.0f, -1.0f};
    case Direction::East:  return {1.0f, 0.0f};
    case Direction::South: return {0.0f, 1.0f};
    case Direction::West:  return {-1.0f, 0.0f};
    }
    return {0.0f, 0.0f};
}

// The short label on a building's tile, for the ones drawn as lettered tiles.
const char* TileLabel(ItemKind kind) {
    switch (kind) {
    case ItemKind::AsteroidCollector: return "A";
    case ItemKind::TractorArray:      return "TA";
    case ItemKind::CargoHold:         return "C";
    case ItemKind::SolarFurnace:      return "SF";
    case ItemKind::Refinery:          return "R";
    case ItemKind::FabricatorMk1:     return "F1";
    case ItemKind::FabricatorMk2:     return "F2";
    case ItemKind::FabricatorMk3:     return "F3";
    case ItemKind::SolarPanel:        return "SP";
    case ItemKind::SolarArray:        return "SA";
    case ItemKind::BatteryBank:       return "BB";
    case ItemKind::SurveyScanner:     return "SS";
    case ItemKind::IonThruster:       return "IT";
    case ItemKind::Autocannon:        return "AC";
    case ItemKind::Railgun:           return "RG";
    default:                          return "?";
    }
}

// The tile color of a lettered building, by family.
ImVec4 TileColor(ItemKind kind) {
    switch (kind) {
    case ItemKind::AsteroidCollector:
    case ItemKind::TractorArray:      return theme::kTileCollector;
    case ItemKind::CargoHold:         return theme::kTileStorage;
    case ItemKind::SolarPanel:
    case ItemKind::SolarArray:
    case ItemKind::BatteryBank:       return theme::kTilePower;
    case ItemKind::SurveyScanner:
    case ItemKind::IonThruster:       return theme::kTileSystem;
    case ItemKind::Autocannon:
    case ItemKind::Railgun:           return theme::kTileTurret;
    default:                          return theme::kTileMachine;
    }
}

// A filled arrowhead centered on `c`, pointing along unit vector `d`, of
// half-length `h`.
void DrawArrow(ImDrawList* draw, ImVec2 c, ImVec2 d, float h, ImU32 color) {
    const ImVec2 p(-d.y, d.x);
    draw->AddTriangleFilled(ImVec2(c.x + d.x * h, c.y + d.y * h),
                            ImVec2(c.x - d.x * h * 0.5f + p.x * h * 0.8f, c.y - d.y * h * 0.5f + p.y * h * 0.8f),
                            ImVec2(c.x - d.x * h * 0.5f - p.x * h * 0.8f, c.y - d.y * h * 0.5f - p.y * h * 0.8f),
                            color);
}

}  // namespace

const char* DirectionName(Direction d) {
    switch (d) {
    case Direction::North: return "North";
    case Direction::East:  return "East";
    case Direction::South: return "South";
    case Direction::West:  return "West";
    }
    return "?";
}

ImVec2 CellMin(ImVec2 origin, float cell, GridPos p) {
    return {origin.x + static_cast<float>(p.x) * cell, origin.y + static_cast<float>(p.y) * cell};
}

ImVec2 FootprintMax(ImVec2 min, float cell, GridSize size) {
    return {min.x + static_cast<float>(size.w) * cell, min.y + static_cast<float>(size.h) * cell};
}

void DrawSprite(ImDrawList* draw, const sprites::Sprite& sprite, ImVec2 min, ImVec2 max,
                Direction facing, float alpha) {
    // The picture's corners, clockwise from its top left; each quarter turn
    // moves them one screen corner on.
    const ImVec2 uv[4] = {sprite.uv0, ImVec2(sprite.uv1.x, sprite.uv0.y), sprite.uv1, ImVec2(sprite.uv0.x, sprite.uv1.y)};
    const int turns = static_cast<int>(facing);
    const auto at = [&](int corner) { return uv[(corner - turns + 4) % 4]; };
    draw->AddImageQuad(sprite.tex, min, ImVec2(max.x, min.y), max, ImVec2(min.x, max.y), at(0), at(1), at(2), at(3),
                       theme::U32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), alpha));
}

void DrawBuilding(ImDrawList* draw, ImVec2 min, float cell, GridSize size,
                  std::optional<ItemKind> kind, Direction facing, float alpha) {
    const float inset = cell * 0.12f;
    const ImVec2 max = FootprintMax(min, cell, size);
    const ImVec2 a(min.x + inset, min.y + inset);
    const ImVec2 b(max.x - inset, max.y - inset);
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float rounding = cell * 0.04f;
    const ImU32 glyph = theme::U32(theme::kTileGlyph, alpha);
    const ImVec2 d = DirectionVector(facing);

    auto letter_tile = [&](ImVec4 color, const char* letter) {
        draw->AddRectFilled(a, b, theme::U32(color, alpha), rounding);
        const ImVec2 ts = ImGui::CalcTextSize(letter);
        if (ts.x < (b.x - a.x) * 0.9f && ts.y < (b.y - a.y) * 0.9f)
            draw->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), glyph, letter);
    };
    // A belt-like strip with an arrow on every cell of the footprint.
    auto conveyor = [&](ImVec4 color) {
        draw->AddRectFilled(a, b, theme::U32(color, alpha), rounding * 0.5f);
        for (int y = 0; y < size.h; ++y)
            for (int x = 0; x < size.w; ++x) {
                const ImVec2 cc(min.x + (static_cast<float>(x) + 0.5f) * cell,
                                min.y + (static_cast<float>(y) + 0.5f) * cell);
                DrawArrow(draw, cc, d, cell * 0.28f, glyph);
            }
    };

    const sprites::Sprite* sprite = kind ? sprites::Building(*kind) : sprites::Bridge();
    if (sprite) {
        DrawSprite(draw, *sprite, min, max, kind && is_rotatable(*kind) ? facing : Direction::North, alpha);
        return;
    }
    if (!kind) {
        letter_tile(theme::kTileBridge, "Bridge");
        return;
    }
    switch (*kind) {
    case ItemKind::TransportBelt:
        conveyor(theme::kTileBelt);
        break;
    case ItemKind::ExpressBelt:
        conveyor(theme::kTileExpressBelt);
        break;
    case ItemKind::BeltTunnel: {
        conveyor(theme::kTileBelt);
        // The tunnel mouth: a bar across the back.
        const ImVec2 back(c.x - d.x * cell * 0.3f, c.y - d.y * cell * 0.3f);
        const ImVec2 p(-d.y * cell * 0.38f, d.x * cell * 0.38f);
        draw->AddLine(ImVec2(back.x - p.x, back.y - p.y), ImVec2(back.x + p.x, back.y + p.y),
                      theme::U32(theme::kTileTunnelMouth, alpha), cell * 0.12f);
        break;
    }
    case ItemKind::FilterSorter:
    case ItemKind::ReverseSorter:
        // No arrow: it doesn't face a way. Its filter is drawn over it (see
        // DrawSorterFilter).
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding * 0.5f);
        draw->AddRect(a, b, theme::U32(*kind == ItemKind::ReverseSorter ? theme::kAccent : theme::kWarn, alpha),
                      rounding * 0.5f, 0, std::max(1.0f, cell * 0.08f));
        break;
    case ItemKind::Junction:
        // A cross: the two ways over it.
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding * 0.5f);
        draw->AddLine(ImVec2(c.x, a.y), ImVec2(c.x, b.y), glyph, std::max(1.0f, cell * 0.1f));
        draw->AddLine(ImVec2(a.x, c.y), ImVec2(b.x, c.y), glyph, std::max(1.0f, cell * 0.1f));
        break;
    case ItemKind::Router:
        // An arrow out of every side.
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding);
        for (const Direction dir : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const ImVec2 v = DirectionVector(dir);
            DrawArrow(draw, ImVec2(c.x + v.x * cell * 0.25f, c.y + v.y * cell * 0.25f), v, cell * 0.12f, glyph);
        }
        break;
    default:
        letter_tile(TileColor(*kind), TileLabel(*kind));
        break;
    }
}

void DrawBufferGauge(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, float fraction) {
    if (cell < 8.0f)
        return;  // too far out to read
    const float inset = cell * 0.2f;
    const float h = std::max(2.0f, cell * 0.08f);
    const ImVec2 a(min.x + inset, max.y - inset * 0.5f - h);
    const ImVec2 b(max.x - inset, max.y - inset * 0.5f);
    draw->AddRectFilled(a, b, theme::U32(theme::kTileGlyph, 0.5f));
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        draw->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * f, b.y),
                            theme::U32(f >= 1.0f ? theme::kWarn : theme::kTileGlyph));
}

namespace {

// The color an item is drawn in, by material family.
ImVec4 ItemColor(ItemKind kind) {
    switch (kind) {
    case ItemKind::IronOre:
    case ItemKind::IronPlate:
    case ItemKind::Steel:           return theme::kItemIron;
    case ItemKind::CopperOre:
    case ItemKind::CopperPlate:     return theme::kItemCopper;
    case ItemKind::Silica:
    case ItemKind::Glass:
    case ItemKind::Silicon:         return theme::kItemSilica;
    case ItemKind::Carbon:          return theme::kItemCarbon;
    case ItemKind::IridiumOre:
    case ItemKind::IridiumPlate:    return theme::kItemExotic;
    case ItemKind::Plastic:         return theme::kItemChemical;
    default:
        return item_category(kind) == ItemCategory::Placeable ? theme::kItemPlaceable : theme::kItemComponent;
    }
}

}  // namespace

void DrawItem(ImDrawList* draw, ImVec2 c, float r, ItemKind kind) {
    if (const sprites::Sprite* sprite = sprites::Item(kind)) {
        const float h = r * 1.3f;  // icons leave a margin around the item
        DrawSprite(draw, *sprite, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h));
        return;
    }
    const ImU32 fill = theme::U32(ItemColor(kind));
    const ImU32 outline = theme::U32(theme::kItemOutline, 0.7f);
    const ItemCategory category = item_category(kind);
    if (category == ItemCategory::RawResource) {
        draw->AddCircleFilled(c, r, fill);
        draw->AddCircle(c, r, outline);
    } else {
        draw->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fill);
        draw->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), outline);
    }
}

void DrawBeltItems(ImDrawList* draw, const Conveyor& belt, ImVec2 min, float cell) {
    const ImVec2 d = DirectionVector(belt.facing());
    const ImVec2 back(min.x + cell * 0.5f - d.x * cell * 0.5f, min.y + cell * 0.5f - d.y * cell * 0.5f);
    for (const BeltItem& item : belt.get_items()) {
        const float t = cell * static_cast<float>(item.pos) / static_cast<float>(kBeltLength);
        DrawItem(draw, ImVec2(back.x + d.x * t, back.y + d.y * t), std::max(1.5f, cell * 0.16f), item.kind);
    }
}

void DrawCornerItem(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, ItemKind kind) {
    if (cell < 8.0f)
        return;  // too far out to read
    DrawItem(draw, ImVec2(max.x - cell * 0.32f, min.y + cell * 0.32f), cell * 0.16f, kind);
}

void DrawMachineStatus(ImDrawList* draw, const Machine& machine, ImVec2 min, ImVec2 max, float cell) {
    const Recipe* recipe = machine.get_recipe();
    if (!recipe)
        return;
    if (!recipe->outputs.empty())
        DrawCornerItem(draw, min, max, cell, recipe->outputs.front().kind);
    if (machine.crafting())
        DrawBufferGauge(draw, min, max, cell, machine.craft_progress());
}

void DrawCollectorStatus(ImDrawList* draw, const Collector& collector, ImVec2 min, ImVec2 max, float cell) {
    DrawCornerItem(draw, min, max, cell, collector.get_target());
    DrawBufferGauge(draw, min, max, cell,
                    static_cast<float>(collector.stored()) / static_cast<float>(collector.capacity()));
}

void DrawJunctionItems(ImDrawList* draw, const Junction& junction, ImVec2 min, float cell) {
    for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West}) {
        const ImVec2 d = DirectionVector(travel);
        const ImVec2 back(min.x + cell * 0.5f - d.x * cell * 0.5f, min.y + cell * 0.5f - d.y * cell * 0.5f);
        for (const JunctionItem& item : junction.lane(travel)) {
            const float t = cell * static_cast<float>(item.elapsed) / static_cast<float>(kJunctionTicks);
            DrawItem(draw, ImVec2(back.x + d.x * t, back.y + d.y * t), std::max(1.5f, cell * 0.16f), item.kind);
        }
    }
}

void DrawRouterItem(ImDrawList* draw, const Router& router, ImVec2 min, float cell) {
    if (const std::optional<ItemKind> held = router.get_held())
        DrawItem(draw, ImVec2(min.x + cell * 0.5f, min.y + cell * 0.5f), std::max(1.5f, cell * 0.16f), *held);
}

void DrawSorterFilter(ImDrawList* draw, const Sorter& sorter, ImVec2 min, float cell) {
    if (const std::optional<ItemKind> filter = sorter.get_filter())
        DrawItem(draw, ImVec2(min.x + cell * 0.5f, min.y + cell * 0.5f), std::max(1.5f, cell * 0.2f), *filter);
}

// ---------------------------------------------------------------------------
// Icons in the panels
// ---------------------------------------------------------------------------

Icon ItemIcon(ItemKind kind) {
    Icon icon;
    icon.type = Icon::Type::Item;
    icon.item = kind;
    return icon;
}

Icon BuildingIcon(const Building& b) {
    Icon icon;
    icon.type = Icon::Type::Bridge;
    if (const std::optional<ItemKind> kind = b.item_kind()) {
        icon.type = Icon::Type::Building;
        icon.item = *kind;
    }
    return icon;
}

Icon RecipeIcon(const Recipe& r) {
    if (!r.name && !r.outputs.empty())
        return ItemIcon(r.outputs.front().kind);
    if (!r.inputs.empty())
        return ItemIcon(r.inputs.front().kind);
    if (!r.outputs.empty())
        return ItemIcon(r.outputs.front().kind);
    return {};
}

const char* IconName(const Icon& icon) {
    switch (icon.type) {
    case Icon::Type::None:     return "None";
    case Icon::Type::Item:
    case Icon::Type::Building: return item_name(icon.item);
    case Icon::Type::Bridge:   return "Bridge";
    }
    return "?";
}

const char* CategoryName(ItemCategory category) {
    switch (category) {
    case ItemCategory::Placeable:     return "Buildings";
    case ItemCategory::RawResource:   return "Raw resources";
    case ItemCategory::Material:      return "Materials";
    case ItemCategory::Component:     return "Components";
    }
    return "?";
}

namespace {

// `sprite` as large as fits in `min`..`max` without stretching, centered.
void DrawSpriteFitted(ImDrawList* draw, const sprites::Sprite& sprite, ImVec2 min, ImVec2 max, float alpha) {
    const float k = std::min((max.x - min.x) / static_cast<float>(sprite.w),
                             (max.y - min.y) / static_cast<float>(sprite.h));
    const ImVec2 size(static_cast<float>(sprite.w) * k, static_cast<float>(sprite.h) * k);
    const ImVec2 a((min.x + max.x - size.x) * 0.5f, (min.y + max.y - size.y) * 0.5f);
    DrawSprite(draw, sprite, a, ImVec2(a.x + size.x, a.y + size.y), Direction::North, alpha);
}

}  // namespace

void DrawIcon(ImDrawList* draw, const Icon& icon, ImVec2 min, ImVec2 max, float alpha) {
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float r = (max.x - min.x) * 0.5f;
    switch (icon.type) {
    case Icon::Type::None: {
        // A crossed-out circle.
        const ImU32 color = theme::U32(theme::kTextDim, alpha);
        const float t = std::max(1.5f, r * 0.12f);
        draw->AddCircle(c, r * 0.6f, color, 0, t);
        draw->AddLine(ImVec2(c.x - r * 0.42f, c.y + r * 0.42f), ImVec2(c.x + r * 0.42f, c.y - r * 0.42f), color, t);
        return;
    }
    case Icon::Type::Item:
        if (const sprites::Sprite* s = sprites::Item(icon.item))
            DrawSprite(draw, *s, min, max, Direction::North, alpha);
        else
            DrawItem(draw, c, r * 0.6f, icon.item);
        return;
    case Icon::Type::Building:
        if (const sprites::Sprite* s = sprites::Building(icon.item))
            DrawSpriteFitted(draw, *s, min, max, alpha);
        else
            DrawIcon(draw, ItemIcon(icon.item), min, max, alpha);
        return;
    case Icon::Type::Bridge:
        if (const sprites::Sprite* s = sprites::Bridge())
            DrawSpriteFitted(draw, *s, min, max, alpha);
        else
            draw->AddRectFilled(min, max, theme::U32(theme::kTileBridge, alpha), r * 0.3f);
        return;
    }
}

}  // namespace ui
