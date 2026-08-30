#ifndef CONFIG_H
#define CONFIG_H

// Device identification
// constexpr const char* DEVICE_ID = "12852607011402";
// constexpr const char* DEVICE_MODEL = "MEGA-UNIQUE-MU-35";
// constexpr const char* RELEASE_DATE = "01-07-2026";
// constexpr const char* FIRMWARE_VERSION = "V1.262.1";
// constexpr float DEVICE_CAPACITY_KG = 40.0f;

constexpr const char* DEVICE_ID = "12850001";
constexpr const char* APPID = "1285.2";
constexpr const char* DEVICE_MODEL = "MEGA-T-20";
constexpr const char* BATCH_ID = "2608.1";
constexpr const char* MANUFACTURING_DATE = "01-07-2026";
constexpr const char* RELEASE_DATE = "01-07-2026";
constexpr const char* FIRMWARE_VERSION = "V2.263.1";
constexpr const char* HARDWARE_VERSION = "V1.263.1";
constexpr float DEVICE_CAPACITY_KG = 20.0f;

// Pin definitions
constexpr int LOADCELL_DOUT_PIN = 18;
constexpr int LOADCELL_SCK_PIN = 19;
constexpr int ADD_BTN_PIN = 25;

// LCD
constexpr uint8_t LCD_ADDR = 0x27;
constexpr uint8_t LCD_COLS = 16;
constexpr uint8_t LCD_ROWS = 2;

// Filter parameters
constexpr int MOVING_AVG_SIZE = 2;
constexpr float ALPHA = 0.65f;

// Median filter
constexpr int MEDIAN_SIZE = 3;          // Use 3 or 5 samples

// Trimmed mean filter
constexpr int TRIMMED_MEAN_SIZE = 5;    // Total samples
constexpr int TRIMMED_DISCARD = 1;      // Discard lowest and highest each

// Weight locking
constexpr float MIN_LOCK_WEIGHT = DEVICE_CAPACITY_KG;           // kg
constexpr float REMOVE_THRESHOLD = DEVICE_CAPACITY_KG * 0.50f; // kg
constexpr float STABILITY_THRESHOLD = DEVICE_CAPACITY_KG * 0.25f; // kg
constexpr int STABLE_TIME_MS = 300;

// Near-zero snapping
constexpr float NEAR_ZERO_THRESHOLD = 0.002f;  // kg (2 grams)
constexpr float NEGATIVE_LIMIT = -0.005f;      // kg (-5 grams)

// Calibration
constexpr float DEFAULT_CALIB_FACTOR = 100.0f;
constexpr long CALIB_MIN_RAW_THRESHOLD = 1000;
constexpr int CALIB_STABLE_COUNT = 5;
constexpr float CALIB_STABLE_THRESHOLD = 100.0f;
constexpr int CALIB_SAMPLE_COUNT = 10;

// Tasks
constexpr int QUEUE_RAW_SIZE = 10;
constexpr int QUEUE_STABLE_SIZE = 5;
constexpr uint32_t HX711_TASK_STACK = 4096;
constexpr uint32_t FILTER_TASK_STACK = 4096;
constexpr uint32_t SERIAL_TASK_STACK = 4096;

#endif // CONFIG_H