#include <Arduino.h>
#include "weight_scale.h"

WeightScale scale;

void setup() {
    if (!scale.begin()) {
        Serial.println("Failed to initialize scale system");
        while (1) { delay(1000); }
    }
}

void loop() {
    // Nothing to do; tasks run independently
    vTaskDelay(pdMS_TO_TICKS(1000));
}