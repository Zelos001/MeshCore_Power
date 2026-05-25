#ifdef ESP_PLATFORM

#include "WifiCliBridge.h"

WifiCliBridge::WifiCliBridge(uint16_t port, Handler handler)
  : _port(port), _handler(handler), _server(port) {
  _buf[0] = 0;
}

void WifiCliBridge::begin() {
  _server.begin();
  _server.setNoDelay(true);
}

void WifiCliBridge::loop() {
  WiFiClient incoming = _server.available();
  if (incoming) {
    if (_client && _client.connected()) {
      _client.println("(displaced)");
      _client.stop();
    }
    _client = incoming;
    _len = 0;
    _buf[0] = 0;
    _client.print("MeshCore CLI ready. Type 'help'.\r\n> ");
  }

  if (!_client || !_client.connected()) return;

  while (_client.available()) {
    char c = (char)_client.read();
    if (c == '\n') continue;       // tolerate CRLF
    if (c == '\b' || c == 0x7f) {  // backspace / DEL
      if (_len > 0) { _len--; _buf[_len] = 0; }
      continue;
    }
    if (c == '\r' || _len >= CMD_BUF - 1) {
      _buf[_len] = 0;
      if (_len > 0) {
        char reply[256];
        reply[0] = 0;
        _handler(_buf, reply);
        if (reply[0]) {
          _client.print(reply);
          _client.print("\r\n");
        }
      }
      _len = 0;
      _buf[0] = 0;
      _client.print("> ");
      continue;
    }
    _buf[_len++] = c;
    _buf[_len] = 0;
  }
}

#endif // ESP_PLATFORM
