// ui_encyclopedia.cpp — the encyclopedia window (E, or the button beside
// the Shop): every item, building, tech, ship layout and enemy ship, to
// search and browse, each with what it is and where it sits in the supply
// chain: what makes it, what it goes into, what unlocks it, and which
// deliveries want it. Every icon and name in an entry links to its own entry
// (see ui_internal.h).
#include "ui/ui_internal.h"

#include "sim/encyclopedia.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

EncyclopediaUiState g_encyclopedia;

namespace {

using Entry = EncyclopediaEntry;
using EntryType = EncyclopediaEntry::Type;

Entry ItemEntry(ItemKind kind) { return {EntryType::Item, static_cast<int>(kind)}; }
Entry TechEntry(std::size_t t) { return {EntryType::Tech, static_cast<int>(t)}; }
Entry ShipEntry(std::size_t l) { return {EntryType::Ship, static_cast<int>(l)}; }
Entry EnemyEntry(EnemyClass c) { return {EntryType::Enemy, static_cast<int>(c)}; }

ItemKind EntryItem(const Entry& e) { return static_cast<ItemKind>(e.index); }
std::size_t EntryIndex(const Entry& e) { return static_cast<std::size_t>(e.index); }

// The entry a link was clicked to, this frame: gone to once the window is
// drawn, so the entry on show is drawn whole first.
std::optional<Entry> g_go_to;
// Links get a fresh ID each, so the same item twice in one entry is two links.
int g_link_id = 0;

const char* EntryName(const Entry& e) {
    switch (e.type) {
    case EntryType::Item:  return item_name(EntryItem(e));
    case EntryType::Tech:  return all_techs()[EntryIndex(e)].name;
    case EntryType::Ship:  return all_layouts()[EntryIndex(e)].name;
    case EntryType::Enemy: return enemy_design(static_cast<EnemyClass>(e.index)).name;
    }
    return "?";
}

// The tab an entry is listed under.
EncyclopediaUiState::Tab TabOf(const Entry& e) {
    switch (e.type) {
    case EntryType::Item:  return EncyclopediaUiState::Tab::Items;
    case EntryType::Tech:  return EncyclopediaUiState::Tab::Techs;
    case EntryType::Ship:  return EncyclopediaUiState::Tab::Ships;
    case EntryType::Enemy: return EncyclopediaUiState::Tab::Enemies;
    }
    return EncyclopediaUiState::Tab::Items;
}

// Every item in the encyclopedia, in ItemKind's order (so by category).
std::vector<ItemKind> AllItems() {
    std::vector<ItemKind> kinds;
    for (int i = 0; i < kItemKindCount; ++i)
        kinds.push_back(static_cast<ItemKind>(i));
    return kinds;
}

// Whether the fleet can make (or has found) `kind` yet: what's not is shown
// faded in lists.
bool Available(const Progress& progress, ItemKind kind) {
    return is_layout_part(kind) || progress.unlocked(kind);
}

void GoTo(const Entry& e) {
    g_go_to = e;
    PlayUi(audio::Sound::UiTabSwitch);
}

// An icon that links to `to`, `size` across, with `badges`; hovering it
// shows the entry's name.
void LinkIcon(const Entry& to, const Icon& icon, float size, Badges badges = {}, float fade = 1.0f) {
    ImGui::PushID(g_link_id++);
    if (IconButton("##link", icon, size, false, badges, fade))
        GoTo(to);
    ImGui::SetItemTooltip("%s", EntryName(to));
    ImGui::PopID();
}

void ItemLink(ItemKind kind, float size, const char* count = nullptr) {
    LinkIcon(ItemEntry(kind), ItemIcon(kind), size, {nullptr, count});
}

// A name that links to `to`, in the text's flow.
void TextLinkTo(const Entry& to, const char* label = nullptr) {
    ImGui::PushID(g_link_id++);
    if (ImGui::TextLink(label ? label : EntryName(to)))
        GoTo(to);
    ImGui::PopID();
}

// Links to `entries`, comma-separated, on the current line.
void TextLinks(const std::vector<Entry>& entries) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(",");
            ImGui::SameLine();
        }
        TextLinkTo(entries[i]);
    }
}

// "Label: " in dim text, on the line the next widget continues.
void Label(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::kTextDim, "%s", text);
    ImGui::SameLine();
}

