#include "save.h"
#include <FlashStorage.h>
#include "brain.h"
#include "config.h"
#include "imu.h"
#include "net.h"

namespace Save {

static const uint32_t MAGIC = 0x7A6D6731;  // "zmg1"
static const uint16_t VERSION = 1;

struct SaveData {
  uint32_t magic;
  uint16_t version;
  uint8_t hasPet, hasCal;
  PetState pet;
  float calD[3], calF[3];
  uint32_t lastEpoch;
  uint32_t steps;
};

FlashStorage(store, SaveData);

static SaveData data;
static uint32_t lastSaveMs = 0;
static bool requested = false;
static uint32_t loadedLastEpoch = 0;

bool load() {
  data = store.read();
  bool valid = data.magic == MAGIC && data.version == VERSION;
  if (valid && data.hasCal) Imu::setCalibration(data.calD, data.calF);
#if defined(CAL_FIXED_D) && defined(CAL_FIXED_F)
  {
    const float D[3] = CAL_FIXED_D;
    const float F[3] = CAL_FIXED_F;
    Imu::setCalibration(D, F);
  }
#endif
  if (!valid || !data.hasPet) return false;
  Brain::s = data.pet;
  for (int i = 0; i < 8; i++) Brain::s.mem[i].ms = 0;
  loadedLastEpoch = data.lastEpoch;
  Imu::steps = data.steps;
  Serial.print(F("[save] restored "));
  Serial.println(Brain::s.name);
  return true;
}

void saveNow() {
  data.magic = MAGIC;
  data.version = VERSION;
  data.hasPet = Brain::s.name[0] != 0;
  data.pet = Brain::s;
  data.hasCal = Imu::calibrated();
  Imu::getCalibration(data.calD, data.calF);
  if (Net::epoch()) data.lastEpoch = Net::epoch();
  data.steps = Imu::steps;
  store.write(data);
  lastSaveMs = millis();
  requested = false;
  Serial.println(F("[save] saved"));
}

void requestSave() { requested = true; }

void service(uint32_t now) {
  if (requested && now - lastSaveMs > SAVE_MIN_GAP_S * 1000UL) saveNow();
  else if (now - lastSaveMs > SAVE_EVERY_MIN * 60000UL) saveNow();
}

void wipe() {
  memset(&data, 0, sizeof(data));
  store.write(data);
}

uint32_t lastSeenEpoch() { return loadedLastEpoch; }

}  // namespace Save
