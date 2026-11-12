#ifndef MUSE_CA_CERT_H
#define MUSE_CA_CERT_H

/*
 * Built-in TLS trust anchors (NUL-terminated PEM bundle) for the Muse cloud
 * endpoints. Public root certificates only; the buffer lives in flash .rodata.
 * An SD-card CA installed through muse_service_set_ca_cert() takes precedence.
 */
extern const char g_muse_builtin_ca_cert[];

#endif /* MUSE_CA_CERT_H */
