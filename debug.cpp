#include "debug.h"
#include <Arduino.h>
#include <Wire.h>
#include "brain.h"
#include "imu.h"
#include "net.h"
#include "physics.h"
#include "render.h"
#include "save.h"

extern "C" char *sbrk(int incr);

namespace Debug {

static char line[48];
static uint8_t len = 0;

static void i2c() {
  Serial.print(F("[i2c] found:"));
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F(" 0x"));
      Serial.print(a, HEX);
    }
  }
  Serial.println();
  for (uint8_t a = 0x6A; a <= 0x6B; a++) {
    Wire.beginTransmission(a);
    Wire.write(0x0F);
    uint8_t err = Wire.endTransmission(false);
    uint8_t n = Wire.requestFrom(a, (uint8_t)1);
    Serial.print(F("  0x"));
    Serial.print(a, HEX);
    Serial.print(F(" WHO_AM_I err="));
    Serial.print(err);
    Serial.print(F(" n="));
    Serial.print(n);
    Serial.print(F(" val=0x"));
    Serial.println(n ? Wire.read() : 0, HEX);
  }
  if (!Imu::ok) Serial.println(Imu::begin() ? F("  imu retry: ok") : F("  imu retry: failed"));
}

static void help() {
  Serial.println(F("commands:"));
  Serial.println(F("  stat                      pet + sensor status"));
  Serial.println(F("  i2c                       scan the bus, re-probe the IMU"));
  Serial.println(F("  oled                      re-init the screen + test pattern"));
  Serial.println(F("  w clear|cloudy|fog|drizzle|rain|snow|storm|hot|cold|wind|calm|real"));
  Serial.println(F("  t HH:MM | t off           fake the local time"));
  Serial.println(F("  e tap|dtap|shake|calm|spin|toss|land|upside|upright|facedown|faceup|held|putdown|walk"));
  Serial.println(F("  n hunger|energy|fun|nausea|trust|grudge <0-100>"));
  Serial.println(F("  b <behavior>              force a behavior (b list)"));
  Serial.println(F("  feral on|off  name <x>  outside  save  wipe  recal"));
}

static void setWeather(const char *k) {
  Net::Weather &w = Net::wx;
  if (!strcmp(k, "real")) {
    Net::fakeWeather = false;
    w.fetchedMs = 0;
    Serial.println(F("weather back to the internet's opinion"));
    return;
  }
  Net::fakeWeather = true;
  if (!w.valid) {
    w = {true, W_CLEAR, 15, 5, 270, 0, true, 7 * 60, 19 * 60, millis()};
  }
  w.valid = true;
  if (!strcmp(k, "hot")) w.tempC = 35;
  else if (!strcmp(k, "cold")) w.tempC = -5;
  else if (!strcmp(k, "wind")) w.windKmh = 50;
  else if (!strcmp(k, "calm")) {
    w.windKmh = 5;
    w.tempC = 15;
  } else {
    static const char *const K[] = {"unknown", "clear", "cloudy", "fog", "drizzle", "rain", "snow", "storm"};
    for (int i = 0; i < 8; i++)
      if (!strcmp(k, K[i])) w.kind = (WeatherKind)i;
    w.precipMm = (w.kind == W_RAIN || w.kind == W_SNOW) ? 1.5f : 0;
  }
  Serial.print(F("weather: "));
  Serial.println(Net::kindName(w.kind));
}

static void event(const char *k) {
  static const char *const N[] = {"", "tap", "dtap", "shake", "calm", "spin", "toss", "land", "upside", "upright",
                                  "facedown", "faceup", "held", "putdown", "walk"};
  for (int i = 1; i < (int)(sizeof(N) / sizeof(N[0])); i++) {
    if (!strcmp(k, N[i])) {
      Imu::pushEvent((Event)i);
      return;
    }
  }
  Serial.println(F("unknown event"));
}

static void need(const char *k, float v) {
  PetState &s = Brain::s;
  if (!strcmp(k, "hunger")) s.hunger = v;
  else if (!strcmp(k, "energy")) s.energy = v;
  else if (!strcmp(k, "fun")) s.fun = v;
  else if (!strcmp(k, "nausea")) s.nausea = v;
  else if (!strcmp(k, "trust")) s.trust = v;
  else if (!strcmp(k, "grudge")) s.grudge = v;
  else Serial.println(F("unknown need"));
}

