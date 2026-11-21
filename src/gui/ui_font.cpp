#include "ui_font.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace incidentlab {
namespace {

struct Face {
    Font font{};
    int atlas_px = 0;
    float draw_px = 0.0F;
    float spacing = 1.0F;
    bool owned = false;
};

struct FontSet {
    bool ready = false;
    bool custom = false;
    float pixel_scale = 1.0F;
    std::string regular_path;
    Face regular[4]{};
    Face title_large{};
    Face title_compact{};
};

FontSet& fonts() {
    static FontSet set;
    return set;
}

int framebuffer_width() {
#if RAYLIB_VERSION_MAJOR > 4 || (RAYLIB_VERSION_MAJOR == 4 && RAYLIB_VERSION_MINOR >= 5)
    return GetRenderWidth();
#else
    return GetScreenWidth();
#endif
}

float framebuffer_scale() {
    const int screen = GetScreenWidth();
    const int render = framebuffer_width();
    if (screen <= 0 || render <= screen) {
        return 1.0F;
    }
    return static_cast<float>(render) / static_cast<float>(screen);
}

bool roomy_window() { return GetScreenHeight() >= 820; }

int logical_size(UiRole role) noexcept {
    const bool roomy = roomy_window();
    switch (role) {
    case UiRole::Title:
        return roomy ? 28 : 24;
    case UiRole::Heading:
        return roomy ? 18 : 17;
    case UiRole::Label:
        return roomy ? 17 : 16;
    case UiRole::Value:
        return roomy ? 18 : 17;
    case UiRole::Timeline:
        return roomy ? 16 : 15;
    case UiRole::Button:
        return roomy ? 17 : 16;
    case UiRole::Detail:
        return roomy ? 16 : 15;
    }
    return 16;
}

int atlas_index(int logical) {
    switch (logical) {
    case 15:
        return 0;
    case 16:
        return 1;
    case 17:
        return 2;
    case 18:
        return 3;
    default:
        return 1;
    }
}

const char* regular_candidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
};

std::string bold_path_for(const std::string& regular) {
    const std::string regular_suffix = "-Regular.ttf";
    if (regular.size() > regular_suffix.size() &&
        regular.compare(regular.size() - regular_suffix.size(), regular_suffix.size(),
                        regular_suffix) == 0) {
        return regular.substr(0, regular.size() - regular_suffix.size()) + "-Bold.ttf";
    }
    const std::string sans = "Sans.ttf";
    if (regular.size() > sans.size() &&
        regular.compare(regular.size() - sans.size(), sans.size(), sans) == 0) {
        return regular.substr(0, regular.size() - sans.size()) + "Sans-Bold.ttf";
    }
    return {};
}

bool is_custom_font(const Font& font) {
    return font.glyphCount > 0 && font.texture.id != 0 &&
           font.texture.id != GetFontDefault().texture.id;
}

void release_face(Face& face) {
    if (face.owned) {
        UnloadFont(face.font);
    }
    face = Face{};
}

Face load_face(const char* path, int draw_px, float pixel_scale, bool prefer_point) {
    Face face;
    face.draw_px = static_cast<float>(draw_px);
    face.spacing = 1.0F;
    if (path == nullptr || !FileExists(path)) {
        return face;
    }
    const int atlas = std::max(draw_px, static_cast<int>(std::lround(draw_px * pixel_scale)));
    Font loaded = LoadFontEx(path, atlas, nullptr, 0);
    if (!is_custom_font(loaded)) {
        return face;
    }
    SetTextureFilter(loaded.texture, prefer_point ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    face.font = loaded;
    face.atlas_px = atlas;
    face.owned = true;
    return face;
}

Face default_face(int draw_px) {
    Face face;
    face.font = GetFontDefault();
    face.draw_px = static_cast<float>(draw_px);
    face.spacing = face.draw_px / 10.0F;
    face.owned = false;
    return face;
}

const Face& face_for(UiRole role) {
    FontSet& set = fonts();
    if (!set.ready) {
        static Face fallback = default_face(16);
        fallback.draw_px = static_cast<float>(logical_size(role));
        fallback.spacing = set.custom ? 1.0F : fallback.draw_px / 10.0F;
        return fallback;
    }
    if (role == UiRole::Title) {
        return roomy_window() ? set.title_large : set.title_compact;
    }
    return set.regular[atlas_index(logical_size(role))];
}

float snap_axis(float value) {
    const float scale = std::max(fonts().pixel_scale, 1.0F);
    return std::round(value * scale) / scale;
}

}  // namespace

void load_ui_fonts() {
    FontSet& set = fonts();
    unload_ui_fonts();

    const float pixel_scale = framebuffer_scale();
    const bool sharp_filter = std::abs(pixel_scale - std::round(pixel_scale)) < 0.02F;
    const char* regular = nullptr;
    for (const char* candidate : regular_candidates) {
        if (FileExists(candidate)) {
            regular = candidate;
            break;
        }
    }

    const int body_sizes[] = {15, 16, 17, 18};
    bool custom = regular != nullptr;
    if (custom) {
        for (int index = 0; index < 4; ++index) {
            set.regular[index] = load_face(regular, body_sizes[index], pixel_scale, sharp_filter);
            custom = custom && set.regular[index].owned;
        }
    }

    std::string bold;
    if (custom) {
        bold = bold_path_for(regular);
        const char* title_path = FileExists(bold.c_str()) ? bold.c_str() : regular;
        set.title_compact = load_face(title_path, 24, pixel_scale, sharp_filter);
        set.title_large = load_face(title_path, 28, pixel_scale, sharp_filter);
        custom = set.title_compact.owned && set.title_large.owned;
    }

    if (!custom) {
        unload_ui_fonts();
        for (int index = 0; index < 4; ++index) {
            set.regular[index] = default_face(body_sizes[index]);
        }
        set.title_compact = default_face(24);
        set.title_large = default_face(28);
        set.custom = false;
        set.pixel_scale = 1.0F;
        set.ready = true;
        std::fprintf(stderr,
                     "INCIDENTLAB: no local DejaVu Sans or Liberation Sans font was found. "
                     "Text uses the built-in bitmap font.\n");
        return;
    }

    set.custom = true;
    set.pixel_scale = pixel_scale;
    set.regular_path = regular;
    set.ready = true;
    const Vector2 dpi = GetWindowScaleDPI();
    std::fprintf(stderr, "INCIDENTLAB: text font: %s (raster scale %.2f, window scale %.2f)\n",
                 regular, static_cast<double>(pixel_scale), static_cast<double>(dpi.x));
}

void unload_ui_fonts() {
    FontSet& set = fonts();
    for (Face& face : set.regular) {
        release_face(face);
    }
    release_face(set.title_compact);
    release_face(set.title_large);
    set.ready = false;
    set.custom = false;
    set.pixel_scale = 1.0F;
    set.regular_path.clear();
}

bool ui_font_is_custom() noexcept { return fonts().custom; }

int ui_text_size(UiRole role) noexcept { return logical_size(role); }

float ui_measure(UiRole role, const char* text) {
    const Face& face = face_for(role);
    if (text == nullptr) {
        return 0.0F;
    }
    return MeasureTextEx(face.font, text, face.draw_px, face.spacing).x;
}

void ui_draw(UiRole role, float x, float y, const char* text, Color color) {
    if (text == nullptr) {
        return;
    }
    const Face& face = face_for(role);
    DrawTextEx(face.font, text, Vector2{snap_axis(x), snap_axis(y)}, face.draw_px, face.spacing,
               color);
}

}  // namespace incidentlab
