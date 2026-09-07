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
#include "seven_seg.h"

class WeightScale {
public:
    WeightScale();
    ~WeightScale();

    bool begin();
    void startCalibration();

    // Task wrappers
    static void hx711TaskWrapper(void* param);
    static void filterTaskWrapper(void* param);
    static void serialTaskWrapper(void* param);
    static void lcdTaskWrapper(void* param);

private:
    // Hardware
    HX711 scale_;
    #if defined(USE_LCD)
    LiquidCrystal_I2C lcd_;
    #endif

    #if defined(USE_SEVEN_SEGMENT)
    SevenSegDisplay display_;
    #endif
    
    Preferences prefs_;

    // Queues
    QueueHandle_t rawQueue_;
    QueueHandle_t stableQueue_;
    QueueHandle_t lcdQueue_;

    // Task handles
    TaskHandle_t hx711TaskHandle_;
    TaskHandle_t filterTaskHandle_;
    TaskHandle_t serialTaskHandle_;
    TaskHandle_t lcdTaskHandle_;

    // Calibration factor
    float calibrationFactor_;

    // Filtering state
    float weightBuffer_[MOVING_AVG_SIZE];
    int bufferIndex_;
    float expFilteredWeight_;

    // New filter buffers
    float medianBuffer_[MEDIAN_SIZE];
    int medianIndex_;
    float trimmedBuffer_[TRIMMED_MEAN_SIZE];
    int trimmedIndex_;

    // Weight locking state
    float lockedWeight_;      // in grams
    bool weightLocked_;
    bool isZero_; // unused

    // LCD update state
    float lastDisplayedWeight_;   // in grams
    unsigned long lastLcdUpdateMs_;
    bool lastLockedState_;

    // Private methods
    void loadCalibrationFactor(float defaultFactor);
    void saveCalibrationFactor(float factor);
    float movingAverage(float newValue);
    float medianFilter(float newValue);
    float trimmedMeanFilter(float newValue);
    float exponentialFilter(float newValue);

    void processHX711();
    void processFilter();
    void processSerialOutput();
    void processLcdUpdates();
};

#endif // WEIGHT_SCALE_H