// A rightward arrow, `size` tall, between a recipe's inputs and outputs.
void Arrow(float size) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float w = size * 0.6f;
    ImGui::Dummy(ImVec2(w, size));
    const float cx = min.x + w * 0.5f;
    const float cy = min.y + size * 0.5f;
    const float r = size * 0.18f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(cx - r * 1.4f, cy), ImVec2(cx + r * 0.2f, cy), theme::U32(theme::kTextDim), 2.0f);
    draw->AddTriangleFilled(ImVec2(cx, cy - r), ImVec2(cx + r * 1.2f, cy), ImVec2(cx, cy + r),
                            theme::U32(theme::kTextDim));
}

std::string Seconds(Tick ticks) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g s", static_cast<double>(ticks) / static_cast<double>(kTicksPerSecond));
    return buf;
}

// Why something isn't unlocked yet, with a link to what unlocks it; nothing
// once it is.
void LockedLine(const Progress& progress, ItemKind kind) {
    if (Available(progress, kind))
        return;
    if (is_weapon(kind)) {
        ImGui::TextColored(theme::kWarnText, "Not found yet");
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::kWarnText, "Locked: needs");
    ImGui::SameLine();
    if (const std::optional<ItemKind> w = weapon_unlocking(kind)) {
        TextLinkTo(ItemEntry(*w));
        ImGui::SameLine();
        ImGui::TextColored(theme::kWarnText, "found");
    } else if (const std::optional<std::size_t> t = tech_unlocking(kind)) {
        TextLinkTo(TechEntry(*t));
    }
}

// One recipe on a line: its inputs, an arrow, its outputs, each a link with
// its count; under it, how long it takes, where it's made, and whether it's
// locked (unless `lock`, false where the entry's header already says).
void RecipeRow(const Recipe& r, const Progress& progress, bool lock = true) {
    const float fh = ImGui::GetFrameHeight();
    const float icon = fh * 1.5f;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::PushID(g_link_id++);
    ImGui::BeginGroup();
    bool first = true;
    for (const ItemStack& s : r.inputs) {
        if (!first)
            ImGui::SameLine(0.0f, gap);
        first = false;
        ItemLink(s.kind, icon, std::to_string(s.count).c_str());
    }
    ImGui::SameLine(0.0f, gap);
    Arrow(icon);
    for (const ItemStack& s : r.outputs) {
        ImGui::SameLine(0.0f, gap);
        ItemLink(s.kind, icon, std::to_string(s.count).c_str());
    }
    std::string where = std::string(craft_category_name(r.category)) + ", " + Seconds(r.ticks) + ", in";
    Label(where.c_str());
    for (const ItemKind m : machines_for(r.category)) {
        ImGui::SameLine(0.0f, gap);
        ItemLink(m, fh);
    }
    if (r.hand_craftable) {
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "or by hand, at the bridge");
    }
    if (lock)
        LockedLine(progress, r.outputs.front().kind);
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::Spacing();
}

// The top of an entry: its picture (if it has one), its name in the title
// font, and a line of what it is under that.
void EntryHeader(const Icon* icon, const char* title, const std::string& subtitle) {
    const float fh = ImGui::GetFrameHeight();
    if (icon) {
        IconTile(*icon, fh * 3.0f);
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, theme::kFontTitle);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::TextColored(theme::kTextDim, "%s", subtitle.c_str());
}

void EndEntryHeader() {
    ImGui::EndGroup();
}

// What one item of `category` is: "Building", "Raw resource"...
const char* KindName(ItemCategory category) {
    switch (category) {
    case ItemCategory::Placeable:   return "Building";
    case ItemCategory::RawResource: return "Raw resource";
    case ItemCategory::Material:    return "Material";
    case ItemCategory::Component:   return "Component";
    }
    return "?";
}

const char* PortName(Port port) {
    return port == Port::Weapon ? "a weapon port" : "a shield port";
}

const char* WeaponFamilyName(WeaponFamily family) {
    switch (family) {
    case WeaponFamily::Laser:   return "Laser";
    case WeaponFamily::Missile: return "Missile";
    case WeaponFamily::Ion:     return "Ion";
    case WeaponFamily::Flak:    return "Flak";
    case WeaponFamily::Beam:    return "Beam";
    }
    return "?";
}

