/*************************************************************
 *
 * Common HTTPS image upload helper for the Beken AI agent stack.
 * Copyright (C) 2025 Beken Corporation
 * All rights reserved.
 *
 *************************************************************/
#include <stdio.h>
#include <string.h>
#include <components/system.h>
#include <components/log.h>
#include <components/webclient.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "cJSON.h"
#include "bk_image_upload.h"

#define TAG "bk_img_upload"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* Multipart boundary used by the upload. Anything is fine as long as
 * it never appears in the JPEG byte stream; pick a long ASCII-only
 * token so the probability of a random collision is negligible. */
#define BK_IMG_UPLOAD_BOUNDARY          "----BekenImgUploadBoundary7c9c3d2e8f1a4b"

/* form-data field name + uploaded filename. The server keys off the
 * "file" field (see FastAPI example: file=@... in curl). The filename
 * is informational only -- the server renames every upload to
 * "<sha1>.jpg" under /images/<YYYYMMDD>/. */
#define BK_IMG_UPLOAD_FIELD_NAME        "file"
#define BK_IMG_UPLOAD_FILENAME          "image.jpg"
#define BK_IMG_UPLOAD_CONTENT_TYPE      "image/jpeg"

/* Response buffer just has to hold the JSON ack
 * ({code,message,image_url}). 1 KB is generous; the URL part is < 100
 * bytes in practice. */
#define BK_IMG_UPLOAD_RESP_BUF_SIZE     1024

/* webclient header working buffer. Matches the size used by
 * agora_agent_send_request -- enough room for Content-Length +
 * Content-Type + boundary + any default headers the SDK inserts. */
#define BK_IMG_UPLOAD_HEADER_BUF_SIZE   1024

/* Parse the JSON ack from POST /api/upload-image and copy "image_url"
 * out. Accepted shape:
 *   { "code": 0, "message": "upload success",
 *     "image_url": "https://apics.aclsemi.com/images/..." }
 * Any non-zero "code" or missing "image_url" string is treated as
 * server-side failure and the URL is left untouched. */
static bk_err_t bk_image_upload_parse_rsp(const char *buffer,
                                          char *image_url_out, size_t image_url_out_len)
{
    bk_err_t ret = BK_FAIL;
    cJSON *json = NULL;
    cJSON *code = NULL;
    cJSON *image_url = NULL;

    if (!buffer || !image_url_out || image_url_out_len == 0)
    {
        LOGE("parse_rsp: invalid args\r\n");
        return BK_FAIL;
    }

    json = cJSON_Parse(buffer);
    if (!json)
    {
        LOGE("parse_rsp: JSON parse error near [%s]\r\n",
             cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "(null)");
        return BK_FAIL;
    }

    code = cJSON_GetObjectItem(json, "code");
    if (!code || ((code->type & 0xFF) != cJSON_Number))
    {
        LOGE("parse_rsp: missing/invalid 'code'\r\n");
        goto __exit;
    }
    if (code->valueint != 0)
    {
        cJSON *message = cJSON_GetObjectItem(json, "message");
        LOGE("parse_rsp: server rejected upload, code=%d msg=%s\r\n",
             code->valueint,
             (message && ((message->type & 0xFF) == cJSON_String) && message->valuestring)
                 ? message->valuestring : "(none)");
        goto __exit;
    }

    image_url = cJSON_GetObjectItem(json, "image_url");
    if (!image_url || ((image_url->type & 0xFF) != cJSON_String) || !image_url->valuestring)
    {
        LOGE("parse_rsp: missing 'image_url'\r\n");
        goto __exit;
    }

    if (os_strlen(image_url->valuestring) + 1 > image_url_out_len)
    {
        LOGE("parse_rsp: image_url too long (%u >= %u)\r\n",
             (unsigned)os_strlen(image_url->valuestring), (unsigned)image_url_out_len);
        goto __exit;
    }

    os_strcpy(image_url_out, image_url->valuestring);
    ret = BK_OK;

__exit:
    cJSON_Delete(json);
    return ret;
}

