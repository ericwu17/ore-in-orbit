// ui_panels.cpp — the game screen's side panels: the build palette (left)
// and the craft panel (right), with the milestone and hand-crafting; and the
// inspector window (see ui_internal.h).
#include "ui/ui_internal.h"

#include <algorithm>
#include <cstdio>

namespace ui {

namespace {

void RecipeDetails(const Recipe& recipe, const Inventory* inv);

// Copying and pasting, in the left panel: what's on the clipboard, the
// tools, and while pasting, what the blueprint costs.
void DrawBlueprintSection(const Ship& ship) {
    if (!theme::CollapsingSection("Blueprints"))
        return;
    BlueprintUiState& bp = g_blueprints;
    if (bp.clipboard)
        ImGui::Text("Clipboard: %s", BlueprintSummary(*bp.clipboard).c_str());
    else
        ImGui::TextColored(theme::kTextDim, "Clipboard empty");

    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (theme::SegmentedButton("Copy", bp.tool == Tool::Copy, ImVec2(third, 0.0f))) {
        if (bp.tool == Tool::Copy) {
            LeaveBlueprintTool();
        } else {
            LeaveBlueprintTool();
            bp.tool = Tool::Copy;
        }
    }
    ImGui::SetItemTooltip("Drag a box over what to copy (%s+C)", kCtrlName);
    ImGui::SameLine();
    ImGui::BeginDisabled(!bp.clipboard && bp.tool != Tool::Paste);
    if (theme::SegmentedButton("Paste", bp.tool == Tool::Paste, ImVec2(third, 0.0f))) {
        if (bp.tool == Tool::Paste)
            LeaveBlueprintTool();
        else
            HoldBlueprint(*bp.clipboard);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Paste the clipboard (%s+V)", kCtrlName);
    ImGui::SameLine();
    if (theme::SegmentedButton("Library", bp.show_library, ImVec2(third, 0.0f)))
        bp.show_library = !bp.show_library;
    ImGui::SetItemTooltip("Saved blueprints, import and export (B)");

    if (bp.tool == Tool::Paste && bp.held) {
        ImGui::TextUnformatted(bp.held->name.empty() ? "Pasting the clipboard" : bp.held->name.c_str());
        // What it costs on bare space, marked where the bridge is short.
        const float icon = ImGui::GetFrameHeight() * 1.4f;
        IconFlow flow;
        flow.Start(icon);
        for (const ItemStack& s : BlueprintCost(*bp.held).stacks()) {
            const int have = ship.inventory().count(s.kind);
            const std::string amount = ShortCount(s.count);
            flow.Next();
            ImGui::PushID(static_cast<int>(s.kind));
            IconTile(ItemIcon(s.kind), icon, {have < s.count ? "!" : nullptr, amount.c_str()});
            ImGui::SetItemTooltip("%s: %d (%d in the bridge)", item_name(s.kind), s.count, have);
            ImGui::PopID();
        }
    }
}

}  // namespace

void DrawBuildPalette(Ship& ship, float width) {
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawPlatePanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y));

    ImGui::BeginChild("##build_palette", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::SectionHeading("Build");

    Bridge& bridge = ship.get_bridge();
    const float icon = ImGui::GetFrameHeight() * 1.9f;
    const std::vector<ItemKind> list = BuildList(ship);
    IconFlow flow;
    flow.Start(icon);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const ItemKind kind = list[i];
        const int count = ship.inventory().count(kind);
        const std::string key = std::to_string(i + 1);
        const std::string amount = ShortCount(count);
        flow.Next();
        ImGui::PushID(static_cast<int>(kind));
        if (IconButton("##build", ItemIcon(kind), icon, g_build.selected == kind,
                       {i < 9 ? key.c_str() : nullptr, amount.c_str()}, count == 0 ? 0.4f : 1.0f))
            ToggleSelected(kind);
        const Recipe* hand = HandRecipeFor(kind);
        if (hand && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const int n = ImGui::GetIO().KeyShift ? 5 : 1;
            int queued = 0;
            while (queued < n && bridge.queue_craft(*hand))
                ++queued;
            if (queued == 0)
                SetMessage(std::string("Can't hand-craft ") + item_name(kind) + " now: see its recipe");
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(item_name(kind));
            const GridSize footprint = footprint_of(kind);
            ImGui::TextColored(theme::kTextDim, "%d in the bridge, %dx%d", count, footprint.w, footprint.h);
            if (i < 9)
                ImGui::TextColored(theme::kTextDim, "Key %s", key.c_str());
            if (hand) {
                ImGui::TextColored(theme::kTextDim, "Right click: hand-craft 1 (Shift: 5)");
                RecipeDetails(*hand, &bridge.get_inventory());
            } else {
                ImGui::TextColored(theme::kTextDim, "Made in machines");
            }
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (g_build.selected) {
        ImGui::TextUnformatted(item_name(*g_build.selected));
        if (is_rotatable(*g_build.selected)) {
            ImGui::SameLine();
            ImGui::TextColored(theme::kTextDim, "facing %s", DirectionName(g_build.facing));
        }
        const auto last = g_settings.last.find(static_cast<int>(*g_build.selected));
        if (last != g_settings.last.end())
            if (const char* name = SettingsName(last->second)) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "New ones:");
                ImGui::SameLine();
                const BuildingSettings& s = last->second;
                const Icon setting = s.recipe ? RecipeIcon(*s.recipe) : ItemIcon(s.target ? *s.target : *s.filter);
                AmountLine(setting, name, theme::kText, false);
            }
    } else {
        ImGui::TextColored(theme::kTextDim, "Nothing selected: clicks inspect");
    }

