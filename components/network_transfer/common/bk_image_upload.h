/*************************************************************
 *
 * Common HTTPS image upload helper for the Beken AI agent stack.
 * Copyright (C) 2025 Beken Corporation
 * All rights reserved.
 *
 *************************************************************/
#ifndef __BK_IMAGE_UPLOAD_H__
#define __BK_IMAGE_UPLOAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <common/bk_typedef.h>
#include <stddef.h>
#include <stdint.h>

/**
 * HTTPS endpoint that the Beken web team provides for staging JPEGs
 * that exceed the inline-payload ceiling of various RTC/RTM channels
 * (e.g. Agora RTM "image.upload" base64 path is capped at ~22 KB raw).
 *
 * The server responds with
 *   { "code": 0, "message": "upload success",
 *     "image_url": "https://apics.aclsemi.com/images/<date>/<sha>.jpg" }
 *
 * The returned image_url is then handed to whichever per-backend
 * "send image URL" API the active RTC engine exposes (e.g.
 * bk_agora_rtm_send_image_url). Define BK_IMAGE_UPLOAD_URL on the
 * compile line to point devices at a private mirror.
 */
#ifndef BK_IMAGE_UPLOAD_URL
//#define BK_IMAGE_UPLOAD_URL            "https://apics.aclsemi.com/api/upload-image"
#define BK_IMAGE_UPLOAD_URL            "http://apics.aclsemi.com/api/upload-image"
#endif

/**
 * Cap on the image_url string handed back to the caller. Must fit any
 * value the upload server can return ("https://apics.aclsemi.com/
 * images/YYYYMMDD/<32hex>.jpg" -> ~80 chars), with headroom.
 */
#define BK_IMAGE_URL_MAX_LEN            256

/**
 * @brief Upload a JPEG to the configured image-upload HTTPS server and
 *        return the publicly reachable URL.
 *
 * Sends a single multipart/form-data POST request with the JPEG as one
 * file field (name="file", filename="image.jpg", Content-Type=image/jpeg),
 * parses the JSON response, and copies the returned "image_url" into
 * @p image_url_out.
 *
 * Designed to be RTC-backend agnostic: Agora, Volc, Lingxin, Sensenova,
 * etc. all share this one HTTPS staging server, and only differ in
 * which "send image URL" RTM/WSS API they call afterwards with the
 * returned URL.
 *
 * Memory: the multipart body is built once in a single PSRAM buffer
 * (header + jpeg + trailer) and posted in one shot. For a typical
 * 100-500 KB JPEG this is a few hundred KB of peak working set, all
 * released before the call returns.
 *
 * Network: uses the project's webclient stack. The default URL is
 * HTTPS, so the project must enable webclient TLS (CONFIG_WEBCLIENT_TLS
 * + CONFIG_PSA_MBEDTLS / CONFIG_MBEDTLS) for this to work.
 *
 * @param[in]  jpeg              Pointer to raw JPEG bytes.
 * @param[in]  jpeg_len          Size of jpeg in bytes (must be > 0).
 * @param[out] image_url_out     Destination string buffer; on success
 *                               receives a NUL-terminated http/https URL.
 *                               Left as an empty string on any failure.
 * @param[in]  image_url_out_len Capacity of @p image_url_out. Should be
 *                               at least BK_IMAGE_URL_MAX_LEN.
 *
 * @return BK_OK on success; BK_FAIL on network / parse / size error.
 */
bk_err_t bk_image_upload_jpeg(const uint8_t *jpeg, size_t jpeg_len,
                              char *image_url_out, size_t image_url_out_len);

#ifdef __cplusplus
}
#endif
#endif /* __BK_IMAGE_UPLOAD_H__ */
