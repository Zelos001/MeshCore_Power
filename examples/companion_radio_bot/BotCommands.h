#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/ContactInfo.h>

/* ----------------------------------------------------------------------------
 *  BotCommands
 *
 *  Built-in "debug bot" for the BLE Companion firmware. When another node on
 *  the mesh sends this node a direct text message beginning with the command
 *  prefix ('!' by default), the bot generates a reply and sends it back over
 *  the mesh - in addition to the companion's normal behaviour of forwarding
 *  the incoming message to the connected phone app.
 *
 *  All of the logic lives in examples/companion_radio_bot/ (hooked in via the
 *  BotMesh subclass) so normal companion builds are completely unaffected.
 *  BotCommands only uses MyMesh's public API (sendMessage, getNodePrefs,
 *  getRecentlyHeard, advert...), so it stays decoupled from the internals.
 *
 *  Commands:
 *    !ping            - the requester's signal into us (SNR/RSSI/hops)
 *    !rf              - SNR + RSSI of the received request packet
 *    !path            - route/hops the request arrived on (flood hashes)
 *    !status          - node name, freq/SF/BW/CR, TX power, heap, contacts
 *    !stats           - packet RX/TX counts + airtime (mesh health)
 *    !neighbors       - recently-heard nodes (alias: !seen)
 *    !uptime          - time since boot
 *    !time            - node clock (UTC) if the RTC is set
 *    !ver             - firmware version + build date
 *
 *  Runtime configuration is NOT done over the mesh - it's set locally via the
 *  companion protocol's custom vars ("bot", "bot.channels"), e.g. with
 *  meshcore-cli, and persisted to flash. See BotConfig.h.
 * ------------------------------------------------------------------------- */

#ifndef BOT_CMD_PREFIX
#define BOT_CMD_PREFIX '!'
#endif

// Forward the incoming command message to the connected phone app as well
// (1 = phone still sees the "!cmd" message, 0 = suppress it from the app).
#ifndef BOT_FORWARD_TO_APP
#define BOT_FORWARD_TO_APP 1
#endif

// --- Rate limiting (anti-abuse: a flood of requests must not make us hammer
//     the airwaves). All times in milliseconds / counts per window. ---

// Minimum gap between ANY two bot replies, regardless of sender.
#ifndef BOT_MIN_REPLY_INTERVAL_MS
#define BOT_MIN_REPLY_INTERVAL_MS 1200
#endif

// Per-sender cooldown: ignore repeat commands from the same node within this.
#ifndef BOT_SENDER_COOLDOWN_MS
#define BOT_SENDER_COOLDOWN_MS 5000
#endif

// Hard cap on replies emitted within any rolling 60s window.
#ifndef BOT_MAX_REPLIES_PER_MIN
#define BOT_MAX_REPLIES_PER_MIN 20
#endif

#ifndef BOT_SENDER_TABLE_SIZE
#define BOT_SENDER_TABLE_SIZE 8
#endif

class MyMesh;

class BotCommands {
public:
  BotCommands() { reset(); }

  // Records boot time; call once from BotMesh::begin().
  void begin();

  // Handle a freshly-received direct text message. Returns true if the text was
  // a bot command (recognised or not, including ones dropped by rate limiting),
  // so the caller knows it was "consumed" by the bot.
  bool handle(MyMesh& mesh, const ContactInfo& from, mesh::Packet* pkt, const char* text);

  // Handle a text message posted to a group channel. Replies on the same
  // channel if the channel is on the configured allow-list (the bot.channels
  // custom var) and the text is a bot command. Returns true if it was a
  // command on an allowed channel.
  bool handleChannel(MyMesh& mesh, const mesh::GroupChannel& channel, mesh::Packet* pkt,
                     uint32_t timestamp, const char* text);

private:
  void reset();
  bool rateLimitOk(const uint8_t* key, unsigned long now);
  static bool channelAllowed(const char* name);

  // Builds the reply text for a parsed command into `out` (capacity out_sz).
  // `requester` is the sender's display name ("" if unknown).
  // Returns the length written, or 0 if nothing should be sent.
  int buildReply(MyMesh& mesh, mesh::Packet* pkt, const char* requester,
                 const char* cmd, const char* args, char* out, size_t out_sz);

  unsigned long _boot_millis;

  struct SenderSlot {
    uint8_t  prefix[4];   // first 4 bytes of sender public key (0 = empty)
    unsigned long last_ms;
  };
  SenderSlot _senders[BOT_SENDER_TABLE_SIZE];

  unsigned long _last_reply_ms;     // for global min-interval
  unsigned long _window_start_ms;   // start of the current 60s window
  uint16_t      _window_count;      // replies emitted in current window
};
