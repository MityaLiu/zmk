/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk_studio, CONFIG_ZMK_STUDIO_LOG_LEVEL);

#include <pb_encode.h>

#include <zmk/keymap.h>
#include <zmk/physical_layouts.h>
#include <zmk/rgb_underglow.h>
#include <zmk/studio/rpc.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#endif

ZMK_RPC_SUBSYSTEM(lighting)

#define LIGHTING_RESPONSE(type, ...) ZMK_RPC_RESPONSE(lighting, type, __VA_ARGS__)
#define LIGHTING_NOTIFICATION(type, ...) ZMK_RPC_NOTIFICATION(lighting, type, __VA_ARGS__)

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)

struct encode_key_rgb_context {
    const uint32_t *selected_to_stock_map;
    size_t map_len;
};

static int selected_position_for_stock_position(const struct encode_key_rgb_context *ctx,
                                                uint32_t stock_position) {
    for (int i = 0; i < ctx->map_len; i++) {
        if (ctx->selected_to_stock_map[i] == stock_position) {
            return i;
        }
    }

    return -EINVAL;
}

static zmk_lighting_RgbUnderglowSegmentType segment_type_for_source(uint8_t source) {
    return source == ZMK_RGB_UNDERGLOW_SOURCE_LOCAL
               ? zmk_lighting_RgbUnderglowSegmentType_RGB_UNDERGLOW_SEGMENT_TYPE_LOCAL
               : zmk_lighting_RgbUnderglowSegmentType_RGB_UNDERGLOW_SEGMENT_TYPE_PERIPHERAL;
}

static bool encode_rgb_underglow_segments(pb_ostream_t *stream, const pb_field_t *field,
                                          void *const *arg) {
    uint16_t local_led_count = 0;
    zmk_rgb_underglow_get_led_count(&local_led_count);

    zmk_lighting_RgbUnderglowSegment local = zmk_lighting_RgbUnderglowSegment_init_zero;
    local.type = zmk_lighting_RgbUnderglowSegmentType_RGB_UNDERGLOW_SEGMENT_TYPE_LOCAL;
    local.source = ZMK_RGB_UNDERGLOW_SOURCE_LOCAL;
    local.led_count = local_led_count;

    if (!pb_encode_tag_for_field(stream, field) ||
        !pb_encode_submessage(stream, &zmk_lighting_RgbUnderglowSegment_msg, &local)) {
        return false;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    for (int i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
        zmk_lighting_RgbUnderglowSegment peripheral = zmk_lighting_RgbUnderglowSegment_init_zero;
        peripheral.type =
            zmk_lighting_RgbUnderglowSegmentType_RGB_UNDERGLOW_SEGMENT_TYPE_PERIPHERAL;
        peripheral.source = i;
        peripheral.led_count = local_led_count;

        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, &zmk_lighting_RgbUnderglowSegment_msg, &peripheral)) {
            return false;
        }
    }
#endif

    return true;
}

static bool encode_key_rgb_entry(const struct zmk_rgb_underglow_per_key_entry *entry,
                                 void *user_data) {
    struct {
        pb_ostream_t *stream;
        const pb_field_t *field;
        const struct encode_key_rgb_context *ctx;
        bool ok;
    } *encode = user_data;

    int selected_position = selected_position_for_stock_position(encode->ctx, entry->key_position);

    if (selected_position < 0) {
        return true;
    }

    zmk_lighting_KeyRgbUnderglow key_rgb = zmk_lighting_KeyRgbUnderglow_init_zero;
    key_rgb.key_position = selected_position;
    key_rgb.has_target = true;
    key_rgb.target.segment_type = segment_type_for_source(entry->target.source);
    key_rgb.target.source = entry->target.source;
    key_rgb.target.led_index = entry->target.led_index;
    key_rgb.has_color = true;
    key_rgb.color.r = entry->color.r;
    key_rgb.color.g = entry->color.g;
    key_rgb.color.b = entry->color.b;

    encode->ok = pb_encode_tag_for_field(encode->stream, encode->field) &&
                 pb_encode_submessage(encode->stream, &zmk_lighting_KeyRgbUnderglow_msg, &key_rgb);

    return encode->ok;
}

