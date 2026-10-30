// PetBot firmware placeholder: prints a boot banner only.
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("PetBot firmware v0.0.0 - boot OK");
}

void loop() {
    delay(1000);
}
