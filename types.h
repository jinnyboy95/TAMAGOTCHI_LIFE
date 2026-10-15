#pragma once
#include <Arduino.h>

struct Vec2 {
  float x, y;
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float frand() { return random(0, 10001) / 10000.0f; }
inline float frand(float a, float b) { return a + (b - a) * frand(); }
inline bool chance(float p) { return frand() < p; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float vlen(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }

// Things the IMU noticed. Brain turns these into reactions.
enum Event : uint8_t {
  EV_NONE,
  EV_TAP,
  EV_DOUBLE_TAP,
  EV_SHAKE_START,
  EV_SHAKE_END,
  EV_SPIN,
  EV_FREEFALL,
  EV_LANDED,
  EV_UPSIDE_DOWN,
  EV_RIGHT_SIDE_UP,
  EV_FACE_DOWN,
  EV_FACE_UP,
  EV_PICKED_UP,
  EV_PUT_DOWN,
  EV_WALKIES,
};

enum WeatherKind : uint8_t {
  W_UNKNOWN,
  W_CLEAR,
  W_CLOUDY,
  W_FOG,
  W_DRIZZLE,
  W_RAIN,
  W_SNOW,
  W_STORM,
};
