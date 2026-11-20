/**
 * @file muse_display.c
 * @brief display.draw_url implementation: http/https download -> JPEG or raw
 *        RGB565 decode -> LVGL overlay on the 320x385 panel.
 *
 * Runs synchronously on the muse session worker; the VM grants this command
 * 60 s (timeout_ms in link.register) and WS keepalive runs on its own task.
 */
#include "muse_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lwip/sockets.h>
#include <lwip/dns.h>
#include <netdb.h>

#include <components/log.h>
#include <os/os.h>
#include <driver/psram.h>
#include <components/bk_frame_buffer.h>

#include "bk_ssl.h"
#include "tjpgd.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "muse_ca_cert.h"
#include "camera_preview.h"   /* camera_preview_is_active() */
#include "ui_theme.h"         /* ui_disp_is_lvgl_thread() */

/* Voice page font: full CJK (U+4E00-9FFF), CJK punctuation (U+3000-30FF)
 * and fullwidth forms (U+FF00-FFEF, i.e. ！？：), 16 px 1bpp, from the
 * pinned upstream Muse esp32 SDK (Apache-2.0, generated from unifont).
 * Everything smaller failed on arbitrary assistant replies: lv_font_ali_16
 * is a ~200-glyph UI subset and LVGL's built-in CJK fonts top out at
 * ~1300 pre-selected chars (no 问/！/？). Auto-compiled via the
 * beken_generated GLOB; MUSE_FONT_CJK_16 defaults to 1. */
extern const lv_font_t muse_font_cjk_16;

/* 16 px body font: Noto Sans CJK SC Bold, 4 bpp anti-aliased, covers
 * GB2312 level-1 + ASCII + UI strings. The unifont full-CJK font stays
 * chained behind it as the rare-glyph fallback. */
extern const lv_font_t muse_font_noto_16;

/* General-Punctuation gap-fill (curly quotes, ellipsis, dashes, middle
 * dot...): U+2010-203B is in NEITHER noto_16 nor the unifont ranges, so
 * LLM replies showed blanks there. Same face/size/bpp as noto_16,
 * generated locally (beken_generated/fonts/muse_font_punct_16.c).
 * NON-const struct so .fallback can be written here. */
extern lv_font_t muse_font_punct_16;

/* Currency / Latin-1 gap-fill (¥ € £ ¢ © ® ° ±, U+00A0-00FF and the
 * U+20A0-20BF currency block): likewise absent from noto_16/punct_16/
 * unifont, so prices rendered as blanks (field 2026-11-11). Generated
 * locally with Pillow from DejaVu Sans Bold, PLAIN 4bpp
 * (beken_generated/fonts/muse_font_money_16.c). NON-const: .fallback is
 * written here. */
extern lv_font_t muse_font_money_16;

/* 30 px title chain: lv_font_ali_30 (Latin + its CJK subset) with
 * muse_font_cjk_30 as fallback for the glyphs it lacks (助/记/...). */
extern const lv_font_t lv_font_ali_30;
extern const lv_font_t muse_font_cjk_30;

/* The generated struct is `const` (line 155407 of muse_font_cjk_16.c), so
 * it lives in flash and its fallback field is NOT writable — two
 * field-test crashes (2026-10-29, MemFault on the
 * muse_font_cjk_16.fallback write) were addr2lined to exactly that
 * instruction. Mirror the small header into RAM once (glyph data stays
 * in flash via the copied pointers) and chain the fallback there. */
static lv_font_t s_cjk_font;
static bool s_cjk_font_ready;

/* Public so other Muse pages (status / voice chat / history) can render
 * arbitrary Chinese text too; lv_font_ali_* subsets miss many glyphs. */
lv_font_t *muse_cjk_font(void)
{
    if (!s_cjk_font_ready) {
        memcpy(&s_cjk_font, &muse_font_noto_16, sizeof(s_cjk_font));
        /* Chain: noto_16 -> punct_16 -> money_16 -> unifont. unifont
         * covers all of U+4E00-9FFF for rare glyphs; its own (flash-const)
         * fallback is NULL, fine because noto_16 already carries ASCII. */
        muse_font_punct_16.fallback = &muse_font_money_16;
        muse_font_money_16.fallback = &muse_font_cjk_16;
        s_cjk_font.fallback = &muse_font_punct_16;
        s_cjk_font_ready = true;
    }
    return &s_cjk_font;
}

static lv_font_t s_title_font;
static bool s_title_font_ready;

lv_font_t *muse_title_font(void)
{
    if (!s_title_font_ready) {
        memcpy(&s_title_font, &lv_font_ali_30, sizeof(s_title_font));
        s_title_font.fallback = &muse_font_cjk_30;
        s_title_font_ready = true;
    }
    return &s_title_font;
}

#define TAG "muse_disp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define SCREEN_W            385
#define SCREEN_H            320
/* 512 KB is 5x the largest plausible panel JPEG (~100 KB) and over 2x the
 * raw-RGB565 ceiling (240 KB). Both big draw-time blocks come from the
 * 16 MB UNCODED slab (same allocator as the Jolly video frames), NOT the
 * 1432 KB PSRAM heap: a fragmented heap only has ~250 KB free under load
 * and the 241 KB frame alloc failed in the field (2026-11-10: VM relayed
 * "显存不够，分配失败"). The slab is nearly empty by design. */
#define FETCH_BODY_CAP      (512U * 1024U)
/* CDNs and signed file services stack CORS/cache/cookie headers well past
 * 4 KB; headers live in the 512 KB PSRAM body buffer, so the cap is free. */
#define FETCH_HEADER_CAP    16384U
#define FETCH_TIMEOUT_MS    15000
#define FETCH_MAX_REDIRECTS 3

