#include <Arduino.h>
#include <WiFi.h>
#include "TM1637Display.h"

// ============ 客户端ID配置 ============
// 三个客户端分别设置为 "client1", "client2", "client3"
const char* CLIENT_ID = "client2";  // 根据实际设备修改此值
// ========================================

// 按钮引脚定义
#define BTN_RED_PIN    12  // D12 - 红色方计分按钮（+1）
#define BTN_BLUE_PIN   14  // D14 - 蓝色方计分按钮（+1）
#define BTN_RESET_PIN  13  // D13 - 重置按钮（绿色）
#define BTN_RED_PLUS2   27  // D27 - 红色方计分按钮（+2）
#define BTN_BLUE_PLUS2  26  // D26 - 蓝色方计分按钮（+2）

// TM1637数码管引脚定义
#define TM1637_CLK    2   // D2 - 时钟引脚
#define TM1637_DIO    15  // D15 - 数据引脚

// 计数器变量
int redCount = 0;
int blueCount = 0;

// 按钮状态变量
bool redReleased = true;
bool blueReleased = true;
bool resetReleased = true;
bool redPlus2Released = true;
bool bluePlus2Released = true;

// WiFi配置
const char* ssid = "ScoreSystem";
const char* password = "12345678";
const char* host = "192.168.4.1";  // score_system的AP IP地址
const int port = 80;

// TM1637显示器对象
TM1637Display display(TM1637_CLK, TM1637_DIO);

// 检测按钮是否被按下
bool isButtonPressed(int pin) {
    return digitalRead(pin) == LOW;
}

// 在数码管上显示比分（未连接WiFi时显示FFFF作为连接状态提示）
void displayScore() {
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

    // 4位数码管：前两位显示红色方，后两位显示蓝色方
    // 限制分数在0-99范围内
    int red = constrain(redCount, 0, 99);
    int blue = constrain(blueCount, 0, 99);

    uint8_t digits[4];
    digits[0] = display.encodeDigit(red / 10);      // 红色方十位
    digits[1] = display.encodeDigitWithDot(red % 10);  // 红色方个位（带小数点形成冒号效果）
    digits[2] = display.encodeDigit(blue / 10);     // 蓝色方十位
    digits[3] = display.encodeDigit(blue % 10);     // 蓝色方个位

    display.setSegments(digits, 4);
}

// 通过WiFi发送比分到score_system
bool sendScoreToServer(int red, int blue) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skip send");
        return false;
    }

    WiFiClient client;
    if (!client.connect(host, port)) {
        Serial.println("Connection to server failed!");
        return false;
    }

    // 发送HTTP GET请求（带上客户端ID）
    String url = "/updateScore?client=" + String(CLIENT_ID) + "&red=" + String(red) + "&blue=" + String(blue);
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + String(host) + "\r\n" +
                 "Connection: close\r\n\r\n");

    Serial.print("Sent: ");
    Serial.println(url);

    // 等待响应（最多2秒，避免长时间阻塞loop）
    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 2000) {
            Serial.println("Server response timeout!");
            client.stop();
            return false;
        }
        delay(1);  // 让出 CPU 给 WiFi 栈
    }

    // 读取响应（简化处理）
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.startsWith("{\"ok\"")) {
            Serial.println("Server response: OK");
        }
    }

    client.stop();
    return true;
}

