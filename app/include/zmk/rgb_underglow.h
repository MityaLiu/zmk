/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

struct zmk_rgb_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct zmk_rgb_underglow_target {
    uint8_t source;
    uint16_t led_index;
};

#define ZMK_RGB_UNDERGLOW_SOURCE_LOCAL UINT8_MAX

int zmk_rgb_underglow_toggle(void);
int zmk_rgb_underglow_get_state(bool *state);
int zmk_rgb_underglow_get_led_count(uint16_t *count);
int zmk_rgb_underglow_on(void);
int zmk_rgb_underglow_off(void);
int zmk_rgb_underglow_cycle_effect(int direction);
int zmk_rgb_underglow_calc_effect(int direction);
int zmk_rgb_underglow_select_effect(int effect);
struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction);
int zmk_rgb_underglow_change_hue(int direction);
int zmk_rgb_underglow_change_sat(int direction);
int zmk_rgb_underglow_change_brt(int direction);
int zmk_rgb_underglow_change_spd(int direction);
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);

int zmk_rgb_underglow_set_pixel_override(uint16_t led_index, struct zmk_rgb_color color);
int zmk_rgb_underglow_clear_pixel_override(uint16_t led_index);
int zmk_rgb_underglow_clear_pixel_overrides(void);

struct zmk_rgb_underglow_per_key_entry {
    bool active;
    uint32_t key_position;
    struct zmk_rgb_underglow_target target;
    struct zmk_rgb_color color;
};

typedef bool (*zmk_rgb_underglow_per_key_entry_cb)(
    const struct zmk_rgb_underglow_per_key_entry *entry, void *user_data);

int zmk_rgb_underglow_per_key_foreach(zmk_rgb_underglow_per_key_entry_cb cb, void *user_data);
int zmk_rgb_underglow_per_key_set(uint32_t key_position, struct zmk_rgb_underglow_target target,
                                  struct zmk_rgb_color color);
int zmk_rgb_underglow_per_key_clear(uint32_t key_position);
int zmk_rgb_underglow_per_key_check_unsaved_changes(void);
int zmk_rgb_underglow_per_key_save_changes(void);
int zmk_rgb_underglow_per_key_discard_changes(void);
int zmk_rgb_underglow_per_key_reset_settings(void);
int zmk_rgb_underglow_per_key_sync_peripherals(void);
