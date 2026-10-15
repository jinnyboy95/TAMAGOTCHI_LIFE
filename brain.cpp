#include "brain.h"
#include <stdarg.h>
#include "config.h"
#include "imu.h"
#include "lines.h"
#include "net.h"
#include "physics.h"
#include "render.h"
#include "save.h"

namespace Brain {

PetState s;
Look look;
Run run;
uint16_t runGen = 0;
bool hatching = true;
char bubble[72];
uint32_t bubbleUntil = 0;
uint32_t flipUntil = 0, clockStolenUntil = 0;
int16_t snowmanX = -1;
uint8_t snowmanStage = 0;

static uint8_t hatchPhase = 0;
static float hatchT = 0;
static float calF[3];
static bool petExists = false;
static uint32_t lastChaos = 0, lastRevenge = 0, lastWeatherAct = 0, nextBolt = 0;
static uint32_t pokes[6];
static uint8_t pokeHead = 0;
static uint8_t feralCure = 0;
static uint32_t shakeStartMs = 0;
static bool wasAsleepAtShake = false;
static char laterBuf[72];
static uint32_t laterAt = 0;
static float neglectMin = 0;
static bool saidNoConfig = false;
static uint32_t lastSnowmanDecay = 0;

// ---------------------------------------------------------------------------
// talking
// ---------------------------------------------------------------------------
static void vsay(uint32_t ms, const char *fmt, va_list ap) {
  vsnprintf(bubble, sizeof(bubble), fmt, ap);
  bubbleUntil = millis() + (ms ? ms : 1600 + strlen(bubble) * 70);
}

void say(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay(0, fmt, ap);
  va_end(ap);
}

void sayFor(uint32_t ms, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay(ms, fmt, ap);
  va_end(ap);
}

// queue one line for after the current bubble (formatted now)
static void sayLater(uint32_t delayMs, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(laterBuf, sizeof(laterBuf), fmt, ap);
  va_end(ap);
  laterAt = millis() + delayMs;
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
void remember(Mem m) {
  s.mem[s.memHead] = {(uint8_t)m, millis()};
  s.memHead = (s.memHead + 1) & 7;
}

int recent(Mem m, uint32_t withinMs) {
  int n = 0;
  uint32_t now = millis();
  for (int i = 0; i < 8; i++)
    if (s.mem[i].type == m && now - s.mem[i].ms < withinMs) n++;
  return n;
}

Mem worstMemory() {
  // newest grudge-worthy memory
  for (int k = 1; k <= 8; k++) {
    const MemEntry &e = s.mem[(s.memHead + 8 - k) & 7];
    if (e.type == M_TOSSED || e.type == M_SHAKEN || e.type == M_WOKEN || e.type == M_POKED || e.type == M_SPUN)
      return (Mem)e.type;
  }
  return M_NONE;
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------
static void makeName(char *out) {
  snprintf(out, sizeof(s.name), "%s%s", PICK(L_NAME_A), PICK(L_NAME_B));
}

void newPet() {
  memset(&s, 0, sizeof(s));
  makeName(s.name);
  s.brave = random(5, 96);
  s.glutton = random(5, 96);
  s.dramatic = random(5, 96);
  s.spite = random(5, 96);
  s.lazy = random(5, 96);
  s.mischief = random(20, 96);  // it's a gremlin. floor of 20.
  s.hunger = 40;
  s.energy = 80;
  s.fun = 70;
  s.trust = 30;
  s.bornEpoch = Net::epoch();
  Serial.print(F("[pet] hatched "));
  Serial.println(s.name);
  Save::requestSave();
}

void rename() {
  char n[12];
  do {
    makeName(n);
  } while (!strcmp(n, s.name));
  strcpy(s.name, n);
  Save::requestSave();
}

void begin(bool haveSave) {
  petExists = haveSave;
  hatching = true;
  hatchT = 0;
  hatchPhase = (Imu::calibrated() || !Imu::ok) ? 2 : 0;
  run = {B_IDLE, 0, 0, 2, 0, 0, true};
  look = {EY_OPEN, MO_SMILE, 0, 0, false, false, false, 1, 0, 0};
}

void onTimeKnown(uint32_t epoch, uint32_t lastSeen) {
  if (!s.bornEpoch) s.bornEpoch = epoch;
  if (!lastSeen || epoch <= lastSeen) return;
  uint32_t mins = (epoch - lastSeen) / 60;
  if (mins > 7 * 24 * 60) mins = 7 * 24 * 60;
  if (mins < 10) return;
  s.hunger += mins * HUNGER_PER_MIN * 0.5f;
  s.fun -= mins * FUN_DRAIN_MIN * 0.5f;
  s.energy = 100;
  // the caller is about to talk about the weather, so wait a beat
  if (mins > 24 * 60) {
    s.feral = true;
    feralCure = 0;
    remember(M_IGNORED);
    sayLater(6000, "you left me for %luh.", (unsigned long)(mins / 60));
  } else if (mins > 60) {
    sayLater(6000, "where were you? %luh!", (unsigned long)(mins / 60));
  }
  s.hunger = clampf(s.hunger, 0, 100);
  s.fun = clampf(s.fun, 0, 100);
}

static bool isChaos(Behavior b) { return b >= B_FAKE_CRASH && b <= B_SPITE_PUKE; }
static bool isWeatherAct(Behavior b) { return b >= B_UMBRELLA && b <= B_SHIVER; }

void start(Behavior b, int16_t arg) {
  // clean up whatever was going on
  World::pet.ghost = false;
  World::walkStop();
  if (World::food.carried) {
    World::food.carried = false;
    World::food.x = World::pet.p.x;
    World::food.y = World::pet.p.y;
  }
  if (!hatching) Render::mode = Render::RM_WORLD;

  uint32_t now = millis();
  if (isChaos(b)) lastChaos = now;
  if (isWeatherAct(b)) lastWeatherAct = now;
  if (b == B_REVENGE) lastRevenge = now;

  run = {b, 0, 0, 0, 0, arg, true};
  runGen++;
  look.eyes = s.feral ? EY_SUS : EY_OPEN;
  look.mouth = s.grudge > 50 ? MO_FLAT : (s.fun < 30 ? MO_FROWN : MO_SMILE);
  look.props = s.accessory;
  look.lookX = 0;
  look.back = look.blush = look.hidden = false;
  look.scale = 1;
  look.shiver = look.melt = 0;
}

void finish() {
  start(B_IDLE, 1);
}

// ---------------------------------------------------------------------------
// decisions
// ---------------------------------------------------------------------------
static const Behavior CHAOS_LIST[] = {B_FAKE_CRASH, B_LEAVE, B_GRAFFITI, B_KNOCK, B_LIGHTS_OFF,
                                      B_STEAL_CLOCK, B_FLIP_UI, B_HICCUPS, B_ZOOMIES, B_RENAME};

static void addWeatherActs(void (*add)(Behavior, float)) {
  const Net::Weather &w = Net::wx;
  float coward = 100 - s.brave;
  switch (w.kind) {
    case W_DRIZZLE:
    case W_RAIN:
      add(B_UMBRELLA, 20 + s.lazy / 3.0f);
      add(B_RAIN_DANCE, 10 + s.mischief / 3.0f);
      add(B_SOAKED, 10 + (s.spite + s.dramatic) / 5.0f);
      break;
    case W_SNOW:
      add(B_SNOWMAN, 30);
      add(B_EAT_SNOW, 10 + s.glutton / 3.0f);
      break;
    case W_STORM:
      add(B_HIDE_STORM, 15 + coward / 2);
      add(B_TAUNT_STORM, 5 + s.brave / 2.0f);
      break;
    case W_CLEAR:
      if (Net::isDark()) add(B_HOWL, 25 + s.dramatic / 4.0f);
      else add(B_SUNBATHE, 35);
      break;
    case W_FOG: add(B_FOG_LOST, 35); break;
    default: break;
  }
  if (Net::windy()) add(B_WINDWALK, 30);
  if (Net::hot()) add(B_MELT, 35);
  if (Net::cold()) add(B_SHIVER, 30);
}

struct Cand {
  Behavior b;
  float w;
};
static Cand cands[24];
static uint8_t nCands;
static void addCand(Behavior b, float w) {
  if (w > 0 && nCands < 24) cands[nCands++] = {b, w};
}

void chooseNext() {
  uint32_t now = millis();
  nCands = 0;
  bool bed = Net::isBedtime();

  if (bed && chance(s.mischief / 500.0f) && s.energy > 30) {
    say(PICK(L_NOCTURNAL));
    start(B_ZOOMIES);
    return;
  }

  addCand(B_SLEEP, bed ? 120 - s.mischief * 0.4f : max(0.0f, 30 - s.energy) * 3);
  if (World::food.active) addCand(B_EAT, 30 + s.hunger + s.glutton / 3.0f - s.grudge / 2);
  if (s.hunger > 55) {
    if (s.stash) addCand(B_EAT_STASH, s.hunger);
    else addCand(B_BEG, (s.hunger - 55) * 2 + (s.feral ? 20 : 0));
  }
  addCand(B_IDLE, 25 + s.lazy / 4.0f);
  addCand(B_WANDER, 25 + (100 - s.lazy) / 5.0f);
  addCand(B_STARE, 3 + s.mischief / 25.0f + (s.feral ? 20 : 0));

  if (Net::configured()) {
    if (Net::everConnected && !Net::connected() && Net::retryDue()) addCand(B_SIGNAL_ANXIETY, 35);
    if (Net::weatherDue()) addCand(B_GO_OUTSIDE, Net::weatherOverdue() ? 400 : 30 + (100 - s.lazy) / 4.0f);
    if (Net::connected() && Net::scanDue()) addCand(B_GOSSIP, 15 + s.mischief / 5.0f);
  } else if (!saidNoConfig) {
    saidNoConfig = true;
    say(PICK(L_NOT_CONFIGURED));
  }

  if (Net::wx.valid && now - lastWeatherAct > 180000UL && !bed) addWeatherActs(addCand);

  if (s.grudge > 40 && Imu::stillMs() > REVENGE_QUIET_S * 1000UL && now - lastRevenge > REVENGE_MIN_GAP_S * 1000UL)
    addCand(B_REVENGE, s.grudge * (0.5f + s.spite / 100.0f));

  if (now - lastChaos > CHAOS_MIN_GAP_S * 1000UL && !bed) {
    float w = (100 - s.fun) / 4 + s.mischief / 5.0f;
    if (s.feral) w *= 2;
    addCand(PICK(CHAOS_LIST), w);
  }

  if (!nCands) {
    start(B_IDLE);
    return;
  }

  int pick = 0;
  if (chance(CHAOS_DICE + s.mischief / 1000.0f)) {
    pick = random(0, nCands);  // the dice say: ignore common sense
  } else {
    float total = 0;
    for (int i = 0; i < nCands; i++) total += cands[i].w * cands[i].w;
    float r = frand() * total;
    for (pick = 0; pick < nCands - 1; pick++) {
      r -= cands[pick].w * cands[pick].w;
      if (r <= 0) break;
    }
  }
  start(cands[pick].b);
}

static void decideFood() {
  if (priority(run.b) >= 2) return;  // busy; it'll notice the snack later
  float wEat = s.hunger + s.glutton / 2.0f + s.trust / 4.0f;
  float wRefuse = s.grudge + s.spite / 3.0f + (s.hunger < 20 ? 30 : 0) + (s.feral ? 25 : 0);
  float wHide = s.hunger < 40 ? s.glutton / 2.0f + s.mischief / 3.0f : 0;
  float wBat = s.mischief / 3.0f + (s.feral ? 30 : 0);
  float r = frand() * (wEat + wRefuse + wHide + wBat);
  if ((r -= wEat) < 0) start(B_EAT);
  else if ((r -= wRefuse) < 0) start(B_REFUSE);
  else if ((r -= wHide) < 0) start(B_HIDE_FOOD);
  else start(B_BAT_FOOD);
}

static void tryStart(Behavior b) {
  if (priority(b) >= priority(run.b)) start(b);
}

void cureFeral(uint8_t n) {
  if (!s.feral) return;
  feralCure += n;
  if (feralCure >= 4) {
    s.feral = false;
    say(PICK(L_UNFERAL));
  }
}

void react(Event e) {
  uint32_t now = millis();
  bool asleep = run.b == B_SLEEP;

  // the flip prank turns the world upside down; so do the meanings
  if (World::flipped) {
    if (e == EV_UPSIDE_DOWN) {
      say("smart.");
      return;
    }
    if (e == EV_RIGHT_SIDE_UP) e = EV_UPSIDE_DOWN;
  }

  switch (e) {
    case EV_SHAKE_START:
      shakeStartMs = now;
      wasAsleepAtShake = asleep;
      if (asleep) {
        remember(M_WOKEN);
        s.grudge += Net::isBedtime() ? 20 : 8;
      }
      start(B_BOUNCE);
      break;

    case EV_SHAKE_END: {
      if (run.b != B_BOUNCE) break;
      float secs = (now - shakeStartMs) / 1000.0f;
      remember(M_SHAKEN);
      bool fun = (s.mischief + s.brave) / 2 > 60;
      if (s.nausea >= PUKE_NAUSEA) {
        start(s.dramatic > 65 && chance(0.4f) ? B_FAINT : B_PUKE);
      } else if (s.nausea > 35) {
        start(B_DIZZY);
      } else if (fun && !wasAsleepAtShake) {
        s.fun += 15;
        say(PICK(L_SHAKE_OK));
        finish();
      } else {
        s.grudge += secs * 3 * (0.5f + s.spite / 100.0f);
        start(B_ANNOYED);
        say(wasAsleepAtShake ? PICK(L_WOKEN) : PICK(L_SHAKE_RUDE));
      }
      break;
    }

    case EV_TAP: {
      pokes[pokeHead] = now;
      pokeHead = (pokeHead + 1) % 6;
      int n = 0;
      for (int i = 0; i < 6; i++) n += pokes[i] && now - pokes[i] < 8000;
      if (asleep) {
        if (n >= 2) {
          remember(M_WOKEN);
          s.grudge += Net::isBedtime() ? 15 : 5;
          start(B_ANNOYED);
          say(PICK(L_WOKEN));
        } else {
          say("mmh..");
        }
      } else if (run.b == B_LIGHTS_OFF) {
        say("fine.");
        finish();
      } else if (run.b == B_SULK) {
        if (chance(s.spite / 100.0f)) start(B_CHOMP);
        else say(PICK(L_SULK));
      } else if (s.feral) {
        if (chance(0.5f)) start(B_CHOMP);
        else say(PICK(L_HISS));
      } else if (n >= 4 || (s.grudge > 60 && n >= 2)) {
        remember(M_POKED);
        s.grudge += 5;
        tryStart(s.brave > 35 ? B_CHOMP : B_ANNOYED);
      } else if (n >= 2) {
        say("what");
      } else if (priority(run.b) <= 1) {
        start(B_CURIOUS);
      }
      break;
    }

    case EV_DOUBLE_TAP:
      if (World::food.active) {
        say("there's already food");
        break;
      }
      World::spawnFood();
      if (asleep) break;  // it'll find it in the morning
      decideFood();
      break;

    case EV_SPIN:
      remember(M_SPUN);
      s.nausea += 25;
      tryStart(B_DIZZY);
      break;

    case EV_FREEFALL:
      s.tosses++;
      start(B_SCREAM);
      break;

    case EV_LANDED:
      if (run.b == B_SCREAM) start(B_SHOCKED);
      break;

    case EV_UPSIDE_DOWN:
      if (!asleep) tryStart(B_UPSIDE);
      break;

    case EV_RIGHT_SIDE_UP:
      if (run.b == B_UPSIDE) {
        say(PICK(L_RIGHTED));
        finish();
      }
      break;

    case EV_FACE_DOWN:
      tryStart(B_DARK);
      break;

    case EV_FACE_UP:
      if (run.b == B_DARK) start(B_LIGHTS_BACK, run.t < 4 ? 0 : ((s.energy < 50 || Net::isBedtime()) ? 1 : 2));
      break;

    case EV_PICKED_UP:
      if (!asleep && priority(run.b) <= 1 && run.b != B_HELD) {
        start(B_HELD);
        remember(M_HELD);
        cureFeral(1);
      }
      break;

    case EV_PUT_DOWN:
      if (run.b == B_HELD) {
        say(PICK(L_PUTDOWN));
        finish();
      }
      break;

    case EV_WALKIES:
      if (asleep) {
        say("...are we moving?");
        break;
      }
      remember(M_WALKED);
      cureFeral(2);
      tryStart(B_WALKIES);
      break;

    default: break;
  }
  s.grudge = clampf(s.grudge, 0, 100);
  s.nausea = clampf(s.nausea, 0, 100);
}

void onImpact(float speed) {
  s.nausea += speed / 45.0f;
  if (run.b == B_SLEEP && speed > 120) {
    remember(M_WOKEN);
    s.grudge += 5;
    start(B_ANNOYED);
    say(PICK(L_WOKEN));
  }
}

// ---------------------------------------------------------------------------
// per-frame
// ---------------------------------------------------------------------------
static void updateHatch(float dt) {
  Render::mode = Render::RM_EGG;
  hatchT += dt;
  float v[3];
  static uint32_t lastCalLog = 0;
  if (hatchPhase < 2 && millis() - lastCalLog > 1000) {
    lastCalLog = millis();
    Serial.print(F("[cal] phase ")); Serial.print(hatchPhase);
    Serial.print(F(hatchPhase ? " (stand me UP)" : " (lay me FLAT)"));
    Serial.print(F(" jitter=")); Serial.print(Imu::jitter, 4);
    Serial.print(F(" (<0.035) spin=")); Serial.print(Imu::spin, 1);
    Serial.print(F(" |a|=")); Serial.print(Imu::accMag(), 3);
    Serial.println(F(" (0.85-1.15)"));
  }
  switch (hatchPhase) {
    case 0:
      strcpy(Render::eggText, "lay me FLAT\nscreen facing up");
      if (Imu::stableVec(v)) {
        memcpy(calF, v, sizeof(calF));
        Serial.println(F("[cal] flat captured"));
        hatchPhase = 1;
      }
      break;
    case 1:
      strcpy(Render::eggText, "now stand me UP\nlike a photo");
      if (Imu::stableVec(v) && (v[0] * calF[0] + v[1] * calF[1] + v[2] * calF[2]) < 0.5f) {
        float D[3] = {-v[0], -v[1], -v[2]};
        Imu::setCalibration(D, calF);
        Imu::getCalibration(D, calF);
        Serial.println(F("[cal] paste into config.h to skip this next time:"));
        Serial.print(F("#define CAL_FIXED_D { "));
        Serial.print(D[0], 3); Serial.print(F("f, ")); Serial.print(D[1], 3); Serial.print(F("f, "));
        Serial.print(D[2], 3); Serial.println(F("f }"));
        Serial.print(F("#define CAL_FIXED_F { "));
        Serial.print(calF[0], 3); Serial.print(F("f, ")); Serial.print(calF[1], 3); Serial.print(F("f, "));
        Serial.print(calF[2], 3); Serial.println(F("f }"));
        Save::requestSave();
        hatchPhase = 2;
        hatchT = 0;
      }
      break;
    default:
      if (petExists) {
        hatching = false;
        Render::mode = Render::RM_WORLD;
        say(s.feral ? PICK(L_FERAL) : "i'm back.");
        start(B_IDLE);
        return;
      }
      strcpy(Render::eggText, petExists ? "" : "...something is\nhappening");
      Render::eggCrack = hatchT / 3.0f;
      if (hatchT > 3.5f) {
        newPet();
        petExists = true;
        hatching = false;
        Render::mode = Render::RM_WORLD;
        World::pet.p = {64, 20};
        World::hop(60);
        start(B_IDLE);
        say(PICK(L_HELLO), s.name);
        sayLater(5000, PICK(L_TIPS));
      }
      break;
  }
}

static void updateNeeds(float dt) {
  float m = dt / 60.0f;
  bool asleep = run.b == B_SLEEP;
  s.hunger += HUNGER_PER_MIN * m * (asleep ? 0.5f : 1.0f);
  s.energy += asleep ? ENERGY_SLEEP_MIN * m : -ENERGY_DRAIN_MIN * m * (1 + s.lazy / 100.0f);
  if (Imu::stillMs() > 60000UL) s.fun -= FUN_DRAIN_MIN * m;
  else s.fun += 1.0f * m;
  if (Imu::shaking) s.nausea += Imu::shake * 12 * dt * (1.3f - s.brave / 200.0f);
  else s.nausea -= NAUSEA_DECAY_S * dt;
  s.grudge -= GRUDGE_DECAY_MIN * m * (1.5f - s.spite / 100.0f);

  // neglect only counts while it's awake and it isn't bedtime
  if (Imu::stillMs() < 2000) neglectMin = 0;
  else if (!asleep && !Net::isBedtime() && Imu::stillMs() > 60000UL) neglectMin += m;
  if (!s.feral && neglectMin > FERAL_AFTER_MIN) {
    s.feral = true;
    feralCure = 0;
    remember(M_IGNORED);
  }

  s.hunger = clampf(s.hunger, 0, 100);
  s.energy = clampf(s.energy, 0, 100);
  s.fun = clampf(s.fun, 0, 100);
  s.nausea = clampf(s.nausea, 0, 100);
  s.trust = clampf(s.trust, 0, 100);
  s.grudge = clampf(s.grudge, 0, 100);
}

static void updateWeatherWorld(uint32_t now) {
  const Net::Weather &w = Net::wx;
  World::rainRate = World::snowRate = World::windAccel = 0;
  if (!w.valid) return;
  switch (w.kind) {
    case W_DRIZZLE: World::rainRate = 10; break;
    case W_RAIN: World::rainRate = min(60.0f, 25 + w.precipMm * 10); break;
    case W_STORM: World::rainRate = 55; break;
    case W_SNOW: World::snowRate = min(20.0f, 6 + w.precipMm * 4); break;
    default: break;
  }
  if (Net::windy()) {
    // wind FROM the east blows toward the west (screen left)
    float dir = sinf(w.windDir * 0.0174533f) > 0 ? -1 : 1;
    World::windAccel = dir * min(w.windKmh, 80.0f) * 1.5f;
  }
  if (w.kind == W_STORM && now > nextBolt) {
    nextBolt = now + random(4000, 14000);
    Render::flash(90);
    Render::bolt();
    if (run.b == B_TAUNT_STORM) say(PICK(L_TAUNT_SORRY));
    else if (run.b != B_SLEEP && chance(0.4f)) sayFor(900, PICK(L_STORM_FLASH));
  }

  // snowmen melt when it stops snowing, and do not survive earthquakes
  if (snowmanStage) {
    if (Imu::shake > 1.0f) {
      snowmanStage = 0;
      s.grudge += 10;
      say("MY SON");
    } else if (w.kind != W_SNOW && now - lastSnowmanDecay > 120000UL) {
      lastSnowmanDecay = now;
      snowmanStage--;
    }
  }
}

static void updatePranks(uint32_t now) {
  if (flipUntil && now > flipUntil) {
    flipUntil = 0;
    World::flipped = false;
    say(PICK(L_UNFLIP));
  }
  if (clockStolenUntil && now > clockStolenUntil) {
    clockStolenUntil = 0;
    if (run.b != B_SLEEP) say("fine. here's your clock.");
  }
  if (laterBuf[0] && now > laterAt && now > bubbleUntil) {
    say("%s", laterBuf);
    laterBuf[0] = 0;
  }
}

void update(uint32_t now, float dt) {
  Event e;
  if (hatching) {
    while (Imu::pollEvent(e)) {
    }
    updateHatch(dt);
    return;
  }
  while (Imu::pollEvent(e)) react(e);
  updateNeeds(dt);
  updateWeatherWorld(now);
  updatePranks(now);
  if (clockStolenUntil) look.props |= PR_CLOCK;
  behave(dt);
}

void printStatus() {
  Serial.print(F("[pet] "));
  Serial.print(s.name);
  Serial.print(F(" doing="));
  Serial.print(behaviorName(run.b));
  Serial.print(F(" t="));
  Serial.println(run.t, 1);
  Serial.print(F("  hunger=")); Serial.print(s.hunger, 0);
  Serial.print(F(" energy=")); Serial.print(s.energy, 0);
  Serial.print(F(" fun=")); Serial.print(s.fun, 0);
  Serial.print(F(" nausea=")); Serial.print(s.nausea, 0);
  Serial.print(F(" trust=")); Serial.print(s.trust, 0);
  Serial.print(F(" grudge=")); Serial.print(s.grudge, 0);
  Serial.print(F(" feral=")); Serial.print(s.feral);
  Serial.print(F(" stash=")); Serial.println(s.stash);
  Serial.print(F("  brave=")); Serial.print(s.brave);
  Serial.print(F(" glutton=")); Serial.print(s.glutton);
  Serial.print(F(" dramatic=")); Serial.print(s.dramatic);
  Serial.print(F(" spite=")); Serial.print(s.spite);
  Serial.print(F(" lazy=")); Serial.print(s.lazy);
  Serial.print(F(" mischief=")); Serial.println(s.mischief);
  Serial.print(F("  pukes=")); Serial.print(s.pukes);
  Serial.print(F(" tosses=")); Serial.print(s.tosses);
  Serial.print(F(" steps=")); Serial.println(Imu::steps);
}

}  // namespace Brain
