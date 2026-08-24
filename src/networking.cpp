#include "networking.h"
#include "config.h"
#include "dashboard.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_attr.h>

extern Dashboard* const dashboards[];
extern const size_t NUM_DASHBOARDS;

namespace networking {

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);

// Cached MQTT payload kept in RTC slow memory so it survives deep sleep.
// 3 KB is plenty for the current schema (ex-payload.json minified is ~2 KB).
// On a scheduled wake we overwrite this with the freshest message; on a
// button wake we replay it back into the dashboards instead of pulling
// from WiFi.
static constexpr size_t RTC_PAYLOAD_CAP = 3072;
RTC_DATA_ATTR static char  rtcPayload[RTC_PAYLOAD_CAP];
RTC_DATA_ATTR static size_t rtcPayloadLen = 0;

// Last AP channel+BSSID, cached to RTC slow memory so subsequent wakes
// can associate directly on the known channel instead of scanning all of
// them first (a 1-3 s high-current radio step every wake). Zero channel
// means "no cache yet". If the AP ever changes, the fast attempt fails
// and we fall back to a scan — then re-cache from that association, so
// the fast path self-heals after one wake.
RTC_DATA_ATTR static int32_t rtcApChannel = 0;
RTC_DATA_ATTR static uint8_t rtcApBssid[6] = {0};

// -- Helpers ------------------------------------------------------------------

static const char* mqttStateName(int state) {
  switch (state) {
    case -4: return "CONNECTION_TIMEOUT";
    case -3: return "CONNECTION_LOST";
    case -2: return "CONNECT_FAILED";
    case -1: return "DISCONNECTED";
    case  0: return "CONNECTED";
    case  1: return "BAD_PROTOCOL";
    case  2: return "BAD_CLIENT_ID";
    case  3: return "UNAVAILABLE";
    case  4: return "BAD_CREDENTIALS";
    case  5: return "UNAUTHORIZED";
    default: return "UNKNOWN";
  }
}

static void dispatchDoc(const char* topic, JsonDocument& doc) {
  bool dispatched = false;
  for (size_t i = 0; i < NUM_DASHBOARDS; i++) {
    if (strcmp(topic, dashboards[i]->topic()) == 0) {
      dashboards[i]->handlePayload(doc);
      Serial.printf("[mqtt] Dispatched to: %s\n", dashboards[i]->name());
      dispatched = true;
    }
  }
  if (!dispatched) {
    Serial.printf("[mqtt] No handler for topic: %s\n", topic);
  }
}

static void onMessage(char* topic, byte* payload, unsigned int length) {
  Serial.printf("[mqtt] Recv on %s (%u bytes)\n", topic, length);

  // Cache to RTC RAM so a button-wake can re-render without hitting WiFi.
  if (length < RTC_PAYLOAD_CAP) {
    memcpy(rtcPayload, payload, length);
    rtcPayloadLen = length;
  } else {
    Serial.printf("[mqtt] Payload %u B > RTC cache %u B — not cached\n",
                  length, (unsigned)RTC_PAYLOAD_CAP);
    rtcPayloadLen = 0;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[mqtt] JSON parse error: %s\n", err.c_str());
    return;
  }
  dispatchDoc(topic, doc);
}

// -- Public API ---------------------------------------------------------------

