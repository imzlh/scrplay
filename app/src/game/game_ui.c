#include "game_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "cimgui.h"
#include "game.h"
#include "game_templates.h"
#include "imgui_shim.h"
#include "screen.h"
#include "util/log.h"

#define SC_GAME_PANEL_WIDTH_BASE 340.0f

// ============================================================================
// Coordinate transforms
// ============================================================================

// normalized device coords [0..1] -> window coords (points)
static void
game_norm_to_window(struct sc_game *game, float nx, float ny, float *wx,
                    float *wy) {
    struct sc_screen *screen = game->screen;
    struct sc_screen_fpoint frame = {
        .x = nx * screen->frame_size.width,
        .y = ny * screen->frame_size.height,
    };
    struct sc_screen_fpoint window = {0.0f, 0.0f};
    sc_screen_frame_to_window_point_f(screen, frame, &window);
    if (wx) {
        *wx = window.x;
    }
    if (wy) {
        *wy = window.y;
    }
}

// Profile radii are lengths relative to the *unrotated frame height*. They
// must not go through game_norm_to_window(), which transforms positions and
// therefore also adds the letterbox offset (and breaks completely at 90/270
// degrees). Convert the length through the content and HiDPI scales only.
static float
norm_length_to_window_points(struct sc_game *game, float length) {
    struct sc_screen *screen = game->screen;
    struct sc_screen_fpoint frame = {
        .x = 0.0f,
        .y = length * screen->frame_size.height,
    };
    struct sc_screen_fpoint window;
    if (!sc_screen_frame_to_window_vector_f(screen, frame, &window)) {
        return 0.0f;
    }
    return hypotf(window.x, window.y);
}

// window coords (points) -> normalized device coords
//
// Pass window points straight to sc_screen_convert_window_to_frame_coords():
// it already applies the HiDPI scale internally. Scaling here beforehand
// would apply it twice.
static void
game_window_to_norm(struct sc_game *game, float wx, float wy, float *nx,
                    float *ny) {
    struct sc_screen *screen = game->screen;
    struct sc_screen_fpoint frame = {0.0f, 0.0f};
    sc_screen_window_to_frame_point_f(
        screen, (struct sc_screen_fpoint) {.x = wx, .y = wy}, &frame);
    *nx = frame.x / screen->frame_size.width;
    *ny = frame.y / screen->frame_size.height;
}

// ============================================================================
// Small helpers and palette
// ============================================================================

static ImVec2
vec2(float x, float y) {
    ImVec2 v = {x, y};
    return v;
}

static ImU32
rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // packed RGBA (the Dear ImGui default, no IMGUI_USE_BGRA_PACKED_COLOR)
    return ((ImU32) a << 24) | ((ImU32) b << 16) | ((ImU32) g << 8) | r;
}

static float
minf(float a, float b) {
    return a < b ? a : b;
}

static float
maxf(float a, float b) {
    return a > b ? a : b;
}

static float
clampf(float v, float min, float max) {
    return v < min ? min : (v > max ? max : v);
}

#define ACCENT rgba(60, 200, 235, 255)     // cyan
#define ACCENT_DIM rgba(60, 200, 235, 90)
#define GOOD rgba(70, 215, 125, 255)       // green
#define WARN rgba(245, 170, 60, 255)       // orange
#define BAD rgba(235, 85, 85, 255)         // red
#define PURPLE rgba(180, 130, 245, 255)
#define TEXT_MAIN rgba(235, 240, 248, 255)
#define SELECTED rgba(80, 220, 120, 255)   // selection green
#define ACCENT_V4 ((ImVec4) {0.24f, 0.78f, 0.92f, 1.0f}) // ACCENT as ImVec4

static struct sc_game_preset *
current_preset(struct sc_game *game) {
    if (!game->profile.preset_count) {
        return NULL;
    }
    if (game->profile.current_preset >= game->profile.preset_count) {
        game->profile.current_preset = 0;
    }
    return &game->profile.presets[game->profile.current_preset];
}

// ============================================================================
// Vector icons (drawn with the ImGui draw list; no icon font needed)
// ============================================================================

enum sc_game_icon {
    SC_ICON_PLUS,
    SC_ICON_PENCIL,
    SC_ICON_JOY,
    SC_ICON_BTN,
    SC_ICON_SWIPE,
    SC_ICON_SAVE,
    SC_ICON_PLAY,
    SC_ICON_TRASH,
    SC_ICON_AIM,
    SC_ICON_CROP,
    SC_ICON_BACK,
    SC_ICON_MENU,
};

static void
draw_icon(ImDrawList *dl, int icon, ImVec2 c, float s, ImU32 col) {
    float h = s / 2;
    // stroke widths track the glyph size so icons stay crisp at any scale
    // (toolbar glyphs are s ~= 17.6*scale: ~2.0/1.5/2.5 px at scale 1)
    float lw = s * 0.115f;      // regular stroke (matches old fixed 2.0f)
    float lw_thin = s * 0.085f; // thin stroke (matches old fixed 1.5f)
    float lw_bold = s * 0.142f; // bold stroke (matches old fixed 2.5f)
    switch (icon) {
        case SC_ICON_PLUS:
            ImDrawList_AddRectFilled(dl, vec2(c.x - h, c.y - s * 0.12f),
                                     vec2(c.x + h, c.y + s * 0.12f), col,
                                     1.5f, 0);
            ImDrawList_AddRectFilled(dl, vec2(c.x - s * 0.12f, c.y - h),
                                     vec2(c.x + s * 0.12f, c.y + h), col,
                                     1.5f, 0);
            break;
        case SC_ICON_PENCIL: {
            // diagonal shaft + tip at the lower left
            ImVec2 a = vec2(c.x - h * 0.65f, c.y + h * 0.65f);
            ImVec2 b = vec2(c.x + h * 0.55f, c.y - h * 0.55f);
            ImDrawList_AddLine(dl, a, b, col, s * 0.125f);
            ImDrawList_AddTriangleFilled(dl, a,
                                         vec2(a.x + s * 0.22f, a.y),
                                         vec2(a.x, a.y - s * 0.22f), col);
            break;
        }
        case SC_ICON_JOY:
            ImDrawList_AddCircle(dl, c, h, col, 24, lw);
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y), vec2(c.x + h, c.y), col,
                               lw_thin);
            ImDrawList_AddLine(dl, vec2(c.x, c.y - h), vec2(c.x, c.y + h), col,
                               lw_thin);
            ImDrawList_AddCircleFilled(dl, c, s * 0.14f, col, 12);
            break;
        case SC_ICON_BTN:
            ImDrawList_AddRect(dl, vec2(c.x - h, c.y - h * 0.8f),
                               vec2(c.x + h, c.y + h * 0.8f), col, 4.0f, 0,
                               lw);
            ImDrawList_AddCircleFilled(dl, c, s * 0.12f, col, 12);
            break;
        case SC_ICON_SWIPE: {
            ImVec2 a = vec2(c.x - h, c.y + h * 0.5f);
            ImVec2 b = vec2(c.x + h, c.y - h * 0.5f);
            ImDrawList_AddLine(dl, a, b, col, lw);
            ImVec2 t1 = vec2(b.x, b.y + s * 0.28f);
            ImVec2 t2 = vec2(b.x - s * 0.28f, b.y);
            ImDrawList_AddTriangleFilled(dl, b, t1, t2, col);
            break;
        }
        case SC_ICON_SAVE:
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y + h * 0.6f),
                               vec2(c.x + h, c.y + h * 0.6f), col, lw);
            ImDrawList_AddLine(dl, vec2(c.x, c.y - h * 0.7f),
                               vec2(c.x, c.y + h * 0.3f), col, lw_bold);
            ImDrawList_AddTriangleFilled(dl, vec2(c.x - h * 0.45f, c.y),
                                         vec2(c.x + h * 0.45f, c.y),
                                         vec2(c.x, c.y + h * 0.55f), col);
            break;
        case SC_ICON_PLAY:
            ImDrawList_AddTriangleFilled(dl, vec2(c.x - h * 0.7f, c.y - h),
                                         vec2(c.x - h * 0.7f, c.y + h),
                                         vec2(c.x + h, c.y), col);
            break;
        case SC_ICON_TRASH:
            ImDrawList_AddRect(dl, vec2(c.x - h * 0.6f, c.y - h * 0.4f),
                               vec2(c.x + h * 0.6f, c.y + h), col, 2.0f, 0,
                               lw);
            ImDrawList_AddLine(dl, vec2(c.x - h * 0.8f, c.y - h * 0.55f),
                               vec2(c.x + h * 0.8f, c.y - h * 0.55f), col,
                               lw_bold);
            break;
        case SC_ICON_AIM:
            ImDrawList_AddCircle(dl, c, h * 0.8f, col, 24, lw);
            ImDrawList_AddLine(dl, vec2(c.x, c.y - h), vec2(c.x, c.y - h * 0.3f),
                               col, lw);
            ImDrawList_AddLine(dl, vec2(c.x, c.y + h * 0.3f),
                               vec2(c.x, c.y + h), col, lw);
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y), vec2(c.x - h * 0.3f, c.y),
                               col, lw);
            ImDrawList_AddLine(dl, vec2(c.x + h * 0.3f, c.y),
                               vec2(c.x + h, c.y), col, lw);
            ImDrawList_AddCircleFilled(dl, c, s * 0.08f, col, 8);
            break;
        case SC_ICON_CROP: {
            // Material-style crop glyph: two open, offset corners. Keep the
            // center empty so it remains legible over a moving video frame.
            float q = h * 0.58f;
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y - q),
                               vec2(c.x + q, c.y - q), col, lw);
            ImDrawList_AddLine(dl, vec2(c.x - q, c.y - h),
                               vec2(c.x - q, c.y + q), col, lw);
            ImDrawList_AddLine(dl, vec2(c.x - q, c.y + q),
                               vec2(c.x + h, c.y + q), col, lw);
            ImDrawList_AddLine(dl, vec2(c.x + q, c.y - q),
                               vec2(c.x + q, c.y + h), col, lw);
            break;
        }
        case SC_ICON_BACK:
            ImDrawList_AddTriangleFilled(dl, vec2(c.x - h * 0.4f, c.y),
                                         vec2(c.x + h * 0.3f, c.y - h * 0.7f),
                                         vec2(c.x + h * 0.3f, c.y + h * 0.7f),
                                         col);
            ImDrawList_AddLine(dl, vec2(c.x + h * 0.1f, c.y),
                               vec2(c.x + h * 0.9f, c.y), col, lw_bold);
            break;
        case SC_ICON_MENU:
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y - h * 0.55f),
                               vec2(c.x + h, c.y - h * 0.55f), col, lw_bold);
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y), vec2(c.x + h, c.y), col,
                               lw_bold);
            ImDrawList_AddLine(dl, vec2(c.x - h, c.y + h * 0.55f),
                               vec2(c.x + h, c.y + h * 0.55f), col, lw_bold);
            break;
        default:
            break;
    }
}

// Icon button with tooltip. Returns true when clicked.
static bool
icon_button(const char *id, int icon, const char *tooltip, float size,
            ImU32 col) {
    ImVec2 sz = vec2(size, size);
    bool clicked = igInvisibleButton(id, sz, ImGuiButtonFlags_None);
    ImDrawList *dl = igGetWindowDrawList();
    ImVec2 p_min;
    ImVec2 p_max;
    igGetItemRectMin(&p_min);
    igGetItemRectMax(&p_max);
    ImVec2 c = vec2((p_min.x + p_max.x) / 2, (p_min.y + p_max.y) / 2);
    bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
    bool active = igIsItemActive();
    if (active || hovered) {
        ImU32 bg = active ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 20);
        ImDrawList_AddRectFilled(dl, p_min, vec2(p_min.x + size,
                                                 p_min.y + size), bg,
                                 size * 0.2f, 0);
    }
    draw_icon(dl, icon, c, size * 0.55f, col);
    if (hovered && tooltip) {
        igSetTooltip(tooltip);
    }
    return clicked;
}

// ============================================================================
// Ghost widgets (pure draw-list output, zero ImGui items)
// ============================================================================

// Element anchor + hit radius/extent, in window points. Swipe endpoints use
// their own handles below, so this helper only supplies its visual fallback.
static bool
element_anchor_pts(struct sc_game *game, const struct sc_game_element *elem,
                   float *ax, float *ay, float *hit_r) {
    if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
        game_norm_to_window(game, elem->joystick.center.x,
                            elem->joystick.center.y, ax, ay);
        float r = norm_length_to_window_points(game, elem->joystick.radius);
        *hit_r = r;
        return r > 0;
    }
    if (elem->type == SC_GAME_ELEMENT_BIND) {
        game_norm_to_window(game, elem->bind.center.x, elem->bind.center.y,
                            ax, ay);
        float s = norm_length_to_window_points(game, elem->bind.size);
        *hit_r = s;
        return s > 0;
    }
    if (elem->type == SC_GAME_ELEMENT_AIM) {
        game_norm_to_window(game, elem->aim.start.x, elem->aim.start.y,
                            ax, ay);
        // Aim is a single touch origin, not an activity region. Keep a fixed
        // generous target so it remains easy to grab at any resolution.
        *hit_r = 24.0f * game->ui.scale;
        return true;
    }
    // swipe: anchor = "from" endpoint
    game_norm_to_window(game, elem->swipe.from.x, elem->swipe.from.y, ax, ay);
    *hit_r = 16.0f * game->ui.scale;
    return true;
}

// Single-letter preview of a bound key, for the joystick WASD markers.
static char
key_letter(const struct sc_game_key *key) {
    if (key->type == SC_GAME_KEY_KEYBOARD && key->value >= 'a'
            && key->value <= 'z') {
        return (char) (key->value - 'a' + 'A');
    }
    return 0;
}

