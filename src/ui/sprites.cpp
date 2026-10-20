// sprites.cpp — see sprites.h.
//
// The PNGs are shelf-packed into one RGBA texture, tallest first, so drawing
// any number of sprites stays a single texture. Each is padded with copies of
// its edge pixels, so linear filtering never blends in a neighbor. The
// texture goes to the renderer backend as an ImGui "user texture": the
// backend uploads and frees it, and this file needs no OpenGL.
#include "ui/sprites.h"

#include "sim/building.h"  // footprint_of, kBridgeSize

#include "imgui_internal.h"  // ImGui::RegisterUserTexture

// The implementation is compiled in src/app/app_icon.cpp.
#include "stb/stb_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// Generated from assets/sprites/ by the makefiles: kEmbeddedSprites[].
#include "sprites_png.h"

namespace ui::sprites {
namespace {

constexpr int kAtlasWidth = 2048;  // wider if one sprite is
constexpr int kPadding = 2;        // edge pixels repeated around each sprite

struct Atlas {
    ImTextureData* texture = nullptr;
    std::array<std::optional<Sprite>, kItemKindCount> items;
    std::array<std::optional<Sprite>, kItemKindCount> buildings;
    std::optional<Sprite> bridge;
    std::array<std::optional<Sprite>, kFluidKindCount> fluids;
};
Atlas g_atlas;

// A decoded PNG on its way into the atlas.
struct Decoded {
    std::optional<Sprite>* slot;
    int w = 0;
    int h = 0;
    unsigned char* pixels = nullptr;  // RGBA, from stbi (stbi_image_free)
    int x = 0;                        // where it goes in the atlas
    int y = 0;
};

const EmbeddedSprite* FindEmbedded(const std::string& path) {
    for (const EmbeddedSprite* s = kEmbeddedSprites; s->path; ++s)
        if (path == s->path)
            return s;
    return nullptr;
}

// Decodes the sprite at `path` (under assets/sprites/), which must be
// `aspect.w` by `aspect.h` in shape, for `slot`. Reports it and leaves the
// slot empty when it can't.
void Decode(std::vector<Decoded>& out, std::optional<Sprite>& slot, const std::string& path, GridSize aspect) {
    const EmbeddedSprite* embedded = FindEmbedded(path);
    if (!embedded) {
        std::printf("Warning: sprite assets/sprites/%s is missing\n", path.c_str());
        return;
    }
    Decoded d{&slot};
    int channels = 0;
    d.pixels = stbi_load_from_memory(embedded->data, static_cast<int>(embedded->size), &d.w, &d.h, &channels, 4);
    if (!d.pixels) {
        std::printf("Warning: can't decode sprite assets/sprites/%s: %s\n", path.c_str(), stbi_failure_reason());
        return;
    }
    if (d.w * aspect.h != d.h * aspect.w) {
        std::printf("Warning: sprite assets/sprites/%s is %dx%d, not %d:%d\n", path.c_str(), d.w, d.h, aspect.w,
                    aspect.h);
        stbi_image_free(d.pixels);
        return;
    }
    out.push_back(d);
}

// Places each sprite in rows across the atlas, tallest first. Returns the
// atlas's size.
ImVec2 Pack(std::vector<Decoded>& sprites) {
    std::vector<Decoded*> order;
    int width = kAtlasWidth;
    for (Decoded& d : sprites) {
        order.push_back(&d);
        width = std::max(width, d.w + 2 * kPadding);
    }
    std::stable_sort(order.begin(), order.end(), [](const Decoded* a, const Decoded* b) { return a->h > b->h; });
    int x = kPadding;
    int y = kPadding;
    int row_h = 0;
    for (Decoded* d : order) {
        if (x + d->w + kPadding > width) {
            x = kPadding;
            y += row_h + 2 * kPadding;
            row_h = 0;
        }
        d->x = x;
        d->y = y;
        x += d->w + 2 * kPadding;
        row_h = std::max(row_h, d->h);
    }
    return ImVec2(static_cast<float>(width), static_cast<float>(y + row_h + kPadding));
}

// Copies `d` into the atlas pixels (`width` wide) at its place, padding it
// with its own edge pixels.
void Blit(unsigned char* atlas, int width, const Decoded& d) {
    for (int dy = -kPadding; dy < d.h + kPadding; ++dy) {
        const int sy = std::clamp(dy, 0, d.h - 1);
        for (int dx = -kPadding; dx < d.w + kPadding; ++dx) {
            const int sx = std::clamp(dx, 0, d.w - 1);
            const std::size_t src = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(d.w) +
                                     static_cast<std::size_t>(sx)) * 4;
            const std::size_t dst = (static_cast<std::size_t>(d.y + dy) * static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(d.x + dx)) * 4;
            std::memcpy(atlas + dst, d.pixels + src, 4);
        }
    }
}

}  // namespace

void Load() {
    std::vector<Decoded> decoded;
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        const std::string id = item_id(kind);
        Decode(decoded, g_atlas.items[static_cast<std::size_t>(i)], "items/" + id + ".png", {1, 1});
        if (is_placeable(kind))
            Decode(decoded, g_atlas.buildings[static_cast<std::size_t>(i)], "buildings/" + id + ".png",
                   footprint_of(kind));
    }
    Decode(decoded, g_atlas.bridge, "buildings/Bridge.png", {kBridgeSize, kBridgeSize});
    for (int i = 0; i < kFluidKindCount; ++i)
        Decode(decoded, g_atlas.fluids[static_cast<std::size_t>(i)],
               std::string("fluids/") + fluid_id(static_cast<FluidKind>(i)) + ".png", {1, 1});
    if (decoded.empty())
        return;

    const ImVec2 size = Pack(decoded);
    const int width = static_cast<int>(size.x);
    const int height = static_cast<int>(size.y);
    auto* texture = new ImTextureData();
    texture->Create(ImTextureFormat_RGBA32, width, height);
    texture->UseColors = true;
    for (const Decoded& d : decoded) {
        Blit(texture->Pixels, width, d);
        Sprite sprite;
        sprite.tex = texture->GetTexRef();
        sprite.uv0 = ImVec2(static_cast<float>(d.x) / size.x, static_cast<float>(d.y) / size.y);
        sprite.uv1 = ImVec2(static_cast<float>(d.x + d.w) / size.x, static_cast<float>(d.y + d.h) / size.y);
        sprite.w = d.w;
        sprite.h = d.h;
        *d.slot = sprite;
        stbi_image_free(d.pixels);
    }
    ImGui::RegisterUserTexture(texture);
    g_atlas.texture = texture;
}

void Unload() {
    if (g_atlas.texture) {
        ImGui::UnregisterUserTexture(g_atlas.texture);
        delete g_atlas.texture;
    }
    g_atlas = {};
}

const Sprite* Item(ItemKind kind) {
    const std::optional<Sprite>& s = g_atlas.items[static_cast<std::size_t>(kind)];
    return s ? &*s : nullptr;
}

const Sprite* Building(ItemKind kind) {
    const std::optional<Sprite>& s = g_atlas.buildings[static_cast<std::size_t>(kind)];
    return s ? &*s : nullptr;
}

const Sprite* Bridge() {
    return g_atlas.bridge ? &*g_atlas.bridge : nullptr;
}

const Sprite* Fluid(FluidKind kind) {
    const std::optional<Sprite>& s = g_atlas.fluids[static_cast<std::size_t>(kind)];
    return s ? &*s : nullptr;
}

}  // namespace ui::sprites
