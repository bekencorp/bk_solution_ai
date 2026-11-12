/**
 * @file muse_voice_proto.c
 * @brief Muse Hatch chat wire format (see the header for the contract).
 *
 * Constants and the WAV/base64 helpers are transcribed from the pinned
 * upstream muse_chat_priv.h / muse_chat_text.c. JSON handling uses cJSON
 * (a full row fits in a 2 KB line buffer, so per-line parse is cheap).
 */
#include "muse_voice_proto.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/* Upstream MUSE_HATCH_NOTE_TAIL (muse_chat_priv.h); both note kinds share
 * it (the item object closes the same way). */
static const char kNoteTail[] = "\"}]}";

const char *muse_voice_proto_note_path(void)    { return "/chat/stream"; }
const char *muse_voice_proto_sub_path(void)     { return "/chat/subscribe"; }
const char *muse_voice_proto_content_type(void) { return "application/json"; }
const char *muse_voice_proto_app_id(void)       { return "hatch-web"; }
const char *muse_voice_proto_note_accept(void)  { return "application/json"; }
const char *muse_voice_proto_sub_accept(void)   { return "application/x-ndjson"; }

/* Shared note head. Identity fields are emitted only when non-empty, in
 * the Linux-SDK order (message, device_id, session_id, output_modality,
 * items): device_id lets Muse attribute the turn to this board instead
 * of the account main chat (Muse support, 2026-10). session_id targets a
 * specific side chat and is sent only on request — an unseen id starts a
 * new conversation whose events our unfiltered /chat/subscribe may not
 * carry, which silently black-holes the turn (field-tested 2026-10). An
 * empty field is OMITTED (an explicit "" can be read as a real id). All
 * strings are our own fixed values, sent without JSON escaping. Returns
 * the head length, 0 on overflow. */
static size_t note_head_build(char *buf, size_t cap, const char *message,
                              const char *device_id, const char *session_id,
                              const char *mime, const char *filename)
{
    const char *m = message ? message : "";
    const char *d = device_id ? device_id : "";
    const char *s = session_id ? session_id : "";
    int n;
    if (d[0] != '\0' && s[0] != '\0') {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"device_id\":\"%s\","
                     "\"session_id\":\"%s\","
                     "\"output_modality\":\"text\",\"items\":[{\"type\":\"file\","
                     "\"mime_type\":\"%s\",\"filename\":\"%s\",\"data_base64\":\"",
                     m, d, s, mime, filename);
    } else if (d[0] != '\0') {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"device_id\":\"%s\","
                     "\"output_modality\":\"text\",\"items\":[{\"type\":\"file\","
                     "\"mime_type\":\"%s\",\"filename\":\"%s\",\"data_base64\":\"",
                     m, d, mime, filename);
    } else if (s[0] != '\0') {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"session_id\":\"%s\","
                     "\"output_modality\":\"text\",\"items\":[{\"type\":\"file\","
                     "\"mime_type\":\"%s\",\"filename\":\"%s\",\"data_base64\":\"",
                     m, s, mime, filename);
    } else {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"output_modality\":\"text\","
                     "\"items\":[{\"type\":\"file\",\"mime_type\":\"%s\","
                     "\"filename\":\"%s\",\"data_base64\":\"",
                     m, mime, filename);
    }
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

size_t muse_voice_proto_note_head(char *buf, size_t cap,
                                  const char *message,
                                  const char *device_id,
                                  const char *session_id)
{
    return note_head_build(buf, cap, message, device_id, session_id,
                           "audio/wav", "voice_note.wav");
}

const char *muse_voice_proto_note_tail(size_t *len)
{
    if (len) {
        *len = sizeof(kNoteTail) - 1U;
    }
    return kNoteTail;
}

size_t muse_voice_proto_image_note_head(char *buf, size_t cap,
                                        const char *query,
                                        const char *device_id,
                                        const char *session_id)
{
    return note_head_build(buf, cap, query, device_id, session_id,
                           "image/jpeg", "photo.jpg");
}

static void put_le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/* Upstream muse_hatch_wav_header(): length fields are left unknown for a
 * body that streams while the mic is open. */
