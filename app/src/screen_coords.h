#ifndef SC_SCREEN_COORDS_H
#define SC_SCREEN_COORDS_H

#include "common.h"

#include <stdbool.h>

#include "options.h"

// Floating-point geometry used by the screen coordinate transforms. Keeping
// this module independent from SDL makes the transforms deterministic and
// straightforward to unit-test.
struct sc_screen_fpoint {
    float x;
    float y;
};

struct sc_screen_fsize {
    float width;
    float height;
};

struct sc_screen_frect {
    float x;
    float y;
    float width;
    float height;
};

// Snapshot of every geometry involved in screen coordinate conversion:
//
//   frame   complete device-video coordinates, before client-side orientation
//   crop    visible rectangle within the complete frame (frame_rect)
//   content oriented crop coordinates
//   output  renderer output coordinates (physical pixels)
//   window  SDL event/ImGui coordinates (window points)
//   rect    content rectangle within the renderer output (letterboxing removed)
//
// The caller is responsible for taking a coherent snapshot. All dimensions
// and rect extents must be finite and strictly positive.
struct sc_screen_coords {
    struct sc_screen_fsize frame_size;
    struct sc_screen_frect frame_rect;
    struct sc_screen_fsize content_size;
    struct sc_screen_fsize output_size;
    struct sc_screen_fsize window_size;
    struct sc_screen_frect content_rect;
    enum sc_orientation orientation;
};

bool
sc_screen_coords_is_valid(const struct sc_screen_coords *coords);

// Transform positions between complete device-frame coordinates and window
// coordinates. Unlike vectors, positions include both the frame_rect crop
// offset and content_rect.x/y.
// On failure (invalid geometry, non-finite input or NULL output), return false
// without writing the output.
bool
sc_screen_coords_frame_to_window_point(
    const struct sc_screen_coords *coords, struct sc_screen_fpoint frame,
    struct sc_screen_fpoint *window);

bool
sc_screen_coords_window_to_frame_point(
    const struct sc_screen_coords *coords, struct sc_screen_fpoint window,
    struct sc_screen_fpoint *frame);

// Convert a window point only if it is inside the displayed video content.
// The content rectangle uses the usual half-open bounds: left/top are
// included, right/bottom are excluded. The result is clamped to actual frame
// pixels inside frame_rect, so it is safe for touch injection even on rotated
// crop edges and fractional HiDPI scales.
bool
sc_screen_coords_window_to_frame_point_checked(
    const struct sc_screen_coords *coords, struct sc_screen_fpoint window,
    struct sc_screen_fpoint *frame);

// Transform displacement vectors. Translation (including letterbox offsets)
// is intentionally ignored. These functions are suitable for relative mouse
// motion and for converting a frame-space radius to window-space length.
bool
sc_screen_coords_frame_to_window_vector(
    const struct sc_screen_coords *coords, struct sc_screen_fpoint frame,
    struct sc_screen_fpoint *window);

bool
sc_screen_coords_window_to_frame_vector(
    const struct sc_screen_coords *coords, struct sc_screen_fpoint window,
    struct sc_screen_fpoint *frame);

#endif
