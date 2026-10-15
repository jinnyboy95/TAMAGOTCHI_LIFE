#include "imu.h"
#include <Wire.h>
#include "config.h"

namespace Imu {

// ---- LSM6DS3 registers ------------------------------------------------------
static const uint8_t ADDR = 0x6A;
static const uint8_t WHO_AM_I = 0x0F;
static const uint8_t CTRL1_XL = 0x10;
static const uint8_t CTRL2_G = 0x11;
static const uint8_t CTRL3_C = 0x12;
static const uint8_t TAP_SRC = 0x1C;
static const uint8_t OUT_TEMP_L = 0x20;
static const uint8_t OUTX_L_G = 0x22;  // gyro xyz then accel xyz, 12 bytes
static const uint8_t TAP_CFG0_DSO = 0x56;
static const uint8_t TAP_CFG1_DSO = 0x57;
static const uint8_t TAP_CFG2 = 0x58;  // TAP_CFG on the DS3, TAP_CFG2 on the DSO
static const uint8_t TAP_THS_6D = 0x59;
static const uint8_t INT_DUR2 = 0x5A;
static const uint8_t WAKE_UP_THS = 0x5B;
static const uint8_t MD1_CFG = 0x5E;

static const float ACC_LSB = 0.000122f;  // g/LSB at +-4g
static const float GYR_LSB = 0.070f;     // dps/LSB at 2000dps

// ---- public state -------------------------------------------------------------
bool ok = false;
Vec2 grav = {0, 1};
Vec2 force = {0, 1};
Vec2 dyn = {0, 0};
float facing = 0;
float shake = 0;
float jitter = 0;
float spin = 0;
float tempC = 0;
bool shaking = false, falling = false, held = false;
Orient orient = OR_NORMAL;
uint32_t steps = 0;
uint32_t lastMotionMs = 0;

// ---- private ------------------------------------------------------------------
static uint8_t chipId = 0;
static float acc[3], accLP[3] = {0, 0, 1}, gyr[3], gyrBias[3];
static float calD[3] = {0, -1, 0}, calF[3] = {0, 0, 1}, calR[3] = {1, 0, 0};
static bool calOk = false;
static uint32_t lastUs = 0;

static Event queue[8];
static uint8_t qHead = 0, qTail = 0;

static uint32_t shakeOnMs = 0, shakeOffMs = 0;
static uint32_t fallMs = 0;
static uint32_t spinMs = 0, lastSpinEv = 0;
static float heldAcc = 0;
static uint32_t quietMs = 0;
static uint32_t pendingTapMs = 0;
static Orient orientCand = OR_NORMAL;
static uint32_t orientCandMs = 0;
static float stepMag = 1;
static bool stepAbove = false;
static uint32_t lastStepMs = 0;
static uint16_t stepRun = 0;
static uint32_t stableSince = 0;
static uint32_t lastTempMs = 0;

static void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static uint8_t readReg(uint8_t reg) {
  uint8_t v = 0;
  readRegs(reg, &v, 1);
  return v;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void norm3(float *v) {
  float m = sqrtf(dot3(v, v));
  if (m < 1e-6f) return;
  v[0] /= m;
  v[1] /= m;
  v[2] /= m;
}

static void cross3(const float *a, const float *b, float *out) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

void pushEvent(Event e) {
  uint8_t next = (qHead + 1) & 7;
  if (next == qTail) return;  // full: drop, the pet has enough going on
  queue[qHead] = e;
  qHead = next;
}

bool pollEvent(Event &e) {
  if (qTail == qHead) return false;
  e = queue[qTail];
  qTail = (qTail + 1) & 7;
  return true;
}

uint32_t stillMs() { return millis() - lastMotionMs; }

float accMag() { return sqrtf(dot3(accLP, accLP)); }

bool calibrated() { return calOk; }

void setCalibration(const float D[3], const float F[3]) {
  for (int i = 0; i < 3; i++) {
    calD[i] = D[i];
    calF[i] = F[i];
  }
  // Make F orthogonal to D, then R = F x D (screen right).
  float k = dot3(calF, calD);
  for (int i = 0; i < 3; i++) calF[i] -= k * calD[i];
  norm3(calD);
  norm3(calF);
  cross3(calF, calD, calR);
  norm3(calR);
  calOk = true;
}

void getCalibration(float D[3], float F[3]) {
  for (int i = 0; i < 3; i++) {
    D[i] = calD[i];
    F[i] = calF[i];
  }
}

bool stableVec(float out[3]) {
  float m = sqrtf(dot3(accLP, accLP));
  // Accel-only: calibration just needs gravity, and a bad gyro bias must not
  // be able to block the hatch ritual.
  bool st = jitter < 0.035f && m > 0.85f && m < 1.15f;
  uint32_t now = millis();
  if (!st) {
    stableSince = 0;
    return false;
  }
  if (!stableSince) stableSince = now;
  if (now - stableSince < 1000) return false;
  for (int i = 0; i < 3; i++) out[i] = accLP[i] / m;
  return true;
}

bool begin() {
  // 0x69 = LSM6DS3, 0x6A = LSM6DS3TR-C, 0x6C = LSM6DSO/DSOX (newer Nano 33 IoT
  // revisions). The chip can be slow to answer right after power-up.
  for (uint8_t tries = 0; tries < 5; tries++) {
    chipId = readReg(WHO_AM_I);
    if (chipId == 0x69 || chipId == 0x6A || chipId == 0x6C) break;
    delay(50);
  }
  Serial.print(F("[imu] WHO_AM_I=0x"));
  Serial.println(chipId, HEX);
  if (chipId != 0x69 && chipId != 0x6A && chipId != 0x6C) {
    ok = false;
    return false;
  }
  writeReg(CTRL3_C, 0x01);  // software reset
  delay(20);
  writeReg(CTRL1_XL, 0x68);  // 416 Hz, +-4g
  writeReg(CTRL2_G, 0x4C);   // 104 Hz, 2000 dps
  writeReg(CTRL3_C, 0x44);   // BDU + auto-increment
  // Hardware tap engine: XYZ taps, latched. TR-C also needs INTERRUPTS_ENABLE.
  if (chipId == 0x6C) {
    // DSO splits tap config over TAP_CFG0..2 with a threshold per axis.
    writeReg(TAP_CFG0_DSO, 0x0F);                           // tap XYZ, latched
    writeReg(TAP_CFG1_DSO, TAP_THRESHOLD & 0x1F);           // X threshold
    writeReg(TAP_CFG2, 0x80 | (TAP_THRESHOLD & 0x1F));      // INTERRUPTS_ENABLE + Y
    writeReg(TAP_THS_6D, TAP_THRESHOLD & 0x1F);             // Z threshold
  } else {
    writeReg(TAP_CFG2, chipId == 0x6A ? 0x8F : 0x0F);
    writeReg(TAP_THS_6D, TAP_THRESHOLD & 0x1F);
  }
  writeReg(INT_DUR2, 0x7F);     // generous double-tap window
  writeReg(WAKE_UP_THS, 0x80);  // enable single + double tap
  writeReg(MD1_CFG, 0x48);      // route single/double tap to INT1 (keeps TAP_SRC fed)
  delay(50);

  // Seed filters and gyro bias assuming we're at rest on boot.
  // The first samples after the gyro powers up are junk, so skip them.
  float sumG[3] = {0, 0, 0};
  uint8_t buf[12];
  uint8_t got = 0;
  for (int n = 0; n < 42; n++) {
    if (readRegs(OUTX_L_G, buf, 12) && n >= 10) {
      got++;
      for (int i = 0; i < 3; i++) {
        sumG[i] += (int16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8)) * GYR_LSB;
        accLP[i] = (int16_t)(buf[6 + i * 2] | (buf[6 + i * 2 + 1] << 8)) * ACC_LSB;
      }
    }
    delay(5);
  }
  for (int i = 0; i < 3; i++) gyrBias[i] = got ? sumG[i] / got : 0;
  lastUs = micros();
  lastMotionMs = millis();
  ok = true;
  return true;
}

static void setOrient(Orient o, uint32_t now) {
  if (o != orientCand) {
    orientCand = o;
    orientCandMs = now;
    return;
  }
  if (o == orient || now - orientCandMs < 400) return;
  Orient prev = orient;
  orient = o;
  if (prev == OR_FACEDOWN) pushEvent(EV_FACE_UP);
  if (prev == OR_UPSIDE) pushEvent(EV_RIGHT_SIDE_UP);
  if (o == OR_FACEDOWN) pushEvent(EV_FACE_DOWN);
  if (o == OR_UPSIDE) pushEvent(EV_UPSIDE_DOWN);
}

void update(uint32_t now) {
  if (!ok) return;
  uint32_t us = micros();
  float dt = (us - lastUs) / 1e6f;
  lastUs = us;
  if (dt <= 0 || dt > 0.2f) dt = 0.02f;

  uint8_t buf[12];
  if (!readRegs(OUTX_L_G, buf, 12)) return;
  for (int i = 0; i < 3; i++) {
    gyr[i] = (int16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8)) * GYR_LSB;
    acc[i] = (int16_t)(buf[6 + i * 2] | (buf[6 + i * 2 + 1] << 8)) * ACC_LSB;
  }

