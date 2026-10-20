// sprites.h — the game's pictures: an icon for every item, and the
// built look of every building, drawn by the UI instead of plain text and
// shapes.
//
// They come from assets/sprites/ (see the readme's "Sprites" section),
// embedded into the executable at build time like the logo and the font, and
// are packed into one atlas texture at startup:
//
//   items/<ItemId>.png       square icon of every item, placeables included
//   buildings/<ItemId>.png   built look of every placeable, facing North,
//                            with its footprint's aspect ratio
//   buildings/Bridge.png     the bridge
//
// (<ItemId>: item_id, e.g. "IronPlate".) A sprite
// that is missing, can't be decoded or has the wrong shape is reported once
// at startup and looked up as null: the UI then draws its old stand-in.
#pragma once

#include "sim/game_types.h"

#include "imgui.h"

namespace ui::sprites {

// Where a sprite is: a rectangle (uv0..uv1) of the atlas texture `tex`, for
// ImGui::Image or ImDrawList::AddImage / AddImageQuad.
struct Sprite {
    ImTextureRef tex;
    ImVec2 uv0;
    ImVec2 uv1;
    int w = 0;  // the PNG's size, in pixels
    int h = 0;
};

// Decodes the embedded sprites into the atlas and hands it to the renderer
// backend (which uploads it with the next frame). Call once, after
// ImGui::CreateContext() and before the first frame.
void Load();

// Forgets the atlas. Call after the renderer backend's shutdown (which frees
// its GPU copy) and before ImGui::DestroyContext().
void Unload();

// The sprites, or null when one isn't there (or before Load()).
const Sprite* Item(ItemKind kind);       // an item's icon
const Sprite* Building(ItemKind kind);   // a placeable, built (facing North)
const Sprite* Bridge();                  // the bridge, built

}  // namespace ui::sprites
