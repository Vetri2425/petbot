// PetBot firmware: boot banner, face on the ST7789, UART link to the brain, debug keys.
#include <Arduino.h>

#include <stdio.h>

#include "config.h"
#include "display.h"
#include "link.h"
#include "serial_log.h"

namespace {

// Debug keys (same as sim/fake_esp32.py): type one on USB serial to send an event.
struct DebugKey {
    char key;
    const char* source;
    const char* detail;
};
const DebugKey kDebugKeys[] = {
    {'h', "touch", "head"},  {'l', "touch", "left"}, {'r', "touch", "right"},
    {'b', "touch", "back"},  {'p', "imu", "pickup"}, {'s', "imu", "shake"},
    {'t', "imu", "tilt"},    {'f', "imu", "fall"},
};

void printHelp() {
    char help[192] = "";
    size_t n = 0;
    for (const DebugKey& k : kDebugKeys) {
        const int w = snprintf(help + n, sizeof(help) - n, " %c=%s %s", k.key, k.source, k.detail);
        if (w < 0 || static_cast<size_t>(w) >= sizeof(help) - n) break;
        n += static_cast<size_t>(w);
    }
    petbot::logLine("[debug] keys:%s  ?=help", help);
}

void handleDebugKey(char c) {
    if (c == '?') {
        printHelp();
        return;
    }
    for (const DebugKey& k : kDebugKeys) {
        if (k.key == c) {
            petbot::link::sendEvent(k.source, k.detail);
            return;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(petbot::config::kUsbBaud);
    delay(500);
    petbot::logBegin();
    petbot::logLine("%s", "");
    petbot::logLine("PetBot firmware v0.1.0 - boot OK");
    petbot::display::begin();  // before the link, so its queue exists for the first MOOD/ANIM
    petbot::link::begin();
    printHelp();
}

void loop() {
    while (Serial.available() > 0) {
        handleDebugKey(static_cast<char>(Serial.read()));
    }
    delay(10);
}
