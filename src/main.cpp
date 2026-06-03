#include <Arduino.h>
#include "HX711.h"
#include <Preferences.h>
#include <Wire.h> 
#include<math.h>
#include <LiquidCrystal_I2C.h>
#include "EspUsbHost.h"

#include <WiFi.h>
#include <PubSubClient.h>

bool IOT_Mode = false;

#define ADD_BTN_PIN 4
#define M1_BTN_PIN 5
#define M2_BTN_PIN 7
#define M3_BTN_PIN 6
#define TARE_BTN_PIN 16
#define ZERO_BTN_PIN 15

#define MQTT_QUEUE_LENGTH 10

struct MqttData {
    float weight;          // in grams
    char barcode[64];      // barcode string
};


// ======================= Barcode Related ==================//
#define BARCODE_MAX_LEN 64
char barcodeBuffer[BARCODE_MAX_LEN];
char lastBarcode[BARCODE_MAX_LEN];
volatile bool barcodeReady = false;

uint8_t indexPos = 0;
class MyEspUsbHost : public EspUsbHost {

    void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) {

        // Ignore empty reports (key release)
        if (keycode == 0) return;

        // ENTER → barcode complete
        if (ascii == '\r') {
            if (indexPos > 0) {
            barcodeBuffer[indexPos] = '\0';
            strcpy(lastBarcode, barcodeBuffer);
            barcodeReady = true;
            indexPos = 0;
            }
            return;
        }

        // Accept only numeric characters (barcode safe filtering)
        if (ascii >= '0' && ascii <= '9') {

            if (indexPos < BARCODE_MAX_LEN - 1) {
            barcodeBuffer[indexPos++] = ascii;
            }
        }
    }
};

MyEspUsbHost usbHost;

