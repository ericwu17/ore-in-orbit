// files.cpp — file handling shared by saves and blueprints (see files.h).
#include "persistence/files.h"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

std::string describe_file_error(const fs::path& path, const std::error_code& ec) {
    return path.filename().string() + ": " + ec.message();
}

bool write_file_atomically(const fs::path& path, const std::function<void(std::ostream&)>& write,
                           std::string& error) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out)
            write(out);
        out.close();
        if (!out) {
            error = "can't write " + tmp.filename().string();
            std::error_code ignored;
            fs::remove(tmp, ignored);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        // Some platforms won't rename over an existing file.
        ec.clear();
        fs::remove(path, ec);
        if (!ec)
            fs::rename(tmp, path, ec);
    }
    if (ec) {
        error = describe_file_error(path, ec);
        return false;
    }
    return true;
}

std::optional<fs::path> new_numbered_path(const fs::path& dir, const std::string& stem, const std::string& extension,
                                          const char* what, std::string& error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = std::string("can't create the ") + what + " folder: " + ec.message();
        return std::nullopt;
    }
    for (int n = 1;; ++n) {
        fs::path path = dir / (stem + "-" + std::to_string(n) + extension);
        if (!fs::exists(path, ec) && !ec)
            return path;
        if (ec) {
            error = describe_file_error(path, ec);
            return std::nullopt;
        }
    }
}

bool delete_file(const fs::path& path, std::string& error) {
    std::error_code ec;
    if (!fs::remove(path, ec)) {
        error = ec ? describe_file_error(path, ec) : path.filename().string() + " is already gone";
        return false;
    }
    return true;
}