const char* TargetGroupName(TargetGroup group) {
    switch (group) {
    case TargetGroup::Weapons:  return "weapons";
    case TargetGroup::Shields:  return "shields";
    case TargetGroup::Engines:  return "engines";
    case TargetGroup::Bridge:   return "bridge";
    case TargetGroup::Industry: return "industry";
    }
    return "?";
}

// What a building does in numbers: its size and health, and what its kind
// adds (a machine's speed, a collector's pace, a belt's, a weapon's volley).
void BuildingStats(ItemKind kind) {
    const GridSize size = footprint_of(kind);
    ImGui::Text("Size %dx%d, health %d%s", size.w, size.h, max_hp_of(kind),
                is_rotatable(kind) ? ", turns (R)" : "");
    if (const std::optional<Port> port = port_of(kind))
        ImGui::Text("Goes only over %s", PortName(*port));
    if (machine_crafting_speed(kind) > 0) {
        std::string crafts;
        for (int c = 0; c < kCraftCategoryCount; ++c)
            if (machine_crafts(kind, static_cast<CraftCategory>(c)))
                crafts += (crafts.empty() ? "" : ", ") + std::string(craft_category_name(static_cast<CraftCategory>(c)));
        if (!crafts.empty())
            ImGui::Text("Runs %s recipes, at speed %d", crafts.c_str(), machine_crafting_speed(kind));
    }
    if (kind == ItemKind::AsteroidCollector || kind == ItemKind::TractorArray) {
        const CollectorSpec& spec = collector_spec(kind);
        ImGui::Text("Gathers %g a second, into a buffer of %d",
                    static_cast<double>(kTicksPerSecond) / static_cast<double>(spec.interval), spec.capacity);
        Label("Can be aimed at");
        for (const ItemKind t : spec.targets) {
            ItemLink(t, ImGui::GetFrameHeight());
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        }
        ImGui::NewLine();
    }
    if (const int speed = belt_speed(kind); speed > 0)
        ImGui::Text("Moves items %g cells a second", static_cast<double>(speed * static_cast<int>(kTicksPerSecond)) /
                                                         static_cast<double>(kBeltLength));
    if (is_weapon(kind)) {
        const WeaponSpec& spec = weapon_spec(kind);
        ImGui::Text("%s: %d shot%s of %d damage, every %s of charge", WeaponFamilyName(spec.family), spec.shots,
                    spec.shots == 1 ? "" : "s", spec.damage, Seconds(spec.charge).c_str());
        ImGui::TextColored(theme::kTextDim, "%s", spec.blurb);
        Label("Each volley burns");
        ItemLink(ItemKind::FuelCell, ImGui::GetFrameHeight());
        ImGui::SameLine();
        ItemLink(spec.ammo, ImGui::GetFrameHeight());
    }
}

// The deliveries asking for `kind`: how many, where, and how far along.
void DeliveryRows(const World& world, ItemKind kind) {
    const std::vector<std::size_t> wanting = deliveries_wanting(kind);
    if (wanting.empty())
        return;
    ImGui::SeparatorText("Deliveries wanting it");
    const Progress& progress = world.get_fleet().get_progress();
    const StarMap& map = world.get_map();
    for (const std::size_t d : wanting) {
        const Delivery& delivery = all_deliveries()[d];
        int count = 0;
        for (const ItemStack& s : delivery.wants)
            if (s.kind == kind)
                count = s.count;
        const int sector = map.delivery_sector(d);
        ImGui::Text("%s: %d", delivery.name, count);
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "to %s, +%d %s", sector >= 0 ? map.at(sector).name.c_str() : "?",
                           delivery_points(d), kPointsShort);
        ImGui::SameLine();
        if (progress.done(d))
            ImGui::TextColored(theme::kGood, "Done");
        else if (progress.open(d))
            ImGui::TextColored(theme::kAccent, "Open, %d in", progress.delivered(d, kind));
        else if (progress.chain_current(delivery.chain) == d)
            ImGui::TextColored(theme::kWarnText, "Waiting on a tech");
        else
            ImGui::TextColored(theme::kTextDim, "Later");
    }
}

