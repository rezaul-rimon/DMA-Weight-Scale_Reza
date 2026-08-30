#include "weight_scale.h"
#include "config.h"

// --------------------------
// Constructor / Destructor
// --------------------------
WeightScale::WeightScale()
: lcd_(LCD_ADDR, LCD_COLS, LCD_ROWS),
    bufferIndex_(0),
    expFilteredWeight_(0.0f),
    lockedWeight_(0.0f),
    weightLocked_(false),
    isZero_(false),
    hx711TaskHandle_(nullptr),
    filterTaskHandle_(nullptr),
    serialTaskHandle_(nullptr) {
}

WeightScale::~WeightScale() {
    if (rawQueue_) vQueueDelete(rawQueue_);
    if (stableQueue_) vQueueDelete(stableQueue_);
}

// --------------------------
// Initialization
// --------------------------
bool WeightScale::begin() {
    Serial.begin(115200);
    delay(100);

    // Print device info
    Serial.println("===============================");
    Serial.println(" DMA-PATHAO Smart Weight Scale ");
    Serial.printf(" Device ID: %s\n", DEVICE_ID);
     Serial.printf(" APP ID: %s\n", APPID);
    Serial.printf(" Device Model: %s\n", DEVICE_MODEL);
     Serial.printf(" Batch ID: %s\n", BATCH_ID);
     Serial.printf(" Manufacturing Date: %s\n", MANUFACTURING_DATE);
    Serial.printf(" Release from DMA: %s\n", RELEASE_DATE);
    Serial.printf(" Firmware Version: %s\n", FIRMWARE_VERSION);
    Serial.printf(" Hardware Verion: %s\n", HARDWARE_VERSION);
    Serial.printf(" Device Capacity: %.2f KG\n", DEVICE_CAPACITY_KG);
    Serial.println("==============================!");
    Serial.println();

    // Initialize LCD
    lcd_.init();
    lcd_.backlight();
    lcd_.clear();
    lcd_.setCursor(0, 0);
    lcd_.print("USB Serial Mode");
    lcd_.setCursor(0, 1);
    lcd_.print("Wt: ");
    lcd_.setCursor(4, 1);
    lcd_.print("--------");

    // Button
    pinMode(ADD_BTN_PIN, INPUT_PULLUP);

    // Check calibration mode
    if (digitalRead(ADD_BTN_PIN) == LOW) {
        Serial.println("Calibration mode requested");
        startCalibration();
        // After calibration, ESP.restart() will be called, so no return
    }

    // Initialize HX711
    scale_.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    loadCalibrationFactor(DEFAULT_CALIB_FACTOR);
    scale_.set_scale(calibrationFactor_);
    scale_.tare();

    // Create queues
    rawQueue_ = xQueueCreate(QUEUE_RAW_SIZE, sizeof(float));
    stableQueue_ = xQueueCreate(QUEUE_STABLE_SIZE, sizeof(float));
    if (rawQueue_ == nullptr || stableQueue_ == nullptr) {
        Serial.println("Failed to create queues!");
        return false;
    }

    // Create tasks
    BaseType_t result = xTaskCreatePinnedToCore(
        hx711TaskWrapper, "HX711", HX711_TASK_STACK,
        this, 2, &hx711TaskHandle_, 0); // Core 0 for real-time
    if (result != pdPASS) return false;

    result = xTaskCreatePinnedToCore(
        filterTaskWrapper, "Filter", FILTER_TASK_STACK,
        this, 1, &filterTaskHandle_, 1); // Core 1
    if (result != pdPASS) return false;

    result = xTaskCreatePinnedToCore(
        serialTaskWrapper, "Serial", SERIAL_TASK_STACK,
        this, 1, &serialTaskHandle_, 1); // Core 1
    if (result != pdPASS) return false;

    Serial.println("Device ready");
    return true;
}

