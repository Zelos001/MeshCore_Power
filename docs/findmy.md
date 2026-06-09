# FindMy / OpenHaystack Beacon

This document describes the experimental FindMy locator beacon for nRF52 MeshCore nodes.

## Purpose

When enabled, a node advertises an Apple [FindMy](https://support.apple.com/find-my) /
[OpenHaystack](https://github.com/seemoo-lab/openhaystack) "offline finding" beacon alongside
its normal mesh duties. Nearby iPhones anonymously relay its encrypted location to Apple's
servers, letting you locate a deployed node through the global Find My network — no extra
infrastructure, GPS, or cellular needed.

This is particularly useful for repeater nodes. To maximise coverage they are often deployed in
exposed, unattended, hard-to-reach spots — rooftops, towers, hilltops, remote solar sites — which
makes them especially prone to going missing, whether through theft, weather, a failed mount, or
simply being forgotten. A FindMy beacon gives you a way to locate (or recover) such a node, or at
least get a last-known position, without having to physically visit the site.

The node only ever broadcasts a **public** advertising key. Decrypting the location reports
requires the matching **private** key, which you keep off-device on your own machine.

## Supported hardware

nRF52840 boards only (Adafruit Bluefruit BLE stack),
but the implementation is easily ported to ESP32 and other BLE capable chips.
The challenge is with concurrent BLE usage, ie. companion nodes. **TODO/Future work** check if
ble adverisements can be sent when the companion is disconnected from the phone,
and disable when reconnected.

Pre-defined build environments:

| Environment | Board |
|-------------|-------|
| `RAK_4631_repeater_findmy` | RAK4631 / RAK WisMesh (repeater, incl. Solar Repeater Mini) |
| `LilyGo_T-Echo_repeater_findmy` | LilyGo T-Echo |

The feature is gated behind the `WITH_FINDMY_BEACON` build flag, so it adds nothing to other
builds. It is intended for always-on roles (repeater/sensor) where BLE is otherwise unused. It
is **not** for the phone-companion firmware, which needs BLE for its own link — a node can be a
FindMy beacon or a phone companion, not both at once.

## Generating keys

Keys are generated off-device with the OpenHaystack / macless-haystack tooling — see
[macless-haystack](https://github.com/dchristl/macless-haystack) (or the original
[OpenHaystack](https://github.com/seemoo-lab/openhaystack)). Its `generate_keys.py` produces
three values per key; use them as follows:

| Value | Where it goes | Purpose |
|-------|---------------|---------|
| **Advertisement key** (public, 28 bytes) | the **node** (`set findmy.key`) | broadcast in the BLE advert + MAC |
| **Private key** | your **server only** | decrypts the location reports |
| **Hashed adv key** | your **server** (lookup id) | used to query Apple for reports |

Only the advertisement (public) key goes on the node. Never load the private key onto the
device — it would be broadcast publicly and reports would not decrypt. (Note: the private key is
also 28 bytes, so the firmware cannot tell them apart by length — pick the value labelled
*advertisement*.)

## Node configuration

Configure over the local USB serial console (115200 baud) or remotely from the MeshCore app's
Command Line tab when logged in as admin. Configuration is stored in `/findmy` on the node's
internal filesystem (independent of the normal node preferences).

```
set findmy.key <base64-advertisement-key>   ->  OK - reboot to apply
set findmy on                                ->  OK - on, reboot to apply
get findmy                                   ->  > on, mac DF:41:B5:D7:F3:BF
reboot
```

- `set findmy.key` must decode to exactly 28 bytes, otherwise it reports
  `Error: decoded N bytes, expected 28`.
- `get findmy` shows the enabled state and the derived BLE MAC (it never echoes the key). The
  MAC is `key[0]|0xC0 : key[1] : key[2] : key[3] : key[4] : key[5]`.
- The beacon starts only at boot, so `reboot` (or a power cycle) is required to apply changes.
  On boot the node prints `FindMy beacon started`.
- `set findmy off` then `reboot` stops advertising (the stored key is kept).

### Verifying

1. **On air:** scan with a BLE tool (e.g. nRF Connect). You should see a non-connectable
   advertiser at the address `get findmy` reported (address type *Random*) carrying Apple
   manufacturer data beginning `4C 00 12 19 …`.
2. **Key sanity:** run heystack's `tools/showmac.py` on the advertisement key you loaded — it
   must print the same MAC as `get findmy`. A mismatch means the wrong key was loaded.

## Retrieving location data

Use your own server with the **private** key — the node cannot retrieve anything itself. With
macless-haystack, query and decrypt reports using its scripts, e.g. `fetch_reports.py` (a
`findmy.py`-style query), which authenticates to Apple (via Anisette), pulls the encrypted
reports for the hashed adv key, and decrypts them with the private key. See the
[macless-haystack](https://github.com/dchristl/macless-haystack) README for setup
(Anisette/Apple-ID requirements) and exact invocation.

## Notes and caveats

- **Latency:** Find My is deliberately latency-tolerant. Reports appear only after the node is
  near a passing iPhone and can take minutes to a few hours.
- **Power:** continuous BLE advertising keeps the SoftDevice awake, so idle current is higher
  than a node with BLE off. The advertising interval defaults to ~2 s; override with
  `-D FINDMY_ADV_INTERVAL=<0.625ms-units>`.
- **Privacy:** the key is static (no rotation), so anyone holding it can track the node.
- **Static key may be rate-limited / blocked (TODO):** genuine AirTags rotate their advertising
  key roughly daily, and Apple's network is tuned for that. A single never-changing key can be
  treated as anomalous and may have its reports throttled or dropped over time, so long-term
  reliability is not guaranteed. **Future work:** add key rotation — store a sequence of keys and
  advance the index on a daily schedule (matching AirTag behaviour), and extend the key generator
  to emit a batch of keys with daily-incrementing indices that the server side can resolve.
- **OTA:** the beacon coexists with `start ota` — triggering a firmware update reuses the
  running BLE stack and switches to the DFU advertiser automatically.