bk_err_t bk_image_upload_jpeg(const uint8_t *jpeg, size_t jpeg_len,
                              char *image_url_out, size_t image_url_out_len)
{
    bk_err_t ret = BK_FAIL;
    struct webclient_session *session = NULL;
    uint8_t *body = NULL;
    char *response_buffer = NULL;
    int resp_status = 0;
    int bytes_read = 0;
    /* Length of the multipart head/tail is bounded by a few short
     * literals + the boundary + the filename. ~256 bytes is enough; we
     * still snprintf with bounds-check to stay tidy. */
    char head[256] = {0};
    static const char tail[] = "\r\n--" BK_IMG_UPLOAD_BOUNDARY "--\r\n";
    size_t head_len = 0;
    size_t tail_len = sizeof(tail) - 1; /* strlen(tail), excluding NUL */
    size_t body_len = 0;

    if (!jpeg || jpeg_len == 0)
    {
        LOGE("upload_jpeg: jpeg buffer required\r\n");
        return BK_FAIL;
    }
    if (!image_url_out || image_url_out_len == 0)
    {
        LOGE("upload_jpeg: output buffer required\r\n");
        return BK_FAIL;
    }
    image_url_out[0] = '\0';

    /* RFC 7578 multipart/form-data preamble for a single file field:
     *   --<boundary>\r\n
     *   Content-Disposition: form-data; name="file"; filename="<name>"\r\n
     *   Content-Type: image/jpeg\r\n
     *   \r\n
     * The closing boundary "\r\n--<boundary>--\r\n" is appended at the
     * end of the byte stream (see `tail`). */
    {
        int n = os_snprintf(head, sizeof(head),
                            "--%s\r\n"
                            "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                            "Content-Type: %s\r\n"
                            "\r\n",
                            BK_IMG_UPLOAD_BOUNDARY,
                            BK_IMG_UPLOAD_FIELD_NAME,
                            BK_IMG_UPLOAD_FILENAME,
                            BK_IMG_UPLOAD_CONTENT_TYPE);
        if (n <= 0 || n >= (int)sizeof(head))
        {
            LOGE("upload_jpeg: multipart head overflow (n=%d)\r\n", n);
            return BK_FAIL;
        }
        head_len = (size_t)n;
    }

    body_len = head_len + jpeg_len + tail_len;
    LOGI("upload_jpeg: jpeg=%u head=%u tail=%u body=%u\r\n",
         (unsigned)jpeg_len, (unsigned)head_len, (unsigned)tail_len, (unsigned)body_len);

    /* Single contiguous buffer keeps webclient_post() simple: it does
     * one webclient_write() for the whole payload, so we don't need to
     * dig into the lower-level webclient_connect/_send_header API. */
    body = (uint8_t *)psram_malloc(body_len);
    if (!body)
    {
        LOGE("upload_jpeg: psram_malloc(%u) OOM for body\r\n", (unsigned)body_len);
        return BK_FAIL;
    }
    os_memcpy(body, head, head_len);
    os_memcpy(body + head_len, jpeg, jpeg_len);
    os_memcpy(body + head_len + jpeg_len, tail, tail_len);

    response_buffer = (char *)psram_malloc(BK_IMG_UPLOAD_RESP_BUF_SIZE);
    if (!response_buffer)
    {
        LOGE("upload_jpeg: psram_malloc(%u) OOM for response\r\n",
             (unsigned)BK_IMG_UPLOAD_RESP_BUF_SIZE);
        goto __exit;
    }
    os_memset(response_buffer, 0, BK_IMG_UPLOAD_RESP_BUF_SIZE);

    session = webclient_session_create(BK_IMG_UPLOAD_HEADER_BUF_SIZE);
    if (!session)
    {
        LOGE("upload_jpeg: webclient_session_create failed\r\n");
        goto __exit;
    }

    /* Headers must be set BEFORE webclient_post(): the post call
     * fires the request line + accumulated headers + body in one go.
     * Note: Content-Length is the full multipart body, NOT just the
     * JPEG. Content-Type carries the boundary that delimits each
     * form-data part on the server side. */
    webclient_header_fields_add(session, "Content-Length: %u\r\n", (unsigned)body_len);
    webclient_header_fields_add(session,
                                "Content-Type: multipart/form-data; boundary=%s\r\n",
                                BK_IMG_UPLOAD_BOUNDARY);

    LOGI("upload_jpeg: POST %s body=%u bytes\r\n", BK_IMAGE_UPLOAD_URL, (unsigned)body_len);

    /* Binary-safe post: pass body_len explicitly so embedded NUL bytes
     * in the JPEG are not truncated by an internal strlen(). */
    resp_status = webclient_post(session, BK_IMAGE_UPLOAD_URL, body, body_len);
    if (resp_status != 200)
    {
        LOGE("upload_jpeg: HTTP POST failed, status=%d\r\n", resp_status);
        goto __exit;
    }

    /* The upload server replies with a short JSON ack; a single read
     * is enough in practice. Loop until we get at least one non-empty
     * chunk to mirror the existing webclient usage pattern in this
     * component. */
    do
    {
        bytes_read = webclient_read(session, response_buffer,
                                    BK_IMG_UPLOAD_RESP_BUF_SIZE - 1);
        if (bytes_read > 0)
        {
            break;
        }
        if (bytes_read < 0)
        {
            LOGE("upload_jpeg: webclient_read failed: %d\r\n", bytes_read);
            goto __exit;
        }
    } while (1);
    response_buffer[bytes_read] = '\0';

    LOGI("upload_jpeg: response (%d bytes): %s\r\n", bytes_read, response_buffer);

    if (BK_OK != bk_image_upload_parse_rsp(response_buffer, image_url_out, image_url_out_len))
    {
        LOGE("upload_jpeg: rsp parse failed\r\n");
        goto __exit;
    }

    LOGI("upload_jpeg: success, image_url=%s\r\n", image_url_out);
    ret = BK_OK;

__exit:
    if (session)
    {
        webclient_close(session);
    }
    if (response_buffer)
    {
        web_free(response_buffer);
    }
    if (body)
    {
        psram_free(body);
    }
    return ret;
}
