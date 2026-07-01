#include <Arduino.h>
#include "HX711.h"
#include <Preferences.h>
#include <Wire.h> 
#include<math.h>
#include <LiquidCrystal_I2C.h>

bool IOT_Mode = false;

// ---------------- PIN CONFIG ----------------
#define LOADCELL_DOUT_PIN 18
#define LOADCELL_SCK_PIN  19
#define ADD_BTN_PIN 25

float lockedWeight = 0;
bool weightLocked = false;

bool isZero = false;
const float minLockWeight = 50.0;
const float removeThreshold = 20.0;
const float stabilityThreshold = 5.0;
const int stableTime = 300;

// ---------------- LCD CONFIG ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Set the LCD I2C address and dimensions

Preferences preferences;

// Save factor
void saveCalibrationFactor(float factor) {
    preferences.begin("scale", false);
    preferences.putFloat("calibFactor", factor);
    preferences.end();
}

// Load factor
float loadCalibrationFactor(float defaultFactor) {
    preferences.begin("scale", true);
    float factor = preferences.getFloat("calibFactor", defaultFactor);
    preferences.end();
    Serial.print("Loaded Calibration Factor: ");
    Serial.println(factor, 6);
    return factor;
}

// ---------------- HX711 ----------------
HX711 scale;

// ---------------- QUEUES ----------------
QueueHandle_t rawWeightQueue;
QueueHandle_t stableWeightQueue;
QueueHandle_t barcodeQueue; // holds last scanned barcode
QueueHandle_t mqttQueue;

// ---------------- CALIBRATION ----------------
float calibrationFactor; // Default factor if not set

// ---------------- FILTER VARIABLES ----------------
#define MOVING_AVG_SIZE 2

float weightBuffer[MOVING_AVG_SIZE];
int bufferIndex = 0;

float expFilteredWeight = 0;
float alpha = 0.65;

// ---------------- TASK HANDLES ----------------
TaskHandle_t hx711TaskHandle;
TaskHandle_t filterTaskHandle;
TaskHandle_t serialTaskHandle;
//=================================================//

// =================================================
// MOVING AVERAGE FILTER
// =================================================
float movingAverage(float newValue) {
    weightBuffer[bufferIndex] = newValue;

    bufferIndex++;
    if(bufferIndex >= MOVING_AVG_SIZE)
        bufferIndex = 0;

    float sum = 0;

    for(int i=0;i<MOVING_AVG_SIZE;i++)
        sum += weightBuffer[i];

    return sum / MOVING_AVG_SIZE;
}

// =================================================
// EXPONENTIAL FILTER
// =================================================
float exponentialFilter(float newValue) {
    expFilteredWeight = alpha * newValue + (1 - alpha) * expFilteredWeight;
    return expFilteredWeight;
}

