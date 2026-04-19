#include "TM1637Display.h"
#include <Arduino.h>

// 7段数码管编码 (0~9, A~F)
static const uint8_t digitToSegment[] = {
    // 0     1     2     3     4     5     6     7     8     9
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    // A     B     C     D     E     F
    0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

TM1637Display::TM1637Display(uint8_t pinClk, uint8_t pinDIO) {
    m_pinClk = pinClk;
    m_pinDIO = pinDIO;
    m_brightness = 7; // 默认最大亮度

    pinMode(m_pinClk, OUTPUT);
    pinMode(m_pinDIO, OUTPUT);
    digitalWrite(m_pinClk, HIGH);
    digitalWrite(m_pinDIO, HIGH);
}

void TM1637Display::setBrightness(uint8_t brightness) {
    m_brightness = brightness & 0x07;
}

void TM1637Display::setSegments(const uint8_t segments[], uint8_t length, uint8_t pos) {
    start();
    writeByte(0x40); // 数据写入模式
    stop();

    start();
    writeByte(0xC0 | pos); // 设置起始地址

    for (uint8_t i = 0; i < length; i++) {
        writeByte(segments[i]);
    }

    stop();

    start();
    writeByte(0x88 | m_brightness); // 设置亮度
    stop();
}

void TM1637Display::clear() {
    uint8_t blank[] = {0, 0, 0, 0};
    setSegments(blank, 4);
}

uint8_t TM1637Display::encodeDigit(uint8_t digit) {
    return digitToSegment[digit & 0x0F];
}

uint8_t TM1637Display::encodeDigitWithDot(uint8_t digit) {
    return digitToSegment[digit & 0x0F] | 0x80;
}

void TM1637Display::start() {
    digitalWrite(m_pinDIO, LOW);
    delayMicros(2);
    digitalWrite(m_pinClk, LOW);
    delayMicros(2);
}

void TM1637Display::stop() {
    digitalWrite(m_pinDIO, LOW);
    delayMicros(2);
    digitalWrite(m_pinClk, HIGH);
    delayMicros(2);
    digitalWrite(m_pinDIO, HIGH);
    delayMicros(2);
}

bool TM1637Display::writeByte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(m_pinClk, LOW);
        delayMicros(2);
        digitalWrite(m_pinDIO, (b & 0x01) ? HIGH : LOW);
        delayMicros(2);
        digitalWrite(m_pinClk, HIGH);
        delayMicros(2);
        b >>= 1;
    }

    // 等待 ACK
    digitalWrite(m_pinClk, LOW);
    delayMicros(2);
    pinMode(m_pinDIO, INPUT);
    delayMicros(2);
    digitalWrite(m_pinClk, HIGH);
    delayMicros(2);
    bool ack = (digitalRead(m_pinDIO) == LOW);
    pinMode(m_pinDIO, OUTPUT);
    delayMicros(2);
    digitalWrite(m_pinClk, LOW);
    delayMicros(2);

    return ack;
}

void TM1637Display::delayMicros(uint16_t micros) {
    delayMicroseconds(micros);
}

void TM1637Display::showNumberDec(int number, bool leading_zeros, uint8_t length, uint8_t pos) {
    uint8_t digits[4];
    
    if (leading_zeros) {
        // 显示前导零
        for (int i = 3; i >= 0; i--) {
            digits[i] = encodeDigit(number % 10);
            number /= 10;
        }
    } else {
        // 不显示前导零
        int temp = number;
        int digitCount = 0;
        if (temp == 0) digitCount = 1;
        while (temp > 0) {
            digitCount++;
            temp /= 10;
        }
        
        temp = number;
        for (int i = 3; i >= 0; i--) {
            if (i < 4 - digitCount) {
                digits[i] = 0; // 空白
            } else {
                digits[i] = encodeDigit(temp % 10);
                temp /= 10;
            }
        }
    }
    
    setSegments(digits, length, pos);
}
