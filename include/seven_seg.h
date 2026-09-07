#ifndef SEVEN_SEG_H
#define SEVEN_SEG_H

#include <Arduino.h>

class SevenSegDisplay {
public:
    SevenSegDisplay(uint8_t dinPin, uint8_t clkPin, uint8_t stbPin);
    void begin();
    void clear();
    void showDashes();    // show "-----" on all 5 digits
    void showWeight(float weightKg);   // shows weight with 3 decimals (e.g., 12.345)
    void showNumber(int number);       // shows integer up to 99999
private:
    void writeByte(uint8_t data);
    void command(uint8_t cmd);
    void updateDisplay();
    void setDigit(uint8_t position, uint8_t value, bool decimalPoint = false);

    uint8_t _dinPin;
    uint8_t _clkPin;
    uint8_t _stbPin;
    uint8_t _buffer[16];               // GN6932 RAM (16 bytes)
};

#endif