// ui_panels.cpp — the game screen's side panels: the build palette (left)
// and the craft panel (right), with the milestone, the inspector and
// hand-crafting (see ui_internal.h).
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
            if (is_unfinished(kind))
                ImGui::TextColored(theme::kWarnText, "Not working yet: it does nothing once built");
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
    const std::size_t i = *milestone_unlocking(recipe.outputs.front().kind);
    return "Unlocks with milestone " + std::to_string(i + 1) + ": " + all_milestones()[i].name;
}

// The corner badge on a locked recipe's icon ("M2"); empty once unlocked.
std::string LockedBadge(const Progress& progress, const Recipe& recipe) {
    if (progress.unlocked(recipe))
        return {};
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
    // Its picture, and its name beside it.
    const float picture = ImGui::GetFrameHeight() * 1.6f;
    const ImVec2 picture_min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(picture, picture));
    DrawIcon(ImGui::GetWindowDrawList(), BuildingIcon(b), picture_min,
             ImVec2(picture_min.x + picture, picture_min.y + picture));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (picture - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextUnformatted(kind ? item_name(*kind) : "Bridge");
    ImGui::TextColored(b.damaged() ? theme::kBad : theme::kTextDim, "Health %d / %d", b.get_hp(), b.max_hp());
    if (kind && is_unfinished(*kind))
        ImGui::TextColored(theme::kWarnText, "Not working yet: it does nothing");

    const float bar_w = ImGui::GetFrameHeight() * 8.0f;
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
    if (const auto* belt = dynamic_cast<const Conveyor*>(&b); belt && belt->moves_items()) {
        ImGui::TextColored(theme::kTextDim, "%.0f cells / s",
                           static_cast<double>(belt->speed() * static_cast<int>(kTicksPerSecond)) / kBeltLength);
        Inventory carried;
        for (const BeltItem& item : belt->get_items())
            carried.add(item.kind);
        for (const ItemStack& s : carried.stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
        const std::optional<ItemKind> filter = sorter->get_filter();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        AmountLine(filter ? ItemIcon(*filter) : Icon{}, filter ? item_name(*filter) : "none", theme::kText, false);
        if (sorter->inverted())
            ImGui::TextColored(theme::kTextDim, "Matching items turn left or right;\nothers go straight through");
        else
            ImGui::TextColored(theme::kTextDim, "Matching items go straight through;\nothers turn left or right");
    }
    if (const auto* junction = dynamic_cast<const Junction*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "Items cross straight over, out the\nopposite side");
        Inventory crossing;
        for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West})
            for (const JunctionItem& item : junction->lane(travel))
                crossing.add(item.kind);
        for (const ItemStack& s : crossing.stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* router = dynamic_cast<const Router*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "Passes items to its other three sides\nin turn");
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
    if (const auto* turret = dynamic_cast<const Turret*>(&b)) {
        const TurretSpec& spec = turret->get_spec();
        ImGui::TextColored(theme::kTextDim, "%d damage, %.1f shots / s, range %.0f", spec.damage,
                           static_cast<double>(kTicksPerSecond) / static_cast<double>(spec.interval),
                           static_cast<double>(spec.range));
        const std::string held = Fraction(turret->get_ammo(), kTurretAmmoCapacity);
        AmountLine(ItemIcon(spec.ammo), held, turret->get_ammo() > 0 ? theme::kText : theme::kBad, in_tooltip);
        if (turret->shots_left() == 0)
            ImGui::TextColored(theme::kBad, "Out of ammunition: belt %s into it", item_name(spec.ammo));
        else
            ImGui::TextColored(theme::kTextDim, "%d shots left", turret->shots_left());
    }
    if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
        ImGui::Text("Stored: %d / %d", hold->get_stored().total(), kCargoCapacity);
        for (const ItemStack& s : hold->get_stored().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (!kind)
        ImGui::TextColored(theme::kTextDim, "Belts deliver into its inventory");
    if (b.rotatable())
        ImGui::TextColored(theme::kTextDim, "Facing %s", DirectionName(b.facing()));
}

namespace {

// The inspected building (clicked on the grid): its details, and for a
// machine, the recipe picker; for a collector, what it gathers; for a
// sorter, the filter picker.
void DrawInspector(Ship& ship) {
    const Building* b = InspectedBuilding(ship);
    if (!b)
        return;
    // The heading, with a cross at its right end to close it.
    const ImVec2 heading_min = ImGui::GetCursorScreenPos();
    const float heading_w = ImGui::GetContentRegionAvail().x;
    theme::SectionHeading("Inspect");
    const ImVec2 after_heading = ImGui::GetCursorScreenPos();
    const float s = ImGui::GetTextLineHeight();
    ImGui::SetCursorScreenPos(ImVec2(heading_min.x + heading_w - s, heading_min.y + ImGui::GetStyle().ItemSpacing.y +
                                                                        (ImGui::GetFrameHeight() - s) * 0.5f));
    if (theme::CloseButton("##close_inspector")) {
        g_build.inspected.reset();
        g_build.inspected_building = nullptr;
        return;
    }
    ImGui::SetItemTooltip("Close (Esc)");
    ImGui::SetCursorScreenPos(after_heading);
    // Whatever is picked below is what new ones of its kind start with.
    const auto remember = [&] {
        if (const Building* changed = ship.building_at(*g_build.inspected))
            if (const std::optional<BuildingSettings> settings = SettingsOf(*changed))
                RememberSettings(*settings);
    };
    BuildingDetails(*b, false);
    if (const auto* machine = dynamic_cast<const Machine*>(b)) {
        const Progress& progress = ship.get_bridge().get_progress();
        const Recipe* current = machine->get_recipe();
        ImGui::SetNextItemWidth(-1.0f);
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
    }
    if (const auto* collector = dynamic_cast<const Collector*>(b)) {
        const ItemKind current = collector->get_target();
        ImGui::SetNextItemWidth(-1.0f);
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
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(b)) {
        const std::optional<ItemKind> current = sorter->get_filter();
        ImGui::SetNextItemWidth(-1.0f);
        if (BeginIconCombo("##filter", current ? ItemIcon(*current) : Icon{},
                           current ? item_name(*current) : "Pick a filter")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, !current) && ship.set_sort_filter(*g_build.inspected, std::nullopt))
                    remember();
                ImGui::SetItemTooltip("None");
            }
            if (const std::optional<ItemKind> picked = ComboSearchItemOptions(current))
                if (ship.set_sort_filter(*g_build.inspected, *picked))
                    remember();
            EndComboSearch();
            ImGui::EndCombo();
        }
    }
}

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
    // The inspector first, so a click on the grid shows up where it's seen.
    ImGui::PushTextWrapPos(0.0f);
    DrawInspector(ship);
    ImGui::PopTextWrapPos();
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