static void
draw_ghost_visual(struct sc_game_ui *ui, struct sc_game *game,
                  const struct sc_game_element *elem, bool edit_mode,
                  bool selected, bool hovered) {
    ImDrawList *dl = igGetBackgroundDrawList_Nil();
    bool highlight = edit_mode && (selected || hovered);
    ImU32 border = highlight ? SELECTED
                             : rgba(255, 255, 255, edit_mode ? 190 : 70);
    ImU32 fill = highlight ? rgba(80, 220, 120, 40)
                           : rgba(255, 255, 255, edit_mode ? 28 : 22);
    ImU32 text_col = rgba(255, 255, 255, edit_mode ? 240 : 90);

    if (elem->type == SC_GAME_ELEMENT_SWIPE) {
        float fx, fy, tx, ty;
        game_norm_to_window(game, elem->swipe.from.x, elem->swipe.from.y,
                            &fx, &fy);
        game_norm_to_window(game, elem->swipe.to.x, elem->swipe.to.y, &tx, &ty);
        float r = 16.0f * ui->scale;
        bool from_hot = hovered
                     && ui->hover_handle == SC_GAME_UI_HANDLE_SWIPE_FROM;
        bool to_hot = hovered
                   && ui->hover_handle == SC_GAME_UI_HANDLE_SWIPE_TO;
        bool line_hot = hovered
                     && ui->hover_handle == SC_GAME_UI_HANDLE_SWIPE_WHOLE;
        ImU32 from_border = from_hot ? SELECTED : GOOD;
        ImU32 to_border = to_hot ? SELECTED : ACCENT;
        ImU32 line_col = line_hot ? SELECTED : border;

        ImDrawList_AddLine(dl, vec2(fx, fy), vec2(tx, ty), line_col,
                           (line_hot ? 4.0f : 3.0f) * ui->scale);
        ImDrawList_AddCircleFilled(dl, vec2(fx, fy), r,
                                   rgba(70, 215, 125, 52), 28);
        ImDrawList_AddCircle(dl, vec2(fx, fy), r, from_border, 28,
                             (from_hot ? 3.0f : 2.0f) * ui->scale);
        ImDrawList_AddCircleFilled(dl, vec2(tx, ty), r,
                                   rgba(60, 200, 235, 52), 28);
        ImDrawList_AddCircle(dl, vec2(tx, ty), r, to_border, 28,
                             (to_hot ? 3.0f : 2.0f) * ui->scale);

        // Short endpoint labels make the two independent handles obvious.
        ImVec2 from_text;
        ImVec2 to_text;
        igCalcTextSize(&from_text, "起", NULL, false, -1.0f);
        igCalcTextSize(&to_text, "终", NULL, false, -1.0f);
        ImDrawList_AddText_Vec2(
            dl, vec2(fx - from_text.x * 0.5f, fy - from_text.y * 0.5f),
            TEXT_MAIN, "起", NULL);
        ImDrawList_AddText_Vec2(
            dl, vec2(tx - to_text.x * 0.5f, ty - to_text.y * 0.5f),
            TEXT_MAIN, "终", NULL);

        ImVec2 ts;
        igCalcTextSize(&ts, elem->label, NULL, false, -1.0f);
        ImDrawList_AddText_Vec2(dl, vec2((fx + tx) / 2 - ts.x / 2,
                                         (fy + ty) / 2 - ts.y
                                             - 6.0f * ui->scale), text_col,
                                elem->label, NULL);
        return;
    }

    float cx, cy, r;
    element_anchor_pts(game, elem, &cx, &cy, &r);
    if (r <= 0) {
        return;
    }

    if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
        ImDrawList_AddCircleFilled(dl, vec2(cx, cy), r, fill, 48);
        ImDrawList_AddCircle(dl, vec2(cx, cy), r, border, 48,
                             2.0f * ui->scale);
        ImDrawList_AddLine(dl, vec2(cx - r, cy), vec2(cx + r, cy),
                           rgba(255, 255, 255, edit_mode ? 90 : 40),
                           1.0f * ui->scale);
        ImDrawList_AddLine(dl, vec2(cx, cy - r), vec2(cx, cy + r),
                           rgba(255, 255, 255, edit_mode ? 90 : 40),
                           1.0f * ui->scale);
        // direction key markers (edit mode)
        if (edit_mode) {
            ImU32 kcol = rgba(255, 255, 255, 170);
            char up = key_letter(&elem->joystick.up);
            char down = key_letter(&elem->joystick.down);
            char left = key_letter(&elem->joystick.left);
            char right = key_letter(&elem->joystick.right);
            float o = r * 0.62f;
            char s[2] = {0, 0};
            if (up) { s[0] = up; ImDrawList_AddText_Vec2(dl, vec2(cx - 5, cy - o - 7), kcol, s, NULL); }
            if (down) { s[0] = down; ImDrawList_AddText_Vec2(dl, vec2(cx - 5, cy + o - 7), kcol, s, NULL); }
            if (left) { s[0] = left; ImDrawList_AddText_Vec2(dl, vec2(cx - o - 5, cy - 7), kcol, s, NULL); }
            if (right) { s[0] = right; ImDrawList_AddText_Vec2(dl, vec2(cx + o - 5, cy - 7), kcol, s, NULL); }
        }
    } else if (elem->type == SC_GAME_ELEMENT_AIM) {
        // One fixed-size home handle. It intentionally does not imply a
        // bounded activity area: gameplay keeps one continuous touch and
        // only returns here when it reaches a screen edge.
        float handle_r = 13.0f * ui->scale;
        float ch = 18.0f * ui->scale;
        ImU32 ccol = rgba(60, 200, 235, edit_mode ? 220 : 90);
        ImDrawList_AddCircleFilled(dl, vec2(cx, cy), handle_r, fill, 28);
        ImDrawList_AddCircle(dl, vec2(cx, cy), handle_r, border, 28,
                             2.0f * ui->scale);
        ImDrawList_AddLine(dl, vec2(cx - ch, cy), vec2(cx + ch, cy), ccol,
                           2.0f * ui->scale);
        ImDrawList_AddLine(dl, vec2(cx, cy - ch), vec2(cx, cy + ch), ccol,
                           2.0f * ui->scale);
        ImDrawList_AddCircleFilled(dl, vec2(cx, cy), 3.0f * ui->scale, ccol,
                                   16);
    } else {
        ImVec2 p_min = vec2(cx - r, cy - r);
        ImVec2 p_max = vec2(cx + r, cy + r);
        ImDrawList_AddRectFilled(dl, p_min, p_max, fill, 6.0f, 0);
        ImDrawList_AddRect(dl, p_min, p_max, border, 6.0f * ui->scale, 0,
                           2.0f * ui->scale);
    }

    ImVec2 ts;
    igCalcTextSize(&ts, elem->label, NULL, false, -1.0f);
    float label_y = elem->type == SC_GAME_ELEMENT_AIM
                  ? cy + 22.0f * ui->scale
                  : cy - ts.y / 2;
    ImDrawList_AddText_Vec2(dl, vec2(cx - ts.x / 2, label_y), text_col,
                            elem->label, NULL);
}

static void
draw_ghosts(struct sc_game_ui *ui, struct sc_game *game, bool edit_mode) {
    struct sc_game_preset *preset = current_preset(game);
    struct sc_screen *screen = game->screen;
    if (!preset || !screen || screen->rect.w <= 0 || screen->rect.h <= 0) {
        return;
    }

    // screen->rect is expressed in renderer-output pixels, whereas ImGui
    // draw lists use logical window points. Clip every ghost to the visible
    // video rectangle so cropped-out mappings never spill into black bars.
    int ww;
    int wh;
    int dw;
    int dh;
    SDL_GetWindowSize(screen->window, &ww, &wh);
    if (SDL_GetRendererOutputSize(screen->display.renderer, &dw, &dh)
            || ww <= 0 || wh <= 0 || dw <= 0 || dh <= 0) {
        return;
    }
    ImVec2 clip_min = vec2((float) screen->rect.x * ww / dw,
                           (float) screen->rect.y * wh / dh);
    ImVec2 clip_max = vec2((float) (screen->rect.x + screen->rect.w) * ww / dw,
                           (float) (screen->rect.y + screen->rect.h) * wh / dh);
    ImDrawList *dl = igGetBackgroundDrawList_Nil();
    ImDrawList_PushClipRect(dl, clip_min, clip_max, true);
    for (size_t i = 0; i < preset->element_count; ++i) {
        draw_ghost_visual(ui, game, &preset->elements[i], edit_mode,
                          edit_mode && (int) i == ui->selected_element,
                          edit_mode && (int) i == ui->hover_elem);
    }
    ImDrawList_PopClipRect(dl);
}

// ============================================================================
// Ghost interaction: manual hit-testing against the ImGui mouse state.
// Runs before the windows are drawn; ghosts never create items, so a click
// belongs to a ghost iff it is not over any UI window (WantCaptureMouse).
// ============================================================================

static float
point_segment_distance_sq(float px, float py, float ax, float ay, float bx,
                          float by) {
    float abx = bx - ax;
    float aby = by - ay;
    float denom = abx * abx + aby * aby;
    float t = denom > 0.0f
            ? ((px - ax) * abx + (py - ay) * aby) / denom
            : 0.0f;
    t = clampf(t, 0.0f, 1.0f);
    float dx = px - (ax + t * abx);
    float dy = py - (ay + t * aby);
    return dx * dx + dy * dy;
}

static enum sc_game_ui_drag_handle
ghost_hit_test(struct sc_game *game, const struct sc_game_element *elem,
               float mx, float my) {
    if (elem->type == SC_GAME_ELEMENT_SWIPE) {
        float fx, fy, tx, ty;
        game_norm_to_window(game, elem->swipe.from.x, elem->swipe.from.y,
                            &fx, &fy);
        game_norm_to_window(game, elem->swipe.to.x, elem->swipe.to.y,
                            &tx, &ty);
        float hit_r = 28.0f * game->ui.scale;
        float fdx = mx - fx;
        float fdy = my - fy;
        float tdx = mx - tx;
        float tdy = my - ty;
        float from_d2 = fdx * fdx + fdy * fdy;
        float to_d2 = tdx * tdx + tdy * tdy;
        float hit_r2 = hit_r * hit_r;
        if (from_d2 <= hit_r2 || to_d2 <= hit_r2) {
            return from_d2 <= to_d2 ? SC_GAME_UI_HANDLE_SWIPE_FROM
                                    : SC_GAME_UI_HANDLE_SWIPE_TO;
        }
        float line_r = 11.0f * game->ui.scale;
        if (point_segment_distance_sq(mx, my, fx, fy, tx, ty)
                <= line_r * line_r) {
            return SC_GAME_UI_HANDLE_SWIPE_WHOLE;
        }
        return SC_GAME_UI_HANDLE_NONE;
    }

    float ax, ay, r;
    if (!element_anchor_pts(game, elem, &ax, &ay, &r)) {
        return SC_GAME_UI_HANDLE_NONE;
    }
    float dx = mx - ax;
    float dy = my - ay;
    float min_r = 18.0f * game->ui.scale; // always grabbable, even when small
    if (r < min_r) {
        r = min_r;
    }
    if (elem->type == SC_GAME_ELEMENT_BIND) {
        return dx >= -r && dx <= r && dy >= -r && dy <= r
             ? SC_GAME_UI_HANDLE_ELEMENT : SC_GAME_UI_HANDLE_NONE;
    }
    return dx * dx + dy * dy <= r * r
         ? SC_GAME_UI_HANDLE_ELEMENT : SC_GAME_UI_HANDLE_NONE;
}

static float
clamp01(float v) {
    return clampf(v, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX);
}

// Keep the working model valid while InputText is active. The edit buffer may
// temporarily contain leading/trailing/repeated whitespace, but only this
// normalized representation is committed to the profile.
static bool
normalize_element_label(const char *input,
                        char output[SC_GAME_ELEMENT_LABEL_MAX]) {
    size_t out = 0;
    bool pending_space = false;
    for (const unsigned char *p = (const unsigned char *) input; *p; ++p) {
        unsigned char c = *p;
        bool whitespace = c == ' ' || c == '\t' || c == '\n'
                       || c == '\r' || c == '\v' || c == '\f';
        if (whitespace) {
            pending_space = out > 0;
            continue;
        }
        if (c < 0x20 || c == 0x7f) {
            continue;
        }
        if (pending_space && out + 1 < SC_GAME_ELEMENT_LABEL_MAX) {
            output[out++] = ' ';
        }
        pending_space = false;
        if (out + 1 < SC_GAME_ELEMENT_LABEL_MAX) {
            output[out++] = (char) c;
        }
    }
    output[out] = '\0';
    return out > 0;
}

