/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_BATTERY 0
#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_LAYER 1
#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_CONNECTIVITY 2
#define ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_DEFAULT ZMK_RGB_UNDERGLOW_STATUS_CHANNEL_CONNECTIVITY

int zmk_rgb_underglow_toggle(void);
int zmk_rgb_underglow_get_state(bool *state);
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
int zmk_rgb_underglow_status_pixel(uint16_t index, struct zmk_led_hsb color);
int zmk_rgb_underglow_status_pixels(const uint16_t *indices, const struct zmk_led_hsb *colors,
                                    uint8_t len);
int zmk_rgb_underglow_clear_status_pixel(void);
int zmk_rgb_underglow_status_channel_pixels(uint8_t channel, const uint16_t *indices,
                                            const struct zmk_led_hsb *colors, uint8_t len);
int zmk_rgb_underglow_clear_status_channel(uint8_t channel);
