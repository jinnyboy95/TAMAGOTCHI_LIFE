#include "physics.h"
#include "brain.h"
#include "imu.h"

namespace World {

Body pet;
Particle parts[MAX_PARTICLES];
Puddle puddles[MAX_PUDDLES];
Food food;
uint8_t snowPile[SCREEN_W];
GFXcanvas1 graffiti(SCREEN_W, SCREEN_H);
Vec2 g = {0, 1};
bool flipped = false;
float rainRate = 0, snowRate = 0, windAccel = 0;

static float rainAcc = 0, snowAcc = 0, windAcc = 0;
static const float H = PET_HALF;

Vec2 rot(uint8_t f, float lx, float ly) {
  switch (f & 3) {
    case 1: return {-ly, lx};
    case 2: return {-lx, -ly};
    case 3: return {ly, -lx};
    default: return {lx, ly};
  }
}

Vec2 up() { return rot(pet.floor, 0, -1); }

void begin() {
  memset(&pet, 0, sizeof(pet));
  pet.p = {64, SCREEN_H - 1 - H};
  pet.target = pet.p;
  pet.walkSpeed = WALK_SPEED;
  clearAll();
}

void clearAll() {
  memset(parts, 0, sizeof(parts));
  memset(puddles, 0, sizeof(puddles));
  memset(&food, 0, sizeof(food));
  memset(snowPile, 0, sizeof(snowPile));
  graffiti.fillScreen(0);
}

void spawn(PType t, float x, float y, float vx, float vy, float life) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (parts[i].type == P_NONE) {
      parts[i] = {x, y, vx, vy, life, (uint8_t)t};
      return;
    }
  }
}

void burst(PType t, float x, float y, int n, float speed, float life) {
  for (int i = 0; i < n; i++) {
    float a = frand(0, 6.2832f);
    float s = frand(0.3f, 1.0f) * speed;
    spawn(t, x, y, cosf(a) * s, sinf(a) * s, life * frand(0.6f, 1.2f));
  }
}

void spawnFood() {
  food.active = true;
  food.grounded = false;
  food.carried = false;
  // drop in from the "sky" side, whatever that is right now
  Vec2 u = up();
  food.x = 64 + u.x * 28 + frand(-30, 30) * fabsf(u.y);
  food.y = 32 + u.y * 28 + frand(-12, 12) * fabsf(u.x);
  clampToScreen(food.x, food.y, 3);
  food.vx = frand(-20, 20);
  food.vy = 0;
}

void walkTo(float x, float y, float speed) {
  pet.target = {x, y};
  pet.walking = true;
  pet.walkSpeed = speed;
}

void walkStop() { pet.walking = false; }

bool arrived() { return !pet.walking; }

void hop(float s) {
  Vec2 u = up();
  pet.v.x += u.x * s;
  pet.v.y += u.y * s;
  pet.grounded = false;
}

void fling(float vx, float vy) {
  pet.v.x += vx;
  pet.v.y += vy;
  pet.grounded = false;
}

bool offscreen() { return pet.p.x < -H || pet.p.x > SCREEN_W + H; }

void clampToScreen(float &x, float &y, float m) {
  x = clampf(x, m, SCREEN_W - 1 - m);
  y = clampf(y, m, SCREEN_H - 1 - m);
}

void addPuddle(float x, float y) {
  clampToScreen(x, y, 2);
  int best = -1;
  float bestD = 1e9f;
  for (int i = 0; i < MAX_PUDDLES; i++) {
    if (!puddles[i].active) continue;
    float d = fabsf(puddles[i].x - x) + fabsf(puddles[i].y - y);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best >= 0 && bestD < 10) {
    if (puddles[best].size < 6) puddles[best].size++;
    return;
  }
  for (int i = 0; i < MAX_PUDDLES; i++) {
    if (!puddles[i].active) {
      puddles[i] = {x, y, 0, 0, 2, true};
      return;
    }
  }
  if (best >= 0 && puddles[best].size < 6) puddles[best].size++;
}

int puddleCount() {
  int n = 0;
  for (int i = 0; i < MAX_PUDDLES; i++) n += puddles[i].active;
  return n;
}

void clearPuddles() {
  for (int i = 0; i < MAX_PUDDLES; i++) puddles[i].active = false;
}

