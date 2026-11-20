# BK7259 Muse Gadget Example Project

- [中文](./README_CN.md)

## 1. Introduction

`beken_muse` is a standalone Muse Gadget product project for BK7259. Based on the protocol of [`facebookincubator/muse-gadget-sdk`](https://github.com/facebookincubator/muse-gadget-sdk) (pinned commit `812c46f9fe42b69d916613a9dc702cbac5ff7b54`), it implements the complete "AI voice companion" chain:

BLE pairing / Wi-Fi provisioning → Noise encrypted session → wake word and hands-free voice → cloud understanding and reply → on-screen text + voice playback.

The project is trimmed from the `beken_robot` application base, keeping only the UI, audio, and demos required by Muse. It builds independently and does not depend on `secureboot_ai`.

- Protocol contract (implemented scope, commands, voice wire format): [docs/muse_protocol_contract.md](docs/muse_protocol_contract.md)
- Voice design: [docs/muse_voice_design.md](docs/muse_voice_design.md)

## 2. Main Features

- **BLE community pairing v5**: P-256 ECDH + HKDF-SHA256 + AES-256-GCM; transports Wi-Fi / account info in encrypted records; physical button (S4) confirmation.
- **Noise encrypted session**: `Noise_XX_25519_AESGCM_SHA256` over WSS; the control session handles `link.register` / `link.invoke`, and a separate chat session handles `POST /chat/stream` + `POST /chat/subscribe`.
- **Hands-free voice turn**: KWS wake word + VAD auto end-of-speech; the recording is uploaded as a voice note.
- **Optional Volcano voice**: streaming ASR (`CONFIG_MUSE_ASR_VOLC`) and streaming TTS (`CONFIG_MUSE_TTS_VOLC`).
- **LVGL voice UI**: `/sd0/jolly/{a,b,question}.mp4` background videos + reply bubbles + pairing/connection status page; shows "Connecting…" in place during boot/reconnect.
- **Cloud display commands**: `display.draw_url`, `display.draw_text`, `display.show_animation`.
- **SD-NAND resources**: KWS models, prompt tones, video assets, TTS output.

## 3. Main Configuration

- **Target chip**: BK7259, build target `bk7259`.
- **Display and input**: LVGL v9, CST9217 touch panel, S2–S5 physical buttons.
- **SD-NAND**: onboard SD-NAND on **SDIO1** (GPIO14–GPIO19), power gate GPIO53; FatFS drive `1:`, VFS mount point `/sd0`.
- **Type-C**: UART log / USB mass-storage mode switch (GPIO54).
- **Audio**: microphone + speaker; voice turns use 16 kHz mono PCM16.
- **Muse switches** (`menuconfig`; enabled by default in this project's defconfig):

  ```text
  CONFIG_MUSE_GADGET=y
  CONFIG_MUSE_GADGET_VOICE=y
  CONFIG_MUSE_ASR_VOLC=y
  CONFIG_MUSE_TTS_VOLC=y
  CONFIG_BEKEN_KWS=y
  CONFIG_BOARD_SD_NAND_ENABLE=y
  ```

## 4. Build

Install the Armino SMP build environment and prepare a `bk_avdk_smp` SDK whose version matches the solution code.

```bash
cd ~/armino/bk_solution_ai/projects/beken_muse
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

Clean rebuild:

```bash
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

You can also use the repository Docker build script:

```bash
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

The flashable image after a successful build:

```text
build/bk7259/beken_muse/package/all-app.bin
```

For flashing and build-environment setup, see the [English guide](../../README.md) in the repository root.

## 5. Preparation

1. Build and flash `all-app.bin`, and connect the LCD, touch panel, microphone, and speaker.
2. Copy the **contents** of the `resources` directory to the SD-NAND root (do not add an extra `resources` level):

   ```text
   /sd0/
   ├── kws_model/
   │   ├── bk_kws_wakeup.tflite
   │   └── bk_kws_commands.tflite
   ├── asr_wakeup_16k_mono_16bit_en.mp3
   ├── asr_standby_16k_mono_16bit_en.mp3
   └── ...other prompt tones
   ```

   See the [resource guide](resources/kws_model_and_prompt_tone_user_mannual.md) for the required file names and directory layout of the KWS models and prompt tones.

3. Add the Jolly video assets (H.264, no audio track): `a.mp4` (voice input), `b.mp4` (reply bubble, 320x320), `question.mp4` (waiting for reply, 384x320):

   ```text
   /sd0/jolly/a.mp4
   /sd0/jolly/b.mp4
   /sd0/jolly/question.mp4
   ```

   If an asset is missing, the page falls back to a plain text layout.

4. Add the Muse TLS trust anchor (PEM). Default path:

   ```text
   /sd0/muse_ca.pem
   ```

   You can also specify it at runtime with `muse ca-load <path>`. If no trust anchor is configured, TLS fails closed (it does not fall back to no verification).

5. Configure the Volcano voice credentials (runtime only; written to easyflash and not included in the firmware):

   ```text
   muse volcappid <APP-ID>
   muse asrkey <Access-Token>
   muse ttskey <Access-Token>
   # Optional: override the default resource / voice
   muse asrres <resource-id>
   muse ttsvoice <speaker-id>
   ```

   | Command | Argument | Required | Default | Description |
   |---|---|---|---|---|
   | `muse volcappid` | Volcano APP ID | When ASR/TTS is enabled | — | Shared by ASR and TTS |
   | `muse asrkey` | ASR Access Token | When ASR is enabled | — | The ASR module stays disabled if unset |
   | `muse ttskey` | TTS Access Token | When TTS is enabled | — | The TTS module stays disabled if unset |
   | `muse asrres` | ASR resource ID | No | `volc.bigasr.sauc.duration` | Use `volc.seedasr.sauc.duration` for 2.0; concurrent editions use `*.concurrent` |
   | `muse ttsvoice` | TTS voice ID | No | `zh_female_xiaohe_uranus_bigtts` | Must match the TTS resource generation, otherwise a 403 is returned |

   Notes:

   - Credentials are loaded when the voice module initializes; after a change, **reboot the device** (or reinitialize voice) for it to take effect.
   - `asrres` / `ttsvoice` defaults can also be changed in `menuconfig`: `CONFIG_MUSE_ASR_VOLC_RESOURCE_ID`, `CONFIG_MUSE_TTS_VOLC_RESOURCE_ID`, `CONFIG_MUSE_TTS_VOLC_VOICE`.
   - An empty argument prints the corresponding `usage:`; a write failure returns `ERROR`.
   - `muse status` only shows pairing / network / Noise state, not whether ASR/TTS is available; check the serial log of the first voice turn.

6. Complete BLE pairing and Wi-Fi provisioning with the Muse App; see the next section.

## 6. Muse App Provisioning Steps

> These steps are adapted from the README of Meta's official `muse-gadget-sdk` (Apache-2.0), rewritten for BK7259's buttons and on-screen prompts; they are not copied verbatim.

1. **Prepare the SDK token**
   Generate an SDK token (`mgst_…`) at <https://gadgets.muse.ai/settings/sdk-tokens>, and read the Gadget SDK Terms first. The token is used at build time as `CONFIG_MUSE_GADGET_SDK_TOKEN`; it does not need to be entered in the App.

2. **Enable Developer mode**
   In the Muse App (iOS / Android), go to `Settings > Devices` and enable **Developer mode**. Without it, community devices do not appear in the device list.

3. **Add the device**
   Tap **+** in the top-right corner (`Settings > Devices > Add Device`) and select the device advertising as `MuseGadget-XXXXXX`. The device status page shows **"Waiting for pairing"** with the prompt "Open the Muse App to search for and pair this device".

4. **Confirm pairing on the device**
   After the App starts pairing, the status page changes to **"Press S4 to confirm pairing"** with the prompt "Confirm in the App, then press S4 to finish pairing". Short-press **S4** to confirm (or use the CLI `muse confirm`).
   - Pairing must be explicitly confirmed by the user on the device; the App cannot complete it alone.
   - community pairing v5 has no manufacturer attestation and cannot prevent active MITM; provision on a trusted network.

5. **Deliver Wi-Fi and account info**
   After confirmation, the App proceeds to Wi-Fi selection/input (SSID, password, hidden network). After submission, the device status page shows **"Waiting for network"** ("Waiting for Wi-Fi connection…"). If it does not complete within 120 seconds, the pairing session times out and returns to "Waiting for pairing"; you can retry.

6. **Connect to Muse**
   Once Wi-Fi is ready, the device shows **"Connecting"** ("Connecting to the cloud…"). After device token mint/refresh and the Noise handshake complete, it changes to **"Connected"** and automatically enters the voice page. After that, just talk hands-free; no button is needed.

7. **Re-provision / clear pairing**
   - Tap **"Clear pairing"** on the status page or run the CLI `muse clear` (`config_clear_setup`, which clears both Wi-Fi and account credentials).
   - To change only Wi-Fi while keeping the account, use `config_clear_pairing` (keeps Wi-Fi and device-level data); see Section 3 of the protocol contract for details.

### Provisioning FAQ

- **The device does not appear in the App list**: make sure Developer mode is enabled; make sure the App version supports community pairing v5; use `muse status` to confirm the device is "Waiting for pairing".
- **Stuck at "Waiting for network"**: check the SSID / password, 2.4 GHz band, and signal strength; hidden networks must be entered manually in the App.
- **Stuck at "Connecting"**: make sure `/sd0/muse_ca.pem` is present and not expired; check the serial log for TLS / Noise errors.

## 7. Running and Operation

- On boot the project automatically calls `muse_service_start()` to start the Muse service; you can also run `muse start` manually from the serial CLI.
- **Provisioned device**: boots directly into the voice page (`a.mp4`); shows "Connecting…" while connecting, and automatically enters hands-free voice turns once connected.
- **Unprovisioned device**: stays on the Muse status page waiting for pairing.

### Touchscreen

- Tap to enter; lists can be scrolled vertically; swipe right from the left edge to go back.

### Common CLI

```text
# Service and pairing
muse start | stop | status | confirm | clear | newid
muse ca-load [path]          # load the TLS trust anchor
muse token <mgst_...>        # write the SDK token at runtime

# Volcano ASR / TTS (see "5. Preparation" for parameters)
muse volcappid <id> | asrkey <key> | ttskey <key>
muse asrres <rid> | ttsvoice <spk>

# Voice and debugging
muse voice [start|stop|toggle]
muse photo                   # capture and upload on the camera page
muse probe [seconds]         # TLS uplink probe to the Noise host, default 10s
muse register-json | mem | time
```

`muse status` output: `state=… paired=… wifi=… noise=… error=…`.

## 8. Related Documents

- [Muse protocol contract](docs/muse_protocol_contract.md): implemented scope, control/chat sessions, command table, voice/image note wire format, security, and reconnection.
- [Muse voice design](docs/muse_voice_design.md): voice turn state machine and the original design rationale.
- [KWS model and prompt tone guide](resources/kws_model_and_prompt_tone_user_mannual.md)

## 9. Notes

- `CONFIG_MUSE_GADGET_SDK_TOKEN` is only used at build time for encrypted pairing and token mint/refresh; do not commit real tokens, and logs print only a short prefix.
- Voice credentials and CA certificates are treated as runtime configuration and are not baked into the firmware.
- If the KWS models, prompt tones, or Jolly videos are missing, the corresponding capability degrades (KWS initialization fails or the page falls back to a text layout) without affecting the rest of the functionality.