    DrawBlueprintSection(ship);

    if (theme::CollapsingSection("Cargo")) {
        const float cargo_icon = ImGui::GetFrameHeight() * 1.6f;
        IconFlow cargo;
        cargo.Start(cargo_icon);
        bool any = false;
        for (int i = 0; i < kItemKindCount; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            const int count = ship.inventory().count(kind);
            if (is_placeable(kind) || count == 0)
                continue;
            any = true;
            const std::string amount = ShortCount(count);
            cargo.Next();
            IconTile(ItemIcon(kind), cargo_icon, {nullptr, amount.c_str()});
            ImGui::SetItemTooltip("%s: %d", item_name(kind), count);
        }
        if (!any)
            ImGui::TextColored(theme::kTextDim, "Empty");
    }
    ImGui::EndChild();
}

namespace {

// What goes into a recipe and what it makes, item by item, for its tooltip.
// `inv` colors inputs by whether the bridge has them; null leaves them plain.
void RecipeDetails(const Recipe& recipe, const Inventory* inv) {
    ImGui::TextColored(theme::kTextDim, "%s, %.1f s", craft_category_name(recipe.category),
                       static_cast<double>(recipe.ticks) / static_cast<double>(kTicksPerSecond));
    for (const ItemStack& s : recipe.inputs) {
        if (inv) {
            const int have = inv->count(s.kind);
            const std::string suffix = "  (have " + std::to_string(have) + ")";
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), have >= s.count ? theme::kText : theme::kBad, true,
                       suffix.c_str());
        } else {
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, true);
        }
    }
    ImGui::TextColored(theme::kTextDim, "Makes");
    for (const ItemStack& s : recipe.outputs)
        AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kGood, true);
}

// A recipe's tooltip, over the item just drawn: its name and details.
void RecipeTooltip(const Recipe& recipe, const Inventory* inv, const char* hint = nullptr) {
    if (!ImGui::BeginItemTooltip())
        return;
    ImGui::TextUnformatted(recipe_name(recipe));
    RecipeDetails(recipe, inv);
    if (hint)
        ImGui::TextColored(theme::kTextDim, "%s", hint);
    ImGui::EndTooltip();
}

// Which milestone unlocks a recipe not unlocked yet ("Unlocks with
// milestone 2: Heavy industry"), for its tooltip; empty once it is.
std::string LockedHint(const Progress& progress, const Recipe& recipe) {
    if (progress.unlocked(recipe))
        return {};
    if (const std::optional<ItemKind> w = weapon_unlocking(recipe.outputs.front().kind))
        return std::string("Unlocks once the fleet finds a ") + item_name(*w) +
               ": salvage one from a beaten enemy ship";
    const std::size_t i = *milestone_unlocking(recipe.outputs.front().kind);
    return "Unlocks with milestone " + std::to_string(i + 1) + ": " + all_milestones()[i].name;
}

// The corner badge on a locked recipe's icon ("M2"); empty once unlocked.
std::string LockedBadge(const Progress& progress, const Recipe& recipe) {
    if (progress.unlocked(recipe))
        return {};
    if (weapon_unlocking(recipe.outputs.front().kind))
        return "W";
    return "M" + std::to_string(*milestone_unlocking(recipe.outputs.front().kind) + 1);
}

// One icon button per hand-craftable recipe whose main output is in
// `category`; the locked ones faded, with the milestone that unlocks them.
void DrawHandCraftButtons(Bridge& bridge, ItemCategory category) {
    const float size = ImGui::GetFrameHeight() * 1.9f;
    IconFlow flow;
    flow.Start(size);
    const std::vector<Recipe>& recipes = all_recipes();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        const Recipe& r = recipes[i];
        if (!r.hand_craftable || item_category(r.outputs.front().kind) != category)
            continue;
        const bool craftable = bridge.can_hand_craft(r);
        const int makes = r.outputs.front().count;
        const std::string badge = "x" + std::to_string(makes);
        const std::string locked = LockedBadge(bridge.get_progress(), r);
        flow.Next();
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!craftable);
        if (IconButton("##craft", RecipeIcon(r), size, false,
                       {locked.empty() ? nullptr : locked.c_str(), makes > 1 ? badge.c_str() : nullptr},
                       locked.empty() ? 1.0f : 0.35f)) {
            const int n = ImGui::GetIO().KeyShift ? 5 : 1;
            for (int k = 0; k < n && bridge.queue_craft(r); ++k) {
            }
        }
        ImGui::EndDisabled();
        const std::string hint = LockedHint(bridge.get_progress(), r);
        RecipeTooltip(r, &bridge.get_inventory(), hint.empty() ? nullptr : hint.c_str());
        ImGui::PopID();
    }
}

}  // namespace