/* ---- URL parsing ----------------------------------------------------------*/

typedef struct {
    bool https;
    char host[128];
    char host_header[160];
    unsigned int port;
    char path[384];     /* owned: redirect resolution rewrites it in place */
} fetch_url_t;

static int parse_url(const char *url, fetch_url_t *out)
{
    const char *p = url;
    const char *host_begin, *host_end, *path_begin, *colon = NULL, *c;
    size_t host_len;

    memset(out, 0, sizeof(*out));
    if (strncmp(p, "http://", 7) == 0) {
        out->https = false;
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        out->https = true;
        p += 8;
    }
    host_begin = p;
    path_begin = strchr(p, '/');
    host_end = path_begin != NULL ? path_begin : p + strlen(p);
    for (c = host_begin; c < host_end; c++) {
        if (*c == ':') colon = c;
    }
    host_len = (size_t)((colon != NULL ? colon : host_end) - host_begin);
    if (host_len == 0U || host_len >= sizeof(out->host)) return -1;
    memcpy(out->host, host_begin, host_len);
    out->host[host_len] = '\0';
    out->port = out->https ? 443U : 80U;
    if (colon != NULL) {
        unsigned int port = (unsigned int)atoi(colon + 1);
        if (port == 0U || port > 65535U) return -1;
        out->port = port;
    }
    {
        const char *pp = path_begin != NULL ? path_begin : "/";
        if (strlen(pp) >= sizeof(out->path)) return -1;
        strcpy(out->path, pp);
    }
    if ((out->https && out->port == 443U) ||
        (!out->https && out->port == 80U)) {
        snprintf(out->host_header, sizeof(out->host_header), "%s", out->host);
    } else {
        snprintf(out->host_header, sizeof(out->host_header), "%s:%u",
                 out->host, out->port);
    }
    return 0;
}

/* ---- transport (tls or plain socket) --------------------------------------*/

typedef struct {
    bool https;
    transport_bk_tls_t ssl;
    int sockfd;
} fetch_conn_t;

static int fetch_connect(fetch_conn_t *c, const fetch_url_t *u,
                         char *err, size_t err_cap)
{
    memset(c, 0, sizeof(*c));
    c->sockfd = -1;

    if (u->https) {
        c->https = true;
        c->ssl.sockfd = -1;
        c->ssl.cfg.cacert_buf = (const unsigned char *)g_muse_builtin_ca_cert;
        c->ssl.cfg.cacert_bytes = (unsigned int)(strlen(g_muse_builtin_ca_cert) + 1U);
        c->ssl.cfg.skip_common_name = false;
        if (_ssl_connect(&c->ssl, u->host, (int)u->port, FETCH_TIMEOUT_MS) != 0) {
            snprintf(err, err_cap, "tls connect failed");
            return -1;
        }
        if (c->ssl.tls == NULL) {
            snprintf(err, err_cap, "tls connect failed");
            _ssl_base_close(&c->ssl);
            return -1;
        }
        {
            /* VERIFY_OPTIONAL lets the handshake finish; the flags say why a
             * chain failed (0x08 untrusted root, 0x04 CN mismatch, ...). */
            uint32_t vflags =
                mbedtls_ssl_get_verify_result(&c->ssl.tls->ssl);
            if (vflags != 0U) {
                snprintf(err, err_cap,
                         "tls certificate verify failed (flags 0x%04x)",
                         (unsigned int)vflags);
                _ssl_base_close(&c->ssl);
                return -1;
            }
        }
        return 0;
    }

    struct addrinfo hints, *res = NULL;
    char port_str[8];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%u", u->port);
    if (getaddrinfo(u->host, port_str, &hints, &res) != 0 || res == NULL) {
        snprintf(err, err_cap, "dns resolve failed");
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        if (fd >= 0) close(fd);
        snprintf(err, err_cap, "tcp connect failed");
        return -1;
    }
    freeaddrinfo(res);
    struct timeval tv;
    tv.tv_sec = FETCH_TIMEOUT_MS / 1000;
    tv.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    c->sockfd = fd;
    return 0;
}

/* 0 = all bytes written, -1 = error. _ssl_write returns the byte count (>0)
 * on success and may write partially (muse_vm_api.c ssl_write_all). */
static int fetch_write(fetch_conn_t *c, const char *buf, int len)
{
    int off = 0;
    while (off < len) {
        int n;
        if (c->https) {
            n = _ssl_write(&c->ssl, buf + off, len - off, FETCH_TIMEOUT_MS);
        } else {
            n = send(c->sockfd, buf + off, len - off, 0);
        }
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

/* >0 bytes read, 0 = peer closed, -1 = error, -2 = timeout.
 * _ssl_read convention (muse_vm_api.c perform_request): >0 bytes, -1 peer
 * close_notify, 0/<-1 IO error. */
static int fetch_read(fetch_conn_t *c, char *buf, int len)
{
    if (c->https) {
        int n = _ssl_read(&c->ssl, buf, len, FETCH_TIMEOUT_MS);
        if (n == -1) return 0;      /* peer close */
        if (n <= 0) return -1;
        return n;
    }
    int n = recv(c->sockfd, buf, len, 0);
    if (n == 0) return 0;
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? -2 : -1;
    }
    return n;
}

static void fetch_close(fetch_conn_t *c)
{
    if (c->https) {
        (void)_ssl_base_close(&c->ssl);
    } else if (c->sockfd >= 0) {
        close(c->sockfd);
    }
    c->sockfd = -1;
}

/* ---- http get --------------------------------------------------------------*/

static const char *find_header_end(const uint8_t *buf, size_t len)
{
    size_t i;
    if (len < 4U) return NULL;
    for (i = 0U; i + 3U < len; i++) {
        if (buf[i] == '\r' && buf[i + 1U] == '\n' &&
            buf[i + 2U] == '\r' && buf[i + 3U] == '\n') {
            return (const char *)(buf + i);
        }
    }
    return NULL;
}

static long header_content_length(const char *headers)
{
    const char *p = headers;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            return strtol(p + 15, NULL, 10);
        }
    }
    return -1L;
}

