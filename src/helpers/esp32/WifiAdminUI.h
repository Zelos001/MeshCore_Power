#pragma once

#ifdef ESP_PLATFORM

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Read-only view into per-role mesh state for the admin web UI. Implemented by the
// role's main.cpp (which knows the concrete MyMesh type) so this helper stays generic.
class MeshInfoProvider {
public:
  virtual ~MeshInfoProvider() = default;
  virtual const char* nodeName() = 0;
  virtual const char* role() = 0;
  virtual const char* firmwareVer() = 0;
  // Each formatter must write a null-terminated string of <= 1024 bytes into 'out'.
  virtual void formatNeighbours(char* out) = 0;
  virtual void formatStats(char* out) = 0;
  virtual void formatRadioStats(char* out) = 0;
  virtual void formatPacketStats(char* out) = 0;

  // Optional: group-channel management. Default impls return empty / fail so non-
  // repeater roles don't have to care. Repeater overrides delegate to the_mesh.
  virtual void listChannels(char* out) { out[0] = 0; }
  virtual bool addChannel(const char* /*name*/, const char* /*psk_base64*/) { return false; }

  // Optional: name-pattern forwarding blacklist.
  virtual void listBlocked(char* out) { out[0] = 0; }
  virtual bool addBlocked(const char* /*pattern*/) { return false; }
  virtual bool removeBlocked(const char* /*pattern*/) { return false; }

  // Optional: LoRa radio params (freq, bw, sf, cr, tx_power_dbm, path_hash_size).
  // Default no-op so non-repeater roles don't need to implement it.
  virtual void formatRadioConfig(char* out) { out[0] = 0; }
};

// Handler invoked when the user runs a CLI command from the web UI. Same shape as
// the TCP CLI bridge handler so a single adapter in main.cpp serves both.
typedef void (*WifiCmdHandler)(const char* command, char* reply);

class AsyncWebSocket;

class WifiAdminUI {
public:
  WifiAdminUI(AsyncWebServer* server, MeshInfoProvider* mesh, WifiCmdHandler cmd_handler = nullptr);
  void begin();

  // Broadcast a packet to all connected WS clients as JSON. Safe to call from the
  // mesh thread (AsyncWebSocket queues to AsyncTCP task internally).
  void pushRxPacket(float snr, float rssi, const uint8_t* raw, int len);
  void pushTxPacket(const uint8_t* raw, int len);
  void pushChat(const char* channel, const char* sender, const char* text, uint32_t timestamp);

private:
  AsyncWebServer* _server;
  AsyncWebSocket* _ws = nullptr;
  MeshInfoProvider* _mesh;
  WifiCmdHandler _cmd_handler;
};

// Singleton helpers, mirroring MqttPublisher pattern. The free functions are
// no-ops until a WifiAdminUI instance has been constructed.
void wifiAdminPushRxPacket(float snr, float rssi, const uint8_t* raw, int len);
void wifiAdminPushTxPacket(const uint8_t* raw, int len);
void wifiAdminPushChat(const char* channel, const char* sender, const char* text, uint32_t timestamp);

#endif // ESP_PLATFORM
