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

  class _RoomMeshInfo : public MeshInfoProvider {
  public:
    const char* nodeName() override { return the_mesh.getNodeName(); }
    const char* role() override { return the_mesh.getRole(); }
    const char* firmwareVer() override { return the_mesh.getFirmwareVer(); }
    void formatNeighbours(char* out) override { the_mesh.formatNeighborsReply(out); }
    void formatStats(char* out) override { the_mesh.formatStatsReply(out); }
    void formatRadioStats(char* out) override { the_mesh.formatRadioStatsReply(out); }
    void formatPacketStats(char* out) override { the_mesh.formatPacketStatsReply(out); }
  };
  static _RoomMeshInfo _mesh_info;
  static WifiAdminUI* admin_ui = nullptr;

  static void _cli_adapter(const char* cmd, char* reply) {
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    the_mesh.handleCommand(0, buf, reply);
  }
  static WifiCliBridge cli_bridge(5050, _cli_adapter);
#endif

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
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
    admin_ui = new WifiAdminUI(wifi_provisioning.webServer(), &_mesh_info);
    admin_ui->begin();
    cli_bridge.begin();
  }
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
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
#if defined(ESP32) && defined(WIFI_PROVISIONING)
  wifi_provisioning.loop();
  if (wifi_provisioning.inStaMode()) cli_bridge.loop();
#endif
  rtc_clock.tick();
}