static bool header_is_chunked(const char *headers)
{
    const char *p = headers;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, "Transfer-Encoding:", 18) == 0) {
            return strncasecmp(p + 18, " chunked", 8) == 0;
        }
    }
    return false;
}

/* Copies the trimmed Location header value into out; 0 when present. */
static int header_location(const char *headers, char *out, size_t out_cap)
{
    const char *p = headers;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, "Location:", 9) == 0) {
            const char *v = p + 9;
            size_t n;
            while (*v == ' ' || *v == '\t') v++;
            n = strcspn(v, "\r\n");
            while (n > 0U && (v[n - 1U] == ' ' || v[n - 1U] == '\t')) n--;
            if (n == 0U || n >= out_cap) return -1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
    }
    return -1;
}

/* Resolves a Location value against the current URL (RFC 7231 subset):
 * absolute http(s)://, scheme-relative //host/path, or root-relative /path.
 * http->https upgrades are followed; https->http downgrades are refused
 * (upstream image_fetch rejects any scheme change). */
static int resolve_redirect(fetch_url_t *u, const char *location,
                            char *err, size_t err_cap)
{
    fetch_url_t next = *u;

    if (strncmp(location, "http://", 7) == 0 ||
        strncmp(location, "https://", 8) == 0) {
        bool to_https = strncmp(location, "https://", 8) == 0;
        if (u->https && !to_https) {
            snprintf(err, err_cap, "redirect downgraded to http");
            return -1;
        }
        if (parse_url(location, &next) != 0) {
            snprintf(err, err_cap, "bad redirect location");
            return -1;
        }
    } else if (strncmp(location, "//", 2) == 0) {
        char absolute[600];
        int n = snprintf(absolute, sizeof(absolute), "%s:%s",
                         u->https ? "https" : "http", location);
        if (n <= 0 || (size_t)n >= sizeof(absolute) ||
            parse_url(absolute, &next) != 0) {
            snprintf(err, err_cap, "bad redirect location");
            return -1;
        }
    } else if (location[0] == '/') {
        if (strlen(location) >= sizeof(next.path)) {
            snprintf(err, err_cap, "bad redirect location");
            return -1;
        }
        strcpy(next.path, location);
    } else {
        snprintf(err, err_cap, "bad redirect location");
        return -1;
    }
    *u = next;
    return 0;
}

/* Dechunk in place; payload is always shorter than the encoded stream, so the
 * write cursor never overtakes the read cursor. Returns payload length. */
static int dechunk_inplace(uint8_t *buf, size_t raw_len, size_t cap)
{
    size_t rd = 0U, wr = 0U;
    for (;;) {
        char line[24];
        size_t li = 0U;
        while (rd < raw_len && li < sizeof(line) - 1U) {
            char ch = (char)buf[rd++];
            if (ch == '\n') break;
            if (ch != '\r') line[li++] = ch;
        }
        line[li] = '\0';
        if (li == 0U) return -1;
        long chunk = strtol(line, NULL, 16);
        if (chunk < 0L) return -1;
        if (chunk == 0L) return (int)wr;
        if (rd + (size_t)chunk > raw_len || wr + (size_t)chunk > cap) return -1;
        memmove(buf + wr, buf + rd, (size_t)chunk);
        wr += (size_t)chunk;
        rd += (size_t)chunk + 2U;   /* trailing CRLF */
        if (rd > raw_len) return -1;
    }
}

/* Single attempt: 0 with *out_status set (body filled only on 200), -1 on
 * transport/parse errors (err filled). On 3xx the Location header is copied
 * to location when a buffer is given. */