void DrawItemEntry(const World& world, const FleetShip& shown, ItemKind kind) {
    const Progress& progress = world.get_fleet().get_progress();
    const float fh = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;

    std::string subtitle = is_weapon(kind) ? "Weapon" : KindName(item_category(kind));
    const int held = shown.ship.get_bridge().get_inventory().count(kind);
    if (!is_layout_part(kind))
        subtitle += ", " + Thousands(held) + " in the bridge";
    const Icon icon = ItemIcon(kind);
    EntryHeader(&icon, item_name(kind), subtitle);
    if (Available(progress, kind))
        ImGui::TextColored(theme::kGood, is_weapon(kind) ? "Found" : "Unlocked");
    else
        LockedLine(progress, kind);
    EndEntryHeader();

    ImGui::Spacing();
    ImGui::TextUnformatted(item_description(kind));

    if (is_building(kind)) {
        ImGui::SeparatorText("Stats");
        BuildingStats(kind);
    }

    // Where it comes from.
    ImGui::SeparatorText("Made by");
    const std::vector<ItemKind> collectors = collectors_gathering(kind);
    if (!collectors.empty()) {
        Label("Gathered from the sector by");
        for (const ItemKind c : collectors) {
            ItemLink(c, fh);
            ImGui::SameLine(0.0f, gap);
        }
        ImGui::NewLine();
        const Sector& here = world.get_map().at(shown.sector);
        ImGui::TextColored(here.left(kind) > 0 ? theme::kTextDim : theme::kWarnText, "%s left in %s",
                           Thousands(here.left(kind)).c_str(), here.name.c_str());
    }
    for (const Recipe* r : recipes_making(kind))
        RecipeRow(*r, progress, r->outputs.front().kind != kind);
    if (is_weapon(kind)) {
        ImGui::TextColored(theme::kTextDim, "Weapons can't be made: they are salvaged from beaten enemy ships.");
        std::vector<Entry> enemies;
        for (const EnemyClass c : enemies_carrying(kind))
            enemies.push_back(EnemyEntry(c));
        Label("Carried by");
        TextLinks(enemies);
    }
    if (is_layout_part(kind)) {
        Label("Comes with every ship's layout:");
        std::vector<Entry> ships;
        for (std::size_t l = 0; l < all_layouts().size(); ++l)
            ships.push_back(ShipEntry(l));
        TextLinks(ships);
    }

    // What it's wanted for: the deliveries first, the goals, then the
    // recipes.
    DeliveryRows(world, kind);
    ImGui::SeparatorText("Used in");
    bool used = false;
    for (const Recipe* r : recipes_using(kind)) {
        RecipeRow(*r, progress);
        used = true;
    }
    if (const std::vector<ItemKind> weapons = weapons_firing(kind); !weapons.empty()) {
        Label("Ammunition for");
        for (const ItemKind w : weapons) {
            ItemLink(w, fh);
            ImGui::SameLine(0.0f, gap);
        }
        ImGui::NewLine();
        used = true;
    }
    if (kind == ItemKind::FuelCell) {
        Label("Fuel for");
        ItemLink(ItemKind::IonThruster, fh);
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "(10 each, to jump) and every weapon (one a volley)");
        used = true;
    }
    if (const std::vector<std::size_t> layouts = layouts_costing(kind); !layouts.empty()) {
        Label("Building a ship:");
        std::vector<Entry> ships;
        for (const std::size_t l : layouts)
            ships.push_back(ShipEntry(l));
        TextLinks(ships);
        used = true;
    }
    if (!used)
        ImGui::TextColored(theme::kTextDim, is_building(kind) ? "Nothing: it's built on the hull." : "Nothing.");
}