void BuildingDetails(const Building& b, bool in_tooltip) {
    const std::optional<ItemKind> kind = b.item_kind();
    // Its picture, its name beside it, and its health (the inspector window
    // has its own heading with them).
    if (in_tooltip) {
        const float picture = ImGui::GetFrameHeight() * 1.6f;
        const ImVec2 picture_min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(picture, picture));
        DrawIcon(ImGui::GetWindowDrawList(), BuildingIcon(b), picture_min,
                 ImVec2(picture_min.x + picture, picture_min.y + picture));
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (picture - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextUnformatted(kind ? item_name(*kind) : "Bridge");
        ImGui::TextColored(b.damaged() ? theme::kBad : theme::kTextDim, "Health %d / %d", b.get_hp(), b.max_hp());
    }

    // Across the inspector window's column; a set width in the tooltip,
    // which fits itself to what's in it.
    const float bar_w = in_tooltip ? ImGui::GetFrameHeight() * 8.0f : -FLT_MIN;
    if (const auto* machine = dynamic_cast<const Machine*>(&b)) {
        std::string crafts;
        for (int c = 0; c < kCraftCategoryCount; ++c)
            if (machine->crafts(static_cast<CraftCategory>(c)))
                crafts += std::string(crafts.empty() ? "" : ", ") + craft_category_name(static_cast<CraftCategory>(c));
        ImGui::TextColored(theme::kTextDim, "%s, speed x%d", crafts.c_str(), machine->crafting_speed());
        const Recipe* recipe = machine->get_recipe();
        if (!recipe) {
            ImGui::TextColored(theme::kWarnText, "No recipe: click it to pick one");
        } else {
            char rate[32];
            std::snprintf(rate, sizeof(rate), "  (%.1f / min)", machine->crafts_per_minute());
            AmountLine(RecipeIcon(*recipe), recipe_name(*recipe), theme::kText, false, rate);
            ImGui::ProgressBar(machine->craft_progress(), ImVec2(bar_w, 0.0f), machine->crafting() ? "" : "Waiting");
            ImGui::TextColored(theme::kTextDim, "Ingredients (per craft)");
            for (const ItemStack& s : recipe->inputs) {
                const int have = machine->get_input().count(s.kind);
                AmountLine(ItemIcon(s.kind), Fraction(have, s.count), have >= s.count ? theme::kText : theme::kTextDim,
                           in_tooltip);
            }
            ImGui::TextColored(theme::kTextDim, "Waiting to go out");
            bool any = false;
            for (const ItemStack& s : machine->get_output().stacks()) {
                AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kGood, in_tooltip);
                any = true;
            }
            if (!any)
                ImGui::TextColored(theme::kTextDim, "  -");
        }
    }
    if (const auto* collector = dynamic_cast<const Collector*>(&b)) {
        const CollectorSpec& spec = collector->get_spec();
        const double seconds = static_cast<double>(spec.interval) / static_cast<double>(kTicksPerSecond);
        if (collector->get_yield() == 0) {
            ImGui::TextColored(theme::kWarnText, "No %s in this sector: aim it at something else",
                               item_name(collector->get_target()));
        } else {
            ImGui::TextColored(theme::kTextDim, "Gathers %s: %.0f / min (%d%% of home)",
                               item_name(collector->get_target()), collector->rate_per_second() * 60.0,
                               collector->get_yield());
            ImGui::TextColored(theme::kTextDim, "At home: 1 every %.1f s", seconds);
        }
        const bool full = collector->full();
        ImGui::ProgressBar(full ? 1.0f : collector->catch_progress(), ImVec2(bar_w, 0.0f), full ? "Buffer full" : "");
        ImGui::Text("Buffer: %d / %d", collector->stored(), collector->capacity());
        for (const ItemStack& s : collector->items().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* belt = dynamic_cast<const Conveyor*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "%.0f cells / s",
                           static_cast<double>(belt->speed() * static_cast<int>(kTicksPerSecond)) / kBeltLength);
        if (const auto* tunnel = dynamic_cast<const Tunnel*>(belt)) {
            const int cells = tunnel->get_distance();
            if (tunnel->is_exit())
                ImGui::TextColored(theme::kTextDim, "Exit: items come up from the entrance %d cell%s back", cells,
                                   cells == 1 ? "" : "s");
            else if (tunnel->linked())
                ImGui::TextColored(theme::kTextDim, "Entrance: items go under to the exit %d cell%s ahead", cells,
                                   cells == 1 ? "" : "s");
            else
                ImGui::TextColored(theme::kWarnText,
                                   "No exit: place a belt tunnel facing the same way, up to %d cells ahead",
                                   kTunnelRange);
        }
        Inventory carried;
        for (const BeltItem& item : belt->get_items())
            carried.add(item.kind);
        for (const ItemStack& s : carried.stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* unloader = dynamic_cast<const Unloader*>(&b)) {
        const std::optional<ItemKind> filter = unloader->get_filter();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        AmountLine(filter ? ItemIcon(*filter) : Icon{}, filter ? item_name(*filter) : "none", theme::kText, false);
        ImGui::TextColored(theme::kTextDim,
                           "Takes %s out of the bridge or cargo hold behind it, onto what's in front: "
                           "up to %d a second",
                           filter ? item_name(*filter) : "every item, in turn,",
                           static_cast<int>(kTicksPerSecond / kUnloaderTicks));
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
        const std::optional<ItemKind> filter = sorter->get_filter();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        AmountLine(filter ? ItemIcon(*filter) : Icon{}, filter ? item_name(*filter) : "none", theme::kText, false);
        if (sorter->inverted())
            ImGui::TextColored(theme::kTextDim, "Matching items turn left or right; others go straight through");
        else
            ImGui::TextColored(theme::kTextDim, "Matching items go straight through; others turn left or right");
    }
    if (const auto* junction = dynamic_cast<const Junction*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "Items cross straight over, out the opposite side");
        Inventory crossing;
        for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West})
            for (const JunctionItem& item : junction->lane(travel))
                crossing.add(item.kind);
        for (const ItemStack& s : crossing.stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* router = dynamic_cast<const Router*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "Passes items to its other three sides in turn");
        if (const std::optional<ItemKind> held = router->get_held())
            AmountLine(ItemIcon(*held), item_name(*held), theme::kText, in_tooltip);
    }
    if (const auto* thruster = dynamic_cast<const Thruster*>(&b)) {
        const std::string fuel = Fraction(thruster->get_fuel(), kThrusterCharge) + " fuel cells";
        ImGui::ProgressBar(static_cast<float>(thruster->get_fuel()) / static_cast<float>(kThrusterCharge),
                           ImVec2(bar_w, 0.0f), fuel.c_str());
        if (thruster->offline())
            ImGui::TextColored(theme::kBad, "Knocked offline: it takes no fuel until it mends");
        else
            ImGui::TextColored(thruster->charged() ? theme::kGood : theme::kTextDim, "%s",
                               thruster->charged() ? "Charged: ready to jump (see the map, M)"
                                                   : "Belt fuel cells into it to charge it");
        ImGui::TextColored(theme::kTextDim, "Part of the ship's layout: it can't be removed");
    }
    if (kind == ItemKind::SurveyScanner)
        ImGui::TextColored(theme::kTextDim, "Surveys the sectors next to the ship's");
    if (const auto* weapon = dynamic_cast<const Weapon*>(&b)) {
        const WeaponSpec& spec = weapon->get_spec();
        ImGui::TextColored(theme::kTextDim, "%s", spec.blurb);
        if (spec.family == WeaponFamily::Beam)
            ImGui::TextColored(theme::kTextDim, "%d damage to each building it crosses, every %.0f s", spec.damage,
                               static_cast<double>(spec.charge) / static_cast<double>(kTicksPerSecond));
        else
            ImGui::TextColored(theme::kTextDim, "%d x %d damage, every %.0f s", spec.shots, spec.damage,
                               static_cast<double>(spec.charge) / static_cast<double>(kTicksPerSecond));
        AmountLine(ItemIcon(ItemKind::FuelCell), Fraction(weapon->get_fuel(), kWeaponFuelCapacity),
                   weapon->get_fuel() > 0 ? theme::kText : theme::kBad, in_tooltip, " power");
        AmountLine(ItemIcon(spec.ammo), Fraction(weapon->get_ammo(), kWeaponAmmoCapacity),
                   weapon->get_ammo() > 0 ? theme::kText : theme::kBad, in_tooltip, " ammo");
        const std::string charge = std::to_string(static_cast<int>(weapon->charge_fraction() * 100.0f)) + "% charged";
        ImGui::ProgressBar(weapon->charge_fraction(), ImVec2(bar_w, 0.0f), charge.c_str());
        if (weapon->get_stun() > 0)
            ImGui::TextColored(theme::kBad, "Stunned by an ion hit: %d s", static_cast<int>((weapon->get_stun() + kTicksPerSecond - 1) / kTicksPerSecond));
        else if (weapon->get_fuel() == 0)
            ImGui::TextColored(theme::kBad, "No power: belt fuel cells into it");
        else if (weapon->get_ammo() == 0)
            ImGui::TextColored(theme::kBad, "No ammunition: belt %s into it", item_name(spec.ammo));
        else
            ImGui::TextColored(theme::kTextDim, "Each volley burns a fuel cell and a round. It charges and fires "
                                                "while an enemy ship is in the sector.");
    }
    if (const auto* shield = dynamic_cast<const ShieldProjector*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "One shield layer round the ship: it stops a laser shot or flak "
                                            "pellet, then recharges. Missiles go through.");
        if (shield->get_stun() > 0)
            ImGui::TextColored(theme::kBad, "Stunned by an ion hit: %d s", static_cast<int>((shield->get_stun() + kTicksPerSecond - 1) / kTicksPerSecond));
        else if (shield->up())
            ImGui::TextColored(theme::kGood, "Layer up");
        else
            ImGui::TextColored(theme::kWarnText, "Recharging: %d s", static_cast<int>((shield->get_recharge() + kTicksPerSecond - 1) / kTicksPerSecond));
    }
    if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
        ImGui::Text("Stored: %d / %d", hold->get_stored().total(), kCargoCapacity);
        for (const ItemStack& s : hold->get_stored().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    const auto* producer_collector = dynamic_cast<const Collector*>(&b);
    const auto* producer_machine = dynamic_cast<const Machine*>(&b);
    if ((producer_collector && producer_collector->on_standby()) ||
        (producer_machine && producer_machine->on_standby()))
        ImGui::TextColored(theme::kTextDim, "On standby: the bridge has what it requests");
    if (!kind)
        ImGui::TextColored(theme::kTextDim, "Belts deliver into its inventory");
    if (in_tooltip && b.rotatable())
        ImGui::TextColored(theme::kTextDim, "Facing %s", DirectionName(b.facing()));
}

