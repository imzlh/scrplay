#include "common.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>

#include "screen_coords.h"

#define EPSILON 0.002f

static void
assert_close(float actual, float expected) {
    assert(fabsf(actual - expected) <= EPSILON);
}

static void
assert_point_close(struct sc_screen_fpoint actual,
                   struct sc_screen_fpoint expected) {
    assert_close(actual.x, expected.x);
    assert_close(actual.y, expected.y);
}

static struct sc_screen_coords
make_coords(enum sc_orientation orientation, float dpi,
            bool side_letterbox) {
    struct sc_screen_fsize frame = {1920.0f, 1080.0f};
    struct sc_screen_fsize content = sc_orientation_is_swap(orientation)
        ? (struct sc_screen_fsize) {frame.height, frame.width}
        : frame;

    // Uniform renderer scaling is intentional: a frame-height-based radius
    // must remain a circle after any orientation. Only the black-border axis
    // differs between the two fixtures.
    float content_scale = 0.625f * dpi;
    float rect_w = content.width * content_scale;
    float rect_h = content.height * content_scale;
    float border = 137.0f * dpi;
    struct sc_screen_fsize output = side_letterbox
        ? (struct sc_screen_fsize) {rect_w + 2.0f * border, rect_h}
        : (struct sc_screen_fsize) {rect_w, rect_h + 2.0f * border};
    struct sc_screen_frect rect = side_letterbox
        ? (struct sc_screen_frect) {border, 0.0f, rect_w, rect_h}
        : (struct sc_screen_frect) {0.0f, border, rect_w, rect_h};

    return (struct sc_screen_coords) {
        .frame_size = frame,
        .frame_rect = {0.0f, 0.0f, frame.width, frame.height},
        .content_size = content,
        .output_size = output,
        .window_size = {output.width / dpi, output.height / dpi},
        .content_rect = rect,
        .orientation = orientation,
    };
}

// Deliberately use different X/Y framebuffer scales and asymmetric black
// borders. This makes the expected values independent enough to catch a
// swapped axis, a missing HiDPI conversion, or letterbox translation applied
// to relative mouse motion (plain round trips would hide those bugs).
static struct sc_screen_coords
make_golden_coords(enum sc_orientation orientation) {
    bool swap = sc_orientation_is_swap(orientation);
    return (struct sc_screen_coords) {
        .frame_size = {1200.0f, 800.0f},
        .frame_rect = {0.0f, 0.0f, 1200.0f, 800.0f},
        .content_size = swap
            ? (struct sc_screen_fsize) {800.0f, 1200.0f}
            : (struct sc_screen_fsize) {1200.0f, 800.0f},
        .output_size = {1500.0f, 1400.0f},
        .window_size = {1000.0f, 700.0f},
        .content_rect = swap
            ? (struct sc_screen_frect) {350.0f, 100.0f, 800.0f, 1200.0f}
            : (struct sc_screen_frect) {150.0f, 300.0f, 1200.0f, 800.0f},
        .orientation = orientation,
    };
}

static struct sc_screen_coords
make_cropped_coords(enum sc_orientation orientation, float dpi) {
    struct sc_screen_fsize frame = {2160.0f, 1080.0f};
    struct sc_screen_frect crop = {120.0f, 60.0f, 1800.0f, 900.0f};
    bool swap = sc_orientation_is_swap(orientation);
    struct sc_screen_fsize content = swap
        ? (struct sc_screen_fsize) {crop.height, crop.width}
        : (struct sc_screen_fsize) {crop.width, crop.height};

    float scale = 0.7f * dpi;
    struct sc_screen_frect rect = {
        .x = 140.0f * dpi,
        .y = 90.0f * dpi,
        .width = content.width * scale,
        .height = content.height * scale,
    };
    struct sc_screen_fsize output = {
        .width = rect.width + 320.0f * dpi,
        .height = rect.height + 180.0f * dpi,
    };
    return (struct sc_screen_coords) {
        .frame_size = frame,
        .frame_rect = crop,
        .content_size = content,
        .output_size = output,
        .window_size = {output.width / dpi, output.height / dpi},
        .content_rect = rect,
        .orientation = orientation,
    };
}

