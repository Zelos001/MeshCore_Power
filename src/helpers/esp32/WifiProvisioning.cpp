#ifdef ESP_PLATFORM

#include "WifiProvisioning.h"
#include <esp_wifi.h>

#ifdef WIFI_DEBUG_LOGGING
  #define WIFI_LOG(fmt, ...) Serial.printf("[wifi] " fmt "\n", ##__VA_ARGS__)
#else
  #define WIFI_LOG(fmt, ...) do {} while (0)
#endif

namespace {
const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>MeshCore WiFi Setup</title>
<style>
body{font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;color:#111}
h1{font-size:1.2em}label{display:block;margin:.6em 0 .2em}input,select,button{font-size:1em;width:100%;padding:.5em;box-sizing:border-box}
button{margin-top:1em;background:#1a73e8;color:#fff;border:0;border-radius:4px;padding:.7em}
.row{display:flex;gap:.5em;align-items:center}.row button{width:auto}
#networks{margin:.5em 0;max-height:240px;overflow:auto;border:1px solid #ddd;border-radius:4px}
.net{padding:.5em;border-bottom:1px solid #eee;cursor:pointer;display:flex;justify-content:space-between}
.net:hover{background:#f4f7ff}.net:last-child{border:0}.rssi{color:#888;font-size:.85em}
.msg{padding:.5em;border-radius:4px;margin-top:1em}.ok{background:#e6f4ea}.err{background:#fce8e6}
</style></head><body>
<h1>MeshCore WiFi Setup</h1>
<div class=row><div style=flex:1>Pick a network or enter one below.</div><button onclick=scan()>Rescan</button></div>
<div id=networks>Scanning&hellip;</div>
<form id=f onsubmit="return save(event)">
<label>SSID<input id=ssid name=ssid required maxlength=32></label>
<label>Password<input id=pwd name=pwd type=password maxlength=64></label>
<button type=submit>Save &amp; connect</button>
</form>
<div id=msg></div>
<script>
async function scan(){const el=document.getElementById('networks');el.textContent='Scanning…';
  try{const r=await fetch('/scan');const j=await r.json();
    if(!j.length){el.textContent='No networks found.';return}
    el.innerHTML=j.map(n=>`<div class=net data-ssid="${n.s.replace(/"/g,'&quot;')}"><span>${n.s||'(hidden)'} ${n.e?'\u{1F512}':''}</span><span class=rssi>${n.r} dBm</span></div>`).join('');
    el.querySelectorAll('.net').forEach(d=>d.onclick=()=>{document.getElementById('ssid').value=d.dataset.ssid;document.getElementById('pwd').focus()});
  }catch(e){el.textContent='Scan failed: '+e}
}
async function save(ev){ev.preventDefault();const m=document.getElementById('msg');m.className='msg';m.textContent='Saving…';
  const fd=new FormData(document.getElementById('f'));
  try{const r=await fetch('/save',{method:'POST',body:new URLSearchParams(fd)});
    if(r.ok){m.className='msg ok';m.textContent='Saved. Device is rebooting and joining the network.';}
    else{m.className='msg err';m.textContent='Save failed.'}
  }catch(e){m.className='msg err';m.textContent='Save failed: '+e}return false}
scan();
</script></body></html>)HTML";

const char STATUS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>MeshCore</title>
<style>body{font-family:system-ui,sans-serif;max-width:520px;margin:1em auto;padding:0 1em}
button{font-size:1em;padding:.5em 1em;margin-right:.5em}.danger{background:#c62828;color:#fff;border:0;border-radius:4px}
pre{background:#f4f4f4;padding:.6em;border-radius:4px;overflow:auto}</style></head><body>
<h1>MeshCore</h1><pre id=s>loading…</pre>
<button class=danger onclick="if(confirm('Wipe WiFi credentials and reboot?'))fetch('/wipe-wifi',{method:'POST'}).then(()=>document.body.innerHTML='<p>Rebooting into setup mode…</p>')">Wipe WiFi &amp; reboot</button>
<script>fetch('/wifi-status').then(r=>r.json()).then(j=>document.getElementById('s').textContent=JSON.stringify(j,null,2));</script>
</body></html>)HTML";
}  // namespace

WifiProvisioning::WifiProvisioning(const Config& cfg) : _cfg(cfg) {}

String WifiProvisioning::_buildApSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s-%02X%02X", _cfg.ap_ssid_prefix, mac[4], mac[5]);
  return String(buf);
}

IPAddress WifiProvisioning::localIP() const {
  return _state == State::AP_PORTAL ? WiFi.softAPIP() : WiFi.localIP();
}

void WifiProvisioning::_checkLongPressWipe() {
  if (_cfg.user_btn_pin < 0) return;
  pinMode(_cfg.user_btn_pin, INPUT_PULLUP);
  delay(20);
  if (digitalRead(_cfg.user_btn_pin) != LOW) return;
  WIFI_LOG("button held at boot, timing for wipe...");
  uint32_t start = millis();
  while (digitalRead(_cfg.user_btn_pin) == LOW) {
    if (millis() - start >= _cfg.long_press_ms) {
      WIFI_LOG("long-press confirmed, clearing creds");
      _clearCreds();
      return;
    }
    delay(50);
  }
}

bool WifiProvisioning::_loadCreds(String& ssid, String& pwd) {
  _prefs.begin(_cfg.nvs_namespace, true);
  ssid = _prefs.getString("ssid", "");
  pwd = _prefs.getString("pwd", "");
  _prefs.end();
  if (ssid.length() == 0 && _cfg.bootstrap_ssid) {
    ssid = _cfg.bootstrap_ssid;
    pwd = _cfg.bootstrap_password ? _cfg.bootstrap_password : "";
  }
  return ssid.length() > 0;
}

void WifiProvisioning::_saveCreds(const String& ssid, const String& pwd) {
  _prefs.begin(_cfg.nvs_namespace, false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pwd", pwd);
  _prefs.end();
}

void WifiProvisioning::_clearCreds() {
  _prefs.begin(_cfg.nvs_namespace, false);
  _prefs.clear();
  _prefs.end();
}

bool WifiProvisioning::_tryStaConnect(const String& ssid, const String& pwd) {
  WIFI_LOG("STA connecting to '%s' (pwd_len=%u)", ssid.c_str(), (unsigned)pwd.length());

  // One-shot event handler to surface low-level disconnect reason codes.
  // Reason 200+ are auth/4-way-handshake failures, 201 = NO_AP_FOUND, 202 = AUTH_FAIL,
  // 204 = HANDSHAKE_TIMEOUT, 205 = CONNECTION_FAIL. The bare WL_NO_SSID_AVAIL the
  // Arduino layer reports can mask several of these.
  static WiFiEventId_t evt_id = 0;
  if (evt_id) WiFi.removeEvent(evt_id);
  evt_id = WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      WIFI_LOG("STA disconnect reason=%u", (unsigned)info.wifi_sta_disconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      WIFI_LOG("STA associated, awaiting IP");
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      WIFI_LOG("STA got IP");
    }
  });

  // Fully clear any prior WiFi state (esp. lingering AP mode from previous boot).
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setSleep(false);            // helps with RX reliability on some routers
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pwd.c_str());

  uint32_t deadline = millis() + _cfg.sta_attempt_timeout_ms;
  while (millis() < deadline) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      WIFI_LOG("STA up, IP=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return true;
    }
    delay(250);
  }
  WIFI_LOG("STA attempt timed out (status=%d)", (int)WiFi.status());
  WiFi.disconnect(true, false);
  return false;
}

void WifiProvisioning::_startApPortal() {
  WIFI_LOG("starting AP portal '%s'", _ap_ssid.c_str());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ap_ssid.c_str(), _cfg.ap_password);
  delay(100);
  IPAddress ip = WiFi.softAPIP();
  WIFI_LOG("AP IP=%s", ip.toString().c_str());

