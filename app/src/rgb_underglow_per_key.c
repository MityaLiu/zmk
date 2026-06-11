/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#endif

#define RGB_UNDERGLOW_PER_KEY_SETTINGS_KEY "rgb/underglow_per_key"
#define RGB_UNDERGLOW_PER_KEY_ENTRY_SETTINGS_KEY "rgb/underglow_per_key/k/%d"
#define RGB_UNDERGLOW_PER_KEY_PENDING_ARRAY_SIZE DIV_ROUND_UP(ZMK_KEYMAP_LEN, 8)

struct zmk_rgb_underglow_per_key_setting {
    uint8_t source;
    uint16_t led_index;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} __packed;

static struct zmk_rgb_underglow_per_key_entry entries[ZMK_KEYMAP_LEN];
static uint8_t saved_entries[RGB_UNDERGLOW_PER_KEY_PENDING_ARRAY_SIZE];
static bool dirty;

static void set_saved(uint32_t key_position, bool saved) {
    WRITE_BIT(saved_entries[key_position / 8], key_position % 8, saved);
}

static bool is_saved(uint32_t key_position) {
    return saved_entries[key_position / 8] & BIT(key_position % 8);
}

static int validate_target(struct zmk_rgb_underglow_target target) {
    if (target.source == ZMK_RGB_UNDERGLOW_SOURCE_LOCAL) {
        uint16_t led_count;
        int ret = zmk_rgb_underglow_get_led_count(&led_count);

        if (ret < 0) {
            return ret;
        }

        if (target.led_index >= led_count) {
            return -EINVAL;
        }
    }

    if (target.source != ZMK_RGB_UNDERGLOW_SOURCE_LOCAL) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        if (target.source >= ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT) {
            return -EINVAL;
        }
#else
        return -EINVAL;
#endif
    }

    return 0;
}

static int apply_local_entries(void) {
    int ret = zmk_rgb_underglow_clear_pixel_overrides();

    if (ret < 0) {
        return ret;
    }

    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        const struct zmk_rgb_underglow_per_key_entry *entry = &entries[i];

        if (!entry->active || entry->target.source != ZMK_RGB_UNDERGLOW_SOURCE_LOCAL) {
            continue;
        }

        ret = zmk_rgb_underglow_set_pixel_override(entry->target.led_index, entry->color);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int zmk_rgb_underglow_per_key_sync_peripherals(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    int ret = zmk_split_central_clear_rgb_underglow();

    if (ret < 0) {
        return ret;
    }

    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        const struct zmk_rgb_underglow_per_key_entry *entry = &entries[i];

        if (!entry->active || entry->target.source == ZMK_RGB_UNDERGLOW_SOURCE_LOCAL) {
            continue;
        }

        ret = zmk_split_central_set_rgb_underglow(entry->target.source, entry->target.led_index,
                                                  entry->color);
        if (ret < 0) {
            return ret;
        }
    }
#endif

    return 0;
}

static int apply_entries(void) {
    int ret = apply_local_entries();

    if (ret < 0) {
        return ret;
    }

    return zmk_rgb_underglow_per_key_sync_peripherals();
}

int zmk_rgb_underglow_per_key_foreach(zmk_rgb_underglow_per_key_entry_cb cb, void *user_data) {
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        if (!entries[i].active) {
            continue;
        }

        if (!cb(&entries[i], user_data)) {
            break;
        }
    }

    return 0;
}

int zmk_rgb_underglow_per_key_set(uint32_t key_position, struct zmk_rgb_underglow_target target,
                                  struct zmk_rgb_color color) {
    if (key_position >= ARRAY_SIZE(entries)) {
        return -EINVAL;
    }

    int ret = validate_target(target);
    if (ret < 0) {
        return ret;
    }

    entries[key_position] = (struct zmk_rgb_underglow_per_key_entry){
        .active = true,
        .key_position = key_position,
        .target = target,
        .color = color,
    };
    dirty = true;

    return apply_entries();
}