namespace {

// What a new request asks for.
constexpr int kNewRequest = 100;

// The bridge's item requests, in its inspector: one row per item asked for,
// with its count to change and how many are on their way (from providers,
// or sent), and a picker to ask for another.
void DrawRequests(Bridge& bridge, const Fleet& fleet, int ship_id) {
    ImGui::SeparatorText("Requests");
    ImGui::TextColored(theme::kTextDim,
                       "How many of each item to keep in the bridge. Machines and collectors set to run only on "
                       "request stop once it has them, and provider ships in this sector send what's missing.");
    // A copy: a row can remove its request.
    const std::vector<ItemStack> requests = bridge.get_requests();
    if (requests.empty())
        ImGui::TextColored(theme::kTextDim, "None");
    for (const ItemStack& r : requests) {
        ImGui::PushID(static_cast<int>(r.kind));
        ImGui::AlignTextToFramePadding();
        IconImage(ItemIcon(r.kind), ImGui::GetFrameHeight());
        ImGui::SetItemTooltip("%s", item_name(r.kind));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        int count = r.count;
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 5.0f);
        if (ImGui::InputInt("##count", &count, 1, 10))
            bridge.set_request(r.kind, std::clamp(count, 0, kMaxRequest));
        ImGui::SetItemTooltip("How many %s to keep in the bridge", item_name(r.kind));
        const int have = bridge.get_inventory().count(r.kind);
        ImGui::SameLine();
        ImGui::TextColored(have >= r.count ? theme::kGood : theme::kTextDim, "have %s", ShortCount(have).c_str());
        if (const int incoming = fleet.in_flight_to(ship_id, r.kind); incoming > 0) {
            ImGui::SameLine();
            ImGui::TextColored(theme::kTextDim, "+%s incoming", ShortCount(incoming).c_str());
            ImGui::SetItemTooltip("%d %s on their way in cargo pods", incoming, item_name(r.kind));
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
        if (theme::CloseButton("##remove"))
            bridge.remove_request(r.kind);
        ImGui::SetItemTooltip("Stop requesting %s", item_name(r.kind));
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (BeginIconCombo("##add_request", Icon{}, "Request an item")) {
        BeginComboSearch();
        if (const std::optional<ItemKind> picked = ComboSearchItemOptions(std::nullopt))
            if (!bridge.request_for(*picked))
                bridge.set_request(*picked, kNewRequest);
        EndComboSearch();
        ImGui::EndCombo();
    }
}

// For a machine or collector, in its inspector: whether it runs only while
// the bridge requests what it makes, and how that request stands. Not one
// of the settings new ones start with: they always run.
void DrawOnRequest(Ship& ship, const Building& b) {
    const auto* collector = dynamic_cast<const Collector*>(&b);
    const auto* machine = dynamic_cast<const Machine*>(&b);
    if (!collector && !machine)
        return;
    bool on = collector ? collector->get_only_when_requested() : machine->get_only_when_requested();
    if (ImGui::Checkbox("Only while the bridge requests it", &on))
        ship.set_only_when_requested(*g_build.inspected, on);
    ImGui::SetItemTooltip("Runs only while the bridge has fewer of what it makes than\nit requests (click the bridge to request items)");
    const std::optional<ItemKind> product = product_of(b);
    if (!on || !product)
        return;
    const Bridge& bridge = ship.get_bridge();
    const std::optional<int> wanted = bridge.request_for(*product);
    if (!wanted) {
        ImGui::TextColored(theme::kWarnText, "The bridge requests no %s: it stays off (click the bridge to request some)",
                           item_name(*product));
        return;
    }
    const int have = bridge.get_inventory().count(*product);
    AmountLine(ItemIcon(*product), Fraction(have, *wanted), have >= *wanted ? theme::kGood : theme::kText, false,
               have >= *wanted ? "  in the bridge: on standby" : "  in the bridge: running");
}

// Whether the inspector window has a settings column for `b`: the bridge
// (its requests), a machine, a collector, or anything with a filter.
bool HasSettings(const Building& b) {
    if (dynamic_cast<const Bridge*>(&b) || dynamic_cast<const Machine*>(&b) || dynamic_cast<const Collector*>(&b))
        return true;
    const std::optional<BuildingSettings> settings = SettingsOf(b);
    return settings && settings->has_filter;
}

// The bridge's provider setting, in its inspector: whether this ship sends
// other ships in its sector what they request, and whether it's short of
// cargo pods to.
void DrawProvider(Fleet& fleet, const FleetShip& shown) {
    ImGui::SeparatorText("Provider");
    bool provides = shown.provides;
    if (ImGui::Checkbox("Provide to ships in this sector", &provides))
        fleet.set_provides(shown.id, provides);
    ImGui::SetItemTooltip("Send other ships in this sector what their bridges request, from what this\n"
                          "bridge holds beyond its own requests, in cargo pods (%d items a pod)",
                          kPodCapacity);
    if (shown.provides && fleet.short_of_pods(shown.id))
        ImGui::TextColored(theme::kWarnText, "Short of cargo pods: requests here are waiting");
}

// The inspector window's settings column (HasSettings): for the bridge,
// whether its ship is a provider and its item requests; for a machine, the
// recipe picker; for a collector, what it gathers (for both, whether they run
// only on request); for a sorter or an unloader, the filter picker.
void DrawSettings(Fleet& fleet, FleetShip& shown, const Building& b) {
    Ship& ship = shown.ship;
    if (dynamic_cast<const Bridge*>(&b)) {
        DrawProvider(fleet, shown);
        DrawRequests(ship.get_bridge(), fleet, shown.id);
        return;
    }
    ImGui::SeparatorText("Settings");
    // Whatever is picked below is what new ones of its kind start with.
    const auto remember = [&] {
        if (const Building* changed = ship.building_at(*g_build.inspected))
            if (const std::optional<BuildingSettings> settings = SettingsOf(*changed))
                RememberSettings(*settings);
    };
    if (const auto* machine = dynamic_cast<const Machine*>(&b)) {
        const Progress& progress = ship.get_bridge().get_progress();
        const Recipe* current = machine->get_recipe();
        ImGui::TextColored(theme::kTextDim, "Recipe");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginIconCombo("##recipe", current ? RecipeIcon(*current) : Icon{},
                           current ? recipe_name(*current) : "Pick a recipe")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, current == nullptr) && ship.set_recipe(*g_build.inspected, nullptr))
                    remember();
                ImGui::SetItemTooltip("None");
            }
            const std::vector<const Recipe*> recipes = machine_recipes(*machine->item_kind());
            for (std::size_t i = 0; i < recipes.size(); ++i) {
                const Recipe* r = recipes[i];
                if (!ComboSearchShows(recipe_name(*r)))
                    continue;
                ImGui::PushID(static_cast<int>(i));
                const std::string hint = LockedHint(progress, *r);
                ImGui::BeginDisabled(!hint.empty());
                if (ComboSearchOption(RecipeIcon(*r), r == current) && r != current &&
                    ship.set_recipe(*g_build.inspected, r))
                    remember();
                ImGui::EndDisabled();
                RecipeTooltip(*r, nullptr, hint.empty() ? nullptr : hint.c_str());
                ImGui::PopID();
            }
            EndComboSearch();
            ImGui::EndCombo();
        }
        ImGui::TextColored(theme::kTextDim, "Changing it returns held ingredients to the bridge.");
        ImGui::Spacing();
    }
    if (const auto* collector = dynamic_cast<const Collector*>(&b)) {
        const ItemKind current = collector->get_target();
        ImGui::TextColored(theme::kTextDim, "Gathers");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginIconCombo("##target", ItemIcon(current), item_name(current))) {
            BeginComboSearch();
            for (const ItemKind kind : collector->get_spec().targets) {
                if (!ComboSearchShows(item_name(kind)))
                    continue;
                ImGui::PushID(static_cast<int>(kind));
                if (ComboSearchOption(ItemIcon(kind), kind == current) && kind != current &&
                    ship.set_collector_target(*g_build.inspected, kind))
                    remember();
                ImGui::SetItemTooltip("%s", item_name(kind));
                ImGui::PopID();
            }
            EndComboSearch();
            ImGui::EndCombo();
        }
        ImGui::Spacing();
    }
    DrawOnRequest(ship, b);
    if (const std::optional<BuildingSettings> settings = SettingsOf(b); settings && settings->has_filter) {
        const std::optional<ItemKind> current = settings->filter;
        ImGui::TextColored(theme::kTextDim, "Filter");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginIconCombo("##filter", current ? ItemIcon(*current) : Icon{},
                           current ? item_name(*current) : "Pick a filter")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, !current) && ship.set_filter(*g_build.inspected, std::nullopt))
                    remember();
                ImGui::SetItemTooltip("None");
            }
            if (const std::optional<ItemKind> picked = ComboSearchItemOptions(current))
                if (ship.set_filter(*g_build.inspected, *picked))
                    remember();
            EndComboSearch();
            ImGui::EndCombo();
        }
    }
    if (dynamic_cast<const Unloader*>(&b) && !ship.unload_source(*g_build.inspected))
        ImGui::TextColored(theme::kWarnText, "Nothing to unload: put the bridge or a cargo hold behind it");
}

