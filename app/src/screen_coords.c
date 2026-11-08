#include "screen_coords.h"

#include <math.h>

static bool
is_valid_size(struct sc_screen_fsize size) {
    return isfinite(size.width) && size.width > 0.0f
        && isfinite(size.height) && size.height > 0.0f;
}

static bool
is_valid_point(struct sc_screen_fpoint point) {
    return isfinite(point.x) && isfinite(point.y);
}

bool
sc_screen_coords_is_valid(const struct sc_screen_coords *coords) {
    if (!coords || !is_valid_size(coords->frame_size)
            || !is_valid_size(coords->content_size)
            || !is_valid_size(coords->output_size)
            || !is_valid_size(coords->window_size)) {
        return false;
    }

    if ((unsigned) coords->orientation > SC_ORIENTATION_FLIP_270) {
        return false;
    }

    const struct sc_screen_frect *frame_rect = &coords->frame_rect;
    if (!isfinite(frame_rect->x) || frame_rect->x < 0.0f
            || !isfinite(frame_rect->y) || frame_rect->y < 0.0f
            || !isfinite(frame_rect->width) || frame_rect->width <= 0.0f
            || !isfinite(frame_rect->height) || frame_rect->height <= 0.0f
            || frame_rect->x + frame_rect->width
                    > coords->frame_size.width
            || frame_rect->y + frame_rect->height
                    > coords->frame_size.height) {
        return false;
    }

    bool swap = sc_orientation_is_swap(coords->orientation);
    float expected_content_width = swap ? frame_rect->height
                                        : frame_rect->width;
    float expected_content_height = swap ? frame_rect->width
                                         : frame_rect->height;
    if (coords->content_size.width != expected_content_width
            || coords->content_size.height != expected_content_height) {
        return false;
    }

    const struct sc_screen_frect *rect = &coords->content_rect;
    return isfinite(rect->x) && isfinite(rect->y)
        && isfinite(rect->width) && rect->width > 0.0f
        && isfinite(rect->height) && rect->height > 0.0f;
}

static struct sc_screen_fpoint
frame_to_content_point(const struct sc_screen_coords *coords,
                       struct sc_screen_fpoint p) {
    float w = coords->content_size.width;
    float h = coords->content_size.height;

    switch (coords->orientation) {
        case SC_ORIENTATION_0:
            return p;
        case SC_ORIENTATION_90:
            return (struct sc_screen_fpoint) {w - p.y, p.x};
        case SC_ORIENTATION_180:
            return (struct sc_screen_fpoint) {w - p.x, h - p.y};
        case SC_ORIENTATION_270:
            return (struct sc_screen_fpoint) {p.y, h - p.x};
        case SC_ORIENTATION_FLIP_0:
            return (struct sc_screen_fpoint) {w - p.x, p.y};
        case SC_ORIENTATION_FLIP_90:
            return (struct sc_screen_fpoint) {w - p.y, h - p.x};
        case SC_ORIENTATION_FLIP_180:
            return (struct sc_screen_fpoint) {p.x, h - p.y};
        case SC_ORIENTATION_FLIP_270:
            return (struct sc_screen_fpoint) {p.y, p.x};
    }

    // The public entry points validate the enum first.
    return (struct sc_screen_fpoint) {0.0f, 0.0f};
}

static struct sc_screen_fpoint
content_to_frame_point(const struct sc_screen_coords *coords,
                       struct sc_screen_fpoint p) {
    float w = coords->content_size.width;
    float h = coords->content_size.height;

    switch (coords->orientation) {
        case SC_ORIENTATION_0:
            return p;
        case SC_ORIENTATION_90:
            return (struct sc_screen_fpoint) {p.y, w - p.x};
        case SC_ORIENTATION_180:
            return (struct sc_screen_fpoint) {w - p.x, h - p.y};
        case SC_ORIENTATION_270:
            return (struct sc_screen_fpoint) {h - p.y, p.x};
        case SC_ORIENTATION_FLIP_0:
            return (struct sc_screen_fpoint) {w - p.x, p.y};
        case SC_ORIENTATION_FLIP_90:
            return (struct sc_screen_fpoint) {h - p.y, w - p.x};
        case SC_ORIENTATION_FLIP_180:
            return (struct sc_screen_fpoint) {p.x, h - p.y};
        case SC_ORIENTATION_FLIP_270:
            return (struct sc_screen_fpoint) {p.y, p.x};
    }

    return (struct sc_screen_fpoint) {0.0f, 0.0f};
}