// ---------------- PIN CONFIG ----------------
#define LOADCELL_DOUT_PIN 1
#define LOADCELL_SCK_PIN  2 

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
TaskHandle_t barcodeTaskHandle;
TaskHandle_t wifiMqttTaskHandle;
TaskHandle_t mqttSendTaskHandle;
//=================================================//

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void wifiMqttTask(void *param)
{
    for (;;)
    {
        // WiFi check
        if (WiFi.status() != WL_CONNECTED)
        {
            lcd.setCursor(14, 0);
            lcd.print("-");
            Serial.println("Reconnecting WiFi...");
            // WiFi.begin("DMA-Link3-2Gn", "dmabd987");
            WiFi.begin("Reza", "rezakhan");
            while (WiFi.status() != WL_CONNECTED)
            {
                Serial.print(".");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            Serial.println("\nWiFi Connected!");
            lcd.setCursor(14, 0);
            lcd.print("W");
        }

        // MQTT check
        if (!mqttClient.connected())
        {
            lcd.setCursor(15, 0);
            lcd.print("-");
            Serial.println("Reconnecting MQTT...");
            while (!mqttClient.connected())
            {
                if (mqttClient.connect("ESP32_Scale", "broker2", "Secret!@#$1234"))
                {
                    Serial.println("MQTT Connected!");
                    lcd.setCursor(15, 0);
                    lcd.print("M");
                }
                else
                {
                    Serial.print("Failed rc=");
                    Serial.print(mqttClient.state());
                    Serial.println(" Retrying in 2s...");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
        }

        // MUST call loop() to process MQTT messages
        mqttClient.loop();

        vTaskDelay(pdMS_TO_TICKS(100)); // small delay, fast enough for loop
    }
}


void mqttSendTask(void *param)
{
    MqttData data;

    for (;;)
    {
        //Only send if MQTT is connected
        if (mqttClient.connected())
        {
            if (xQueueReceive(mqttQueue, &data, portMAX_DELAY))
            {
                #define DEVICE_ID "1285002603110001"

                char payload[128];

                // Device_ID,weight,barcode
                snprintf(payload, sizeof(payload), "%s,%.2f,%s",
                        DEVICE_ID,
                        data.weight,
                        data.barcode);

                mqttClient.publish("Pathao/WeightScale/PUB", payload);

                Serial.print("I am from Mqtt Queue: ");
                Serial.println(payload);
                lcd.setCursor(11, 0);
                lcd.print("DS");
            }
            
        }
        else
        {
            // MQTT not connected, skip sending for now, will retry next data
            // Serial.println("MQTT not connected, waiting...");
        }
    }
}

/*
void wifiMqttTask(void *param)
{
    for (;;)
    {
        // WiFi check
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Reconnecting WiFi...");
            WiFi.begin("DMA-Link3-2Gn", "dmabd987");
            while (WiFi.status() != WL_CONNECTED)
            {
                Serial.print(".");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            Serial.println("\nWiFi Connected!");
        }

        // MQTT check
        if (!mqttClient.connected())
        {
            Serial.println("Reconnecting MQTT...");
            while (!mqttClient.connected())
            {
                if (mqttClient.connect("ESP32_Scale", "broker2.dma-bd.com", "Secret!@#$1234"))
                {
                    Serial.println("MQTT Connected!");
                }
                else
                {
                    Serial.print("Failed rc=");
                    Serial.print(mqttClient.state());
                    Serial.println(" Retrying in 2s...");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
            }
        }

        // MUST call loop() to process MQTT messages
        mqttClient.loop();

        vTaskDelay(pdMS_TO_TICKS(100)); // small delay, fast enough for loop
    }
}
*/

// =================================================
// MOVING AVERAGE FILTER
// =================================================
float movingAverage(float newValue)
{
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
float exponentialFilter(float newValue)
{
    expFilteredWeight = alpha * newValue + (1 - alpha) * expFilteredWeight;
    return expFilteredWeight;
}

// =================================================
// HX711 TASK
// =================================================
void hx711Task(void *param)
{
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
void filterTask(void *param)
{
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
// BARCODE TASK
// =================================================

void barcodeTask(void *param)
{
    const int maxLen = 64;
    char barcode[maxLen];
    
    for (;;)
    {
        usbHost.task(); // keep USB host alive
        
        if (barcodeReady)
        {
            barcodeReady = false;
            
            strncpy(barcode, lastBarcode, maxLen);
            barcode[maxLen-1] = '\0';
            
            // send to queue
            xQueueSend(barcodeQueue, &barcode, portMAX_DELAY);
            
            // reset for next scan
            memset(lastBarcode, 0, maxLen);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // small delay
    }
}

// =================================================
// SERIAL TASK
// =================================================
void serialTask(void *param)
{
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
        if (xQueueReceive(stableWeightQueue, &weight, portMAX_DELAY))
        {
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
                        if(IOT_Mode)
                        {
                            barcodeProcessed = false; // allow barcode for next weight
                            if(xQueueReceive(barcodeQueue, &scannedBarcode, 0) == pdTRUE)
                            {
                                // Clear any pending barcode if user removed weight before processing
                                scannedBarcode[0] = '\0';
                            }
                            barcodeBuffer[0] = '\0'; // clear barcode buffer
                            lcd.setCursor(11, 0);
                            lcd.print("--");
                        }
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

                    // -----------------------
                    // WAIT FOR BARCODE IF ENABLED AND NOT PROCESSED
                    // -----------------------
                    if (IOT_Mode && !barcodeProcessed)
                    {
                        while (xQueueReceive(barcodeQueue, &scannedBarcode, portMAX_DELAY) != pdTRUE)
                        {
                            // live LCD while waiting
                            snprintf(currentLCD, sizeof(currentLCD), "%7.3f KG", lockedWeight / 1000.0);
                            if (strcmp(currentLCD, lastLCD) != 0)
                            {
                                lcd.setCursor(4, 1);
                                lcd.print(currentLCD);
                                strcpy(lastLCD, currentLCD);
                            }
                            vTaskDelay(pdMS_TO_TICKS(50));
                        }

                        Serial.print("Weight: ");
                        Serial.print(lockedWeight / 1000.0, 3);
                        Serial.print(" KG, Barcode: ");
                        Serial.println(scannedBarcode);

                        MqttData mData;
                        mData.weight = lockedWeight / 1000.0; // in KG
                        strcpy(mData.barcode, scannedBarcode);

                        xQueueSend(mqttQueue, &mData, portMAX_DELAY);

                        barcodeProcessed = true; // mark as done for this weight
                    }
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
    }
}

// =================================================
// CALIBRATION ROUTINE
// =================================================
void runCalibration()
{
    Serial.println("\n--- CALIBRATION MODE ---");

    scale.begin(LOADCELL_DOUT_PIN,LOADCELL_SCK_PIN);

    Serial.println("Remove all weight.");
    delay(5000);

    scale.set_scale();
    scale.tare();

    Serial.println("Tare complete.");


    Serial.println("Place known weight on scale...");

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
                Serial.println(rawReading);
                break;
            }
            else
            {
                Serial.println("Waiting for known weight...");
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
void setup()
{
    Serial.begin(115200);
    delay(500);

    pinMode(4, INPUT_PULLUP);

    if(digitalRead(4) == LOW)
    {
        // runCalibration();
        Serial.println("Calibration button pressed. Running calibration...");
        IOT_Mode = true; // Force USB Serial mode for calibration
    }
    else
    {
        Serial.println("Normal startup. Calibration button not pressed.");
    }

    if(IOT_Mode)
    {
        // USB Host Setup
        usbHost.begin();
        usbHost.setHIDLocal(HID_LOCAL_US);
    }

    // Normal Mode
    // Serial.println("\n--- NORMAL MODE ---");
    
    scale.begin(LOADCELL_DOUT_PIN,LOADCELL_SCK_PIN);
    
    calibrationFactor = loadCalibrationFactor(1.0f); // Default factor if not set
    scale.set_scale(calibrationFactor);
    scale.tare();

    if(IOT_Mode)
    {
        Serial.println("IOT Mode: MQTT tasks will be created");
        lcd.init();                      // initialize the lcd 
        lcd.backlight();
        lcd.setCursor(0, 0);
        lcd.clear();
        lcd.print("IOT Mode");
        lcd.setCursor(0, 1);
        lcd.print("Wt: ");
        lcd.setCursor(4, 1);
        lcd.print("--------");

        lcd.setCursor(11, 0);
        lcd.print("-- --");
    }
    else
    {
        Serial.println("USB Serial Mode: Barcode scanning disabled");
        lcd.init();                      // initialize the lcd 
        lcd.backlight();
        lcd.setCursor(0, 0);
        lcd.clear();
        lcd.print("USB Serial Mode");
        lcd.setCursor(0, 1);
        lcd.print("Wt: ");
        lcd.setCursor(4, 1);
        lcd.print("--------");
    }


    mqttClient.setServer("broker2.dma-bd.com", 1883);
    // mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
    //     // Handle incoming MQTT messages if needed
    // });
    mqttClient.setKeepAlive(60);

    // Create queues
    rawWeightQueue = xQueueCreate(10,sizeof(float));
    stableWeightQueue = xQueueCreate(5,sizeof(float));
    barcodeQueue = xQueueCreate(5,sizeof(barcodeBuffer));
    mqttQueue = xQueueCreate(MQTT_QUEUE_LENGTH, sizeof(MqttData));

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

        if(IOT_Mode == true){
                Serial.println("IOT Mode: MQTT tasks will be created");
                xTaskCreatePinnedToCore(
                    barcodeTask,
                    "Barcode Task",
                    4096,
                    NULL,
                    2,
                    &barcodeTaskHandle,
                    1);

                xTaskCreatePinnedToCore(
                    wifiMqttTask,
                    "WiFi MQTT Task",
                    4096,
                    NULL,
                    1,
                    &wifiMqttTaskHandle,
                    1);

                xTaskCreatePinnedToCore(
                    mqttSendTask,
                    "MQTT Send Task",
                    4096,
                    NULL,
                    1,
                    &mqttSendTaskHandle,
                    1);
        }
    
}


void loop()
{
}