static void
update_ghost_interaction(struct sc_game_ui *ui, struct sc_game *game) {
    ImGuiIO *io = igGetIO_Nil();
    struct sc_game_preset *preset = current_preset(game);
    ui->hover_elem = -1;
    ui->hover_handle = SC_GAME_UI_HANDLE_NONE;
    if (!preset) {
        ui->drag_active = false;
        ui->drag_handle = SC_GAME_UI_HANDLE_NONE;
        return;
    }

    float mx = io->MousePos.x;
    float my = io->MousePos.y;

    // Endpoint handles are small, intentional targets. Give them priority
    // over joystick/button regions and the aim handle, and keep the selected
    // swipe first when multiple controls overlap. This makes both endpoints
    // reliably reachable without changing the normal topmost-element rule.
    if (ui->selected_element >= 0
            && ui->selected_element < (int) preset->element_count) {
        struct sc_game_element *selected =
            &preset->elements[ui->selected_element];
        if (selected->type == SC_GAME_ELEMENT_SWIPE) {
            enum sc_game_ui_drag_handle handle =
                ghost_hit_test(game, selected, mx, my);
            if (handle == SC_GAME_UI_HANDLE_SWIPE_FROM
                    || handle == SC_GAME_UI_HANDLE_SWIPE_TO) {
                ui->hover_elem = ui->selected_element;
                ui->hover_handle = handle;
            }
        }
    }
    if (ui->hover_elem < 0) {
        for (size_t i = preset->element_count; i-- > 0;) {
            struct sc_game_element *elem = &preset->elements[i];
            if (elem->type != SC_GAME_ELEMENT_SWIPE) {
                continue;
            }
            enum sc_game_ui_drag_handle handle =
                ghost_hit_test(game, elem, mx, my);
            if (handle == SC_GAME_UI_HANDLE_SWIPE_FROM
                    || handle == SC_GAME_UI_HANDLE_SWIPE_TO) {
                ui->hover_elem = (int) i;
                ui->hover_handle = handle;
                break;
            }
        }
    }
    if (ui->hover_elem < 0) {
        // Remaining hit regions follow visual stacking: last drawn is first.
        for (size_t i = preset->element_count; i-- > 0;) {
            enum sc_game_ui_drag_handle handle =
                ghost_hit_test(game, &preset->elements[i], mx, my);
            if (handle != SC_GAME_UI_HANDLE_NONE) {
                ui->hover_elem = (int) i;
                ui->hover_handle = handle;
                break;
            }
        }
    }

    bool over_canvas = !io->WantCaptureMouse && !io->WantTextInput;

    // press: select and start dragging
    if (over_canvas && io->MouseClicked[0]) {
        if (ui->hover_elem >= 0) {
            ui->drag_active = true;
            ui->drag_elem = ui->hover_elem;
            ui->drag_handle = ui->hover_handle;
            ui->selected_element = ui->hover_elem;
            float nx, ny;
            game_window_to_norm(game, mx, my, &nx, &ny);
            struct sc_game_element *elem = &preset->elements[ui->drag_elem];
            float axn, ayn;
            if (elem->type == SC_GAME_ELEMENT_SWIPE) {
                bool endpoint_to = ui->drag_handle
                                 == SC_GAME_UI_HANDLE_SWIPE_TO;
                axn = endpoint_to ? elem->swipe.to.x : elem->swipe.from.x;
                ayn = endpoint_to ? elem->swipe.to.y : elem->swipe.from.y;
            } else if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
                axn = elem->joystick.center.x;
                ayn = elem->joystick.center.y;
            } else if (elem->type == SC_GAME_ELEMENT_AIM) {
                axn = elem->aim.start.x;
                ayn = elem->aim.start.y;
            } else {
                axn = elem->bind.center.x;
                ayn = elem->bind.center.y;
            }
            ui->drag_grab_nx = nx - axn;
            ui->drag_grab_ny = ny - ayn;
        } else {
            ui->selected_element = -1;
        }
    }

    // drag: move the anchor with the mouse
    if (ui->drag_active) {
        if (!io->MouseDown[0]) {
            ui->drag_active = false;
            ui->drag_handle = SC_GAME_UI_HANDLE_NONE;
        } else if (ui->drag_elem >= 0
                && ui->drag_elem < (int) preset->element_count) {
            struct sc_game_element *elem = &preset->elements[ui->drag_elem];
            float nx, ny;
            game_window_to_norm(game, mx, my, &nx, &ny);
            nx = clamp01(nx - ui->drag_grab_nx);
            ny = clamp01(ny - ui->drag_grab_ny);
            if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
                elem->joystick.center.x = nx;
                elem->joystick.center.y = ny;
            } else if (elem->type == SC_GAME_ELEMENT_BIND) {
                elem->bind.center.x = nx;
                elem->bind.center.y = ny;
            } else if (elem->type == SC_GAME_ELEMENT_AIM) {
                elem->aim.start.x = nx;
                elem->aim.start.y = ny;
            } else if (elem->type == SC_GAME_ELEMENT_SWIPE) {
                if (ui->drag_handle == SC_GAME_UI_HANDLE_SWIPE_FROM) {
                    elem->swipe.from.x = nx;
                    elem->swipe.from.y = ny;
                } else if (ui->drag_handle == SC_GAME_UI_HANDLE_SWIPE_TO) {
                    elem->swipe.to.x = nx;
                    elem->swipe.to.y = ny;
                } else {
                    float dx = nx - elem->swipe.from.x;
                    float dy = ny - elem->swipe.from.y;
                    elem->swipe.from.x = nx;
                    elem->swipe.from.y = ny;
                    elem->swipe.to.x = clamp01(elem->swipe.to.x + dx);
                    elem->swipe.to.y = clamp01(elem->swipe.to.y + dy);
                }
            }
        }
    }

    // wheel: resize the hovered element
    if (over_canvas && ui->hover_elem >= 0 && io->MouseWheel != 0.0f) {
        struct sc_game_element *elem = &preset->elements[ui->hover_elem];
        float d = io->MouseWheel * 0.004f;
        if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
            elem->joystick.radius = clampf(elem->joystick.radius + d,
                                           SC_GAME_JOYSTICK_RADIUS_MIN,
                                           SC_GAME_JOYSTICK_RADIUS_MAX);
        } else if (elem->type == SC_GAME_ELEMENT_BIND) {
            elem->bind.size = clampf(elem->bind.size + d,
                                     SC_GAME_BIND_SIZE_MIN,
                                     SC_GAME_BIND_SIZE_MAX);
        }
    }
}

// ============================================================================
// Crop editor: full-frame preview with album-style handles
// ============================================================================

struct sc_game_crop_view {
    ImVec2 full_min;
    ImVec2 full_max;
    ImVec2 crop_min;
    ImVec2 crop_max;
    ImVec2 frame_handles[8];
    // Interaction points may be pulled inward from the toolbar/viewport edge
    // while frame_handles keep the exact crop geometry for drawing.
    ImVec2 handles[8];
};

static void
crop_apply_preview(struct sc_game_ui *ui, bool preview) {
    struct sc_game *game = ui->game;
    struct sc_screen *screen = game ? game->screen : NULL;
    if (!screen || !screen->video) {
        return;
    }
    const struct sc_game_crop *crop = &game->profile.crop;
    sc_screen_set_crop(screen, crop->enabled, crop->left, crop->top,
                       crop->right, crop->bottom, preview);
}

static void
crop_set_editing(struct sc_game_ui *ui, bool editing) {
    if (ui->crop_editing == editing) {
        if (editing) {
            crop_apply_preview(ui, true);
        }
        return;
    }

    ui->crop_editing = editing;
    ui->crop_drag_active = false;
    ui->crop_hover_handle = SC_GAME_UI_CROP_HANDLE_NONE;
    ui->crop_drag_handle = SC_GAME_UI_CROP_HANDLE_NONE;
    ui->drag_active = false;
    ui->drag_handle = SC_GAME_UI_HANDLE_NONE;
    ui->hover_elem = -1;
    ui->selected_element = -1;
    if (editing) {
        ui->panel_open = false;
        ui->crop_profile_revision = ui->game->profile_revision;
        sc_game_cancel_binding(ui->game);
    }
    crop_apply_preview(ui, editing);
}

static void
crop_point_to_window(struct sc_game *game, float x, float y, ImVec2 *out) {
    game_norm_to_window(game, x, y, &out->x, &out->y);
}

static bool
crop_get_view(struct sc_game *game, struct sc_game_crop_view *view) {
    struct sc_screen *screen = game->screen;
    if (!screen || !screen->frame_size.width || !screen->frame_size.height
            || screen->rect.w <= 0 || screen->rect.h <= 0) {
        return false;
    }

    const struct sc_game_crop *crop = &game->profile.crop;
    float left = crop->enabled ? crop->left : 0.0f;
    float top = crop->enabled ? crop->top : 0.0f;
    float right = crop->enabled ? crop->right : 1.0f;
    float bottom = crop->enabled ? crop->bottom : 1.0f;

    ImVec2 full[4];
    crop_point_to_window(game, 0.0f, 0.0f, &full[0]);
    crop_point_to_window(game, 1.0f, 0.0f, &full[1]);
    crop_point_to_window(game, 0.0f, 1.0f, &full[2]);
    crop_point_to_window(game, 1.0f, 1.0f, &full[3]);
    view->full_min = view->full_max = full[0];
    for (size_t i = 1; i < ARRAY_LEN(full); ++i) {
        view->full_min.x = minf(view->full_min.x, full[i].x);
        view->full_min.y = minf(view->full_min.y, full[i].y);
        view->full_max.x = maxf(view->full_max.x, full[i].x);
        view->full_max.y = maxf(view->full_max.y, full[i].y);
    }

    float mid_x = (left + right) * 0.5f;
    float mid_y = (top + bottom) * 0.5f;
    // Corners first, then raw-frame left/right/top/bottom midpoints. The raw
    // identity is retained even if rotation makes (for example) LEFT appear
    // at the top of the window.
    crop_point_to_window(game, left, top, &view->frame_handles[0]);
    crop_point_to_window(game, right, top, &view->frame_handles[1]);
    crop_point_to_window(game, left, bottom, &view->frame_handles[2]);
    crop_point_to_window(game, right, bottom, &view->frame_handles[3]);
    crop_point_to_window(game, left, mid_y, &view->frame_handles[4]);
    crop_point_to_window(game, right, mid_y, &view->frame_handles[5]);
    crop_point_to_window(game, mid_x, top, &view->frame_handles[6]);
    crop_point_to_window(game, mid_x, bottom, &view->frame_handles[7]);

    view->crop_min = view->crop_max = view->frame_handles[0];
    for (size_t i = 1; i < 4; ++i) {
        view->crop_min.x = minf(view->crop_min.x,
                                view->frame_handles[i].x);
        view->crop_min.y = minf(view->crop_min.y,
                                view->frame_handles[i].y);
        view->crop_max.x = maxf(view->crop_max.x,
                                view->frame_handles[i].x);
        view->crop_max.y = maxf(view->crop_max.y,
                                view->frame_handles[i].y);
    }
    float toolbar_floor = 46.0f * game->ui.scale + 9.0f * game->ui.scale;
    float edge_pad = 9.0f * game->ui.scale;
    float min_x = view->full_min.x + edge_pad;
    float max_x = view->full_max.x - edge_pad;
    float max_y = view->full_max.y - edge_pad;
    if (max_x < min_x) {
        min_x = max_x = (view->full_min.x + view->full_max.x) * 0.5f;
    }
    float min_y = maxf(view->full_min.y + edge_pad, toolbar_floor);
    if (max_y < min_y) {
        min_y = max_y = maxf(view->full_min.y,
                              (view->full_min.y + view->full_max.y) * 0.5f);
    }
    for (size_t i = 0; i < ARRAY_LEN(view->handles); ++i) {
        view->handles[i].x = clampf(view->frame_handles[i].x, min_x, max_x);
        view->handles[i].y = clampf(view->frame_handles[i].y, min_y, max_y);
    }
    return view->full_max.x > view->full_min.x
        && view->full_max.y > view->full_min.y;
}

static enum sc_game_ui_crop_handle
crop_hit_test(const struct sc_game_crop_view *view, float mx, float my,
              float scale) {
    static const enum sc_game_ui_crop_handle handles[8] = {
        SC_GAME_UI_CROP_HANDLE_TOP_LEFT,
        SC_GAME_UI_CROP_HANDLE_TOP_RIGHT,
        SC_GAME_UI_CROP_HANDLE_BOTTOM_LEFT,
        SC_GAME_UI_CROP_HANDLE_BOTTOM_RIGHT,
        SC_GAME_UI_CROP_HANDLE_LEFT,
        SC_GAME_UI_CROP_HANDLE_RIGHT,
        SC_GAME_UI_CROP_HANDLE_TOP,
        SC_GAME_UI_CROP_HANDLE_BOTTOM,
    };
    float radius = 22.0f * scale;
    float best = radius * radius;
    enum sc_game_ui_crop_handle result = SC_GAME_UI_CROP_HANDLE_NONE;
    for (size_t i = 0; i < ARRAY_LEN(handles); ++i) {
        float dx = mx - view->handles[i].x;
        float dy = my - view->handles[i].y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= best) {
            best = d2;
            result = handles[i];
        }
    }
    if (result != SC_GAME_UI_CROP_HANDLE_NONE) {
        return result;
    }
    if (mx >= view->crop_min.x && mx <= view->crop_max.x
            && my >= view->crop_min.y && my <= view->crop_max.y) {
        return SC_GAME_UI_CROP_HANDLE_MOVE;
    }
    return SC_GAME_UI_CROP_HANDLE_NONE;
}

static bool
crop_handle_left(enum sc_game_ui_crop_handle h) {
    return h == SC_GAME_UI_CROP_HANDLE_LEFT
        || h == SC_GAME_UI_CROP_HANDLE_TOP_LEFT
        || h == SC_GAME_UI_CROP_HANDLE_BOTTOM_LEFT;
}

static bool
crop_handle_right(enum sc_game_ui_crop_handle h) {
    return h == SC_GAME_UI_CROP_HANDLE_RIGHT
        || h == SC_GAME_UI_CROP_HANDLE_TOP_RIGHT
        || h == SC_GAME_UI_CROP_HANDLE_BOTTOM_RIGHT;
}

static bool
crop_handle_top(enum sc_game_ui_crop_handle h) {
    return h == SC_GAME_UI_CROP_HANDLE_TOP
        || h == SC_GAME_UI_CROP_HANDLE_TOP_LEFT
        || h == SC_GAME_UI_CROP_HANDLE_TOP_RIGHT;
}

static bool
crop_handle_bottom(enum sc_game_ui_crop_handle h) {
    return h == SC_GAME_UI_CROP_HANDLE_BOTTOM
        || h == SC_GAME_UI_CROP_HANDLE_BOTTOM_LEFT
        || h == SC_GAME_UI_CROP_HANDLE_BOTTOM_RIGHT;
}

static void
update_crop_interaction(struct sc_game_ui *ui, struct sc_game *game) {
    ImGuiIO *io = igGetIO_Nil();
    struct sc_game_crop_view view;
    if (!crop_get_view(game, &view)) {
        ui->crop_drag_active = false;
        ui->crop_hover_handle = SC_GAME_UI_CROP_HANDLE_NONE;
        return;
    }

    float mx = io->MousePos.x;
    float my = io->MousePos.y;
    bool over_canvas = !io->WantCaptureMouse && !io->WantTextInput;
    ui->crop_hover_handle = over_canvas
        ? crop_hit_test(&view, mx, my, ui->scale)
        : SC_GAME_UI_CROP_HANDLE_NONE;

    if (over_canvas && io->MouseClicked[0]
            && ui->crop_hover_handle != SC_GAME_UI_CROP_HANDLE_NONE) {
        ui->crop_drag_active = true;
        ui->crop_drag_handle = ui->crop_hover_handle;
        game_window_to_norm(game, mx, my, &ui->crop_drag_start_nx,
                            &ui->crop_drag_start_ny);
        const struct sc_game_crop *crop = &game->profile.crop;
        ui->crop_drag_start_left = crop->enabled ? crop->left : 0.0f;
        ui->crop_drag_start_top = crop->enabled ? crop->top : 0.0f;
        ui->crop_drag_start_right = crop->enabled ? crop->right : 1.0f;
        ui->crop_drag_start_bottom = crop->enabled ? crop->bottom : 1.0f;
    }

    if (!ui->crop_drag_active) {
        return;
    }
    if (!io->MouseDown[0]) {
        ui->crop_drag_active = false;
        ui->crop_drag_handle = SC_GAME_UI_CROP_HANDLE_NONE;
        return;
    }

    float nx;
    float ny;
    game_window_to_norm(game, mx, my, &nx, &ny);
    float dx = nx - ui->crop_drag_start_nx;
    float dy = ny - ui->crop_drag_start_ny;
    float left = ui->crop_drag_start_left;
    float top = ui->crop_drag_start_top;
    float right = ui->crop_drag_start_right;
    float bottom = ui->crop_drag_start_bottom;
    enum sc_game_ui_crop_handle handle = ui->crop_drag_handle;

    if (handle == SC_GAME_UI_CROP_HANDLE_MOVE) {
        float width = right - left;
        float height = bottom - top;
        left = clampf(left + dx, 0.0f, 1.0f - width);
        top = clampf(top + dy, 0.0f, 1.0f - height);
        right = left + width;
        bottom = top + height;
    } else {
        if (crop_handle_left(handle)) {
            left = clampf(left + dx, 0.0f,
                          right - SC_GAME_CROP_MIN_SIZE);
        }
        if (crop_handle_right(handle)) {
            right = clampf(right + dx, left + SC_GAME_CROP_MIN_SIZE,
                           1.0f);
        }
        if (crop_handle_top(handle)) {
            top = clampf(top + dy, 0.0f,
                         bottom - SC_GAME_CROP_MIN_SIZE);
        }
        if (crop_handle_bottom(handle)) {
            bottom = clampf(bottom + dy, top + SC_GAME_CROP_MIN_SIZE,
                            1.0f);
        }
    }

    struct sc_game_crop *crop = &game->profile.crop;
    crop->enabled = true;
    crop->left = left;
    crop->top = top;
    crop->right = right;
    crop->bottom = bottom;
    crop_apply_preview(ui, true);
}

