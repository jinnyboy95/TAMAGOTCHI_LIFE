#include "net.h"
#include <ArduinoJson.h>
#include <WiFiNINA.h>
#include "brain.h"
#include "config.h"
#include "save.h"
#include "secrets.h"

namespace Net {

Weather wx = {};
char city[24] = "";
char ssids[8][33];
int8_t ssidRssi[8];
uint8_t ssidCount = 0;
uint8_t networksSeen = 0;
bool everConnected = false;
int16_t fakeMinutes = -1;
bool fakeWeather = false;

static float lat = 0, lon = 0;
static bool haveGeo = false;
static int32_t utcOffset = 0;
static bool haveOffset = false;
static uint32_t epochAtSync = 0, msAtSync = 0;
static bool told = false;
static uint32_t lastGeoMs = 0, lastNtpMs = 0, lastWeatherAttempt = 0, lastScanMs = 0, lastConnAttempt = 0;
static bool weatherAttempted = false;

void begin() {
#if defined(HOME_LAT) && defined(HOME_LON)
  lat = HOME_LAT;
  lon = HOME_LON;
  haveGeo = true;
#endif
}

bool configured() { return strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0; }

bool connected() { return configured() && WiFi.status() == WL_CONNECTED; }

int rssi() { return connected() ? WiFi.RSSI() : -127; }

bool ensureConnected() {
  if (!configured()) return false;
  if (WiFi.status() == WL_CONNECTED) return true;
  lastConnAttempt = millis();
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("[net] no WiFi module?!"));
    return false;
  }
  Serial.print(F("[net] connecting to "));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 16 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[net] connect failed"));
    return false;
  }
  everConnected = true;
  Serial.print(F("[net] connected, ip "));
  Serial.println(WiFi.localIP());
  return true;
}

// ---------------------------------------------------------------------------
// HTTP: plain GET, HTTP/1.0 so there's no chunked encoding to deal with
// ---------------------------------------------------------------------------
static bool httpGet(WiFiClient &c, const char *host, uint16_t port, const char *path) {
  c.setTimeout(8000);
  if (!c.connect(host, port)) return false;
  c.print(F("GET "));
  c.print(path);
  c.println(F(" HTTP/1.0"));
  c.print(F("Host: "));
  c.println(host);
  c.println(F("User-Agent: tamagotchi-life/1.0"));
  c.println(F("Connection: close"));
  c.println();
  char status[40];
  size_t n = c.readBytesUntil('\n', status, sizeof(status) - 1);
  status[n] = 0;
  if (!strstr(status, " 200")) {
    Serial.print(F("[net] "));
    Serial.print(host);
    Serial.print(F(" said: "));
    Serial.println(status);
    c.stop();
    return false;
  }
  if (!c.find((char *)"\r\n\r\n")) {
    c.stop();
    return false;
  }
  return true;
}

static void fmtCoord(char *out, size_t len, float v) {
  long m = lroundf(v * 1000);
  bool neg = m < 0;
  if (neg) m = -m;
  snprintf(out, len, "%s%ld.%03ld", neg ? "-" : "", m / 1000, m % 1000);
}

static bool syncTime() {
  for (int i = 0; i < 6; i++) {
    unsigned long t = WiFi.getTime();
    if (t > 1600000000UL) {
      epochAtSync = t;
      msAtSync = millis();
      lastNtpMs = millis();
      Serial.print(F("[net] epoch "));
      Serial.println(t);
      return true;
    }
    delay(500);
  }
  return false;
}

static bool fetchGeo() {
  WiFiClient c;
  if (!httpGet(c, "ip-api.com", 80, "/json/?fields=status,city,lat,lon,offset")) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, c);
  c.stop();
  if (err || strcmp(doc["status"] | "", "success") != 0) return false;
  lat = doc["lat"];
  lon = doc["lon"];
  strlcpy(city, doc["city"] | "", sizeof(city));
  for (char *p = city; *p; p++) *p = tolower(*p);
  if (!haveOffset) {
    utcOffset = doc["offset"] | 0;
    haveOffset = true;
  }
  haveGeo = true;
  lastGeoMs = millis();
  Serial.print(F("[net] you live in "));
  Serial.print(city);
  Serial.print(F(" ("));
  Serial.print(lat, 3);
  Serial.print(',');
  Serial.print(lon, 3);
  Serial.println(')');
  return true;
}

static WeatherKind fromWmo(int c) {
  if (c <= 1) return W_CLEAR;
  if (c <= 3) return W_CLOUDY;
  if (c == 45 || c == 48) return W_FOG;
  if (c >= 51 && c <= 57) return W_DRIZZLE;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return W_RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return W_SNOW;
  if (c >= 95) return W_STORM;
  return W_CLOUDY;
}

static int16_t hhmm(const char *iso) {
  // "2026-10-15T07:12" -> 432
  if (!iso || strlen(iso) < 16) return -1;
  return (iso[11] - '0') * 600 + (iso[12] - '0') * 60 + (iso[14] - '0') * 10 + (iso[15] - '0');
}

