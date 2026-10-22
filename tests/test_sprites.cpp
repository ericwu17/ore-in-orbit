// test_sprites.cpp — headless checks of assets/sprites/ (see src/ui/sprites.h):
// every item has a square icon, and every placeable and the bridge
// a built sprite with its footprint's shape. Catches an ItemKind added
// without art, and a footprint changed without its sprite. Runs from the
// repo root, like every test.
#include "sim/building.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

int g_failures = 0;

// Reads a PNG's width and height from its IHDR chunk (big-endian, at bytes
// 16..23). False if `path` can't be read or isn't a PNG.
bool png_size(const std::string& path, int& w, int& h) {
    std::ifstream in(path, std::ios::binary);
    unsigned char header[24] = {};
    if (!in.read(reinterpret_cast<char*>(header), sizeof(header)))
        return false;
    constexpr unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    for (int i = 0; i < 8; ++i)
        if (header[i] != kSignature[i])
            return false;
    const auto be32 = [&](int at) {
        return static_cast<int>((static_cast<unsigned>(header[at]) << 24) | (static_cast<unsigned>(header[at + 1]) << 16) |
                                (static_cast<unsigned>(header[at + 2]) << 8) | static_cast<unsigned>(header[at + 3]));
    };
    w = be32(16);
    h = be32(20);
    return w > 0 && h > 0;
}

// The sprite at assets/sprites/<rel> exists and is `aspect.w` by `aspect.h`
// in shape; reports it (and counts a failure) when not.
void check_sprite(const std::string& rel, GridSize aspect) {
    const std::string path = "assets/sprites/" + rel;
    int w = 0;
    int h = 0;
    if (!png_size(path, w, h)) {
        std::printf("  missing or not a PNG: %s\n", path.c_str());
        ++g_failures;
    } else if (w * aspect.h != h * aspect.w) {
        std::printf("  %s is %dx%d, not %d:%d\n", path.c_str(), w, h, aspect.w, aspect.h);
        ++g_failures;
    }
}

void test_item_icons() {
    for (int i = 0; i < kItemKindCount; ++i)
        check_sprite(std::string("items/") + item_id(static_cast<ItemKind>(i)) + ".png", {1, 1});
}

void test_building_sprites() {
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_building(kind))
            check_sprite(std::string("buildings/") + item_id(kind) + ".png", footprint_of(kind));
    }
    check_sprite("buildings/Bridge.png", {kBridgeSize, kBridgeSize});
    check_sprite("buildings/Hull.png", {1, 1});
}

}  // namespace

int main() {
    test_item_icons();
    test_building_sprites();
    if (g_failures > 0)
        std::printf("test_sprites: %d sprites missing or misshapen "
                    "(python3 tools/make_placeholder_sprites.py makes stand-ins)\n",
                    g_failures);
    assert(g_failures == 0);
    std::puts("test_sprites: all passed");
    return 0;
}
