# Companion Radio + Built-in Debug Bot

This is the standard MeshCore **BLE Companion** firmware with a built-in
"debug bot". When the node receives a **direct text message** over the
LoRa mesh whose text begins with `!`, it auto-replies over the mesh with useful
debugging info — while still behaving as a normal companion (the incoming
message is still delivered to the phone app over BLE).

Supported boards (PlatformIO env names):

| Board | Env |
|-------|-----|
| Seeed XIAO ESP32-S3 + Wio-SX1262 | `Xiao_S3_WIO_companion_radio_ble_bot` |
| Seeed XIAO nRF52840 + Wio-SX1262 | `Xiao_nrf52_companion_radio_ble_bot` |
| Heltec LoRa32 V3                 | `Heltec_v3_companion_radio_ble_bot` |
| Heltec LoRa32 V4                 | `heltec_v4_companion_radio_ble_bot` |
| RAK4631 (WisBlock)               | `RAK_4631_companion_radio_ble_bot` |
| RAK WisMesh 1W Booster (RAK3401 + RAK13302) | `RAK_3401_companion_radio_ble_bot` |
| Seeed T1000-E Card Tracker       | `t1000e_companion_radio_ble_bot` |
| Heltec T114                      | `Heltec_t114_companion_radio_ble_bot` |

Adding another board is ~12 lines of `platformio.ini` (see
`variants/bot_envs/platformio.ini`) — all bot code is board-agnostic.

## Commands

Send any of these as a normal direct message to the bot node from another
MeshCore client:

| Command        | Reply |
|----------------|-------|
| `!ping`        | Reports the requester's signal into us: `<name>: heard you at SNR …, RSSI …, N hop(s)` |
| `!rf`          | SNR + RSSI of the received request packet |
| `!path`        | Route the request arrived on, with hop hashes resolved to contact names: `flood, N hop(s): AA(RptAlpha) BB(?) …` or `direct route` |
| `!status`      | name, freq/SF/BW/CR, TX power, contact count, free heap |
| `!stats`       | Packet RX/TX counts (flood/direct) + TX/RX airtime — mesh-health snapshot |
| `!neighbors`   | Recently-heard nodes + hop counts (alias `!seen`) |
| `!uptime`      | Time since boot (`2d 4h 13m 7s`) |
| `!time`        | Node clock (UTC epoch) if the RTC is set |
| `!ver`         | Firmware version + build date |

`!path` and `!rf` describe the **request packet as this node received it**,
which is exactly what you want when probing a path through the mesh. `!path`
resolves each hop's hash against this node's contact list; hops it doesn't
have a contact for show as `(?)`.

Unrecognised `!` commands are silently ignored (no reply), so typos and other
bots' command prefixes don't generate mesh traffic.

## Where it listens

- **Direct messages:** the bot replies to a `!`-command sent to it as a direct
  message from any contact (subject to rate limiting).
- **Group channels:** the bot also replies to `!`-commands posted in an
  **allow-listed** channel, posting the reply back to the **same channel**.
  Channels not on the list (e.g. `Public`) are ignored, so the bot can't spam
  busy channels. The allow-list is **empty by default** (DM-only) and is
  configured at runtime — see below. Replies never start with `!`, so they
  cannot re-trigger the bot.

## Runtime configuration

The bot is configured **locally** over the companion protocol's custom-vars
frames — from meshcore-cli or any client that exposes custom variables — and
NOT via mesh messages, so only the paired/local user can change it. Settings
persist to flash (`/bot_prefs`) and survive reboots.

| Var | Values | Meaning |
|-----|--------|---------|
| `bot`          | `1` / `0` (also `on`/`off`) | Enable/disable all bot replies |
| `bot.channels` | `#name[,#name...]` or `none` | Channel allow-list (default: `none`, DM-only) |

With [meshcore-cli](https://github.com/meshcore-dev/meshcore-cli) (unknown
`set`/`get` names fall through to custom vars):

```sh
meshcore-cli -d "XiaoS3 Bot" set bot 0                       # mute the bot
meshcore-cli -d "XiaoS3 Bot" set bot 1                       # re-enable it
meshcore-cli -d "XiaoS3 Bot" set bot.channels "#test,#bots"  # allow channels
meshcore-cli -d "XiaoS3 Bot" set bot.channels none           # DM-only again
meshcore-cli -d "XiaoS3 Bot" get custom                      # show all vars
```

