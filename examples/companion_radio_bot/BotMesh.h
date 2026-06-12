#pragma once

#include "MyMesh.h"        // stock companion mesh (examples/companion_radio)
#include "BotCommands.h"

#include <utility>

/* ----------------------------------------------------------------------------
 *  BotMesh
 *
 *  The companion mesh with the debug bot attached. This subclass is the ONLY
 *  coupling point between the bot and the stock companion firmware: it hooks
 *  the two virtual receive callbacks and begin(), and delegates everything
 *  else to MyMesh untouched. The stock sources in examples/companion_radio
 *  are compiled as-is, so upstream pulls never conflict with the bot.
 * ------------------------------------------------------------------------- */
class BotMesh : public MyMesh {
public:
  // Forward whatever constructor signature upstream's main.cpp uses, so this
  // doesn't need updating when MyMesh's constructor changes.
  template <typename... Args>
  BotMesh(Args&&... args) : MyMesh(std::forward<Args>(args)...) {}

  // NOTE: not virtual in MyMesh — main.cpp calls this through the BotMesh
  // object directly, so this shadowing override is what runs at startup.
  void begin(bool has_display) {
    MyMesh::begin(has_display);
    _bot.begin();
  }

protected:
  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override {
    // Intercept '!'-prefixed debug commands and auto-reply over the mesh.
    bool was_bot_cmd = _bot.handle(*this, from, pkt, text);
  #if BOT_FORWARD_TO_APP == 0
    if (was_bot_cmd) {
      markConnectionActive(from);  // the skipped base handler would have done this
      return;  // suppress the command message from the phone app
    }
  #else
    (void)was_bot_cmd;
  #endif
    MyMesh::onMessageRecv(from, pkt, sender_timestamp, text);
  }

  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override {
    // Answer '!'-prefixed commands posted to an allow-listed channel.
    _bot.handleChannel(*this, channel, pkt, timestamp, text);
    MyMesh::onChannelMessageRecv(channel, pkt, timestamp, text);
  }

private:
  BotCommands _bot;
};
