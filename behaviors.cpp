#include "brain.h"
#include "config.h"
#include "imu.h"
#include "lines.h"
#include "net.h"
#include "physics.h"
#include "render.h"
#include "save.h"

namespace Brain {

static const char *const NAMES[] = {
    "idle", "wander", "stare", "sleep", "beg", "eat", "refuse", "hidefood", "batfood", "eatstash",
    "bounce", "puke", "faint", "dizzy", "scream", "shocked", "sulk", "chomp", "annoyed", "curious",
    "held", "walkies", "upside", "dark", "lightsback",
    "revenge", "crash", "leave", "graffiti", "knock", "lightsoff", "stealclock", "flip",
    "hiccups", "zoomies", "rename", "spitepuke",
    "outside", "gossip", "nonet",
    "umbrella", "raindance", "soaked", "snowman", "eatsnow", "hidestorm", "tauntstorm",
    "sunbathe", "howl", "windwalk", "foglost", "melt", "shiver"};
static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == B_COUNT, "behavior name table out of sync");

const char *behaviorName(Behavior b) { return b < B_COUNT ? NAMES[b] : "?"; }

int behaviorByName(const char *n) {
  for (int i = 0; i < B_COUNT; i++)
    if (!strcmp(n, NAMES[i])) return i;
  return -1;
}

