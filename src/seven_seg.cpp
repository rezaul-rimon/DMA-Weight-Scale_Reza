#include "seven_seg.h"
#include <string.h>

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

// Letter segment patterns (approximations)
static const uint8_t segU = SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;  // U
static const uint8_t segS = SEG_A | SEG_F | SEG_G | SEG_C | SEG_D;   // S
static const uint8_t segB = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G; // B
static const uint8_t segP = SEG_A | SEG_B | SEG_E | SEG_F | SEG_G;   // P
static const uint8_t segC = SEG_A | SEG_D | SEG_E | SEG_F;           // C
static const uint8_t segSpace = 0x00;                                 // blank

uint8_t getCharSegments(char c) {
    switch (c) {
        case 'U': return segU;
        case 'S': return segS;
        case 'B': return segB;
        case 'P': return segP;
        case 'C': return segC;
        case ' ': return segSpace;
        case '8': return digitTable[8];   // use digit 8 pattern
        default:  return segSpace;
    }
}

// ---------------- Constructor ----------------
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
    command(0x8F);   // Display ON, max brightness
    clear();
}

void SevenSegDisplay::clear() {
    memset(_buffer, 0x00, sizeof(_buffer));
    updateDisplay();
}

// ---------------- Low-level communication ----------------
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

// ---------------- Display functions ----------------
// Show dashes on weight digits (positions 1-5), without disturbing total price
void SevenSegDisplay::showDashes() {
    // Clear only first 5 digits
    for (int i = 0; i < 5; i++) _buffer[i] = 0x00;
    for (int i = 0; i < 5; i++) {
        _buffer[i] = SEG_G;   // dash on each weight digit
    }
    updateDisplay();
}

// Show "USB PC" on total price digits (positions 11-16)
void SevenSegDisplay::showTotalPriceMessage() {
    const char* msg = "USB PC";   // 6 chars
    int pos = 11;                 // start at physical digit 11
    int len = strlen(msg);
    for (int i = 0; i < len && pos <= 16; i++) {
        _buffer[pos - 1] = getCharSegments(msg[i]);
        pos++;
    }
    updateDisplay();
}

// Show weight on positions 1-5 (does not affect total price)
void SevenSegDisplay::showWeight(float weightKg) {
    // Clear only the weight digits
    for (int i = 0; i < 5; i++) _buffer[i] = 0x00;

    bool negative = weightKg < 0.0f;
    float absWeight = fabs(weightKg);

    int thousandths = (int)(absWeight * 1000.0f + 0.5f);
    if (thousandths > 99999) thousandths = 99999;

    // Zero case
    if (thousandths == 0 && !negative) {
        setDigit(5, 0);
        updateDisplay();
        return;
    }

    // Negative < 1 kg: show "-0.XXX"
    if (negative && thousandths < 1000) {
        int hundreds = (thousandths / 100) % 10;
        int tens = (thousandths / 10) % 10;
        int ones = thousandths % 10;
        _buffer[0] = SEG_G;       // minus sign
        setDigit(2, 0, true);     // '0' with decimal
        setDigit(3, hundreds, false);
        setDigit(4, tens, false);
        setDigit(5, ones, false);
        updateDisplay();
        return;
    }

    int integerPart = thousandths / 1000;

    if (integerPart == 0) {
        // 0.XXX
        int hundreds = (thousandths / 100) % 10;
        int tens = (thousandths / 10) % 10;
        int ones = thousandths % 10;
        _buffer[0] = 0x00;       // blank
        setDigit(2, 0, true);
        setDigit(3, hundreds, false);
        setDigit(4, tens, false);
        setDigit(5, ones, false);
    } else if (integerPart < 10) {
        // X.XXX
        int tenths = (thousandths / 100) % 10;
        int hundredths = (thousandths / 10) % 10;
        int thousandthsDigit = thousandths % 10;
        _buffer[0] = 0x00;       // blank
        setDigit(2, integerPart, true);
        setDigit(3, tenths, false);
        setDigit(4, hundredths, false);
        setDigit(5, thousandthsDigit, false);
    } else {
        // XX.XXX
        int tens = integerPart / 10;
        int ones = integerPart % 10;
        int tenths = (thousandths / 100) % 10;
        int hundredths = (thousandths / 10) % 10;
        int thousandthsDigit = thousandths % 10;
        setDigit(1, tens, false);
        setDigit(2, ones, true);
        setDigit(3, tenths, false);
        setDigit(4, hundredths, false);
        setDigit(5, thousandthsDigit, false);
    }

    updateDisplay();
}

// For testing only (unused in production)
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