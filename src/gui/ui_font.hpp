#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "raylib.h"

namespace incidentlab {

/// Roles map to one rasterized size. Text is drawn at that size, not scaled up from a smaller atlas.
enum class UiRole { Title, Heading, Label, Value, Timeline, Button, Detail };

/// Load DejaVu Sans or Liberation Sans from the local system. Safe to call again.
/// A missing font keeps the built-in raylib font so the window still opens.
void load_ui_fonts();

/// Release faces this module owns. The built-in font is left loaded for CloseWindow().
void unload_ui_fonts();

/// True when a TrueType face was loaded. False when drawing uses the built-in bitmap font.
bool ui_font_is_custom() noexcept;

/// Logical pixel size for the current window. Layout uses this; it is not the atlas size.
int ui_text_size(UiRole role) noexcept;

float ui_measure(UiRole role, const char* text);
void ui_draw(UiRole role, float x, float y, const char* text, Color color);

}  // namespace incidentlab
