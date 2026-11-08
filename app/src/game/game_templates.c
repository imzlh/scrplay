#include "game_templates.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef bool (*sc_game_template_builder)(struct sc_game_preset *preset);

struct sc_game_template_entry {
    struct sc_game_template_info info;
    sc_game_template_builder build;
};

static void
init_preset(struct sc_game_preset *preset, const char *name, bool aim,
            float sensitivity, bool humanize, float humanize_amp) {
    memset(preset, 0, sizeof(*preset));
    snprintf(preset->name, sizeof(preset->name), "%s", name);
    preset->aim.enabled = aim;
    preset->aim.sensitivity = sensitivity;
    preset->humanize.enabled = humanize;
    preset->humanize.amp = humanize_amp;
}

static bool
add_joystick(struct sc_game_preset *preset, const char *label, float x,
             float y, float radius, const char *up, const char *down,
             const char *left, const char *right) {
    if (preset->element_count >= SC_GAME_MAX_ELEMENTS) {
        return false;
    }

    struct sc_game_element elem = {
        .type = SC_GAME_ELEMENT_JOYSTICK,
        .joystick = {
            .center = {x, y},
            .radius = radius,
        },
    };
    snprintf(elem.label, sizeof(elem.label), "%s", label);
    if (!sc_game_key_from_name(up, &elem.joystick.up)
            || !sc_game_key_from_name(down, &elem.joystick.down)
            || !sc_game_key_from_name(left, &elem.joystick.left)
            || !sc_game_key_from_name(right, &elem.joystick.right)) {
        return false;
    }

    preset->elements[preset->element_count++] = elem;
    return true;
}

static bool
add_bind(struct sc_game_preset *preset, const char *label, const char *key,
         enum sc_game_bind_action action, float x, float y, float size) {
    if (preset->element_count >= SC_GAME_MAX_ELEMENTS) {
        return false;
    }

    struct sc_game_element elem = {
        .type = SC_GAME_ELEMENT_BIND,
        .bind = {
            .action = action,
            .center = {x, y},
            .size = size,
        },
    };
    snprintf(elem.label, sizeof(elem.label), "%s", label);
    if (!sc_game_key_from_name(key, &elem.bind.key)) {
        return false;
    }

    preset->elements[preset->element_count++] = elem;
    return true;
}

static bool
add_swipe(struct sc_game_preset *preset, const char *label, const char *key,
          float from_x, float from_y, float to_x, float to_y, uint32_t dur_ms,
          uint32_t every_ms) {
    if (preset->element_count >= SC_GAME_MAX_ELEMENTS) {
        return false;
    }

    struct sc_game_element elem = {
        .type = SC_GAME_ELEMENT_SWIPE,
        .swipe = {
            .from = {from_x, from_y},
            .to = {to_x, to_y},
            .dur_ms = dur_ms,
            .every_ms = every_ms,
        },
    };
    snprintf(elem.label, sizeof(elem.label), "%s", label);
    if (!sc_game_key_from_name(key, &elem.swipe.key)) {
        return false;
    }

    preset->elements[preset->element_count++] = elem;
    return true;
}

static bool
add_aim_start(struct sc_game_preset *preset, float x, float y) {
    if (preset->element_count >= SC_GAME_MAX_ELEMENTS) {
        return false;
    }

    struct sc_game_element elem = {
        .type = SC_GAME_ELEMENT_AIM,
        .aim = {
            .start = {x, y},
        },
    };
    snprintf(elem.label, sizeof(elem.label), "瞄准起点");
    preset->elements[preset->element_count++] = elem;
    return true;
}

static bool
build_empty(struct sc_game_preset *preset) {
    init_preset(preset, "空白", false, 1.0f, false, 0.0f);
    return true;
}