  _dns = new DNSServer();
  _dns->setErrorReplyCode(DNSReplyCode::NoError);
  _dns->start(_cfg.dns_port, "*", ip);

  _server = new AsyncWebServer(_cfg.web_port);
  _registerPortalRoutes();
  _server->begin();
  _state = State::AP_PORTAL;
}

void WifiProvisioning::_startStaWebServer() {
  // Lazy: only spin up the server if a caller asks via webServer(); status routes are
  // attached the first time the server is materialized.
  if (_server) return;
  _server = new AsyncWebServer(_cfg.web_port);
  _registerStatusRoutes();
  _server->begin();
}

void WifiProvisioning::_registerPortalRoutes() {
  _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", PORTAL_HTML);
  });

  _server->on("/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) { req->send(200, "application/json", "[]"); return; }
    if (n < 0) { WiFi.scanNetworks(true); req->send(200, "application/json", "[]"); return; }
    String out = "[";
    for (int i = 0; i < n; i++) {
      if (i) out += ',';
      String s = WiFi.SSID(i);
      s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
      out += "{\"s\":\""; out += s;
      out += "\",\"r\":"; out += String(WiFi.RSSI(i));
      out += ",\"e\":"; out += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
      out += "}";
    }
    out += "]";
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    req->send(200, "application/json", out);
  });

  _server->on("/save", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("ssid", true)) { req->send(400, "text/plain", "missing ssid"); return; }
    String ssid = req->getParam("ssid", true)->value();
    String pwd = req->hasParam("pwd", true) ? req->getParam("pwd", true)->value() : String();
    if (ssid.length() == 0 || ssid.length() > 32) { req->send(400, "text/plain", "bad ssid"); return; }
    _saveCreds(ssid, pwd);
    req->send(200, "text/plain", "ok");
    _reboot_pending = true;
    _reboot_at_ms = millis() + 800;
  });

  // Captive-portal probes: send everything to root so the OS pops the portal.
  _server->onNotFound([this](AsyncWebServerRequest* req) {
    req->redirect("http://" + WiFi.softAPIP().toString() + "/");
  });

  // Kick off an initial scan in the background.
  WiFi.scanNetworks(true);
}

