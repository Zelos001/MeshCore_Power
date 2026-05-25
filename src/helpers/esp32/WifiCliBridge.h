#pragma once

#ifdef ESP_PLATFORM

#include <Arduino.h>
#include <WiFi.h>

// Telnet-style TCP CLI bridge: each '\r'-terminated line from a connected client is
// passed to the provided handler, whose reply is written back to the client. One
// client at a time; later connections displace the active one.

class WifiCliBridge {
public:
  typedef void (*Handler)(const char* command, char* reply);

  WifiCliBridge(uint16_t port, Handler handler);
  void begin();
  void loop();
  bool hasClient() { return _client && _client.connected(); }

private:
  uint16_t _port;
  Handler _handler;
  WiFiServer _server;
  WiFiClient _client;
  static constexpr size_t CMD_BUF = 256;
  char _buf[CMD_BUF];
  size_t _len = 0;
};

#endif // ESP_PLATFORM
