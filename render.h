#pragma once
#include "types.h"

namespace Render {

enum Mode : uint8_t {
  RM_WORLD,  // normal
  RM_EGG,    // hatch / calibration ritual
  RM_CRASH,  // fake "pet.exe stopped responding"
  RM_BRB,    // pet left; a sign stands in its place
  RM_DARK,   // "lights off" prank: only its eyes
};

extern Mode mode;
extern char eggText[48];
extern float eggCrack;      // 0..1
extern char signText[24];

bool begin();
extern bool ok;
void test();  // re-init + full-white / text test pattern
void frame(uint32_t now);
void flash(uint16_t ms);              // invert the panel briefly (lightning, CHOMP)
void shake(uint16_t ms, uint8_t amp);  // screen shake
void bolt();                          // draw a lightning bolt on the next frames

}  // namespace Render