static int http_get_once(const fetch_url_t *u, uint8_t *body, size_t body_cap,
                         size_t *out_len, int *out_status,
                         char *location, size_t loc_cap,
                         char *err, size_t err_cap)
{
    fetch_conn_t c;
    char request[512];
    size_t used = 0U, body_off = 0U, have;
    long content_length;
    bool chunked;

    if (fetch_connect(&c, u, err, err_cap) != 0) {
        return -1;
    }

    int rl = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: */*\r\nConnection: close\r\n\r\n",
        u->path, u->host_header);
    if (rl <= 0 || fetch_write(&c, request, rl) != 0) {
        snprintf(err, err_cap, "write failed");
        goto fail;
    }

    /* Headers first. */
    for (;;) {
        const char *end;
        int n;
        if (used >= FETCH_HEADER_CAP) {
            snprintf(err, err_cap, "headers too large");
            goto fail;
        }
        n = fetch_read(&c, (char *)body + used, 1500);
        if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
        if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
        if (n == 0)  { snprintf(err, err_cap, "closed during headers"); goto fail; }
        used += (size_t)n;
        end = find_header_end(body, used);
        if (end != NULL) {
            body_off = (size_t)(end - (const char *)body) + 4U;
            break;
        }
    }

    body[used] = '\0';
    if (sscanf((const char *)body, "HTTP/%*s %d", out_status) != 1) {
        snprintf(err, err_cap, "bad status line");
        goto fail;
    }
    {
        char saved = (char)body[body_off];
        body[body_off] = '\0';
        if (*out_status != 200) {
            /* Non-200: keep only the Location (redirect target); the body,
             * if any, is not an image and is not read. */
            if (location != NULL &&
                header_location((const char *)body, location, loc_cap) != 0) {
                location[0] = '\0';
            }
            body[body_off] = (uint8_t)saved;
            fetch_close(&c);
            return 0;
        }
        content_length = header_content_length((const char *)body);
        chunked = header_is_chunked((const char *)body);
        body[body_off] = (uint8_t)saved;
    }

    /* Slide any already-read body bytes to the front. */
    have = used - body_off;
    if (have > 0U) memmove(body, body + body_off, have);

    if (content_length >= 0L) {
        size_t want = (size_t)content_length;
        if (want > body_cap) {
            snprintf(err, err_cap, "image too large (>%u KB)",
                     (unsigned)(body_cap / 1024U));
            goto fail;
        }
        while (have < want) {
            int n = fetch_read(&c, (char *)body + have, (int)(want - have));
            if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
            if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
            if (n == 0)  { snprintf(err, err_cap, "truncated body"); goto fail; }
            have += (size_t)n;
        }
    } else if (!chunked) {
        /* close-delimited */
        for (;;) {
            int n;
            if (have >= body_cap) {
                snprintf(err, err_cap, "image too large (>%u KB)",
                         (unsigned)(body_cap / 1024U));
                goto fail;
            }
            n = fetch_read(&c, (char *)body + have, (int)(body_cap - have));
            if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
            if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
            if (n == 0)  break;
            have += (size_t)n;
        }
    } else {
        /* chunked: read the raw stream to EOF, then dechunk in place. */
        for (;;) {
            int n;
            if (have >= body_cap) {
                snprintf(err, err_cap, "image too large (>%u KB)",
                         (unsigned)(body_cap / 1024U));
                goto fail;
            }
            n = fetch_read(&c, (char *)body + have, (int)(body_cap - have));
            if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
            if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
            if (n == 0)  break;
            have += (size_t)n;
        }
        int payload = dechunk_inplace(body, have, body_cap);
        if (payload < 0) {
            snprintf(err, err_cap, "bad chunked body");
            goto fail;
        }
        have = (size_t)payload;
    }

    fetch_close(&c);
    *out_len = have;
    return 0;

fail:
    fetch_close(&c);
    return -1;
}

/* GET with redirect following (upstream FETCH_MAX_REDIRECTS contract). */
static int http_get(const char *url, uint8_t *body, size_t body_cap,
                    size_t *out_len, char *err, size_t err_cap)
{
    fetch_url_t u;
    char location[512];
    int status = 0;
    int redirects = 0;

    if (parse_url(url, &u) != 0) {
        snprintf(err, err_cap, "bad url");
        return -1;
    }
    LOGI("GET %s://%s%s\r\n", u.https ? "https" : "http", u.host_header,
         u.path);
    for (;;) {
        location[0] = '\0';
        if (http_get_once(&u, body, body_cap, out_len, &status,
                          location, sizeof(location), err, err_cap) != 0) {
            return -1;
        }
        if (status == 200) {
            return 0;
        }
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            if (location[0] == '\0') {
                snprintf(err, err_cap, "redirect %d without location", status);
                return -1;
            }
            if (redirects >= FETCH_MAX_REDIRECTS) {
                snprintf(err, err_cap, "too many redirects");
                return -1;
            }
            redirects++;
            LOGI("redirect %d -> %s\r\n", status, location);
            if (resolve_redirect(&u, location, err, err_cap) != 0) {
                return -1;
            }
            continue;
        }
        snprintf(err, err_cap, "http status %d", status);
        return -1;
    }
}

/* ---- shared HTTP POST (TTS and future API callers) -------------------------*/

/* POST body (raw) with Content-Length; extra_headers are verbatim header
 * lines (each must already end with \r\n). Response body is read with the
 * same content-length/chunked/close-delimited handling as GET; on non-200
 * the body (if any) is left in resp for the caller to log. */
