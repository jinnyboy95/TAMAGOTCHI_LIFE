#pragma once
#include "types.h"

// LSM6DS3 (onboard Nano 33 IoT IMU) + gesture detection, all in screen terms.
namespace Imu {

enum Orient : uint8_t { OR_NORMAL, OR_FLAT, OR_UPSIDE, OR_FACEDOWN };

extern bool ok;
extern Vec2 grav;        // low-passed gravity in the screen plane (g); +y = toward screen bottom
extern Vec2 force;       // instantaneous -accel in screen plane (g): what a loose object feels
extern Vec2 dyn;         // dynamic (shake) accel in screen plane (g)
extern float facing;     // gravity . screen-normal: -1 face up, +1 face down
extern float shake;      // smoothed shake energy (g)
extern float jitter;     // smoothed small motion (g)
extern float spin;       // gyro magnitude minus bias (dps)
extern float tempC;
extern bool shaking, falling, held;
extern Orient orient;
extern uint32_t steps;   // lifetime steps
extern uint32_t lastMotionMs;

bool begin();
void update(uint32_t now);
bool pollEvent(Event &e);
void pushEvent(Event e);
uint32_t stillMs();
float accMag();          // |low-passed accel| (g), ~1 at rest

// Calibration: D = screen-down, F = screen-front (toward viewer), IMU frame.
bool calibrated();
void setCalibration(const float D[3], const float F[3]);
void getCalibration(float D[3], float F[3]);
// True when the board has been resting at ~1g for a second; out = unit accel.
bool stableVec(float out[3]);

}  // namespace Imu