void eraseGraffiti(float amount) {
  int n = (int)(amount * 6);
  for (int i = 0; i < n; i++) graffiti.fillRect(random(0, SCREEN_W), random(0, SCREEN_H), 7, 4, 0);
}

// ---------------------------------------------------------------------------

static void updateGravity() {
  if (!Imu::ok || !Imu::calibrated()) {
    g = {0, 1};
    return;
  }
  Vec2 gs = Imu::grav;
  if (flipped) gs = {-gs.x, -gs.y};
  float m = vlen(gs);
  // Lying flat on a desk: fake a floor at the bottom so it still feels like a pet.
  float virt = clampf(1.0f - m / 0.45f, 0, 1);
  g = {gs.x, gs.y + virt * FLAT_GRAVITY_G};
}

static uint8_t floorFromGravity(uint8_t cur) {
  float ax = fabsf(g.x), ay = fabsf(g.y);
  if (ax < 0.15f && ay < 0.15f) return cur;  // weightless: keep whatever we had
  uint8_t cand = ay >= ax ? (g.y > 0 ? 0 : 2) : (g.x < 0 ? 1 : 3);
  if (cand == cur) return cur;
  // hysteresis so it doesn't flicker at 45 degrees
  float curComp = (cur == 0) ? g.y : (cur == 2) ? -g.y : (cur == 1) ? -g.x : g.x;
  float candComp = (cand == 0) ? g.y : (cand == 2) ? -g.y : (cand == 1) ? -g.x : g.x;
  return candComp > curComp + 0.15f ? cand : cur;
}

static void updatePet(float dt) {
  Vec2 a = {g.x * GRAVITY_PX, g.y * GRAVITY_PX};
  if (Imu::ok && Imu::calibrated()) {
    Vec2 d = Imu::dyn;
    if (flipped) d = {-d.x, -d.y};
    a.x -= d.x * SHAKE_PX;  // device jerks right -> pet sloshes left
    a.y -= d.y * SHAKE_PX;
  }
  a.x += windAccel;

  pet.floor = floorFromGravity(pet.floor);
  Vec2 t = rot(pet.floor, 1, 0);  // walking direction
  Vec2 n = rot(pet.floor, 0, 1);  // into the floor
  float gt = a.x * t.x + a.y * t.y;

  pet.v.x += a.x * dt;
  pet.v.y += a.y * dt;

  pet.slipping = false;
  if (pet.grounded) {
    float vt = pet.v.x * t.x + pet.v.y * t.y;
    float vn = pet.v.x * n.x + pet.v.y * n.y;
    bool canGrip = fabsf(gt) < 0.45f * GRAVITY_PX;
    float desired = 0;
    if (pet.walking) {
      float along = (pet.target.x - pet.p.x) * t.x + (pet.target.y - pet.p.y) * t.y;
      if (fabsf(along) < 1.5f) pet.walking = false;
      else desired = along > 0 ? pet.walkSpeed : -pet.walkSpeed;
    }
    if (canGrip) {
      // feet on the ground: either walk or stand still
      vt = pet.walking ? desired : vt * expf(-12 * dt);
    } else {
      pet.slipping = true;
      vt *= expf(-1.5f * dt);
    }
    pet.v.x = t.x * vt + n.x * vn;
    pet.v.y = t.y * vt + n.y * vn;
  }

  float sp = vlen(pet.v);
  if (sp > PET_MAX_SPEED) {
    pet.v.x *= PET_MAX_SPEED / sp;
    pet.v.y *= PET_MAX_SPEED / sp;
  }

  pet.p.x += pet.v.x * dt;
  pet.p.y += pet.v.y * dt;

  // walls
  float impact = 0;
  float minX = pet.ghost ? -40 : H, maxX = pet.ghost ? SCREEN_W + 40 : SCREEN_W - 1 - H;
  float minY = H, maxY = SCREEN_H - 1 - H;
  if (pet.p.x < minX) {
    pet.p.x = minX;
    if (pet.v.x < 0) {
      impact = max(impact, -pet.v.x);
      pet.v.x = -pet.v.x * BOUNCE_RESTITUTION;
    }
  } else if (pet.p.x > maxX) {
    pet.p.x = maxX;
    if (pet.v.x > 0) {
      impact = max(impact, pet.v.x);
      pet.v.x = -pet.v.x * BOUNCE_RESTITUTION;
    }
  }
  if (pet.p.y < minY) {
    pet.p.y = minY;
    if (pet.v.y < 0) {
      impact = max(impact, -pet.v.y);
      pet.v.y = -pet.v.y * BOUNCE_RESTITUTION;
    }
  } else if (pet.p.y > maxY) {
    pet.p.y = maxY;
    if (pet.v.y > 0) {
      impact = max(impact, pet.v.y);
      pet.v.y = -pet.v.y * BOUNCE_RESTITUTION;
    }
  }

  if (impact > 70) {
    pet.squash = min(0.45f, impact / 400.0f);
    Brain::onImpact(impact);
    if (impact > 140) burst(P_STAR, pet.p.x, pet.p.y, 3, 60, 0.4f);
  }
  pet.squash *= expf(-9 * dt);

  // standing on the floor?
  float distToFloor;
  switch (pet.floor) {
    case 1: distToFloor = pet.p.x - minX; break;
    case 2: distToFloor = pet.p.y - minY; break;
    case 3: distToFloor = maxX - pet.p.x; break;
    default: distToFloor = maxY - pet.p.y; break;
  }
  float vn = pet.v.x * n.x + pet.v.y * n.y;
  bool wasGrounded = pet.grounded;
  pet.grounded = distToFloor < 0.6f && fabsf(vn) < 35;
  if (pet.grounded) {
    // kill the tiny bounce so it rests
    pet.v.x -= n.x * vn;
    pet.v.y -= n.y * vn;
    pet.airTime = 0;
  } else {
    pet.airTime += dt;
  }
  if (!wasGrounded && pet.grounded && pet.squash < 0.1f) pet.squash = 0.12f;
}

