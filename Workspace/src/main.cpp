#include <Arduino.h>
#include <Wire.h>

// --- 6 Encoders ---
const int NUM_ENCODERS = 6;
const int PIN_A[NUM_ENCODERS] = {2, 4, 6, 8, 10, 12};
const int PIN_B[NUM_ENCODERS] = {3, 5, 7, 9, 11, 14};

volatile long encoderPosition[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
volatile int lastA[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
volatile int lastB[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};

// ISR for each encoder
void encoderISR0() {
    int a = digitalRead(PIN_A[0]); int b = digitalRead(PIN_B[0]);
    if (a != lastA[0]) { encoderPosition[0] += (a == b) ? -1 : 1; }
    if (b != lastB[0]) { encoderPosition[0] += (a == b) ? 1 : -1; }
    lastA[0] = a; lastB[0] = b;
}
void encoderISR1() {
    int a = digitalRead(PIN_A[1]); int b = digitalRead(PIN_B[1]);
    if (a != lastA[1]) { encoderPosition[1] += (a == b) ? -1 : 1; }
    if (b != lastB[1]) { encoderPosition[1] += (a == b) ? 1 : -1; }
    lastA[1] = a; lastB[1] = b;
}
void encoderISR2() {
    int a = digitalRead(PIN_A[2]); int b = digitalRead(PIN_B[2]);
    if (a != lastA[2]) { encoderPosition[2] += (a == b) ? -1 : 1; }
    if (b != lastB[2]) { encoderPosition[2] += (a == b) ? 1 : -1; }
    lastA[2] = a; lastB[2] = b;
}
void encoderISR3() {
    int a = digitalRead(PIN_A[3]); int b = digitalRead(PIN_B[3]);
    if (a != lastA[3]) { encoderPosition[3] += (a == b) ? -1 : 1; }
    if (b != lastB[3]) { encoderPosition[3] += (a == b) ? 1 : -1; }
    lastA[3] = a; lastB[3] = b;
}
void encoderISR4() {
    int a = digitalRead(PIN_A[4]); int b = digitalRead(PIN_B[4]);
    if (a != lastA[4]) { encoderPosition[4] += (a == b) ? -1 : 1; }
    if (b != lastB[4]) { encoderPosition[4] += (a == b) ? 1 : -1; }
    lastA[4] = a; lastB[4] = b;
}
void encoderISR5() {
    int a = digitalRead(PIN_A[5]); int b = digitalRead(PIN_B[5]);
    if (a != lastA[5]) { encoderPosition[5] += (a == b) ? -1 : 1; }
    if (b != lastB[5]) { encoderPosition[5] += (a == b) ? 1 : -1; }
    lastA[5] = a; lastB[5] = b;
}

void (*encoderISRs[NUM_ENCODERS])() = {
    encoderISR0, encoderISR1, encoderISR2,
    encoderISR3, encoderISR4, encoderISR5
};

// --- MPR121 Touch Sensor ---
const uint8_t MPR121_ADDR = 0x5A;
uint16_t lastTouchState = 0;
bool mpr121Found = false;

void mpr121WriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MPR121_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t mpr121ReadRegister(uint8_t reg) {
    Wire.beginTransmission(MPR121_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(MPR121_ADDR, (uint8_t)1);
    return Wire.read();
}

bool mpr121Init() {
    // Check device is on the bus
    Wire.beginTransmission(MPR121_ADDR);
    if (Wire.endTransmission() != 0) return false;

    // Soft reset
    mpr121WriteRegister(0x80, 0x63);
    delay(10);

    // Set touch/release thresholds for all 12 electrodes
    for (uint8_t i = 0; i < 12; i++) {
        mpr121WriteRegister(0x41 + i * 2, 12);  // Touch threshold
        mpr121WriteRegister(0x42 + i * 2, 6);   // Release threshold
    }

    // MHD, NHD, NCL, FDL - filtering
    mpr121WriteRegister(0x2B, 0x01); // MHD Rising
    mpr121WriteRegister(0x2C, 0x01); // NHD Rising
    mpr121WriteRegister(0x2D, 0x0E); // NCL Rising
    mpr121WriteRegister(0x2E, 0x00); // FDL Rising
    mpr121WriteRegister(0x2F, 0x01); // MHD Falling
    mpr121WriteRegister(0x30, 0x05); // NHD Falling
    mpr121WriteRegister(0x31, 0x01); // NCL Falling
    mpr121WriteRegister(0x32, 0x00); // FDL Falling
    mpr121WriteRegister(0x33, 0x00); // NHD Touched
    mpr121WriteRegister(0x34, 0x00); // NCL Touched
    mpr121WriteRegister(0x35, 0x00); // FDL Touched

    // Debounce
    mpr121WriteRegister(0x5B, 0x00);

    // FFI, CDC config
    mpr121WriteRegister(0x5C, 0x10);
    // CDT, SFI, ESI config
    mpr121WriteRegister(0x5D, 0x20);

    // Enable all 12 electrodes, set run mode
    mpr121WriteRegister(0x5E, 0x8F);

    return true;
}

uint16_t mpr121ReadTouch() {
    uint8_t lsb = mpr121ReadRegister(0x00);
    uint8_t msb = mpr121ReadRegister(0x01);
    return (msb << 8) | lsb;
}

// --- PCA9685 LED Driver ---
const uint8_t PCA9685_ADDR = 0x40;
bool pca9685Found = false;

void pca9685WriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(PCA9685_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

bool pca9685Init() {
    Wire.beginTransmission(PCA9685_ADDR);
    if (Wire.endTransmission() != 0) return false;

    pca9685WriteRegister(0x00, 0x20); // Mode1: auto-increment
    delay(5);
    pca9685WriteRegister(0x01, 0x04); // Mode2: totem pole outputs
    return true;
}

void pca9685SetPWM(uint8_t channel, uint16_t value) {
    // value: 0-4095
    uint8_t reg = 0x06 + 4 * channel;
    Wire.beginTransmission(PCA9685_ADDR);
    Wire.write(reg);
    Wire.write(0x00);           // ON low
    Wire.write(0x00);           // ON high
    Wire.write(value & 0xFF);   // OFF low
    Wire.write(value >> 8);     // OFF high
    Wire.endTransmission();
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    // --- Encoder setup ---
    for (int i = 0; i < NUM_ENCODERS; i++) {
        pinMode(PIN_A[i], INPUT_PULLUP);
        pinMode(PIN_B[i], INPUT_PULLUP);
        lastA[i] = digitalRead(PIN_A[i]);
        lastB[i] = digitalRead(PIN_B[i]);
        attachInterrupt(digitalPinToInterrupt(PIN_A[i]), encoderISRs[i], CHANGE);
        attachInterrupt(digitalPinToInterrupt(PIN_B[i]), encoderISRs[i], CHANGE);
    }

    // --- I2C setup ---
    Wire.begin();
    delay(100);

    // I2C scan
    Serial.println("I2C scan:");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("  Found device at 0x");
            Serial.println(addr, HEX);
        }
    }
    Serial.println("I2C scan done");

    // MPR121 init
    if (mpr121Init()) {
        mpr121Found = true;
        Serial.println("MPR121 found!");
    } else {
        Serial.println("MPR121 NOT found");
    }

    // PCA9685 init
    if (pca9685Init()) {
        pca9685Found = true;
        Serial.println("PCA9685 found!");
        // Turn off all channels (active-LOW: 4095 = off)
        for (int i = 0; i < 16; i++) {
            pca9685SetPWM(i, 4095);
        }
        // Startup LED test: sweep channel 0 brightness up then down
        Serial.println("LED test: sweep channel 0...");
        for (int v = 4095; v >= 0; v -= 256) {
            pca9685SetPWM(0, (uint16_t)v);
            delay(30);
        }
        pca9685SetPWM(0, 0); // full brightness briefly
        delay(200);
        for (uint16_t v = 0; v <= 4095; v += 256) {
            pca9685SetPWM(0, v);
            delay(30);
        }
        pca9685SetPWM(0, 4095); // off
        Serial.println("LED test done");
    } else {
        Serial.println("PCA9685 NOT found");
    }

    delay(500);
    Serial.println("DogControl Ready!");
    Serial.println("6 Encoders + MPR121 Touch + PCA9685 LEDs");
}

// Serial command buffer for receiving from DAW
String serialInputBuffer = "";

void processSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            serialInputBuffer.trim();
            // Parse LED commands: "LED:channel:ON" or "LED:channel:OFF"
            if (serialInputBuffer.startsWith("LED:")) {
                int firstColon = 3;  // position of first ':'
                int secondColon = serialInputBuffer.indexOf(':', firstColon + 1);
                if (secondColon > 0) {
                    int channel = serialInputBuffer.substring(firstColon + 1, secondColon).toInt();
                    String state = serialInputBuffer.substring(secondColon + 1);
                    if (pca9685Found && channel >= 0 && channel < 16) {
                        if (state == "ON") {
                            pca9685SetPWM(channel, 0);     // active-LOW: 0 = on
                        } else if (state == "OFF") {
                            pca9685SetPWM(channel, 4095);  // active-LOW: 4095 = off
                        }
                    }
                }
            }
            serialInputBuffer = "";
        } else {
            serialInputBuffer += c;
        }
    }
}