static bool
build_open_world(struct sc_game_preset *preset) {
    init_preset(preset, "开放世界", true, 1.0f, true, 0.003f);
    return add_joystick(preset, "移动摇杆", 0.17f, 0.78f, 0.10f,
                        "w", "s", "a", "d")
        && add_bind(preset, "普攻", "lmb", SC_GAME_ACTION_TAP,
                    0.86f, 0.75f, 0.08f)
        && add_bind(preset, "重击", "rmb", SC_GAME_ACTION_HOLD,
                    0.78f, 0.72f, 0.07f)
        && add_bind(preset, "技能", "e", SC_GAME_ACTION_TAP,
                    0.76f, 0.86f, 0.065f)
        && add_bind(preset, "爆发", "q", SC_GAME_ACTION_TAP,
                    0.81f, 0.64f, 0.055f)
        && add_bind(preset, "冲刺", "shift", SC_GAME_ACTION_HOLD,
                    0.93f, 0.86f, 0.06f)
        && add_bind(preset, "跳跃", "space", SC_GAME_ACTION_TAP,
                    0.61f, 0.90f, 0.055f)
        && add_bind(preset, "交互", "f", SC_GAME_ACTION_TAP,
                    0.55f, 0.72f, 0.05f)
        && add_bind(preset, "地图", "tab", SC_GAME_ACTION_TAP,
                    0.94f, 0.08f, 0.045f)
        && add_aim_start(preset, 0.56f, 0.28f);
}

static bool
build_driving(struct sc_game_preset *preset) {
    init_preset(preset, "驾驶", false, 1.0f, true, 0.002f);
    return add_bind(preset, "左转", "a", SC_GAME_ACTION_HOLD,
                    0.15f, 0.70f, 0.085f)
        && add_bind(preset, "右转", "d", SC_GAME_ACTION_HOLD,
                    0.29f, 0.70f, 0.085f)
        && add_bind(preset, "油门", "w", SC_GAME_ACTION_HOLD,
                    0.89f, 0.66f, 0.08f)
        && add_bind(preset, "倒车", "s", SC_GAME_ACTION_HOLD,
                    0.89f, 0.84f, 0.08f)
        && add_bind(preset, "刹车", "space", SC_GAME_ACTION_HOLD,
                    0.69f, 0.86f, 0.055f)
        && add_bind(preset, "手刹", "shift", SC_GAME_ACTION_HOLD,
                    0.78f, 0.86f, 0.055f)
        && add_bind(preset, "氮气", "e", SC_GAME_ACTION_TAP,
                    0.90f, 0.48f, 0.055f)
        && add_bind(preset, "自动驾驶", "q", SC_GAME_ACTION_TAP,
                    0.38f, 0.48f, 0.045f);
}

static bool
build_turn_based(struct sc_game_preset *preset) {
    init_preset(preset, "回合制", false, 1.0f, true, 0.004f);
    return add_swipe(preset, "滑动移动", "w", 0.50f, 0.64f,
                     0.50f, 0.43f, 280, 520)
        && add_bind(preset, "普攻", "lmb", SC_GAME_ACTION_TAP,
                    0.88f, 0.74f, 0.08f)
        && add_bind(preset, "技能一", "1", SC_GAME_ACTION_TAP,
                    0.62f, 0.84f, 0.06f)
        && add_bind(preset, "技能二", "2", SC_GAME_ACTION_TAP,
                    0.72f, 0.84f, 0.06f)
        && add_bind(preset, "技能三", "3", SC_GAME_ACTION_TAP,
                    0.82f, 0.84f, 0.06f)
        && add_bind(preset, "终结技", "4", SC_GAME_ACTION_TAP,
                    0.91f, 0.58f, 0.06f)
        && add_bind(preset, "结束回合", "space", SC_GAME_ACTION_TAP,
                    0.91f, 0.10f, 0.045f);
}

static bool
build_shooter(struct sc_game_preset *preset) {
    init_preset(preset, "射击", true, 0.85f, true, 0.002f);
    return add_joystick(preset, "移动摇杆", 0.16f, 0.79f, 0.10f,
                        "w", "s", "a", "d")
        && add_bind(preset, "开火", "lmb", SC_GAME_ACTION_HOLD,
                    0.90f, 0.73f, 0.075f)
        && add_bind(preset, "瞄具", "rmb", SC_GAME_ACTION_HOLD,
                    0.78f, 0.70f, 0.065f)
        && add_bind(preset, "换弹", "e", SC_GAME_ACTION_TAP,
                    0.84f, 0.87f, 0.055f)
        && add_bind(preset, "冲刺", "shift", SC_GAME_ACTION_HOLD,
                    0.64f, 0.88f, 0.055f)
        && add_bind(preset, "跳跃", "space", SC_GAME_ACTION_TAP,
                    0.55f, 0.88f, 0.055f)
        && add_bind(preset, "投掷物", "q", SC_GAME_ACTION_TAP,
                    0.73f, 0.55f, 0.05f)
        && add_bind(preset, "交互", "f", SC_GAME_ACTION_TAP,
                    0.51f, 0.70f, 0.05f)
        && add_bind(preset, "主武器", "1", SC_GAME_ACTION_TAP,
                    0.93f, 0.18f, 0.045f)
        && add_bind(preset, "副武器", "2", SC_GAME_ACTION_TAP,
                    0.86f, 0.18f, 0.045f)
        && add_aim_start(preset, 0.62f, 0.31f);
}