int muse_display_http_post(const char *url, const char *extra_headers,
                           const uint8_t *body, size_t body_len,
                           uint8_t *resp, size_t resp_cap, size_t *out_len,
                           char *err, size_t err_cap)
{
    fetch_url_t u;
    fetch_conn_t c;
    char request[768];
    int rl, n;
    size_t used = 0U, body_off = 0U, have;
    long content_length;
    bool chunked;
    int status = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (parse_url(url, &u) != 0) {
        snprintf(err, err_cap, "bad url");
        return -1;
    }
    rl = snprintf(request, sizeof(request),
                  "POST %s HTTP/1.1\r\nHost: %s\r\nAccept: */*\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %u\r\nConnection: close\r\n%s\r\n",
                  u.path, u.host_header, (unsigned)body_len,
                  extra_headers != NULL ? extra_headers : "");
    if (rl <= 0 || (size_t)rl >= sizeof(request)) {
        snprintf(err, err_cap, "request too large");
        return -1;
    }
    if (fetch_connect(&c, &u, err, err_cap) != 0) {
        return -1;
    }
    if (fetch_write(&c, request, rl) != 0 ||
        (body_len > 0 && fetch_write(&c, (const char *)body, (int)body_len) != 0)) {
        snprintf(err, err_cap, "write failed");
        goto fail;
    }

    for (;;) {
        const char *end;
        if (used >= FETCH_HEADER_CAP) {
            snprintf(err, err_cap, "headers too large");
            goto fail;
        }
        n = fetch_read(&c, (char *)resp + used, 1500);
        if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
        if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
        if (n == 0)  { snprintf(err, err_cap, "closed during headers"); goto fail; }
        used += (size_t)n;
        end = find_header_end(resp, used);
        if (end != NULL) {
            body_off = (size_t)(end - (const char *)resp) + 4U;
            break;
        }
    }
    resp[used] = '\0';
    if (sscanf((const char *)resp, "HTTP/%*s %d", &status) != 1) {
        snprintf(err, err_cap, "bad status line");
        goto fail;
    }
    {
        char saved = (char)resp[body_off];
        resp[body_off] = '\0';
        content_length = header_content_length((const char *)resp);
        chunked = header_is_chunked((const char *)resp);
        resp[body_off] = (uint8_t)saved;
    }
    have = used - body_off;
    if (have > 0U) {
        memmove(resp, resp + body_off, have);
    }
    if (content_length >= 0L) {
        size_t want = (size_t)content_length;
        if (want > resp_cap) {
            snprintf(err, err_cap, "response too large");
            goto fail;
        }
        while (have < want) {
            n = fetch_read(&c, (char *)resp + have, (int)(want - have));
            if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
            if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
            if (n == 0)  { snprintf(err, err_cap, "truncated body"); goto fail; }
            have += (size_t)n;
        }
    } else if (!chunked) {
        for (;;) {
            if (have >= resp_cap) break;
            n = fetch_read(&c, (char *)resp + have, (int)(resp_cap - have));
            if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
            if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
            if (n == 0)  break;
            have += (size_t)n;
        }
    } else {
        for (;;) {
            if (have >= resp_cap) {
                snprintf(err, err_cap, "response too large");
                goto fail;
            }
            n = fetch_read(&c, (char *)resp + have, (int)(resp_cap - have));
            if (n == -2) { snprintf(err, err_cap, "read timeout"); goto fail; }
            if (n < 0)   { snprintf(err, err_cap, "read failed"); goto fail; }
            if (n == 0)  break;
            have += (size_t)n;
        }
        int payload = dechunk_inplace(resp, have, resp_cap);
        if (payload < 0) {
            snprintf(err, err_cap, "bad chunked body");
            goto fail;
        }
        have = (size_t)payload;
    }
    fetch_close(&c);
    if (status != 200) {
        /* Keep the body for the caller to log (API error JSON). */
        resp[have < resp_cap ? have : resp_cap - 1U] = '\0';
        if (out_len != NULL) {
            *out_len = have;
        }
        snprintf(err, err_cap, "http status %d", status);
        return -2;
    }
    if (out_len != NULL) {
        *out_len = have;
    }
    return 0;

fail:
    fetch_close(&c);
    return -1;
}

/* ---- JPEG (classic ChaN TJpgDec, upstream esp32 image_fetch pattern) ------*/

#define JPEG_POOL_BYTES     4096U

typedef struct {
    const uint8_t *src;
    size_t size;
    size_t pos;
    uint16_t *frame;    /* letterboxed SCREEN_W*SCREEN_H RGB565 output */
    int cx;             /* crop window origin in decoded pixels */
    int cy;
    int ox;             /* letterbox offset in the output frame */
    int oy;
} jpeg_draw_t;

static size_t jpeg_in(JDEC *jd, uint8_t *buf, size_t len)
{
    jpeg_draw_t *f = (jpeg_draw_t *)jd->device;
    size_t avail = f->size - f->pos;
    size_t n = len < avail ? len : avail;
    if (buf != NULL && n > 0U) {
        memcpy(buf, f->src + f->pos, n);
    }
    f->pos += n;
    return n;
}

static int jpeg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_draw_t *f = (jpeg_draw_t *)jd->device;
    const uint16_t *src = (const uint16_t *)bitmap;
    int w = (int)(rect->right - rect->left) + 1;
    int h = (int)(rect->bottom - rect->top) + 1;
    int x, y;

    /* Copy only the crop window, shifted to the letterbox slot in the
     * full-panel frame. */
    for (y = 0; y < h; y++) {
        int dy = (int)rect->top + y - f->cy + f->oy;
        if (dy < 0) continue;
        if (dy >= SCREEN_H) break;
        for (x = 0; x < w; x++) {
            int dx = (int)rect->left + x - f->cx + f->ox;
            if (dx < 0) continue;
            if (dx >= SCREEN_W) break;
            f->frame[dy * SCREEN_W + dx] = src[y * w + x];
        }
    }
    return 1;
}

/* Full-panel RGB565 frame, allocated once from the PSRAM heap on the first
 * draw_url (BEFORE the 1 MB body buffer, so the two big blocks don't
 * fragment each other) and kept forever: the overlay references it and the
 * next decode simply overwrites it. A .psram.bss static would not fit —
 * PSRAM_DATA_SECTION is already near its link-time budget. */
static uint16_t *s_frame;