// --------------------------
// Calibration
// --------------------------
void WeightScale::startCalibration() {
    Serial.println("\n--- CALIBRATION MODE ---");
    lcd_.clear();
    lcd_.setCursor(0, 0);
    lcd_.print("Calibration Mode");
    lcd_.setCursor(0, 1);
    lcd_.print("Remove all Wt.");

    scale_.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    delay(5000);

    scale_.set_scale();
    scale_.tare();
    Serial.println("Tare complete");
    lcd_.setCursor(0, 1);
    lcd_.print("Tare complete  ");

    Serial.println("Place known weight...");
    lcd_.setCursor(0, 1);
    lcd_.print("Place known Wt ");

    // Wait for weight
    long rawReading = 0;
    while (true) {
        if (scale_.is_ready()) {
            rawReading = scale_.get_units(10);
            if (fabs(rawReading) > CALIB_MIN_RAW_THRESHOLD) {
                Serial.printf("Weight detected! Raw: %ld\n", rawReading);
                lcd_.setCursor(0, 1);
                lcd_.print("Wt Detected    ");
                break;
            }
        }
        delay(1000);
    }

    // Stabilize
    long previous = scale_.get_units(10);
    int stableCount = 0;
    Serial.println("Stabilizing...");
    lcd_.setCursor(0, 1);
    lcd_.print("Processing...  ");

    while (stableCount < CALIB_STABLE_COUNT) {
        long current = scale_.get_units(10);
        Serial.printf("Reading: %ld\n", current);
        if (fabs(current - previous) < CALIB_STABLE_THRESHOLD) {
            stableCount++;
        } else {
            stableCount = 0;
        }
        previous = current;
        delay(500);
    }

    // Average samples
    long sum = 0;
    Serial.println("Collecting samples...");
    for (int i = 0; i < CALIB_SAMPLE_COUNT; i++) {
        long reading = scale_.get_units(10);
        Serial.printf("Sample %d: %ld\n", i + 1, reading);
        sum += reading;
        delay(500);
    }
    rawReading = sum / CALIB_SAMPLE_COUNT;
    Serial.printf("Average raw: %ld\n", rawReading);

    // Get known weight
    Serial.println("Enter known weight in grams:");
    lcd_.setCursor(0, 1);
    lcd_.print("Wait for Value ");
    while (!Serial.available());
    float knownWeight = Serial.parseFloat();

    calibrationFactor_ = rawReading / knownWeight;
    saveCalibrationFactor(calibrationFactor_);
    Serial.printf("New calibration factor: %.6f\n", calibrationFactor_);

    for (int i = 10; i > 0; i--) {
        Serial.printf("Restarting in %d seconds...\n", i);
        delay(1000);
    }
    ESP.restart();
}

// --------------------------
// Preferences
// --------------------------
void WeightScale::loadCalibrationFactor(float defaultFactor) {
    prefs_.begin("scale", true);
    calibrationFactor_ = prefs_.getFloat("calibFactor", defaultFactor);
    prefs_.end();
    Serial.printf("Loaded calibration factor: %.6f\n", calibrationFactor_);
}

void WeightScale::saveCalibrationFactor(float factor) {
    prefs_.begin("scale", false);
    prefs_.putFloat("calibFactor", factor);
    prefs_.end();
}

// --------------------------
// Filtering
// --------------------------
float WeightScale::movingAverage(float newValue) {
    weightBuffer_[bufferIndex_] = newValue;
    bufferIndex_ = (bufferIndex_ + 1) % MOVING_AVG_SIZE;
    float sum = 0;
    for (int i = 0; i < MOVING_AVG_SIZE; i++) {
        sum += weightBuffer_[i];
    }
    return sum / MOVING_AVG_SIZE;
}

// Median filter
float WeightScale::medianFilter(float newValue) {
    medianBuffer_[medianIndex_] = newValue;
    medianIndex_ = (medianIndex_ + 1) % MEDIAN_SIZE;

    // Copy buffer for sorting
    float temp[MEDIAN_SIZE];
    memcpy(temp, medianBuffer_, sizeof(temp));

    // Simple insertion sort (small array)
    for (int i = 1; i < MEDIAN_SIZE; i++) {
        float key = temp[i];
        int j = i - 1;
        while (j >= 0 && temp[j] > key) {
            temp[j + 1] = temp[j];
            j--;
        }
        temp[j + 1] = key;
    }

    // Return middle element
    return temp[MEDIAN_SIZE / 2];
}

// Trimmed mean filter
float WeightScale::trimmedMeanFilter(float newValue) {
    trimmedBuffer_[trimmedIndex_] = newValue;
    trimmedIndex_ = (trimmedIndex_ + 1) % TRIMMED_MEAN_SIZE;

    // Copy and sort
    float temp[TRIMMED_MEAN_SIZE];
    memcpy(temp, trimmedBuffer_, sizeof(temp));

    for (int i = 1; i < TRIMMED_MEAN_SIZE; i++) {
        float key = temp[i];
        int j = i - 1;
        while (j >= 0 && temp[j] > key) {
            temp[j + 1] = temp[j];
            j--;
        }
        temp[j + 1] = key;
    }

    // Average the middle (discard lowest and highest TRIMMED_DISCARD)
    float sum = 0;
    int count = 0;
    for (int i = TRIMMED_DISCARD; i < TRIMMED_MEAN_SIZE - TRIMMED_DISCARD; i++) {
        sum += temp[i];
        count++;
    }
    return sum / count;
}

