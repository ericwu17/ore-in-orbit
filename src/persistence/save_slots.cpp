// save_slots.cpp — the saves folder (see save_slots.h).
#include "persistence/save_slots.h"

#include "persistence/files.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

constexpr const char* kExtension = ".sav";

std::int64_t now() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

}  // namespace

std::vector<SaveSlot> list_saves(const fs::path& dir) {
    std::vector<SaveSlot> slots;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kExtension)
            continue;
        SaveSlot slot{it->path(), std::nullopt, {}};
        std::ifstream in(slot.path, std::ios::binary);
        if (!in)
            slot.error = "can't open the file";
        else
            slot.header = read_save_header(in, slot.error);
        slots.push_back(std::move(slot));
    }
    std::sort(slots.begin(), slots.end(), [](const SaveSlot& a, const SaveSlot& b) {
        if (a.header.has_value() != b.header.has_value())
            return a.header.has_value();
        if (a.header && a.header->saved_at != b.header->saved_at)
            return a.header->saved_at > b.header->saved_at;
        return a.path.filename() < b.path.filename();
    });
    return slots;
}

std::optional<fs::path> new_save_path(const fs::path& dir, std::string& error) {
    return new_numbered_path(dir, "save", kExtension, "saves", error);
}

std::string next_default_name(const std::vector<SaveSlot>& slots) {
    for (int n = 1;; ++n) {
        const std::string name = "Ship " + std::to_string(n);
        const bool taken = std::any_of(slots.begin(), slots.end(),
                                       [&](const SaveSlot& s) { return s.header && s.header->name == name; });
        if (!taken)
            return name;
    }
}

bool write_save_file(const fs::path& path, const std::string& name, const World& world, std::string& error,
                     std::optional<std::int64_t> saved_at) {
    return write_file_atomically(
        path, [&](std::ostream& out) { write_save(out, name, saved_at.value_or(now()), world); }, error);
}

std::optional<LoadedSave> load_save_file(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "can't open " + path.filename().string();
        return std::nullopt;
    }
    return read_save(in, error);
}

bool rename_save(const fs::path& path, const std::string& new_name, std::string& error) {
    const std::optional<LoadedSave> save = load_save_file(path, error);
    return save && write_save_file(path, new_name, save->world, error, save->header.saved_at);
}

std::optional<fs::path> copy_save(const fs::path& dir, const fs::path& path, std::string& error) {
    const std::optional<LoadedSave> save = load_save_file(path, error);
    if (!save)
        return std::nullopt;
    std::optional<fs::path> copy = new_save_path(dir, error);
    if (!copy || !write_save_file(*copy, save->header.name + " (copy)", save->world, error))
        return std::nullopt;
    return copy;
}

bool delete_save(const fs::path& path, std::string& error) {
    return delete_file(path, error);
}