void WifiProvisioning::_registerStatusRoutes() {
  // STA-mode routes live under /wifi-* so callers (e.g. WifiAdminUI) own '/'.
  _server->on("/wifi-setup", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", STATUS_HTML);
  });
  _server->on("/wifi-status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String out = "{";
    out += "\"ssid\":\"" + WiFi.SSID() + "\",";
    out += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    out += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    out += "\"mac\":\"" + WiFi.macAddress() + "\"";
    out += "}";
    req->send(200, "application/json", out);
  });
  _server->on("/wipe-wifi", HTTP_POST, [this](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", "ok");
    _clearCreds();
    _reboot_pending = true;
    _reboot_at_ms = millis() + 500;
  });
}

void WifiProvisioning::wipeAndReboot() {
  _clearCreds();
  delay(200);
  ESP.restart();
}

void WifiProvisioning::begin() {
  _ap_ssid = _buildApSsid();
  _checkLongPressWipe();

  String ssid, pwd;
  bool have_creds = _loadCreds(ssid, pwd);

  if (have_creds) {
    _state = State::STA_CONNECTING;
    for (uint8_t i = 0; i < _cfg.sta_max_attempts; i++) {
      if (_tryStaConnect(ssid, pwd)) {
        _state = State::STA_CONNECTED;
        _startStaWebServer();
        return;
      }
      delay(500);
    }
    WIFI_LOG("STA failed %u times, falling back to AP", (unsigned)_cfg.sta_max_attempts);
  }
  _startApPortal();
}

void WifiProvisioning::loop() {
  if (_reboot_pending && (int32_t)(millis() - _reboot_at_ms) >= 0) {
    WIFI_LOG("rebooting on request");
    delay(50);
    ESP.restart();
  }
  if (_state == State::AP_PORTAL && _dns) {
    _dns->processNextRequest();
  }
}

#endif // ESP_PLATFORM
