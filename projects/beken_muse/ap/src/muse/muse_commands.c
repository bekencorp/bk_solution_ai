/**
 * @file muse_commands.c
 * @brief Minimum Muse control-plane command implementation.
 */
#include "muse_commands.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bk_wifi.h"
#include "bk_wifi_types.h"
#include "cJSON.h"
#include "muse_display.h"
#if CONFIG_MUSE_GADGET_VOICE
#include "muse_voice.h"
#endif

static char *json_string(const cJSON *object, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* link.register field values, mirroring the upstream esp32 gadget
 * (noise_control.cpp build_register_json) / linux (link_client.py
 * register_params) contract. platform/model_id are free-form ("linux" is
 * accepted upstream); device_family matches the esp32 link gadget. */
#define GADGET_PLATFORM      "bk7259"
#define GADGET_VERSION       "4.0.1"
#define GADGET_DEVICE_FAMILY "link"
#define GADGET_MODEL_ID      "hatch_link"
/* Verbatim from upstream noise_control.cpp add_command("device.health"). */
#define DEVICE_HEALTH_DESC \
    "Report basic Link health, including battery level (percent), voltage, " \
    "and whether it is charging or on USB power; each is null when the " \
    "device has no battery or can't measure it"
/* Adapted from upstream's display.draw_url description for this panel
 * (320x385 RGB565 colour screen, no e-paper). */
#define DRAW_URL_DESC \
    "Download an image and draw it on the 320x385 colour screen. Takes a " \
    "baseline (not progressive) JPEG, shrunk 1/2, 1/4 or 1/8 on the device " \
    "if needed and centred, or raw RGB565 (high byte first, 640 bytes per " \
    "row). Plain http:// uses the least device memory. Replies when the " \
    "image is drawn. Hides the image on display.show_animation."
#define SHOW_ANIMATION_DESC \
    "Clear the image drawn by display.draw_url and bring back the normal UI."
#define DRAW_TEXT_DESC \
    "Draw a text page on the screen. Use this to deliver assistant replies " \
    "to the user when no audio answer is wanted; the text stays until " \
    "display.show_animation or the next draw call."

int muse_commands_build_register(const char *node_id, const char *register_id,
                                 char *out, size_t out_size)
{
    char display_name[24];
    const char *suffix;
    int n;
    size_t i;

    if (node_id == NULL || register_id == NULL ||
        out == NULL || out_size == 0) {
        return -1;
    }
    /* display_name mirrors the BLE advertised "MuseGadget-XXXXXX": its suffix
     * is the node_id tail (identity salt), uppercased. */
    suffix = strrchr(node_id, '-');
    suffix = suffix != NULL ? suffix + 1 : node_id;
    n = snprintf(display_name, sizeof(display_name), "MuseGadget-");
    for (i = (size_t)n; *suffix != '\0' && i < sizeof(display_name) - 1;
         suffix++) {
        char c = *suffix;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        display_name[i++] = c;
    }
    display_name[i] = '\0';

    /* Envelope {"type":"req","id":uuid} and params/commands_v2 shapes match
     * upstream exactly; the server matches the reply by id. */
    n = snprintf(out, out_size,
        "{\"type\":\"req\",\"id\":\"%s\",\"method\":\"link.register\","
        "\"params\":{\"node_id\":\"%s\",\"display_name\":\"%s\","
        "\"platform\":\"%s\",\"version\":\"%s\","
        "\"device_family\":\"%s\",\"model_id\":\"%s\","
        "\"is_wakeup_supported\":false,"
        "\"commands_v2\":{\"device.health\":{\"description\":\"%s\","
        "\"required\":{},\"optional\":{}},"
        "\"display.draw_url\":{\"description\":\"%s\","
        "\"required\":{\"url\":{\"type\":\"string\",\"description\":\"%s\"}},"
        "\"optional\":{\"row\":{\"type\":\"integer\",\"description\":\"%s\"}},"
        "\"timeout_ms\":60000},"
        "\"display.show_animation\":{\"description\":\"%s\","
        "\"required\":{},\"optional\":{}},"
        "\"display.draw_text\":{\"description\":\"%s\","
        "\"required\":{\"text\":{\"type\":\"string\",\"description\":\"%s\"}},"
        "\"optional\":{},\"timeout_ms\":10000}}}}",
        register_id, node_id, display_name, GADGET_PLATFORM, GADGET_VERSION,
        GADGET_DEVICE_FAMILY, GADGET_MODEL_ID, DEVICE_HEALTH_DESC,
        DRAW_URL_DESC,
        "http:// or https:// URL of the image.",
        "Row to draw the top of the image at; by default the image is "
        "centred down the screen.",
        SHOW_ANIMATION_DESC,
        DRAW_TEXT_DESC,
        "UTF-8 text to show on the screen.");
    return n >= 0 && (size_t)n < out_size ? n : -1;
}

int muse_commands_handle_invoke(const char *json, char *out, size_t out_size)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    cJSON *command_item = NULL;
    char *method;
    char *request_id;
    char *command;
    wifi_link_status_t link = {0};
    bool connected = false;
    int n = -1;

    if (json == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        goto done;
    }
    method = json_string(root, "method");
    request_id = json_string(root, "id");
    params = cJSON_GetObjectItemCaseSensitive(root, "params");
    /* Muse's production gateway spells the method "device.invoke"; the SDK
     * contract says "link.invoke". Accept both. */
    if (!method ||
        (strcmp(method, "link.invoke") != 0 &&
         strcmp(method, "device.invoke") != 0) ||
        !request_id || !cJSON_IsObject(params)) {
        goto done;
    }

    command = json_string(params, "command");
    if (command == NULL) {
        command_item = cJSON_GetObjectItemCaseSensitive(params, "name");
        command = cJSON_IsString(command_item) ? command_item->valuestring : NULL;
    }
    if (command == NULL) {
        /* The pinned daemon puts `command` beside `method` at the root. */
        command = json_string(root, "command");
    }

    if (command != NULL && strcmp(command, "display.draw_url") == 0) {
        char *url = json_string(params, "url");
        cJSON *row_item = cJSON_GetObjectItemCaseSensitive(params, "row");
        int row = cJSON_IsNumber(row_item) ? row_item->valueint : -1;
        muse_display_result_t r;

        (void)muse_display_draw_url(url, row, &r);
        if (r.ok) {
            n = snprintf(out, out_size,
                "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":true,"
                "\"payload\":{\"format\":\"%s\",\"width\":%d,\"height\":%d,"
                "\"scale\":%d,\"bytes\":%d,\"ms\":%d}}}",
                request_id, r.format, r.width, r.height, r.scale, r.bytes,
                r.ms);
        } else {
            n = snprintf(out, out_size,
                "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":false,"
                "\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}}",
                request_id, r.err_code, r.err_msg);
        }
        goto done;
    }
    if (command != NULL && strcmp(command, "display.show_animation") == 0) {
        (void)muse_display_show_animation();
        n = snprintf(out, out_size,
            "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":true}}",
            request_id);
        goto done;
    }
    /* Assistant text reply channel (Muse 2026-10: replies reach the board
     * by invoking a registered display command, not the chat event stream).
     * Show the text on the voice overlay page. */
    if (command != NULL && strcmp(command, "display.draw_text") == 0) {
        char *text = json_string(params, "text");
        if (text == NULL || text[0] == '\0') {
            n = snprintf(out, out_size,
                "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":false,"
                "\"error\":{\"code\":\"invalid_params\",\"message\":"
                "\"missing text\"}}}",
                request_id);
            goto done;
        }
#if CONFIG_MUSE_GADGET_VOICE
        /* While a voice turn is waiting this invoke IS its reply: bind it
         * so the turn ends normally (no note Reset, no spurious
         * "no reply (timeout)" overwriting the answer). */
        if (muse_voice_on_reply_text(text)) {
            n = snprintf(out, out_size,
                "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":true}}",
                request_id);
            goto done;
        }