static void
test_rotation_goldens(void) {
    static const struct {
        enum sc_orientation orientation;
        struct sc_screen_fpoint window_point;
        struct sc_screen_fpoint window_vector;
    } cases[] = {
        {SC_ORIENTATION_0,   {300.0f, 250.0f},
                             {26.666667f, -10.0f}},
        {SC_ORIENTATION_90,  {633.333333f, 200.0f},
                             {13.333333f, 20.0f}},
        {SC_ORIENTATION_180, {700.0f, 450.0f},
                             {-26.666667f, 10.0f}},
        {SC_ORIENTATION_270, {366.666667f, 500.0f},
                             {-13.333333f, -20.0f}},
    };
    const struct sc_screen_fpoint frame_point = {300.0f, 200.0f};
    const struct sc_screen_fpoint frame_vector = {40.0f, -20.0f};

    for (size_t i = 0; i < ARRAY_LEN(cases); ++i) {
        struct sc_screen_coords coords =
            make_golden_coords(cases[i].orientation);
        struct sc_screen_fpoint actual;

        assert(sc_screen_coords_frame_to_window_point(
            &coords, frame_point, &actual));
        assert_point_close(actual, cases[i].window_point);
        assert(sc_screen_coords_window_to_frame_point(
            &coords, cases[i].window_point, &actual));
        assert_point_close(actual, frame_point);

        assert(sc_screen_coords_frame_to_window_vector(
            &coords, frame_vector, &actual));
        assert_point_close(actual, cases[i].window_vector);
        assert(sc_screen_coords_window_to_frame_vector(
            &coords, cases[i].window_vector, &actual));
        assert_point_close(actual, frame_vector);
    }
}

static void
test_window_resize_and_relative_mouse_golden(void) {
    // 90-degree client rotation in a square, HiDPI window. The video is
    // pillarboxed in renderer pixels, while SDL mouse deltas are expressed in
    // window points. X and Y framebuffer scales intentionally differ.
    struct sc_screen_coords coords = {
        .frame_size = {1920.0f, 1080.0f},
        .frame_rect = {0.0f, 0.0f, 1920.0f, 1080.0f},
        .content_size = {1080.0f, 1920.0f},
        .output_size = {2000.0f, 2000.0f},
        .window_size = {1250.0f, 1000.0f},
        .content_rect = {437.0f, 0.0f, 1125.0f, 2000.0f},
        .orientation = SC_ORIENTATION_90,
    };
    assert(sc_screen_coords_is_valid(&coords));

    struct sc_screen_fpoint actual;
    assert(sc_screen_coords_frame_to_window_point(
        &coords, (struct sc_screen_fpoint) {480.0f, 270.0f}, &actual));
    assert_point_close(actual,
                       (struct sc_screen_fpoint) {800.46875f, 250.0f});

    // Desktop-right/down delta -> oriented frame down/left. Letterbox x=437
    // must not appear in the result.
    assert(sc_screen_coords_window_to_frame_vector(
        &coords, (struct sc_screen_fpoint) {100.0f, 50.0f}, &actual));
    assert_point_close(actual,
                       (struct sc_screen_fpoint) {96.0f, -153.6f});

    // Resize to a 1x window with different pillarboxing. The same frame point
    // follows the content rect, without changing its device-space identity.
    coords.output_size = (struct sc_screen_fsize) {1000.0f, 1000.0f};
    coords.window_size = coords.output_size;
    coords.content_rect =
        (struct sc_screen_frect) {219.0f, 0.0f, 562.0f, 1000.0f};
    assert(sc_screen_coords_frame_to_window_point(
        &coords, (struct sc_screen_fpoint) {480.0f, 270.0f}, &actual));
    assert_point_close(actual,
                       (struct sc_screen_fpoint) {640.5f, 250.0f});
}