static bool
build_moba(struct sc_game_preset *preset) {
    init_preset(preset, "MOBA即时战术", false, 1.0f, true, 0.003f);
    return add_joystick(preset, "移动摇杆", 0.16f, 0.79f, 0.10f,
                        "w", "s", "a", "d")
        && add_bind(preset, "普通攻击", "lmb", SC_GAME_ACTION_TAP,
                    0.91f, 0.76f, 0.075f)
        && add_bind(preset, "技能一", "1", SC_GAME_ACTION_TAP,
                    0.74f, 0.84f, 0.06f)
        && add_bind(preset, "技能二", "2", SC_GAME_ACTION_TAP,
                    0.82f, 0.88f, 0.06f)
        && add_bind(preset, "技能三", "3", SC_GAME_ACTION_TAP,
                    0.90f, 0.87f, 0.06f)
        && add_bind(preset, "终极技能", "4", SC_GAME_ACTION_TAP,
                    0.83f, 0.66f, 0.065f)
        && add_bind(preset, "召唤技能", "q", SC_GAME_ACTION_TAP,
                    0.65f, 0.88f, 0.05f)
        && add_bind(preset, "主动装备", "f", SC_GAME_ACTION_TAP,
                    0.68f, 0.70f, 0.05f)
        && add_bind(preset, "回城", "space", SC_GAME_ACTION_HOLD,
                    0.56f, 0.89f, 0.045f)
        && add_bind(preset, "战术地图", "tab", SC_GAME_ACTION_TAP,
                    0.92f, 0.09f, 0.045f);
}

static bool
build_platformer(struct sc_game_preset *preset) {
    init_preset(preset, "横版动作", false, 1.0f, true, 0.002f);
    return add_joystick(preset, "移动摇杆", 0.16f, 0.79f, 0.10f,
                        "w", "s", "a", "d")
        && add_bind(preset, "攻击", "lmb", SC_GAME_ACTION_TAP,
                    0.89f, 0.77f, 0.075f)
        && add_bind(preset, "重击", "rmb", SC_GAME_ACTION_HOLD,
                    0.79f, 0.80f, 0.065f)
        && add_bind(preset, "跳跃", "space", SC_GAME_ACTION_TAP,
                    0.91f, 0.60f, 0.060f)
        && add_bind(preset, "闪避", "shift", SC_GAME_ACTION_TAP,
                    0.70f, 0.86f, 0.055f)
        && add_bind(preset, "技能", "e", SC_GAME_ACTION_TAP,
                    0.76f, 0.64f, 0.055f)
        && add_bind(preset, "交互", "f", SC_GAME_ACTION_TAP,
                    0.58f, 0.78f, 0.050f);
}

static bool
build_rhythm(struct sc_game_preset *preset) {
    // Timing games benefit from deterministic touch positions. In particular,
    // random position jitter can move a note across a narrow lane boundary.
    init_preset(preset, "节奏音游", false, 1.0f, false, 0.0f);
    // HOLD mirrors the physical key duration, so the same four lanes handle
    // both ordinary notes and long notes without duplicate bindings.
    return add_bind(preset, "轨道一", "d", SC_GAME_ACTION_HOLD,
                    0.32f, 0.83f, 0.070f)
        && add_bind(preset, "轨道二", "f", SC_GAME_ACTION_HOLD,
                    0.44f, 0.83f, 0.070f)
        && add_bind(preset, "轨道三", "j", SC_GAME_ACTION_HOLD,
                    0.56f, 0.83f, 0.070f)
        && add_bind(preset, "轨道四", "k", SC_GAME_ACTION_HOLD,
                    0.68f, 0.83f, 0.070f);
}

