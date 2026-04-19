#ifndef TM1637DISPLAY_H
#define TM1637DISPLAY_H

#include <Arduino.h>

class TM1637Display {
public:
    TM1637Display(uint8_t pinClk, uint8_t pinDIO);
    
    void setBrightness(uint8_t brightness);
    void setSegments(const uint8_t segments[], uint8_t length, uint8_t pos = 0);
    void clear();
    uint8_t encodeDigit(uint8_t digit);
    uint8_t encodeDigitWithDot(uint8_t digit);
    
    // 便捷方法：显示4位数字
    void showNumberDec(int number, bool leading_zeros = false, uint8_t length = 4, uint8_t pos = 0);

private:
    uint8_t m_pinClk;
    uint8_t m_pinDIO;
    uint8_t m_brightness;
    
    void start();
    void stop();
    bool writeByte(uint8_t b);
    void delayMicros(uint16_t micros);
};

#endif // TM1637DISPLAY_H