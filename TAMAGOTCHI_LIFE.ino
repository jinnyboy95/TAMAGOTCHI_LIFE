// TAMAGOTCHI_LIFE - a chaotic little gremlin living in a 128x64 OLED.
// Board: Arduino Nano 33 IoT. Display: SSD1306 on SDA=A4 / SCL=A5 (0x3C).
//
// It senses: tilt, shakes, taps, spins, tosses, being held, being walked
// (onboard LSM6DS3), plus local weather, time and nearby WiFi names.
// It has needs, a personality, a memory, and grudges. It will act on them.
//
// Serial console at 115200: type "help".

#include <Wire.h>
#include "brain.h"
#include "config.h"
#include "debug.h"
#include "imu.h"
#include "net.h"
#include "physics.h"
#include "render.h"
#include "save.h"

static uint32_t lastMs = 0;

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) {
  }

  Wire.begin();
  Wire.setClock(I2C_CLOCK);
  Serial.print(F("[i2c] found:"));
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F(" 0x"));
      Serial.print(a, HEX);
    }
  }
  Serial.println();
  if (!Render::begin()) Serial.println(F("[oled] no SSD1306 at 0x3C - check wiring"));
  if (!Imu::begin()) Serial.println(F("[imu] LSM6DS3 not responding"));

  randomSeed(analogRead(A0) ^ ((uint32_t)analogRead(A1) << 10) ^ micros() ^ (uint32_t)(Imu::tempC * 1000));

  Net::begin();
  World::begin();
  bool havePet = Save::load();
  Brain::begin(havePet);

  Serial.println(F("[tamagotchi] alive. type 'help'."));
  lastMs = millis();
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastMs) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;  // after a blocking network call, don't teleport
  lastMs = now;

  Debug::poll();
  Imu::update(now);
  Brain::update(now, dt);
  World::update(dt);
  Save::service(now);
  Render::frame(now);

  uint32_t spent = millis() - now;
  if (spent < FRAME_MS) delay(FRAME_MS - spent);
}