static struct sc_screen_fpoint
frame_to_content_vector(enum sc_orientation orientation,
                        struct sc_screen_fpoint v) {
    switch (orientation) {
        case SC_ORIENTATION_0:
            return v;
        case SC_ORIENTATION_90:
            return (struct sc_screen_fpoint) {-v.y, v.x};
        case SC_ORIENTATION_180:
            return (struct sc_screen_fpoint) {-v.x, -v.y};
        case SC_ORIENTATION_270:
            return (struct sc_screen_fpoint) {v.y, -v.x};
        case SC_ORIENTATION_FLIP_0:
            return (struct sc_screen_fpoint) {-v.x, v.y};
        case SC_ORIENTATION_FLIP_90:
            return (struct sc_screen_fpoint) {-v.y, -v.x};
        case SC_ORIENTATION_FLIP_180:
            return (struct sc_screen_fpoint) {v.x, -v.y};
        case SC_ORIENTATION_FLIP_270:
            return (struct sc_screen_fpoint) {v.y, v.x};
    }

    return (struct sc_screen_fpoint) {0.0f, 0.0f};
}

static struct sc_screen_fpoint
content_to_frame_vector(enum sc_orientation orientation,
                        struct sc_screen_fpoint v) {
    switch (orientation) {
        case SC_ORIENTATION_0:
            return v;
        case SC_ORIENTATION_90:
            return (struct sc_screen_fpoint) {v.y, -v.x};
        case SC_ORIENTATION_180:
            return (struct sc_screen_fpoint) {-v.x, -v.y};
        case SC_ORIENTATION_270:
            return (struct sc_screen_fpoint) {-v.y, v.x};
        case SC_ORIENTATION_FLIP_0:
            return (struct sc_screen_fpoint) {-v.x, v.y};
        case SC_ORIENTATION_FLIP_90:
            return (struct sc_screen_fpoint) {-v.y, -v.x};
        case SC_ORIENTATION_FLIP_180:
            return (struct sc_screen_fpoint) {v.x, -v.y};
        case SC_ORIENTATION_FLIP_270:
            return (struct sc_screen_fpoint) {v.y, v.x};
    }

    return (struct sc_screen_fpoint) {0.0f, 0.0f};
}

bool
sc_screen_coords_frame_to_window_point(
        const struct sc_screen_coords *coords, struct sc_screen_fpoint frame,
        struct sc_screen_fpoint *window) {
    if (!window || !sc_screen_coords_is_valid(coords)
            || !is_valid_point(frame)) {
        return false;
    }

    // Orientation is applied to crop-local coordinates. Keep the crop offset
    // out of that transform so its inverse can restore complete device-frame
    // coordinates after client-side rotation.
    struct sc_screen_fpoint local = {
        .x = frame.x - coords->frame_rect.x,
        .y = frame.y - coords->frame_rect.y,
    };
    struct sc_screen_fpoint content = frame_to_content_point(coords, local);
    struct sc_screen_fpoint result = {
        .x = (coords->content_rect.x
            + content.x * coords->content_rect.width
                          / coords->content_size.width)
           * coords->window_size.width / coords->output_size.width,
        .y = (coords->content_rect.y
            + content.y * coords->content_rect.height
                          / coords->content_size.height)
           * coords->window_size.height / coords->output_size.height,
    };
    if (!is_valid_point(result)) {
        return false;
    }

    *window = result;
    return true;
}