  // ---- filters ----
  float kLP = dt / (0.12f + dt);
  float d[3];
  for (int i = 0; i < 3; i++) {
    accLP[i] += (acc[i] - accLP[i]) * kLP;
    d[i] = acc[i] - accLP[i];
  }
  float dMag = sqrtf(dot3(d, d));
  jitter += (dMag - jitter) * (dt / (0.5f + dt));
  shake += (dMag - shake) * (dt / (0.15f + dt));

  float gw[3];
  for (int i = 0; i < 3; i++) gw[i] = gyr[i] - gyrBias[i];
  spin = sqrtf(dot3(gw, gw));
  // Track bias whenever the accel says we're still. No spin gate: a bad boot
  // bias would otherwise read as >20 dps forever and never get corrected.
  if (jitter < STILL_JITTER_G) {
    for (int i = 0; i < 3; i++) gyrBias[i] += (gyr[i] - gyrBias[i]) * (dt / (2.0f + dt));
  }

  // ---- project into screen space (gravity = -accel) ----
  grav.x = -dot3(accLP, calR);
  grav.y = -dot3(accLP, calD);
  facing = -dot3(accLP, calF);
  force.x = -dot3(acc, calR);
  force.y = -dot3(acc, calD);
  dyn.x = dot3(d, calR);
  dyn.y = dot3(d, calD);

