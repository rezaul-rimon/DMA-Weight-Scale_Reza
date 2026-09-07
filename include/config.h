#ifndef CONFIG_H
#define CONFIG_H


// --------------------------
// Device Identification
// --------------------------
constexpr const char* DEVICE_ID = "12850017";
constexpr const char* APPID = "1285.2";
constexpr const char* DEVICE_MODEL = "MEGA-T-20";
constexpr const char* BATCH_ID = "2608.1";
constexpr const char* MANUFACTURING_DATE = "01-09-2026";
constexpr const char* RELEASE_DATE = "07-09-2026";
constexpr const char* FIRMWARE_VERSION = "V2.263.2";
constexpr const char* HARDWARE_VERSION = "V1.263.1";
constexpr float DEVICE_CAPACITY_KG = 20.0f;


// --------------------------
// Pin Definitions
// --------------------------
constexpr int LOADCELL_DOUT_PIN = 18;
constexpr int LOADCELL_SCK_PIN = 19;
constexpr int ADD_BTN_PIN = 25;                      // Button for calibration mode

// --------------------------
// Display Selection
// --------------------------
#define USE_LCD
// #define USE_SEVEN_SEGMENT

// --------------------------
// LCD Configuration
// --------------------------
#if defined(USE_LCD)
    constexpr uint8_t LCD_ADDR = 0x27;                   // I2C address
    constexpr uint8_t LCD_COLS = 16;
    constexpr uint8_t LCD_ROWS = 2;
#endif

// --------------------------
// Seven Segment Configuration
// --------------------------
#if defined(USE_SEVEN_SEGMENT)
    #define DIN_PIN   21
    #define CLK_PIN   22
    #define STB_PIN   5
#endif

// --------------------------
// Filter Parameters
// --------------------------
constexpr int MOVING_AVG_SIZE = 2;                   // Number of samples for moving average
constexpr int MEDIAN_SIZE = 3;                       // Number of samples for median filter (odd recommended)
constexpr int TRIMMED_MEAN_SIZE = 5;                 // Total samples for trimmed mean
constexpr int TRIMMED_DISCARD = 1;                   // Discard lowest and highest N samples
constexpr float ALPHA = 0.65f;                       // Exponential filter smoothing factor (0..1)

// Stability detection threshold for filter task (in grams)
constexpr float FILTER_STABILITY_THRESHOLD_GRAMS = DEVICE_CAPACITY_KG * 0.15f;
constexpr int FILTER_STABLE_COUNT = 2;               // Consecutive stable readings required

// --------------------------
// Weight Locking Thresholds (all in grams)
// --------------------------
constexpr float MIN_LOCK_WEIGHT = DEVICE_CAPACITY_KG;               // 40,000 g
constexpr float REMOVE_THRESHOLD = DEVICE_CAPACITY_KG * 0.50f;      // 20,000 g (50% of capacity)
constexpr float STABILITY_THRESHOLD = DEVICE_CAPACITY_KG;   // 10,000 g (25% of capacity)
constexpr int STABLE_TIME_MS = 300;                   // Time in ms the weight must remain stable before locking

// --------------------------
// Near-Zero Snapping
// --------------------------
constexpr float NEAR_ZERO_THRESHOLD = DEVICE_CAPACITY_KG * 0.1f;          // ±2 g considered zero
constexpr float NEGATIVE_LIMIT = DEVICE_CAPACITY_KG * -0.1f;              // Allow negative readings down to -5 g

// --------------------------
// Display Lock Hysteresis (grams)
// --------------------------
constexpr float DISPLAY_LOCK_HYSTERESIS_GRAMS = 5.0f;   // display shows locked weight if within ±15 g

// --------------------------
// LCD Update Control
// --------------------------
constexpr float LCD_DEADBAND_GRAMS = 5.0f;           // Only update if weight changes by more than 5 g
constexpr uint32_t LCD_MIN_INTERVAL_MS = 100;        // Minimum time between LCD updates (ms)

// --------------------------
// Calibration Settings
// --------------------------
constexpr float DEFAULT_CALIB_FACTOR = 100.0f;       // Fallback factor if none stored
constexpr long CALIB_MIN_RAW_THRESHOLD = 1000;       // Raw units to detect that a weight is placed
constexpr int CALIB_STABLE_COUNT = 5;                // Required stable readings during calibration
constexpr float CALIB_STABLE_THRESHOLD = 100.0f;     // Allowed variation during stabilization (raw units)
constexpr int CALIB_SAMPLE_COUNT = 10;               // Number of samples to average for final factor

// --------------------------
// Task & Queue Sizes
// --------------------------
constexpr int QUEUE_RAW_SIZE = 10;
constexpr int QUEUE_STABLE_SIZE = 5;
constexpr uint32_t HX711_TASK_STACK = 4096;
constexpr uint32_t FILTER_TASK_STACK = 4096;
constexpr uint32_t SERIAL_TASK_STACK = 4096;
constexpr uint32_t LCD_TASK_STACK = 2048;

#endif // CONFIG_H