static void
test_screen_corners_for_cardinal_rotations(void) {
    static const struct {
        enum sc_orientation orientation;
        struct sc_screen_fpoint top_left;
        struct sc_screen_fpoint top_right;
        struct sc_screen_fpoint bottom_left;
        struct sc_screen_fpoint bottom_right;
    } cases[] = {
        {SC_ORIENTATION_0,
         {0.0f, 0.0f}, {1200.0f, 0.0f},
         {0.0f, 800.0f}, {1200.0f, 800.0f}},
        {SC_ORIENTATION_90,
         {0.0f, 800.0f}, {0.0f, 0.0f},
         {1200.0f, 800.0f}, {1200.0f, 0.0f}},
        {SC_ORIENTATION_180,
         {1200.0f, 800.0f}, {0.0f, 800.0f},
         {1200.0f, 0.0f}, {0.0f, 0.0f}},
        {SC_ORIENTATION_270,
         {1200.0f, 0.0f}, {1200.0f, 800.0f},
         {0.0f, 0.0f}, {0.0f, 800.0f}},
    };

    for (size_t i = 0; i < ARRAY_LEN(cases); ++i) {
        struct sc_screen_coords coords =
            make_golden_coords(cases[i].orientation);
        float left = coords.content_rect.x
                   * coords.window_size.width / coords.output_size.width;
        float top = coords.content_rect.y
                  * coords.window_size.height / coords.output_size.height;
        float right = (coords.content_rect.x + coords.content_rect.width)
                    * coords.window_size.width / coords.output_size.width;
        float bottom = (coords.content_rect.y + coords.content_rect.height)
                     * coords.window_size.height / coords.output_size.height;
        const struct sc_screen_fpoint window_corners[] = {
            {left, top}, {right, top}, {left, bottom}, {right, bottom},
        };
        const struct sc_screen_fpoint expected[] = {
            cases[i].top_left, cases[i].top_right,
            cases[i].bottom_left, cases[i].bottom_right,
        };
        for (size_t corner = 0; corner < ARRAY_LEN(window_corners);
                ++corner) {
            struct sc_screen_fpoint actual;
            assert(sc_screen_coords_window_to_frame_point(
                &coords, window_corners[corner], &actual));
            assert_point_close(actual, expected[corner]);
        }
    }
}

static void
test_transform_matrix(void) {
    static const float dpis[] = {1.0f, 1.25f, 1.5f, 2.0f};
    static const struct sc_screen_fpoint points[] = {
        {0.0f, 0.0f},
        {1920.0f, 0.0f},
        {0.0f, 1080.0f},
        {1920.0f, 1080.0f},
        {317.25f, 809.75f},
    };
    static const struct sc_screen_fpoint vectors[] = {
        {0.0f, 0.0f},
        {123.5f, -71.25f},
        {-400.0f, 250.0f},
    };

    for (unsigned o = SC_ORIENTATION_0;
            o <= SC_ORIENTATION_FLIP_270; ++o) {
        for (size_t d = 0; d < ARRAY_LEN(dpis); ++d) {
            for (unsigned letterbox = 0; letterbox < 2; ++letterbox) {
                struct sc_screen_coords coords =
                    make_coords((enum sc_orientation) o, dpis[d], letterbox);
                assert(sc_screen_coords_is_valid(&coords));

                for (size_t i = 0; i < ARRAY_LEN(points); ++i) {
                    struct sc_screen_fpoint window;
                    struct sc_screen_fpoint roundtrip;
                    assert(sc_screen_coords_frame_to_window_point(
                        &coords, points[i], &window));
                    assert(sc_screen_coords_window_to_frame_point(
                        &coords, window, &roundtrip));
                    assert_point_close(roundtrip, points[i]);
                }

                for (size_t i = 0; i < ARRAY_LEN(vectors); ++i) {
                    struct sc_screen_fpoint window;
                    struct sc_screen_fpoint roundtrip;
                    assert(sc_screen_coords_frame_to_window_vector(
                        &coords, vectors[i], &window));
                    assert(sc_screen_coords_window_to_frame_vector(
                        &coords, window, &roundtrip));
                    assert_point_close(roundtrip, vectors[i]);

                    // Letterbox translation must never affect vectors.
                    struct sc_screen_coords shifted = coords;
                    shifted.content_rect.x += 91.0f;
                    shifted.content_rect.y -= 53.0f;
                    struct sc_screen_fpoint shifted_window;
                    assert(sc_screen_coords_frame_to_window_vector(
                        &shifted, vectors[i], &shifted_window));
                    assert_point_close(shifted_window, window);
                }

                // Element radii are defined relative to frame height. The
                // frame vector rotates, but its window-space length must not
                // change with orientation, DPI or letterbox placement.
                const float radius = 0.12f;
                const float radius_px = radius * coords.frame_size.height;
                const float diagonal = radius_px * 0.70710678118f;
                const struct sc_screen_fpoint radius_vectors[] = {
                    {radius_px, 0.0f},
                    {0.0f, radius_px},
                    {diagonal, diagonal},
                };
                float expected_radius = radius_px * 0.625f;
                float expected_radius_sq = expected_radius * expected_radius;
                for (size_t i = 0; i < ARRAY_LEN(radius_vectors); ++i) {
                    struct sc_screen_fpoint radius_window;
                    assert(sc_screen_coords_frame_to_window_vector(
                        &coords, radius_vectors[i], &radius_window));
                    float actual_radius_sq =
                        radius_window.x * radius_window.x
                      + radius_window.y * radius_window.y;
                    assert(fabsf(actual_radius_sq - expected_radius_sq)
                           <= expected_radius_sq * 0.00001f);
                }
            }
        }
    }
}