Channel names must match the stored channel name exactly, including the
leading `#`, and the node must have those channels added
(key = `SHA256(name)[:16]`).

## How it works

This is a **separate firmware type** that layers on top of the stock companion
example without modifying it. The upstream tree (`examples/companion_radio/`,
`variants/<board>/`) is untouched, so pulling from upstream never conflicts
with the bot. Everything lives in `examples/companion_radio_bot/` plus one
env file holding all the board envs (`variants/bot_envs/platformio.ini`):

- `BotCommands.{h,cpp}` — all the bot logic (commands, rate limiting).
- `BotConfig.{h,cpp}` — the persisted runtime settings (`/bot_prefs` on the
  node's filesystem): enabled flag + channel allow-list.
- `BotSensorManager.h` / `BotTarget.cpp` — expose those settings as the
  `bot` / `bot.channels` custom vars. `BotTarget.cpp` compiles the board's
  stock `target.cpp` with the global `sensors` object swapped for a wrapper
  subclass that adds the two settings to the custom-vars get/set hooks.
- `BotMyMesh.cpp` — compiles the stock `MyMesh.cpp` with uses of `sensors`
  routed through an opaque reference accessor. Necessary because the
  compiler devirtualizes calls on a global object of known type, which
  would silently bypass the wrapper's overrides (it did, until this shim).
- `BotMesh.h` — a `MyMesh` subclass; the only coupling point. It overrides the
  two virtual receive callbacks (`onMessageRecv`, `onChannelMessageRecv`) to
  hand messages to the bot, then delegates to the stock behaviour, and
  shadows `begin()` to start the bot's uptime clock.
- `main.cpp` — a shim that `#include`s the stock companion `main.cpp` verbatim
  (no copy to keep in sync), with `MyMesh` renamed to `BotMesh` so the global
  mesh object is our subclass. Because `MyMesh.h` declares
  `extern MyMesh the_mesh;`, the bot instance is named `the_bot_mesh` and the
  env links it back under the original symbol with
  `-Wl,--defsym,the_mesh=the_bot_mesh` (other translation units like
  `UITask.cpp` resolve against it normally).

The bot uses only `MyMesh`'s **public** API (`sendMessage`, `getNodePrefs`,
`getRecentlyHeard`, `advert`, `getRTCClock`), so it stays decoupled. Stock
companion builds are completely unaffected — they never compile these files.

### Abuse protection (rate limiting)

A node receiving a flood of `!` messages must not be tricked into hammering the
airwaves. `BotCommands` enforces three limits (all overridable via `-D`):

| Define | Default | Meaning |
|--------|---------|---------|
| `BOT_MIN_REPLY_INTERVAL_MS` | `1200` | Min gap between any two bot replies |
| `BOT_SENDER_COOLDOWN_MS`    | `5000` | Per-sender cooldown |
| `BOT_MAX_REPLIES_PER_MIN`   | `20`   | Hard cap on replies per rolling 60 s |

Dropped requests are silently ignored (no reply), but are still forwarded to the
phone app.

### Other build options

| Define | Default | Meaning |
|--------|---------|---------|
| `BOT_CMD_PREFIX`     | `'!'` | Command prefix character |
| `BOT_FORWARD_TO_APP` | `1`   | `0` = hide bot commands from the phone app |

## Build & flash

Use the env for your board from the table at the top, e.g.:

```sh
# build
pio run -e Xiao_S3_WIO_companion_radio_ble_bot

# build + flash (board on USB)
pio run -e Xiao_S3_WIO_companion_radio_ble_bot -t upload

# serial monitor
pio device monitor -b 115200
```

nRF52 boards (RAK4631, T1000-E, T114) also produce a `firmware.uf2` in
`.pio/build/<env>/` for drag-and-drop flashing via the UF2 bootloader.

Default BLE pairing PIN is `123456` (`BLE_PIN_CODE`). Pair with any MeshCore
client (phone app, web client) as you would the normal companion firmware.