void DrawTechEntry(const World& world, std::size_t t) {
    const Progress& progress = world.get_fleet().get_progress();
    const Tech& tech = all_techs()[t];
    const float fh = ImGui::GetFrameHeight();
    const std::string subtitle = "Tech T" + std::to_string(t + 1) + ", tier " + std::to_string(tech_tier(t)) + ", " +
                                 std::to_string(tech.cost) + " " + kPointsShort;
    const Icon icon = tech.unlocks.empty() ? Icon{Icon::Type::Bridge} : ItemIcon(tech.unlocks.front());
    EntryHeader(&icon, tech.name, subtitle);
    if (progress.bought(t))
        ImGui::TextColored(theme::kGood, "Owned");
    else if (progress.can_buy(t))
        ImGui::TextColored(theme::kAccent, "For sale: buy it in the Shop");
    else if (progress.for_sale(t))
        ImGui::TextColored(theme::kTextDim, "For sale, %d more %s needed", tech.cost - progress.points(), kPointsName);
    else
        ImGui::TextColored(theme::kWarnText, "Needs the techs before it first");
    EndEntryHeader();
    ImGui::Spacing();
    ImGui::TextUnformatted(tech.blurb);
    if (!progress.bought(t)) {
        if (ImGui::SmallButton("Open the Shop")) {
            g_fleet.show_shop = true;
            PlayUi(audio::Sound::UiPanelOpen);
        }
    }

    ImGui::SeparatorText("Unlocks");
    for (const ItemKind kind : tech.unlocks) {
        ItemLink(kind, fh * 1.5f);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(item_name(kind));
        ImGui::TextColored(theme::kTextDim, "%s", KindName(item_category(kind)));
        ImGui::EndGroup();
    }
    for (std::size_t l = 0; l < all_layouts().size(); ++l)
        if (all_layouts()[l].tech == t) {
            Label("The ship layout");
            TextLinkTo(ShipEntry(l));
        }

    ImGui::SeparatorText("In the tree");
    std::vector<Entry> before;
    for (const std::size_t p : tech_prerequisites(t))
        before.push_back(TechEntry(p));
    Label("Comes after");
    if (before.empty())
        ImGui::TextColored(theme::kTextDim, "nothing: open from the start");
    else
        TextLinks(before);
    std::vector<Entry> after;
    for (const std::size_t n : techs_after(t))
        after.push_back(TechEntry(n));
    Label("Leads to");
    if (after.empty())
        ImGui::TextColored(theme::kTextDim, "nothing: a capstone");
    else
        TextLinks(after);

    const std::vector<std::size_t> opens = deliveries_opened_by(t);
    if (!opens.empty()) {
        ImGui::SeparatorText("Deliveries waiting on it");
        for (const std::size_t d : opens) {
            ImGui::Text("%s", all_deliveries()[d].name);
            ImGui::SameLine();
            if (progress.done(d))
                ImGui::TextColored(theme::kGood, "Done");
            else
                ImGui::TextColored(theme::kTextDim, "+%d %s", delivery_points(d), kPointsShort);
        }
    }
}

// A ship layout drawn small: its hull, bridge, engines and ports.
void DrawLayoutPicture(const ShipLayout& layout) {
    const GridSize bounds = layout_bounds(layout);
    const float avail = ImGui::GetContentRegionAvail().x;
    // Small enough to fit across, and to keep the biggest from filling the
    // entry.
    const float cell = std::min({ImGui::GetFrameHeight() * 0.4f, avail / static_cast<float>(std::max(1, bounds.w)),
                                 ImGui::GetFrameHeight() * 8.0f / static_cast<float>(std::max(1, bounds.h))});
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(cell * static_cast<float>(bounds.w), cell * static_cast<float>(bounds.h)));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto at = [&](GridPos p) {
        return ImVec2(min.x + cell * static_cast<float>(p.x), min.y + cell * static_cast<float>(p.y));
    };
    const auto box = [&](GridPos p, GridSize s, ImVec4 color) {
        const ImVec2 a = at(p);
        draw->AddRectFilled(a, ImVec2(a.x + cell * static_cast<float>(s.w), a.y + cell * static_cast<float>(s.h)),
                            theme::U32(color));
    };
    for (const HullRect& r : layout.hull)
        box(r.at, r.size, theme::kTileHull);
    box(layout.bridge, {kBridgeSize, kBridgeSize}, theme::kTileBridge);
    for (const GridPos& e : layout.engines)
        box(e, footprint_of(ItemKind::IonThruster), theme::kTileSystem);
    for (const GridPos& p : layout.weapon_ports)
        box(p, {1, 1}, theme::kBad);
    for (const GridPos& p : layout.shield_ports)
        box(p, {1, 1}, theme::kTeal);
}

