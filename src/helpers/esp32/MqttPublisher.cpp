#if defined(ESP_PLATFORM) && defined(MQTT_PUBLISHER)

#include "MqttPublisher.h"
#include <Preferences.h>

#ifdef WIFI_DEBUG_LOGGING
  #define MQTT_LOG(fmt, ...) Serial.printf("[mqtt] " fmt "\n", ##__VA_ARGS__)
#else
  #define MQTT_LOG(fmt, ...) do {} while (0)
#endif

namespace {
const char SETUP_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=en><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>MeshCore MQTT</title><link rel=stylesheet href=/style.css></head>
<body><div class=wrap>
<header class=top>
  <div><h1>MQTT publisher</h1>
    <div class=meta>forwards every RX and TX (repeated) mesh packet as JSON</div></div>
  <div class=btns><a class=btn href="/">&larr; Home</a></div>
</header>
<div class=card><h2>Broker</h2>
  <small>Published to <code>&lt;topic&gt;/rx</code> and <code>&lt;topic&gt;/tx</code>. Leave host blank to disable.</small>
  <form id=f onsubmit="return save(event)">
    <div class=row>
      <div class=grow><label>Host<input id=host name=host maxlength=120 placeholder="broker.example.com"></label></div>
      <div class=port><label>Port<input id=port name=port type=number value=1883></label></div>
    </div>
    <label>Username (optional)<input id=user name=user maxlength=64></label>
    <label>Password (optional)<input id=pwd name=pwd type=password maxlength=64></label>
    <label>Topic<input id=topic name=topic maxlength=120></label>
    <button type=submit class=primary>Save</button>
  </form>
  <div id=msg></div>
  <small>Saving does not reconnect immediately &mdash; reboot from the home page to apply.</small>
</div>
<div class=card><h2>Status</h2><table id=st></table></div>
<script>
const $=(id)=>document.getElementById(id);
async function load(){const r=await fetch('/mqtt-status');if(!r.ok)return;const j=await r.json();
  $('host').value=j.host||'';$('port').value=j.port||1883;
  $('user').value=j.user||'';$('topic').value=j.topic||'';
  $('st').innerHTML=`<tr><th>Connected</th><td>${j.connected?'yes':'no'} (state ${j.state})</td></tr>
    <tr><th>Published</th><td>${j.published}</td></tr>
    <tr><th>Queue depth</th><td>${j.queue_depth}</td></tr>
    <tr><th>Dropped</th><td>${j.dropped}</td></tr>
    <tr><th>Errors</th><td>${j.errors}</td></tr>`;
}
async function save(ev){ev.preventDefault();const m=$('msg');m.className='msg';m.textContent='Saving…';
  const fd=new FormData($('f'));
  try{const r=await fetch('/mqtt-save',{method:'POST',body:new URLSearchParams(fd)});
    if(r.ok){m.className='msg ok';m.textContent='Saved. Reboot to apply.';}
    else{m.className='msg err';m.textContent='Save failed.'}
  }catch(e){m.className='msg err';m.textContent='Save failed: '+e}return false}
load();setInterval(load,5000);
</script></body></html>)HTML";
}  // namespace

MqttPublisher& mqttPublisher() {
  static MqttPublisher _inst;
  return _inst;
}

void mqttPublishRawPacket(float snr, float rssi, const uint8_t* raw, int len) {
  mqttPublisher().enqueueRawPacket(snr, rssi, raw, len);
}

void mqttPublishTxPacket(const uint8_t* raw, int len) {
  mqttPublisher().enqueueTxPacket(raw, len);
}

MqttPublisher::MqttPublisher() : _client(_net) {}

String MqttPublisher::_defaultTopic() {
  // Base topic; per-direction suffix ("/rx" or "/tx") is appended at publish time.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[40];
  snprintf(buf, sizeof(buf), "meshcore/%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void MqttPublisher::_loadConfig() {
  Preferences p;
  p.begin("mc-mqtt", true);
  _host = p.getString("host", "");
  _port = p.getUShort("port", 1883);
  _user = p.getString("user", "");
  _pwd = p.getString("pwd", "");
  _topic = p.getString("topic", _defaultTopic());
  p.end();
  if (_client_id.length() == 0) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "mc-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _client_id = buf;
  }
}

void MqttPublisher::_saveConfig(const String& host, uint16_t port, const String& user,
                                const String& pwd, const String& topic) {
  Preferences p;
  p.begin("mc-mqtt", false);
  p.putString("host", host);
  p.putUShort("port", port);
  p.putString("user", user);
  p.putString("pwd", pwd);
  p.putString("topic", topic.length() ? topic : _defaultTopic());
  p.end();
}

void MqttPublisher::begin() {
  _loadConfig();
  if (!isConfigured()) {
    MQTT_LOG("not configured, idle");
    return;
  }
  _client.setServer(_host.c_str(), _port);
  _client.setBufferSize(1024);
  MQTT_LOG("configured: %s:%u topic=%s", _host.c_str(), _port, _topic.c_str());
}

bool MqttPublisher::_ensureConnected() {
  if (_client.connected()) return true;
  if (!isConfigured()) return false;
  if (millis() - _last_attempt_ms < _retry_backoff_ms) return false;
  _last_attempt_ms = millis();

  bool ok;
  if (_user.length() > 0) {
    ok = _client.connect(_client_id.c_str(), _user.c_str(), _pwd.c_str());
  } else {
    ok = _client.connect(_client_id.c_str());
  }
  if (ok) {
    MQTT_LOG("connected to %s:%u as %s", _host.c_str(), _port, _client_id.c_str());
    _retry_backoff_ms = 2000;
  } else {
    MQTT_LOG("connect failed, state=%d", _client.state());
    _retry_backoff_ms = min<uint32_t>(_retry_backoff_ms * 2, 60000);
  }
  return ok;
}