void muse_voice_proto_wav_header(uint8_t h[MUSE_VOICE_WAV_HEADER_BYTES], uint32_t rate)
{
    memcpy(h, "RIFF", 4);
    put_le(h + 4, UINT32_MAX, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le(h + 16, 16, 4);
    put_le(h + 20, 1, 2);      /* PCM */
    put_le(h + 22, 1, 2);      /* mono */
    put_le(h + 24, rate, 4);
#if CONFIG_MUSE_VOICE_PCM_8BIT
    put_le(h + 28, rate, 4);   /* byte rate: 8-bit mono = rate * 1 */
    put_le(h + 32, 1, 2);
    put_le(h + 34, 8, 2);
#else
    put_le(h + 28, rate * 2, 4);
    put_le(h + 32, 2, 2);
    put_le(h + 34, 16, 2);
#endif
    memcpy(h + 36, "data", 4);
    put_le(h + 40, UINT32_MAX, 4);
}

/* Upstream muse_hatch_base64(). */
size_t muse_voice_proto_base64(const uint8_t *in, size_t n, char *out)
{
    static const char A[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *o = out;
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        *o++ = A[v >> 18];
        *o++ = A[v >> 12 & 63];
        *o++ = A[v >> 6 & 63];
        *o++ = A[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0);
        *o++ = A[v >> 18];
        *o++ = A[v >> 12 & 63];
        *o++ = i + 1 < n ? A[v >> 6 & 63] : '=';
        *o++ = '=';
    }
    return (size_t)(o - out);
}

static void copy_str(cJSON *node, const char *name, char *out, size_t cap)
{
    cJSON *item = node ? cJSON_GetObjectItemCaseSensitive(node, name) : NULL;
    if (cJSON_IsString(item) && item->valuestring && cap) {
        size_t n = strlen(item->valuestring);
        if (n >= cap) {
            n = cap - 1U;
        }
        memcpy(out, item->valuestring, n);
        out[n] = '\0';
    }
}

int muse_voice_proto_parse_ack(const char *json,
                               char *note_id, size_t note_cap,
                               char *parent_id, size_t parent_cap)
{
    if (note_cap) {
        note_id[0] = '\0';
    }
    if (parent_cap) {
        parent_id[0] = '\0';
    }
    if (!json) {
        return -1;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return -1;
    }
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *node = cJSON_IsObject(result) ? result : root;
    copy_str(node, "message_id", note_id, note_cap);
    copy_str(node, "reply_to_message_id", parent_id, parent_cap);
    cJSON_Delete(root);
    return (note_cap && note_id[0]) ? 0 : -1;
}

/* Upstream parse_row(): a subscription line is
 * {"type":"event","payload":{...}}; payloads carry event_name (or event),
 * message_id (or id), reply_to_message_id (or parent_message_id) and one of
 * display_text/content/text. */
bool muse_voice_proto_parse_row(const char *line, muse_voice_row_t *row)
{
    memset(row, 0, sizeof(*row));
    row->ready = true;
    if (!line || !*line) {
        return false;
    }
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        return false;
    }
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *node = cJSON_IsObject(payload) ? payload : root;

    copy_str(node, "event_name", row->event, sizeof(row->event));
    if (!row->event[0]) {
        copy_str(node, "event", row->event, sizeof(row->event));
    }
    copy_str(node, "message_id", row->msg, sizeof(row->msg));
    if (!row->msg[0]) {
        copy_str(node, "id", row->msg, sizeof(row->msg));
    }
    copy_str(node, "reply_to_message_id", row->reply_to, sizeof(row->reply_to));
    if (!row->reply_to[0]) {
        copy_str(node, "parent_message_id", row->reply_to, sizeof(row->reply_to));
    }
    cJSON *flag = cJSON_GetObjectItemCaseSensitive(node, "display_text_ready");
    if (cJSON_IsBool(flag)) {
        row->ready = cJSON_IsTrue(flag);
    }
    /* display_text outranks content, content outranks text. */
    copy_str(node, "text", row->text, sizeof(row->text));
    if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(node, "content"))) {
        copy_str(node, "content", row->text, sizeof(row->text));
    }
    if (cJSON_IsString(cJSON_GetObjectItemCaseSensitive(node, "display_text"))) {
        copy_str(node, "display_text", row->text, sizeof(row->text));
    }
    cJSON_Delete(root);
    return row->event[0] != '\0';
}
