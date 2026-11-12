/**
 * @file muse_vm_api.h
 * @brief Synchronous, bounded Muse VM REST client.
 */
#ifndef __BK_MUSE_VM_API_H__
#define __BK_MUSE_VM_API_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSE_VM_API_URL_MAX          512U
#define MUSE_VM_API_TOKEN_MAX        2048U
#define MUSE_VM_API_DEVICE_ID_MAX    128U
#define MUSE_VM_API_VM_HOST_MAX      256U
#define MUSE_VM_API_VM_ID_MAX        128U
#define MUSE_VM_API_RESPONSE_MAX     16384U
/* Fits the built-in 15-root bundle (22.7 KB PEM) with headroom; only ever
 * a validation bound — the PEM is referenced by pointer, never copied. */
#define MUSE_VM_API_CA_CERT_MAX      32768U

typedef enum {
    MUSE_VM_API_OK = 0,
    MUSE_VM_API_ERR_INVALID_ARG = -1,
    MUSE_VM_API_ERR_LIMIT = -2,
    MUSE_VM_API_ERR_TLS_CONFIG = -3,
    MUSE_VM_API_ERR_HTTP_INIT = -4,
    MUSE_VM_API_ERR_HTTP_HEADER = -5,
    MUSE_VM_API_ERR_HTTP_IO = -6,
    MUSE_VM_API_ERR_HTTP_STATUS = -7,
    MUSE_VM_API_ERR_UNAUTHORIZED = -8,
    MUSE_VM_API_ERR_RESPONSE_TOO_LARGE = -9,
    MUSE_VM_API_ERR_JSON = -10,
    MUSE_VM_API_ERR_NO_VM = -11,
    MUSE_VM_API_ERR_MISSING_FIELD = -12
} muse_vm_api_err_t;

/**
 * A PEM trust anchor is mandatory. Its length includes the terminating NUL
 * when supplied (zero means strlen() + 1). The client never enables either
 * skip_cert_common_name_check or an insecure TLS verification mode.
 */
typedef struct {
    const char *ca_cert_pem;
    size_t ca_cert_len;
    unsigned int timeout_ms;
} muse_vm_api_tls_t;

typedef struct {
    char vm_host[MUSE_VM_API_VM_HOST_MAX];
    char vm_auth_token[MUSE_VM_API_TOKEN_MAX];
    char vm_id[MUSE_VM_API_VM_ID_MAX];
} muse_vm_info_t;

typedef struct {
    char access_token[MUSE_VM_API_TOKEN_MAX];
    char refresh_token[MUSE_VM_API_TOKEN_MAX];
} muse_vm_device_tokens_t;

/**
 * GET <api_base_url>/fetch_vms (no query string; device identity is the
 * Bearer access token). The default VM is selected from the official
 * "vm_list" array, or the first usable entry if no default exists.
 */
muse_vm_api_err_t muse_vm_api_fetch_vms(
    const muse_vm_api_tls_t *tls,
    const char *api_base_url,
    const char *access_bearer_token,
    const char *device_or_node_id,
    muse_vm_info_t *out_vm);

/**
 * POST <api_base_url>/device_token/refresh. The access token is attempted
 * first. On HTTP 401 only, the request is retried with:
 *     Authorization: Bearer hatch_refresh:<raw_refresh_token>
 */
muse_vm_api_err_t muse_vm_api_refresh_device_token(
    const muse_vm_api_tls_t *tls,
    const char *api_base_url,
    const char *access_bearer_token,
    const char *raw_refresh_token,
    const char *device_id,
    const char *sdk_token,
    muse_vm_device_tokens_t *out_tokens);

#ifdef __cplusplus
}
#endif

#endif
