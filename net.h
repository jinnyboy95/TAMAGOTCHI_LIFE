#pragma once
#include "types.h"

// WiFi as a sense organ: weather, local time, location, and neighbours' SSIDs.
// Every call that touches the radio BLOCKS (1-10 s). Behaviors hide that by
// sending the pet "outside" first.
namespace Net {

struct Weather {
  bool valid;
  WeatherKind kind;
  float tempC;
  float windKmh;
  float windDir;      // degrees the wind comes FROM
  float precipMm;
  bool isDay;
  int16_t sunriseMin;  // local minutes since midnight
  int16_t sunsetMin;
  uint32_t fetchedMs;
};

extern Weather wx;
extern char city[24];
extern char ssids[8][33];
extern int8_t ssidRssi[8];
extern uint8_t ssidCount;
extern uint8_t networksSeen;
extern bool everConnected;

// debug overrides
extern int16_t fakeMinutes;   // -1 = real clock
extern bool fakeWeather;      // true = don't overwrite wx from the internet

void begin();
bool configured();
bool connected();
int rssi();

bool ensureConnected();  // blocking
bool refreshAll();       // connect + time + (geo if due) + weather; blocking
bool scan();             // blocking

bool weatherDue();
bool weatherOverdue();
bool scanDue();
bool retryDue();

uint32_t epoch();        // UTC seconds, 0 if unknown
int localMinutes();      // 0..1439, or -1 if unknown
bool isDark();
bool isBedtime();
float moonPhase();       // 0 = new, 0.5 = full
bool hot();
bool cold();
bool windy();
const char *kindName(WeatherKind k);

}  // namespace Net