static bool fetchWeather() {
  char la[16], lo[16], path[256];
  fmtCoord(la, sizeof(la), lat);
  fmtCoord(lo, sizeof(lo), lon);
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%s&longitude=%s"
           "&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,is_day,precipitation"
           "&daily=sunrise,sunset&timezone=auto&forecast_days=1",
           la, lo);

  JsonDocument filter;
  filter["utc_offset_seconds"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["current"]["wind_direction_10m"] = true;
  filter["current"]["is_day"] = true;
  filter["current"]["precipitation"] = true;
  filter["daily"]["sunrise"] = true;
  filter["daily"]["sunset"] = true;

  JsonDocument doc;
  bool ok = false;
  {
    WiFiClient c;
    if (httpGet(c, "api.open-meteo.com", 80, path)) {
      ok = !deserializeJson(doc, c, DeserializationOption::Filter(filter));
      c.stop();
    }
  }
  if (!ok) {
    WiFiSSLClient c;
    if (httpGet(c, "api.open-meteo.com", 443, path)) {
      ok = !deserializeJson(doc, c, DeserializationOption::Filter(filter));
      c.stop();
    }
  }
  if (!ok || doc["current"].isNull()) return false;

  if (!doc["utc_offset_seconds"].isNull()) {
    utcOffset = doc["utc_offset_seconds"];
    haveOffset = true;
  }
  if (fakeWeather) return true;  // debug override wins; still learned the clock
  JsonObject cur = doc["current"];
  wx.kind = fromWmo(cur["weather_code"] | 3);
  wx.tempC = cur["temperature_2m"] | 15.0f;
  wx.windKmh = cur["wind_speed_10m"] | 0.0f;
  wx.windDir = cur["wind_direction_10m"] | 0.0f;
  wx.precipMm = cur["precipitation"] | 0.0f;
  wx.isDay = (cur["is_day"] | 1) == 1;
  wx.sunriseMin = hhmm(doc["daily"]["sunrise"][0]);
  wx.sunsetMin = hhmm(doc["daily"]["sunset"][0]);
  wx.fetchedMs = millis();
  wx.valid = true;
  Serial.print(F("[net] weather: "));
  Serial.print(kindName(wx.kind));
  Serial.print(' ');
  Serial.print(wx.tempC, 1);
  Serial.print(F("C wind "));
  Serial.print(wx.windKmh, 0);
  Serial.println(F("km/h"));
  return true;
}

bool refreshAll() {
  weatherAttempted = true;
  lastWeatherAttempt = millis();
  if (!ensureConnected()) return false;
  if (!epochAtSync || millis() - lastNtpMs > NTP_EVERY_H * 3600000UL) syncTime();
  if (!haveGeo || millis() - lastGeoMs > GEO_EVERY_H * 3600000UL) fetchGeo();
  bool ok = haveGeo && fetchWeather();
  lastWeatherAttempt = millis();
  if (epochAtSync && !told) {
    told = true;
    Brain::onTimeKnown(epoch(), Save::lastSeenEpoch());
  }
  return ok;
}

bool scan() {
  lastScanMs = millis();
  int n = WiFi.scanNetworks();
  if (n < 0) return false;
  networksSeen = n;
  ssidCount = 0;
  for (int i = 0; i < n && ssidCount < 8; i++) {
    const char *name = WiFi.SSID(i);
    if (!name || !name[0] || !strcmp(name, WIFI_SSID)) continue;
    bool dup = false;
    for (int j = 0; j < ssidCount; j++) dup |= !strcmp(ssids[j], name);
    if (dup) continue;
    strlcpy(ssids[ssidCount], name, sizeof(ssids[0]));
    ssidRssi[ssidCount] = WiFi.RSSI(i);
    ssidCount++;
  }
  Serial.print(F("[net] sniffed "));
  Serial.print(n);
  Serial.println(F(" networks"));
  return true;
}

bool weatherDue() {
  if (fakeWeather) return false;
  uint32_t now = millis();
  if (!weatherAttempted) return true;
  if (now - lastWeatherAttempt < 60000UL) return false;
  if (wx.valid) return now - wx.fetchedMs > WEATHER_EVERY_MIN * 60000UL;
  return now - lastWeatherAttempt > WIFI_RETRY_MIN * 60000UL;
}

bool weatherOverdue() {
  if (fakeWeather) return false;
  if (!weatherAttempted) return true;
  return wx.valid && millis() - wx.fetchedMs > WEATHER_FORCE_MIN * 60000UL && millis() - lastWeatherAttempt > 60000UL;
}

bool scanDue() {
  if (!lastScanMs) return millis() > 120000UL;
  return millis() - lastScanMs > SCAN_EVERY_MIN * 60000UL;
}

bool retryDue() { return millis() - lastConnAttempt > WIFI_RETRY_MIN * 60000UL; }

uint32_t epoch() {
  if (!epochAtSync) return 0;
  return epochAtSync + (millis() - msAtSync) / 1000;
}

int localMinutes() {
  if (fakeMinutes >= 0) return fakeMinutes;
  if (!epochAtSync || !haveOffset) return -1;
  return (int)(((int64_t)epoch() + utcOffset) % 86400 / 60);
}

bool isDark() {
  int m = localMinutes();
  if (m < 0) return wx.valid ? !wx.isDay : false;
  int rise = (wx.valid && wx.sunriseMin >= 0) ? wx.sunriseMin : 7 * 60;
  int set = (wx.valid && wx.sunsetMin >= 0) ? wx.sunsetMin : 19 * 60 + 30;
  return m < rise || m >= set;
}

bool isBedtime() {
  int m = localMinutes();
  if (m < 0) return false;
  return m >= BEDTIME_MIN || m < WAKE_MIN;
}

float moonPhase() {
  uint32_t e = epoch();
  if (!e) return 0.5f;
  float days = (e - 947182440UL) / 86400.0f;  // a known new moon, 2000-01-06
  float p = fmodf(days, 29.530588f) / 29.530588f;
  return p < 0 ? p + 1 : p;
}

bool hot() { return wx.valid && wx.tempC >= HOT_C; }
bool cold() { return wx.valid && wx.tempC <= COLD_C; }
bool windy() { return wx.valid && wx.windKmh >= WINDY_KMH; }

const char *kindName(WeatherKind k) {
  static const char *const N[] = {"unknown", "clear", "cloudy", "fog", "drizzle", "rain", "snow", "storm"};
  return k <= W_STORM ? N[k] : "?";
}

}  // namespace Net
