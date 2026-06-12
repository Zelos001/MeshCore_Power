#include "BotCommands.h"
#include "BotConfig.h"
#include "MyMesh.h"      // full MyMesh definition + target.h (radio_driver, sensors)

#include <string.h>
#include <stdio.h>

void BotCommands::reset() {
  _boot_millis = 0;
  _last_reply_ms = 0;
  _window_start_ms = 0;
  _window_count = 0;
  memset(_senders, 0, sizeof(_senders));
}

void BotCommands::begin() {
  _boot_millis = millis();
  _window_start_ms = _boot_millis;
}

// ---- Rate limiting ----------------------------------------------------------

bool BotCommands::rateLimitOk(const uint8_t* pub_key, unsigned long now) {
  // Rolling 60s global window cap.
  if (now - _window_start_ms >= 60000UL) {
    _window_start_ms = now;
    _window_count = 0;
  }
  if (_window_count >= BOT_MAX_REPLIES_PER_MIN) return false;

  // Global minimum gap between any two replies.
  if (_last_reply_ms != 0 && (now - _last_reply_ms) < BOT_MIN_REPLY_INTERVAL_MS) return false;

  // Per-sender cooldown. Find existing slot, or the oldest/empty slot to reuse.
  int slot = -1, oldest = 0;
  for (int i = 0; i < BOT_SENDER_TABLE_SIZE; i++) {
    if (memcmp(_senders[i].prefix, pub_key, 4) == 0 &&
        (_senders[i].prefix[0] | _senders[i].prefix[1] | _senders[i].prefix[2] | _senders[i].prefix[3])) {
      slot = i;
      break;
    }
    if (_senders[i].last_ms < _senders[oldest].last_ms) oldest = i;
  }
  if (slot >= 0) {
    if ((now - _senders[slot].last_ms) < BOT_SENDER_COOLDOWN_MS) return false;
  } else {
    slot = oldest;
    memcpy(_senders[slot].prefix, pub_key, 4);
  }
  _senders[slot].last_ms = now;
  return true;
}

// ---- Helpers ----------------------------------------------------------------

