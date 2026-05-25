#pragma once

#ifdef ESP_PLATFORM

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

// AP-first WiFi provisioning with NVS-persisted STA credentials.
// Boot order: long-press USER_BTN to force AP, else try STA from NVS (3 attempts),
// else fall back to AP captive portal. Save from the portal triggers a reboot into STA.

class WifiProvisioning {
public:
  struct Config {
    const char* nvs_namespace = "mc-wifi";
    const char* ap_ssid_prefix = "MeshCore-Setup";
    const char* ap_password = "meshcore123";       // 8+ chars required by ESP32 softAP
    int user_btn_pin = -1;                          // <0 disables long-press wipe
    uint32_t long_press_ms = 5000;
    uint8_t sta_max_attempts = 3;
    uint32_t sta_attempt_timeout_ms = 15000;
    uint16_t web_port = 80;
    uint16_t dns_port = 53;
    const char* bootstrap_ssid = nullptr;           // optional: seed NVS on first boot
    const char* bootstrap_password = nullptr;
  };

  enum class State { BOOT, STA_CONNECTING, STA_CONNECTED, AP_PORTAL };

  explicit WifiProvisioning(const Config& cfg);

  // Blocks until STA is up or AP portal is running. Safe to call once from setup().
  void begin();

  // Must be polled from loop(); drives DNSServer in AP mode and reconnect logic in STA.
  void loop();

  bool isReady() const { return _state == State::STA_CONNECTED || _state == State::AP_PORTAL; }
  bool inApMode() const { return _state == State::AP_PORTAL; }
  bool inStaMode() const { return _state == State::STA_CONNECTED; }
  State state() const { return _state; }

  IPAddress localIP() const;
  String apSsid() const { return _ap_ssid; }

  // Returns the underlying server so other modules (e.g. WifiAdminUI) can attach routes
  // after provisioning is complete. Only valid once STA is up; in AP mode the server is
  // dedicated to the captive portal.
  AsyncWebServer* webServer() { return _server; }

  // Wipes saved creds and reboots into AP portal. Safe to call from a web handler.
  void wipeAndReboot();

private:
  void _checkLongPressWipe();
  bool _loadCreds(String& ssid, String& pwd);
  void _saveCreds(const String& ssid, const String& pwd);
  void _clearCreds();
  bool _tryStaConnect(const String& ssid, const String& pwd);
  void _startApPortal();
  void _startStaWebServer();
  void _registerPortalRoutes();
  void _registerStatusRoutes();
  String _buildApSsid();

  Config _cfg;
  Preferences _prefs;
  DNSServer* _dns = nullptr;
  AsyncWebServer* _server = nullptr;
  State _state = State::BOOT;
  String _ap_ssid;
  bool _portal_routes_registered = false;
  bool _status_routes_registered = false;
  // Saved-cred staging for /save handler -> main loop reboot:
  volatile bool _reboot_pending = false;
  uint32_t _reboot_at_ms = 0;
};

#endif // ESP_PLATFORM