bool
sc_screen_coords_window_to_frame_point(
        const struct sc_screen_coords *coords, struct sc_screen_fpoint window,
        struct sc_screen_fpoint *frame) {
    if (!frame || !sc_screen_coords_is_valid(coords)
            || !is_valid_point(window)) {
        return false;
    }

    struct sc_screen_fpoint content = {
        .x = (window.x * coords->output_size.width
                        / coords->window_size.width
            - coords->content_rect.x)
           * coords->content_size.width / coords->content_rect.width,
        .y = (window.y * coords->output_size.height
                        / coords->window_size.height
            - coords->content_rect.y)
           * coords->content_size.height / coords->content_rect.height,
    };
    struct sc_screen_fpoint local = content_to_frame_point(coords, content);
    struct sc_screen_fpoint result = {
        .x = local.x + coords->frame_rect.x,
        .y = local.y + coords->frame_rect.y,
    };
    if (!is_valid_point(result)) {
        return false;
    }

    *frame = result;
    return true;
}

bool
sc_screen_coords_window_to_frame_point_checked(
        const struct sc_screen_coords *coords, struct sc_screen_fpoint window,
        struct sc_screen_fpoint *frame) {
    if (!frame || !sc_screen_coords_is_valid(coords)
            || !is_valid_point(window)) {
        return false;
    }

    // Mouse events are expressed in logical window points, while the content
    // rectangle is expressed in renderer-output pixels.
    struct sc_screen_fpoint output = {
        .x = window.x * coords->output_size.width
                      / coords->window_size.width,
        .y = window.y * coords->output_size.height
                      / coords->window_size.height,
    };
    const struct sc_screen_frect *rect = &coords->content_rect;
    if (output.x < rect->x || output.x >= rect->x + rect->width
            || output.y < rect->y || output.y >= rect->y + rect->height) {
        return false;
    }

    struct sc_screen_fpoint result;
    if (!sc_screen_coords_window_to_frame_point(coords, window, &result)) {
        return false;
    }

    // Cardinal rotations map some included content edges to the mathematical
    // crop extent. Touch coordinates, however, address pixels and therefore
    // end one pixel before the exclusive right/bottom crop boundaries.
    float min_x = coords->frame_rect.x;
    float min_y = coords->frame_rect.y;
    float max_x = min_x + coords->frame_rect.width - 1.0f;
    float max_y = min_y + coords->frame_rect.height - 1.0f;
    if (result.x < min_x) {
        result.x = min_x;
    } else if (result.x > max_x) {
        result.x = max_x;
    }
    if (result.y < min_y) {
        result.y = min_y;
    } else if (result.y > max_y) {
        result.y = max_y;
    }

    *frame = result;
    return true;
}

bool
sc_screen_coords_frame_to_window_vector(
        const struct sc_screen_coords *coords, struct sc_screen_fpoint frame,
        struct sc_screen_fpoint *window) {
    if (!window || !sc_screen_coords_is_valid(coords)
            || !is_valid_point(frame)) {
        return false;
    }

    struct sc_screen_fpoint content =
        frame_to_content_vector(coords->orientation, frame);
    struct sc_screen_fpoint result = {
        .x = content.x * coords->content_rect.width
                       / coords->content_size.width
                       * coords->window_size.width
                       / coords->output_size.width,
        .y = content.y * coords->content_rect.height
                       / coords->content_size.height
                       * coords->window_size.height
                       / coords->output_size.height,
    };
    if (!is_valid_point(result)) {
        return false;
    }

    *window = result;
    return true;
}

bool
sc_screen_coords_window_to_frame_vector(
        const struct sc_screen_coords *coords, struct sc_screen_fpoint window,
        struct sc_screen_fpoint *frame) {
    if (!frame || !sc_screen_coords_is_valid(coords)
            || !is_valid_point(window)) {
        return false;
    }

    struct sc_screen_fpoint content = {
        .x = window.x * coords->output_size.width
                      / coords->window_size.width
                      * coords->content_size.width
                      / coords->content_rect.width,
        .y = window.y * coords->output_size.height
                      / coords->window_size.height
                      * coords->content_size.height
                      / coords->content_rect.height,
    };
    struct sc_screen_fpoint result =
        content_to_frame_vector(coords->orientation, content);
    if (!is_valid_point(result)) {
        return false;
    }

    *frame = result;
    return true;
}
