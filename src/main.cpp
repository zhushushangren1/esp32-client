#include <Arduino.h>
#include <WiFi.h>
#include "TM1637Display.h"

const char* CLIENT_ID = "client3";

#define BTN_RED_PIN     12
#define BTN_BLUE_PIN    14
#define BTN_RESET_PIN   13
#define BTN_RED_PLUS2   27
#define BTN_BLUE_PLUS2  26

#define TM1637_CLK      2
#define TM1637_DIO      15

const char* ssid = "ScoreSystem";
const char* password = "12345678";
const char* host = "192.168.4.1";
const int port = 80;

int redCount = 0;
int blueCount = 0;

bool redReleased = true;
bool blueReleased = true;
bool resetReleased = true;
bool redPlus2Released = true;
bool bluePlus2Released = true;

bool submissionLocked = false;
unsigned long lockedRoundId = 0;
unsigned long lastRoundStatusCheck = 0;
const unsigned long ROUND_STATUS_INTERVAL_MS = 1000;
unsigned long lastDisplayRefresh = 0;
const unsigned long DISPLAY_REFRESH_INTERVAL_MS = 500;


TM1637Display display(TM1637_CLK, TM1637_DIO);

bool isButtonPressed(int pin) {
    return digitalRead(pin) == LOW;
}

unsigned long extractJsonUnsignedLong(const String& body, const char* key) {
    String marker = "\"" + String(key) + "\":";
    int start = body.indexOf(marker);
    if (start < 0) {
        return 0;
    }

    start += marker.length();
    while (start < body.length() && body[start] == ' ') {
        start++;
    }

    int end = start;
    while (end < body.length() && isDigit(body[end])) {
        end++;
    }

    if (end == start) {
        return 0;
    }

    return body.substring(start, end).toInt();
}

void displayLockedState() {
    const uint8_t DASH = 0x40;
    uint8_t segs[4] = {DASH, DASH, DASH, DASH};
    display.setSegments(segs, 4);
}

void displayScore() {
    if (submissionLocked) {
        displayLockedState();
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        uint8_t segs[4] = {
            display.encodeDigit(0xF),
            display.encodeDigit(0xF),
            display.encodeDigit(0xF),
            display.encodeDigit(0xF)
        };
        display.setSegments(segs, 4);
        return;
    }

    int red = constrain(redCount, 0, 99);
    int blue = constrain(blueCount, 0, 99);

    uint8_t digits[4];
    digits[0] = display.encodeDigit(red / 10);
    digits[1] = display.encodeDigitWithDot(red % 10);
    digits[2] = display.encodeDigit(blue / 10);
    digits[3] = display.encodeDigit(blue % 10);
    display.setSegments(digits, 4);
}

bool readHttpBody(WiFiClient& client, String& body) {
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 2000) {
            return false;
        }
        delay(1);
    }

    bool inBody = false;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (!inBody) {
            if (line == "\r" || line.length() == 0) {
                inBody = true;
            }
            continue;
        }
        body += line;
    }

    return true;
}

bool sendScoreToServer(int red, int blue) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skip send");
        return false;
    }

    WiFiClient client;
    Serial.println("Connecting to server...");
    Serial.flush();
    if (!client.connect(host, port)) {
        Serial.println("Connection to server failed!");
        return false;
    }
    Serial.println("Server TCP connected");
    Serial.flush();

    String url = "/updateScore?client=" + String(CLIENT_ID) +
                 "&red=" + String(red) +
                 "&blue=" + String(blue);
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + String(host) + "\r\n" +
                 "Connection: close\r\n\r\n");

    Serial.print("Sent: ");
    Serial.println(url);
    Serial.flush();

    String body;
    if (!readHttpBody(client, body)) {
        Serial.println("Server response timeout!");
        client.stop();
        return false;
    }

    client.stop();

    if (body.indexOf("\"ok\":true") >= 0) {
        Serial.println("Server response: OK");
        Serial.flush();
        lockedRoundId = extractJsonUnsignedLong(body, "roundId");
        return true;
    }

    Serial.print("Server rejected request: ");
    Serial.println(body);
    return false;
}

bool fetchRoundStatus(bool& roundOpen, bool& submitted, unsigned long& statusRoundId) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClient client;
    if (!client.connect(host, port)) {
        return false;
    }

    client.print(String("GET /roundStatus?client=") + String(CLIENT_ID) + " HTTP/1.1\r\n" +
                 "Host: " + String(host) + "\r\n" +
                 "Connection: close\r\n\r\n");

    String body;
    if (!readHttpBody(client, body)) {
        client.stop();
        return false;
    }

    client.stop();

    if (body.indexOf("\"roundOpen\":true") >= 0) {
        roundOpen = true;
    } else if (body.indexOf("\"roundOpen\":false") >= 0) {
        roundOpen = false;
    } else {
        return false;
    }

    submitted = (body.indexOf("\"submitted\":true") >= 0);
    statusRoundId = extractJsonUnsignedLong(body, "roundId");
    return true;
}