void MqttPublisher::enqueueRawPacket(float snr, float rssi, const uint8_t* raw, int len) {
  if (!isConfigured()) return;
  if (len < 0) return;
  if ((size_t)len > sizeof(Entry::data)) len = sizeof(Entry::data);

  uint8_t next = (_q_head + 1) % QUEUE_LEN;
  if (next == _q_tail) {
    _q_tail = (_q_tail + 1) % QUEUE_LEN;
    _dropped++;
  }
  Entry& e = _queue[_q_head];
  e.dir = Dir::RX;
  e.ts = millis();
  e.snr = snr;
  e.rssi = rssi;
  e.len = (uint16_t)len;
  memcpy(e.data, raw, len);
  _q_head = next;
}

void MqttPublisher::enqueueTxPacket(const uint8_t* raw, int len) {
  if (!isConfigured()) return;
  if (len < 0) return;
  if ((size_t)len > sizeof(Entry::data)) len = sizeof(Entry::data);

  uint8_t next = (_q_head + 1) % QUEUE_LEN;
  if (next == _q_tail) {
    _q_tail = (_q_tail + 1) % QUEUE_LEN;
    _dropped++;
  }
  Entry& e = _queue[_q_head];
  e.dir = Dir::TX;
  e.ts = millis();
  e.snr = 0.0f;
  e.rssi = 0;
  e.len = (uint16_t)len;
  memcpy(e.data, raw, len);
  _q_head = next;
}

void MqttPublisher::_publishOne() {
  if (_q_head == _q_tail) return;
  Entry& e = _queue[_q_tail];

  char hex[2 * sizeof(Entry::data) + 1];
  static const char H[] = "0123456789abcdef";
  for (uint16_t i = 0; i < e.len; i++) {
    hex[2 * i]     = H[(e.data[i] >> 4) & 0xF];
    hex[2 * i + 1] = H[e.data[i] & 0xF];
  }
  hex[2 * e.len] = 0;

  char payload[2 * sizeof(Entry::data) + 128];
  const bool is_rx = (e.dir == Dir::RX);
  int n;
  if (is_rx) {
    n = snprintf(payload, sizeof(payload),
      "{\"dir\":\"rx\",\"ts\":%u,\"rssi\":%d,\"snr\":%.2f,\"len\":%u,\"raw\":\"%s\"}",
      (unsigned)e.ts, (int)e.rssi, e.snr, (unsigned)e.len, hex);
  } else {
    n = snprintf(payload, sizeof(payload),
      "{\"dir\":\"tx\",\"ts\":%u,\"len\":%u,\"raw\":\"%s\"}",
      (unsigned)e.ts, (unsigned)e.len, hex);
  }
  if (n <= 0 || (size_t)n >= sizeof(payload)) {
    _publish_errs++;
    _q_tail = (_q_tail + 1) % QUEUE_LEN;
    return;
  }

  String full_topic = _topic + (is_rx ? "/rx" : "/tx");
  if (_client.publish(full_topic.c_str(), payload)) {
    _published++;
    _q_tail = (_q_tail + 1) % QUEUE_LEN;
  } else {
    _publish_errs++;
    // leave entry in queue; will retry on next loop tick after reconnect
  }
}

void MqttPublisher::loop() {
  if (!isConfigured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (!_ensureConnected()) return;
  _client.loop();
  // Drain a few per tick to keep up under bursts without hogging the loop.
  for (int i = 0; i < 4 && _client.connected(); i++) {
    if (_q_head == _q_tail) break;
    _publishOne();
  }
}

void MqttPublisher::attachWebRoutes(AsyncWebServer* server) {
  if (!server) return;

  server->on("/mqtt-setup", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", SETUP_HTML);
  });

  server->on("/mqtt-status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String out = "{";
    out += "\"host\":\"";  out += _host; out += "\",";
    out += "\"port\":";    out += String(_port); out += ",";
    out += "\"user\":\"";  out += _user; out += "\",";
    out += "\"topic\":\""; out += _topic; out += "\",";
    out += "\"connected\":"; out += (_client.connected() ? "true" : "false"); out += ",";
    out += "\"state\":";   out += String(_client.state()); out += ",";
    out += "\"published\":"; out += String(_published); out += ",";
    out += "\"dropped\":"; out += String(_dropped); out += ",";
    out += "\"errors\":";  out += String(_publish_errs); out += ",";
    out += "\"queue_depth\":"; out += String((uint8_t)((_q_head + QUEUE_LEN - _q_tail) % QUEUE_LEN));
    out += "}";
    req->send(200, "application/json", out);
  });

  server->on("/mqtt-save", HTTP_POST, [this](AsyncWebServerRequest* req) {
    String host  = req->hasParam("host", true)  ? req->getParam("host", true)->value()  : String();
    String puser = req->hasParam("user", true)  ? req->getParam("user", true)->value()  : String();
    String ppwd  = req->hasParam("pwd", true)   ? req->getParam("pwd", true)->value()   : String();
    String topic = req->hasParam("topic", true) ? req->getParam("topic", true)->value() : String();
    uint16_t port = 1883;
    if (req->hasParam("port", true)) {
      long v = req->getParam("port", true)->value().toInt();
      if (v > 0 && v < 65536) port = (uint16_t)v;
    }
    _saveConfig(host, port, puser, ppwd, topic);
    req->send(200, "text/plain", "ok");
  });
}

#endif // ESP_PLATFORM && MQTT_PUBLISHER
