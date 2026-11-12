# Muse Gadget minimum protocol contract

Upstream: `facebookincubator/muse-gadget-sdk`

Pinned commit: `812c46f9fe42b69d916613a9dc702cbac5ff7b54`

The upstream code is Apache-2.0 licensed. Any copied source must retain its
original copyright and license header. This document records the interoperability
contract; it does not relicense the Muse service or SDK token.

## Scope

The first BK7259 integration implements community pairing v5, the Noise control
session, and `device.health`. UI, voice, images, home-network tunnel and OTA are
out of scope.

## BLE

- Service: `7fdd3d1c-38ea-46cf-8b46-314ecf5f240c`
- RX characteristic: `4d593029-28a2-4a6e-a1f0-3c2d5e8f9b01`
- TX characteristic: `d75dc4ca-7b2b-4e9c-8f0a-1d2e3f4a5b6c`
- Local name: `MuseGadget-XXXXXX`
- Pairing protocol: version 5, model `hatch_link`
- Community authentication has no manufacturer attestation. It requires the
  physical-button policy.

The command sequence is:

1. Plaintext `pairing_client_hello`.
2. Device hello response.
3. Encrypted `pairing_client_finished`.
4. Device arms physical confirmation only after sending the encrypted prompt.
5. S4 confirms the active generation.
6. Encrypted `pairing_confirmed` status carries the build-time SDK token.
7. Encrypted provisioning commands deliver Wi-Fi/account data.
8. Credentials and `setup_complete` are committed atomically.

After a hello succeeds, arbitrary plaintext commands and statuses are blocked.
Encrypted records use the label `hatch-link ble setup v1`, per-direction
counters, a 12-byte AES-GCM nonce and a 16-byte tag. Pairing uses P-256 ECDH,
HKDF-SHA256 and AES-256-GCM; the community signer does not use the production
eFuse ECDSA identity.

Timeouts from the pinned implementation:

- client-finished: 60 seconds
- physical confirmation: 60 seconds
- ready/provisioning: 120 seconds

## Persistent data

The upstream setup keys are:

`ssid`, `password`, `wifi_channel`, `wifi_others`, `wifi_hidden`,
`access_token`, `refresh_token`, `vm_url`, `username`, `api_url`,
`api_url_v2`, `noise_host`, and `setup_complete`.

`config_clear_pairing` preserves Wi-Fi and device-level data.
`config_clear_setup` removes Wi-Fi, account credentials and setup completion.
The BK port must use bounded values and a commit marker written only after all
credential writes succeed.

The SDK token is not a stored device credential. It is injected at build time
and included in the encrypted `pairing_confirmed` status so the App can present
it when minting the device token. Logs may print only a short prefix.

## VM API

Default API base is supplied by upstream `vm_api.h`. Relevant paths are:

- `GET /fetch_vms`, bearer access token
- `POST /device_token/mint`, bearer access token
- `POST /device_token/refresh`, bearer access token, then
  `Bearer hatch_refresh:<raw-refresh-token>` after HTTP 401

Mint and refresh bodies contain `device_id` and, when configured, `sdk_token`.
Responses provide an `access_token` and `refresh_token`. VM entries provide
`vm_ws_url` (legacy `vm_url`), `vm_auth_token`, optional `vm_id`, name and
default flag. A VM must be connected using the token from the same VM entry.

## Noise control session

- Host default: `hatch.metaaivm.com`
- Handshake: `Noise_XX_25519_AESGCM_SHA256`
- Transport: WebSocket over TLS, then Noise messages in binary WebSocket
  payloads
- Noise nonce: four zero bytes followed by a big-endian 64-bit counter
- Transport payloads are protobuf-like ordered chunk frames, then service
  envelopes

The BK Mbed TLS backend must provide X25519, SHA-256, HMAC/HKDF-SHA256,
AES-256-GCM and secure random bytes. A failed authenticated decrypt poisons the
receive state. This SDK's normal AP build has PSA client headers but no TF-M PSA
service, so the port uses the equivalent classic Mbed TLS primitives.

## Command contract

After the Noise handshake, the device sends a JSON-RPC-style request whose
method is `link.register`. The minimum registration advertises only
`device.health`. `link.invoke` carries a command name, request id and parameters.

The minimum response preserves the request id and returns:

```json
{"ok":true,"payload":{"board":"BK7259","firmware":"<version>","network":{"connected":true}}}
```

The exact upstream health fields may be extended, but existing field types must
not change. Unimplemented commands must not be advertised.

