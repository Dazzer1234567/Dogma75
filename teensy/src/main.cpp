#include <Arduino.h>
#include <Wire.h>

// --- Encoder pins ---
const int PIN_A = 2;
const int PIN_B = 3;

// --- MCP23017 ---
const uint8_t MCP23017_ADDR = 0x20;
const uint8_t IODIRA = 0x00;
const uint8_t IODIRB = 0x01;
const uint8_t GPPUA = 0x0C;
const uint8_t GPIOA = 0x12;
const uint8_t GPIOB = 0x13;

// --- Encoder state ---
volatile long encoderPosition = 0;
volatile int lastA = 0;
volatile int lastB = 0;

// --- Button state ---
uint8_t lastButtonState = 0xFF;

void encoderISR() {
    int a = digitalRead(PIN_A);
    int b = digitalRead(PIN_B);

    if (a != lastA) {
        if (a == b) {
            encoderPosition--;
        } else {
            encoderPosition++;
        }
    }
    if (b != lastB) {
        if (a == b) {
            encoderPosition++;
        } else {
            encoderPosition--;
        }
    }

    lastA = a;
    lastB = b;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    // --- Encoder setup ---
    pinMode(PIN_A, INPUT_PULLUP);
    pinMode(PIN_B, INPUT_PULLUP);
    lastA = digitalRead(PIN_A);
    lastB = digitalRead(PIN_B);
    attachInterrupt(digitalPinToInterrupt(PIN_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_B), encoderISR, CHANGE);

    // --- MCP23017 setup ---
    Wire.begin();
    delay(100);

    Wire.beginTransmission(MCP23017_ADDR);
    if (Wire.endTransmission() == 0) {
        Serial.println("MCP23017 found!");

        // Port A as inputs
        Wire.beginTransmission(MCP23017_ADDR);
        Wire.write(IODIRA);
        Wire.write(0xFF);
        Wire.endTransmission();

        // Pull-ups on Port A
        Wire.beginTransmission(MCP23017_ADDR);
        Wire.write(GPPUA);
        Wire.write(0xFF);
        Wire.endTransmission();

        // Port B as outputs (for LED)
        Wire.beginTransmission(MCP23017_ADDR);
        Wire.write(IODIRB);
        Wire.write(0x00);
        Wire.endTransmission();

        // LEDs off
        Wire.beginTransmission(MCP23017_ADDR);
        Wire.write(GPIOB);
        Wire.write(0x00);
        Wire.endTransmission();
    } else {
        Serial.println("MCP23017 not found - button won't work");
    }

    delay(500);
    Serial.println("DogControl Ready!");
    Serial.println("Turn encoder or press button...");
}

void loop() {
    static long lastPosition = 0;
    static unsigned long lastPrint = 0;
    static int lastRawA = -1;
    static int lastRawB = -1;

    // --- Debug: read raw pin values ---
    int rawA = digitalRead(PIN_A);
    int rawB = digitalRead(PIN_B);
    if (rawA != lastRawA || rawB != lastRawB) {
        Serial.print("Raw pins - A: ");
        Serial.print(rawA);
        Serial.print(" B: ");
        Serial.println(rawB);
        lastRawA = rawA;
        lastRawB = rawB;
    }

    // --- Read encoder and send delta ---
    long pos = encoderPosition;
    if (pos != lastPosition) {
        long delta = pos - lastPosition;
        Serial.print("Delta:");
        Serial.println(delta);
        lastPosition = pos;
    }

    // --- Read button from MCP23017 ---
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(GPIOA);
    Wire.endTransmission();
    Wire.requestFrom(MCP23017_ADDR, (uint8_t)1);
    uint8_t buttonState = Wire.read();

    if ((buttonState & 0x01) != (lastButtonState & 0x01)) {
        if ((buttonState & 0x01) == 0) {
            Serial.println("Button PRESSED!");
            digitalWrite(LED_BUILTIN, HIGH);
            // LED on
            Wire.beginTransmission(MCP23017_ADDR);
            Wire.write(GPIOB);
            Wire.write(0x01);
            Wire.endTransmission();
        } else {
            Serial.println("Button released");
            digitalWrite(LED_BUILTIN, LOW);
            // LED off
            Wire.beginTransmission(MCP23017_ADDR);
            Wire.write(GPIOB);
            Wire.write(0x00);
            Wire.endTransmission();
        }
        lastButtonState = buttonState;
    }

    // --- Status every second ---
    if (millis() - lastPrint > 1000) {
        Serial.print("Pin2=");
        Serial.print(rawA);
        Serial.print(" Pin3=");
        Serial.print(rawB);
        Serial.print(" | Encoder: ");
        Serial.print(pos);
        Serial.print(" | Button: ");
        Serial.println((buttonState & 0x01) ? "released" : "pressed");
        lastPrint = millis();
    }

    delay(1);
}
