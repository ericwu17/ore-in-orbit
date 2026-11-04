// sprites.h — the game's pictures: an icon for every item, and the built
// look of every building.
//
// They come from assets/sprites/ (see the readme's "Sprites" section),
// embedded into the executable at build time like the logo and the font, and
// are packed into one atlas texture at startup:
//
//   items/<ItemId>.png       square icon of every item, buildings included
//   buildings/<ItemId>.png   built look of every building, facing North,
//                            with its footprint's aspect ratio
//   buildings/Bridge.png     the bridge
//   buildings/Hull.png       one cell of hull (the deck plating)
//   buildings/BeltTunnelEntrance.png
//                            a belt tunnel's entrance (BeltTunnel.png is its
//                            exit)
//   buildings/<Belt><Shape>.png
//                            a transport or express belt fed from the side
//                            (see BeltShape), facing North:
//                            TransportBeltCurveLeft.png, ExpressBeltMerge.png
//
// (<ItemId>: item_id, e.g. "IronPlate".) A sprite that is missing, can't be
// decoded or has the wrong shape is reported once at startup and looked up
// as null: the UI then draws a stand-in shape.
#pragma once

#include "sim/game_types.h"

#include "imgui.h"

namespace ui::sprites {

// The shape a belt takes from what feeds it (see ui::BeltShapeAt), facing
// North. "Left" and "right" are as seen looking the way the belt goes.
enum class BeltShape {
    Straight,    // fed from behind, or not at all: the belt's own sprite
    CurveLeft,   // from the left only: a quarter turn
    CurveRight,  // from the right only
    SideLeft,    // from behind and the left: a straight run joined on its left
    SideRight,   // from behind and the right
    SideBoth,    // from behind and both sides
    Merge,       // from both sides, not behind: two turns meeting
};
constexpr int kBeltShapeCount = 7;

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
const Sprite* Building(ItemKind kind);   // a building, built (facing North)
const Sprite* Bridge();                  // the bridge, built
const Sprite* Hull();                    // one cell of hull
const Sprite* TunnelEntrance();          // a belt tunnel's entrance (facing North)
// A transport or express belt in `shape` (facing North); for Straight, its
// own sprite. Null for other kinds.
const Sprite* Belt(ItemKind kind, BeltShape shape);

}  // namespace ui::sprites