static void sensors() {
  Serial.print(F("  oled ok=")); Serial.print(Render::ok);
  Serial.print(F(" imu ok=")); Serial.print(Imu::ok);
  Serial.print(F(" cal=")); Serial.print(Imu::calibrated());
  Serial.print(F(" grav=")); Serial.print(Imu::grav.x, 2); Serial.print(','); Serial.print(Imu::grav.y, 2);
  Serial.print(F(" facing=")); Serial.print(Imu::facing, 2);
  Serial.print(F(" orient=")); Serial.println(Imu::orient);
  Serial.print(F("  shake=")); Serial.print(Imu::shake, 3);
  Serial.print(F(" jitter=")); Serial.print(Imu::jitter, 4);
  Serial.print(F(" spin=")); Serial.print(Imu::spin, 1);
  Serial.print(F(" |a|=")); Serial.print(Imu::accMag(), 3);
  Serial.print(F(" held=")); Serial.print(Imu::held);
  Serial.print(F(" still=")); Serial.print(Imu::stillMs() / 1000);
  Serial.print(F("s temp=")); Serial.println(Imu::tempC, 1);
  Serial.print(F("  wifi=")); Serial.print(Net::connected());
  Serial.print(F(" rssi=")); Serial.print(Net::rssi());
  Serial.print(F(" time=")); Serial.print(Net::localMinutes());
  Serial.print(F(" dark=")); Serial.print(Net::isDark());
  Serial.print(F(" bed=")); Serial.print(Net::isBedtime());
  Serial.print(F(" wx=")); Serial.print(Net::kindName(Net::wx.kind));
  Serial.print(F(" valid=")); Serial.print(Net::wx.valid);
  Serial.print(F(" fake=")); Serial.println(Net::fakeWeather);
  char top;
  Serial.print(F("  free ram ~")); Serial.println(&top - reinterpret_cast<char *>(sbrk(0)));
}

static void run(char *cmd) {
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = 0;
  char *arg2 = arg ? strchr(arg, ' ') : nullptr;
  if (arg2) *arg2++ = 0;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) help();
  else if (!strcmp(cmd, "i2c")) i2c();
  else if (!strcmp(cmd, "oled")) Render::test();
  else if (!strcmp(cmd, "stat")) {
    Brain::printStatus();
    sensors();
  } else if (!strcmp(cmd, "w") && arg) setWeather(arg);
  else if (!strcmp(cmd, "t") && arg) {
    if (!strcmp(arg, "off")) Net::fakeMinutes = -1;
    else Net::fakeMinutes = atoi(arg) * 60 + (strchr(arg, ':') ? atoi(strchr(arg, ':') + 1) : 0);
    Serial.print(F("local minutes = "));
    Serial.println(Net::localMinutes());
  } else if (!strcmp(cmd, "e") && arg) event(arg);
  else if (!strcmp(cmd, "n") && arg && arg2) need(arg, atof(arg2));
  else if (!strcmp(cmd, "b") && arg) {
    if (!strcmp(arg, "list")) {
      for (int i = 0; i < B_COUNT; i++) {
        Serial.print(Brain::behaviorName((Behavior)i));
        Serial.print(i % 8 == 7 ? '\n' : ' ');
      }
      Serial.println();
      return;
    }
    int b = Brain::behaviorByName(arg);
    if (b < 0) Serial.println(F("unknown behavior (b list)"));
    else Brain::start((Behavior)b);
  } else if (!strcmp(cmd, "feral") && arg) Brain::s.feral = !strcmp(arg, "on");
  else if (!strcmp(cmd, "name") && arg) strlcpy(Brain::s.name, arg, sizeof(Brain::s.name));
  else if (!strcmp(cmd, "outside")) Brain::start(B_GO_OUTSIDE);
  else if (!strcmp(cmd, "save")) Save::saveNow();
  else if (!strcmp(cmd, "wipe") || !strcmp(cmd, "recal")) {
    Save::wipe();
    Serial.println(F("wiped. rebooting into a fresh egg..."));
    delay(200);
    NVIC_SystemReset();
  } else Serial.println(F("? (try help)"));
}

void poll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = 0;
      if (len) run(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

}  // namespace Debug
