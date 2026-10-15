#pragma once
#include <Arduino.h>

// Pet + calibration persisted to the SAMD21's flash.
// Note: uploading a new sketch erases this. See CAL_FIXED_* in config.h.
namespace Save {

bool load();  // true if a pet was restored
void service(uint32_t now);
void requestSave();
void saveNow();
void wipe();
uint32_t lastSeenEpoch();

}  // namespace Save