/* 0 ok, 1 unsupported, 2 too large, 3 out of memory, 4 decode failed */
static int jpeg_decode_frame(const uint8_t *jpg, size_t jpg_size,
                             uint8_t **out_frame, int *out_w, int *out_h,
                             uint8_t *out_scale, char *err, size_t err_cap)
{
    JDEC jd;
    jpeg_draw_t f;
    void *pool;
    JRESULT rc;
    uint8_t scale = 0U;
    int w, h;

    pool = psram_malloc(JPEG_POOL_BYTES);
    if (pool == NULL) {
        snprintf(err, err_cap, "jpeg pool alloc failed");
        return 3;
    }
    memset(&f, 0, sizeof(f));
    f.src = jpg;
    f.size = jpg_size;

    rc = jd_prepare(&jd, jpeg_in, pool, JPEG_POOL_BYTES, &f);
    if (rc != JDR_OK) {
        psram_free(pool);
        snprintf(err, err_cap, "not a valid JPEG (baseline only)");
        return 1;
    }
    /* Cover-fit: TJpgDec only shrinks by 1/2/4/8 and never enlarges, so a
     * plain fit leaves big black bars (1024x1024 -> 256x256 centered).
     * Pick the largest downscale that still covers the panel in both axes,
     * then centre-crop to exactly SCREEN_W x SCREEN_H. Images too small to
     * cover keep the old fit-centre behaviour. */
    {
        uint8_t cover = 0U;
        while (cover < 3U &&
               (int)(jd.width >> (cover + 1U)) >= SCREEN_W &&
               (int)(jd.height >> (cover + 1U)) >= SCREEN_H) {
            cover++;
        }
        if ((int)(jd.width >> cover) >= SCREEN_W &&
            (int)(jd.height >> cover) >= SCREEN_H) {
            /* Covers both axes: efficient downscale + centre-crop. */
            scale = cover;
        } else if (cover == 0U &&
                   ((int)jd.width >= SCREEN_W || (int)jd.height >= SCREEN_H)) {
            /* One-axis cover (the VM's 320x384 portrait shot on the 385x320
             * logical panel): shrinking to fit BOTH axes quarters the image
             * for no reason (field 2026-11-10: Beken logo drawn 160x192).
             * Keep native scale: centre-crop the overflowing axis,
             * letterbox the other. */
            scale = 0;
        } else {
            while (scale < 3U &&
                   ((int)(jd.width >> scale) > SCREEN_W ||
                    (int)(jd.height >> scale) > SCREEN_H)) {
                scale++;
            }
        }
    }
    w = (int)(jd.width >> scale);
    h = (int)(jd.height >> scale);
    if (w <= 0 || h <= 0) {
        psram_free(pool);
        snprintf(err, err_cap, "JPEG is too large even at 1/8 scale");
        return 2;
    }
    /* Render letterboxed into a cleared full-panel frame and always report
     * SCREEN_W x SCREEN_H: the overlay's LVGL image stride (w*2) then can
     * never mismatch the frame's 385-px rows (small/one-axis images used
     * to skew on the stale-row stride gap, and showed leftovers beside the
     * crop window). */
    memset(s_frame, 0, (size_t)SCREEN_W * SCREEN_H * 2U);
    f.frame = s_frame;
    /* crop origin for axes that overflow the panel */
    f.cx = (w > SCREEN_W) ? (w - SCREEN_W) / 2 : 0;
    f.cy = (h > SCREEN_H) ? (h - SCREEN_H) / 2 : 0;
    /* letterbox offset for axes that underflow it */
    f.ox = (w < SCREEN_W) ? (SCREEN_W - w) / 2 : 0;
    f.oy = (h < SCREEN_H) ? (SCREEN_H - h) / 2 : 0;
    w = SCREEN_W;
    h = SCREEN_H;
    rc = jd_decomp(&jd, jpeg_out, scale);
    psram_free(pool);
    if (rc != JDR_OK) {
        snprintf(err, err_cap, "%s",
                 rc == JDR_FMT3
                     ? "unsupported JPEG: use baseline, not progressive"
                     : "JPEG decode failed");
        return rc == JDR_FMT3 ? 1 : 4;
    }
    *out_frame = (uint8_t *)f.frame;
    *out_w = w;
    *out_h = h;
    *out_scale = scale;
    return 0;
}

/* ---- LVGL overlay ------------------------------------------------------------*/

static lv_obj_t *s_overlay;
static lv_image_dsc_t *s_dsc;
static uint8_t *s_img_data;     /* points into s_frame[]; never freed */

/* Voice text overlay (defined below); declared for show_animation(). */
static void voice_overlay_delete_locked(void);

/* Caller must hold lv_vendor_disp_lock(). */
static void overlay_delete_locked(void)
{
    if (s_overlay != NULL) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
    }
    if (s_dsc != NULL) {
        free(s_dsc);
        s_dsc = NULL;
    }
    s_img_data = NULL;
}

static int overlay_show(uint8_t *rgb565, int w, int h, int row,
                        char *err, size_t err_cap)
{
    int x = (SCREEN_W - w) / 2;
    int y = row >= 0 ? row : (SCREEN_H - h) / 2;
    lv_obj_t *img;

    if (camera_preview_is_active()) {
        /* LVGL is stopped while the camera preview owns the screen. */
        snprintf(err, err_cap, "display unavailable (camera preview)");
        return -1;
    }

    lv_vendor_disp_lock();
    overlay_delete_locked();
    voice_overlay_delete_locked();

    s_dsc = (lv_image_dsc_t *)malloc(sizeof(lv_image_dsc_t));
    if (s_dsc == NULL) {
        lv_vendor_disp_unlock();
        snprintf(err, err_cap, "out of memory");
        return -1;
    }
    memset(s_dsc, 0, sizeof(*s_dsc));
    s_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc->header.w = (uint32_t)w;
    s_dsc->header.h = (uint32_t)h;
    s_dsc->header.stride = (uint32_t)(w * 2);
    s_dsc->data = rgb565;
    s_dsc->data_size = (uint32_t)(w * h * 2);

    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);

    img = lv_image_create(s_overlay);
    lv_image_set_src(img, s_dsc);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    lv_obj_set_pos(img, x, y);

    s_img_data = rgb565;
    lv_vendor_disp_unlock();
    return 0;
}

int muse_display_show_animation(void)
{
    if (camera_preview_is_active()) {
        /* LVGL is stopped during preview; nothing to clear can be drawn
         * either, so this is a safe no-op. */
        return 0;
    }
    /* Same re-lock guard as show_voice_text: some callers run on the LVGL
     * task (button callbacks already hold g_disp_mutex, non-recursive);
     * re-locking there self-deadlocks. */
    const bool on_disp_thread = ui_disp_is_lvgl_thread();
    if (!on_disp_thread) {
        lv_vendor_disp_lock();
    }
    overlay_delete_locked();
    voice_overlay_delete_locked();
    if (!on_disp_thread) {
        lv_vendor_disp_unlock();
    }
    return 0;
}

