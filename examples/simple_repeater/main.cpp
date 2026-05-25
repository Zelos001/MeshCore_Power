#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

#if defined(ESP32) && defined(WIFI_PROVISIONING)
  #include <helpers/esp32/WifiProvisioning.h>
  #include <helpers/esp32/WifiAdminUI.h>
  #include <helpers/esp32/WifiCliBridge.h>
  #ifdef MQTT_PUBLISHER
    #include <helpers/esp32/MqttPublisher.h>
  #endif
  static WifiProvisioning::Config _wifi_cfg = []() {
    WifiProvisioning::Config c;
    c.ap_password = "meshcore123";
    #ifdef PIN_USER_BTN
    c.user_btn_pin = PIN_USER_BTN;
    #endif
    #ifdef WIFI_SSID
    c.bootstrap_ssid = WIFI_SSID;
    c.bootstrap_password = WIFI_PWD;
    #endif
    return c;
  }();
  WifiProvisioning wifi_provisioning(_wifi_cfg);

  class _RepeaterMeshInfo : public MeshInfoProvider {
  public:
    const char* nodeName() override { return the_mesh.getNodeName(); }
    const char* role() override { return the_mesh.getRole(); }
    const char* firmwareVer() override { return the_mesh.getFirmwareVer(); }
    void formatNeighbours(char* out) override { the_mesh.formatNeighborsReply(out); }
    void formatStats(char* out) override { the_mesh.formatStatsReply(out); }
    void formatRadioStats(char* out) override { the_mesh.formatRadioStatsReply(out); }
    void formatPacketStats(char* out) override { the_mesh.formatPacketStatsReply(out); }
    void listChannels(char* out) override { the_mesh.listChatChannels(out, 512); }
    bool addChannel(const char* name, const char* psk_base64) override {
      return the_mesh.addPersistentChatChannel(name, psk_base64);
    }
    void listBlocked(char* out) override { the_mesh.listBlockPatterns(out, 512); }
    bool addBlocked(const char* pattern) override { return the_mesh.addBlockPattern(pattern); }
    bool removeBlocked(const char* pattern) override { return the_mesh.removeBlockPattern(pattern); }
    void formatRadioConfig(char* out) override {
      NodePrefs* p = the_mesh.getNodePrefs();
      sprintf(out,
        "{\"freq\":%.3f,\"bw\":%.1f,\"sf\":%u,\"cr\":%u,\"tx_power\":%d,"
        "\"path_hash_mode\":%u,\"path_hash_bytes\":%u,\"rx_boosted\":%u}",
        p->freq, p->bw, (unsigned)p->sf, (unsigned)p->cr, (int)p->tx_power_dbm,
        (unsigned)p->path_hash_mode, (unsigned)(p->path_hash_mode + 1),
        (unsigned)p->rx_boosted_gain);
    }
  };
  static _RepeaterMeshInfo _mesh_info;
  static WifiAdminUI* admin_ui = nullptr;

  static void _cli_adapter(const char* cmd, char* reply) {
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    the_mesh.handleCommand(0, buf, reply);
  }
  static WifiCliBridge cli_bridge(5050, _cli_adapter);
  // Same adapter wired into the web admin UI's /api/cmd route.
#endif

void halt() {
  while (1) ;
}

static char command[160];

// For power saving
unsigned long lastActive = 0; // mark last active time
unsigned long nextSleepinSecs = 120; // next sleep in seconds. The first sleep (if enabled) is after 2 minutes from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
static unsigned long userBtnDownAt = 0;
#define USER_BTN_HOLD_OFF_MILLIS 1500
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

  // For power saving
  lastActive = millis(); // mark last active time since boot

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#if defined(ESP32) && defined(WIFI_PROVISIONING)
  board.setInhibitSleep(true);   // WiFi dies when board sleeps
  wifi_provisioning.begin();
  if (wifi_provisioning.inStaMode() && wifi_provisioning.webServer()) {
    admin_ui = new WifiAdminUI(wifi_provisioning.webServer(), &_mesh_info, _cli_adapter);
    admin_ui->begin();
    cli_bridge.begin();
    #ifdef MQTT_PUBLISHER
    mqttPublisher().attachWebRoutes(wifi_provisioning.webServer());
    mqttPublisher().begin();
    #endif
  }
  #ifdef MQTT_PUBLISHER
  else if (wifi_provisioning.webServer()) {
    // AP-mode setup: still let user configure MQTT for after they connect.
    mqttPublisher().attachWebRoutes(wifi_provisioning.webServer());
  }
  #endif
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= USER_BTN_HOLD_OFF_MILLIS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
#if defined(ESP32) && defined(WIFI_PROVISIONING)
  wifi_provisioning.loop();
  if (wifi_provisioning.inStaMode()) {
    cli_bridge.loop();
    #ifdef MQTT_PUBLISHER
    mqttPublisher().loop();
    #endif
  }
#endif
  rtc_clock.tick();

  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
    #if defined(NRF52_PLATFORM)
    board.sleep(1800); // nrf ignores seconds param, sleeps whenever possible
    #else
    if (the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) { // To check if it is time to sleep
      board.sleep(1800);             // To sleep. Wake up after 30 minutes or when receiving a LoRa packet
      lastActive = millis();
      nextSleepinSecs = 5;  // Default: To work for 5s and sleep again
    } else {
      nextSleepinSecs += 5; // When there is pending work, to work another 5s
    }
    #endif
  }
}
