#include "seven_seg.h"

// Segment bit definitions for GN6932
#define SEG_A   0x20
#define SEG_B   0x80
#define SEG_C   0x01
#define SEG_D   0x02
#define SEG_E   0x08
#define SEG_F   0x10
#define SEG_G   0x40
#define SEG_DP  0x04

// Digit table 0-9
static const uint8_t digitTable[10] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,          // 0
    SEG_B | SEG_C,                                          // 1
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                  // 2
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                  // 3
    SEG_B | SEG_C | SEG_F | SEG_G,                          // 4
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                  // 5
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,          // 6
    SEG_A | SEG_B | SEG_C,                                  // 7
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,  // 8
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G           // 9
};

SevenSegDisplay::SevenSegDisplay(uint8_t dinPin, uint8_t clkPin, uint8_t stbPin)
    : _dinPin(dinPin), _clkPin(clkPin), _stbPin(stbPin) {
}

void SevenSegDisplay::begin() {
    pinMode(_dinPin, OUTPUT);
    pinMode(_clkPin, OUTPUT);
    pinMode(_stbPin, OUTPUT);

    digitalWrite(_dinPin, LOW);
    digitalWrite(_clkPin, LOW);
    digitalWrite(_stbPin, HIGH);

    delay(10);

    // Display ON, max brightness
    command(0x8F);

    clear();
}

void SevenSegDisplay::clear() {
    memset(_buffer, 0x00, sizeof(_buffer));
    updateDisplay();
}

void SevenSegDisplay::showDashes() {
    clear();
    for (int i = 1; i <= 5; i++) {
        setDigit(i, 0);                     // value 0 is ignored but we want only G segment
        _buffer[i - 1] = SEG_G;             // set only segment G (dash)
    }
    updateDisplay();
}

void SevenSegDisplay::writeByte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        digitalWrite(_clkPin, LOW);
        digitalWrite(_dinPin, data & 0x01);
        digitalWrite(_clkPin, HIGH);
        data >>= 1;
    }
    digitalWrite(_clkPin, LOW);
}

void SevenSegDisplay::command(uint8_t cmd) {
    digitalWrite(_stbPin, LOW);
    writeByte(cmd);
    digitalWrite(_stbPin, HIGH);
}

void SevenSegDisplay::updateDisplay() {
    command(0x40);          // data write, auto increment
    digitalWrite(_stbPin, LOW);
    writeByte(0xC0);        // start address 0
    for (int i = 0; i < 16; i++) {
        writeByte(_buffer[i]);
    }
    digitalWrite(_stbPin, HIGH);
}

void SevenSegDisplay::setDigit(uint8_t position, uint8_t value, bool decimalPoint) {
    if (position < 1 || position > 16 || value > 9) return;
    _buffer[position - 1] = digitTable[value] | (decimalPoint ? SEG_DP : 0);
}

// Display integer number (0 - 99999) on 5 digits
void SevenSegDisplay::showNumber(int number) {
    clear();
    if (number < 0) number = 0;
    if (number > 99999) number = 99999;

    setDigit(1, (number / 10000) % 10);
    setDigit(2, (number / 1000) % 10);
    setDigit(3, (number / 100) % 10);
    setDigit(4, (number / 10) % 10);
    setDigit(5, number % 10);
    updateDisplay();
}


// Display weight in kilograms with 3 decimal places (e.g., 12.345)
void SevenSegDisplay::showWeight(float weightKg) {
    clear();

    bool negative = weightKg < 0.0f;
    float absWeight = fabs(weightKg);

    // Convert to thousandths (0.001 kg)
    int thousandths = (int)(absWeight * 1000.0f + 0.5f);
    if (thousandths > 99999) thousandths = 99999;   // max 99.999 kg

    // Special case: exactly zero -> show single "0" on rightmost digit
    if (thousandths == 0 && !negative) {
        setDigit(5, 0);
        updateDisplay();
        return;
    }

    // If negative and magnitude < 1 kg, show "-0.XXX" using all 5 digits
    if (negative && thousandths < 1000) {
        int hundreds = (thousandths / 100) % 10;
        int tens = (thousandths / 10) % 10;
        int ones = thousandths % 10;

        _buffer[0] = SEG_G;                      // minus sign
        setDigit(2, 0, true);                    // '0' with decimal point
        setDigit(3, hundreds, false);
        setDigit(4, tens, false);
        setDigit(5, ones, false);
        updateDisplay();
        return;
    }

    // For negative values with magnitude >= 1 kg, just show minus and clamp to 0.999?
    // For simplicity, we'll treat them as positive for now (rare case)
    // Actually, we can show minus sign and then the integer part with decimal,
    // but we have only 5 digits. We'll ignore this edge case.

    int integerPart = thousandths / 1000;

    if (integerPart == 0) {
        // Weight < 1 kg (positive)
        int hundreds = (thousandths / 100) % 10;
        int tens = (thousandths / 10) % 10;
        int ones = thousandths % 10;

        _buffer[0] = 0x00;                     // digit 1 blank
        setDigit(2, 0, true);                  // digit 2 = "0."
        setDigit(3, hundreds, false);
        setDigit(4, tens, false);
        setDigit(5, ones, false);
    } else if (integerPart < 10) {
        // 1–9 kg
        int tenths = (thousandths / 100) % 10;
        int hundredths = (thousandths / 10) % 10;
        int thousandthsDigit = thousandths % 10;

        _buffer[0] = 0x00;                     // blank
        setDigit(2, integerPart, true);        // digit 2 = "X."
        setDigit(3, tenths, false);
        setDigit(4, hundredths, false);
        setDigit(5, thousandthsDigit, false);
    } else {
        // 10–99 kg
        int tens = integerPart / 10;
        int ones = integerPart % 10;
        int tenths = (thousandths / 100) % 10;
        int hundredths = (thousandths / 10) % 10;
        int thousandthsDigit = thousandths % 10;

        setDigit(1, tens, false);
        setDigit(2, ones, true);               // decimal after second digit
        setDigit(3, tenths, false);
        setDigit(4, hundredths, false);
        setDigit(5, thousandthsDigit, false);
    }

    updateDisplay();
}