/* ---- voice reply text overlay ----------------------------------------------*/

static lv_obj_t *s_voice_overlay;
static lv_obj_t *s_voice_title;
static lv_obj_t *s_voice_body;

/* The reply page renders at 1x: the 2x transform_zoom approach needed an
 * intermediate render layer whose allocation fails on this port (NULL
 * layer buffer -> blend writes to address 0, MemFault, 2026-10-29). A
 * larger font can replace the zoom once generated; keep 1x until then. */

/* While the camera preview owns the screen, LVGL is stopped (pure GPU
 * composite; see camera_preview.c). Any lv_* call in that window — the
 * Muse reply lands right after a photo shot — has crashed the board, so
 * replies shown during preview are stashed here and flushed by
 * camera_preview once LVGL is back (muse_display_flush_pending() from
 * camera_preview_resume_lvgl). .psram.bss: keep the scarce SRAM heap free. */
#define VOICE_PENDING_TITLE_MAX 32U
#define VOICE_PENDING_BODY_MAX  1024U
static char s_pending_title[VOICE_PENDING_TITLE_MAX]
    __attribute__((section(".psram.bss")));
static char s_pending_body[VOICE_PENDING_BODY_MAX]
    __attribute__((section(".psram.bss")));
static bool s_pending_show;
static bool s_pending_hide;

/* Caller must hold lv_vendor_disp_lock(). */
static void voice_overlay_delete_locked(void)
{
    if (s_voice_overlay != NULL) {
        lv_obj_del(s_voice_overlay);
        s_voice_overlay = NULL;
        s_voice_title = NULL;
        s_voice_body = NULL;
    }
}

/* Caller must hold lv_vendor_disp_lock(). */
static void voice_overlay_show_locked(const char *title, const char *body)
{
    s_voice_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_voice_overlay);
    lv_obj_set_size(s_voice_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_voice_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_voice_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_voice_overlay, LV_OPA_COVER, 0);

    /* muse_font_cjk_16 was generated without ASCII (0x3000-30FF,
     * 0x4E00-9FFF, 0xFF00-FFEF only): digits, Latin letters and halfwidth
     * punctuation would render blank. Use the RAM mirror with Montserrat
     * 14 chained as fallback so missing glyphs resolve there (LVGL walks
     * the fallback chain per glyph). */
    s_voice_title = lv_label_create(s_voice_overlay);
    lv_obj_set_style_text_color(s_voice_title, lv_color_hex(0x7dfcff), 0);
    lv_obj_set_style_text_font(s_voice_title, muse_cjk_font(), 0);
    lv_label_set_text(s_voice_title, title);
    lv_obj_set_width(s_voice_title, SCREEN_W);
    lv_obj_set_style_text_align(s_voice_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_voice_title, 0, 12);

    s_voice_body = lv_label_create(s_voice_overlay);
    lv_obj_set_style_text_color(s_voice_body, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_voice_body, muse_cjk_font(), 0);
    lv_label_set_long_mode(s_voice_body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_voice_body, SCREEN_W - 32);
    lv_label_set_text(s_voice_body, body);
    /* Centre the block vertically so short replies don't hug the top of
     * the full-screen page; long answers stay top-anchored for reading. */
    lv_obj_update_layout(s_voice_overlay);
    {
        int bh = (int)lv_obj_get_height(s_voice_body);
        int by = (SCREEN_H - bh) / 2;
        if (by < 56) {
            by = 56;
        }
        lv_obj_set_pos(s_voice_body, 16, by);
    }
}

int muse_display_show_voice_text(const char *title, const char *body)
{
    if (title == NULL) {
        title = "";
    }
    if (body == NULL) {
        body = "";
    }

    if (camera_preview_is_active()) {
        /* Preview owns the screen; show it once LVGL is back. */
        snprintf(s_pending_title, sizeof(s_pending_title), "%s", title);
        snprintf(s_pending_body, sizeof(s_pending_body), "%s", body);
        s_pending_hide = false;
        s_pending_show = true;
        return 0;
    }
    s_pending_show = false;
    s_pending_hide = false;

    /* The mic-button callback runs on the LVGL task, which already holds
     * g_disp_mutex (non-recursive) around lv_task_handler(): re-locking
     * here self-deadlocked the whole UI at the second PTT (field log:
     * head sent, "PTT start" never followed, tp queue flooded). */
    const bool on_disp_thread = ui_disp_is_lvgl_thread();
    if (!on_disp_thread) {
        lv_vendor_disp_lock();
    }
    /* One overlay family on screen at a time: the voice page wins over a
     * stale draw_url image and vice versa. */
    overlay_delete_locked();
    voice_overlay_delete_locked();
    voice_overlay_show_locked(title, body);
    if (!on_disp_thread) {
        lv_vendor_disp_unlock();
    }
    return 0;
}

int muse_display_hide_voice_text(void)
{
    if (camera_preview_is_active()) {
        /* The overlay cannot be up during preview; record the intent so a
         * stale pending show is dropped too. */
        s_pending_show = false;
        s_pending_hide = true;
        return 0;
    }
    s_pending_hide = false;
    /* See show_voice_text: skip the re-lock when already on the LVGL
     * task (button callbacks run under g_disp_mutex). */
    const bool on_disp_thread = ui_disp_is_lvgl_thread();
    if (!on_disp_thread) {
        lv_vendor_disp_lock();
    }
    voice_overlay_delete_locked();
    if (!on_disp_thread) {
        lv_vendor_disp_unlock();
    }
    return 0;
}

/* Called by camera_preview after LVGL is running again: replays the
 * reply that arrived while the preview owned the screen. Runs inside
 * camera_preview_resume_lvgl() BEFORE the preview state leaves STOPPING,
 * so the public show/hide entry points would re-stash (their
 * camera_preview_is_active() guard is still true): draw directly. */
