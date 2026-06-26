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

void halt() {
  while (1) ;
}

#ifndef ENABLE_GPIO_CONTACT_INPUT
#define ENABLE_GPIO_CONTACT_INPUT 0
#endif

#ifndef ENABLE_GPIO_CONTACT_DEBUG
#define ENABLE_GPIO_CONTACT_DEBUG 0
#endif

#ifndef GPIO_CONTACT_ACTIVE_LOW
#define GPIO_CONTACT_ACTIVE_LOW 1
#endif

#ifndef GPIO_CONTACT_COOLDOWN_MS
#define GPIO_CONTACT_COOLDOWN_MS 5000
#endif

#ifndef GPIO_CONTACT_OPEN_TEXT
#define GPIO_CONTACT_OPEN_TEXT "GPIO contact opened"
#endif

#ifndef GPIO_CONTACT_CLOSED_TEXT
#define GPIO_CONTACT_CLOSED_TEXT "GPIO contact closed"
#endif

#if defined(ENABLE_GPIO_CONTACT_INPUT) && ENABLE_GPIO_CONTACT_INPUT == 1
  #ifndef GPIO_CONTACT_PIN
    #error "GPIO_CONTACT_PIN must be defined when ENABLE_GPIO_CONTACT_INPUT=1"
  #endif
static bool gpio_contact_active = false;
static int gpio_contact_last_raw = HIGH;
static unsigned long gpio_contact_debounce_until = 0;
static unsigned long gpio_contact_last_change = 0;
static bool gpio_contact_initialized = false;
static const unsigned long GPIO_CONTACT_DEBOUNCE_MS = 50;
#endif

static char command[MAX_POST_TEXT_LEN+1];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#if defined(ENABLE_GPIO_CONTACT_INPUT) && ENABLE_GPIO_CONTACT_INPUT == 1
  pinMode(GPIO_CONTACT_PIN, INPUT_PULLUP);
  int raw = digitalRead(GPIO_CONTACT_PIN);
  gpio_contact_last_raw = raw;
  gpio_contact_active = (raw == (GPIO_CONTACT_ACTIVE_LOW ? LOW : HIGH));
  gpio_contact_debounce_until = millis() + GPIO_CONTACT_DEBOUNCE_MS;
  gpio_contact_last_change = millis();
  gpio_contact_initialized = true;

  #if defined(ENABLE_GPIO_CONTACT_DEBUG) && ENABLE_GPIO_CONTACT_DEBUG == 1
    Serial.printf("GPIO contact enabled: pin=%d active_low=%d initial_raw=%d initial_state=%s\n",
                  (int)GPIO_CONTACT_PIN,
                  (int)GPIO_CONTACT_ACTIVE_LOW,
                  raw == LOW ? 0 : 1,
                  gpio_contact_active ? "closed" : "opened");
  #endif
#endif

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
    command[len - 1] = 0;  // replace carriage return with C string null terminator
    char reply[160];
    memset(reply, 0, sizeof(reply));
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#if defined(ENABLE_GPIO_CONTACT_INPUT) && ENABLE_GPIO_CONTACT_INPUT == 1
  int gpio_raw = digitalRead(GPIO_CONTACT_PIN);
  bool gpio_active = (gpio_raw == (GPIO_CONTACT_ACTIVE_LOW ? LOW : HIGH));

  if (gpio_raw != gpio_contact_last_raw) {
    gpio_contact_last_raw = gpio_raw;
    gpio_contact_debounce_until = millis() + GPIO_CONTACT_DEBOUNCE_MS;

    #if defined(ENABLE_GPIO_CONTACT_DEBUG) && ENABLE_GPIO_CONTACT_DEBUG == 1
      Serial.printf("GPIO contact raw changed: pin=%d raw=%d active=%d\n",
                    (int)GPIO_CONTACT_PIN,
                    gpio_raw == LOW ? 0 : 1,
                    gpio_active ? 1 : 0);
    #endif
  }

  if (gpio_contact_initialized &&
      (long)(millis() - gpio_contact_debounce_until) >= 0 &&
      gpio_active != gpio_contact_active &&
      millis() - gpio_contact_last_change >= GPIO_CONTACT_COOLDOWN_MS) {
    gpio_contact_active = gpio_active;
    gpio_contact_last_change = millis();

    const char* msg = gpio_contact_active ? GPIO_CONTACT_CLOSED_TEXT : GPIO_CONTACT_OPEN_TEXT;

    #if defined(ENABLE_GPIO_CONTACT_DEBUG) && ENABLE_GPIO_CONTACT_DEBUG == 1
      Serial.println(msg);
    #endif

    the_mesh.addSystemPost(msg);
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
}
