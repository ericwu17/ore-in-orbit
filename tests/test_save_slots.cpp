// test_save_slots.cpp — headless tests for the saves folder: listing saves
// newest first, naming new ones, renaming, copying and deleting, and
// unreadable files showing up as such. Works in a scratch folder under the
// system temp directory, removed again at the end.
#include "persistence/save_slots.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

fs::path new_save(const fs::path& dir, const std::string& name, const World& world, std::int64_t saved_at) {
    std::string error;
    const std::optional<fs::path> path = new_save_path(dir, error);
    assert(path);
    assert(write_save_file(*path, name, world, error, saved_at));
    return *path;
}

void test_slots(const fs::path& dir) {
    // No folder yet: no saves, and the first default name.
    assert(list_saves(dir).empty());
    assert(next_default_name({}) == "Ship 1");

    World early;
    World later;
    run(later, 600);
    const fs::path first = new_save(dir, "Ship 1", early, 1'000);
    const fs::path second = new_save(dir, "Ship 2", later, 2'000);
    assert(first != second);

    // Newest first.
    std::vector<SaveSlot> slots = list_saves(dir);
    assert(slots.size() == 2);
    assert(slots[0].path == second && slots[0].header->name == "Ship 2" && slots[0].header->played == 600);
    assert(slots[1].path == first && slots[1].header->saved_at == 1'000);
    assert(next_default_name(slots) == "Ship 3");

    // Renaming keeps the file and when it was saved.
    std::string error;
    assert(rename_save(first, "Outpost", error));
    slots = list_saves(dir);
    assert(slots[1].path == first && slots[1].header->name == "Outpost" && slots[1].header->saved_at == 1'000);
    assert(next_default_name(slots) == "Ship 1");

    // Copying makes a new file, the same game, saved now (so it comes first).
    const std::optional<fs::path> copy = copy_save(dir, second, error);
    assert(copy && *copy != second);
    slots = list_saves(dir);
    assert(slots.size() == 3);
    assert(slots[0].path == *copy && slots[0].header->name == "Ship 2 (copy)" && slots[0].header->played == 600);
    const std::optional<LoadedSave> loaded = load_save_file(*copy, error);
    assert(loaded && loaded->world.get_tick() == 600);

    // Overwriting a save (as autosave does) replaces it whole.
    assert(write_save_file(second, "Ship 2", early, error, 3'000));
    const std::optional<LoadedSave> overwritten = load_save_file(second, error);
    assert(overwritten && overwritten->header.saved_at == 3'000 && overwritten->world.get_tick() == 0);
    assert(list_saves(dir).size() == 3);

    // Deleting.
    assert(delete_save(*copy, error));
    assert(list_saves(dir).size() == 2);
    assert(!delete_save(*copy, error) && !error.empty());
    assert(!load_save_file(*copy, error));

    // A damaged save is listed last, with the reason; leftovers from an
    // interrupted write and other files aren't listed at all.
    std::ofstream(dir / "broken.sav") << "not a save";
    std::ofstream(dir / "save-9.sav.tmp") << "half a save";
    std::ofstream(dir / "notes.txt") << "hello";
    slots = list_saves(dir);
    assert(slots.size() == 3);
    assert(slots[2].path.filename() == "broken.sav" && !slots[2].header && !slots[2].error.empty());
    error.clear();
    assert(!rename_save(dir / "broken.sav", "Fixed", error) && !error.empty());
    assert(!copy_save(dir, dir / "broken.sav", error));
    assert(delete_save(dir / "broken.sav", error));
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ore-in-orbit-test-save-slots";
    fs::remove_all(dir);
    test_slots(dir / "saves");
    fs::remove_all(dir);
    std::puts("test_save_slots: all passed");
    return 0;
}