static void
test_cropped_hidpi_transform_matrix(void) {
    static const float dpis[] = {1.25f, 1.5f, 2.0f};
    static const struct sc_screen_fpoint points[] = {
        {120.0f, 60.0f},
        {1920.0f, 60.0f},
        {120.0f, 960.0f},
        {1920.0f, 960.0f},
        {651.25f, 734.75f},
    };
    static const struct sc_screen_fpoint vectors[] = {
        {0.0f, 0.0f},
        {123.5f, -71.25f},
        {-400.0f, 250.0f},
    };

    for (unsigned o = SC_ORIENTATION_0;
            o <= SC_ORIENTATION_FLIP_270; ++o) {
        for (size_t d = 0; d < ARRAY_LEN(dpis); ++d) {
            struct sc_screen_coords coords =
                make_cropped_coords((enum sc_orientation) o, dpis[d]);
            assert(sc_screen_coords_is_valid(&coords));

            for (size_t i = 0; i < ARRAY_LEN(points); ++i) {
                struct sc_screen_fpoint window;
                struct sc_screen_fpoint roundtrip;
                assert(sc_screen_coords_frame_to_window_point(
                    &coords, points[i], &window));
                assert(sc_screen_coords_window_to_frame_point(
                    &coords, window, &roundtrip));
                assert_point_close(roundtrip, points[i]);
            }

            for (size_t i = 0; i < ARRAY_LEN(vectors); ++i) {
                struct sc_screen_fpoint window;
                struct sc_screen_fpoint roundtrip;
                assert(sc_screen_coords_frame_to_window_vector(
                    &coords, vectors[i], &window));
                assert(sc_screen_coords_window_to_frame_vector(
                    &coords, window, &roundtrip));
                assert_point_close(roundtrip, vectors[i]);
            }
        }
    }
}

static void
test_cropped_rotation_goldens(void) {
    static const struct {
        enum sc_orientation orientation;
        struct sc_screen_fpoint window_point;
        struct sc_screen_fpoint window_vector;
    } cases[] = {
        {SC_ORIENTATION_0,   {455.0f, 247.5f}, {28.0f, -14.0f}},
        {SC_ORIENTATION_90,  {612.5f, 405.0f}, {14.0f, 28.0f}},
        {SC_ORIENTATION_180, {1085.0f, 562.5f}, {-28.0f, 14.0f}},
        {SC_ORIENTATION_270, {297.5f, 1035.0f}, {-14.0f, -28.0f}},
    };
    const struct sc_screen_fpoint frame_point = {570.0f, 285.0f};
    const struct sc_screen_fpoint frame_vector = {40.0f, -20.0f};

    for (size_t i = 0; i < ARRAY_LEN(cases); ++i) {
        struct sc_screen_coords coords =
            make_cropped_coords(cases[i].orientation, 1.5f);
        struct sc_screen_fpoint actual;

        assert(sc_screen_coords_frame_to_window_point(
            &coords, frame_point, &actual));
        assert_point_close(actual, cases[i].window_point);
        assert(sc_screen_coords_window_to_frame_point(
            &coords, cases[i].window_point, &actual));
        assert_point_close(actual, frame_point);

        assert(sc_screen_coords_frame_to_window_vector(
            &coords, frame_vector, &actual));
        assert_point_close(actual, cases[i].window_vector);
        assert(sc_screen_coords_window_to_frame_vector(
            &coords, cases[i].window_vector, &actual));
        assert_point_close(actual, frame_vector);
    }
}

