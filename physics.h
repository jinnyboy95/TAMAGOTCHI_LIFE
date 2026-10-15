#pragma once
#include <Adafruit_GFX.h>
#include "config.h"
#include "types.h"

// The little world inside the screen. Everything falls toward *real* down.
namespace World {

// floor: 0 = bottom, 1 = left, 2 = top, 3 = right (wall the pet stands on)
struct Body {
  Vec2 p, v;
  uint8_t floor;
  bool grounded;
  bool slipping;  // standing but the tilt is too steep to grip
  float squash;   // 0..0.5, relaxes over time
  Vec2 target;
  bool walking;
  float walkSpeed;
  bool ghost;     // may leave the screen sideways
  float airTime;
};

enum PType : uint8_t { P_NONE, P_RAIN, P_SNOW, P_PUKE, P_STAR, P_DRIP, P_HEART, P_ZZZ, P_CRUMB, P_WIND, P_SPARK };

struct Particle {
  float x, y, vx, vy, life;
  uint8_t type;
};

struct Puddle {
  float x, y, vx, vy;
  uint8_t size;
  bool active;
};

struct Food {
  float x, y, vx, vy;
  bool active, grounded, carried;
};

extern Body pet;
extern Particle parts[MAX_PARTICLES];
extern Puddle puddles[MAX_PUDDLES];
extern Food food;
extern uint8_t snowPile[SCREEN_W];
extern GFXcanvas1 graffiti;
extern Vec2 g;           // effective gravity in screen space (g units)
extern bool flipped;     // "rotate the UI 180" prank
extern float rainRate;   // particles / second
extern float snowRate;
extern float windAccel;  // px/s^2, + = blowing right

void begin();
void update(float dt);

Vec2 rot(uint8_t floor, float lx, float ly);  // pet-local -> screen direction
Vec2 up();
void spawn(PType t, float x, float y, float vx, float vy, float life);
void burst(PType t, float x, float y, int n, float speed, float life);
void spawnFood();
void walkTo(float x, float y, float speed = WALK_SPEED);
void walkStop();
bool arrived();
void hop(float strength);
void fling(float vx, float vy);
bool offscreen();
void addPuddle(float x, float y);
int puddleCount();
void clearPuddles();
void eraseGraffiti(float amount);
void clearAll();
void clampToScreen(float &x, float &y, float margin);

}  // namespace World