static void emitWeather(float dt) {
  // spawn on the edge opposite gravity so rain always falls "down"
  Vec2 u = up();
  rainAcc += rainRate * dt;
  while (rainAcc >= 1) {
    rainAcc -= 1;
    float x = u.y != 0 ? frand(0, SCREEN_W) : (u.x < 0 ? 0 : SCREEN_W - 1);
    float y = u.x != 0 ? frand(0, SCREEN_H) : (u.y < 0 ? 0 : SCREEN_H - 1);
    spawn(P_RAIN, x, y, g.x * 90, g.y * 90, 2.0f);
  }
  snowAcc += snowRate * dt;
  while (snowAcc >= 1) {
    snowAcc -= 1;
    float x = u.y != 0 ? frand(0, SCREEN_W) : (u.x < 0 ? 0 : SCREEN_W - 1);
    float y = u.x != 0 ? frand(0, SCREEN_H) : (u.y < 0 ? 0 : SCREEN_H - 1);
    spawn(P_SNOW, x, y, g.x * 10, g.y * 10, 12.0f);
  }
  if (fabsf(windAccel) > 1) {
    windAcc += fabsf(windAccel) / 25 * dt;
    while (windAcc >= 1) {
      windAcc -= 1;
      float x = windAccel > 0 ? 0 : SCREEN_W - 1;
      spawn(P_WIND, x, frand(2, SCREEN_H - 2), windAccel > 0 ? 140 : -140, 0, 1.2f);
    }
  }
}

static void settlePile(int x) {
  for (int k = 0; k < 4; k++) {
    if (x > 0 && snowPile[x] > snowPile[x - 1] + 1) {
      snowPile[x]--;
      snowPile[--x]++;
    } else if (x < SCREEN_W - 1 && snowPile[x] > snowPile[x + 1] + 1) {
      snowPile[x]--;
      snowPile[++x]++;
    } else {
      break;
    }
  }
}

