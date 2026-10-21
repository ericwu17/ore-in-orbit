// files.h — file handling shared by the saves and blueprint folders: writing
// a file without ever leaving a half-written one, and numbering new files.
// Headless like the simulation. Nothing here throws: failures return false
// or nullopt and say why in `error`.
#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <system_error>

// Writes `path` with `write`, replacing the file only once the whole new one
// is on disk (via a temporary file), so a crash mid-write leaves the old one
// intact.
bool write_file_atomically(const std::filesystem::path& path, const std::function<void(std::ostream&)>& write,
                           std::string& error);

// The first "<stem>-<N><extension>" in `dir` (N from 1) that doesn't exist,
// creating the folder if needed (`what` names it in the error).
std::optional<std::filesystem::path> new_numbered_path(const std::filesystem::path& dir, const std::string& stem,
                                                       const std::string& extension, const char* what,
                                                       std::string& error);

// Deletes the file; false, saying why, if it can't (or is already gone).
bool delete_file(const std::filesystem::path& path, std::string& error);

// "<file name>: <what went wrong>".
std::string describe_file_error(const std::filesystem::path& path, const std::error_code& ec);