// =================================================
// HX711 TASK
// =================================================
void hx711Task(void *param) {
    scale.set_gain(128);

    float weight;

    for(;;)
    {
        if(scale.is_ready())
        {
            weight = scale.get_units(1);

            xQueueSend(rawWeightQueue,&weight,portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =================================================
// FILTER TASK
// =================================================
void filterTask(void *param) {
    float raw;
    float filtered;
    float lastStable = 0;

    int stableCount = 0;
    const float threshold = 3.0;
    const int stableLimit = 2;

    for(;;)
    {
        if(xQueueReceive(rawWeightQueue,&raw,portMAX_DELAY))
        {
            float avg = movingAverage(raw);
            filtered = exponentialFilter(avg);

            if(fabs(filtered - lastStable) < threshold)
            {
                stableCount++;
            }
            else
            {
                stableCount = 0;
            }

            lastStable = filtered;

            if(stableCount >= stableLimit)
            {
                xQueueSend(stableWeightQueue,&filtered,portMAX_DELAY);
                stableCount = 0;
            }
        }
    }
}


// =================================================
// SERIAL TASK
// =================================================
void serialTask(void *param) {
    float weight;
    float lastWeight = 0;

    unsigned long stableStartTime = 0;
    bool stabilityTimerStarted = false;

    char lastLCD[16] = "";
    char currentLCD[16];

    const bool use_barcode = true;  // enable barcode wait
    char scannedBarcode[64];

    const float nearZeroThreshold = 1.0; // readings within ±1 g are considered zero
    const float negativeLimit = -5.0;    // negatives less than this are valid

    static bool barcodeProcessed = false; // NEW: track if barcode already read for current weight

    for (;;)
    {
        if (xQueueReceive(stableWeightQueue, &weight, portMAX_DELAY)) {
            // -----------------------
            // SNAP NEAR ZERO
            // -----------------------
            if (weight > -nearZeroThreshold && weight < nearZeroThreshold)
            {
                weight = 0.0;
            }
            else if (weight < negativeLimit)
            {
                // keep large negative readings as-is
            }

            // -----------------------
            // UNLOCK IF WEIGHT REMOVED
            // -----------------------
            static int removeCounter = 0;

            if (weightLocked)
            {
                if (weight < removeThreshold)
                {
                    removeCounter++;
                    if (removeCounter > 3)
                    {
                        weightLocked = false;
                        removeCounter = 0;
                        Serial.println("Scale reset");
                        

                    }
                }
                else
                {
                    removeCounter = 0;
                }
            }

            // -----------------------
            // CHECK STABILITY & LOCK
            // -----------------------
            if (!weightLocked && weight > minLockWeight && fabs(weight - lastWeight) < stabilityThreshold)
            {
                if (!stabilityTimerStarted)
                {
                    stableStartTime = millis();
                    stabilityTimerStarted = true;
                }

                if (millis() - stableStartTime >= stableTime)
                {
                    lockedWeight = weight;
                    weightLocked = true;

                    Serial.print("Weight locked: ");
                    Serial.print(lockedWeight / 1000.0, 3);
                    Serial.println(" KG");

                    snprintf(currentLCD, sizeof(currentLCD), "%7.3f KG", lockedWeight / 1000.0);
                    lcd.setCursor(4, 1);
                    lcd.print(currentLCD);
                    strcpy(lastLCD, currentLCD);

                    stabilityTimerStarted = false;
                }
            }
            else
            {
                stabilityTimerStarted = false;
            }

            // -----------------------
            // LIVE LCD BEFORE LOCK
            // -----------------------
            if (!weightLocked || !barcodeProcessed)
            {
                snprintf(currentLCD, sizeof(currentLCD), "%7.3f KG", weight / 1000.0);
                if (strcmp(currentLCD, lastLCD) != 0)
                {
                    lcd.setCursor(4, 1);
                    lcd.print(currentLCD);
                    strcpy(lastLCD, currentLCD);
                }
            }

            lastWeight = weight;
        }

        // Serial.println(analogRead(ADD_BTN_PIN));
    }
}

// =================================================
// CALIBRATION ROUTINE
// =================================================
void runCalibration() {
    Serial.println("\n--- CALIBRATION MODE ---");
    lcd.setCursor(0,0);
    lcd.print("Calibration Mode");

    scale.begin(LOADCELL_DOUT_PIN,LOADCELL_SCK_PIN);

    Serial.println("Remove all weight.");
    lcd.setCursor(0,1);
    lcd.print("Remove all Wt.");
    delay(5000);

    scale.set_scale();
    scale.tare();

    Serial.println(" Tare complete ");
    lcd.setCursor(0,1);
    lcd.print(" Tare complete ");


    Serial.println("Place known weight on scale...");
    lcd.setCursor(0,1);
    lcd.print("Place known Wt");

    long rawReading = 0;
    const long minWeightThreshold = 1000; // grams or approximate raw units

    while (true)
    {
        if (scale.is_ready())
        {
            rawReading = scale.get_units(10); // or raw value

            // check if weight is actually placed
            if (fabs(rawReading) > minWeightThreshold)
            {
                Serial.print("Weight detected! Raw: ");
                lcd.setCursor(0,1);
                lcd.print("  Wt Detected  ");
                Serial.println(rawReading);
                break;
            }
            else
            {
                Serial.println("Waiting for known weight...");
                lcd.setCursor(0,1);
                lcd.print(" Wait for Wt.. ");
            }
        }
        delay(1000);
    }

    // Stabilization loop
    long previous = scale.get_units(10);
    int stableCount = 0;

    const int stableLimit = 5;      // require 5 stable readings
    const float threshold = 100.0;    // grams difference allowed

    Serial.println("Stabilizing readings...");
    lcd.setCursor(0,1);
    lcd.print(" Processing... ");

    while(stableCount < stableLimit)
    {
        long current = scale.get_units(10);

        Serial.print("Reading: ");
        Serial.println(current);

        if(fabs(current - previous) < threshold)
        {
            stableCount++;
        }
        else
        {
            stableCount = 0;
        }

        previous = current;

        delay(500);
    }
    Serial.println("Reading stabilized.");
    //=============================================================//

    // Average multiple readings for better accuracy
    long sum = 0;
    const int samples = 10;

    Serial.println("Collecting samples for averaging...");

    for(int i=0;i<samples;i++)
    {
        long reading = scale.get_units(10);

        Serial.print("Sample ");
        Serial.print(i+1);
        Serial.print(": ");
        Serial.println(reading);

        sum += reading;

        delay(500);
    }

    rawReading = sum / samples;

    Serial.print("Average reading = ");
    Serial.println(rawReading);
    // Calculate calibration factor

    Serial.println("Enter known weight in grams:");
    lcd.setCursor(0,1);
    lcd.print("Wait for Value ");

    while(!Serial.available());

    float knownWeight = Serial.parseFloat();

    calibrationFactor = rawReading / knownWeight;

    Serial.print("New Calibration Factor = ");
    Serial.println(calibrationFactor,6);

    saveCalibrationFactor(calibrationFactor);

    for(int i=10;i>0;i--)
    {
        Serial.print("Restarting in ");
        Serial.print(i);
        Serial.println(" seconds...");
        delay(1000);
    }

    ESP.restart();

    while(true);
}


// =================================================
// SETUP
// =================================================
void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("===============================");
    Serial.println(" DMA-PATHAO Smart Weight Scale ");
    Serial.println("==============================!");
    Serial.println();

    delay(2000);

     lcd.init();                      // initialize the lcd 
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.clear();
    lcd.print("USB Serial Mode");
    lcd.setCursor(0, 1);
    lcd.print("Wt: ");
    lcd.setCursor(4, 1);
    lcd.print("--------");

    pinMode(ADD_BTN_PIN, INPUT_PULLUP);

    if(digitalRead(ADD_BTN_PIN) == LOW){
        Serial.println("===============================");
        Serial.println("  Device is Calibration Mode!  ");
        Serial.println("==============================!");
        Serial.println();
        runCalibration();
    }

    Serial.println("===============================");
    Serial.println("     Device is USB-PC Mode!    ");
    Serial.println("==============================!");
    Serial.println();

    scale.begin(LOADCELL_DOUT_PIN,LOADCELL_SCK_PIN);
    
    calibrationFactor = loadCalibrationFactor(100.0f); // Default factor if not set
    scale.set_scale(calibrationFactor);
    scale.tare();

    // Create queues
    rawWeightQueue = xQueueCreate(10,sizeof(float));
    stableWeightQueue = xQueueCreate(5,sizeof(float));

    // Create tasks
    xTaskCreatePinnedToCore(
        hx711Task,
        "HX711 Task",
        4096,
        NULL,
        2,
        &hx711TaskHandle,
        1);

    xTaskCreatePinnedToCore(
        filterTask,
        "Filter Task",
        4096,
        NULL,
        1,
        &filterTaskHandle,
        1);

    xTaskCreatePinnedToCore(
        serialTask,
        "Serial Task",
        4096,
        NULL,
        1,
        &serialTaskHandle,
        1);
    
}


void loop() {
    // Empty loop since tasks are handling the operations
}
