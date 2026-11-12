/**
 * @file muse_vm_api.c
 * @brief Synchronous, bounded Muse VM REST client.
 */

#include "muse_vm_api.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bk_ssl.h>
#include <components/log.h>
#include "cJSON.h"

#define MUSE_VM_REST_TAG "muse_rest"

#define MUSE_HTTP_DEFAULT_TIMEOUT 10000U
#define MUSE_BEARER_PREFIX        "Bearer "
#define MUSE_REFRESH_PREFIX       "hatch_refresh:"

/* The WebSocket transport implementation exports these underscored symbols. */
int _ssl_read(transport_bk_tls_t *ssl, char *buffer, int len, int timeout_ms);
int _ssl_write(transport_bk_tls_t *ssl, const char *buffer, int len,
               int timeout_ms);
int _ssl_connect(transport_bk_tls_t *ssl, const char *host, int port,
                 int timeout_ms);
int _ssl_base_close(transport_bk_tls_t *ssl);

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} muse_response_t;

typedef struct {
    char host[MUSE_VM_API_VM_HOST_MAX];
    char host_header[MUSE_VM_API_VM_HOST_MAX + 8U];
    const char *target;
    size_t target_length;
    unsigned int port;
} muse_https_url_t;

static size_t bounded_strlen(const char *value, size_t limit)
{
    size_t length = 0;

    if (value == NULL) {
        return 0;
    }
    while (length < limit && value[length] != '\0') {
        ++length;
    }
    return length;
}

static void clear_and_free(char *value, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)value;

    if (value == NULL) {
        return;
    }
    while (length-- != 0U) {
        *cursor++ = 0;
    }
    free(value);
}