// The top of the inspector window: a sunk strip with an amber stripe down
// its left edge, the building's picture, and beside it its name, its health
// and which way it faces.
void DrawInspectorHeader(const Building& b) {
    const std::optional<ItemKind> kind = b.item_kind();
    const float fh = ImGui::GetFrameHeight();
    const float pad = ImGui::GetStyle().WindowPadding.y;
    const float stripe = std::max(3.0f, fh * 0.15f);
    const float picture = fh * 2.6f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    // The strip goes behind what's on it, whose height it takes: draw that
    // first, on top, then the strip under it.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    const ImVec2 picture_min(min.x + stripe + pad, min.y + pad);
    ImGui::SetCursorScreenPos(picture_min);
    ImGui::Dummy(ImVec2(picture, picture));
    DrawIcon(draw, BuildingIcon(b), picture_min, ImVec2(picture_min.x + picture, picture_min.y + picture));
    ImGui::SameLine(0.0f, pad);
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, theme::kFontTitle);
    ImGui::TextUnformatted(kind ? item_name(*kind) : "Bridge");
    ImGui::PopFont();
    const std::string health = "Health " + Fraction(b.get_hp(), b.max_hp());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, b.damaged() ? theme::kBad : theme::kGood);
    ImGui::ProgressBar(static_cast<float>(b.get_hp()) / static_cast<float>(std::max(1, b.max_hp())),
                       ImVec2(min.x + width - pad - ImGui::GetCursorScreenPos().x, 0.0f), health.c_str());
    ImGui::PopStyleColor();
    if (b.rotatable())
        ImGui::TextColored(theme::kTextDim, "Facing %s", DirectionName(b.facing()));
    ImGui::EndGroup();
    const ImVec2 max(min.x + width, std::max(picture_min.y + picture, ImGui::GetItemRectMax().y) + pad);
    draw->ChannelsSetCurrent(0);
    draw->AddRectFilled(min, max, theme::U32(theme::kInset));
    draw->AddRect(min, max, theme::U32(theme::kBevelDark));
    draw->AddRectFilled(min, ImVec2(min.x + stripe, max.y), theme::U32(theme::kAccent));
    draw->ChannelsMerge();
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, max.y - min.y));
}

}  // namespace