static void fmtUptime(char* out, size_t out_sz, uint32_t secs) {
  uint32_t d = secs / 86400; secs %= 86400;
  uint32_t h = secs / 3600;  secs %= 3600;
  uint32_t m = secs / 60;    secs %= 60;
  if (d > 0) {
    snprintf(out, out_sz, "%lud %luh %lum %lus", (unsigned long)d, (unsigned long)h,
             (unsigned long)m, (unsigned long)secs);
  } else if (h > 0) {
    snprintf(out, out_sz, "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)secs);
  } else {
    snprintf(out, out_sz, "%lum %lus", (unsigned long)m, (unsigned long)secs);
  }
}

// Resolve a flood-path repeater hash (first hsize bytes of its public key) to a
// contact name. Returns false if no known contact matches.
static bool lookupNameByHash(MyMesh& mesh, const uint8_t* hash, uint8_t hsize,
                             char* name, size_t name_sz) {
  ContactsIterator it;
  ContactInfo ci;
  while (it.hasNext(&mesh, ci)) {
    if (memcmp(ci.id.pub_key, hash, hsize) == 0) {
      snprintf(name, name_sz, "%s", ci.name);
      return true;
    }
  }
  return false;
}

// ---- Command dispatch -------------------------------------------------------

int BotCommands::buildReply(MyMesh& mesh, mesh::Packet* pkt, const char* requester,
                            const char* cmd, const char* args, char* out, size_t out_sz) {
  NodePrefs* p = mesh.getNodePrefs();

  if (!strcasecmp(cmd, "ping")) {
    // Report the REQUESTER's signal into us - the key range-testing metric.
    const char* who = (requester && requester[0]) ? requester : "you";
    return snprintf(out, out_sz, "%s: heard you at SNR %.1fdB, RSSI %ddBm, %u hop(s)",
                    who, pkt->getSNR(), (int)radio_driver.getLastRSSI(),
                    (unsigned)(pkt->isRouteFlood() ? pkt->getPathHashCount() : 0));
  }

  if (!strcasecmp(cmd, "uptime")) {
    char up[40];
    fmtUptime(up, sizeof(up), (uint32_t)((millis() - _boot_millis) / 1000UL));
    return snprintf(out, out_sz, "up %s", up);
  }

  if (!strcasecmp(cmd, "path")) {
    if (pkt->isRouteFlood()) {
      uint8_t hops  = pkt->getPathHashCount();   // actual number of repeater hashes
      uint8_t hsize = pkt->getPathHashSize();    // bytes per hash
      uint16_t nbytes = pkt->getPathByteLen();   // hops * hsize
      if (nbytes > MAX_PATH_SIZE) nbytes = MAX_PATH_SIZE;
      int w = snprintf(out, out_sz, "flood, %u hop(s)", (unsigned)hops);
      if (hsize > 0 && nbytes >= hsize) {
        w += snprintf(out + w, out_sz - w, ":");
        uint16_t shown = 0;
        // per hop: " HASH(name)" - name resolved from contacts, "?" if unknown
        for (uint16_t h = 0; (uint16_t)((h + 1) * hsize) <= nbytes; h++) {
          if ((size_t)w + hsize * 2 + 18 >= out_sz) break;  // out of reply space
          const uint8_t* hash = pkt->path + h * hsize;
          w += snprintf(out + w, out_sz - w, " ");
          for (uint8_t b = 0; b < hsize; b++)
            w += snprintf(out + w, out_sz - w, "%02X", hash[b]);
          char name[14];
          if (lookupNameByHash(mesh, hash, hsize, name, sizeof(name)))
            w += snprintf(out + w, out_sz - w, "(%s)", name);
          else
            w += snprintf(out + w, out_sz - w, "(?)");
          shown++;
        }
        if (shown * hsize < nbytes) w += snprintf(out + w, out_sz - w, " ...");
      }
      return w;
    }
    return snprintf(out, out_sz, "direct route");
  }

  if (!strcasecmp(cmd, "rf")) {
    // Signal quality of the request packet as this node received it.
    return snprintf(out, out_sz, "SNR %.2f dB, RSSI %d dBm",
                    pkt->getSNR(), (int)radio_driver.getLastRSSI());
  }

  if (!strcasecmp(cmd, "status")) {
    int w = snprintf(out, out_sz, "%s | %.3fMHz SF%u BW%.0f CR%u | TX%ddBm | contacts:%d",
                     p->node_name, p->freq, p->sf, p->bw, p->cr,
                     (int)p->tx_power_dbm, mesh.getNumContacts());
#ifdef ESP32
    w += snprintf(out + w, out_sz - w, " | heap:%uK", (unsigned)(ESP.getFreeHeap() / 1024));
#endif
    return w;
  }

  if (!strcasecmp(cmd, "ver")) {
    return snprintf(out, out_sz, "MeshCore companion-bot %s (%s)", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
  }

  if (!strcasecmp(cmd, "time")) {
    uint32_t t = mesh.getRTCClock()->getCurrentTime();
    if (t < 1700000000UL) {  // RTC not set (before ~2023)
      return snprintf(out, out_sz, "clock not set (epoch %lu)", (unsigned long)t);
    }
    return snprintf(out, out_sz, "UTC epoch %lu", (unsigned long)t);
  }

  if (!strcasecmp(cmd, "neighbors") || !strcasecmp(cmd, "neighbours") || !strcasecmp(cmd, "seen")) {
    AdvertPath heard[6];
    int n = mesh.getRecentlyHeard(heard, 6);
    int w = snprintf(out, out_sz, "recently heard:");
    int shown = 0;
    for (int k = 0; k < n && (size_t)w + 8 < out_sz; k++) {
      if (heard[k].name[0] == 0) continue;          // empty slot
      const char* hops = (heard[k].path_len == 0xFF) ? "?" : NULL;
      if (hops) {
        w += snprintf(out + w, out_sz - w, " %s(%s)", heard[k].name, hops);
      } else {
        w += snprintf(out + w, out_sz - w, " %s(%uh)", heard[k].name, (unsigned)heard[k].path_len);
      }
      shown++;
    }
    if (shown == 0) w += snprintf(out + w, out_sz - w, " none");
    return w;
  }

  if (!strcasecmp(cmd, "stats")) {
    unsigned long air = mesh.getTotalAirTime() / 1000UL;     // seconds on air (tx)
    unsigned long rxair = mesh.getReceiveAirTime() / 1000UL; // seconds on air (rx)
    return snprintf(out, out_sz,
      "rx F:%lu D:%lu | tx F:%lu D:%lu | air tx:%lus rx:%lus",
      (unsigned long)mesh.getNumRecvFlood(), (unsigned long)mesh.getNumRecvDirect(),
      (unsigned long)mesh.getNumSentFlood(), (unsigned long)mesh.getNumSentDirect(),
      air, rxair);
  }

  // Unknown command: stay silent (no reply), so typos and other bots' commands
  // don't generate mesh traffic.
  return 0;
}

// ---- Entry point ------------------------------------------------------------

bool BotCommands::handle(MyMesh& mesh, const ContactInfo& from, mesh::Packet* pkt, const char* text) {
  if (text == NULL || text[0] != BOT_CMD_PREFIX) return false;  // not a bot command

  // Bot disabled (the "bot" custom var, set via meshcore-cli / phone app):
  // stay completely silent; the message flows to the app as normal text.
  if (!bot_config.isEnabled()) return false;

  unsigned long now = millis();
  if (!rateLimitOk(from.id.pub_key, now)) {
    return true;  // recognised as a command, but dropped to protect the airwaves
  }

  // Split "<cmd> <args>" after the prefix.
  char cmd[24];
  const char* sp = text + 1;
  size_t ci = 0;
  while (*sp && *sp != ' ' && ci < sizeof(cmd) - 1) cmd[ci++] = *sp++;
  cmd[ci] = 0;
  while (*sp == ' ') sp++;
  const char* args = (*sp) ? sp : NULL;

  if (cmd[0] == 0) return true;  // bare "!" with no command

  char reply[168];  // MAX_TEXT_LEN is ~160
  int len = buildReply(mesh, pkt, from.name, cmd, args, reply, sizeof(reply));
  if (len <= 0) return true;
  if ((size_t)len >= sizeof(reply)) len = sizeof(reply) - 1;
  reply[len] = 0;

  uint32_t expected_ack = 0, est_timeout = 0;
  uint32_t ts = mesh.getRTCClock()->getCurrentTime();
  mesh.sendMessage(from, ts, 1, reply, expected_ack, est_timeout);

  _last_reply_ms = now;
  _window_count++;
  return true;
}

// ---- Channel commands -------------------------------------------------------

bool BotCommands::channelAllowed(const char* name) {
  if (name == NULL || name[0] == 0) return false;
  const char* p = bot_config.channels();  // runtime allow-list ("" = none)
  while (*p) {
    const char* start = p;
    while (*p && *p != ',') p++;             // [start, p) is one entry
    size_t entry_len = (size_t)(p - start);
    if (entry_len > 0 && strlen(name) == entry_len && strncmp(name, start, entry_len) == 0) {
      return true;
    }
    if (*p == ',') p++;
  }
  return false;
}

bool BotCommands::handleChannel(MyMesh& mesh, const mesh::GroupChannel& channel, mesh::Packet* pkt,
                                uint32_t timestamp, const char* text) {
  if (!bot_config.isEnabled()) return false;  // bot disabled: fully inert on channels
  if (text == NULL) return false;
  // Channel payloads are formatted "<sender>: <message>" (the sender name is
  // part of the text, since a shared-key channel carries no contact identity).
  // Skip that prefix to find the actual command.
  const char* colon = strstr(text, ": ");
  const char* body = colon ? (colon + 2) : text;
  if (body[0] != BOT_CMD_PREFIX) return false;  // not a bot command

  // Extract the sender display name (the part before ": ") for !ping.
  char sender[20];
  size_t snlen = colon ? (size_t)(colon - text) : 0;
  if (snlen >= sizeof(sender)) snlen = sizeof(sender) - 1;
  memcpy(sender, text, snlen);
  sender[snlen] = 0;

  // Resolve which channel this arrived on, and check the allow-list.
  int idx = mesh.findChannelIdx(channel);
  if (idx < 0) return false;
  ChannelDetails ch;
  if (!mesh.getChannel(idx, ch)) return false;
  if (!channelAllowed(ch.name)) return false;

  unsigned long now = millis();
  // Rate-limit keyed by channel name (channel msgs carry no sender identity).
  uint8_t ckey[4] = {0};
  for (int i = 0; i < 4 && ch.name[i]; i++) ckey[i] = (uint8_t)ch.name[i];
  if (!rateLimitOk(ckey, now)) return true;

  // Parse "<cmd> <args>" after the prefix.
  char cmd[24];
  const char* sp = body + 1;
  size_t ci = 0;
  while (*sp && *sp != ' ' && ci < sizeof(cmd) - 1) cmd[ci++] = *sp++;
  cmd[ci] = 0;
  while (*sp == ' ') sp++;
  const char* args = (*sp) ? sp : NULL;
  if (cmd[0] == 0) return true;

  char reply[168];
  int len = buildReply(mesh, pkt, sender, cmd, args, reply, sizeof(reply));
  if (len <= 0) return true;
  if ((size_t)len >= sizeof(reply)) len = sizeof(reply) - 1;
  reply[len] = 0;

  // Post the reply back to the same channel. (Replies never start with the
  // command prefix, so they cannot re-trigger the bot.)
  mesh::GroupChannel chan = channel;  // sendGroupMessage needs a non-const ref
  mesh.sendGroupMessage(timestamp, chan, mesh.getNodePrefs()->node_name, reply, len);

  _last_reply_ms = now;
  _window_count++;
  return true;
}
