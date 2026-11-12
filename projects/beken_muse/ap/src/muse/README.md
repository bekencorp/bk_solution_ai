# Muse Gadget integration status

The code in this directory is guarded by `CONFIG_MUSE_GADGET` and is disabled
by default.

Implemented:

- frozen upstream protocol contract and UUIDs
- isolated lifecycle worker and pairing generation/state ownership
- bounded credential storage with a last-written setup marker
- minimum `link.register` and `device.health` JSON handling
- headless CLI and S4 confirmation routing
- pinned upstream Noise core with BK PSA/Mbed TLS backend
- bounded HTTPS VM API client and reconnecting Noise WebSocket session
- BK GAP/GATTS pairing-v5 service with encrypted community pairing records
- explicit handoff from the existing BLE provisioning owner
- optional pairing-v5 Wi-Fi provisioning and restart restore
- device-token refresh on VM API authorization expiry

Before starting the service, place the PEM trust anchor at
`CONFIG_MUSE_GADGET_CA_PATH` (default `/sd0/muse_ca.pem`) or load another path
with `muse ca-load <path>`. The certificate is copied into RAM and is never
persisted as an account credential. VM HTTPS and WebSocket TLS deliberately
fail closed when no trust anchor is configured.

The feature is ready for hardware interoperability testing, but remains
disabled by default. Do not ship with `CONFIG_MUSE_GADGET=y` until pairing with
the real Muse App, cloud reconnect, reboot persistence, BLE callback ownership,
and BK7259 memory use have been validated on target.