static void
draw_crop_rect(ImDrawList *dl, ImVec2 min, ImVec2 max, ImU32 color) {
    if (max.x > min.x && max.y > min.y) {
        ImDrawList_AddRectFilled(dl, min, max, color, 0.0f,
                                 ImDrawFlags_None);
    }
}

static void
draw_crop_editor(struct sc_game_ui *ui, struct sc_game *game) {
    struct sc_game_crop_view view;
    if (!crop_get_view(game, &view)) {
        return;
    }
    ImDrawList *dl = igGetBackgroundDrawList_Nil();
    ImU32 scrim = rgba(0, 0, 0, 150);
    ImDrawList_PushClipRect(dl, view.full_min, view.full_max, true);
    draw_crop_rect(dl, view.full_min,
                   vec2(view.full_max.x, view.crop_min.y), scrim);
    draw_crop_rect(dl, vec2(view.full_min.x, view.crop_max.y), view.full_max,
                   scrim);
    draw_crop_rect(dl, vec2(view.full_min.x, view.crop_min.y),
                   vec2(view.crop_min.x, view.crop_max.y), scrim);
    draw_crop_rect(dl, vec2(view.crop_max.x, view.crop_min.y),
                   vec2(view.full_max.x, view.crop_max.y), scrim);

    float third_w = (view.crop_max.x - view.crop_min.x) / 3.0f;
    float third_h = (view.crop_max.y - view.crop_min.y) / 3.0f;
    ImU32 grid = rgba(220, 245, 250, 105);
    for (int i = 1; i <= 2; ++i) {
        float x = view.crop_min.x + third_w * i;
        float y = view.crop_min.y + third_h * i;
        ImDrawList_AddLine(dl, vec2(x, view.crop_min.y),
                           vec2(x, view.crop_max.y), grid, 1.0f * ui->scale);
        ImDrawList_AddLine(dl, vec2(view.crop_min.x, y),
                           vec2(view.crop_max.x, y), grid, 1.0f * ui->scale);
    }
    ImDrawList_AddRect(dl, view.crop_min, view.crop_max, ACCENT, 0.0f,
                       ImDrawFlags_None, 2.0f * ui->scale);

    static const enum sc_game_ui_crop_handle handles[8] = {
        SC_GAME_UI_CROP_HANDLE_TOP_LEFT,
        SC_GAME_UI_CROP_HANDLE_TOP_RIGHT,
        SC_GAME_UI_CROP_HANDLE_BOTTOM_LEFT,
        SC_GAME_UI_CROP_HANDLE_BOTTOM_RIGHT,
        SC_GAME_UI_CROP_HANDLE_LEFT,
        SC_GAME_UI_CROP_HANDLE_RIGHT,
        SC_GAME_UI_CROP_HANDLE_TOP,
        SC_GAME_UI_CROP_HANDLE_BOTTOM,
    };
    for (size_t i = 0; i < ARRAY_LEN(handles); ++i) {
        bool hot = ui->crop_hover_handle == handles[i]
                || (ui->crop_drag_active
                    && ui->crop_drag_handle == handles[i]);
        float radius = (hot ? 5.5f : 3.5f) * ui->scale;
        float dx = view.handles[i].x - view.frame_handles[i].x;
        float dy = view.handles[i].y - view.frame_handles[i].y;
        if (dx * dx + dy * dy > 1.0f) {
            ImDrawList_AddLine(dl, view.frame_handles[i], view.handles[i],
                               rgba(60, 200, 235, 170),
                               1.5f * ui->scale);
        }
        ImDrawList_AddCircleFilled(dl, view.handles[i], radius,
                                   hot ? TEXT_MAIN : ACCENT, 16);
    }
    ImDrawList_PopClipRect(dl);

    if (ui->crop_hover_handle == SC_GAME_UI_CROP_HANDLE_MOVE) {
        igSetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (ui->crop_hover_handle != SC_GAME_UI_CROP_HANDLE_NONE) {
        igSetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

static void
crop_use_full_frame(struct sc_game_ui *ui) {
    struct sc_game_crop *crop = &ui->game->profile.crop;
    crop->enabled = false;
    crop->left = 0.0f;
    crop->top = 0.0f;
    crop->right = 1.0f;
    crop->bottom = 1.0f;
    crop_apply_preview(ui, true);
}

static void
crop_use_centered_16_9(struct sc_game_ui *ui) {
    struct sc_screen *screen = ui->game->screen;
    if (!screen || !screen->frame_size.width || !screen->frame_size.height) {
        return;
    }
    float frame_aspect = (float) screen->frame_size.width
                       / screen->frame_size.height;
    // Match the decoded frame's natural direction. Client display rotation
    // does not change which raw rectangle is portrait or landscape.
    float target = frame_aspect >= 1.0f ? 16.0f / 9.0f : 9.0f / 16.0f;
    struct sc_game_crop *crop = &ui->game->profile.crop;
    crop->enabled = true;
    crop->left = 0.0f;
    crop->top = 0.0f;
    crop->right = 1.0f;
    crop->bottom = 1.0f;
    if (frame_aspect > target) {
        float width = target / frame_aspect;
        crop->left = (1.0f - width) * 0.5f;
        crop->right = crop->left + width;
    } else {
        float height = frame_aspect / target;
        crop->top = (1.0f - height) * 0.5f;
        crop->bottom = crop->top + height;
    }
    crop_apply_preview(ui, true);
}

// ============================================================================
// Editor: top toolbar + side panel
// ============================================================================

static bool
add_element(struct sc_game_ui *ui, struct sc_game *game,
            enum sc_game_element_type type) {
    struct sc_game_preset *preset = current_preset(game);
    if (!preset || preset->element_count >= SC_GAME_MAX_ELEMENTS) {
        return false;
    }
    struct sc_game_element *e = &preset->elements[preset->element_count++];
    memset(e, 0, sizeof(*e));
    e->type = type;
    if (type == SC_GAME_ELEMENT_JOYSTICK) {
        snprintf(e->label, sizeof(e->label), "摇杆");
        e->joystick.center.x = 0.5f;
        e->joystick.center.y = 0.5f;
        e->joystick.radius = 0.075f;
        e->joystick.up.type = SC_GAME_KEY_KEYBOARD;
        e->joystick.up.value = 'w';
        e->joystick.down.type = SC_GAME_KEY_KEYBOARD;
        e->joystick.down.value = 's';
        e->joystick.left.type = SC_GAME_KEY_KEYBOARD;
        e->joystick.left.value = 'a';
        e->joystick.right.type = SC_GAME_KEY_KEYBOARD;
        e->joystick.right.value = 'd';
    } else if (type == SC_GAME_ELEMENT_SWIPE) {
        snprintf(e->label, sizeof(e->label), "滑动");
        e->swipe.key.type = SC_GAME_KEY_NONE;
        e->swipe.from.x = 0.50f;
        e->swipe.from.y = 0.62f;
        e->swipe.to.x = 0.50f;
        e->swipe.to.y = 0.42f;
        e->swipe.dur_ms = 280;
        e->swipe.every_ms = 520;
    } else if (type == SC_GAME_ELEMENT_AIM) {
        snprintf(e->label, sizeof(e->label), "瞄准起点");
        e->aim.start.x = 0.55f;
        e->aim.start.y = 0.25f;
        // An aim origin only makes sense with camera control enabled.
        preset->aim.enabled = true;
    } else {
        snprintf(e->label, sizeof(e->label), "按钮");
        e->bind.center.x = 0.5f;
        e->bind.center.y = 0.5f;
        e->bind.size = 0.08f;
        e->bind.action = SC_GAME_ACTION_TAP;
        e->bind.key.type = SC_GAME_KEY_NONE;
    }
    ui->selected_element = (int) (preset->element_count - 1);
    ui->panel_open = true;
    return true;
}

// A low-emphasis Material-style leading action: transparent at rest and a
// soft tonal surface on hover/press. It deliberately does not look like a
// raised primary button.
static bool
toolbar_back_action(struct sc_game_ui *ui, float height) {
    const char *label = "返回";
    ImVec2 ts;
    igCalcTextSize(&ts, label, NULL, false, -1.0f);
    float icon_size = height * 0.48f;
    float pad = 10.0f * ui->scale;
    float gap = 7.0f * ui->scale;
    float width = pad + icon_size + gap + ts.x + pad;
    bool clicked = igInvisibleButton("##back", vec2(width, height),
                                     ImGuiButtonFlags_None);
    ImVec2 min;
    ImVec2 max;
    igGetItemRectMin(&min);
    igGetItemRectMax(&max);
    ImDrawList *dl = igGetWindowDrawList();
    if (igIsItemHovered(ImGuiHoveredFlags_None) || igIsItemActive()) {
        ImU32 bg = igIsItemActive() ? rgba(75, 145, 205, 72)
                                    : rgba(75, 145, 205, 42);
        ImDrawList_AddRectFilled(dl, min, max, bg, 7.0f * ui->scale, 0);
    }
    draw_icon(dl, SC_ICON_BACK,
              vec2(min.x + pad + icon_size * 0.5f,
                   min.y + height * 0.5f), icon_size, TEXT_MAIN);
    ImDrawList_AddText_Vec2(dl,
                            vec2(min.x + pad + icon_size + gap,
                                 min.y + (height - ts.y) * 0.5f),
                            TEXT_MAIN, label, NULL);
    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        igSetTooltip("返回运行模式");
    }
    return clicked;
}

static bool
draw_toolbar(struct sc_game_ui *ui, struct sc_game *game) {
    bool return_to_play = false;
    ImGuiIO *io = igGetIO_Nil();
    float view_w = io->DisplaySize.x;
    float view_h = io->DisplaySize.y;
    float bar_h = 46.0f * ui->scale;
    if (bar_h > view_h * 0.2f) {
        bar_h = view_h * 0.2f;
    }
    igSetNextWindowPos(vec2(0, 0), ImGuiCond_Always, vec2(0, 0));
    igSetNextWindowSize(vec2(view_w, bar_h), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
    igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                          (ImVec4) {0.045f, 0.052f, 0.068f, 0.94f});
    if (igBegin("##editbar", NULL, flags)) {
        ImDrawList *dl = igGetWindowDrawList();
        float action = 32.0f * ui->scale;
        float gap = 8.0f * ui->scale;
        float y = (bar_h - action) * 0.5f;
        float x = 12.0f * ui->scale;

        igSetCursorScreenPos(vec2(x, y));
        if (toolbar_back_action(ui, action)) {
            // Defer the mode switch until the whole editor frame has run.
            // Later widgets may still commit a value in this same frame; the
            // render tail must detect that mutation before set_mode() saves
            // and applies the final profile.
            if (ui->crop_editing) {
                crop_set_editing(ui, false);
            }
            return_to_play = true;
        }
        ImVec2 back_max;
        igGetItemRectMax(&back_max);
        x = back_max.x + 18.0f * ui->scale;

        ImVec2 actions_max = back_max;
        igSetCursorScreenPos(vec2(x, y));
        if (ui->crop_editing) {
            if (igButton("完整画面##cropfull", vec2(0, action))) {
                crop_use_full_frame(ui);
            }
            igSameLine(0, gap);
            if (igButton("16:9 居中##crop169", vec2(0, action))) {
                crop_use_centered_16_9(ui);
            }
            igSameLine(0, gap);
            igPushStyleColor_Vec4(ImGuiCol_Button,
                                  (ImVec4) {0.10f, 0.43f, 0.55f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                                  (ImVec4) {0.14f, 0.56f, 0.69f, 1.0f});
            bool done = igButton("完成裁剪##cropdone", vec2(0, action));
            igPopStyleColor(2);
            if (done) {
                crop_set_editing(ui, false);
            }
            igGetItemRectMax(&actions_max);
        } else {
            if (icon_button("##addjoy", SC_ICON_JOY, "添加虚拟摇杆", action,
                            ACCENT)) {
                add_element(ui, game, SC_GAME_ELEMENT_JOYSTICK);
            }
            igSameLine(0, gap);
            if (icon_button("##addswipe", SC_ICON_SWIPE, "添加滑动移动",
                            action, GOOD)) {
                add_element(ui, game, SC_GAME_ELEMENT_SWIPE);
            }
            igSameLine(0, gap);
            if (icon_button("##addaim", SC_ICON_AIM, "添加瞄准起点", action,
                            ACCENT)) {
                add_element(ui, game, SC_GAME_ELEMENT_AIM);
            }
            igSameLine(0, gap);
            if (icon_button("##addbtn", SC_ICON_BTN, "添加按键映射", action,
                            WARN)) {
                add_element(ui, game, SC_GAME_ELEMENT_BIND);
            }
            igSameLine(0, gap);
            if (icon_button("##crop", SC_ICON_CROP, "裁剪画面", action,
                            ACCENT)) {
                crop_set_editing(ui, true);
            }
            igSameLine(0, 18.0f * ui->scale);
            if (icon_button("##panel", SC_ICON_MENU,
                            ui->panel_open ? "关闭详细面板" : "打开详细面板",
                            action, ACCENT)) {
                ui->panel_open = !ui->panel_open;
            }
            igGetItemRectMax(&actions_max);
        }

        // Auto-save is state, not an action: render it as quiet trailing text.
        const char *save_text = "已自动保存";
        ImVec4 save_col = (ImVec4) {0.56f, 0.70f, 0.64f, 1.0f};
        enum sc_game_save_status save_status = sc_game_get_save_status(game);
        if (save_status == SC_GAME_SAVE_STATUS_SAVING) {
            save_text = "正在保存…";
            save_col = (ImVec4) {0.47f, 0.78f, 0.92f, 1.0f};
        } else if (save_status == SC_GAME_SAVE_STATUS_ERROR) {
            save_text = "保存失败";
            save_col = (ImVec4) {0.94f, 0.42f, 0.42f, 1.0f};
        }
        ImVec2 save_size;
        igCalcTextSize(&save_size, save_text, NULL, false, -1.0f);
        float save_x = view_w - save_size.x - 15.0f * ui->scale;
        // Hide the quiet status on narrow windows instead of letting it
        // collide with the toolbar actions.
        if (save_x - 15.0f * ui->scale > actions_max.x) {
            ImDrawList_AddCircleFilled(dl,
                vec2(save_x - 9.0f * ui->scale, bar_h * 0.5f),
                3.0f * ui->scale,
                save_status == SC_GAME_SAVE_STATUS_ERROR ? BAD : GOOD, 12);
            ImDrawList_AddText_Vec2(dl,
                vec2(save_x, (bar_h - save_size.y) * 0.5f),
                rgba((uint8_t) (save_col.x * 255),
                     (uint8_t) (save_col.y * 255),
                     (uint8_t) (save_col.z * 255), 255), save_text, NULL);
        }

        // Subtle bottom shadow gives the surface elevation without separators.
        ImDrawList_AddLine(dl, vec2(0, bar_h - 1.0f),
                           vec2(view_w, bar_h - 1.0f),
                           rgba(0, 0, 0, 80), 1.0f * ui->scale);
    }
    igEnd();
    igPopStyleColor(1);
    return return_to_play;
}

// ---- panel widgets ---------------------------------------------------------

// Major section header: accent-colored separator text with breathing room
// above and below. Used for 配置/预设/控件列表/属性/鼠标瞄准/拟人抖动.
static void
section_title(struct sc_game_ui *ui, const char *label) {
    igDummy(vec2(0, 8.0f * ui->scale));
    igPushStyleColor_Vec4(ImGuiCol_Text, ACCENT_V4);
    igSeparatorText(label);
    igPopStyleColor(1);
    igDummy(vec2(0, 4.0f * ui->scale));
}

// Minor header inside a section (element-type name in the 属性 area).
static void
sub_title(struct sc_game_ui *ui, const char *label) {
    igDummy(vec2(0, 2.0f * ui->scale));
    igTextColored(ACCENT_V4, "%s", label);
    igDummy(vec2(0, 3.0f * ui->scale));
}

// The 重命名 modal: opened by the pencil button on the 配置/预设 rows.
// Must be drawn in the same ID scope (the panel child) as the igOpenPopup.
static void
draw_rename_popup(struct sc_game_ui *ui, struct sc_game *game) {
    if (!igBeginPopupModal("重命名", NULL,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (ui->rename_focus) {
        igSetKeyboardFocusHere(0);
        ui->rename_focus = false;
    }
    bool enter = igInputText("##rename", ui->rename_buf,
                             sizeof(ui->rename_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue, NULL,
                             NULL);
    bool confirm = igButton("确定", vec2(0, 0));
    igSameLine(0, 8.0f * ui->scale);
    if (igButton("取消", vec2(0, 0))) {
        igCloseCurrentPopup();
        igEndPopup();
        return;
    }
    bool valid = ui->rename_is_profile
               ? sc_game_profile_name_is_valid(ui->rename_buf)
               : sc_game_preset_name_is_valid(ui->rename_buf);
    if (!valid) {
        igTextColored((ImVec4) {0.94f, 0.42f, 0.42f, 1.0f},
                      "名称不能为空，也不能包含空白或控制字符");
    }
    if ((confirm || enter) && valid) {
        if (ui->rename_is_profile) {
            char old[SC_GAME_PROFILE_NAME_MAX];
            snprintf(old, sizeof(old), "%s", game->profile.name);
            if (strcmp(old, ui->rename_buf)) {
                snprintf(game->profile.name, sizeof(game->profile.name),
                         "%s", ui->rename_buf);
                if (sc_game_save_profile(game)) {
                    sc_game_delete_profile(game, old);
                } else {
                    snprintf(game->profile.name,
                             sizeof(game->profile.name), "%s", old);
                }
            }
        } else {
            struct sc_game_preset *preset = current_preset(game);
            if (preset) {
                snprintf(preset->name, sizeof(preset->name), "%s",
                         ui->rename_buf);
            }
        }
        igCloseCurrentPopup();
    }
    igEndPopup();
}

static void
draw_create_profile_popup(struct sc_game_ui *ui, struct sc_game *game) {
    if (!igBeginPopupModal("新建配置", &ui->profile_create_popup,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    igTextDisabled("创建一套独立的游戏配置");
    if (ui->profile_create_focus) {
        igSetKeyboardFocusHere(0);
        ui->profile_create_focus = false;
    }
    bool enter = igInputTextWithHint(
        "##newprofile", "配置名称", ui->new_profile_name,
        sizeof(ui->new_profile_name), ImGuiInputTextFlags_EnterReturnsTrue,
        NULL, NULL);
    bool valid = sc_game_profile_name_is_valid(ui->new_profile_name);
    bool exists = valid
               && sc_game_profile_exists(game, ui->new_profile_name);

    if (ui->new_profile_name[0] && !valid) {
        igTextColored((ImVec4) {0.94f, 0.42f, 0.42f, 1.0f},
                      "名称不能包含空白、斜杠或控制字符");
    } else if (exists) {
        igTextColored((ImVec4) {0.94f, 0.42f, 0.42f, 1.0f},
                      "已有同名配置，不会覆盖");
    } else if (ui->profile_create_failed) {
        igTextColored((ImVec4) {0.94f, 0.42f, 0.42f, 1.0f},
                      "创建失败，请检查保存目录");
    } else {
        igTextDisabled("创建后会自动切换到新配置");
    }

    bool can_create = valid && !exists;
    igBeginDisabled(!can_create);
    bool create = igButton("创建", vec2(0, 0));
    igEndDisabled();
    igSameLine(0, 8.0f * ui->scale);
    if (igButton("取消", vec2(0, 0))) {
        ui->profile_create_popup = false;
        ui->profile_create_failed = false;
        igCloseCurrentPopup();
        igEndPopup();
        return;
    }

    if ((create || enter) && can_create) {
        if (sc_game_create_profile(game, ui->new_profile_name)) {
            ui->new_profile_name[0] = '\0';
            ui->profile_create_popup = false;
            ui->profile_create_failed = false;
            ui->selected_element = -1;
            ui->label_edit_element = -1;
            igCloseCurrentPopup();
        } else {
            ui->profile_create_failed = true;
        }
    }
    igEndPopup();
}

static void
combo_profiles(struct sc_game_ui *ui, struct sc_game *game) {
    char names[32][SC_GAME_PROFILE_NAME_MAX];
    size_t count = sc_game_list_profiles(game, names, 32);

    float ibh = igGetFrameHeight();
    // Keep room for both rename and create. The old row only exposed rename,
    // which made it impossible to add a configuration after start-up.
    igPushItemWidth(-(2.0f * ibh + 14.0f * ui->scale));
    if (igBeginCombo("##profile", game->profile.name, ImGuiComboFlags_None)) {
        for (size_t i = 0; i < count; ++i) {
            bool selected = !strcmp(names[i], game->profile.name);
            if (igSelectable_Bool(names[i], selected, ImGuiSelectableFlags_None,
                                  vec2(0, 0))) {
                sc_game_load_profile(game, names[i]);
                ui->selected_element = -1;
            }
        }
        igEndCombo();
    }
    igPopItemWidth();
    igSameLine(0, 6.0f * ui->scale);
    if (icon_button("##editprofile", SC_ICON_PENCIL, "重命名配置", ibh,
                    TEXT_MAIN)) {
        ui->rename_is_profile = true;
        ui->rename_focus = true;
        snprintf(ui->rename_buf, sizeof(ui->rename_buf), "%s",
                 game->profile.name);
        igOpenPopup_Str("重命名", ImGuiPopupFlags_None);
    }
    igSameLine(0, 4.0f * ui->scale);
    if (icon_button("##addprofile", SC_ICON_PLUS, "新建配置", ibh, GOOD)) {
        ui->new_profile_name[0] = '\0';
        ui->profile_create_popup = true;
        ui->profile_create_focus = true;
        ui->profile_create_failed = false;
        igOpenPopup_Str("新建配置", ImGuiPopupFlags_None);
    }

    draw_rename_popup(ui, game);
    draw_create_profile_popup(ui, game);
}

static void
combo_presets(struct sc_game_ui *ui, struct sc_game *game) {
    struct sc_game_preset *preset = current_preset(game);
    float ibh = igGetFrameHeight();
    // reserve room on the right for the pencil/plus/trash icon buttons
    igPushItemWidth(-(3.0f * ibh + 16.0f * ui->scale));
    if (igBeginCombo("##preset", preset ? preset->name : "-",
                     ImGuiComboFlags_None)) {
        for (size_t i = 0; i < game->profile.preset_count; ++i) {
            bool selected = i == game->profile.current_preset;
            if (igSelectable_Bool(game->profile.presets[i].name, selected,
                                  ImGuiSelectableFlags_None, vec2(0, 0))) {
                game->profile.current_preset = i;
                ui->selected_element = -1;
                sc_game_apply_profile(game);
            }
        }
        igEndCombo();
    }
    igPopItemWidth();
    igSameLine(0, 6.0f * ui->scale);
    if (icon_button("##editpreset", SC_ICON_PENCIL, "重命名预设", ibh,
                    TEXT_MAIN)) {
        if (preset) {
            ui->rename_is_profile = false;
            ui->rename_focus = true;
            snprintf(ui->rename_buf, sizeof(ui->rename_buf), "%s",
                     preset->name);
            igOpenPopup_Str("重命名", ImGuiPopupFlags_None);
        }
    }
    igSameLine(0, 4.0f * ui->scale);
    if (icon_button("##addpreset", SC_ICON_PLUS, "新建预设", ibh, GOOD)) {
        ui->template_selected = SC_GAME_TEMPLATE_OPEN_WORLD;
        ui->template_popup = true;
        igOpenPopup_Str("从玩法模板新建", ImGuiPopupFlags_None);
    }
    if (game->profile.preset_count > 1) {
        igSameLine(0, 4.0f * ui->scale);
        if (icon_button("##delpreset", SC_ICON_TRASH, "删除当前预设", ibh,
                        BAD)) {
            sc_game_cancel_binding(game);
            size_t cur = game->profile.current_preset;
            memmove(&game->profile.presets[cur],
                    &game->profile.presets[cur + 1],
                    (game->profile.preset_count - cur - 1)
                        * sizeof(game->profile.presets[0]));
            --game->profile.preset_count;
            if (game->profile.current_preset >= game->profile.preset_count) {
                game->profile.current_preset = 0;
            }
            ui->selected_element = -1;
            sc_game_apply_profile(game);
        }
    }

    if (ui->template_popup && igBeginPopupModal(
            "从玩法模板新建", &ui->template_popup,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        igTextDisabled("模板会填入常用键位和大致位置，创建后按游戏画面对齐");
        igDummy(vec2(0, 4.0f * ui->scale));
        for (size_t i = 0; i < sc_game_template_count(); ++i) {
            const struct sc_game_template_info *info =
                sc_game_template_at(i);
            bool selected = ui->template_selected == (int) info->id;
            igPushID_Int((int) i);
            if (igSelectable_Bool(info->name, selected,
                                  ImGuiSelectableFlags_None,
                                  vec2(360.0f * ui->scale, 0))) {
                ui->template_selected = (int) info->id;
            }
            if (selected) {
                igTextDisabled("%s", info->description);
                igTextDisabled("适合：%s", info->recommended_for);
                igTextDisabled("默认：%s", info->controls);
            }
            igPopID();
        }
        bool full = game->profile.preset_count >= SC_GAME_MAX_PRESETS;
        if (full) {
            igTextColored((ImVec4) {0.94f, 0.42f, 0.42f, 1.0f},
                          "预设数量已达到上限");
        }
        igBeginDisabled(full);
        if (igButton("创建预设", vec2(0, 0))) {
            sc_game_cancel_binding(game);
            size_t index;
            if (sc_game_template_append(
                    &game->profile,
                    (enum sc_game_template_id) ui->template_selected,
                    &index)) {
                ui->selected_element = -1;
                ui->label_edit_element = -1;
                sc_game_apply_profile(game);
                ui->template_popup = false;
                igCloseCurrentPopup();
            }
        }
        igEndDisabled();
        igSameLine(0, 8.0f * ui->scale);
        if (igButton("取消", vec2(0, 0))) {
            ui->template_popup = false;
            igCloseCurrentPopup();
        }
        igEndPopup();
    }
}

// Edit the combo modifiers (shift/ctrl/alt) of a key binding.
static void
combo_mods(struct sc_game_ui *ui, struct sc_game_key *key) {
    bool sh = key->mods & SC_GAME_MOD_SHIFT;
    bool ct = key->mods & SC_GAME_MOD_CTRL;
    bool al = key->mods & SC_GAME_MOD_ALT;
    igTextDisabled("组合键（可选）");

    // Keep each checkbox and its label in one item. On narrow panels these
    // units wrap together instead of leaving the last label outside the
    // window (the old checkbox + detached igText layout did exactly that).
    const char *labels[] = {"Shift", "Ctrl", "Alt"};
    bool *values[] = {&sh, &ct, &al};
    uint8_t masks[] = {SC_GAME_MOD_SHIFT, SC_GAME_MOD_CTRL, SC_GAME_MOD_ALT};
    float item_widths[3];
    for (size_t i = 0; i < 3; ++i) {
        ImVec2 text_size;
        igCalcTextSize(&text_size, labels[i], NULL, false, -1.0f);
        item_widths[i] = igGetFrameHeight() + 5.0f * ui->scale
                       + text_size.x;
    }

    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    float row_width = 0.0f;
    for (size_t i = 0; i < 3; ++i) {
        float gap = i ? 10.0f * ui->scale : 0.0f;
        if (i && row_width + gap + item_widths[i] <= avail.x) {
            igSameLine(0, gap);
            row_width += gap;
        } else if (i) {
            row_width = 0.0f;
        }
        igPushID_Int((int) i);
        if (igCheckbox(labels[i], values[i])) {
            key->mods = *values[i] ? (key->mods | masks[i])
                                   : (key->mods & ~masks[i]);
        }
        igPopID();
        row_width += item_widths[i];
    }
}

// ImGui's stock slider draws its visible label to the right of the value
// track. A full-width item therefore pushes Chinese labels out of a narrow
// panel. Material fields put the label above a full-width, label-less track.
static bool
slider_int_field(const char *id, const char *label, int *value,
                 int min_value, int max_value, const char *unit) {
    igPushID_Str(id);
    igTextDisabled("%s", label);
    igSetNextItemWidth(-1.0f);
    char format[24];
    snprintf(format, sizeof(format), "%%d%s%s", unit ? " " : "",
             unit ? unit : "");
    bool changed = igSliderInt("##value", value, min_value, max_value,
                               format, ImGuiSliderFlags_None);
    igPopID();
    return changed;
}

static bool
slider_float_field(const char *id, const char *label, float *value,
                   float min_value, float max_value, const char *format) {
    igPushID_Str(id);
    igTextDisabled("%s", label);
    igSetNextItemWidth(-1.0f);
    bool changed = igSliderFloat("##value", value, min_value, max_value,
                                 format, ImGuiSliderFlags_None);
    igPopID();
    return changed;
}

static bool
input_float2_field(const char *id, const char *label, float value[2]) {
    igPushID_Str(id);
    igTextDisabled("%s", label);
    igSetNextItemWidth(-1.0f);
    bool changed = igInputFloat2("##value", value, "%.3f",
                                 ImGuiInputTextFlags_None);
    igPopID();
    return changed;
}

static const char *
key_preview(const struct sc_game_key *key) {
    static char buf[32];
    if (key->type == SC_GAME_KEY_NONE) {
        return "未绑定";
    }
    sc_game_key_to_name(key, buf, sizeof(buf));
    return buf;
}

static void
key_bind_button(const char *id, const char *name, struct sc_game *game,
                struct sc_game_key *key) {
    bool armed = sc_game_is_binding_target(game, key);
    char label[96];
    snprintf(label, sizeof(label), "%s%s%s", name && *name ? name : "按键",
             armed ? "：按任意键…" : "：", armed ? "" : key_preview(key));
    igPushID_Str(id);
    igPushStyleColor_Vec4(ImGuiCol_Button, armed
            ? (ImVec4) {0.45f, 0.30f, 0.10f, 1.0f}
            : (ImVec4) {0.14f, 0.16f, 0.22f, 1.0f});
    bool clicked = igButton(label, vec2(-1, 0));
    igPopStyleColor(1);
    igPopID();
    if (clicked) {
        sc_game_arm_binding(game, key);
    }
}

static void
edit_element_panel(struct sc_game_ui *ui, struct sc_game *game) {
    struct sc_game_preset *preset = current_preset(game);
    section_title(ui, "属性");
    if (!preset || ui->selected_element < 0
            || ui->selected_element >= (int) preset->element_count) {
        ui->label_edit_element = -1;
        igTextWrapped("在画面上点击控件，或在列表中选择以编辑属性。");
        return;
    }
    struct sc_game_element *elem = &preset->elements[ui->selected_element];

    if (elem->type == SC_GAME_ELEMENT_SWIPE) {
        sub_title(ui, "滑动移动（按住重复拖动）");
        igTextWrapped("拖动绿色“起”点或蓝色“终”点可单独改方向；拖动连线可整体移动。");
        combo_mods(ui, &elem->swipe.key);
        key_bind_button("swipekey", "触发按键", game, &elem->swipe.key);
        int dur = (int) elem->swipe.dur_ms;
        int every = (int) elem->swipe.every_ms;
        if (slider_int_field("swipe_duration", "拖动时长", &dur,
                             SC_GAME_SWIPE_DURATION_MIN,
                             SC_GAME_SWIPE_DURATION_MAX, "ms")) {
            elem->swipe.dur_ms = (uint32_t) dur;
        }
        if (slider_int_field("swipe_period", "重复间隔", &every,
                             SC_GAME_SWIPE_PERIOD_MIN,
                             SC_GAME_SWIPE_PERIOD_MAX, "ms")) {
            elem->swipe.every_ms = (uint32_t) every;
        }
        if (igCollapsingHeader_TreeNodeFlags("高级位置##swipe",
                                             ImGuiTreeNodeFlags_None)) {
            float f[2] = {elem->swipe.from.x, elem->swipe.from.y};
            if (input_float2_field("swipe_from", "起点（0～1）", f)) {
                elem->swipe.from.x = clamp01(f[0]);
                elem->swipe.from.y = clamp01(f[1]);
            }
            float t[2] = {elem->swipe.to.x, elem->swipe.to.y};
            if (input_float2_field("swipe_to", "终点（0～1）", t)) {
                elem->swipe.to.x = clamp01(t[0]);
                elem->swipe.to.y = clamp01(t[1]);
            }
        }
    } else if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
        sub_title(ui, "虚拟摇杆");
        igTextDisabled("方向键（点击后按下新按键）");
        key_bind_button("joyup", "上", game, &elem->joystick.up);
        key_bind_button("joydown", "下", game, &elem->joystick.down);
        key_bind_button("joyleft", "左", game, &elem->joystick.left);
        key_bind_button("joyright", "右", game, &elem->joystick.right);
        slider_float_field("joystick_radius", "摇杆半径",
                           &elem->joystick.radius,
                           SC_GAME_JOYSTICK_RADIUS_MIN,
                           SC_GAME_JOYSTICK_RADIUS_MAX, "%.3f");
        if (igCollapsingHeader_TreeNodeFlags("高级位置##joystick",
                                             ImGuiTreeNodeFlags_None)) {
            float c[2] = {elem->joystick.center.x, elem->joystick.center.y};
            if (input_float2_field("joystick_center", "中心（0～1）", c)) {
                elem->joystick.center.x = clamp01(c[0]);
                elem->joystick.center.y = clamp01(c[1]);
            }
        }
    } else if (elem->type == SC_GAME_ELEMENT_AIM) {
        sub_title(ui, "瞄准起点");
        igTextWrapped("把准星拖到画面空旷处。瞄准会从这里按下并持续滑动；"
                      "到屏幕边缘时会立即回到起点继续。");
        if (igCollapsingHeader_TreeNodeFlags("高级位置##aim",
                                             ImGuiTreeNodeFlags_None)) {
            float p[2] = {elem->aim.start.x, elem->aim.start.y};
            if (input_float2_field("aim_start", "起点（0～1）", p)) {
                elem->aim.start.x = clamp01(p[0]);
                elem->aim.start.y = clamp01(p[1]);
            }
        }
    } else {
        sub_title(ui, "按键映射");
        if (igRadioButton_Bool("单击(tap)",
                               elem->bind.action == SC_GAME_ACTION_TAP)) {
            elem->bind.action = SC_GAME_ACTION_TAP;
        }
        igSameLine(0, -1);
        if (igRadioButton_Bool("按住(hold)",
                               elem->bind.action == SC_GAME_ACTION_HOLD)) {
            elem->bind.action = SC_GAME_ACTION_HOLD;
        }
        slider_float_field("bind_size", "触控范围", &elem->bind.size,
                           SC_GAME_BIND_SIZE_MIN, SC_GAME_BIND_SIZE_MAX,
                           "%.3f");
        combo_mods(ui, &elem->bind.key);
        key_bind_button("bindkey", "触发按键", game, &elem->bind.key);
        if (igCollapsingHeader_TreeNodeFlags("高级位置##bind",
                                             ImGuiTreeNodeFlags_None)) {
            float p[2] = {elem->bind.center.x, elem->bind.center.y};
            if (input_float2_field("bind_center", "位置（0～1）", p)) {
                elem->bind.center.x = clamp01(p[0]);
                elem->bind.center.y = clamp01(p[1]);
            }
        }
    }

    // common: label + delete
    igDummy(vec2(0, 6.0f * ui->scale));
    igSeparator();
    if (ui->label_edit_element != ui->selected_element) {
        ui->label_edit_element = ui->selected_element;
        snprintf(ui->label_edit_buf, sizeof(ui->label_edit_buf), "%s",
                 elem->label);
    }
    igTextDisabled("名称");
    igSetNextItemWidth(-1.0f);
    if (igInputText("##element_name", ui->label_edit_buf,
                    sizeof(ui->label_edit_buf), ImGuiInputTextFlags_None,
                    NULL, NULL)) {
        char normalized[SC_GAME_ELEMENT_LABEL_MAX];
        if (normalize_element_label(ui->label_edit_buf, normalized)) {
            snprintf(elem->label, sizeof(elem->label), "%s", normalized);
        }
    }
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4) {0.42f, 0.14f, 0.14f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4) {0.58f, 0.20f, 0.20f, 1.0f});
    if (igSmallButton("删除此控件")) {
        sc_game_cancel_binding(game);
        size_t idx = (size_t) ui->selected_element;
        memmove(&preset->elements[idx], &preset->elements[idx + 1],
                (preset->element_count - idx - 1)
                    * sizeof(preset->elements[0]));
        --preset->element_count;
        ui->selected_element = -1;
        ui->label_edit_element = -1;
    }
    igPopStyleColor(2);
}

static void
element_list(struct sc_game_ui *ui, struct sc_game *game) {
    struct sc_game_preset *preset = current_preset(game);
    if (!preset) {
        return;
    }
    section_title(ui, "控件列表");
    // A compact one-line Material list. The old Selectable started after the
    // leading dot but still consumed the full available width, so its blue
    // selection surface extended beyond the panel and looked like an empty
    // second row. Compute the remaining width explicitly.
    float row_h = igGetFrameHeight();
    float chip_r = 6.0f * ui->scale;
    float chip_pad = 6.0f * ui->scale;  // chip left inset
    float chip_gap = 10.0f * ui->scale; // chip -> text gap
    for (size_t i = 0; i < preset->element_count; ++i) {
        struct sc_game_element *elem = &preset->elements[i];

        ImVec2 row_pos;
        igGetCursorScreenPos(&row_pos);
        ImDrawList *dl = igGetWindowDrawList();
        ImVec2 c = vec2(row_pos.x + chip_pad + chip_r,
                        row_pos.y + row_h / 2);
        ImU32 chip = ACCENT;
        const char *tag = "摇杆";
        if (elem->type == SC_GAME_ELEMENT_BIND) {
            tag = elem->bind.action == SC_GAME_ACTION_TAP ? "单击" : "按住";
            chip = elem->bind.action == SC_GAME_ACTION_TAP ? WARN : PURPLE;
        } else if (elem->type == SC_GAME_ELEMENT_SWIPE) {
            tag = "滑动";
            chip = GOOD;
        } else if (elem->type == SC_GAME_ELEMENT_AIM) {
            tag = "瞄准起点";
            chip = ACCENT;
        }
        ImDrawList_AddCircleFilled(dl, c, chip_r, chip, 16);

        char visible[SC_GAME_ELEMENT_LABEL_MAX + 32];
        if (elem->label[0] && strcmp(tag, elem->label)) {
            snprintf(visible, sizeof(visible), "%s · %s", tag, elem->label);
        } else {
            snprintf(visible, sizeof(visible), "%s", tag);
        }
        float text_x = row_pos.x + chip_pad + chip_r * 2 + chip_gap;
        igSetCursorScreenPos(vec2(text_x, row_pos.y));
        ImVec2 avail;
        igGetContentRegionAvail(&avail);
        float selectable_w = maxf(1.0f, avail.x);
        char id[32];
        snprintf(id, sizeof(id), "##element_%zu", i);
        if (igSelectable_Bool(id, (int) i == ui->selected_element,
                              ImGuiSelectableFlags_NoPadWithHalfSpacing,
                              vec2(selectable_w, row_h))) {
            ui->selected_element = (int) i;
        }
        ImVec2 text_pos = vec2(text_x + 8.0f * ui->scale,
                               row_pos.y
                               + (row_h - igGetTextLineHeight()) * 0.5f);
        ImVec4 clip = {text_x, row_pos.y, text_x + selectable_w,
                       row_pos.y + row_h};
        ImDrawList_AddText_FontPtr(dl, igGetFont(), igGetFontSize(), text_pos,
                                   TEXT_MAIN, visible, NULL, 0.0f, &clip);
    }
}

static void
aim_panel(struct sc_game_ui *ui, struct sc_game *game) {
    struct sc_game_preset *preset = current_preset(game);
    if (!preset) {
        return;
    }
    section_title(ui, "鼠标瞄准");
    igCheckbox("启用（相对鼠标拖动视角）", &preset->aim.enabled);
    if (preset->aim.enabled) {
        slider_float_field("aim_sensitivity", "灵敏度",
                           &preset->aim.sensitivity,
                           SC_GAME_AIM_SENSITIVITY_MIN,
                           SC_GAME_AIM_SENSITIVITY_MAX, "%.2f");
    }
    section_title(ui, "拟人抖动");
    igCheckbox("启用（位置/时序随机扰动）", &preset->humanize.enabled);
    if (preset->humanize.enabled) {
        slider_float_field("humanize_amplitude", "抖动幅度",
                           &preset->humanize.amp,
                           SC_GAME_HUMANIZE_AMP_MIN,
                           SC_GAME_HUMANIZE_AMP_MAX, "%.4f");
    }
}

static void
draw_panel(struct sc_game_ui *ui, struct sc_game *game) {
    ImGuiIO *io = igGetIO_Nil();
    float bar_h = 46.0f * ui->scale;
    float panel_w = minf(SC_GAME_PANEL_WIDTH_BASE * ui->scale,
                         io->DisplaySize.x * 0.60f);
    float panel_h = io->DisplaySize.y * 0.74f;
    float min_h = 260.0f * ui->scale;
    float max_h = maxf(min_h, io->DisplaySize.y - bar_h
                                    - 20.0f * ui->scale);
    panel_h = clampf(panel_h, min_h, max_h);
    igSetNextWindowPos(vec2(io->DisplaySize.x - panel_w
                                - 12.0f * ui->scale,
                            bar_h + 10.0f * ui->scale),
                       ImGuiCond_FirstUseEver, vec2(0, 0));
    igSetNextWindowSize(vec2(panel_w, panel_h), ImGuiCond_FirstUseEver);
    igSetNextWindowSizeConstraints(vec2(280.0f * ui->scale,
                                         220.0f * ui->scale),
                                   vec2(io->DisplaySize.x * 0.82f, max_h),
                                   NULL, NULL);
    bool panel_open = ui->panel_open;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                          (ImVec4) {0.06f, 0.065f, 0.085f, 0.97f});
    if (igBegin("游戏模式编辑", &panel_open, flags)) {
        ui->panel_open = panel_open;
        igPushItemWidth(-1);
        section_title(ui, "配置");
        combo_profiles(ui, game);
        section_title(ui, "预设");
        combo_presets(ui, game);
        element_list(ui, game);
        edit_element_panel(ui, game);
        aim_panel(ui, game);
        igDummy(vec2(0, 8.0f * ui->scale));
        igPopItemWidth();
    }
    igEnd();
    ui->panel_open = panel_open;
    igPopStyleColor(1);
}

// ============================================================================
// Play-mode HUD
// ============================================================================

#define SC_GAME_HUD_IDLE_DELAY 3.0f // idle seconds before the bar expands

static void
draw_hud(struct sc_game_ui *ui, struct sc_game *game) {
    struct sc_game_preset *preset = current_preset(game);
    const char *control_name = sc_game_is_native(game)
                             ? "普通模式"
                             : (preset ? preset->name : "-");
    bool captured = sc_game_is_captured(game);
    ImGuiIO *io = igGetIO_Nil();
    char edit_hotkey[32];
    char preset_hotkey[32];
    sc_game_hotkey_to_name(&game->profile.hotkey_edit, edit_hotkey,
                           sizeof(edit_hotkey));
    sc_game_hotkey_to_name(&game->profile.hotkey_next_preset, preset_hotkey,
                           sizeof(preset_hotkey));

    // ---- activity detection ----------------------------------------------
    // Compare this frame's input state against the previous frame's. Input is
    // fed to ImGui from game.c's handle_event and from sc_game_ui_render
    // (forced mouse pos), so io already mirrors everything the user did.
    // While ImGui owns the mouse/keyboard (hovering this bar, a popup or a
    // modal open), input is interaction WITH the HUD, not with the game, and
    // must not collapse it.
    bool activity = false;
    if (!io->WantCaptureMouse) {
        if (io->MousePos.x > -1e8f && ui->hud_last_mx > -1e8f
                && (io->MousePos.x != ui->hud_last_mx
                    || io->MousePos.y != ui->hud_last_my)) {
            activity = true; // mouse moved over the game
        }
        for (int i = 0; i < 5; ++i) {
            if (io->MouseDown[i] != ui->hud_last_mdown[i]) {
                activity = true; // click / release
            }
        }
        if (io->MouseWheel != 0.0f) {
            activity = true; // wheel (drained each frame, so != 0 = scrolled)
        }
    }
    if (!io->WantCaptureKeyboard) {
        if (io->InputQueueCharacters.Size > 0) {
            activity = true; // typed characters this frame
        }
        // any named key held (1.91 has no io.KeysDown[]): covers the non-text
        // gameplay keys (arrows, modifiers, ...) that produce no characters
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            if (igIsKeyDown_Nil((ImGuiKey) k)) {
                activity = true;
                break;
            }
        }
    }
    ui->hud_last_mx = io->MousePos.x;
    ui->hud_last_my = io->MousePos.y;
    for (int i = 0; i < 5; ++i) {
        ui->hud_last_mdown[i] = io->MouseDown[i];
    }

    // ---- idle / hover state machine ----------------------------------------
    // Gameplay input (captured mouse) collapses the bar; a few idle seconds
    // bring it back. While the mouse is FREE (Ctrl held / click-released)
    // the user is heading for the bar: it must never collapse then.
    if (activity) {
        ui->hud_idle = 0.0f;
        if (captured) {
            ui->hud_shown = false;
        }
    } else {
        ui->hud_idle += io->DeltaTime;
        if (ui->hud_idle >= SC_GAME_HUD_IDLE_DELAY) {
            ui->hud_shown = true;
        }
    }
    if (!captured && ui->hud_anim > 0.0f) {
        ui->hud_shown = true; // free mouse: bar stays available
    }
    if (!captured && !ui->hud_shown) {
        // collapsed with a free mouse: hovering the top strip reveals the
        // interactive bar (otherwise its buttons would be unreachable)
        float strip = ui->hud_h + 10.0f * ui->scale;
        if (io->MousePos.x >= 0.0f && io->MousePos.x <= io->DisplaySize.x
                && io->MousePos.y >= 0.0f && io->MousePos.y <= strip) {
            ui->hud_shown = true;
            ui->hud_idle = 0.0f;
        }
    }

    // ---- per-frame eased animation (cubic ease-out) ------------------------
    if (ui->hud_shown != ui->hud_anim_shown) {
        ui->hud_anim_shown = ui->hud_shown;
        ui->hud_anim_from = ui->hud_anim;
        ui->hud_anim_t = 0.0f;
    }
    float target = ui->hud_shown ? 1.0f : 0.0f;
    if (ui->hud_anim != target) {
        float dur = ui->hud_shown ? 0.30f : 0.22f; // expand a touch slower
        float p = ui->hud_anim_t / dur;
        if (p > 1.0f) {
            p = 1.0f;
        }
        ui->hud_anim_t += io->DeltaTime;
        float eased = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
        ui->hud_anim = ui->hud_anim_from
                     + (target - ui->hud_anim_from) * eased;
    }

    if (!ui->hud_shown && ui->hud_anim < 0.05f) {
        // Never disappear completely during gameplay: retain a quiet chip
        // showing the active control mode. The detailed help still expands
        // after idle or whenever the mouse is released.
        igSetNextWindowPos(vec2(io->DisplaySize.x * 0.5f,
                                6.0f * ui->scale),
                           ImGuiCond_Always, vec2(0.5f, 0.0f));
        igSetNextWindowBgAlpha(0.56f);
        igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
                            vec2(10.0f * ui->scale, 4.0f * ui->scale));
        ImGuiWindowFlags chip_flags = ImGuiWindowFlags_NoDecoration
                                    | ImGuiWindowFlags_AlwaysAutoResize
                                    | ImGuiWindowFlags_NoMove
                                    | ImGuiWindowFlags_NoInputs
                                    | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoFocusOnAppearing;
        if (igBegin("##hud_preset_chip", NULL, chip_flags)) {
            igTextDisabled("控制 · %s", control_name);
        }
        igEnd();
        igPopStyleVar(1);
        return;
    }

    // ---- draw the sliding bar ----------------------------------------------
    // y offset slides it off the top edge, style alpha fades content and
    // bg alpha fades the panel, all driven by the eased value.
    // A small top margin keeps the bar from hugging the window edge.
    float alpha = captured ? 0.72f : 0.85f;
    float top_margin = 6.0f * ui->scale;
    float yoff = (1.0f - ui->hud_anim) * -(ui->hud_h + top_margin)
               + top_margin;
    igSetNextWindowPos(vec2(io->DisplaySize.x * 0.5f, yoff),
                       ImGuiCond_Always, vec2(0.5f, 0.0f));
    igSetNextWindowBgAlpha(alpha * ui->hud_anim);
    igPushStyleVar_Float(ImGuiStyleVar_Alpha, ui->hud_anim);
    // slim horizontal bar: less vertical padding than the global style
    ImGuiStyle *st = igGetStyle();
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding,
                        vec2(st->FramePadding.x, 3.0f * ui->scale));

    if (captured) {
        // tiny non-interactive badge: name line + bold key-hint line
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                               | ImGuiWindowFlags_AlwaysAutoResize
                               | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoInputs
                               | ImGuiWindowFlags_NoSavedSettings
                               | ImGuiWindowFlags_NoFocusOnAppearing;
        if (igBegin("##hudbadge", NULL, flags)) {
            igText("%s · %s", game->profile.name,
                   control_name);
            if (!sc_game_is_native(game)) {
                igTextColored((ImVec4) {0.47f, 0.84f, 0.96f, 1.0f}, "Alt");
                igSameLine(0, 4.0f * ui->scale);
                igTextDisabled("临时解锁");
                igSameLine(0, 9.0f * ui->scale);
            }
            igTextColored((ImVec4) {0.47f, 0.84f, 0.96f, 1.0f}, "%s",
                          edit_hotkey);
            igSameLine(0, 4.0f * ui->scale);
            igTextDisabled("编辑");
            igSameLine(0, 9.0f * ui->scale);
            igTextColored((ImVec4) {0.47f, 0.84f, 0.96f, 1.0f}, "%s",
                          preset_hotkey);
            igSameLine(0, 4.0f * ui->scale);
            igTextDisabled(sc_game_is_native(game)
                               ? "短按返回游戏"
                               : "短按预设 · 长按普通");
            ImVec2 wsize;
            igGetWindowSize(&wsize);
            ui->hud_h = wsize.y;
        }
        igEnd();
        igPopStyleVar(2);
        return;
    }

    // released: interactive mini bar
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_AlwaysAutoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!igBegin("##hud", NULL, flags)) {
        igEnd();
        igPopStyleVar(2);
        return;
    }

    igText("%s", game->profile.name);
    if (preset) {
        igSameLine(0, 10.0f * ui->scale);
        igPushItemWidth(150.0f * ui->scale);
        if (igBeginCombo("##hudpreset", control_name,
                         ImGuiComboFlags_None)) {
            for (size_t i = 0; i < game->profile.preset_count; ++i) {
                bool selected = !sc_game_is_native(game)
                             && i == game->profile.current_preset;
                if (igSelectable_Bool(game->profile.presets[i].name, selected,
                                      ImGuiSelectableFlags_None,
                                      vec2(0, 0))) {
                    sc_game_select_preset(game, i);
                }
            }
            bool native_selected = sc_game_is_native(game);
            if (igSelectable_Bool("普通模式", native_selected,
                                  ImGuiSelectableFlags_None, vec2(0, 0))) {
                sc_game_select_native(game);
            }
            igEndCombo();
        }
        igPopItemWidth();
    }
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4) {0.16f, 0.35f, 0.55f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4) {0.22f, 0.48f, 0.72f, 1.0f});
    igSameLine(0, 10.0f * ui->scale);
    if (igButton("编辑", vec2(0, 0))) {
        sc_game_set_mode(game, SC_GAME_MODE_EDIT);
    }
    igPopStyleColor(2);
    igSameLine(0, 8 * ui->scale);
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4) {0.13f, 0.42f, 0.24f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4) {0.18f, 0.58f, 0.33f, 1.0f});
    if (igButton("锁定鼠标", vec2(0, 0))) {
        sc_game_request_capture(game, true);
    }
    igPopStyleColor(2);
    ImVec2 wsize;
    igGetWindowSize(&wsize);
    ui->hud_h = wsize.y;
    igEnd();
    igPopStyleVar(2);
}

