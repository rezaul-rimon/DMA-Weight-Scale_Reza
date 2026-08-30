#ifndef WEIGHT_SCALE_H
#define WEIGHT_SCALE_H

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "config.h"  

class WeightScale {
public:
    WeightScale();
    ~WeightScale();

    // Initialize hardware, preferences, queues, and tasks
    bool begin();

    // Calibration entry point (call from main if button pressed)
    void startCalibration();

    // Task functions (static wrappers)
    static void hx711TaskWrapper(void* param);
    static void filterTaskWrapper(void* param);
    static void serialTaskWrapper(void* param);

private:
    // Hardware
    HX711 scale_;
    LiquidCrystal_I2C lcd_;
    Preferences prefs_;

    // Queues
    QueueHandle_t rawQueue_;
    QueueHandle_t stableQueue_;

    // Calibration factor
    float calibrationFactor_;

    // Filtering state
    float weightBuffer_[MOVING_AVG_SIZE];
    int bufferIndex_;
    
    float medianBuffer_[MEDIAN_SIZE];
    int medianIndex_ = 0;
    
    float trimmedBuffer_[TRIMMED_MEAN_SIZE];
    int trimmedIndex_ = 0;

    float expFilteredWeight_;

    // Weight locking state
    float lockedWeight_;
    bool weightLocked_;
    bool isZero_; // not used but kept for potential future use

    // Task handles
    TaskHandle_t hx711TaskHandle_;
    TaskHandle_t filterTaskHandle_;
    TaskHandle_t serialTaskHandle_;

    // Private methods
    void loadCalibrationFactor(float defaultFactor);
    void saveCalibrationFactor(float factor);
    float movingAverage(float newValue);
    float medianFilter(float newValue);
    float trimmedMeanFilter(float newValue);
    float exponentialFilter(float newValue);
    void processSerialOutput(); // main loop of serial task
    void processFilter();        // main loop of filter task
    void readHX711();            // main loop of HX711 task
    void updateLCD(const char* line1, const char* line2);
};

#endif // WEIGHT_SCALE_H