#endif
        if (muse_display_show_voice_text("Muse", text) != 0) {
            n = snprintf(out, out_size,
                "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":false,"
                "\"error\":{\"code\":\"display_busy\",\"message\":"
                "\"display unavailable\"}}}",
                request_id);
            goto done;
        }
        n = snprintf(out, out_size,
            "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":true}}",
            request_id);
        goto done;
    }

    if (!command || strcmp(command, "device.health") != 0) {
        /* Upstream (noise_control.cpp): code "unsupported", message carries
         * the offending command name. */
        n = snprintf(out, out_size,
            "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":false,"
            "\"error\":{\"code\":\"unsupported\",\"message\":"
            "\"unsupported command: %s\"}}}",
            request_id, command != NULL ? command : "");
        goto done;
    }

    if (bk_wifi_sta_get_link_status(&link) == BK_OK) {
        connected = link.state == WIFI_LINKSTATE_STA_CONNECTED ||
                    link.state == WIFI_LINKSTATE_STA_GOT_IP;
    }
    n = snprintf(out, out_size,
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{\"ok\":true,"
        "\"payload\":{\"board\":\"BK7259\",\"firmware\":\"4.0.1\","
        "\"network\":{\"connected\":%s}}}}",
        request_id, connected ? "true" : "false");

done:
    cJSON_Delete(root);
    return n >= 0 && (size_t)n < out_size ? n : -1;
}