bool connectWiFi() {
  // Persistent off: the Arduino core defaults to writing SSID/pass to NVS
  // flash on every begin() — a per-wake flash write (power + NVS wear) we
  // don't need since credentials are compiled in.
  WiFi.persistent(false);
  Serial.printf("[wifi] Connecting to %s...\n", config::WIFI_SSID);
  WiFi.mode(WIFI_STA);

  // Outer deadline measured from the very first attempt; the inner
  // per-attempt timer still restarts every 30 s to re-issue WiFi.begin.
  const unsigned long totalStart = millis();

  // Fast path: with a cached channel+BSSID the core can associate
  // directly instead of scanning every channel first.
  if (rtcApChannel > 0) {
    Serial.printf("[wifi] Fast connect (cached ch=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x)\n",
                  rtcApChannel,
                  rtcApBssid[0], rtcApBssid[1], rtcApBssid[2],
                  rtcApBssid[3], rtcApBssid[4], rtcApBssid[5]);
    WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD, rtcApChannel, rtcApBssid);
    const unsigned long fastStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - fastStart > 5000) break;
      if (millis() - totalStart > config::WIFI_CONNECT_TIMEOUT_MS) {
        Serial.printf("[wifi] Gave up after %lu ms — will fall back to cached payload\n",
                      millis() - totalStart);
        return false;
      }
      delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] Fast connect failed — falling back to scan");
      WiFi.disconnect(true);
      delay(250);
      WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
    }
  } else {
    WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
  }

  unsigned long attemptStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - attemptStart > 30000) {
      Serial.println("\n[wifi] Timeout — restarting attempt");
      WiFi.disconnect(true);
      delay(1000);
      WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);
      attemptStart = millis();
    }
    if (millis() - totalStart > config::WIFI_CONNECT_TIMEOUT_MS) {
      Serial.printf("[wifi] Gave up after %lu ms — will fall back to cached payload\n",
                    millis() - totalStart);
      return false;
    }
  }

  Serial.println();
  Serial.println("[wifi] Connected");
  // Cache this association's channel+BSSID for the next wake's fast path.
  int32_t ch = WiFi.channel();
  uint8_t *bssid = WiFi.BSSID();          // 6 bytes, valid while associated
  if (ch > 0 && bssid != nullptr) {
    rtcApChannel = ch;
    memcpy(rtcApBssid, bssid, 6);
  }
  Serial.printf("  SSID:    %s\n", WiFi.SSID().c_str());
  Serial.printf("  IP:      %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("  DNS:     %s\n", WiFi.dnsIP().toString().c_str());
  Serial.printf("  Subnet:  %s\n", WiFi.subnetMask().toString().c_str());
  Serial.printf("  RSSI:    %d dBm\n", WiFi.RSSI());
  Serial.printf("  MAC:     %s\n", WiFi.macAddress().c_str());
  Serial.printf("  Channel: %d\n", WiFi.channel());

  // Kick off SNTP and apply the POSIX timezone. SNTP runs in the background;
  // time(nullptr) will start returning a real epoch within a few seconds.
  // waitForTimeSync() must be called before sleeping or the very first
  // scheduled wake will land at a random minute, not on the hour.
  configTzTime(config::TIMEZONE,
               config::NTP_SERVER_1,
               config::NTP_SERVER_2);
  Serial.printf("[time] NTP requested (servers %s, %s), TZ=%s\n",
                config::NTP_SERVER_1, config::NTP_SERVER_2, config::TIMEZONE);
  return true;
}

bool waitForTimeSync(unsigned long timeoutMs) {
  constexpr time_t MIN_REASONABLE_EPOCH = 1700000000;  // 2023-11-14

  time_t now = time(nullptr);
  if (now >= MIN_REASONABLE_EPOCH) {
    struct tm lt;
    localtime_r(&now, &lt);
    Serial.printf("[time] Clock already set: %04d-%02d-%02d %02d:%02d:%02d\n",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                  lt.tm_hour, lt.tm_min, lt.tm_sec);
    return true;
  }

  Serial.printf("[time] Waiting for SNTP sync (up to %lu ms)...\n", timeoutMs);
  const unsigned long start = millis();
  while ((now = time(nullptr)) < MIN_REASONABLE_EPOCH) {
    if (millis() - start > timeoutMs) {
      Serial.println("[time] SNTP sync TIMEOUT — wake alignment will drift");
      return false;
    }
    delay(200);
  }

  struct tm lt;
  localtime_r(&now, &lt);
  Serial.printf("[time] Synced in %lu ms: %04d-%02d-%02d %02d:%02d:%02d\n",
                millis() - start,
                lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                lt.tm_hour, lt.tm_min, lt.tm_sec);
  return true;
}

