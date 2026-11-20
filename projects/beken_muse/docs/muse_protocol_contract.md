# Muse Gadget protocol integration contract

Upstream: `facebookincubator/muse-gadget-sdk`

Pinned commit: `812c46f9fe42b69d916613a9dc702cbac5ff7b54`

The upstream code is Apache-2.0 licensed. Any copied source must retain its
original copyright and license header. This document records the
interoperability contract implemented by the BK7259 port; it does not relicense
the Muse service or SDK token.

Field names and framing follow the pinned upstream. Where the BK port adds or
deviates, the difference is called out explicitly in the relevant section.

## 1. Scope

Implemented in `beken_muse/ap/src/muse/`:

- BLE community pairing v5 and Wi-Fi/account provisioning
  (`muse_ble_pairing.c`, `muse_pairing.c`)
- VM API device-token mint/refresh (`muse_vm_api.c`)
- Noise control session with `link.register` / `link.invoke`
  (`muse_session.cpp`, `muse_commands.c`)
- Display commands: `display.draw_url`, `display.show_animation`,
  `display.draw_text` (`muse_display.c`)
- Voice/photo turns: note upload plus reply subscription
  (`muse_voice.c`, `muse_voice_proto.c`, `muse_chat.cpp`)
- Optional Volcano streaming ASR and TTS (`muse_asr_volc.c`,
  `muse_tts_volc.c`)

Not implemented (out of scope):

- Home-network tunnel
- OTA
- Manufacturer attestation (community pairing is used instead)

## 2. BLE

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

## 3. Persistent data

The upstream setup keys are:

`ssid`, `password`, `wifi_channel`, `wifi_others`, `wifi_hidden`,
`access_token`, `refresh_token`, `vm_url`, `username`, `api_url`,
`api_url_v2`, `noise_host`, and `setup_complete`.

`config_clear_pairing` preserves Wi-Fi and device-level data.
`config_clear_setup` removes Wi-Fi, account credentials and setup completion.
The BK port uses bounded values and a commit marker written only after all
credential writes succeed.

The SDK token is not a stored device credential. It is injected at build time
and included in the encrypted `pairing_confirmed` status so the App can present
it when minting the device token. Logs may print only a short prefix.

## 4. VM API

Default API base is supplied by upstream `vm_api.h`. Relevant paths are:

- `GET /fetch_vms`, bearer access token
- `POST /device_token/mint`, bearer access token
- `POST /device_token/refresh`, bearer access token, then
  `Bearer hatch_refresh:<raw-refresh-token>` after HTTP 401

Mint and refresh bodies contain `device_id` and, when configured, `sdk_token`.
Responses provide an `access_token` and `refresh_token`. VM entries provide
`vm_ws_url` (legacy `vm_url`), `vm_auth_token`, optional `vm_id`, name and
default flag. A VM must be connected using the token from the same VM entry.

On authorization expiry the service requests a token refresh and lets the Noise
worker reconnect with the new credentials (`muse_service_request_token_refresh`
in `muse_service.c`).

## 5. Noise transport

Both the control session and the chat session use the same transport:

- Host default: `hatch.metaaivm.com` (`CONFIG_MUSE_GADGET_NOISE_HOST`)
- Path: `wss://<host>/v1/noise`
- Handshake: `Noise_XX_25519_AESGCM_SHA256`
- Transport: WebSocket over TLS, then Noise messages in binary WebSocket
  payloads
- Noise nonce: four zero bytes followed by a big-endian 64-bit counter
- Transport payloads are protobuf-like ordered chunk frames, then service
  envelopes
- Application requests (for example `POST /chat/stream`) are carried *inside*
  the Noise service stream as `ApplicationRequest` + `BodyChunk` frames +
  `end_body`; they are not raw HTTP over the WebSocket

The BK Mbed TLS backend must provide X25519, SHA-256, HMAC/HKDF-SHA256,
AES-256-GCM and secure random bytes. A failed authenticated decrypt poisons the
receive state. This SDK's normal AP build has PSA client headers but no TF-M PSA
service, so the port uses the equivalent classic Mbed TLS primitives.

Each session is owned by exactly one worker; `ClientSession` is never touched
from another task.

## 6. Control session

- Service stream: `ServiceType::Daemon`, control stream id `1`
- One `link.register` per connection, then `link.invoke` requests

`link.register` envelope (upstream shape, matched by `id`):

```json
{"type":"req","id":"<uuid>","method":"link.register","params":{
  "node_id":"<node id>",
  "display_name":"MuseGadget-XXXXXX",
  "platform":"bk7259",
  "version":"4.0.1",
  "device_family":"link",
  "model_id":"hatch_link",
  "is_wakeup_supported":false,
  "commands_v2":{ ... }
}}
```

`commands_v2` advertises exactly the commands the device implements:

| Command | Required | Optional | Timeout |
|---|---|---|---|
| `device.health` | — | — | — |
| `display.draw_url` | `url`: string | `row`: integer | 60000 ms |
| `display.show_animation` | — | — | — |
| `display.draw_text` | `text`: string | — | 10000 ms |