  float mag = sqrtf(dot3(acc, acc));
  bool moved = jitter > MOTION_JITTER_G || spin > 25;
  if (moved) lastMotionMs = now;

  // ---- shake ----
  if (!shaking) {
    if (shake > SHAKE_START_G) {
      if (!shakeOnMs) shakeOnMs = now;
      if (now - shakeOnMs > SHAKE_START_MS) {
        shaking = true;
        shakeOffMs = 0;
        pushEvent(EV_SHAKE_START);
      }
    } else {
      shakeOnMs = 0;
    }
  } else {
    if (shake < SHAKE_END_G) {
      if (!shakeOffMs) shakeOffMs = now;
      if (now - shakeOffMs > SHAKE_END_MS) {
        shaking = false;
        shakeOnMs = 0;
        pushEvent(EV_SHAKE_END);
      }
    } else {
      shakeOffMs = 0;
    }
  }

  // ---- free fall (toss / drop) ----
  if (!falling) {
    if (mag < FREEFALL_G && !shaking) {
      if (!fallMs) fallMs = now;
      if (now - fallMs > FREEFALL_MS) {
        falling = true;
        pushEvent(EV_FREEFALL);
      }
    } else {
      fallMs = 0;
    }
  } else if (mag > 0.8f) {
    falling = false;
    fallMs = 0;
    pushEvent(EV_LANDED);
  }

  // ---- spin ----
  if (spin > SPIN_DPS) {
    if (!spinMs) spinMs = now;
    if (now - spinMs > SPIN_MS && now - lastSpinEv > 3000) {
      lastSpinEv = now;
      pushEvent(EV_SPIN);
    }
  } else {
    spinMs = 0;
  }

  // ---- held in a hand vs sitting on a table ----
  bool handish = !shaking && !falling && (jitter > STILL_JITTER_G || spin > HELD_GYRO_DPS);
  if (handish) {
    heldAcc += dt;
    quietMs = 0;
  } else {
    heldAcc = max(0.0f, heldAcc - dt * 0.5f);
    if (!quietMs) quietMs = now;
  }
  if (!held && heldAcc * 1000 > HELD_MS) {
    held = true;
    pushEvent(EV_PICKED_UP);
  } else if (held && quietMs && now - quietMs > PUTDOWN_MS) {
    held = false;
    heldAcc = 0;
    pushEvent(EV_PUT_DOWN);
  }

  // ---- orientation ----
  Orient o = OR_NORMAL;
  if (facing > 0.75f) o = OR_FACEDOWN;
  else if (facing < -0.75f) o = OR_FLAT;
  else if (grav.y < -0.55f) o = OR_UPSIDE;
  if (!shaking && !falling) setOrient(o, now);

  // ---- software pedometer ----
  stepMag += (mag - stepMag) * (dt / (0.08f + dt));
  if (!shaking && !falling) {
    if (!stepAbove && stepMag > STEP_PEAK_G) {
      stepAbove = true;
      uint32_t gap = now - lastStepMs;
      if (gap > 280 && gap < 1500) {
        stepRun++;
        steps++;
        if (stepRun == STEP_BURST || (stepRun > STEP_BURST && stepRun % 60 == 0)) pushEvent(EV_WALKIES);
      } else if (gap >= 1500) {
        stepRun = 1;
      }
      lastStepMs = now;
    } else if (stepAbove && stepMag < 1.0f) {
      stepAbove = false;
    }
  }
  if (now - lastStepMs > 3000) stepRun = 0;

  // ---- hardware taps (single tap is delayed so a double tap can cancel it) ----
  uint8_t tap = readReg(TAP_SRC);
  if (!shaking && !falling) {
    if (tap & 0x10) {  // DOUBLE_TAP
      pendingTapMs = 0;
      lastMotionMs = now;
      pushEvent(EV_DOUBLE_TAP);
    } else if (tap & 0x20) {  // SINGLE_TAP
      if (!pendingTapMs) pendingTapMs = now;
    }
  }
  if (pendingTapMs && now - pendingTapMs > 350) {
    pendingTapMs = 0;
    lastMotionMs = now;
    pushEvent(EV_TAP);
  }

  // ---- temperature (board-warmed, so only good for "relative" vibes) ----
  if (now - lastTempMs > 5000) {
    lastTempMs = now;
    uint8_t t[2];
    if (readRegs(OUT_TEMP_L, t, 2)) {
      int16_t raw = (int16_t)(t[0] | (t[1] << 8));
      tempC = 25.0f + raw / (chipId == 0x69 ? 16.0f : 256.0f);
    }
  }
}

}  // namespace Imu