void DrawShipEntry(const World& world, std::size_t l) {
    const ShipLayout& layout = all_layouts()[l];
    const Progress& progress = world.get_fleet().get_progress();
    const float fh = ImGui::GetFrameHeight();
    const Icon icon{Icon::Type::Bridge};
    EntryHeader(&icon, layout.name, "Ship layout");
    if (layout_unlocked(layout, progress))
        ImGui::TextColored(theme::kGood, "Unlocked");
    else if (layout.tech) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::kWarnText, "Locked: needs");
        ImGui::SameLine();
        TextLinkTo(TechEntry(*layout.tech));
    }
    EndEntryHeader();
    ImGui::Spacing();
    ImGui::TextUnformatted(layout.description);
    ImGui::SeparatorText("Hull");
    const GridSize bounds = layout_bounds(layout);
    ImGui::Text("%d cells of hull, %dx%d; %zu engines", layout_hull_count(layout), bounds.w, bounds.h,
                layout.engines.size());
    ImGui::Text("%zu weapon ports, %zu shield ports", layout.weapon_ports.size(), layout.shield_ports.size());
    DrawLayoutPicture(layout);
    ImGui::TextColored(theme::kTextDim, "Bridge amber, engines red-brown, weapon ports red, shield ports teal");

    ImGui::SeparatorText("Cost");
    if (&layout == &starter_layout())
        ImGui::TextColored(theme::kTextDim, "A new game's flagship is one. Another costs:");
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    ItemLink(ItemKind::ShipStarterPack, fh * 1.5f, "1");
    for (const ItemStack& s : layout.cost) {
        ImGui::SameLine(0.0f, gap);
        ItemLink(s.kind, fh * 1.5f, ShortCount(s.count).c_str());
    }
    ImGui::TextColored(theme::kTextDim, "Built from the bridge: Fleet, Build a new ship.");
}

void DrawEnemyEntry(const World& world, EnemyClass c) {
    (void)world;
    const EnemyDesign& design = enemy_design(c);
    const float fh = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const std::string subtitle = "Enemy ship, threat " + std::to_string(design.min_threat) +
                                 (design.max_threat > design.min_threat ? " to " + std::to_string(design.max_threat)
                                                                        : std::string());
    const Icon icon{Icon::Type::Bridge};
    EntryHeader(&icon, design.name, subtitle);
    ImGui::TextColored(theme::kBad, "Bridge %d, %d shield layer%s", design.hull, design.shields,
                       design.shields == 1 ? "" : "s");
    EndEntryHeader();
    ImGui::Spacing();
    ImGui::TextUnformatted(design.description);

    ImGui::SeparatorText("Armed with");
    for (const ItemKind w : design.weapons) {
        ItemLink(w, fh * 1.5f);
        ImGui::SameLine(0.0f, gap);
    }
    ImGui::NewLine();
    std::string order;
    for (const TargetGroup g : design.priorities)
        order += (order.empty() ? "" : ", then ") + std::string(TargetGroupName(g));
    ImGui::TextColored(theme::kTextDim, "Shoots at your %s", order.c_str());

    ImGui::SeparatorText("Salvage");
    ImGui::TextColored(theme::kTextDim,
                       "Beaten, it leaves materials, fuel and ammunition, and some of its weapons; "
                       "%d%% of the time, also one from its hold the fleet hasn't found:",
                       static_cast<int>(std::lround(design.loot_chance * 100.0f)));
    for (const ItemKind w : design.loot) {
        ItemLink(w, fh * 1.5f);
        ImGui::SameLine(0.0f, gap);
    }
    ImGui::NewLine();
    if (const ShipLayout* layout = find_layout(design.layout)) {
        ImGui::SeparatorText("Hull");
        DrawLayoutPicture(*layout);
    }
}