void unlockForNextRound() {
    submissionLocked = false;
    lockedRoundId = 0;
    redCount = 0;
    blueCount = 0;
    Serial.println("Next round opened. Ready for scoring.");
    displayScore();
}

void pollRoundStatusIfNeeded() {
    if (!submissionLocked) {
        return;
    }

    if (millis() - lastRoundStatusCheck < ROUND_STATUS_INTERVAL_MS) {
        return;
    }
    lastRoundStatusCheck = millis();

    bool roundOpen = false;
    bool submitted = true;
    unsigned long statusRoundId = 0;
    if (fetchRoundStatus(roundOpen, submitted, statusRoundId) &&
        roundOpen &&
        (!submitted || (lockedRoundId != 0 && statusRoundId != lockedRoundId))) {
        unlockForNextRound();
    }
}

void refreshDisplayIfNeeded() {
    if (millis() - lastDisplayRefresh < DISPLAY_REFRESH_INTERVAL_MS) {
        return;
    }
    lastDisplayRefresh = millis();

    if (submissionLocked || WiFi.status() != WL_CONNECTED) {
        displayScore();
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(BTN_RED_PIN, INPUT_PULLUP);
    pinMode(BTN_BLUE_PIN, INPUT_PULLUP);
    pinMode(BTN_RESET_PIN, INPUT_PULLUP);
    pinMode(BTN_RED_PLUS2, INPUT_PULLUP);
    pinMode(BTN_BLUE_PLUS2, INPUT_PULLUP);

    display.setBrightness(0x07);
    displayScore();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_2dBm);
    WiFi.begin(ssid, password);
    Serial.println("WiFi: connecting to ScoreSystem in background...");

    delay(100);

    redReleased = !isButtonPressed(BTN_RED_PIN);
    blueReleased = !isButtonPressed(BTN_BLUE_PIN);
    resetReleased = !isButtonPressed(BTN_RESET_PIN);
    redPlus2Released = !isButtonPressed(BTN_RED_PLUS2);
    bluePlus2Released = !isButtonPressed(BTN_BLUE_PLUS2);

    Serial.println("=== Score System Ready ===");
    Serial.print("Client ID: ");
    Serial.println(CLIENT_ID);
    Serial.println("==========================");
    Serial.flush();
}

void loop() {
    static bool lastConnected = false;
    bool nowConnected = (WiFi.status() == WL_CONNECTED);
    if (nowConnected != lastConnected) {
        if (nowConnected) {
            Serial.print("WiFi connected! IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("WiFi disconnected!");
        }
        lastConnected = nowConnected;
        displayScore();
    }

    pollRoundStatusIfNeeded();
    refreshDisplayIfNeeded();

    if (submissionLocked) {
        delay(10);
        return;
    }

    if (isButtonPressed(BTN_RED_PIN)) {
        if (redReleased) {
            delay(50);
            if (isButtonPressed(BTN_RED_PIN)) {
                redCount++;
                Serial.print("Red +1 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                displayScore();
                redReleased = false;
            }
        }
    } else {
        redReleased = true;
    }

    if (isButtonPressed(BTN_BLUE_PIN)) {
        if (blueReleased) {
            delay(50);
            if (isButtonPressed(BTN_BLUE_PIN)) {
                blueCount++;
                Serial.print("Blue +1 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                displayScore();
                blueReleased = false;
            }
        }
    } else {
        blueReleased = true;
    }

    if (isButtonPressed(BTN_RED_PLUS2)) {
        if (redPlus2Released) {
            delay(50);
            if (isButtonPressed(BTN_RED_PLUS2)) {
                redCount += 2;
                Serial.print("Red +2 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                displayScore();
                redPlus2Released = false;
            }
        }
    } else {
        redPlus2Released = true;
    }

    if (isButtonPressed(BTN_BLUE_PLUS2)) {
        if (bluePlus2Released) {
            delay(50);
            if (isButtonPressed(BTN_BLUE_PLUS2)) {
                blueCount += 2;
                Serial.print("Blue +2 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                displayScore();
                bluePlus2Released = false;
            }
        }
    } else {
        bluePlus2Released = true;
    }

    if (isButtonPressed(BTN_RESET_PIN)) {
        if (resetReleased) {
            delay(50);
            if (isButtonPressed(BTN_RESET_PIN)) {
                Serial.println("Green button pressed - Sending score to server...");
                Serial.flush();

                if (sendScoreToServer(redCount, blueCount)) {
                    Serial.println("Score sent successfully!");
                    submissionLocked = true;
                    displayScore();
                } else {
                    Serial.println("Failed to send score! Score kept for retry.");
                    for (int i = 0; i < 2; i++) {
                        display.clear();
                        delay(100);
                        displayScore();
                        delay(100);
                    }
                }

                resetReleased = false;
            }
        }
    } else {
        resetReleased = true;
    }

    delay(10);
}