static muse_vm_api_err_t validate_required_string(const char *value,
                                                  size_t capacity)
{
    size_t length;

    if (value == NULL || *value == '\0' || capacity < 2U) {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    length = bounded_strlen(value, capacity);
    return length < capacity ? MUSE_VM_API_OK : MUSE_VM_API_ERR_LIMIT;
}

static muse_vm_api_err_t validate_tls(const muse_vm_api_tls_t *tls)
{
    size_t pem_length;

    if (tls == NULL || tls->ca_cert_pem == NULL) {
        return MUSE_VM_API_ERR_TLS_CONFIG;
    }

    if (tls->ca_cert_len != 0U) {
        if (tls->ca_cert_len < 2U ||
            tls->ca_cert_len > MUSE_VM_API_CA_CERT_MAX ||
            tls->ca_cert_pem[tls->ca_cert_len - 1U] != '\0' ||
            bounded_strlen(tls->ca_cert_pem, tls->ca_cert_len) !=
                tls->ca_cert_len - 1U) {
            return MUSE_VM_API_ERR_TLS_CONFIG;
        }
        return MUSE_VM_API_OK;
    }

    pem_length = bounded_strlen(tls->ca_cert_pem, MUSE_VM_API_CA_CERT_MAX);
    if (pem_length == 0U || pem_length >= MUSE_VM_API_CA_CERT_MAX) {
        return MUSE_VM_API_ERR_TLS_CONFIG;
    }
    return MUSE_VM_API_OK;
}

static bool valid_https_base_url(const char *url)
{
    const char *authority;
    const char *cursor;
    size_t length;

    if (url == NULL) {
        return false;
    }
    length = bounded_strlen(url, MUSE_VM_API_URL_MAX);
    if (length == 0U || length >= MUSE_VM_API_URL_MAX ||
        length < sizeof("https://") ||
        memcmp(url, "https://", sizeof("https://") - 1U) != 0) {
        return false;
    }

    authority = url + sizeof("https://") - 1U;
    if (*authority == '\0' || *authority == '/' || *authority == ':' ||
        *authority == '?' || *authority == '#') {
        return false;
    }
    for (cursor = authority; *cursor != '\0' && *cursor != '/'; ++cursor) {
        if (*cursor == '@' || *cursor == '?' || *cursor == '#' ||
            (unsigned char)*cursor <= 0x20U) {
            return false;
        }
    }
    for (; *cursor != '\0'; ++cursor) {
        if (*cursor == '?' || *cursor == '#' ||
            (unsigned char)*cursor <= 0x20U) {
            return false;
        }
    }
    return true;
}

static muse_vm_api_err_t append_url_part(char *output, size_t output_size,
                                         size_t *used, const char *part,
                                         size_t part_length)
{
    if (part_length > output_size - 1U - *used) {
        return MUSE_VM_API_ERR_LIMIT;
    }
    memcpy(output + *used, part, part_length);
    *used += part_length;
    output[*used] = '\0';
    return MUSE_VM_API_OK;
}

static muse_vm_api_err_t build_endpoint_url(const char *base,
                                            const char *path,
                                            char output[MUSE_VM_API_URL_MAX])
{
    size_t base_length = strlen(base);
    size_t path_length = strlen(path);
    size_t used = 0;
    muse_vm_api_err_t result;

    while (base_length > sizeof("https://") - 1U &&
           base[base_length - 1U] == '/') {
        --base_length;
    }
    result = append_url_part(output, MUSE_VM_API_URL_MAX, &used,
                             base, base_length);
    if (result == MUSE_VM_API_OK) {
        result = append_url_part(output, MUSE_VM_API_URL_MAX, &used,
                                 path, path_length);
    }
    return result;
}

static muse_vm_api_err_t make_bearer(const char *token, const char *token_prefix,
                                     char **output, size_t *output_length)
{
    size_t prefix_length = strlen(MUSE_BEARER_PREFIX);
    size_t token_prefix_length = token_prefix != NULL ? strlen(token_prefix) : 0U;
    size_t token_length;
    char *header;

    muse_vm_api_err_t result =
        validate_required_string(token, MUSE_VM_API_TOKEN_MAX);

    if (result != MUSE_VM_API_OK) {
        return result;
    }
    token_length = strlen(token);
    if (prefix_length + token_prefix_length + token_length + 1U <
        token_length) {
        return MUSE_VM_API_ERR_LIMIT;
    }
    header = (char *)malloc(prefix_length + token_prefix_length +
                           token_length + 1U);
    if (header == NULL) {
        return MUSE_VM_API_ERR_HTTP_INIT;
    }
    memcpy(header, MUSE_BEARER_PREFIX, prefix_length);
    if (token_prefix_length != 0U) {
        memcpy(header + prefix_length, token_prefix, token_prefix_length);
    }
    memcpy(header + prefix_length + token_prefix_length, token,
           token_length + 1U);
    *output = header;
    *output_length = prefix_length + token_prefix_length + token_length;
    return MUSE_VM_API_OK;
}

static bool valid_header_value(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    if (value == NULL) {
        return false;
    }
    while (*cursor != '\0') {
        if (*cursor < 0x20U || *cursor == 0x7fU) {
            return false;
        }
        ++cursor;
    }
    return true;
}

static bool ascii_equal_nocase(const char *left, size_t left_length,
                               const char *right)
{
    size_t index;

    if (strlen(right) != left_length) {
        return false;
    }
    for (index = 0; index < left_length; ++index) {
        unsigned char a = (unsigned char)left[index];
        unsigned char b = (unsigned char)right[index];

        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static muse_vm_api_err_t parse_https_url(const char *url,
                                         muse_https_url_t *parsed)
{
    const char *authority;
    const char *authority_end;
    const char *host_start;
    const char *host_end;
    const char *port_start = NULL;
    const char *cursor;
    size_t host_length;
    size_t authority_length;
    unsigned long port = 443U;

    memset(parsed, 0, sizeof(*parsed));
    if (url == NULL ||
        strncmp(url, "https://", sizeof("https://") - 1U) != 0) {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    authority = url + sizeof("https://") - 1U;
    authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/') {
        unsigned char value = (unsigned char)*authority_end;
        if (value <= 0x20U || value == 0x7fU || *authority_end == '@' ||
            *authority_end == '?' || *authority_end == '#') {
            return MUSE_VM_API_ERR_INVALID_ARG;
        }
        ++authority_end;
    }
    if (authority == authority_end) {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }

    host_start = authority;
    if (*host_start == '[') {
        host_start++;
        host_end = host_start;
        while (host_end < authority_end && *host_end != ']') {
            ++host_end;
        }
        if (host_end == authority_end || host_end == host_start) {
            return MUSE_VM_API_ERR_INVALID_ARG;
        }
        if (host_end + 1U < authority_end) {
            if (host_end[1] != ':') {
                return MUSE_VM_API_ERR_INVALID_ARG;
            }
            port_start = host_end + 2U;
        }
    } else {
        host_end = host_start;
        while (host_end < authority_end && *host_end != ':') {
            if (*host_end == '[' || *host_end == ']') {
                return MUSE_VM_API_ERR_INVALID_ARG;
            }
            ++host_end;
        }
        if (host_end < authority_end) {
            port_start = host_end + 1U;
        }
    }
    host_length = (size_t)(host_end - host_start);
    if (host_length == 0U || host_length >= sizeof(parsed->host)) {
        return MUSE_VM_API_ERR_LIMIT;
    }
    if (port_start != NULL) {
        if (port_start == authority_end) {
            return MUSE_VM_API_ERR_INVALID_ARG;
        }
        port = 0U;
        for (cursor = port_start; cursor < authority_end; ++cursor) {
            if (*cursor < '0' || *cursor > '9') {
                return MUSE_VM_API_ERR_INVALID_ARG;
            }
            port = port * 10U + (unsigned long)(*cursor - '0');
            if (port > 65535U) {
                return MUSE_VM_API_ERR_INVALID_ARG;
            }
        }
        if (port == 0U) {
            return MUSE_VM_API_ERR_INVALID_ARG;
        }
    }

    authority_length = (size_t)(authority_end - authority);
    if (authority_length >= sizeof(parsed->host_header)) {
        return MUSE_VM_API_ERR_LIMIT;
    }
    memcpy(parsed->host, host_start, host_length);
    parsed->host[host_length] = '\0';
    memcpy(parsed->host_header, authority, authority_length);
    parsed->host_header[authority_length] = '\0';
    parsed->port = (unsigned int)port;
    parsed->target = *authority_end == '\0' ? "/" : authority_end;
    parsed->target_length = *authority_end == '\0' ? 1U :
        strlen(authority_end);
    return MUSE_VM_API_OK;
}

static muse_vm_api_err_t ssl_write_all(transport_bk_tls_t *ssl,
                                       const char *data, size_t length,
                                       int timeout_ms)
{
    size_t written = 0U;

    while (written < length) {
        size_t remaining = length - written;
        int amount = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        int result = _ssl_write(ssl, data + written, amount, timeout_ms);

        if (result <= 0 || result > amount) {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        written += (size_t)result;
    }
    return MUSE_VM_API_OK;
}

static const char *find_crlf(const char *start, const char *end)
{
    const char *cursor;

    for (cursor = start; end - cursor >= 2; ++cursor) {
        if (cursor[0] == '\r' && cursor[1] == '\n') {
            return cursor;
        }
    }
    return NULL;
}

static bool parse_decimal_size(const char *start, const char *end,
                               size_t *value)
{
    size_t parsed = 0U;
    const char *cursor;

    if (start == end) {
        return false;
    }
    for (cursor = start; cursor < end; ++cursor) {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        digit = (unsigned int)(*cursor - '0');
        if (parsed > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

static bool parse_chunk_size(const char *start, const char *end, size_t *value)
{
    size_t parsed = 0U;
    size_t digits = 0U;
    const char *cursor = start;

    while (cursor < end && *cursor != ';') {
        unsigned int digit;
        unsigned char ch = (unsigned char)*cursor;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10U;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10U;
        } else {
            return false;
        }
        if (parsed > (SIZE_MAX - digit) / 16U) {
            return false;
        }
        parsed = parsed * 16U + digit;
        ++digits;
        ++cursor;
    }
    if (digits == 0U) {
        return false;
    }
    if (cursor < end) {
        ++cursor;
        for (; cursor < end; ++cursor) {
            unsigned char ch = (unsigned char)*cursor;
            if (ch < 0x20U || ch == 0x7fU) {
                return false;
            }
        }
    }
    *value = parsed;
    return true;
}

static muse_vm_api_err_t decode_chunked(char *body, size_t wire_length,
                                        size_t *decoded_length)
{
    const char *end = body + wire_length;
    const char *cursor = body;
    size_t output = 0U;

    for (;;) {
        const char *line_end = find_crlf(cursor, end);
        size_t chunk_size;

        if (line_end == NULL ||
            !parse_chunk_size(cursor, line_end, &chunk_size)) {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        cursor = line_end + 2U;
        if (chunk_size == 0U) {
            for (;;) {
                line_end = find_crlf(cursor, end);
                if (line_end == NULL) {
                    return MUSE_VM_API_ERR_HTTP_IO;
                }
                if (line_end == cursor) {
                    cursor += 2U;
                    if (cursor != end) {
                        return MUSE_VM_API_ERR_HTTP_IO;
                    }
                    body[output] = '\0';
                    *decoded_length = output;
                    return MUSE_VM_API_OK;
                }
                if (*cursor == ' ' || *cursor == '\t' ||
                    memchr(cursor, ':', (size_t)(line_end - cursor)) == NULL) {
                    return MUSE_VM_API_ERR_HTTP_IO;
                }
                cursor = line_end + 2U;
            }
        }
        if (chunk_size > (size_t)(end - cursor) ||
            chunk_size > wire_length - output) {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        memmove(body + output, cursor, chunk_size);
        output += chunk_size;
        cursor += chunk_size;
        if (end - cursor < 2 || cursor[0] != '\r' || cursor[1] != '\n') {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        cursor += 2U;
    }
}

static muse_vm_api_err_t parse_http_response(muse_response_t *response,
                                             int *status_code)
{
    char *end = response->data + response->length;
    char *line_end = (char *)find_crlf(response->data, end);
    char *headers_end = NULL;
    char *cursor;
    bool have_content_length = false;
    bool chunked = false;
    size_t content_length = 0U;
    size_t body_length;

    if (line_end == NULL || line_end - response->data < 12 ||
        memcmp(response->data, "HTTP/1.", 7U) != 0 ||
        (response->data[7] != '0' && response->data[7] != '1') ||
        response->data[8] != ' ' ||
        response->data[9] < '0' || response->data[9] > '9' ||
        response->data[10] < '0' || response->data[10] > '9' ||
        response->data[11] < '0' || response->data[11] > '9' ||
        (line_end - response->data > 12 && response->data[12] != ' ')) {
        return MUSE_VM_API_ERR_HTTP_IO;
    }
    *status_code = (response->data[9] - '0') * 100 +
        (response->data[10] - '0') * 10 + (response->data[11] - '0');

    cursor = line_end + 2U;
    while (cursor < end) {
        char *colon;
        char *name_end;
        char *value;
        char *value_end;

        line_end = (char *)find_crlf(cursor, end);
        if (line_end == NULL) {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        if (line_end == cursor) {
            headers_end = line_end + 2U;
            break;
        }
        if (*cursor == ' ' || *cursor == '\t') {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        colon = (char *)memchr(cursor, ':', (size_t)(line_end - cursor));
        if (colon == NULL) {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        name_end = colon;
        while (name_end > cursor &&
               (name_end[-1] == ' ' || name_end[-1] == '\t')) {
            --name_end;
        }
        value = colon + 1U;
        while (value < line_end && (*value == ' ' || *value == '\t')) {
            ++value;
        }
        value_end = line_end;
        while (value_end > value &&
               (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            --value_end;
        }
        if (ascii_equal_nocase(cursor, (size_t)(name_end - cursor),
                               "Content-Length")) {
            size_t parsed;
            if (!parse_decimal_size(value, value_end, &parsed) ||
                (have_content_length && parsed != content_length)) {
                return MUSE_VM_API_ERR_HTTP_IO;
            }
            have_content_length = true;
            content_length = parsed;
        } else if (ascii_equal_nocase(cursor,
                                      (size_t)(name_end - cursor),
                                      "Transfer-Encoding")) {
            if (chunked ||
                !ascii_equal_nocase(value, (size_t)(value_end - value),
                                    "chunked")) {
                return MUSE_VM_API_ERR_HTTP_IO;
            }
            chunked = true;
        }
        cursor = line_end + 2U;
    }
    if (headers_end == NULL || (chunked && have_content_length)) {
        return MUSE_VM_API_ERR_HTTP_IO;
    }

    body_length = (size_t)(end - headers_end);
    if (chunked) {
        muse_vm_api_err_t result =
            decode_chunked(headers_end, body_length, &body_length);
        if (result != MUSE_VM_API_OK) {
            return result;
        }
        memmove(response->data, headers_end, body_length);
        response->data[body_length] = '\0';
    } else {
        if (have_content_length && content_length != body_length) {
            return MUSE_VM_API_ERR_HTTP_IO;
        }
        memmove(response->data, headers_end, body_length);
        response->data[body_length] = '\0';
    }
    response->length = body_length;
    return MUSE_VM_API_OK;
}

static muse_vm_api_err_t perform_request(const muse_vm_api_tls_t *tls,
                                         const char *url,
                                         bool post,
                                         const char *bearer_token,
                                         const char *bearer_token_prefix,
                                         const char *body,
                                         int *status_code,
                                         muse_response_t *response)
{
    transport_bk_tls_t ssl;
    muse_https_url_t parsed;
    char *authorization = NULL;
    char *request = NULL;
    size_t authorization_length = 0;
    size_t body_length = body != NULL ? strlen(body) : 0U;
    size_t request_capacity;
    int request_length;
    int timeout_ms;
    bool connected = false;
    muse_vm_api_err_t result;

    result = parse_https_url(url, &parsed);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    result = make_bearer(bearer_token, bearer_token_prefix, &authorization,
                         &authorization_length);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    if (!valid_header_value(authorization)) {
        result = MUSE_VM_API_ERR_HTTP_HEADER;
        goto cleanup;
    }
    if (body_length > (size_t)INT_MAX ||
        parsed.target_length > (size_t)INT_MAX ||
        authorization_length > (size_t)INT_MAX) {
        result = MUSE_VM_API_ERR_LIMIT;
        goto cleanup;
    }

    request_capacity = parsed.target_length + strlen(parsed.host_header) +
        authorization_length + body_length + 256U;
    if (request_capacity < body_length) {
        result = MUSE_VM_API_ERR_LIMIT;
        goto cleanup;
    }
    request = (char *)malloc(request_capacity);
    if (request == NULL) {
        result = MUSE_VM_API_ERR_HTTP_INIT;
        goto cleanup;
    }
    if (post) {
        request_length = snprintf(
            request, request_capacity,
            "POST %.*s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: %s\r\n"
            "X-API-Version: 1.0.0\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n\r\n%s",
            (int)parsed.target_length, parsed.target, parsed.host_header,
            authorization, (unsigned long)body_length, body != NULL ? body : "");
    } else {
        request_length = snprintf(
            request, request_capacity,
            "GET %.*s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: %s\r\n"
            "X-API-Version: 1.0.0\r\n"
            "Accept: application/json\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n",
            (int)parsed.target_length, parsed.target, parsed.host_header,
            authorization);
    }
    if (request_length < 0 || (size_t)request_length >= request_capacity) {
        result = MUSE_VM_API_ERR_LIMIT;
        goto cleanup;
    }

    memset(&ssl, 0, sizeof(ssl));
    ssl.sockfd = -1;
    ssl.cfg.cacert_buf = (const unsigned char *)tls->ca_cert_pem;
    ssl.cfg.cacert_bytes = (unsigned int)(tls->ca_cert_len != 0U ?
        tls->ca_cert_len : strlen(tls->ca_cert_pem) + 1U);
    ssl.cfg.skip_common_name = false;
    timeout_ms = tls->timeout_ms != 0U ? (int)tls->timeout_ms :
        (int)MUSE_HTTP_DEFAULT_TIMEOUT;
    if (_ssl_connect(&ssl, parsed.host, (int)parsed.port, timeout_ms) != 0) {
        result = MUSE_VM_API_ERR_HTTP_IO;
        goto cleanup;
    }
    connected = true;
    if (ssl.tls == NULL ||
        mbedtls_ssl_get_verify_result(&ssl.tls->ssl) != 0U) {
        result = MUSE_VM_API_ERR_TLS_CONFIG;
        goto cleanup;
    }
    result = ssl_write_all(&ssl, request, (size_t)request_length, timeout_ms);
    if (result != MUSE_VM_API_OK) {
        goto cleanup;
    }

    response->length = 0U;
    for (;;) {
        char extra;
        size_t available = response->capacity - response->length;
        char *destination = available != 0U ?
            response->data + response->length : &extra;
        int amount = available != 0U ? (int)available : 1;
        int received = _ssl_read(&ssl, destination, amount, timeout_ms);

        if (received == 0) {
            result = MUSE_VM_API_ERR_HTTP_IO;
            goto cleanup;
        }
        if (received == -1) {
            break;
        }
        if (received < -1) {
            result = MUSE_VM_API_ERR_HTTP_IO;
            goto cleanup;
        }
        if (available == 0U) {
            result = MUSE_VM_API_ERR_RESPONSE_TOO_LARGE;
            goto cleanup;
        }
        if ((size_t)received > available) {
            result = MUSE_VM_API_ERR_HTTP_IO;
            goto cleanup;
        }
        response->length += (size_t)received;
    }
    response->data[response->length] = '\0';
    result = parse_http_response(response, status_code);
    if (result != MUSE_VM_API_OK) {
        goto cleanup;
    }
    if (*status_code < 200 || *status_code >= 300) {
        /* Non-2xx: dump a bounded, sanitised body prefix so the server's
         * rejection reason (e.g. /device_token/refresh 401) is visible on
         * the wire instead of a bare status code. */
        char preview[201];
        size_t n = response->length < sizeof(preview) - 1U ?
                   response->length : sizeof(preview) - 1U;
        size_t i;
        memcpy(preview, response->data, n);
        preview[n] = '\0';
        for (i = 0U; i < n; ++i) {
            if (preview[i] == '\r' || preview[i] == '\n' ||
                preview[i] == '\0') {
                preview[i] = ' ';
            }
        }
        BK_LOGW(MUSE_VM_REST_TAG, "http %d %s body[%u]=%.200s\r\n",
                *status_code, url, (unsigned)response->length, preview);
    }
    if (*status_code == 401) {
        result = MUSE_VM_API_ERR_UNAUTHORIZED;
    } else if (*status_code < 200 || *status_code >= 300) {
        result = MUSE_VM_API_ERR_HTTP_STATUS;
    } else {
        result = MUSE_VM_API_OK;
    }

cleanup:
    if (connected) {
        (void)_ssl_base_close(&ssl);
    }
    clear_and_free(request, request != NULL ? request_capacity : 0U);
    clear_and_free(authorization, authorization_length);
    return result;
}

static const char *json_string(const cJSON *object, const char *name)
{
    cJSON *item;

    if (!cJSON_IsObject(object)) {
        return NULL;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : NULL;
}

static muse_vm_api_err_t copy_json_string(const cJSON *object,
                                          const char *name, char *output,
                                          size_t output_size, bool required)
{
    const char *value = json_string(object, name);
    size_t length;

    if (value == NULL || *value == '\0') {
        output[0] = '\0';
        return required ? MUSE_VM_API_ERR_MISSING_FIELD : MUSE_VM_API_OK;
    }
    length = bounded_strlen(value, output_size);
    if (length >= output_size) {
        output[0] = '\0';
        return MUSE_VM_API_ERR_LIMIT;
    }
    memcpy(output, value, length + 1U);
    return MUSE_VM_API_OK;
}

static bool extract_vm_host(const char *url,
                            char output[MUSE_VM_API_VM_HOST_MAX])
{
    const char *authority;
    const char *end;
    const char *host_end;
    const char *port = NULL;
    unsigned long port_number = 0;
    size_t host_length;

    if (url == NULL) {
        return false;
    }
    if (strncmp(url, "wss://", sizeof("wss://") - 1U) == 0) {
        authority = url + sizeof("wss://") - 1U;
    } else if (strncmp(url, "https://", sizeof("https://") - 1U) == 0) {
        authority = url + sizeof("https://") - 1U;
    } else {
        return false;
    }

    end = authority;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') {
        if (*end == '@' || (unsigned char)*end <= 0x20U) {
            return false;
        }
        ++end;
    }
    if (authority == end) {
        return false;
    }

    if (*authority == '[') {
        host_end = authority + 1U;
        while (host_end < end && *host_end != ']') {
            ++host_end;
        }
        if (host_end == end || host_end == authority + 1U ||
            (host_end + 1U < end && host_end[1] != ':')) {
            return false;
        }
        if (host_end + 1U < end) {
            port = host_end + 2U;
        }
        ++authority;
    } else {
        host_end = authority;
        while (host_end < end && *host_end != ':') {
            if (*host_end == '[' || *host_end == ']') {
                return false;
            }
            ++host_end;
        }
        if (host_end < end) {
            port = host_end + 1U;
        }
    }

    if (port != NULL) {
        const char *digit = port;
        if (digit == end) {
            return false;
        }
        while (digit < end) {
            if (*digit < '0' || *digit > '9') {
                return false;
            }
            port_number = port_number * 10U + (unsigned long)(*digit - '0');
            if (port_number > 65535U) {
                return false;
            }
            ++digit;
        }
        if (port_number == 0U) {
            return false;
        }
    }

    host_length = (size_t)(host_end - authority);
    if (host_length == 0U || host_length >= MUSE_VM_API_VM_HOST_MAX) {
        return false;
    }
    memcpy(output, authority, host_length);
    output[host_length] = '\0';
    return true;
}

static bool json_default_flag(const cJSON *entry)
{
    static const char *const names[] = {"default", "is_default", "isDefault"};
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(entry, names[index]);
        if (cJSON_IsTrue(item) ||
            (cJSON_IsNumber(item) && item->valuedouble != 0.0)) {
            return true;
        }
    }
    return false;
}

static const char *json_string_first(const cJSON *object,
                                     const char *const *names, size_t count)
{
    size_t i;
    if (!cJSON_IsObject(object)) {
        return NULL;
    }
    for (i = 0U; i < count; ++i) {
        const char *value = json_string(object, names[i]);
        if (value != NULL && value[0] != '\0') {
            return value;
        }
    }
    return NULL;
}

/* Like json_string_first, but returns the matched key name instead of the
 * value. Diagnostic-only: lets the trace show which alias actually won. */
static const char *json_matched_key(const cJSON *object,
                                    const char *const *names, size_t count)
{
    size_t i;

    if (!cJSON_IsObject(object)) {
        return NULL;
    }
    for (i = 0U; i < count; ++i) {
        const char *value = json_string(object, names[i]);
        if (value != NULL && value[0] != '\0') {
            return names[i];
        }
    }
    return NULL;
}

/* Diagnostic-only: dump the member names of one JSON object on a single
 * bounded log line. Values are never printed, so no secret leaks here. */
static void log_json_object_keys(const char *label, const cJSON *object)
{
    char names[256];
    size_t used = 0U;
    const cJSON *item;

    names[0] = '\0';
    if (!cJSON_IsObject(object)) {
        return;
    }
    cJSON_ArrayForEach(item, object) {
        size_t n = item->string != NULL ? strlen(item->string) : 0U;
        if (n == 0U || used + n + 2U >= sizeof(names)) {
            continue;
        }
        if (used != 0U) {
            names[used++] = ',';
        }
        memcpy(names + used, item->string, n);
        used += n;
        names[used] = '\0';
    }
    BK_LOGI(MUSE_VM_REST_TAG, "fetch_vms %s keys: %s\r\n", label, names);
}

/* Candidate field names for the Noise endpoint and VM token. The backend has
 * used more than one spelling across environments; accept all of them rather
 * than failing a device that is genuinely assigned a VM. */
static const char *const vm_url_names[] = {
    "vm_ws_url", "vm_url", "ws_url", "wss_url", "noise_url",
    "websocket_url", "webSocketUrl", "endpoint", "host"
};
static const char *const vm_token_names[] = {
    "vm_auth_token", "vm_token", "auth_token", "token",
    "access_token", "vmAccessToken"
};
static const char *const vm_id_names[] = {
    "vm_id", "id", "vmId", "name"
};

static const char *vm_url_field(const cJSON *entry)
{
    return json_string_first(entry, vm_url_names,
                             sizeof(vm_url_names) / sizeof(vm_url_names[0]));
}

static const char *vm_token_field(const cJSON *entry)
{
    return json_string_first(entry, vm_token_names,
                             sizeof(vm_token_names) / sizeof(vm_token_names[0]));
}

static const char *vm_id_field(const cJSON *entry)
{
    return json_string_first(entry, vm_id_names,
                             sizeof(vm_id_names) / sizeof(vm_id_names[0]));
}

static muse_vm_api_err_t parse_vm_entry(const cJSON *entry,
                                        muse_vm_info_t *output)
{
    const char *url;
    const char *token;
    const char *id;
    size_t length;

    if (!cJSON_IsObject(entry)) {
        return MUSE_VM_API_ERR_NO_VM;
    }
    url = vm_url_field(entry);
    if (!extract_vm_host(url, output->vm_host)) {
        memset(output, 0, sizeof(*output));
        return MUSE_VM_API_ERR_NO_VM;
    }

    token = vm_token_field(entry);
    if (token == NULL) {
        memset(output, 0, sizeof(*output));
        return MUSE_VM_API_ERR_NO_VM;
    }
    length = bounded_strlen(token, sizeof(output->vm_auth_token));
    if (length >= sizeof(output->vm_auth_token)) {
        memset(output, 0, sizeof(*output));
        return MUSE_VM_API_ERR_LIMIT;
    }
    memcpy(output->vm_auth_token, token, length + 1U);

    output->vm_id[0] = '\0';
    id = vm_id_field(entry);
    if (id != NULL) {
        length = bounded_strlen(id, sizeof(output->vm_id));
        if (length >= sizeof(output->vm_id)) {
            length = sizeof(output->vm_id) - 1U;
        }
        memcpy(output->vm_id, id, length);
        output->vm_id[length] = '\0';
    } else {
        /* Official fallback: derive vm_id from the first host label of
         * vm_ws_url, e.g. wss://<vm_id>.domain/... */
        const char *dot = strchr(output->vm_host, '.');
        if (dot != NULL) {
            size_t label = (size_t)(dot - output->vm_host);
            if (label > 0U && label < sizeof(output->vm_id)) {
                memcpy(output->vm_id, output->vm_host, label);
                output->vm_id[label] = '\0';
            }
        }
    }
    /* Diagnostic-only token provenance (values never printed): which alias
     * won, plus a length/prefix fingerprint. If the selected Noise token is
     * actually the account-level "access_token"/"token" alias, that is the
     * prime suspect for the edge 401 on /v1/noise. */
    {
        const char *hit_url = json_matched_key(
            entry, vm_url_names, sizeof(vm_url_names) / sizeof(vm_url_names[0]));
        const char *hit_token = json_matched_key(
            entry, vm_token_names,
            sizeof(vm_token_names) / sizeof(vm_token_names[0]));
        const char *hit_id = json_matched_key(
            entry, vm_id_names, sizeof(vm_id_names) / sizeof(vm_id_names[0]));
        char pfx[8];
        size_t tok_len = bounded_strlen(output->vm_auth_token,
                                        sizeof(output->vm_auth_token));
        size_t pfx_len = tok_len < sizeof(pfx) - 1U ?
                         tok_len : sizeof(pfx) - 1U;
        memcpy(pfx, output->vm_auth_token, pfx_len);
        pfx[pfx_len] = '\0';
        BK_LOGI(MUSE_VM_REST_TAG,
                "fetch_vms matched fields: url=<%s> token=<%s> id=<%s>; "
                "noise_token len=%u pfx='%s' host='%s'\r\n",
                hit_url != NULL ? hit_url : "?",
                hit_token != NULL ? hit_token : "?",
                hit_id != NULL ? hit_id : "(derived)",
                (unsigned)tok_len, pfx, output->vm_host);
    }
    /* Diagnostic: what state does the backend believe this VM is in? This is
     * the authoritative answer to "is the VM activated", for support
     * tickets. vm_state/default values are not secrets. */
    {
        const cJSON *state = cJSON_GetObjectItem(entry, "vm_state");
        const cJSON *dflt = cJSON_GetObjectItem(entry, "default");
        BK_LOGI(MUSE_VM_REST_TAG,
                "fetch_vms vm_state='%s' default=%d vm_id='%s'\r\n",
                cJSON_IsString(state) ? state->valuestring : "(none)",
                cJSON_IsTrue(dflt) ? 1 : 0,
                output->vm_id[0] != '\0' ? output->vm_id : "(none)");
    }
    return MUSE_VM_API_OK;
}

static cJSON *find_vm_collection(cJSON *root)
{
    cJSON *collection;
    cJSON *data;

    if (cJSON_IsArray(root)) {
        return root;
    }
    if (!cJSON_IsObject(root)) {
        return NULL;
    }
    /* Official muse-gadget-sdk contract: the assigned VMs are returned in a
     * top-level array named "vm_list". Check it first. */
    collection = cJSON_GetObjectItemCaseSensitive(root, "vm_list");
    if (cJSON_IsArray(collection)) {
        return collection;
    }
    collection = cJSON_GetObjectItemCaseSensitive(root, "vms");
    if (cJSON_IsArray(collection) || cJSON_IsObject(collection)) {
        return collection;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsArray(data)) {
        return data;
    }
    if (cJSON_IsObject(data)) {
        collection = cJSON_GetObjectItemCaseSensitive(data, "vm_list");
        if (cJSON_IsArray(collection)) {
            return collection;
        }
        collection = cJSON_GetObjectItemCaseSensitive(data, "vms");
        if (cJSON_IsArray(collection) || cJSON_IsObject(collection)) {
            return collection;
        }
    }
    return root;
}

static muse_vm_api_err_t parse_vms(const char *json, size_t json_length,
                                   muse_vm_info_t *output)
{
    cJSON *root = NULL;
    cJSON *collection;
    cJSON *entry;
    cJSON *explicit_default;
    const char *default_id = NULL;
    muse_vm_info_t first;
    bool have_first = false;
    muse_vm_api_err_t remembered = MUSE_VM_API_ERR_NO_VM;
    muse_vm_api_err_t result = MUSE_VM_API_ERR_NO_VM;

    memset(output, 0, sizeof(*output));
    memset(&first, 0, sizeof(first));
    root = cJSON_ParseWithLength(json, json_length);
    if (root == NULL) {
        return MUSE_VM_API_ERR_JSON;
    }

    /* Official contract: even HTTP 200 may carry a business error. */
    if (cJSON_IsObject(root)) {
        cJSON *err_title = cJSON_GetObjectItemCaseSensitive(root, "error_title");
        cJSON *err_code = cJSON_GetObjectItemCaseSensitive(root, "backend_error_code");
        cJSON *err_summary = cJSON_GetObjectItemCaseSensitive(root, "error_summary");
        if (cJSON_IsString(err_title) || cJSON_IsString(err_code)) {
            BK_LOGW(MUSE_VM_REST_TAG,
                    "fetch_vms backend error: title=%s code=%s summary=%s\r\n",
                    cJSON_IsString(err_title) ? err_title->valuestring : "?",
                    cJSON_IsString(err_code) ? err_code->valuestring : "?",
                    cJSON_IsString(err_summary) ? err_summary->valuestring : "?");
            result = MUSE_VM_API_ERR_NO_VM;
            goto cleanup;
        }
    }

    /* Diagnostic-only: show the shape of the fetch_vms body (key names only,
     * no values) so a missing VM-bearer field is visible on the wire. */
    if (cJSON_IsObject(root)) {
        cJSON *diag_list = cJSON_GetObjectItemCaseSensitive(root, "vm_list");
        cJSON *diag_data = cJSON_GetObjectItemCaseSensitive(root, "data");
        log_json_object_keys("root", root);
        if (cJSON_IsObject(diag_data)) {
            log_json_object_keys("data", diag_data);
        }
        if (cJSON_IsArray(diag_list) && cJSON_GetArraySize(diag_list) > 0) {
            BK_LOGI(MUSE_VM_REST_TAG, "fetch_vms vm_list count=%d\r\n",
                    cJSON_GetArraySize(diag_list));
            log_json_object_keys("vm_list[0]",
                                 cJSON_GetArrayItem(diag_list, 0));
        }
    } else if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
        BK_LOGI(MUSE_VM_REST_TAG, "fetch_vms array count=%d\r\n",
                cJSON_GetArraySize(root));
        log_json_object_keys("[0]", cJSON_GetArrayItem(root, 0));
    }

    explicit_default = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "default_vm") : NULL;
    if (cJSON_IsObject(explicit_default) &&
        parse_vm_entry(explicit_default, output) == MUSE_VM_API_OK) {
        result = MUSE_VM_API_OK;
        goto cleanup;
    }
    if (cJSON_IsString(explicit_default) &&
        explicit_default->valuestring != NULL) {
        default_id = explicit_default->valuestring;
    } else if (cJSON_IsObject(root)) {
        default_id = json_string(root, "default_vm_id");
    }

    collection = find_vm_collection(root);
    if (collection == root && cJSON_IsObject(root) &&
        vm_url_field(root) != NULL) {
        result = parse_vm_entry(root, output);
        goto cleanup;
    }
    if (collection == NULL) {
        goto cleanup;
    }

    cJSON_ArrayForEach(entry, collection) {
        muse_vm_info_t candidate;
        muse_vm_api_err_t candidate_result;

        memset(&candidate, 0, sizeof(candidate));
        candidate_result = parse_vm_entry(entry, &candidate);
        if (candidate_result != MUSE_VM_API_OK) {
            if (candidate_result == MUSE_VM_API_ERR_LIMIT) {
                remembered = candidate_result;
            }
            continue;
        }
        if (json_default_flag(entry) ||
            (default_id != NULL &&
             ((candidate.vm_id[0] != '\0' &&
               strcmp(default_id, candidate.vm_id) == 0) ||
              (json_string(entry, "name") != NULL &&
               strcmp(default_id, json_string(entry, "name")) == 0)))) {
            *output = candidate;
            result = MUSE_VM_API_OK;
            goto cleanup;
        }
        if (!have_first) {
            first = candidate;
            have_first = true;
        }
    }

    if (have_first) {
        *output = first;
        result = MUSE_VM_API_OK;
    } else {
        result = remembered;
    }

cleanup:
    cJSON_Delete(root);
    if (result != MUSE_VM_API_OK) {
        memset(output, 0, sizeof(*output));
    }
    return result;
}

static cJSON *token_payload(cJSON *root)
{
    cJSON *data;

    if (!cJSON_IsObject(root)) {
        return NULL;
    }
    if (json_string(root, "access_token") != NULL ||
        json_string(root, "refresh_token") != NULL) {
        return root;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    return cJSON_IsObject(data) ? data : root;
}

static muse_vm_api_err_t parse_tokens(const char *json, size_t json_length,
                                      muse_vm_device_tokens_t *output)
{
    cJSON *root;
    cJSON *payload;
    muse_vm_api_err_t result;

    memset(output, 0, sizeof(*output));
    root = cJSON_ParseWithLength(json, json_length);
    if (root == NULL) {
        return MUSE_VM_API_ERR_JSON;
    }
    payload = token_payload(root);
    if (payload == NULL) {
        result = MUSE_VM_API_ERR_JSON;
        goto cleanup;
    }

    result = copy_json_string(payload, "access_token", output->access_token,
                              sizeof(output->access_token), true);
    if (result == MUSE_VM_API_OK) {
        result = copy_json_string(payload, "refresh_token",
                                  output->refresh_token,
                                  sizeof(output->refresh_token), true);
    }

cleanup:
    cJSON_Delete(root);
    if (result != MUSE_VM_API_OK) {
        memset(output, 0, sizeof(*output));
    }
    return result;
}

static muse_vm_api_err_t make_token_body(const char *device_id,
                                         const char *sdk_token,
                                         char **output)
{
    cJSON *root;
    char *json = NULL;

    root = cJSON_CreateObject();
    if (root == NULL) {
        return MUSE_VM_API_ERR_JSON;
    }
    if (cJSON_AddStringToObject(root, "device_id", device_id) == NULL ||
        (sdk_token != NULL && *sdk_token != '\0' &&
         cJSON_AddStringToObject(root, "sdk_token", sdk_token) == NULL)) {
        cJSON_Delete(root);
        return MUSE_VM_API_ERR_JSON;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return MUSE_VM_API_ERR_JSON;
    }
    *output = json;
    return MUSE_VM_API_OK;
}

static muse_vm_api_err_t validate_common(const muse_vm_api_tls_t *tls,
                                         const char *api_base_url,
                                         const char *access_token)
{
    muse_vm_api_err_t result = validate_tls(tls);
    size_t url_length;

    if (result != MUSE_VM_API_OK) {
        return result;
    }
    if (api_base_url == NULL || *api_base_url == '\0') {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    url_length = bounded_strlen(api_base_url, MUSE_VM_API_URL_MAX);
    if (url_length >= MUSE_VM_API_URL_MAX) {
        return MUSE_VM_API_ERR_LIMIT;
    }
    if (!valid_https_base_url(api_base_url)) {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    result = validate_required_string(access_token, MUSE_VM_API_TOKEN_MAX);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    if (tls->timeout_ms > (unsigned int)INT32_MAX) {
        return MUSE_VM_API_ERR_LIMIT;
    }
    return MUSE_VM_API_OK;
}

muse_vm_api_err_t muse_vm_api_fetch_vms(
    const muse_vm_api_tls_t *tls,
    const char *api_base_url,
    const char *access_bearer_token,
    const char *device_or_node_id,
    muse_vm_info_t *out_vm)
{
    char url[MUSE_VM_API_URL_MAX];
    muse_response_t response;
    muse_vm_api_err_t result;
    int status_code = 0;

    if (out_vm == NULL) {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    memset(out_vm, 0, sizeof(*out_vm));
    result = validate_required_string(device_or_node_id,
                                      MUSE_VM_API_DEVICE_ID_MAX);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    result = validate_common(tls, api_base_url, access_bearer_token);
    if (result != MUSE_VM_API_OK) {
        return result;
    }

    /* Official contract: GET /fetch_vms with no query string; the device is
     * identified solely by its Bearer access token. (The node_id argument is
     * retained for API compatibility but is not sent.) */
    (void)device_or_node_id;
    result = build_endpoint_url(api_base_url, "/fetch_vms", url);
    if (result != MUSE_VM_API_OK) {
        return result;
    }

    memset(&response, 0, sizeof(response));
    response.capacity = MUSE_VM_API_RESPONSE_MAX;
    response.data = (char *)calloc(response.capacity + 1U, 1U);
    if (response.data == NULL) {
        return MUSE_VM_API_ERR_HTTP_INIT;
    }
    result = perform_request(tls, url, false, access_bearer_token,
                             NULL, NULL, &status_code, &response);
    if (result == MUSE_VM_API_OK) {
        result = parse_vms(response.data, response.length, out_vm);
        if (result != MUSE_VM_API_OK) {
            /* 2xx but no usable VM: dump a bounded, sanitised prefix of the
             * server body so an empty/placeholder/account response is visible
             * on the wire instead of a bare err=-11. */
            char preview[256];
            size_t n = response.length < sizeof(preview) - 1U ?
                       response.length : sizeof(preview) - 1U;
            size_t i;
            memcpy(preview, response.data, n);
            preview[n] = '\0';
            for (i = 0U; i < n; ++i) {
                if (preview[i] == '\r' || preview[i] == '\n' ||
                    preview[i] == '\0') {
                    preview[i] = ' ';
                }
            }
            BK_LOGW(MUSE_VM_REST_TAG,
                    "fetch_vms no usable VM (err=%d) http=%d body[%u]=%.200s\r\n",
                    (int)result, status_code, (unsigned)response.length,
                    preview);
        }
    } else {
        BK_LOGW(MUSE_VM_REST_TAG,
                "fetch_vms request failed (err=%d) http=%d\r\n",
                (int)result, status_code);
    }
    clear_and_free(response.data, response.capacity + 1U);
    return result;
}

static muse_vm_api_err_t token_request(
    const muse_vm_api_tls_t *tls,
    const char *api_base_url,
    const char *path,
    const char *bearer_token,
    const char *bearer_token_prefix,
    const char *device_id,
    const char *sdk_token,
    muse_vm_device_tokens_t *out_tokens)
{
    char url[MUSE_VM_API_URL_MAX];
    char *body = NULL;
    muse_response_t response;
    muse_vm_api_err_t result;
    int status_code = 0;

    result = build_endpoint_url(api_base_url, path, url);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    result = make_token_body(device_id, sdk_token, &body);
    if (result != MUSE_VM_API_OK) {
        return result;
    }

    memset(&response, 0, sizeof(response));
    response.capacity = MUSE_VM_API_RESPONSE_MAX;
    response.data = (char *)calloc(response.capacity + 1U, 1U);
    if (response.data == NULL) {
        clear_and_free(body, strlen(body));
        return MUSE_VM_API_ERR_HTTP_INIT;
    }

    result = perform_request(tls, url, true, bearer_token,
                             bearer_token_prefix, body, &status_code,
                             &response);
    if (result == MUSE_VM_API_OK) {
        result = parse_tokens(response.data, response.length, out_tokens);
    }
    clear_and_free(response.data, response.capacity + 1U);
    clear_and_free(body, strlen(body));
    return result;
}

muse_vm_api_err_t muse_vm_api_refresh_device_token(
    const muse_vm_api_tls_t *tls,
    const char *api_base_url,
    const char *access_bearer_token,
    const char *raw_refresh_token,
    const char *device_id,
    const char *sdk_token,
    muse_vm_device_tokens_t *out_tokens)
{
    muse_vm_api_err_t result;
    const char *raw;

    if (out_tokens == NULL) {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    memset(out_tokens, 0, sizeof(*out_tokens));
    result = validate_required_string(raw_refresh_token,
                                      MUSE_VM_API_TOKEN_MAX);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    result = validate_required_string(device_id, MUSE_VM_API_DEVICE_ID_MAX);
    if (result != MUSE_VM_API_OK) {
        return result;
    }
    if (sdk_token != NULL && *sdk_token != '\0') {
        result = validate_required_string(sdk_token, MUSE_VM_API_TOKEN_MAX);
        if (result != MUSE_VM_API_OK) {
            return result;
        }
    }
    result = validate_common(tls, api_base_url, access_bearer_token);
    if (result != MUSE_VM_API_OK) {
        return result;
    }

    /* Upstream (esp32 app.c, linux muse_api.py) never presents the access
     * token to this endpoint: the server can accept it and answer 200 with
     * replacement tokens that every endpoint then rejects, overwriting the
     * working credentials. Go straight to the refresh-token leg. Apps may
     * hand over refresh tokens that already carry the "hatch_refresh:"
     * prefix; doubling it makes the server reject the token, so strip
     * everything up to the last ':' (esp32 make_refresh_bearer, linux
     * rsplit). */
    raw = strrchr(raw_refresh_token, ':');
    raw = raw != NULL ? raw + 1 : raw_refresh_token;
    if (*raw == '\0') {
        return MUSE_VM_API_ERR_INVALID_ARG;
    }
    return token_request(tls, api_base_url, "/device_token/refresh",
                         raw, MUSE_REFRESH_PREFIX, device_id, sdk_token,
                         out_tokens);
}