static bool encode_key_rgb_entries(pb_ostream_t *stream, const pb_field_t *field,
                                   void *const *arg) {
    const struct encode_key_rgb_context *ctx = *arg;
    struct {
        pb_ostream_t *stream;
        const pb_field_t *field;
        const struct encode_key_rgb_context *ctx;
        bool ok;
    } encode = {
        .stream = stream,
        .field = field,
        .ctx = ctx,
        .ok = true,
    };

    zmk_rgb_underglow_per_key_foreach(encode_key_rgb_entry, &encode);

    return encode.ok;
}

static int storage_key_position_for_selected(uint32_t selected_key_position,
                                             uint32_t *storage_key_position) {
    const uint32_t *pos_map;
    int ret = zmk_physical_layouts_get_selected_to_stock_position_map(&pos_map);

    if (ret < 0) {
        return ret;
    }

    if (selected_key_position >= ret || pos_map[selected_key_position] >= ZMK_KEYMAP_LEN) {
        return -EINVAL;
    }

    *storage_key_position = pos_map[selected_key_position];
    return 0;
}

static int color_from_msg(const zmk_lighting_RgbColor *msg, struct zmk_rgb_color *color) {
    if (msg->r > UINT8_MAX || msg->g > UINT8_MAX || msg->b > UINT8_MAX) {
        return -EINVAL;
    }

    *color = (struct zmk_rgb_color){
        .r = msg->r,
        .g = msg->g,
        .b = msg->b,
    };

    return 0;
}

static struct zmk_rgb_underglow_target target_from_msg(const zmk_lighting_RgbUnderglowTarget *msg) {
    return (struct zmk_rgb_underglow_target){
        .source = msg->segment_type ==
                          zmk_lighting_RgbUnderglowSegmentType_RGB_UNDERGLOW_SEGMENT_TYPE_LOCAL
                      ? ZMK_RGB_UNDERGLOW_SOURCE_LOCAL
                      : msg->source,
        .led_index = msg->led_index,
    };
}

#endif

static zmk_studio_Response get_rgb_underglow(const zmk_studio_Request *req) {
    zmk_lighting_RgbUnderglow resp = zmk_lighting_RgbUnderglow_init_zero;

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    const uint32_t *pos_map;
    int ret = zmk_physical_layouts_get_selected_to_stock_position_map(&pos_map);

    if (ret < 0) {
        return ZMK_RPC_SIMPLE_ERR(GENERIC);
    }

    static struct encode_key_rgb_context ctx;
    ctx.selected_to_stock_map = pos_map;
    ctx.map_len = ret;

    resp.supported = true;
    resp.segments.funcs.encode = encode_rgb_underglow_segments;
    resp.keys.funcs.encode = encode_key_rgb_entries;
    resp.keys.arg = &ctx;
#endif

    return LIGHTING_RESPONSE(get_rgb_underglow, resp);
}