void setup() {
    Serial.begin(115200);
    
    // 设置所有按钮引脚为输入模式，启用内部上拉电阻
    pinMode(BTN_RED_PIN, INPUT_PULLUP);
    pinMode(BTN_BLUE_PIN, INPUT_PULLUP);
    pinMode(BTN_RESET_PIN, INPUT_PULLUP);
    pinMode(BTN_RED_PLUS2, INPUT_PULLUP);
    pinMode(BTN_BLUE_PLUS2, INPUT_PULLUP);
    
    // 初始化TM1637显示器
    display.setBrightness(0x07);  // 设置亮度

    // 初始显示（WiFi尚未连接时会显示 FFFF 作为未就绪提示）
    displayScore();

    // 启动WiFi（非阻塞，后台自动重连）
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(ssid, password);
    Serial.println("WiFi: connecting to ScoreSystem in background...");

    // 等待引脚稳定，避免启动时误触发
    delay(100);
    
    // 读取初始按钮状态，防止启动时误触发
    redReleased = !isButtonPressed(BTN_RED_PIN);
    blueReleased = !isButtonPressed(BTN_BLUE_PIN);
    resetReleased = !isButtonPressed(BTN_RESET_PIN);
    redPlus2Released = !isButtonPressed(BTN_RED_PLUS2);
    bluePlus2Released = !isButtonPressed(BTN_BLUE_PLUS2);
    
    Serial.println("=== Score System Ready ===");
    Serial.print("Client ID: ");
    Serial.println(CLIENT_ID);
    Serial.println("Red +1: GPIO12 (D12)");
    Serial.println("Blue +1: GPIO14 (D14)");
    Serial.println("Red +2: GPIO27 (D27)");
    Serial.println("Blue +2: GPIO26 (D26)");
    Serial.println("Reset/Send: GPIO13 (D13)");
    Serial.println("Display: TM1637 (CLK=D2, DIO=D15)");
    Serial.println("==========================");
    Serial.flush();
}

void loop() {
    // 检测WiFi连接状态变化：断开→显示FFFF，连上→刷新为当前分数
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

    // 检测红色方按钮
    if (isButtonPressed(BTN_RED_PIN)) {
        if (redReleased) {
            delay(50);  // 消抖
            if (isButtonPressed(BTN_RED_PIN)) {
                redCount++;
                Serial.print("Red +1 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                Serial.flush();
                displayScore();  // 更新显示
                redReleased = false;
            }
        }
    } else {
        redReleased = true;
    }
    
    // 检测蓝色方按钮
    if (isButtonPressed(BTN_BLUE_PIN)) {
        if (blueReleased) {
            delay(50);  // 消抖
            if (isButtonPressed(BTN_BLUE_PIN)) {
                blueCount++;
                Serial.print("Blue +1 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                Serial.flush();
                displayScore();  // 更新显示
                blueReleased = false;
            }
        }
    } else {
        blueReleased = true;
    }
    
    // 检测红色方+2按钮
    if (isButtonPressed(BTN_RED_PLUS2)) {
        if (redPlus2Released) {
            delay(50);  // 消抖
            if (isButtonPressed(BTN_RED_PLUS2)) {
                redCount += 2;
                Serial.print("Red +2 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                Serial.flush();
                displayScore();  // 更新显示
                redPlus2Released = false;
            }
        }
    } else {
        redPlus2Released = true;
    }
    
    // 检测蓝色方+2按钮
    if (isButtonPressed(BTN_BLUE_PLUS2)) {
        if (bluePlus2Released) {
            delay(50);  // 消抖
            if (isButtonPressed(BTN_BLUE_PLUS2)) {
                blueCount += 2;
                Serial.print("Blue +2 | Score: ");
                Serial.print(redCount);
                Serial.print(" - ");
                Serial.print(blueCount);
                Serial.println(" | Blue");
                Serial.flush();
                displayScore();  // 更新显示
                bluePlus2Released = false;
            }
        }
    } else {
        bluePlus2Released = true;
    }
    
    // 检测重置按钮（绿色按钮）- 发送比分后重置
    if (isButtonPressed(BTN_RESET_PIN)) {
        if (resetReleased) {
            delay(50);  // 消抖
            if (isButtonPressed(BTN_RESET_PIN)) {
                Serial.println("Green button pressed - Sending score to server...");
                Serial.flush();

                // 发送比分到server，仅在成功时才清零
                if (sendScoreToServer(redCount, blueCount)) {
                    Serial.println("Score sent successfully!");
                    redCount = 0;
                    blueCount = 0;
                    Serial.println("Reset! Score: 0 - 0");
                    Serial.flush();
                    displayScore();  // 更新显示为0000
                } else {
                    Serial.println("Failed to send score! Score kept for retry.");
                    Serial.flush();
                    // 发送失败：保留分数，闪烁一下提示用户
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