#pragma once
#include "types.h"

// ---------------------------------------------------------------------------
// How the pet looks this frame (behaviors write it, render reads it).
// ---------------------------------------------------------------------------
enum Eyes : uint8_t { EY_OPEN, EY_CLOSED, EY_HAPPY, EY_ANGRY, EY_X, EY_SPIRAL, EY_WIDE, EY_SLEEPY, EY_HEART, EY_SQUINT, EY_SUS };
enum Mouth : uint8_t { MO_NONE, MO_SMILE, MO_FROWN, MO_O, MO_SCREAM, MO_TEETH, MO_WAVY, MO_TONGUE, MO_FLAT, MO_GRIN };

enum Prop : uint16_t {
  PR_UMBRELLA = 1 << 0,
  PR_SHADES = 1 << 1,
  PR_BLANKET = 1 << 2,
  PR_ANTENNA = 1 << 3,
  PR_SCARF = 1 << 4,
  PR_HAT = 1 << 5,
  PR_CROWN = 1 << 6,
  PR_CLOCK = 1 << 7,
  PR_FOOD = 1 << 8,
  PR_SOCK = 1 << 9,
  PR_PEN = 1 << 10,
};

struct Look {
  Eyes eyes;
  Mouth mouth;
  uint16_t props;
  float lookX;   // -1..1 pupil direction
  bool back;     // turned away from you
  bool blush;
  bool hidden;   // not drawn at all
  float scale;   // >1 = pressed against the glass
  float shiver;  // px of jitter
  float melt;    // 0..1 flattened into a puddle
};

// ---------------------------------------------------------------------------
// Behaviors. Order matters only for the name table in behaviors.cpp.
// ---------------------------------------------------------------------------
enum Behavior : uint8_t {
  B_IDLE, B_WANDER, B_STARE, B_SLEEP, B_BEG, B_EAT, B_REFUSE, B_HIDE_FOOD, B_BAT_FOOD, B_EAT_STASH,
  B_BOUNCE, B_PUKE, B_FAINT, B_DIZZY, B_SCREAM, B_SHOCKED, B_SULK, B_CHOMP, B_ANNOYED, B_CURIOUS,
  B_HELD, B_WALKIES, B_UPSIDE, B_DARK, B_LIGHTS_BACK,
  B_REVENGE, B_FAKE_CRASH, B_LEAVE, B_GRAFFITI, B_KNOCK, B_LIGHTS_OFF, B_STEAL_CLOCK, B_FLIP_UI,
  B_HICCUPS, B_ZOOMIES, B_RENAME, B_SPITE_PUKE,
  B_GO_OUTSIDE, B_GOSSIP, B_SIGNAL_ANXIETY,
  B_UMBRELLA, B_RAIN_DANCE, B_SOAKED, B_SNOWMAN, B_EAT_SNOW, B_HIDE_STORM, B_TAUNT_STORM,
  B_SUNBATHE, B_HOWL, B_WINDWALK, B_FOG_LOST, B_MELT, B_SHIVER,
  B_COUNT
};

// Things the pet remembers you doing.
enum Mem : uint8_t { M_NONE, M_TOSSED, M_SHAKEN, M_PUKED, M_WOKEN, M_FED, M_WALKED, M_HELD, M_POKED, M_DARK, M_IGNORED, M_SPUN };

struct MemEntry {
  uint8_t type;
  uint32_t ms;
};

struct PetState {
  char name[12];
  // personality, 0..100, rolled at hatch
  uint8_t brave, glutton, dramatic, spite, lazy, mischief;
  // needs / feelings, 0..100
  float hunger, energy, fun, nausea, trust, grudge;
  bool feral;
  uint8_t stash;       // hidden snacks
  uint16_t accessory;  // a Prop it found and insists on wearing
  uint16_t pukes, tosses;
  uint32_t bornEpoch;
  MemEntry mem[8];
  uint8_t memHead;
};

struct Run {
  Behavior b;
  float t;       // seconds in this behavior
  float timer;   // general purpose
  float dur;
  uint8_t phase;
  int16_t arg;
  bool first;
};

namespace Brain {

extern PetState s;
extern Look look;
extern Run run;
extern uint16_t runGen;  // bumps on every start(), so behave() knows it was replaced
extern bool hatching;
extern char bubble[72];
extern uint32_t bubbleUntil;

// pranks that outlive the behavior that started them
extern uint32_t flipUntil, clockStolenUntil;
extern int16_t snowmanX;
extern uint8_t snowmanStage;

void begin(bool haveSave);
void update(uint32_t now, float dt);
void react(Event e);
void onImpact(float speed);
void onTimeKnown(uint32_t epoch, uint32_t lastSeenEpoch);

void say(const char *fmt, ...);
void sayFor(uint32_t ms, const char *fmt, ...);
void start(Behavior b, int16_t arg = 0);
void finish();
void chooseNext();
void newPet();
void rename();
void cureFeral(uint8_t n);

void remember(Mem m);
int recent(Mem m, uint32_t withinMs);
Mem worstMemory();

const char *behaviorName(Behavior b);
int behaviorByName(const char *n);
void printStatus();

// implemented in behaviors.cpp
void behave(float dt);
uint8_t priority(Behavior b);

}  // namespace Brain