`link.invoke` (the production gateway spells it `device.invoke`; the device
accepts both) carries `id` and a `params` object. The command name is read from
`params.command`, then `params.name`, then the root `command`, because the
pinned daemon places it beside `method`.

Response envelope:

```json
{"jsonrpc":"2.0","id":"<id>","result":{"ok":true,"payload":{...}}}
{"jsonrpc":"2.0","id":"<id>","result":{"ok":false,"error":{"code":"...","message":"..."}}}
```

`device.health` payload:

```json
{"board":"BK7259","firmware":"4.0.1","network":{"connected":true}}
```

Unknown commands return `code":"unsupported"` with the offending name in
`message`. Missing `display.draw_text` text returns `code":"invalid_params"`.

## 7. Chat session

The voice path uses a **dedicated Noise connection**, separate from the control
session (`muse_chat.cpp`). It does **not** send `/link-control` or
`link.register`; the server scopes the conversation to this connection, so
assistant replies fan out to this connection's subscription instead of the
account's main chat.

- `POST /chat/subscribe`, body `{}`, long-lived, `Accept:
  application/x-ndjson`. The response is an NDJSON stream of
  `{"type":"event","payload":{...}}` rows. The device consumes
  `message.assistant` (reply text) and `message.user` (transcript), plus
  `delta.text_append` when present.
- `POST /chat/stream`, one per note, `Content-Type: application/json`,
  `Accept: application/json`, `x-app-id: hatch-web`. Opened right before the
  note; the 200 ACK carries `result.message_id` / `reply_to_message_id`.

Both requests are wrapped in Noise `ApplicationRequest` frames as described in
section 5.

## 8. Voice / photo note wire format

`muse_voice_proto.c` is the only file that knows the note wire format.

Voice note body:

```
NOTE_HEAD + base64( WAV(44 B, 16 kHz mono PCM16) + mic PCM ) + NOTE_TAIL
```

- Sample format: 16 kHz mono PCM16, 20 ms / 640-byte frames.
- Streaming WAV header: `RIFF`/`data` sizes are `0xFFFFFFFF` (unknown while
  streaming).
- Body chunking: 1536 PCM bytes per chunk (2048 base64 chars), chunk buffer
  2048 + 8 bytes.
- The head ends with `"data_base64":"`; the last chunk appends the JSON tail.
- `message`: text attached to the note (for example the in-band instruction to
  reply via `display.draw_text`); empty for none.
- `device_id` / `session_id`: sent in upstream Linux-SDK order when non-empty,
  so the turn is attributed to this board's conversation; both empty emits the
  legacy esp32 head shape.

Photo note: same envelope and tail, but the file item is a single JPEG. The
optional `query` lands in `message`.

## 9. Display commands

- `display.draw_url`: draw a baseline (not progressive) JPEG, shrunk 1/2, 1/4
  or 1/8 on-device and centred, or raw RGB565 (high byte first, 770 bytes per
  row). `row` overrides the vertical position. The drawable surface is the
  logical 385x320 landscape screen (the panel is 320x385 through `ROTATE_90`).
  `http://` uses the least device memory. Reply payload reports
  `format`/`width`/`height`/`scale`/`bytes`/`ms`.
- `display.show_animation`: clear any image drawn by `display.draw_url` and
  restore the normal UI.
- `display.draw_text`: UTF-8 text page. This is the assistant reply channel:
  if a voice turn is waiting, the text binds to that turn; otherwise it is a
  proactive push and the device shows and speaks it without interrupting an
  active turn (it is parked until the device is idle).

## 10. Security and credentials

- TLS/WebSocket trust anchor: PEM at `CONFIG_MUSE_GADGET_CA_PATH` (default
  `/sd0/muse_ca.pem`), loaded with `muse start` or `muse ca-load <path>`. The
  certificate is copied into RAM and never persisted as an account credential.
  VM HTTPS and WebSocket TLS deliberately fail closed when no trust anchor is
  configured.
- Speech credentials: Volcano APP ID / ASR key / TTS key are runtime-only
  (`muse volcappid|asrkey|ttskey`), persisted to easyflash, and never baked
  into the build.
- SDK token: build-time only (`CONFIG_MUSE_GADGET_SDK_TOKEN`), used for
  encrypted pairing and token mint/refresh; never printed in full.

## 11. Reconnect and observability

- The control and chat workers reconnect with backoff. Transient failures
  (transport error, 502 while the VM is still waking) keep the UI in
  "connecting"; only non-self-healing failures (auth, TLS verify, no memory)
  surface as a hard error.
- The chat worker can be asked to rebuild the session when the link is healthy
  but the reply never arrives (the server delivered it to a stale session):
  `muse_chat_request_reconnect`.
- Both sessions log an RX-idle probe every 30 s (time since the last business
  downlink). This measures the server's idle heartbeat and is the basis for a
  future dead-link threshold.