static void
test_center_16_9_crop_golden(void) {
    // A 2160x1080 decoded frame cropped by 120 pixels on both horizontal
    // sides produces exactly 1920x1080. The complete frame size must remain
    // unchanged so returned points still address the Android display.
    struct sc_screen_coords coords = {
        .frame_size = {2160.0f, 1080.0f},
        .frame_rect = {120.0f, 0.0f, 1920.0f, 1080.0f},
        .content_size = {1920.0f, 1080.0f},
        .output_size = {1920.0f, 1080.0f},
        .window_size = {1280.0f, 720.0f}, // 1.5x HiDPI
        .content_rect = {0.0f, 0.0f, 1920.0f, 1080.0f},
        .orientation = SC_ORIENTATION_0,
    };
    assert(sc_screen_coords_is_valid(&coords));

    struct sc_screen_fpoint actual;
    assert(sc_screen_coords_window_to_frame_point(
        &coords, (struct sc_screen_fpoint) {0.0f, 0.0f}, &actual));
    assert_point_close(actual, (struct sc_screen_fpoint) {120.0f, 0.0f});
    assert(sc_screen_coords_frame_to_window_point(
        &coords, (struct sc_screen_fpoint) {1080.0f, 540.0f}, &actual));
    assert_point_close(actual, (struct sc_screen_fpoint) {640.0f, 360.0f});

    assert(sc_screen_coords_window_to_frame_point_checked(
        &coords, (struct sc_screen_fpoint) {0.0f, 0.0f}, &actual));
    assert_point_close(actual, (struct sc_screen_fpoint) {120.0f, 0.0f});
    assert(sc_screen_coords_window_to_frame_point_checked(
        &coords, (struct sc_screen_fpoint) {1279.999f, 719.999f}, &actual));
    assert(actual.x >= 120.0f && actual.x <= 2039.0f);
    assert(actual.y >= 0.0f && actual.y <= 1079.0f);
}

static void
test_invalid_geometry(void) {
    struct sc_screen_coords coords =
        make_coords(SC_ORIENTATION_0, 1.0f, false);
    struct sc_screen_fpoint input = {1.0f, 2.0f};
    struct sc_screen_fpoint sentinel = {31.0f, 47.0f};
    struct sc_screen_fpoint output;

#define ASSERT_INVALID(FIELD) \
    do { \
        struct sc_screen_coords invalid = coords; \
        invalid.FIELD = 0.0f; \
        output = sentinel; \
        assert(!sc_screen_coords_frame_to_window_point( \
            &invalid, input, &output)); \
        assert_point_close(output, sentinel); \
    } while (0)

    ASSERT_INVALID(frame_size.width);
    ASSERT_INVALID(frame_size.height);
    ASSERT_INVALID(frame_rect.width);
    ASSERT_INVALID(frame_rect.height);
    ASSERT_INVALID(content_size.width);
    ASSERT_INVALID(content_size.height);
    ASSERT_INVALID(output_size.width);
    ASSERT_INVALID(output_size.height);
    ASSERT_INVALID(window_size.width);
    ASSERT_INVALID(window_size.height);
    ASSERT_INVALID(content_rect.width);
    ASSERT_INVALID(content_rect.height);

#undef ASSERT_INVALID

    assert(!sc_screen_coords_is_valid(NULL));
    assert(!sc_screen_coords_frame_to_window_point(&coords, input, NULL));

    struct sc_screen_coords inconsistent = coords;
    inconsistent.content_size.width += 1.0f;
    assert(!sc_screen_coords_is_valid(&inconsistent));

    struct sc_screen_coords crop_outside = coords;
    crop_outside.frame_rect.x = -1.0f;
    assert(!sc_screen_coords_is_valid(&crop_outside));
    crop_outside = coords;
    crop_outside.frame_rect.y = -1.0f;
    assert(!sc_screen_coords_is_valid(&crop_outside));
    crop_outside = coords;
    crop_outside.frame_rect.x = 1.0f;
    assert(!sc_screen_coords_is_valid(&crop_outside));
    crop_outside = coords;
    crop_outside.frame_rect.y = 1.0f;
    assert(!sc_screen_coords_is_valid(&crop_outside));

    struct sc_screen_coords bad_orientation = coords;
    bad_orientation.orientation = (enum sc_orientation) 8;
    assert(!sc_screen_coords_is_valid(&bad_orientation));

    output = sentinel;
    input.x = NAN;
    assert(!sc_screen_coords_frame_to_window_vector(&coords, input, &output));
    assert_point_close(output, sentinel);
}

