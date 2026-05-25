#pragma once

#if defined(ESP_PLATFORM) && defined(MQTT_PUBLISHER)

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>

// Publishes raw received mesh packets to an MQTT broker as JSON.
// Configuration is stored in NVS (Preferences namespace 'mc-mqtt') and
// editable via the /mqtt-setup web routes this module registers.
//
// Packets are enqueued from any context (e.g. radio rx callback) and drained
// from loop() so the MQTT publish call never blocks the mesh path.

class MqttPublisher {
public:
  MqttPublisher();

  // Load config from NVS and start. Must be called after WiFi is in STA mode.
  void begin();

  // Drive reconnects and drain the publish queue. Call from main loop.
  void loop();

  // Producer side: safe to call from logRxRaw / logTx. Drops oldest if queue is full.
  void enqueueRawPacket(float snr, float rssi, const uint8_t* raw, int len);
  void enqueueTxPacket(const uint8_t* raw, int len);

  // Register /mqtt-setup (GET form, POST save) and /mqtt-status (JSON) on the
  // given web server. Reads/writes NVS; does NOT auto-reconnect on save —
  // user-triggered reboot picks up the new config.
  void attachWebRoutes(AsyncWebServer* server);

  bool isConnected() { return _client.connected(); }
  bool isConfigured() const { return _host.length() > 0; }
  const String& host() const { return _host; }
  uint16_t port() const { return _port; }
  const String& topic() const { return _topic; }

private:
  void _loadConfig();
  void _saveConfig(const String& host, uint16_t port, const String& user,
                   const String& pwd, const String& topic);
  bool _ensureConnected();
  void _publishOne();
  static String _defaultTopic();

  WiFiClient _net;
  PubSubClient _client;
  String _host;
  uint16_t _port = 1883;
  String _user;
  String _pwd;
  String _topic;
  String _client_id;
  uint32_t _last_attempt_ms = 0;
  uint32_t _retry_backoff_ms = 2000;

  enum class Dir : uint8_t { RX = 0, TX = 1 };
  struct Entry {
    Dir dir;
    uint32_t ts;
    float snr;     // valid for RX only
    float rssi;    // valid for RX only
    uint16_t len;
    uint8_t data[256];
  };
  static constexpr size_t QUEUE_LEN = 16;
  Entry _queue[QUEUE_LEN];
  volatile uint8_t _q_head = 0;
  volatile uint8_t _q_tail = 0;
  volatile uint32_t _dropped = 0;
  uint32_t _published = 0;
  uint32_t _publish_errs = 0;
};

// Singleton accessor + free functions for use from logRxRaw / logTx without
// pulling in the full header at every hook site.
MqttPublisher& mqttPublisher();
void mqttPublishRawPacket(float snr, float rssi, const uint8_t* raw, int len);
void mqttPublishTxPacket(const uint8_t* raw, int len);

#endif // ESP_PLATFORM && MQTT_PUBLISHER
