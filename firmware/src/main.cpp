// PetBot firmware: boot banner, UART link to the brain, debug keys on USB serial.
#include <Arduino.h>

#include "config.h"
#include "link.h"

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
    Serial.print("[debug] keys:");
    for (const DebugKey& k : kDebugKeys) {
        Serial.print(' ');
        Serial.print(k.key);
        Serial.print('=');
        Serial.print(k.source);
        Serial.print(' ');
        Serial.print(k.detail);
    }
    Serial.println("  ?=help");
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
    Serial.println();
    Serial.println("PetBot firmware v0.1.0 - boot OK");
    petbot::link::begin();
    printHelp();
}

void loop() {
    while (Serial.available() > 0) {
        handleDebugKey(static_cast<char>(Serial.read()));
    }
    delay(10);
}
