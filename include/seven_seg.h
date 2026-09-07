#ifndef SEVEN_SEG_H
#define SEVEN_SEG_H

#include <Arduino.h>

class SevenSegDisplay {
public:
    SevenSegDisplay(uint8_t dinPin, uint8_t clkPin, uint8_t stbPin);
    void begin();
    void clear();
    void showWeight(float weightKg);      // 5-digit weight with decimal
    void showDashes();                    // show "-----" on weight digits
    void showTotalPriceMessage();         // show "USB PC" on total price digits (11-16)
    void showNumber(int number);          // for testing (0-99999)
private:
    void writeByte(uint8_t data);
    void command(uint8_t cmd);
    void updateDisplay();
    void setDigit(uint8_t position, uint8_t value, bool decimalPoint = false);

    uint8_t _dinPin;
    uint8_t _clkPin;
    uint8_t _stbPin;
    uint8_t _buffer[16];                  // GN6932 RAM (16 bytes)
};

#endif