void DrawInspectorWindow(Fleet& fleet, FleetShip& shown_ship) {
    Ship& ship = shown_ship.ship;
    const Building* b = InspectedBuilding(ship);
    if (!b)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float fh = ImGui::GetFrameHeight();
    // In the middle of the screen the first time it opens; after that, where
    // it was last moved to and at the size it was last resized to (ImGui
    // keeps both, in imgui.ini too), up to most of the screen; what doesn't
    // fit scrolls.
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fh * 32.0f, fh * 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(fh * 22.0f, fh * 8.0f),
                                        ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.8f));
    bool open = true;
    const bool shown = ImGui::Begin("Inspect###inspector", &open, ImGuiWindowFlags_NoCollapse);
    if (!open) {
        g_build.inspected.reset();
        g_build.inspected_building = nullptr;
    }
    if (!shown || !open) {
        ImGui::End();
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    DrawInspectorHeader(*b);
    // What it's doing on the left and what can be set on the right, or just
    // what it's doing, across the window, when nothing can be.
    if (HasSettings(*b) &&
        ImGui::BeginTable("##inspector", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Status");
        BuildingDetails(*b, false);
        ImGui::TableNextColumn();
        DrawSettings(fleet, shown_ship, *b);
        ImGui::EndTable();
    } else {
        ImGui::SeparatorText("Status");
        BuildingDetails(*b, false);
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}

namespace {

// The milestone being worked on: what to deliver, how much has been, and
// what it unlocks.
void DrawMilestone(const Progress& progress) {
    if (!theme::CollapsingSection("Milestone"))
        return;
    const Milestone* m = progress.current_milestone();
    if (!m) {
        ImGui::TextColored(theme::kGood, "All milestones complete");
        return;
    }
    ImGui::Text("%zu / %zu: %s", progress.current_index() + 1, all_milestones().size(), m->name);
    ImGui::TextColored(theme::kTextDim, "Deliver to the bridge:");
    for (const ItemStack& s : m->deliver) {
        const int have = progress.delivered_count(s.kind);
        const std::string overlay = std::to_string(have) + " / " + std::to_string(s.count);
        ImGui::AlignTextToFramePadding();
        IconImage(ItemIcon(s.kind), ImGui::GetFrameHeight());
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::ProgressBar(static_cast<float>(have) / static_cast<float>(s.count), ImVec2(-1.0f, 0.0f),
                           overlay.c_str());
    }
    ImGui::TextColored(theme::kTextDim, "Unlocks");
    const float icon = ImGui::GetFrameHeight() * 1.4f;
    IconFlow flow;
    flow.Start(icon);
    for (const ItemKind kind : m->unlocks) {
        flow.Next();
        IconTile(ItemIcon(kind), icon);
        ImGui::SetItemTooltip("%s", item_name(kind));
    }
}

}  // namespace

void DrawCraftPanel(World& world, FleetShip& current, float width) {
    Ship& ship = current.ship;
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawPlatePanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y));

    ImGui::BeginChild("##craft_panel", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    Bridge& bridge = ship.get_bridge();
    DrawMilestone(bridge.get_progress());
    ImGui::PushTextWrapPos(0.0f);
    DrawFleetSection(world, current);
    ImGui::PopTextWrapPos();

    const float icon = ImGui::GetFrameHeight() * 1.6f;
    const std::deque<CraftJob>& queue = bridge.craft_queue();
    if (!theme::CollapsingSection("Craft queue")) {
    } else if (queue.empty()) {
        ImGui::TextColored(theme::kTextDim, "Idle");
    } else {
        constexpr std::size_t kShown = 8;
        std::optional<std::size_t> cancel;
        IconFlow flow;
        flow.Start(icon);
        for (std::size_t i = 0; i < std::min(queue.size(), kShown); ++i) {
            const CraftJob& job = queue[i];
            flow.Next();
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 job_min = ImGui::GetCursorScreenPos();
            // The first one is being crafted: highlighted, with its progress
            // along the bottom, and only a right click cancels it.
            const bool clicked = IconButton("##job", RecipeIcon(*job.recipe), icon, i == 0);
            if (i == 0) {
                const float progress = static_cast<float>(job.elapsed) / static_cast<float>(job.recipe->ticks);
                const float h = std::max(3.0f, icon * 0.1f);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(job_min.x + 2.0f, job_min.y + icon - h - 2.0f),
                    ImVec2(job_min.x + 2.0f + (icon - 4.0f) * progress, job_min.y + icon - 2.0f),
                    theme::U32(theme::kAccent));
            }
            if ((clicked && i > 0) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                cancel = i;
            ImGui::SetItemTooltip("%s\n%s", recipe_name(*job.recipe),
                                  i == 0 ? "Crafting: right click to cancel and refund" : "Click to cancel and refund");
            ImGui::PopID();
        }
        if (queue.size() > kShown)
            ImGui::TextColored(theme::kTextDim, "... and %zu more", queue.size() - kShown);
        if (cancel)
            bridge.cancel_craft(*cancel);
    }

    if (theme::CollapsingSection("Hand-craft")) {
        ImGui::TextColored(theme::kTextDim, "Click: 1   Shift-click: 5");
        ImGui::SeparatorText("Materials");
        DrawHandCraftButtons(bridge, ItemCategory::Material);
        ImGui::SeparatorText("Components");
        DrawHandCraftButtons(bridge, ItemCategory::Component);
        ImGui::SeparatorText("Buildings");
        DrawHandCraftButtons(bridge, ItemCategory::Placeable);
    }

    if (!theme::CollapsingSection("Recipe book", false)) {
        ImGui::EndChild();
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim, "Made in machines: click one on the grid to pick its recipe.");
    ImGui::PopTextWrapPos();
    const std::vector<Recipe>& recipes = all_recipes();
    for (int c = 0; c < kCraftCategoryCount; ++c) {
        const CraftCategory category = static_cast<CraftCategory>(c);
        if (!ImGui::TreeNode(craft_category_name(category)))
            continue;
        IconFlow flow;
        flow.Start(icon);
        for (std::size_t i = 0; i < recipes.size(); ++i) {
            const Recipe& r = recipes[i];
            if (r.category != category || r.hand_craftable)
                continue;
            flow.Next();
            const std::string locked = LockedBadge(bridge.get_progress(), r);
            IconTile(RecipeIcon(r), icon, {locked.empty() ? nullptr : locked.c_str()});
            const std::string hint = LockedHint(bridge.get_progress(), r);
            RecipeTooltip(r, &bridge.get_inventory(), hint.empty() ? nullptr : hint.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

}  // namespace ui