float WeightScale::exponentialFilter(float newValue) {
    expFilteredWeight_ = ALPHA * newValue + (1.0f - ALPHA) * expFilteredWeight_;
    return expFilteredWeight_;
}

// --------------------------
// Task Wrappers
// --------------------------
void WeightScale::hx711TaskWrapper(void* param) {
    static_cast<WeightScale*>(param)->readHX711();
}

void WeightScale::filterTaskWrapper(void* param) {
    static_cast<WeightScale*>(param)->processFilter();
}

void WeightScale::serialTaskWrapper(void* param) {
    static_cast<WeightScale*>(param)->processSerialOutput();
}

// --------------------------
// Task Implementations
// --------------------------
void WeightScale::readHX711() {
    float weight;
    for (;;) {
        if (scale_.is_ready()) {
            weight = scale_.get_units(1);
            xQueueSend(rawQueue_, &weight, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WeightScale::processFilter() {
    float raw, filtered;
    float lastStable = 0.0f;
    int stableCount = 0;
    const float threshold = DEVICE_CAPACITY_KG * 0.15f; // kg
    const int stableLimit = 2;

    for (;;) {
        if (xQueueReceive(rawQueue_, &raw, portMAX_DELAY)) {
            float avg = movingAverage(raw);
            // float avg = medianFilter(raw);
            // float avg = trimmedMeanFilter(raw);

            filtered = exponentialFilter(avg);

            if (fabs(filtered - lastStable) < threshold) {
                stableCount++;
            } else {
                stableCount = 0;
            }
            lastStable = filtered;

            if (stableCount >= stableLimit) {
                xQueueSend(stableQueue_, &filtered, portMAX_DELAY);
                stableCount = 0;
            }
        }
    }
}

void WeightScale::processSerialOutput() {
    float weight;
    float lastWeight = 0.0f;
    unsigned long stableStartTime = 0;
    bool stabilityTimerStarted = false;
    char lastLCD[16] = "";
    char currentLCD[16];
    int removeCounter = 0;

    for (;;) {
        if (xQueueReceive(stableQueue_, &weight, portMAX_DELAY)) {
            // Snap near zero
            if (weight > -NEAR_ZERO_THRESHOLD && weight < NEAR_ZERO_THRESHOLD) {
                weight = 0.0f;
            }

            // Unlock if weight removed
            if (weightLocked_) {
                if (weight < REMOVE_THRESHOLD) {
                    removeCounter++;
                    if (removeCounter > 3) {
                        weightLocked_ = false;
                        removeCounter = 0;
                        Serial.println("Scale reset");
                    }
                } else {
                    removeCounter = 0;
                }
            }

            // Check stability & lock
            if (!weightLocked_ && weight > MIN_LOCK_WEIGHT &&
                fabs(weight - lastWeight) < STABILITY_THRESHOLD) {
                if (!stabilityTimerStarted) {
                    stableStartTime = millis();
                    stabilityTimerStarted = true;
                }
                if (millis() - stableStartTime >= STABLE_TIME_MS) {
                    lockedWeight_ = weight;
                    weightLocked_ = true;
                    Serial.printf("Weight locked: %.3f KG\n", lockedWeight_);
                    snprintf(currentLCD, sizeof(currentLCD), "%7.3f KG", lockedWeight_);
                    lcd_.setCursor(4, 1);
                    lcd_.print(currentLCD);
                    strcpy(lastLCD, currentLCD);
                    stabilityTimerStarted = false;
                }
            } else {
                stabilityTimerStarted = false;
            }

            // Live LCD before lock
            if (!weightLocked_) {
                snprintf(currentLCD, sizeof(currentLCD), "%7.3f KG", weight);
                if (strcmp(currentLCD, lastLCD) != 0) {
                    lcd_.setCursor(4, 1);
                    lcd_.print(currentLCD);
                    strcpy(lastLCD, currentLCD);
                }
            }

            lastWeight = weight;
        }
        // Optional: check button for other commands
    }
}