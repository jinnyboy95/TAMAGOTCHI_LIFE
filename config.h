#pragma once
// ---------------------------------------------------------------------------
// TAMAGOTCHI_LIFE tunables. Everything you might want to tweak lives here.
// ---------------------------------------------------------------------------

// ---- Display / bus --------------------------------------------------------
#define OLED_ADDR          0x3C
#define I2C_CLOCK          400000UL
#define SCREEN_W           128
#define SCREEN_H           64
#define FRAME_MS           33        // ~30 fps cap

// ---- IMU axis calibration -------------------------------------------------
// On first boot the egg asks you to lay it flat, then stand it up, and prints
// the result to Serial. Paste those numbers here so re-uploads (which wipe
// flash) don't make you redo the ritual. Leave commented to always use the
// ritual / saved calibration.
// #define CAL_FIXED_D  { 0.0f, -1.0f, 0.0f }   // screen-down in IMU frame
// #define CAL_FIXED_F  { 0.0f,  0.0f, 1.0f }   // screen-front (toward you)

// ---- Motion thresholds (g / dps) ------------------------------------------
#define TAP_THRESHOLD      0x06      // LSM6DS3 TAP_THS (5 bit, 1 LSB = 0.125g at +-4g)
#define SHAKE_START_G      0.70f     // smoothed dynamic accel to count as shaking
#define SHAKE_END_G        0.25f
#define SHAKE_START_MS     250
#define SHAKE_END_MS       400
#define FREEFALL_G         0.30f     // |a| below this = falling
#define FREEFALL_MS        100
#define SPIN_DPS           250.0f
#define SPIN_MS            600
#define STILL_JITTER_G     0.010f    // below = sitting on a table
#define MOTION_JITTER_G    0.030f    // above = "someone touched me"
#define HELD_GYRO_DPS      3.0f
#define HELD_MS            1500
#define PUTDOWN_MS         3000
#define STEP_PEAK_G        1.12f
#define STEP_BURST         12        // consecutive steps before "walkies"

// ---- World physics --------------------------------------------------------
#define GRAVITY_PX         350.0f    // px/s^2 per g of in-plane gravity
#define SHAKE_PX           650.0f    // px/s^2 per g of shake (dynamic accel)
#define FLAT_GRAVITY_G     0.6f      // virtual "down" when lying flat
#define PET_HALF           11        // collision half-size (px)
#define PET_MAX_SPEED      260.0f
#define BOUNCE_RESTITUTION 0.55f
#define WALK_SPEED         22.0f
#define MAX_PARTICLES      56
#define MAX_PUDDLES        4

// ---- Needs (units per minute, 0..100 scale) -------------------------------
#define HUNGER_PER_MIN     0.35f     // ~4.5h from full to starving
#define ENERGY_DRAIN_MIN   0.25f
#define ENERGY_SLEEP_MIN   1.2f
#define FUN_DRAIN_MIN      0.6f      // only while ignored
#define GRUDGE_DECAY_MIN   0.5f
#define NAUSEA_DECAY_S     4.0f
#define PUKE_NAUSEA        65.0f
#define FERAL_AFTER_MIN    (6 * 60)  // ignored this long -> feral

// ---- Chaos ----------------------------------------------------------------
#define CHAOS_MIN_GAP_S    120       // min seconds between unprompted chaos
#define REVENGE_QUIET_S    20        // must be left alone this long to strike
#define REVENGE_MIN_GAP_S  180
#define CHAOS_DICE         0.06f     // base chance a decision ignores utility

// ---- Time / network -------------------------------------------------------
#define BEDTIME_MIN        (23 * 60) // local minutes
#define WAKE_MIN           (7 * 60)
#define WEATHER_EVERY_MIN  15
#define WEATHER_FORCE_MIN  25        // after this the pet stops procrastinating
#define GEO_EVERY_H        24
#define NTP_EVERY_H        6
#define SCAN_EVERY_MIN     40
#define WIFI_RETRY_MIN     3
#define HOT_C              30.0f
#define COLD_C             0.0f
#define WINDY_KMH          30.0f

// ---- Persistence ----------------------------------------------------------
#define SAVE_EVERY_MIN     15
#define SAVE_MIN_GAP_S     60        // rate limit for event-triggered saves
