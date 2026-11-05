// embedded_sounds.h — the sound files under assets/sounds/, embedded into
// the executable at build time (the makefile generates the arrays, as it
// does the sprites'), in their own translation unit: they're large, and only
// recompile when a sound changes.
#pragma once

#include <span>
#include <string_view>

namespace audio {

// The embedded file at `path` under assets/sounds/ ("sfx/ui_click.wav"), or
// an empty span if there's none.
std::span<const unsigned char> EmbeddedSound(std::string_view path);

}  // namespace audio