bool connectMqtt() {
  mqtt.setServer(config::MQTT_HOST, config::MQTT_PORT);
  mqtt.setCallback(onMessage);
  if (!mqtt.setBufferSize(4096)) {
    Serial.printf("[mqtt] WARNING: setBufferSize(4096) failed — payloads may truncate\n");
  }
  mqtt.setKeepAlive(30);

  // Unique client ID per device: two desk devices waking in the same
  // window would otherwise evict each other's MQTT session (brokers
  // force-disconnect the older connection when a client ID collides).
  // Base name from config + 16 bits from the efuse MAC's upper half —
  // on the classic ESP32 getEfuseMac() returns the OUI (vendor bytes,
  // identical across all Espressif chips) in the LOW 24 bits and the
  // device-specific NIC suffix up high, so bits 47:32 (the last two MAC
  // bytes) differ per device.
  static char clientId[32];
  uint64_t mac = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "%s-%04X",
           config::MQTT_CLIENT_ID, (uint16_t)(mac >> 32));

  const unsigned long start = millis();
  int n = 0;
  while (!mqtt.connected()) {
    if (millis() - start > config::MQTT_CONNECT_TIMEOUT_MS) {
      Serial.printf("[mqtt] Gave up after %lu ms (state=%d %s)\n",
                    millis() - start, mqtt.state(), mqttStateName(mqtt.state()));
      return false;
    }

    Serial.printf("[mqtt] Connecting to %s:%d as %s...\n",
                  config::MQTT_HOST, config::MQTT_PORT, clientId);

    bool ok = config::MQTT_USER
      ? mqtt.connect(clientId, config::MQTT_USER, config::MQTT_PASS)
      : mqtt.connect(clientId);

    if (ok) {
      Serial.println("[mqtt] Connected");
      for (size_t i = 0; i < NUM_DASHBOARDS; i++) {
        bool subOk = mqtt.subscribe(dashboards[i]->topic());
        Serial.printf("[mqtt]   subscribe %s -> %s\n",
                      dashboards[i]->topic(), subOk ? "OK" : "FAIL");
      }
      return true;
    }

    int s = mqtt.state();
    unsigned long backoff = min(config::MQTT_RETRY_BACKOFF_BASE_MS << n, 8000UL);
    Serial.printf("[mqtt] Failed (state=%d %s) — retry in %lu ms (attempt %d)\n",
                  s, mqttStateName(s), backoff, n + 1);
    delay(backoff);
    n++;
  }
  return true;
}

bool pumpForPayload(unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    mqtt.loop();
    // Subscribers all share the same topic; the first dashboard that has
    // data after subscribe is a sufficient signal that the retained
    // message arrived.
    for (size_t i = 0; i < NUM_DASHBOARDS; i++) {
      if (dashboards[i]->hasData) return true;
    }
    delay(20);
  }
  return false;
}

bool replayCachedPayload() {
  if (rtcPayloadLen == 0) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rtcPayload, rtcPayloadLen);
  if (err) {
    Serial.printf("[mqtt] Cached payload parse error: %s\n", err.c_str());
    return false;
  }

  // All dashboards share the same topic in this firmware. Dispatch by
  // walking the registry and matching topics — same logic as live MQTT.
  // Use the first dashboard's topic as the canonical key.
  if (NUM_DASHBOARDS == 0) return false;
  const char* topic = dashboards[0]->topic();
  dispatchDoc(topic, doc);
  Serial.printf("[mqtt] Replayed %u cached bytes\n", (unsigned)rtcPayloadLen);
  return true;
}

bool hasCachedPayload() { return rtcPayloadLen > 0; }

// Kill the radios cleanly once the wake's data work is done — the render
// that follows takes seconds and needs no radio. MQTT gets a clean
// DISCONNECT (if connected) so the broker drops the session promptly;
// WiFi.disconnect(true) + WIFI_OFF powers the STA interface down fully.
// Safe to call regardless of current radio state (all no-ops when off).
// Call AFTER logHealth() — its report is meant to capture live status.
void shutdown() {
  if (mqtt.connected()) {
    mqtt.disconnect();
    Serial.println("[mqtt] Disconnected");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[wifi] Radio off");
}

void logHealth() {
  Serial.println("---- health check ----");
  Serial.printf("  Uptime:      %lu s\n", millis() / 1000);
  Serial.printf("  Free heap:   %u bytes\n", ESP.getFreeHeap());
  Serial.printf("  WiFi:        %s (RSSI %d dBm)\n",
                WiFi.status() == WL_CONNECTED ? "UP" : "DOWN",
                WiFi.RSSI());
  Serial.printf("  MQTT:        %s (state=%s)\n",
                mqtt.connected() ? "UP" : "DOWN",
                mqttStateName(mqtt.state()));
  Serial.println("----------------------");
}

bool isWiFiConnected() { return WiFi.status() == WL_CONNECTED; }
bool isMqttConnected() { return mqtt.connected(); }

} // namespace networking