// The left column's list: under the tabs, every entry of one kind; with a
// search, every entry of any kind whose name matches. Returns the first
// match (Enter goes to it).
std::optional<Entry> DrawEntryList(const Progress& progress) {
    EncyclopediaUiState& e = g_encyclopedia;
    const bool searching = e.search[0] != '\0';
    const auto shows = [&](const char* name) { return !searching || ContainsIgnoringCase(name, e.search); };
    std::optional<Entry> first;
    const auto note = [&](const Entry& entry) {
        if (!first)
            first = entry;
    };
    using Tab = EncyclopediaUiState::Tab;

    {
        if (searching || e.tab == Tab::Items) {
            const float icon = ImGui::GetFrameHeight() * 1.7f;
            for (const ItemCategory category : {ItemCategory::Placeable, ItemCategory::RawResource,
                                                ItemCategory::Material, ItemCategory::Component}) {
                std::vector<ItemKind> kinds;
                for (const ItemKind k : AllItems())
                    if (item_category(k) == category && shows(item_name(k)))
                        kinds.push_back(k);
                if (kinds.empty())
                    continue;
                ImGui::SeparatorText(CategoryName(category));
                IconFlow flow;
                flow.Start(icon);
                for (const ItemKind k : kinds) {
                    flow.Next();
                    note(ItemEntry(k));
                    ImGui::PushID(static_cast<int>(k));
                    const bool selected = e.entry == ItemEntry(k);
                    if (IconButton("##item", ItemIcon(k), icon, selected, {},
                                   Available(progress, k) ? 1.0f : 0.4f) &&
                        !selected)
                        GoTo(ItemEntry(k));
                    ImGui::SetItemTooltip("%s", item_name(k));
                    ImGui::PopID();
                }
            }
        }
    }
    // A list of names; `subheading_of` (when browsing) heads a new group
    // wherever it changes.
    const auto rows = [&](Tab tab, const char* heading, std::size_t n, auto entry_of, auto color_of,
                          auto subheading_of) {
        if (!searching && e.tab != tab)
            return;
        bool headed = false;
        std::string group;
        for (std::size_t i = 0; i < n; ++i) {
            const Entry entry = entry_of(i);
            if (!shows(EntryName(entry)))
                continue;
            if (!headed && searching)
                ImGui::SeparatorText(heading);
            headed = true;
            if (!searching && subheading_of(i) != group) {
                group = subheading_of(i);
                ImGui::SeparatorText(group.c_str());
            }
            note(entry);
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, color_of(i));
            const bool selected = e.entry == entry;
            if (ImGui::Selectable(EntryName(entry), selected) && !selected)
                GoTo(entry);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    };
    // Techs tier by tier, as the Shop lists them; owned ones dimmed.
    std::vector<std::size_t> techs(all_techs().size());
    for (std::size_t t = 0; t < techs.size(); ++t)
        techs[t] = t;
    std::stable_sort(techs.begin(), techs.end(), [](std::size_t a, std::size_t b) { return tech_tier(a) < tech_tier(b); });
    rows(Tab::Techs, "Techs", techs.size(), [&](std::size_t i) { return TechEntry(techs[i]); },
         [&](std::size_t i) { return progress.bought(techs[i]) ? theme::kTextDim : theme::kText; },
         [&](std::size_t i) { return "Tier " + std::to_string(tech_tier(techs[i])); });
    rows(Tab::Ships, "Ships", all_layouts().size(), [](std::size_t i) { return ShipEntry(i); },
         [&](std::size_t i) { return layout_unlocked(all_layouts()[i], progress) ? theme::kText : theme::kTextDim; },
         [](std::size_t) { return std::string(); });
    rows(Tab::Enemies, "Enemies", static_cast<std::size_t>(kEnemyClassCount),
         [](std::size_t i) { return EnemyEntry(static_cast<EnemyClass>(i)); },
         [](std::size_t) { return theme::kBad; }, [](std::size_t) { return std::string(); });
    if (searching && !first)
        ImGui::TextColored(theme::kTextDim, "No matches");
    return first;
}

}  // namespace

void OpenEncyclopedia(const EncyclopediaEntry& entry) {
    EncyclopediaUiState& e = g_encyclopedia;
    if (!e.shown)
        PlayUi(audio::Sound::UiPanelOpen);
    if (!(e.entry == entry)) {
        e.back.push_back(e.entry);
        e.forward.clear();
        e.entry = entry;
        e.scroll_to_top = true;
    }
    e.shown = true;
    e.tab = TabOf(entry);
}

void OpenEncyclopedia(ItemKind kind) {
    OpenEncyclopedia(ItemEntry(kind));
}

void OpenEncyclopediaTech(std::size_t tech) {
    OpenEncyclopedia(TechEntry(tech));
}

void ToggleEncyclopedia() {
    g_encyclopedia.shown = !g_encyclopedia.shown;
    if (g_encyclopedia.shown)
        g_encyclopedia.focus_search = true;
    PlayUi(g_encyclopedia.shown ? audio::Sound::UiPanelOpen : audio::Sound::UiPanelClose);
}

void LookUpOrToggleEncyclopedia(const Ship& ship) {
    const Building* b = g_build.hover_cell && !g_map.shown ? ship.building_at(*g_build.hover_cell) : nullptr;
    if (const std::optional<ItemKind> kind = b ? b->item_kind() : std::nullopt)
        OpenEncyclopedia(*kind);
    else
        ToggleEncyclopedia();
}