static zmk_studio_Response set_key_rgb_underglow(const zmk_studio_Request *req) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    const zmk_lighting_SetKeyRgbUnderglowRequest *set_req =
        &req->subsystem.lighting.request_type.set_key_rgb_underglow;

    uint32_t storage_key_position;
    int ret = storage_key_position_for_selected(set_req->key_position, &storage_key_position);

    if (ret < 0) {
        return LIGHTING_RESPONSE(
            set_key_rgb_underglow,
            zmk_lighting_SetKeyRgbUnderglowResponse_SET_KEY_RGB_UNDERGLOW_RESP_INVALID_KEY_POSITION);
    }

    struct zmk_rgb_color color;
    ret = color_from_msg(&set_req->color, &color);
    if (ret < 0) {
        return LIGHTING_RESPONSE(
            set_key_rgb_underglow,
            zmk_lighting_SetKeyRgbUnderglowResponse_SET_KEY_RGB_UNDERGLOW_RESP_INVALID_COLOR);
    }

    ret = zmk_rgb_underglow_per_key_set(storage_key_position, target_from_msg(&set_req->target),
                                        color);
    if (ret < 0) {
        return LIGHTING_RESPONSE(
            set_key_rgb_underglow,
            zmk_lighting_SetKeyRgbUnderglowResponse_SET_KEY_RGB_UNDERGLOW_RESP_INVALID_TARGET);
    }

    raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
        .notification = LIGHTING_NOTIFICATION(unsaved_changes_status_changed, true)});

    return LIGHTING_RESPONSE(set_key_rgb_underglow,
                             zmk_lighting_SetKeyRgbUnderglowResponse_SET_KEY_RGB_UNDERGLOW_RESP_OK);
#else
    return LIGHTING_RESPONSE(
        set_key_rgb_underglow,
        zmk_lighting_SetKeyRgbUnderglowResponse_SET_KEY_RGB_UNDERGLOW_RESP_NOT_SUPPORTED);
#endif
}

static zmk_studio_Response clear_key_rgb_underglow(const zmk_studio_Request *req) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    const zmk_lighting_ClearKeyRgbUnderglowRequest *clear_req =
        &req->subsystem.lighting.request_type.clear_key_rgb_underglow;

    uint32_t storage_key_position;
    int ret = storage_key_position_for_selected(clear_req->key_position, &storage_key_position);

    if (ret < 0) {
        return LIGHTING_RESPONSE(
            clear_key_rgb_underglow,
            zmk_lighting_ClearKeyRgbUnderglowResponse_CLEAR_KEY_RGB_UNDERGLOW_RESP_INVALID_KEY_POSITION);
    }

    ret = zmk_rgb_underglow_per_key_clear(storage_key_position);
    if (ret < 0) {
        return ZMK_RPC_SIMPLE_ERR(GENERIC);
    }

    raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
        .notification = LIGHTING_NOTIFICATION(unsaved_changes_status_changed, true)});

    return LIGHTING_RESPONSE(
        clear_key_rgb_underglow,
        zmk_lighting_ClearKeyRgbUnderglowResponse_CLEAR_KEY_RGB_UNDERGLOW_RESP_OK);
#else
    return LIGHTING_RESPONSE(
        clear_key_rgb_underglow,
        zmk_lighting_ClearKeyRgbUnderglowResponse_CLEAR_KEY_RGB_UNDERGLOW_RESP_NOT_SUPPORTED);
#endif
}

static zmk_studio_Response preview_rgb_underglow_target(const zmk_studio_Request *req) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    const zmk_lighting_PreviewRgbUnderglowTargetRequest *preview_req =
        &req->subsystem.lighting.request_type.preview_rgb_underglow_target;

    struct zmk_rgb_color color;
    int ret = color_from_msg(&preview_req->color, &color);

    if (ret < 0) {
        return LIGHTING_RESPONSE(
            preview_rgb_underglow_target,
            zmk_lighting_PreviewRgbUnderglowTargetResponse_PREVIEW_RGB_UNDERGLOW_TARGET_RESP_INVALID_COLOR);
    }

    struct zmk_rgb_underglow_target target = target_from_msg(&preview_req->target);

    if (target.source == ZMK_RGB_UNDERGLOW_SOURCE_LOCAL) {
        ret = zmk_rgb_underglow_set_pixel_override(target.led_index, color);
    } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        ret = zmk_split_central_set_rgb_underglow(target.source, target.led_index, color);
#else
        ret = -EINVAL;