int zmk_rgb_underglow_per_key_clear(uint32_t key_position) {
    if (key_position >= ARRAY_SIZE(entries)) {
        return -EINVAL;
    }

    if (!entries[key_position].active) {
        return 0;
    }

    entries[key_position].active = false;
    dirty = true;

    return apply_entries();
}

int zmk_rgb_underglow_per_key_check_unsaved_changes(void) { return dirty ? 1 : 0; }

int zmk_rgb_underglow_per_key_save_changes(void) {
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        char setting_name[32];
        sprintf(setting_name, RGB_UNDERGLOW_PER_KEY_ENTRY_SETTINGS_KEY, i);

        if (entries[i].active) {
            struct zmk_rgb_underglow_per_key_setting setting = {
                .source = entries[i].target.source,
                .led_index = entries[i].target.led_index,
                .r = entries[i].color.r,
                .g = entries[i].color.g,
                .b = entries[i].color.b,
            };

            int ret = settings_save_one(setting_name, &setting, sizeof(setting));
            if (ret < 0) {
                return ret;
            }

            set_saved(i, true);
        } else if (is_saved(i)) {
            int ret = settings_delete(setting_name);
            if (ret < 0) {
                return ret;
            }

            set_saved(i, false);
        }
    }

    dirty = false;
    return 0;
}

int zmk_rgb_underglow_per_key_discard_changes(void) {
    memset(entries, 0, sizeof(entries));
    memset(saved_entries, 0, sizeof(saved_entries));

    int ret = settings_load_subtree(RGB_UNDERGLOW_PER_KEY_SETTINGS_KEY);
    if (ret < 0) {
        return ret;
    }

    dirty = false;
    return apply_entries();
}

int zmk_rgb_underglow_per_key_reset_settings(void) {
    for (int i = 0; i < ARRAY_SIZE(entries); i++) {
        if (!entries[i].active && !is_saved(i)) {
            continue;
        }

        char setting_name[32];
        sprintf(setting_name, RGB_UNDERGLOW_PER_KEY_ENTRY_SETTINGS_KEY, i);
        settings_delete(setting_name);
    }

    memset(entries, 0, sizeof(entries));
    memset(saved_entries, 0, sizeof(saved_entries));
    dirty = false;

    return apply_entries();
}

static int rgb_underglow_per_key_settings_set(const char *name, size_t len,
                                              settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (!settings_name_steq(name, "k", &next) || !next) {
        return -ENOENT;
    }

    char *endptr;
    uint32_t key_position = strtoul(next, &endptr, 10);

    if (*endptr != '\0' || key_position >= ARRAY_SIZE(entries)) {
        LOG_WRN("Invalid RGB underglow per-key setting key position: %s", next);
        return -EINVAL;
    }

    if (len != sizeof(struct zmk_rgb_underglow_per_key_setting)) {
        return -EINVAL;
    }

    struct zmk_rgb_underglow_per_key_setting setting;
    int rc = read_cb(cb_arg, &setting, sizeof(setting));

    if (rc < 0) {
        return rc;
    }

    entries[key_position] = (struct zmk_rgb_underglow_per_key_entry){
        .active = true,
        .key_position = key_position,
        .target =
            {
                .source = setting.source,
                .led_index = setting.led_index,
            },
        .color =
            {
                .r = setting.r,
                .g = setting.g,
                .b = setting.b,
            },
    };
    set_saved(key_position, true);

    return 0;
}

static int rgb_underglow_per_key_settings_commit(void) {
    dirty = false;
    return apply_entries();
}

SETTINGS_STATIC_HANDLER_DEFINE(rgb_underglow_per_key, RGB_UNDERGLOW_PER_KEY_SETTINGS_KEY, NULL,
                               rgb_underglow_per_key_settings_set,
                               rgb_underglow_per_key_settings_commit, NULL);