static const struct sc_game_template_entry templates[] = {
    {
        .info = {
            .id = SC_GAME_TEMPLATE_EMPTY,
            .name = "空白",
            .description = "从空画布开始，只添加真正需要的控件",
            .recommended_for = "特殊玩法、自定义布局",
            .controls = "无默认键位",
            .features = 0,
        },
        .build = build_empty,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_OPEN_WORLD,
            .name = "开放世界",
            .description = "WASD 移动、鼠标视角与常用动作键",
            .recommended_for = "动作冒险、角色扮演、探索",
            .controls = "WASD / 鼠标视角 / LMB / RMB / E / Q",
            .features = SC_GAME_TEMPLATE_FEATURE_MOVEMENT
                      | SC_GAME_TEMPLATE_FEATURE_ACTIONS
                      | SC_GAME_TEMPLATE_FEATURE_AIM,
        },
        .build = build_open_world,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_DRIVING,
            .name = "驾驶",
            .description = "方向、油门、刹车、手刹与加速操作",
            .recommended_for = "竞速、载具、飞行驾驶",
            .controls = "WASD / Space / Shift / E / Q",
            .features = SC_GAME_TEMPLATE_FEATURE_MOVEMENT
                      | SC_GAME_TEMPLATE_FEATURE_ACTIONS,
        },
        .build = build_driving,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_TURN_BASED,
            .name = "回合制",
            .description = "滑动移动与数字技能栏，不启用持续鼠标视角",
            .recommended_for = "回合战斗、卡牌、菜单密集玩法",
            .controls = "W 滑动 / LMB / 1-4 / Space",
            .features = SC_GAME_TEMPLATE_FEATURE_ACTIONS
                      | SC_GAME_TEMPLATE_FEATURE_GESTURE,
        },
        .build = build_turn_based,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_SHOOTER,
            .name = "射击",
            .description = "WASD、持续开火、瞄具与低灵敏度鼠标视角",
            .recommended_for = "第一人称、第三人称射击",
            .controls = "WASD / 鼠标视角 / LMB / RMB / E",
            .features = SC_GAME_TEMPLATE_FEATURE_MOVEMENT
                      | SC_GAME_TEMPLATE_FEATURE_ACTIONS
                      | SC_GAME_TEMPLATE_FEATURE_AIM,
        },
        .build = build_shooter,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_MOBA,
            .name = "MOBA即时战术",
            .description = "移动摇杆、四技能、装备与战术地图",
            .recommended_for = "MOBA、即时战术、多人竞技",
            .controls = "WASD / LMB / 1-4 / Q / F / Space",
            .features = SC_GAME_TEMPLATE_FEATURE_MOVEMENT
                      | SC_GAME_TEMPLATE_FEATURE_ACTIONS,
        },
        .build = build_moba,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_PLATFORMER,
            .name = "横版动作",
            .description = "移动、攻击、跳跃、闪避与常用技能",
            .recommended_for = "平台跳跃、横版闯关、格斗动作",
            .controls = "WASD / LMB / RMB / Space / Shift / E",
            .features = SC_GAME_TEMPLATE_FEATURE_MOVEMENT
                      | SC_GAME_TEMPLATE_FEATURE_ACTIONS,
        },
        .build = build_platformer,
    },
    {
        .info = {
            .id = SC_GAME_TEMPLATE_RHYTHM,
            .name = "节奏音游",
            .description = "四轨按键时长同步，关闭随机位置扰动",
            .recommended_for = "四键节奏、落键、音乐游戏",
            .controls = "D / F / J / K 四轨",
            .features = SC_GAME_TEMPLATE_FEATURE_ACTIONS,
        },
        .build = build_rhythm,
    },
};

static_assert(ARRAY_LEN(templates) == SC_GAME_TEMPLATE_COUNT,
              "one entry is required for every template id");

size_t
sc_game_template_count(void) {
    return ARRAY_LEN(templates);
}

const struct sc_game_template_info *
sc_game_template_at(size_t index) {
    return index < ARRAY_LEN(templates) ? &templates[index].info : NULL;
}