int muse_display_flush_pending(void)
{
    if (s_pending_show) {
        char title[VOICE_PENDING_TITLE_MAX];
        char body[VOICE_PENDING_BODY_MAX];
        snprintf(title, sizeof(title), "%s", s_pending_title);
        snprintf(body, sizeof(body), "%s", s_pending_body);
        s_pending_show = false;
        LOGI("flush pending voice overlay after preview\r\n");
        lv_vendor_disp_lock();
        overlay_delete_locked();
        voice_overlay_delete_locked();
        voice_overlay_show_locked(title, body);
        lv_vendor_disp_unlock();
        return 0;
    }
    if (s_pending_hide) {
        s_pending_hide = false;
        lv_vendor_disp_lock();
        voice_overlay_delete_locked();
        lv_vendor_disp_unlock();
        return 0;
    }
    return 0;
}

/* ---- public command ----------------------------------------------------------*/

static void set_error(muse_display_result_t *r, const char *code, const char *msg)
{
    size_t i;
    r->ok = false;
    snprintf(r->err_code, sizeof(r->err_code), "%s", code);
    snprintf(r->err_msg, sizeof(r->err_msg), "%s", msg);
    /* err_msg is embedded into a JSON reply: strip quotes/backslashes. */
    for (i = 0U; r->err_msg[i] != '\0'; i++) {
        if (r->err_msg[i] == '"' || r->err_msg[i] == '\\') {
            r->err_msg[i] = ' ';
        }
    }
}

int muse_display_draw_url(const char *url, int row, muse_display_result_t *result)
{
    uint8_t *body = NULL;
    uint8_t *image = NULL;
    size_t body_len = 0U;
    char err[MUSE_DISPLAY_ERR_MAX];
    uint32_t started = rtos_get_time();
    int ret = -1;

    memset(result, 0, sizeof(*result));
    if (url == NULL || url[0] == '\0') {
        set_error(result, "missing_param", "url is required");
        return -1;
    }

    /* Grab the persistent frame first: ordering the two big slab blocks
     * this way keeps the body alloc from taking the last contiguous chunk
     * and starving the frame (the Meta CDN failure). Both come from the
     * UNCODED slab -- the 1432 KB PSRAM heap is too fragmented under load
     * for 241 KB+ one-shots (field: "frame buffer alloc failed"). */
    if (s_frame == NULL) {
        s_frame = (uint16_t *)bk_frame_buffer_malloc(
            MEM_SLAB_HEAP_UNCODED, (size_t)SCREEN_W * SCREEN_H * 2U);
        if (s_frame == NULL) {
            set_error(result, "out_of_memory", "frame buffer alloc failed");
            return -1;
        }
    }
    body = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                             FETCH_BODY_CAP);
    if (body == NULL) {
        set_error(result, "out_of_memory", "download buffer alloc failed");
        return -1;
    }
    err[0] = '\0';
    if (http_get(url, body, FETCH_BODY_CAP, &body_len, err, sizeof(err)) != 0) {
        set_error(result, "download_failed",
                  err[0] != '\0' ? err : "download failed");
        goto done;
    }
    result->bytes = (int)body_len;

    if (body_len >= 2U && body[0] == 0xFFU && body[1] == 0xD8U) {
        /* baseline JPEG */
        static const char *const k_codes[] = {
            "", "unsupported", "image_too_large", "out_of_memory",
            "decode_failed"
        };
        uint8_t scale = 0U;
        int w = 0, h = 0, rc;

        err[0] = '\0';
        rc = jpeg_decode_frame(body, body_len, &image, &w, &h, &scale,
                               err, sizeof(err));
        if (rc != 0) {
            set_error(result, k_codes[rc],
                      err[0] != '\0' ? err : "JPEG decode failed");
            goto done;
        }
        snprintf(result->format, sizeof(result->format), "jpeg");
        result->width = w;
        result->height = h;
        result->scale = 1 << scale;
    } else {
        /* raw RGB565, whole rows of panel width, high byte first (upstream
         * image_fetch raw contract); LVGL wants little-endian, so swap. */
        size_t pixels, i;
        int h;
        if (body_len == 0U || (body_len % (SCREEN_W * 2U)) != 0U) {
            set_error(result, "unsupported",
                      "raw data must be whole rows of RGB565, high byte first");
            goto done;
        }
        h = (int)(body_len / (SCREEN_W * 2U));
        if (h > SCREEN_H) {
            set_error(result, "image_too_large", "raw image taller than panel");
            goto done;
        }
        /* h <= SCREEN_H rows of SCREEN_W pixels: fits the frame buffer. */
        image = (uint8_t *)s_frame;
        pixels = body_len / 2U;
        for (i = 0U; i < pixels; i++) {
            ((uint16_t *)image)[i] =
                (uint16_t)(((uint16_t)body[i * 2U] << 8) | body[i * 2U + 1U]);
        }
        snprintf(result->format, sizeof(result->format), "rgb565");
        result->width = SCREEN_W;
        result->height = h;
        result->scale = 1;
    }

    err[0] = '\0';
    if (overlay_show(image, result->width, result->height, row,
                     err, sizeof(err)) != 0) {
        set_error(result, "out_of_memory",
                  err[0] != '\0' ? err : "display failed");
        goto done;
    }
    result->ok = true;
    result->ms = (int)(rtos_get_time() - started);
    ret = 0;

done:
    (void)image;    /* points into the persistent s_frame; nothing to free */
    bk_frame_buffer_free(body);
    LOGI("draw_url %s: %s\r\n", result->ok ? "done" : "failed",
         result->ok ? result->format : result->err_msg);
    return ret;
}