// ============================================================================
// Profile picker (start-up modal)
// ============================================================================

static void
draw_picker(struct sc_game_ui *ui, struct sc_game *game) {
    ImVec2 center = vec2(igGetIO_Nil()->DisplaySize.x * 0.5f,
                         igGetIO_Nil()->DisplaySize.y * 0.5f);
    igSetNextWindowPos(center, ImGuiCond_Always, vec2(0.5f, 0.5f));
    if (!igBeginPopupModal("选择游戏配置", NULL,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    char names[32][SC_GAME_PROFILE_NAME_MAX];
    size_t count = sc_game_list_profiles(game, names, 32);
    if (!count) {
        igText("还没有任何配置文件。");
    }
    for (size_t i = 0; i < count; ++i) {
        igPushID_Int((int) i);
        if (igSelectable_Bool(names[i], false, ImGuiSelectableFlags_None,
                              vec2(260, 0))) {
            if (sc_game_load_profile(game, names[i])) {
                ui->picker_open = false;
                igCloseCurrentPopup();
            }
        }
        igSameLine(0, -1);
        if (igSmallButton("删除") && strcmp(names[i], game->profile.name)) {
            sc_game_delete_profile(game, names[i]);
        }
        igPopID();
    }

    igSeparator();
    igInputText("##newname", ui->new_profile_name,
                sizeof(ui->new_profile_name), ImGuiInputTextFlags_None, NULL,
                NULL);
    igSameLine(0, -1);
    if (igButton("新建", vec2(0, 0)) && ui->new_profile_name[0]) {
        if (sc_game_create_profile(game, ui->new_profile_name)) {
            ui->new_profile_name[0] = '\0';
            ui->picker_open = false;
            igCloseCurrentPopup();
        }
    }
    if (game->profile_loaded) {
        igSameLine(0, -1);
        if (igButton("关闭", vec2(0, 0))) {
            ui->picker_open = false;
            igCloseCurrentPopup();
        }
    } else {
        igTextDisabled("请先选择或新建一个配置");
    }
    char edit_hotkey[32];
    sc_game_hotkey_to_name(&game->profile.hotkey_edit, edit_hotkey,
                           sizeof(edit_hotkey));
    igTextDisabled("提示：也可关闭后在运行模式下按 %s 进入编辑",
                   edit_hotkey);
    igEndPopup();
}

// ============================================================================
// Init / events / render
// ============================================================================

static const char *default_font_paths[] = {
    // Prefer the medium-weight Simplified Chinese face: it stays crisp over a
    // moving game image and matches Material UI typography much better than
    // the thin-looking regular CJK face.
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "C:\\Windows\\Fonts\\msyh.ttc",
};

static int
font_face_for_path(const char *path) {
    // Face 2 is Simplified Chinese only for the known NotoSansCJK TTC.
    // User-provided TTF/OTF and other collections normally expose face 0.
    return path && strstr(path, "NotoSansCJK") && strstr(path, ".ttc")
         ? 2 : 0;
}

bool
sc_game_ui_init(struct sc_game_ui *ui, struct sc_game *game,
                SDL_Window *window, SDL_Renderer *renderer,
                const char *font_path, const char *ini_path) {
    ui->game = game;
    ui->renderer = renderer;
    ui->selected_element = -1;
    ui->label_edit_element = -1;
    ui->label_edit_buf[0] = '\0';
    ui->hover_elem = -1;
    ui->hover_handle = SC_GAME_UI_HANDLE_NONE;
    ui->drag_active = false;
    ui->drag_elem = -1;
    ui->drag_handle = SC_GAME_UI_HANDLE_NONE;
    ui->crop_editing = false;
    ui->crop_drag_active = false;
    ui->crop_hover_handle = SC_GAME_UI_CROP_HANDLE_NONE;
    ui->crop_drag_handle = SC_GAME_UI_CROP_HANDLE_NONE;
    ui->crop_drag_start_nx = 0.0f;
    ui->crop_drag_start_ny = 0.0f;
    ui->crop_drag_start_left = 0.0f;
    ui->crop_drag_start_top = 0.0f;
    ui->crop_drag_start_right = 1.0f;
    ui->crop_drag_start_bottom = 1.0f;
    ui->crop_profile_revision = game->profile_revision;

    // Effective UI scale: if SDL itself reports logical coordinates (window
    // size != renderer output size), HiDPI is already handled and rendered
    // through DisplayFramebufferScale; otherwise SDL gave us physical pixels
    // and we must scale the UI ourselves.
    int ww = 0;
    int wh = 0;
    int dw = 0;
    int dh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetRendererOutputSize(renderer, &dw, &dh);
    bool sdl_uses_logical_points = ww > 0 && wh > 0
                                && (dw != ww || dh != wh);
    ui->scale = sdl_uses_logical_points ? 1.0f : game->ui_scale;
    ui->picker_open = false;
    ui->new_profile_name[0] = '\0';
    ui->profile_create_popup = false;
    ui->profile_create_focus = false;
    ui->profile_create_failed = false;
    ui->template_popup = false;
    ui->template_selected = SC_GAME_TEMPLATE_OPEN_WORLD;
    ui->rename_popup = false;
    ui->rename_is_profile = false;
    ui->rename_focus = false;
    ui->rename_buf[0] = '\0';

    // HUD auto-hide bar: start expanded (visible on first entry into play
    // mode); the height estimate is replaced by the measured value each frame
    ui->hud_shown = true;
    ui->hud_anim_shown = true;
    ui->hud_anim = 1.0f;
    ui->hud_anim_from = 1.0f;
    ui->hud_anim_t = 0.0f;
    ui->hud_idle = 0.0f;
    ui->hud_h = 56.0f * ui->scale;
    ui->hud_last_mx = -1e9f;
    ui->hud_last_my = -1e9f;
    for (int i = 0; i < 5; ++i) {
        ui->hud_last_mdown[i] = false;
    }

    igCreateContext(NULL);
    ImGuiIO *io = igGetIO_Nil();

    // window layout persistence
    char *ini = strdup(ini_path);
    if (ini) {
        io->IniFilename = ini;
    }

    // style: dark, rounded, with breathing room. Set as base values BEFORE
    // ImGuiStyle_ScaleAllSizes() below so HiDPI scales them exactly once.
    ImGuiStyle *style = igGetStyle();
    igStyleColorsDark(NULL);
    style->WindowRounding = 8.0f;
    style->WindowBorderSize = 0.0f;
    style->FrameRounding = 5.0f;
    style->GrabRounding = 5.0f;
    style->PopupRounding = 6.0f;
    style->ScrollbarSize = 12.0f;
    style->WindowPadding = (ImVec2) {14.0f, 12.0f};
    style->FramePadding = (ImVec2) {10.0f, 7.0f};
    style->ItemSpacing = (ImVec2) {10.0f, 9.0f};
    style->ItemInnerSpacing = (ImVec2) {8.0f, 6.0f};
    style->Colors[ImGuiCol_TextDisabled] =
        (ImVec4) {0.82f, 0.84f, 0.88f, 1.0f};
    style->Colors[ImGuiCol_WindowBg] =
        (ImVec4) {0.08f, 0.08f, 0.10f, 0.94f};
    style->Colors[ImGuiCol_PopupBg] =
        (ImVec4) {0.08f, 0.08f, 0.10f, 0.97f};
    style->Colors[ImGuiCol_CheckMark] =
        (ImVec4) {0.35f, 0.85f, 0.50f, 1.0f};
    style->Colors[ImGuiCol_SliderGrab] =
        (ImVec4) {0.35f, 0.85f, 0.50f, 1.0f};

    float font_px = 17.0f * ui->scale;
    if (ui->scale > 1.0f) {
        ImGuiStyle_ScaleAllSizes(style, ui->scale);
    }

    // Build one compact atlas. The stock Simplified Chinese common range
    // handles normal names; explicitly add all fixed UI copy and dynamic
    // profile/preset/element labels so uncommon glyphs remain available.
    bool font_loaded = false;
    ImFontConfig *fc = ImFontConfig_ImFontConfig();
    ImFontGlyphRangesBuilder *range_builder =
        ImFontGlyphRangesBuilder_ImFontGlyphRangesBuilder();
    ImVector_ImWchar *font_ranges = ImVector_ImWchar_create();
    if (!fc || !range_builder || !font_ranges) {
        LOGE("Game mode: could not allocate font configuration");
        if (fc) {
            ImFontConfig_destroy(fc);
        }
        if (range_builder) {
            ImFontGlyphRangesBuilder_destroy(range_builder);
        }
        if (font_ranges) {
            ImVector_ImWchar_destroy(font_ranges);
        }
        return false;
    }
    ImFontGlyphRangesBuilder_AddRanges(
        range_builder,
        ImFontAtlas_GetGlyphRangesChineseSimplifiedCommon(io->Fonts));
    static const char ui_glyphs[] =
        "返回运行模式添加虚拟摇杆滑动移动瞄准起点按键映射打开关闭详细面板"
        "已自动保存正在保存失败配置预设控件列表属性鼠标拟人抖动重命名确定取消"
        "名称不能为空也不能包含空白斜杠或控制字符新建删除当前方向点击后按下任意键"
        "触发拖动时长重复间隔高级位置起点终点中心单击按住触控范围"
        "可选绿色蓝色连线整体移动摇杆抖动灵敏度位置"
        "在画面上选择以编辑把准星拖到空旷处视角手指持续屏幕边缘立即回位永远"
        "不会碰到按钮启用相对灵敏度时序随机扰动幅度游戏模式方案临时解锁切换"
        "锁定还没有任何文件提示也可运行进入未绑定组合键正在选择创建重名"
        "一套独立游戏不会覆盖切换检查目录"
        "默认键位大致对齐常用裁剪画面完整居中完成拖动边缘九宫格";
    ImFontGlyphRangesBuilder_AddText(range_builder, ui_glyphs, NULL);
    for (size_t i = 0; i < sc_game_template_count(); ++i) {
        const struct sc_game_template_info *info = sc_game_template_at(i);
        ImFontGlyphRangesBuilder_AddText(range_builder, info->name, NULL);
        ImFontGlyphRangesBuilder_AddText(range_builder, info->description,
                                         NULL);
        ImFontGlyphRangesBuilder_AddText(range_builder,
                                         info->recommended_for, NULL);
        ImFontGlyphRangesBuilder_AddText(range_builder, info->controls, NULL);
    }
    ImFontGlyphRangesBuilder_AddText(range_builder, game->profile.name, NULL);
    char profile_names[32][SC_GAME_PROFILE_NAME_MAX];
    size_t profile_count = sc_game_list_profiles(game, profile_names, 32);
    for (size_t i = 0; i < profile_count; ++i) {
        ImFontGlyphRangesBuilder_AddText(range_builder, profile_names[i], NULL);
    }
    for (size_t i = 0; i < game->profile.preset_count; ++i) {
        const struct sc_game_preset *preset = &game->profile.presets[i];
        ImFontGlyphRangesBuilder_AddText(range_builder, preset->name, NULL);
        for (size_t j = 0; j < preset->element_count; ++j) {
            ImFontGlyphRangesBuilder_AddText(range_builder,
                                             preset->elements[j].label, NULL);
        }
    }
    ImFontGlyphRangesBuilder_BuildRanges(range_builder, font_ranges);
    ImFontGlyphRangesBuilder_destroy(range_builder);
    ui->font_ranges = font_ranges;
    const ImWchar *glyph_ranges = font_ranges->Data;
    if (font_path) {
        fc->FontNo = font_face_for_path(font_path);
        ImFont *font = ImFontAtlas_AddFontFromFileTTF(io->Fonts, font_path,
                                                      font_px, fc,
                                                      glyph_ranges);
        font_loaded = font != NULL;
        if (!font_loaded) {
            LOGW("Game mode: could not load font '%s'", font_path);
        }
    }
    if (!font_loaded) {
        for (size_t i = 0; i < ARRAY_LEN(default_font_paths); ++i) {
            fc->FontNo = font_face_for_path(default_font_paths[i]);
            ImFont *font =
                ImFontAtlas_AddFontFromFileTTF(io->Fonts,
                                               default_font_paths[i], font_px,
                                               fc, glyph_ranges);
            if (font) {
                font_loaded = true;
                LOGI("Game mode: using font %s", default_font_paths[i]);
                break;
            }
        }
    }
    if (!font_loaded) {
        LOGW("Game mode: no CJK font found, UI may not display Chinese");
    }

    ImFontConfig_destroy(fc);

    if (!sc_imgui_sdl_init_for_sdl_renderer(window, renderer)) {
        LOGE("Game mode: could not init ImGui SDL2 backend");
        return false;
    }
    if (!sc_imgui_sdlrenderer2_init(renderer)) {
        LOGE("Game mode: could not init ImGui SDL renderer backend");
        return false;
    }

    ui->inited = true;
    return true;
}

void
sc_game_ui_end_crop(struct sc_game_ui *ui) {
    if (ui && ui->crop_editing) {
        crop_set_editing(ui, false);
    }
}

void
sc_game_ui_destroy(struct sc_game_ui *ui) {
    if (!ui->inited) {
        return;
    }
    sc_game_ui_end_crop(ui);
    ImGuiIO *io = igGetIO_Nil();
    if (io->IniFilename) {
        // our strdup'ed copy
        free((void *) io->IniFilename);
        io->IniFilename = NULL;
    }
    sc_imgui_sdlrenderer2_shutdown();
    sc_imgui_sdl_shutdown();
    igDestroyContext(NULL);
    ImVector_ImWchar_destroy(ui->font_ranges);
    ui->font_ranges = NULL;
    ui->inited = false;
}

bool
sc_game_ui_process_event(struct sc_game_ui *ui, const SDL_Event *event,
                         bool edit_mode) {
    bool is_edit_mode = edit_mode
                     || (ui->game
                         && ui->game->mode == SC_GAME_MODE_EDIT);
    if (is_edit_mode && ui->crop_editing && event->type == SDL_KEYDOWN
            && !event->key.repeat && event->key.keysym.sym == SDLK_ESCAPE) {
        crop_set_editing(ui, false);
        return true;
    }
    return sc_imgui_sdl_process_event(event);
}

void
sc_game_ui_render(struct sc_game_ui *ui) {
    if (!ui->inited || !ui->game->screen) {
        return;
    }
    struct sc_game *game = ui->game;
    bool edit_mode = game->mode == SC_GAME_MODE_EDIT;
    bool return_to_play = false;

    // A hotkey or an external state transition may leave edit mode without a
    // toolbar frame. Never leak the full-frame crop preview into gameplay.
    if (!edit_mode && ui->crop_editing) {
        crop_set_editing(ui, false);
    }

    // HiDPI: ImGui works in window points, rendering is scaled to pixels
    int ww;
    int wh;
    SDL_GetWindowSize(game->screen->window, &ww, &wh);
    int dw;
    int dh;
    SDL_GetRendererOutputSize(ui->renderer, &dw, &dh);
    ImGuiIO *io = igGetIO_Nil();
    if (ww > 0 && wh > 0) {
        io->DisplayFramebufferScale.x = (float) dw / ww;
        io->DisplayFramebufferScale.y = (float) dh / wh;
    }

    sc_imgui_sdl_new_frame();
    sc_imgui_sdlrenderer2_new_frame();
    // Guarantee a valid mouse position: the SDL2 backend reports the mouse
    // as "outside" whenever the window lacks mouse focus (focus flips during
    // capture toggles, Wayland quirks), which would freeze every button.
    {
        int mx = 0;
        int my = 0;
        uint32_t mstate = SDL_GetMouseState(&mx, &my);
        int wwd = 0;
        int whd = 0;
        SDL_GetWindowSize(game->screen->window, &wwd, &whd);
        if (mx >= 0 && my >= 0 && mx < wwd && my < whd) {
            ImGuiIO_AddMousePosEvent(io, (float) mx, (float) my);
        }
        ImGuiIO_AddMouseButtonEvent(io, ImGuiMouseButton_Left,
                                    (mstate & SDL_BUTTON_LMASK) != 0);
        ImGuiIO_AddMouseButtonEvent(io, ImGuiMouseButton_Right,
                                    (mstate & SDL_BUTTON_RMASK) != 0);
    }
    igNewFrame();

    // Detect every editor mutation, including canvas drag/wheel operations,
    // without scattering dirty calls across all individual widgets.
    struct sc_game_profile profile_before = game->profile;
    uint64_t revision_before = game->profile_revision;

    if (ui->crop_editing
            && ui->crop_profile_revision != game->profile_revision) {
        // Profile switches replace the complete working model. Cancel a drag
        // started against the old rectangle and preview the new one.
        ui->crop_drag_active = false;
        ui->crop_drag_handle = SC_GAME_UI_CROP_HANDLE_NONE;
        ui->crop_profile_revision = game->profile_revision;
        crop_apply_preview(ui, true);
    }

    // ghost interaction first (uses this frame's mouse state); visuals after.
    // Ghosts are an editor concept only: play mode shows a clean game view.
    if (edit_mode && !ui->crop_editing) {
        update_ghost_interaction(ui, game);
        draw_ghosts(ui, game, true);
    } else if (edit_mode) {
        update_crop_interaction(ui, game);
        draw_crop_editor(ui, game);
    }

    if (edit_mode) {
        return_to_play = draw_toolbar(ui, game);
        if (ui->panel_open && !ui->crop_editing) {
            draw_panel(ui, game);
        }
    } else {
        draw_hud(ui, game);
    }

    if (ui->picker_open) {
        if (!igIsPopupOpen_Str("选择游戏配置", ImGuiPopupFlags_None)) {
            igOpenPopup_Str("选择游戏配置", ImGuiPopupFlags_None);
        }
        draw_picker(ui, game);
    }

    if (edit_mode && revision_before == game->profile_revision
            && memcmp(&profile_before, &game->profile,
                            sizeof(profile_before))) {
        sc_game_mark_profile_dirty(game);
    }

    igRender();
    sc_imgui_sdlrenderer2_render(sc_imgui_get_draw_data(), ui->renderer);

    if (return_to_play) {
        // All editor widgets have committed and the snapshot comparison above
        // has marked the profile dirty. set_mode() can now synchronously save
        // and apply the final state without losing a same-frame edit.
        sc_game_set_mode(game, SC_GAME_MODE_PLAY);
    }
}
