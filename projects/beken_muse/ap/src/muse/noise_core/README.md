# Muse Noise Core

This directory contains the unmodified `include/` and PSA-backed `src/`
implementation from:

- Repository: `facebookincubator/muse-gadget-sdk`
- Upstream path: `esp32/components/noise_core`
- Commit: `812c46f9fe42b69d916613a9dc702cbac5ff7b54`
- License: Apache License 2.0 (preserved in every upstream source file)

The seven source files selected by the upstream component use PSA Crypto.
`MbedtlsCryptoBackend.cpp` is intentionally not copied because it is not in
the upstream ESP32 component source list.

The protocol is `Noise_XX_25519_AESGCM_SHA256`; do not substitute P-256 or a
different Noise construction. The BK PSA configuration must provide X25519
(`PSA_ECC_FAMILY_MONTGOMERY`, 255 bits), ECDH, AES-256-GCM, HMAC/HKDF-SHA-256,
SHA-256, random generation, and volatile key import/export/generation.

Host checks:

```sh
# Compile all production sources against the BK PSA headers.
c++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -Iinclude -I/path/to/bk_sdk/ap/components/psa_mbedtls/mbedtls/include \
  src/ClientSession.cpp src/InitiatorHandshake.cpp src/PsaCryptoBackend.cpp \
  src/ServiceCodec.cpp src/Status.cpp src/Transport.cpp \
  src/TransportFrameCodec.cpp

# Codec round-trip smoke test (does not require a host PSA library).
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
  tests/codec_smoke.cpp src/ServiceCodec.cpp src/TransportFrameCodec.cpp \
  src/Status.cpp -o /tmp/noise_core_codec_smoke
/tmp/noise_core_codec_smoke
```

`tests/psa_crypto_smoke.cpp` additionally checks SHA-256, X25519 agreement,
RFC 5869 HKDF output, AES-GCM round trip, and authentication failure. It must
be linked with a host-built PSA/Mbed TLS library.
