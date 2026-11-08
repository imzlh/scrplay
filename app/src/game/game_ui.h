#ifndef SC_GAME_UI_H
#define SC_GAME_UI_H

#include "common.h"

#include <stdbool.h>

#include "game_profile.h"

#include <SDL2/SDL.h>

struct sc_game;

enum sc_game_ui_drag_handle {
    SC_GAME_UI_HANDLE_NONE,
    SC_GAME_UI_HANDLE_ELEMENT,
    SC_GAME_UI_HANDLE_SWIPE_FROM,
    SC_GAME_UI_HANDLE_SWIPE_TO,
    SC_GAME_UI_HANDLE_SWIPE_WHOLE,
};

// Crop handles are tied to edges in the unrotated decoded frame, not to
// their apparent direction in the window. This keeps dragging correct for
// every rotation and mirrored display orientation.
enum sc_game_ui_crop_handle {
    SC_GAME_UI_CROP_HANDLE_NONE,
    SC_GAME_UI_CROP_HANDLE_MOVE,
    SC_GAME_UI_CROP_HANDLE_LEFT,
    SC_GAME_UI_CROP_HANDLE_RIGHT,
    SC_GAME_UI_CROP_HANDLE_TOP,
    SC_GAME_UI_CROP_HANDLE_BOTTOM,
    SC_GAME_UI_CROP_HANDLE_TOP_LEFT,
    SC_GAME_UI_CROP_HANDLE_TOP_RIGHT,
    SC_GAME_UI_CROP_HANDLE_BOTTOM_LEFT,
    SC_GAME_UI_CROP_HANDLE_BOTTOM_RIGHT,
};

// All the Dear ImGui drawing for game mode: the start-up profile picker, the
// edit-mode editor (top toolbar, side panel and on-screen ghost widgets), and
// the play-mode HUD. Pure C, talking to cimgui.
//
// Architecture note: the ghost widgets are pure draw-list output. They never
// create ImGui windows or items, so they can never steal clicks from the
// editor; dragging/resizing is implemented by manual hit-testing against the
// ImGui mouse state (see update_ghost_interaction()).
struct sc_game_ui {
    bool inited;
    struct sc_game *game;
    SDL_Renderer *renderer;

    float scale; // effective UI scale (1 = none)
    void *font_ranges; // owned ImWchar array kept alive for the font atlas

    // editor state
    int selected_element; // index in current preset, -1 = none
    int label_edit_element; // selected element mirrored by label_edit_buf
    char label_edit_buf[SC_GAME_ELEMENT_LABEL_MAX];
    bool panel_open;      // edit mode: detailed side panel expanded
    bool picker_open;     // start-up profile picker

    // ghost interaction (manual hit-testing, no ImGui items involved)
    int hover_elem;    // element under the mouse, -1 = none
    enum sc_game_ui_drag_handle hover_handle;
    bool drag_active;  // LMB held on a ghost
    int drag_elem;     // element being dragged
    enum sc_game_ui_drag_handle drag_handle;
    float drag_grab_nx; // grab offset: mouse norm - anchor norm
    float drag_grab_ny;

    // Album-style crop editor. While active, the complete decoded frame is
    // previewed and the discarded area is shaded by the UI. Runtime drawing
    // submits only the selected source rectangle.
    bool crop_editing;
    bool crop_drag_active;
    enum sc_game_ui_crop_handle crop_hover_handle;
    enum sc_game_ui_crop_handle crop_drag_handle;
    float crop_drag_start_nx;
    float crop_drag_start_ny;
    float crop_drag_start_left;
    float crop_drag_start_top;
    float crop_drag_start_right;
    float crop_drag_start_bottom;
    uint64_t crop_profile_revision;

    char new_profile_name[SC_GAME_PROFILE_NAME_MAX];
    bool profile_create_popup;
    bool profile_create_focus;
    bool profile_create_failed;
    bool template_popup;
    int template_selected;

    // rename popup (pencil button on the 配置/预设 rows)
    bool rename_popup;      // modal open
    bool rename_is_profile; // true = profile, false = current preset
    bool rename_focus;      // focus the input on the first frame
    char rename_buf[SC_GAME_PROFILE_NAME_MAX];

    // play-mode HUD: top-center auto-hide bar. Any user input collapses it,
    // a few idle seconds (or hovering the top strip with a free mouse)
    // expands it again; transitions are eased per-frame from io->DeltaTime.
    bool hud_shown;        // target state: bar expanded
    bool hud_anim_shown;   // previous target, to detect transition restarts
    float hud_anim;        // eased 0..1: 0 = hidden, 1 = expanded
    float hud_anim_from;   // anim value when the current transition started
    float hud_anim_t;      // seconds elapsed in the current transition
    float hud_idle;        // seconds since the last detected user activity
    float hud_h;           // last measured bar height (slide-out distance)
    float hud_last_mx;     // mouse pos last frame (activity detection),
    float hud_last_my;     // -1e9 = not yet seen a valid position
    bool hud_last_mdown[5]; // mouse buttons last frame
};

bool
sc_game_ui_init(struct sc_game_ui *ui, struct sc_game *game,
                SDL_Window *window, SDL_Renderer *renderer,
                const char *font_path, const char *ini_path);

void
sc_game_ui_destroy(struct sc_game_ui *ui);

// Synchronously leave the crop tool and restore runtime crop rendering. Mode
// hotkeys call this before leaving EDIT so preview coordinates can never leak
// into gameplay while video frames are paused.
void
sc_game_ui_end_crop(struct sc_game_ui *ui);

// Feed an SDL event to ImGui. Returns true if the event must be consumed
// (not forwarded to the device / input manager).
bool
sc_game_ui_process_event(struct sc_game_ui *ui, const SDL_Event *event,
                         bool edit_mode);

// Draw one full ImGui frame for the current game mode.
void
sc_game_ui_render(struct sc_game_ui *ui);

#endif