const struct sc_game_template_info *
sc_game_template_get(enum sc_game_template_id id) {
    if ((unsigned) id >= SC_GAME_TEMPLATE_COUNT) {
        return NULL;
    }

    // IDs currently match their list position, but search explicitly so the
    // public identifier remains stable if display order changes later.
    for (size_t i = 0; i < ARRAY_LEN(templates); ++i) {
        if (templates[i].info.id == id) {
            return &templates[i].info;
        }
    }
    return NULL;
}

static const struct sc_game_template_entry *
template_entry(enum sc_game_template_id id) {
    const struct sc_game_template_info *info = sc_game_template_get(id);
    return info ? container_of(info, struct sc_game_template_entry, info)
                : NULL;
}

bool
sc_game_template_build(enum sc_game_template_id id,
                       struct sc_game_preset *preset) {
    assert(preset);

    const struct sc_game_template_entry *entry = template_entry(id);
    if (!entry) {
        return false;
    }

    struct sc_game_preset built;
    if (!entry->build(&built)) {
        return false;
    }
    *preset = built;
    return true;
}

static bool
preset_name_used(const struct sc_game_profile *profile, const char *name,
                 size_t ignored_index) {
    for (size_t i = 0; i < profile->preset_count; ++i) {
        if (i != ignored_index && !strcmp(profile->presets[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool
make_unique_name(const struct sc_game_profile *profile, const char *base,
                 size_t ignored_index,
                 char out[SC_GAME_PRESET_NAME_MAX]) {
    // Callers commonly rename in place (preset.name is both base and out).
    // Keep a local copy so snprintf never receives overlapping buffers.
    char base_copy[SC_GAME_PRESET_NAME_MAX];
    int copied = snprintf(base_copy, sizeof(base_copy), "%s", base);
    if (copied < 0 || copied >= (int) sizeof(base_copy)) {
        return false;
    }

    if (!preset_name_used(profile, base_copy, ignored_index)) {
        snprintf(out, SC_GAME_PRESET_NAME_MAX, "%s", base_copy);
        return true;
    }

    // At most SC_GAME_MAX_PRESETS names exist, so this bounded sequence must
    // find a free suffix unless a future validator permits duplicate names.
    for (size_t suffix = 2; suffix <= SC_GAME_MAX_PRESETS + 1; ++suffix) {
        int r = snprintf(out, SC_GAME_PRESET_NAME_MAX, "%s%zu", base_copy,
                         suffix);
        if (r < 0 || r >= SC_GAME_PRESET_NAME_MAX) {
            return false;
        }
        if (!preset_name_used(profile, out, ignored_index)) {
            return true;
        }
    }
    return false;
}

bool
sc_game_template_append(struct sc_game_profile *profile,
                        enum sc_game_template_id id, size_t *preset_index) {
    assert(profile);

    if (!sc_game_profile_validate(profile)
            || profile->preset_count >= SC_GAME_MAX_PRESETS) {
        return false;
    }

    struct sc_game_preset preset;
    if (!sc_game_template_build(id, &preset)
            || !make_unique_name(profile, preset.name, SIZE_MAX,
                                 preset.name)) {
        return false;
    }

    // Commit as a transaction so even bytes outside preset_count remain
    // untouched on failure (important for deterministic dirty detection).
    struct sc_game_profile candidate = *profile;
    size_t index = candidate.preset_count;
    candidate.presets[index] = preset;
    ++candidate.preset_count;
    candidate.current_preset = index;
    if (!sc_game_profile_validate(&candidate)) {
        return false;
    }

    *profile = candidate;

    if (preset_index) {
        *preset_index = index;
    }
    return true;
}

bool
sc_game_template_replace(struct sc_game_profile *profile, size_t preset_index,
                         enum sc_game_template_id id) {
    assert(profile);

    if (!sc_game_profile_validate(profile)
            || preset_index >= profile->preset_count) {
        return false;
    }

    struct sc_game_preset preset;
    if (!sc_game_template_build(id, &preset)
            || !make_unique_name(profile, preset.name, preset_index,
                                 preset.name)) {
        return false;
    }

    struct sc_game_profile candidate = *profile;
    candidate.presets[preset_index] = preset;
    candidate.current_preset = preset_index;
    if (!sc_game_profile_validate(&candidate)) {
        return false;
    }

    *profile = candidate;
    return true;
}