void loop() {
    static long lastPosition[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
    static long pendingDelta[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
    static unsigned long lastSendTime[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
    static unsigned long lastHeartbeat = 0;

    // --- Serial commands from DAW ---
    processSerialCommands();

    // --- Encoders (accumulate deltas, send every 10ms) ---
    unsigned long nowMicros = micros();
    for (int i = 0; i < NUM_ENCODERS; i++) {
        long pos = encoderPosition[i];
        if (pos != lastPosition[i]) {
            pendingDelta[i] += (pos - lastPosition[i]);
            lastPosition[i] = pos;
        }
        // Send accumulated delta every 10ms (filters out ±1 oscillations)
        if (pendingDelta[i] != 0 && (nowMicros - lastSendTime[i]) >= 10000) {
            Serial.print("E");
            Serial.print(i + 1);
            Serial.print(":");
            Serial.println(pendingDelta[i]);
            pendingDelta[i] = 0;
            lastSendTime[i] = nowMicros;
            digitalWrite(LED_BUILTIN, HIGH);
        }
    }
    digitalWrite(LED_BUILTIN, LOW);

    // --- Touch sensor ---
    if (mpr121Found) {
        uint16_t touchState = mpr121ReadTouch();
        if (touchState != lastTouchState) {
            for (int i = 0; i < 12; i++) {
                bool now = touchState & (1 << i);
                bool was = lastTouchState & (1 << i);
                if (now && !was) {
                    Serial.print("TOUCH:");
                    Serial.println(i);
                    // LED is now controlled by DAW response, not local toggle
                } else if (!now && was) {
                    Serial.print("RELEASE:");
                    Serial.println(i);
                }
            }
            lastTouchState = touchState;
        }
    }

    // --- Heartbeat ---
    if (millis() - lastHeartbeat > 5000) {
        Serial.print("heartbeat MPR121:");
        Serial.print(mpr121Found ? "OK" : "NO");
        Serial.print(" PCA9685:");
        Serial.println(pca9685Found ? "OK" : "NO");
        lastHeartbeat = millis();
    }

    delayMicroseconds(100);
}