static void updateParticles(float dt) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle &p = parts[i];
    if (p.type == P_NONE) continue;
    p.life -= dt;
    if (p.life <= 0) {
      p.type = P_NONE;
      continue;
    }
    float gk = 0;
    switch (p.type) {
      case P_RAIN: gk = 1.5f; break;
      case P_PUKE: gk = 1.0f; break;
      case P_DRIP:
      case P_CRUMB: gk = 0.7f; break;
      case P_SNOW: gk = 0.06f; break;
      default: break;
    }
    p.vx += g.x * GRAVITY_PX * gk * dt;
    p.vy += g.y * GRAVITY_PX * gk * dt;
    if (p.type == P_SNOW) {
      p.vx += (sinf(t * 2 + i) * 6 + windAccel * 0.3f) * dt;
      p.vx = clampf(p.vx, -30, 30);
      p.vy = clampf(p.vy, -30, 30);
    } else if (p.type == P_RAIN) {
      p.vx += windAccel * 0.5f * dt;
    } else if (p.type == P_HEART || p.type == P_ZZZ) {
      p.vx = -g.x * 10 + sinf(t * 3 + i) * 6;
      p.vy = -g.y * 10;
    } else if (p.type == P_STAR || p.type == P_SPARK) {
      p.vx *= expf(-3 * dt);
      p.vy *= expf(-3 * dt);
    }
    p.x += p.vx * dt;
    p.y += p.vy * dt;

    bool out = p.x < 0 || p.x > SCREEN_W - 1 || p.y < 0 || p.y > SCREEN_H - 1;
    if (p.type == P_SNOW && pet.floor == 0 && p.y >= SCREEN_H - 1 - snowPile[(int)clampf(p.x, 0, SCREEN_W - 1)]) {
      int x = (int)clampf(p.x, 0, SCREEN_W - 1);
      if (snowPile[x] < 10) {
        snowPile[x]++;
        settlePile(x);
      }
      p.type = P_NONE;
      continue;
    }
    if (!out) continue;
    if (p.type == P_PUKE) addPuddle(p.x, p.y);
    if (p.type == P_RAIN && chance(0.3f)) {
      float x = p.x, y = p.y;
      clampToScreen(x, y, 1);
      spawn(P_CRUMB, x, y, frand(-25, 25), -g.y * 30 - g.x * 0, 0.2f);
    }
    p.type = P_NONE;
  }

  // Snow pile only lives on the bottom edge. Tilt it and it avalanches.
  if (g.y < 0.45f || fabsf(g.x) > 0.7f) {
    for (int k = 0; k < 6; k++) {
      int x = random(0, SCREEN_W);
      if (snowPile[x]) {
        snowPile[x]--;
        spawn(P_SNOW, x, SCREEN_H - 2 - snowPile[x], g.x * 40, g.y * 40, 6.0f);
      }
    }
  }
}

static void updateLoose(float &x, float &y, float &vx, float &vy, float r, float k, float dt, bool &grounded) {
  vx += g.x * GRAVITY_PX * k * dt;
  vy += g.y * GRAVITY_PX * k * dt;
  if (Imu::shaking) {
    Vec2 d = Imu::dyn;
    if (flipped) d = {-d.x, -d.y};
    vx -= d.x * SHAKE_PX * 0.8f * dt;
    vy -= d.y * SHAKE_PX * 0.8f * dt;
  }
  vx *= expf(-2 * dt);
  vy *= expf(-2 * dt);
  x += vx * dt;
  y += vy * dt;
  grounded = false;
  if (x < r) { x = r; vx = -vx * 0.3f; grounded |= g.x < -0.3f; }
  if (x > SCREEN_W - 1 - r) { x = SCREEN_W - 1 - r; vx = -vx * 0.3f; grounded |= g.x > 0.3f; }
  if (y < r) { y = r; vy = -vy * 0.3f; grounded |= g.y < -0.3f; }
  if (y > SCREEN_H - 1 - r) { y = SCREEN_H - 1 - r; vy = -vy * 0.3f; grounded |= g.y > 0.3f; }
}

static void updateObjects(float dt) {
  bool dummy;
  for (int i = 0; i < MAX_PUDDLES; i++) {
    Puddle &p = puddles[i];
    if (!p.active) continue;
    // a violent shake splatters puddles back into the air. gross.
    if (Imu::shake > 1.4f) {
      p.active = false;
      for (int k = 0; k < p.size * 2; k++) spawn(P_PUKE, p.x, p.y, frand(-90, 90), frand(-90, 90), 3);
      continue;
    }
    updateLoose(p.x, p.y, p.vx, p.vy, 2, 0.5f, dt, dummy);
  }
  if (food.active && !food.carried) updateLoose(food.x, food.y, food.vx, food.vy, 3, 1.0f, dt, food.grounded);
}

void update(float dt) {
  updateGravity();
  updatePet(dt);
  emitWeather(dt);
  updateParticles(dt);
  updateObjects(dt);
  if (Imu::shaking) eraseGraffiti(Imu::shake);
}

}  // namespace World