uint8_t priority(Behavior b) {
  switch (b) {
    case B_SCREAM: return 5;
    case B_BOUNCE:
    case B_PUKE:
    case B_FAINT: return 3;
    case B_SHOCKED:
    case B_DIZZY:
    case B_CHOMP:
    case B_UPSIDE:
    case B_DARK:
    case B_SULK: return 2;
    case B_IDLE:
    case B_WANDER:
    case B_STARE: return 0;
    default: return (b >= B_UMBRELLA && b <= B_SHIVER) ? 0 : 1;
  }
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static World::Body &P = World::pet;
static Run &R = run;
static float frameDt = 0;

static Vec2 tangent() { return World::rot(P.floor, 1, 0); }
static bool horizontalFloor() { return P.floor == 0 || P.floor == 2; }

static void walkAlong(float frac, float speed = WALK_SPEED) {
  Vec2 t = tangent();
  float span = t.x != 0 ? 50 : 18;
  World::walkTo(64 + t.x * frac * span, 32 + t.y * frac * span, speed);
}

static float alongTo(float x, float y) {
  Vec2 t = tangent();
  return (x - P.p.x) * t.x + (y - P.p.y) * t.y;
}

static void walkOff(int dir) {
  P.ghost = true;
  Vec2 t = tangent();
  World::walkTo(P.p.x + t.x * dir * 220, P.p.y + t.y * dir * 220, 32);
}

static void enterFrom(int dir) {
  if (horizontalFloor()) P.p.x = dir < 0 ? -20 : SCREEN_W + 20;
  P.ghost = true;
  World::walkTo(64, 32, 30);
}

static int nearSide() { return P.p.x < 64 ? -1 : 1; }

// fires once every `period` seconds of behavior time
static bool every(float period) { return fmodf(R.t, period) < frameDt; }

static void pose(Eyes e, Mouth m) {
  look.eyes = e;
  look.mouth = m;
}

static Vec2 headPos(float lx, float ly) {
  Vec2 o = World::rot(P.floor, lx, ly);
  return {P.p.x + o.x, P.p.y + o.y};
}

static void chomping() {
  look.mouth = fmodf(R.t, 0.3f) < 0.15f ? MO_O : MO_FLAT;
  if (every(0.25f)) {
    Vec2 m = headPos(look.lookX * 4, 3);
    World::spawn(World::P_CRUMB, m.x, m.y, frand(-30, 30), frand(-40, 0), 0.5f);
  }
}

static int nearestPuddle() {
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < MAX_PUDDLES; i++) {
    if (!World::puddles[i].active) continue;
    float d = fabsf(alongTo(World::puddles[i].x, World::puddles[i].y));
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}

static const char *skyWord() {
  const Net::Weather &w = Net::wx;
  switch (w.kind) {
    case W_CLEAR: return w.isDay ? "sunny" : "clear";
    case W_CLOUDY: return "cloudy";
    case W_FOG: return "foggy";
    case W_DRIZZLE: return "drizzly";
    case W_RAIN: return "raining. ew";
    case W_SNOW: return "SNOWING";
    case W_STORM: return "STORMING";
    default: return "weird";
  }
}

// ---------------------------------------------------------------------------
// everyday life
// ---------------------------------------------------------------------------
static void bIdle() {
  if (R.first) R.dur = R.arg ? frand(0.8f, 2) : frand(2, 5);
  if (every(2.5f)) look.lookX = frand(-1, 1);
  if (R.t > R.dur) chooseNext();
}

static void bWander() {
  if (R.first) walkAlong(frand(-1, 1));
  if (World::arrived() || R.t > 8) finish();
}

static void bStare() {
  if (R.first) {
    R.dur = 6;
    pose(EY_WIDE, MO_NONE);
    World::walkStop();
  }
  look.lookX = 0;
  if (R.phase == 0 && R.t > 1.5f) {
    say(PICK(L_STARE));
    R.phase = 1;
  }
  if (R.t > R.dur) {
    if (s.mischief > 60) say("anyway.");
    finish();
  }
}

static void bSleep() {
  if (R.first) pose(EY_CLOSED, MO_NONE);
  look.mouth = fmodf(R.t, 3) < 1.5f ? MO_NONE : MO_O;
  if (every(1.6f)) {
    Vec2 h = headPos(6, -10);
    World::spawn(World::P_ZZZ, h.x, h.y, 0, 0, 2.5f);
  }
  bool bed = Net::isBedtime();
  if ((!bed && s.energy > 90) || (!bed && R.t > 45 * 60)) {
    say(PICK(L_WAKE));
    finish();
  } else if (s.hunger > 92) {
    say("hungry. can't sleep.");
    finish();
  }
}

static void bBeg() {
  if (R.first) {
    R.dur = 5;
    pose(EY_WIDE, MO_O);
    say(s.feral ? PICK(L_BEG_FERAL) : PICK(L_BEG));
  }
  if (every(1.0f)) World::hop(40);
  if (R.t > R.dur) finish();
}

static void bEat() {
  World::Food &f = World::food;
  if (!f.active) {
    finish();
    return;
  }
  if (R.phase == 0) {
    World::walkTo(f.x, f.y, 30);
    if (fabsf(alongTo(f.x, f.y)) < 10 && f.grounded) {
      World::walkStop();
      look.lookX = alongTo(f.x, f.y) > 0 ? 1 : -1;
      R.phase = 1;
      R.timer = 0;
    } else if (R.t > 10) {
      say("can't reach it");
      finish();
    }
  } else if (R.phase == 1) {
    chomping();
    if (R.timer > 1.6f) {
      f.active = false;
      bool stuffed = s.hunger < 10;
      s.hunger = max(0.0f, s.hunger - 35);
      s.trust += 3;
      s.grudge -= 5;
      remember(M_FED);
      cureFeral(1);
      if (stuffed) {
        s.nausea += 30 + s.glutton / 4.0f;
        say(PICK(L_FULL));
        pose(EY_SPIRAL, MO_WAVY);
      } else {
        say(PICK(L_YUM));
        pose(EY_HAPPY, MO_SMILE);
      }
      R.phase = 2;
      R.timer = 0;
    }
  } else if (R.timer > 2) {
    finish();
  }
}

static void bRefuse() {
  if (R.first) {
    look.back = true;
    say(PICK(L_REFUSE));
    s.grudge -= 3;
  }
  if (R.phase == 0 && R.t > 1.2f) {
    float a = World::food.active ? alongTo(World::food.x, World::food.y) : 1;
    walkAlong(a > 0 ? -0.9f : 0.9f);
    R.phase = 1;
  }
  if (R.t > 4) finish();
}

static void bHideFood() {
  World::Food &f = World::food;
  switch (R.phase) {
    case 0:
      if (!f.active) {
        finish();
        return;
      }
      World::walkTo(f.x, f.y, 30);
      if (fabsf(alongTo(f.x, f.y)) < 10) {
        f.carried = true;
        look.props |= PR_FOOD;
        say(PICK(L_HIDE));
        R.dur = nearSide();
        walkOff((int)R.dur);
        R.phase = 1;
        R.timer = 0;
      } else if (R.t > 10) {
        finish();
      }
      break;
    case 1:
      if (World::offscreen() || R.timer > 8) {
        if (!World::offscreen()) say("*stuffs cheeks*");
        f.active = f.carried = false;
        look.props &= ~PR_FOOD;
        s.stash++;
        R.phase = 2;
        R.timer = 0;
      }
      break;
    case 2:
      if (R.timer > 1.5f && World::offscreen()) {
        enterFrom((int)R.dur);
        R.phase = 3;
        R.timer = 0;
      } else if (!World::offscreen()) {
        finish();
      }
      break;
    default:
      if (fabsf(P.p.x - 64) < 35 || R.timer > 6) finish();
      break;
  }
}

static void bBatFood() {
  World::Food &f = World::food;
  if (!f.active) {
    finish();
    return;
  }
  if (R.phase == 0) {
    World::walkTo(f.x, f.y, 40);
    if (fabsf(alongTo(f.x, f.y)) < 11) {
      World::walkStop();
      Vec2 t = tangent();
      Vec2 u = World::up();
      float dir = alongTo(f.x, f.y) >= 0 ? 1 : -1;
      f.vx = t.x * dir * 160 + u.x * 90;
      f.vy = t.y * dir * 160 + u.y * 90;
      f.grounded = false;
      say(PICK(L_BAT));
      pose(EY_SQUINT, MO_GRIN);
      R.phase = 1;
      R.timer = 0;
    } else if (R.t > 10) {
      finish();
    }
  } else if (R.timer > 2) {
    finish();
  }
}

static void bEatStash() {
  if (R.first) {
    if (!s.stash) {
      finish();
      return;
    }
    say("brb");
    R.dur = nearSide();
    walkOff((int)R.dur);
  }
  switch (R.phase) {
    case 0:
      if (World::offscreen() || R.t > 6) {
        R.phase = 1;
        R.timer = 0;
      }
      break;
    case 1:
      if (R.timer > 1.2f) {
        look.props |= PR_FOOD;
        if (World::offscreen()) enterFrom((int)R.dur);
        R.phase = 2;
        R.timer = 0;
      }
      break;
    case 2:
      if (fabsf(P.p.x - 64) < 40 || R.timer > 6) {
        P.ghost = false;
        World::walkStop();
        R.phase = 3;
        R.timer = 0;
      }
      break;
    default:
      chomping();
      if (R.timer > 1.6f) {
        look.props &= ~PR_FOOD;
        if (s.stash) s.stash--;
        s.hunger = max(0.0f, s.hunger - 30);
        say(PICK(L_STASH));
        finish();
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// being manhandled
// ---------------------------------------------------------------------------
static void bBounce() {
  bool enjoy = (s.mischief + s.brave) / 2 > 60 && s.nausea < 50;
  if (s.nausea > 50) pose(EY_SPIRAL, MO_WAVY);
  else if (enjoy) pose(EY_HAPPY, MO_GRIN);
  else pose(EY_WIDE, MO_SCREAM);
  if (R.first || every(2.2f)) sayFor(1500, enjoy ? PICK(L_WHEE) : PICK(L_AAA));
  if (!Imu::shaking && R.t > 1.0f) finish();
}

static void bPuke() {
  // arg 1 = on purpose
  if (R.first) {
    R.dur = chance(0.5f) ? 1 : -1;
    say(R.arg ? PICK(L_SPITE) : PICK(L_PRE_PUKE));
    World::walkStop();
  }
  switch (R.phase) {
    case 0:
      if (R.arg) {
        pose(EY_SUS, MO_GRIN);
        look.lookX = 0;
      } else {
        pose(EY_SPIRAL, MO_WAVY);
        look.shiver = 1;
      }
      if (R.t > 1.4f) {
        R.phase = 1;
        R.timer = 0;
      }
      break;
    case 1:
      look.shiver = 0;
      look.lookX = R.dur;
      pose(EY_X, MO_SCREAM);
      if (every(0.04f)) {
        Vec2 m = headPos(R.dur * 5, 3);
        Vec2 v = World::rot(P.floor, R.dur * frand(40, 95), -frand(10, 60));
        World::spawn(World::P_PUKE, m.x, m.y, v.x, v.y, 3);
      }
      if (R.timer > 1.6f) {
        s.nausea = 0;
        s.hunger = min(100.0f, s.hunger + 15);
        s.pukes++;
        remember(M_PUKED);
        Save::requestSave();
        R.phase = 2;
        R.timer = 0;
        pose(EY_SLEEPY, MO_FLAT);
      }
      break;
    case 2:
      if (R.timer > 0.8f) {
        float r = frand() * 100;
        if (r < (s.glutton + s.mischief) / 4.0f && nearestPuddle() >= 0) {
          R.phase = 3;
        } else if (r < 60 && s.dramatic > 50) {
          pose(EY_ANGRY, MO_FROWN);
          look.lookX = 0;
          say("look what you did");
          R.phase = 5;
        } else {
          say(PICK(L_POST_PUKE));
          R.phase = 5;
        }
        R.timer = 0;
      }
      break;
    case 3: {
      int i = nearestPuddle();
      if (i < 0) {
        finish();
        return;
      }
      World::walkTo(World::puddles[i].x, World::puddles[i].y, 25);
      if (fabsf(alongTo(World::puddles[i].x, World::puddles[i].y)) < 10 || R.timer > 6) {
        World::walkStop();
        R.phase = 4;
        R.timer = 0;
      }
      break;
    }
    case 4:
      chomping();
      if (R.timer > 1.5f) {
        int i = nearestPuddle();
        if (i >= 0) World::puddles[i].active = false;
        say(PICK(L_EAT_PUKE));
        R.phase = 5;
        R.timer = 0;
      }
      break;
    default:
      if (R.timer > 2.5f) finish();
      break;
  }
}

static void bFaint() {
  if (R.first) {
    pose(EY_X, MO_TONGUE);
    World::walkStop();
  }
  look.melt = 0.35f;
  if (R.phase == 0 && R.t > 3.5f) {
    say(PICK(L_FAINT));
    R.phase = 1;
  }
  if (R.t > 6.5f) {
    s.nausea = max(0.0f, s.nausea - 40);
    finish();
  }
}

static void bDizzy() {
  if (R.first) {
    R.dur = frand(4, 6);
    say(PICK(L_DIZZY));
  }
  pose(EY_SPIRAL, MO_WAVY);
  if (every(0.7f)) {
    walkAlong(frand(-1, 1), 35);
    if (chance(0.3f)) World::hop(30);
  }
  if (R.t > R.dur) finish();
}

static void bScream() {
  if (R.first) sayFor(3000, PICK(L_SCREAM));
  pose(EY_WIDE, MO_SCREAM);
  look.lookX = sinf(R.t * 20);
  if (R.t > 3) start(B_SHOCKED);
}

static void bShocked() {
  if (R.first) {
    pose(EY_WIDE, MO_O);
    look.shiver = 1.2f;
  }
  if (R.phase == 0 && R.t > 1.2f) {
    look.shiver = 0;
    if (s.brave > 70) {
      s.fun += 20;
      s.grudge += 3;
      pose(EY_HAPPY, MO_GRIN);
      say(PICK(L_TOSSED_BRAVE));
    } else {
      s.grudge += 25 * (1.2f - s.brave / 100.0f);
      s.trust -= 10;
      remember(M_TOSSED);
      pose(EY_ANGRY, MO_FROWN);
      say(PICK(L_TOSSED));
    }
    Save::requestSave();
    R.phase = 1;
  }
  if (R.t > 4) {
    if (s.brave <= 70 && s.grudge > 60 && chance(s.spite / 100.0f)) start(B_SULK);
    else finish();
  }
}

static void bSulk() {
  if (R.first) {
    R.dur = 8 + s.spite * 0.15f;
    say(PICK(L_SULK));
    World::walkStop();
  }
  // glance back to check if you're sorry yet
  float g = fmodf(R.t, 4);
  look.back = !(g > 3.3f);
  pose(EY_ANGRY, MO_FLAT);
  if (R.t > R.dur) finish();
}

static void bChomp() {
  if (R.first) World::walkStop();
  if (R.t < 0.3f) {
    look.scale = 1 + R.t / 0.3f * 0.9f;
    pose(EY_ANGRY, MO_TEETH);
  } else if (R.phase == 0) {
    Render::flash(120);
    Render::shake(300, 3);
    sayFor(1200, PICK(L_CHOMP));
    s.grudge -= 10;
    R.phase = 1;
  } else if (R.t < 0.8f) {
    look.scale = 1.9f - (R.t - 0.3f) / 0.5f * 0.9f;
  } else {
    look.scale = 1;
    pose(EY_SQUINT, MO_GRIN);
    if (R.phase == 1 && R.t > 1.4f) {
      say(PICK(L_SMUG));
      R.phase = 2;
    }
    if (R.t > 3.2f) finish();
  }
}

static void bAnnoyed() {
  if (R.first && millis() > bubbleUntil) say(PICK(L_ANNOYED));
  pose(EY_ANGRY, MO_FLAT);
  if (R.t > 2.5f) finish();
}

static void bCurious() {
  if (R.first) {
    look.lookX = chance(0.5f) ? 1 : -1;
    sayFor(900, "?");
    World::hop(40);
  }
  if (R.t > 1.5f) finish();
}

static void bHeld() {
  if (R.first) {
    R.arg = s.trust > 40 && s.grudge < 50;
    say(R.arg ? PICK(L_HELD_LOVE) : PICK(L_HELD_SUS));
  }
  float m = frameDt;
  if (R.arg) {
    pose(EY_HAPPY, MO_SMILE);
    look.blush = true;
    if (every(1.2f)) {
      Vec2 h = headPos(frand(-6, 6), -10);
      World::spawn(World::P_HEART, h.x, h.y, 0, 0, 2);
    }
    s.trust += m * 0.4f;
    s.grudge -= m * 0.3f;
    s.fun += m * 0.5f;
  } else {
    pose(EY_SUS, MO_FLAT);
    s.trust += m * 0.15f;
  }
  if (!Imu::held || R.t > 25) finish();
}

static void bWalkies() {
  if (R.first) {
    say(PICK(L_WALKIES));
    s.grudge -= 15;
    s.trust += 5;
    s.fun += 20;
  }
  pose(EY_HAPPY, MO_GRIN);
  if (every(0.45f)) World::hop(45);
  if (R.t > 6) finish();
}

static void bUpside() {
  if (R.first) say(s.mischief > 70 && chance(0.4f) ? "i live here now" : PICK(L_UPSIDE));
  if (R.t > 40) {
    pose(EY_CLOSED, MO_NONE);  // gave up and fell asleep on the ceiling
  } else {
    pose(EY_ANGRY, MO_FROWN);
    if (R.t > 1 && every(7)) say(PICK(L_UPSIDE));
    s.grudge += frameDt * 0.3f;
  }
  bool upsideNow = World::flipped ? Imu::orient != Imu::OR_UPSIDE : Imu::orient == Imu::OR_UPSIDE;
  if (!upsideNow && R.t > 2) finish();
}

static void bDark() {
  pose(R.t > 20 ? EY_CLOSED : EY_WIDE, MO_O);
  if (Imu::orient != Imu::OR_FACEDOWN && R.t > 1) {
    int16_t a = R.t < 4 ? 0 : ((s.energy < 50 || Net::isBedtime()) ? 1 : 2);
    start(B_LIGHTS_BACK, a);
  }
}

static void bLightsBack() {
  if (R.first) {
    if (R.arg == 1) {
      pose(EY_SLEEPY, MO_FLAT);
      say(PICK(L_DARK_SLEEPY));
    } else if (R.arg == 2) {
      pose(EY_WIDE, MO_O);
      look.shiver = 1;
      say(PICK(L_DARK_PANIC));
      remember(M_DARK);
      s.grudge += 5;
    } else {
      say("hey.");
    }
  }
  if (R.t > 3) finish();
}

// ---------------------------------------------------------------------------
// chaos & revenge
// ---------------------------------------------------------------------------
static void bRevenge() {
  if (R.first) {
    World::walkStop();
    pose(EY_SUS, MO_GRIN);
    switch (worstMemory()) {
      case M_TOSSED: say(PICK(L_REMEMBER_TOSS)); break;
      case M_SHAKEN: say(PICK(L_REMEMBER_SHAKE)); break;
      case M_WOKEN: say(PICK(L_REMEMBER_WOKE)); break;
      case M_POKED: say(PICK(L_REMEMBER_POKE)); break;
      default: say(PICK(L_REMEMBER_GENERIC)); break;
    }
  }
  look.lookX = 0;
  if (R.t > 2.8f) {
    static const Behavior ACTS[] = {B_FLIP_UI, B_LIGHTS_OFF, B_STEAL_CLOCK, B_FAKE_CRASH,
                                    B_SPITE_PUKE, B_GRAFFITI, B_SULK, B_KNOCK};
    s.grudge = max(0.0f, s.grudge - 25);
    Save::requestSave();
    start(PICK(ACTS), 1);
  }
}

static void bFakeCrash() {
  if (R.first) {
    Render::mode = Render::RM_CRASH;
    look.hidden = true;
    bubbleUntil = 0;
    R.dur = frand(3.5f, 6);
  }
  if (R.phase == 0 && R.t > R.dur) {
    Render::mode = Render::RM_WORLD;
    look.hidden = false;
    P.p = {64, 32};
    World::hop(90);
    pose(EY_HAPPY, MO_GRIN);
    say(PICK(L_CRASH_BACK));
    s.fun += 15;
    R.phase = 1;
  }
  if (R.t > R.dur + 2.5f) finish();
}

static void bLeave() {
  switch (R.phase) {
    case 0:
      if (R.first) {
        say(PICK(L_BRB));
        R.timer = nearSide();  // remember the side in timer until we're gone
        R.arg = (int16_t)R.timer;
        walkOff(R.arg);
      }
      if (World::offscreen() || R.t > 8) {
        look.hidden = true;
        Render::mode = Render::RM_BRB;
        strcpy(Render::signText, "back soon");
        R.dur = frand(20, 60);
        R.phase = 1;
        R.timer = 0;
      }
      break;
    case 1:
      if (R.timer > R.dur) {
        static const uint16_t LOOT[] = {PR_HAT, PR_CROWN, PR_SOCK, PR_SCARF, 0};
        Render::mode = Render::RM_WORLD;
        look.hidden = false;
        s.accessory = PICK(LOOT);
        look.props = s.accessory;
        enterFrom(R.arg);
        R.phase = 2;
        R.timer = 0;
      }
      break;
    default:
      if (fabsf(P.p.x - 64) < 30 || R.timer > 8) {
        P.ghost = false;
        say(s.accessory ? PICK(L_FOUND) : "nothing out there.");
        Save::requestSave();
        finish();
      }
      break;
  }
}

static char graffitiMsg[24];
static int16_t gx, gy;

static void bGraffiti() {
  if (R.first) {
    walkAlong(frand(-0.8f, 0.8f));
    look.props |= PR_PEN;
  }
  if (R.phase == 0) {
    if (World::arrived() || R.t > 4) {
      World::walkStop();
      snprintf(graffitiMsg, sizeof(graffitiMsg), R.arg ? PICK(L_GRAFFITI_MAD) : PICK(L_GRAFFITI), s.name);
      for (char *c = graffitiMsg; *c; c++) *c = toupper(*c);
      int len = strlen(graffitiMsg);
      gx = random(1, max(2, SCREEN_W - len * 6));
      gy = random(1, 40);
      R.phase = 1;
      R.timer = 0;
      R.dur = 0;  // letters drawn
      pose(EY_SQUINT, MO_TONGUE);
    }
  } else if (R.phase == 1) {
    if (R.timer > 0.15f) {
      R.timer = 0;
      int i = (int)R.dur;
      if (graffitiMsg[i]) {
        World::graffiti.drawChar(gx + i * 6, gy, graffitiMsg[i], 1, 0, 1);
        look.lookX = (gx + i * 6) < P.p.x ? -1 : 1;
        R.dur += 1;
      } else {
        say("art.");
        pose(EY_HAPPY, MO_SMILE);
        look.props &= ~PR_PEN;
        R.phase = 2;
      }
    }
  } else if (R.timer > 2) {
    finish();
  }
}

static void bKnock() {
  if (R.first) {
    World::walkStop();
    look.scale = 1.7f;
    pose(EY_WIDE, MO_O);
  }
  look.lookX = 0;
  if (every(0.9f) && R.phase < 4) {
    Render::shake(160, 2);
    sayFor(1100, L_KNOCK[R.phase % 4]);
    R.phase++;
  }
  if (R.t > 4.5f) finish();
}

static void bLightsOff() {
  if (R.first) {
    Render::mode = Render::RM_DARK;
    say(PICK(L_LIGHTS_OFF));
    R.dur = frand(8, 15);
  }
  if (R.t > R.dur) {
    say("hehe");
    finish();
  }
}

static void bStealClock() {
  if (R.first) {
    World::hop(110);
    clockStolenUntil = millis() + (uint32_t)frand(45, 90) * 1000;
    say(PICK(L_STEAL));
    look.props |= PR_CLOCK;
  }
  pose(EY_SQUINT, MO_GRIN);
  if (World::arrived()) walkAlong(frand(-1, 1), 60);
  if (R.t > 6) finish();
}

static void bFlipUi() {
  if (R.first) {
    say(PICK(L_FLIP));
    World::flipped = true;
    flipUntil = millis() + (uint32_t)frand(60, 150) * 1000;
  }
  pose(EY_HAPPY, MO_GRIN);
  if (R.t > 3) finish();
}

static void bHiccups() {
  if (R.first) {
    R.dur = 15;
    R.timer = -frand(0.5f, 1.5f);
  }
  if (R.timer > 0) {
    World::hop(70);
    P.squash = 0.3f;
    sayFor(700, "hic!");
    R.timer = -frand(1, 2);
  }
  if (R.t > R.dur) {
    say("...i think they're gone");
    finish();
  }
}

static void bZoomies() {
  if (R.first) {
    R.dur = 6;
    say(PICK(L_ZOOM));
  }
  pose(EY_WIDE, MO_GRIN);
  if (World::arrived()) {
    walkAlong(frand(-1, 1), 95);
    if (chance(0.3f)) World::hop(60);
  }
  if (R.t > R.dur) finish();
}

static void bRename() {
  if (R.first) {
    say("i am no longer %s.", s.name);
    pose(EY_SUS, MO_FLAT);
  }
  if (R.phase == 0 && R.t > 2.5f) {
    rename();
    say(PICK(L_RENAME), s.name);
    pose(EY_HAPPY, MO_GRIN);
    R.phase = 1;
  }
  if (R.t > 5.5f) finish();
}

// ---------------------------------------------------------------------------
// network senses
// ---------------------------------------------------------------------------
static void bGoOutside() {
  switch (R.phase) {
    case 0:
      if (R.first) {
        say(PICK(L_OUTSIDE));
        R.arg = nearSide();
        walkOff(R.arg);
      }
      if (World::offscreen() || R.t > 7) {
        look.hidden = true;
        bubbleUntil = 0;
        Render::mode = Render::RM_BRB;
        strcpy(Render::signText, "checking sky");
        Render::frame(millis());  // show the sign, then go block on the radio
        bool ok = Net::refreshAll();
        Render::mode = Render::RM_WORLD;
        look.hidden = false;
        if (ok && Net::wx.valid && (Net::wx.kind == W_RAIN || Net::wx.kind == W_DRIZZLE || Net::wx.kind == W_STORM))
          look.props |= PR_UMBRELLA;
        enterFrom(R.arg);
        R.dur = ok;
        R.phase = 1;
        R.timer = 0;
      }
      break;
    case 1:
      if (fabsf(P.p.x - 64) < 35 || R.timer > 6) {
        P.ghost = false;
        World::walkStop();
        if (R.dur && Net::wx.valid) {
          int t = (int)lroundf(Net::wx.tempC);
          if (Net::city[0] && chance(0.5f)) say("%s: %dC, %s.", Net::city, t, skyWord());
          else say("it's %dC and %s.", t, skyWord());
          if (Net::hot()) pose(EY_SLEEPY, MO_TONGUE);
          else if (Net::cold()) look.shiver = 0.6f;
        } else {
          say(PICK(L_OUTSIDE_FAIL));
          pose(EY_SUS, MO_FROWN);
        }
        R.phase = 2;
        R.timer = 0;
      }
      break;
    default:
      if (R.timer > 3.5f) finish();
      break;
  }
}

static void bGossip() {
  switch (R.phase) {
    case 0:
      if (R.first) {
        look.props |= PR_ANTENNA;
        pose(EY_SUS, MO_FLAT);
        say(PICK(L_SNIFF));
        World::walkStop();
      }
      if (R.t > 1.5f) {
        Render::frame(millis());
        Net::scan();
        R.phase = 1;
        R.timer = 0;
      }
      break;
    case 1:
      pose(EY_SQUINT, MO_GRIN);
      if (!Net::ssidCount) say(PICK(L_LONELY));
      else say(PICK(L_GOSSIP), Net::ssids[random(0, Net::ssidCount)]);
      R.phase = 2;
      R.timer = 0;
      break;
    case 2:
      if (R.timer > 4.5f) {
        if (Net::networksSeen > 12) say(PICK(L_CROWD));
        R.phase = 3;
        R.timer = 0;
      }
      break;
    default:
      if (R.timer > 3) finish();
      break;
  }
}

static void bSignalAnxiety() {
  if (R.phase == 0) {
    if (R.first) {
      look.props |= PR_ANTENNA;
      say(PICK(L_NO_NET));
    }
    pose(EY_WIDE, MO_WAVY);
    if (World::arrived()) walkAlong(P.p.x < 64 ? 0.8f : -0.8f, 35);
    if (R.t > 3) {
      Render::frame(millis());
      R.arg = Net::ensureConnected();
      R.phase = 1;
      R.timer = 0;
      if (R.arg) {
        pose(EY_HAPPY, MO_GRIN);
        say(PICK(L_NET_BACK));
        World::hop(60);
        s.fun += 10;
      } else {
        pose(EY_SUS, MO_FROWN);
        say(PICK(L_NET_ALONE));
      }
    }
  } else if (R.timer > 3) {
    finish();
  }
}

// ---------------------------------------------------------------------------
// weather moods
// ---------------------------------------------------------------------------
static void bUmbrella() {
  if (R.first) say(PICK(L_UMBRELLA));
  look.props |= PR_UMBRELLA;
  pose(EY_SQUINT, MO_SMILE);
  if (every(8)) walkAlong(frand(-0.7f, 0.7f), 15);
  if (R.t > 30) finish();
}

static void bRainDance() {
  if (R.first) say(PICK(L_RAIN_DANCE));
  pose(EY_HAPPY, MO_GRIN);
  if (every(0.5f)) {
    World::hop(55);
    walkAlong(fmodf(R.t, 2) < 1 ? -0.4f : 0.4f, 30);
  }
  if (R.t > 10) finish();
}

static void bSoaked() {
  if (R.first) {
    say(PICK(L_SOAKED));
    s.grudge += 3;  // the weather is your fault, obviously
    World::walkStop();
  }
  pose(EY_ANGRY, MO_FROWN);
  if (every(0.3f)) {
    Vec2 h = headPos(frand(-9, 9), frand(-4, 6));
    World::spawn(World::P_DRIP, h.x, h.y, 0, 0, 1);
  }
  if (R.t > 12) finish();
}

static void bSnowman() {
  if (P.floor != 0) {
    finish();
    return;
  }
  if (R.first) walkAlong(frand(-0.6f, 0.6f));
  if (R.phase == 0) {
    if (World::arrived() || R.t > 5) {
      World::walkStop();
      int side = P.p.x < 64 ? 1 : -1;
      snowmanX = (int16_t)clampf(P.p.x + side * 18, 6, SCREEN_W - 7);
      if (snowmanStage > 0) snowmanStage = 0;
      look.lookX = side;
      R.phase = 1;
      R.timer = 0;
    }
  } else if (R.phase == 1) {
    pose(EY_HAPPY, MO_TONGUE);
    if (R.timer > 2.5f) {
      R.timer = 0;
      World::hop(25);
      if (++snowmanStage >= 3) {
        say(PICK(L_SNOWMAN), s.name);
        pose(EY_HAPPY, MO_SMILE);
        R.phase = 2;
      }
    }
  } else if (R.timer > 3.5f) {
    finish();
  }
}

static void bEatSnow() {
  if (R.first) say(PICK(L_EAT_SNOW));
  pose(EY_HAPPY, MO_TONGUE);
  if (every(0.4f)) {
    int x0 = (int)clampf(P.p.x - 6, 0, SCREEN_W - 1), x1 = (int)clampf(P.p.x + 6, 0, SCREEN_W - 1);
    for (int x = x0; x <= x1; x++)
      if (World::snowPile[x]) World::snowPile[x]--;
  }
  if (R.t > 6) finish();
}

static void bHideStorm() {
  if (R.first) {
    say(PICK(L_HIDE_STORM));
    World::walkStop();
  }
  look.props |= PR_BLANKET;
  look.shiver = 0.7f;
  pose(EY_WIDE, MO_NONE);
  if (R.t > 20 || Net::wx.kind != W_STORM) finish();
}

static void bTauntStorm() {
  if (R.first) say(PICK(L_TAUNT));
  pose(EY_ANGRY, MO_GRIN);
  if (every(1.2f)) World::hop(50);
  if (R.t > 10) finish();
}

static void bSunbathe() {
  if (R.first) {
    say(PICK(L_SUN));
    World::walkStop();
  }
  look.props |= PR_SHADES;
  pose(EY_CLOSED, MO_SMILE);
  if (R.t > 30) finish();
}

static void bHowl() {
  if (R.first) {
    say(PICK(L_HOWL));
    World::walkStop();
  }
  pose(EY_CLOSED, MO_O);
  if (R.phase == 0 && R.t > 4) {
    say(PICK(L_HOWL_AFTER));
    pose(EY_SUS, MO_FLAT);
    R.phase = 1;
  }
  if (R.t > 7) finish();
}

static void bWindwalk() {
  if (R.first) {
    say(PICK(L_WIND));
    if (chance(0.3f)) World::fling(World::windAccel > 0 ? 120 : -120, -60);
  }
  pose(EY_SQUINT, MO_FLAT);
  if (World::arrived()) walkAlong(World::windAccel > 0 ? -0.9f : 0.9f, 16);
  if (R.t > 12) finish();
}

static void bFogLost() {
  if (R.first) say(PICK(L_FOG));
  pose(EY_WIDE, MO_O);
  if (World::arrived()) walkAlong(frand(-1, 1), 12);
  if (R.t > 12) finish();
}

static void bMelt() {
  if (R.first) {
    say(PICK(L_MELT));
    World::walkStop();
  }
  if (R.phase == 0) {
    look.melt = min(0.75f, R.t / 8);
    pose(EY_SLEEPY, MO_TONGUE);
    if (every(0.5f)) {
      Vec2 h = headPos(frand(-8, 8), -2);
      World::spawn(World::P_DRIP, h.x, h.y, 0, 0, 1);
    }
    if (R.t > 12) {
      look.melt = 0;
      World::hop(50);
      say("nope. still hot.");
      R.phase = 1;
    }
  } else if (R.t > 15) {
    finish();
  }
}

static void bShiver() {
  if (R.first) say(PICK(L_COLD));
  look.props |= PR_SCARF;
  look.shiver = 1;
  pose(EY_SQUINT, MO_WAVY);
  if (R.t > 10) finish();
}

// ---------------------------------------------------------------------------

void behave(float dt) {
  frameDt = dt;
  uint16_t gen = runGen;
  switch (R.b) {
    case B_IDLE: bIdle(); break;
    case B_WANDER: bWander(); break;
    case B_STARE: bStare(); break;
    case B_SLEEP: bSleep(); break;
    case B_BEG: bBeg(); break;
    case B_EAT: bEat(); break;
    case B_REFUSE: bRefuse(); break;
    case B_HIDE_FOOD: bHideFood(); break;
    case B_BAT_FOOD: bBatFood(); break;
    case B_EAT_STASH: bEatStash(); break;
    case B_BOUNCE: bBounce(); break;
    case B_PUKE: bPuke(); break;
    case B_FAINT: bFaint(); break;
    case B_DIZZY: bDizzy(); break;
    case B_SCREAM: bScream(); break;
    case B_SHOCKED: bShocked(); break;
    case B_SULK: bSulk(); break;
    case B_CHOMP: bChomp(); break;
    case B_ANNOYED: bAnnoyed(); break;
    case B_CURIOUS: bCurious(); break;
    case B_HELD: bHeld(); break;
    case B_WALKIES: bWalkies(); break;
    case B_UPSIDE: bUpside(); break;
    case B_DARK: bDark(); break;
    case B_LIGHTS_BACK: bLightsBack(); break;
    case B_REVENGE: bRevenge(); break;
    case B_FAKE_CRASH: bFakeCrash(); break;
    case B_LEAVE: bLeave(); break;
    case B_GRAFFITI: bGraffiti(); break;
    case B_KNOCK: bKnock(); break;
    case B_LIGHTS_OFF: bLightsOff(); break;
    case B_STEAL_CLOCK: bStealClock(); break;
    case B_FLIP_UI: bFlipUi(); break;
    case B_HICCUPS: bHiccups(); break;
    case B_ZOOMIES: bZoomies(); break;
    case B_RENAME: bRename(); break;
    case B_SPITE_PUKE: start(B_PUKE, 1); break;
    case B_GO_OUTSIDE: bGoOutside(); break;
    case B_GOSSIP: bGossip(); break;
    case B_SIGNAL_ANXIETY: bSignalAnxiety(); break;
    case B_UMBRELLA: bUmbrella(); break;
    case B_RAIN_DANCE: bRainDance(); break;
    case B_SOAKED: bSoaked(); break;
    case B_SNOWMAN: bSnowman(); break;
    case B_EAT_SNOW: bEatSnow(); break;
    case B_HIDE_STORM: bHideStorm(); break;
    case B_TAUNT_STORM: bTauntStorm(); break;
    case B_SUNBATHE: bSunbathe(); break;
    case B_HOWL: bHowl(); break;
    case B_WINDWALK: bWindwalk(); break;
    case B_FOG_LOST: bFogLost(); break;
    case B_MELT: bMelt(); break;
    case B_SHIVER: bShiver(); break;
    default: finish(); break;
  }
  if (gen == runGen) {
    R.first = false;
    R.t += dt;
    R.timer += dt;
  }

  // face where we're walking
  if (P.walking) look.lookX = alongTo(P.target.x, P.target.y) > 0 ? 1 : -1;
  // carried snack rides along in front of the face
  if (World::food.carried) {
    Vec2 h = headPos(look.lookX * 9, 2);
    World::food.x = h.x;
    World::food.y = h.y;
  }
}

}  // namespace Brain
