# TAMAGOTCHI_LIFE

A chaotic gremlin that lives in a 128x64 SSD1306 OLED and runs on an Arduino Nano 33 IoT.
It has needs, a random personality, a memory and grudges, and it acts on them.

## Setup
1. Wire the OLED: VCC→3.3V, GND→GND, SDA→A4, SCL→A5 (address 0x3C).
2. Put your WiFi in `secrets.h` (copy it from `secrets.example.h`). Location comes from your IP.
3. Libraries: Adafruit SSD1306, Adafruit GFX, WiFiNINA, ArduinoJson 7, FlashStorage.
4. Build and upload: `arduino-cli compile --fqbn arduino:samd:nano_33_iot .` then
   `arduino-cli upload -p COM5 --fqbn arduino:samd:nano_33_iot .`
5. First boot is an egg that calibrates the motion sensor. Lay the device flat, then stand it up like a photo.
   The calibration is printed to Serial. Paste it into `config.h` (`CAL_FIXED_*`), because every upload erases flash.

## How to interact (no buttons)
| You do | It does (depends on mood and personality) |
|---|---|
| tilt | it, its food, puke, rain and snow all slide toward real "down" |
| shake | bounces off the walls, gets sick, may puke, faint or call you rude |
| tap | looks at you. Keep poking and it CHOMPs the glass |
| double tap | drops a snack. It may eat it, refuse it, hide it or yeet it |
| spin it | dizzy |
| toss / drop it | screams, then holds a grudge (brave ones say "AGAIN!!") |
| turn it upside down | it falls to the ceiling and sulks |
| lay it face down | "WHO TURNED OFF THE LIGHTS" |
| hold it | hearts if it trusts you, suspicion if it doesn't |
| walk around carrying it | walkies, which forgives grudges |
| ignore it | it gets bored, causes chaos, and eventually goes feral |

Weather, time and nearby WiFi names come from the internet. Expect umbrellas, snowmen, lightning, melting and gossip.
Grudges come back later as revenge: a flipped UI, lights off, a stolen clock, a fake crash, spite puke or graffiti.

## Serial console (115200)
Type `help`. Useful commands:
- `stat`
- `w rain|snow|storm|clear|fog|hot|cold|wind|real`
- `t 23:30`
- `e toss|shake|tap|dtap|spin|upside|facedown|walk`
- `n hunger 90`
- `b list` / `b <behavior>`
- `wipe` (new egg)

Thresholds and rates live in `config.h`. Use `stat` to see live shake, jitter and spin values while tuning.