static void
test_checked_content_hit_and_pixel_bounds(void) {
    static const float dpis[] = {1.0f, 1.25f, 1.5f, 2.0f};

    for (unsigned o = SC_ORIENTATION_0;
            o <= SC_ORIENTATION_FLIP_270; ++o) {
        for (size_t d = 0; d < ARRAY_LEN(dpis); ++d) {
            for (unsigned letterbox = 0; letterbox < 2; ++letterbox) {
                struct sc_screen_coords coords =
                    make_coords((enum sc_orientation) o, dpis[d], letterbox);
                float sx = coords.window_size.width
                         / coords.output_size.width;
                float sy = coords.window_size.height
                         / coords.output_size.height;
                float left = coords.content_rect.x * sx;
                float top = coords.content_rect.y * sy;
                float right = (coords.content_rect.x
                             + coords.content_rect.width) * sx;
                float bottom = (coords.content_rect.y
                              + coords.content_rect.height) * sy;
                struct sc_screen_fpoint actual;

                // Half-open content bounds: left/top included, right/bottom
                // excluded. The epsilon is expressed in output pixels before
                // conversion back to logical window points.
                assert(sc_screen_coords_window_to_frame_point_checked(
                    &coords, (struct sc_screen_fpoint) {left, top}, &actual));
                assert(actual.x >= 0.0f
                    && actual.x <= coords.frame_size.width - 1.0f);
                assert(actual.y >= 0.0f
                    && actual.y <= coords.frame_size.height - 1.0f);

                assert(!sc_screen_coords_window_to_frame_point_checked(
                    &coords, (struct sc_screen_fpoint) {right, top}, &actual));
                assert(!sc_screen_coords_window_to_frame_point_checked(
                    &coords, (struct sc_screen_fpoint) {left, bottom},
                    &actual));
                assert(!sc_screen_coords_window_to_frame_point_checked(
                    &coords,
                    (struct sc_screen_fpoint) {left - 0.25f * sx,
                                                top + 0.25f * sy},
                    &actual));
                assert(!sc_screen_coords_window_to_frame_point_checked(
                    &coords,
                    (struct sc_screen_fpoint) {left + 0.25f * sx,
                                                top - 0.25f * sy},
                    &actual));

                assert(sc_screen_coords_window_to_frame_point_checked(
                    &coords,
                    (struct sc_screen_fpoint) {right - 0.25f * sx,
                                                bottom - 0.25f * sy},
                    &actual));
                assert(actual.x >= 0.0f
                    && actual.x <= coords.frame_size.width - 1.0f);
                assert(actual.y >= 0.0f
                    && actual.y <= coords.frame_size.height - 1.0f);
            }
        }
    }
}

static void
test_checked_crop_pixel_bounds(void) {
    static const float dpis[] = {1.0f, 1.5f, 2.0f};

    for (unsigned o = SC_ORIENTATION_0;
            o <= SC_ORIENTATION_FLIP_270; ++o) {
        for (size_t d = 0; d < ARRAY_LEN(dpis); ++d) {
            struct sc_screen_coords coords =
                make_cropped_coords((enum sc_orientation) o, dpis[d]);
            float sx = coords.window_size.width / coords.output_size.width;
            float sy = coords.window_size.height / coords.output_size.height;
            float left = coords.content_rect.x * sx;
            float top = coords.content_rect.y * sy;
            float right = (coords.content_rect.x
                         + coords.content_rect.width) * sx;
            float bottom = (coords.content_rect.y
                          + coords.content_rect.height) * sy;
            struct sc_screen_fpoint actual;

            assert(sc_screen_coords_window_to_frame_point_checked(
                &coords, (struct sc_screen_fpoint) {left, top}, &actual));
            assert(actual.x >= coords.frame_rect.x
                && actual.x <= coords.frame_rect.x
                             + coords.frame_rect.width - 1.0f);
            assert(actual.y >= coords.frame_rect.y
                && actual.y <= coords.frame_rect.y
                             + coords.frame_rect.height - 1.0f);

            assert(!sc_screen_coords_window_to_frame_point_checked(
                &coords, (struct sc_screen_fpoint) {right, top}, &actual));
            assert(!sc_screen_coords_window_to_frame_point_checked(
                &coords, (struct sc_screen_fpoint) {left, bottom}, &actual));

            assert(sc_screen_coords_window_to_frame_point_checked(
                &coords,
                (struct sc_screen_fpoint) {right - 0.25f * sx,
                                            bottom - 0.25f * sy},
                &actual));
            assert(actual.x >= coords.frame_rect.x
                && actual.x <= coords.frame_rect.x
                             + coords.frame_rect.width - 1.0f);
            assert(actual.y >= coords.frame_rect.y
                && actual.y <= coords.frame_rect.y
                             + coords.frame_rect.height - 1.0f);
        }
    }
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_rotation_goldens();
    test_window_resize_and_relative_mouse_golden();
    test_screen_corners_for_cardinal_rotations();
    test_transform_matrix();
    test_cropped_hidpi_transform_matrix();
    test_cropped_rotation_goldens();
    test_center_16_9_crop_golden();
    test_invalid_geometry();
    test_checked_content_hit_and_pixel_bounds();
    test_checked_crop_pixel_bounds();
    return 0;
}