void DrawEncyclopediaWindow(const World& world, const FleetShip& shown) {
    EncyclopediaUiState& e = g_encyclopedia;
    if (!e.shown)
        return;
    g_link_id = 0;
    g_go_to.reset();
    const float fh = ImGui::GetFrameHeight();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(fh * 40.0f, fh * 26.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(fh * 30.0f, fh * 14.0f),
                                        ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.9f));
    bool open = true;
    const bool visible = ImGui::Begin("Encyclopedia", &open, ImGuiWindowFlags_NoCollapse);
    if (!open) {
        e.shown = false;
        PlayUi(audio::Sound::UiPanelClose);
    }
    if (!visible || !open) {
        ImGui::End();
        return;
    }
    const Progress& progress = world.get_fleet().get_progress();

    // The top row: back and forward through the entries seen, and the search.
    ImGui::BeginDisabled(e.back.empty());
    const bool back = ImGui::ArrowButton("##back", ImGuiDir_Left);
    ImGui::SetItemTooltip("Back (mouse back button)");
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::BeginDisabled(e.forward.empty());
    const bool forward = ImGui::ArrowButton("##forward", ImGuiDir_Right);
    ImGui::SetItemTooltip("Forward");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (e.focus_search) {
        ImGui::SetKeyboardFocusHere();
        e.focus_search = false;
    }
    ImGui::SetNextItemWidth(fh * 12.0f);
    const bool enter = ImGui::InputTextWithHint("##search", "Search everything", e.search, sizeof e.search,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (e.search[0] != '\0') {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::SmallButton("Clear"))
            e.search[0] = '\0';
    }
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // The left column: the tabs and the list; the right: the entry.
    const float list_w = fh * 12.5f;
    ImGui::BeginChild("##list", ImVec2(list_w, 0.0f), ImGuiChildFlags_Borders);
    if (e.search[0] == '\0') {
        using Tab = EncyclopediaUiState::Tab;
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const struct {
            Tab tab;
            const char* label;
        } tabs[] = {{Tab::Items, "Items"}, {Tab::Techs, "Techs"}, {Tab::Ships, "Ships"}, {Tab::Enemies, "Enemies"}};
        for (int i = 0; i < 4; ++i) {
            if (i % 2 == 1)
                ImGui::SameLine();
            if (theme::SegmentedButton(tabs[i].label, e.tab == tabs[i].tab, ImVec2(half, 0.0f)) &&
                e.tab != tabs[i].tab) {
                e.tab = tabs[i].tab;
                PlayUi(audio::Sound::UiTabSwitch);
            }
        }
    }
    const std::optional<Entry> first = DrawEntryList(progress);
    ImGui::EndChild();
    if (enter && first)
        GoTo(*first);

    ImGui::SameLine();
    ImGui::BeginChild("##entry", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (e.scroll_to_top) {
        ImGui::SetScrollY(0.0f);
        e.scroll_to_top = false;
    }
    ImGui::PushTextWrapPos(0.0f);
    switch (e.entry.type) {
    case EntryType::Item:  DrawItemEntry(world, shown, EntryItem(e.entry)); break;
    case EntryType::Tech:  DrawTechEntry(world, EntryIndex(e.entry)); break;
    case EntryType::Ship:  DrawShipEntry(world, EntryIndex(e.entry)); break;
    case EntryType::Enemy: DrawEnemyEntry(world, static_cast<EnemyClass>(e.entry.index)); break;
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::End();

    if (g_go_to) {
        OpenEncyclopedia(*g_go_to);
        // Enter took the first match: back to browsing.
        if (enter)
            e.search[0] = '\0';
    } else if ((back || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton(3)))) && !e.back.empty()) {
        e.forward.push_back(e.entry);
        e.entry = e.back.back();
        e.back.pop_back();
        e.tab = TabOf(e.entry);
        e.scroll_to_top = true;
        PlayUi(audio::Sound::UiTabSwitch);
    } else if (forward && !e.forward.empty()) {
        e.back.push_back(e.entry);
        e.entry = e.forward.back();
        e.forward.pop_back();
        e.tab = TabOf(e.entry);
        e.scroll_to_top = true;
        PlayUi(audio::Sound::UiTabSwitch);
    }
}

}  // namespace ui