#endif
    }

    if (ret < 0) {
        return LIGHTING_RESPONSE(
            preview_rgb_underglow_target,
            zmk_lighting_PreviewRgbUnderglowTargetResponse_PREVIEW_RGB_UNDERGLOW_TARGET_RESP_INVALID_TARGET);
    }

    return LIGHTING_RESPONSE(
        preview_rgb_underglow_target,
        zmk_lighting_PreviewRgbUnderglowTargetResponse_PREVIEW_RGB_UNDERGLOW_TARGET_RESP_OK);
#else
    return LIGHTING_RESPONSE(
        preview_rgb_underglow_target,
        zmk_lighting_PreviewRgbUnderglowTargetResponse_PREVIEW_RGB_UNDERGLOW_TARGET_RESP_NOT_SUPPORTED);
#endif
}

static zmk_studio_Response check_unsaved_changes(const zmk_studio_Request *req) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    return LIGHTING_RESPONSE(check_unsaved_changes,
                             zmk_rgb_underglow_per_key_check_unsaved_changes() > 0);
#else
    return LIGHTING_RESPONSE(check_unsaved_changes, false);
#endif
}

static void map_errno_to_save_resp(int err, zmk_lighting_SaveChangesResponse *resp) {
    resp->which_result = zmk_lighting_SaveChangesResponse_err_tag;

    switch (err) {
    case -ENOTSUP:
        resp->result.err = zmk_lighting_SaveChangesErrorCode_SAVE_CHANGES_ERR_NOT_SUPPORTED;
        break;
    case -ENOSPC:
        resp->result.err = zmk_lighting_SaveChangesErrorCode_SAVE_CHANGES_ERR_NO_SPACE;
        break;
    default:
        resp->result.err = zmk_lighting_SaveChangesErrorCode_SAVE_CHANGES_ERR_GENERIC;
        break;
    }
}

static zmk_studio_Response save_changes(const zmk_studio_Request *req) {
    zmk_lighting_SaveChangesResponse resp = zmk_lighting_SaveChangesResponse_init_zero;
    resp.which_result = zmk_lighting_SaveChangesResponse_ok_tag;
    resp.result.ok = true;

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    int ret = zmk_rgb_underglow_per_key_save_changes();
    if (ret < 0) {
        map_errno_to_save_resp(ret, &resp);
        return LIGHTING_RESPONSE(save_changes, resp);
    }
#endif

    raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
        .notification = LIGHTING_NOTIFICATION(unsaved_changes_status_changed, false)});

    return LIGHTING_RESPONSE(save_changes, resp);
}

static zmk_studio_Response discard_changes(const zmk_studio_Request *req) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    int ret = zmk_rgb_underglow_per_key_discard_changes();
    if (ret < 0) {
        return ZMK_RPC_SIMPLE_ERR(GENERIC);
    }
#endif

    raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
        .notification = LIGHTING_NOTIFICATION(unsaved_changes_status_changed, false)});

    return LIGHTING_RESPONSE(discard_changes, true);
}

static int lighting_settings_reset(void) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_PER_KEY)
    return zmk_rgb_underglow_per_key_reset_settings();
#else
    return 0;
#endif
}

ZMK_RPC_SUBSYSTEM_SETTINGS_RESET(lighting, lighting_settings_reset);

ZMK_RPC_SUBSYSTEM_HANDLER(lighting, get_rgb_underglow, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, set_key_rgb_underglow, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, clear_key_rgb_underglow, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, preview_rgb_underglow_target, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, check_unsaved_changes, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, save_changes, ZMK_STUDIO_RPC_HANDLER_SECURED);
ZMK_RPC_SUBSYSTEM_HANDLER(lighting, discard_changes, ZMK_STUDIO_RPC_HANDLER_SECURED);

static int event_mapper(const zmk_event_t *eh, zmk_studio_Notification *n) { return 0; }

ZMK_RPC_EVENT_MAPPER(lighting, event_mapper);
