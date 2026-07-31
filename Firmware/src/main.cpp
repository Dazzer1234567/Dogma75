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

// --- MPR121 Touch Sensors (up to 3 chips) ---
const int NUM_MPR121 = 3;
const uint8_t MPR121_ADDR[NUM_MPR121] = {0x5A, 0x5B, 0x5C};
uint16_t lastTouchState[NUM_MPR121] = {0, 0, 0};
bool mpr121Found[NUM_MPR121] = {false, false, false};
int mpr121Count = 0;

void mpr121WriteRegister(uint8_t addr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t mpr121ReadRegister(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0;
    return Wire.read();
}

bool mpr121Init(uint8_t addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) return false;

    // Soft reset
    mpr121WriteRegister(addr, 0x80, 0x63);
    delay(10);

    // Set touch/release thresholds for all 12 electrodes
    for (uint8_t i = 0; i < 12; i++) {
        mpr121WriteRegister(addr, 0x41 + i * 2, 12);  // Touch threshold
        mpr121WriteRegister(addr, 0x42 + i * 2, 6);   // Release threshold
    }

    // MHD, NHD, NCL, FDL - filtering
    mpr121WriteRegister(addr, 0x2B, 0x01);
    mpr121WriteRegister(addr, 0x2C, 0x01);
    mpr121WriteRegister(addr, 0x2D, 0x0E);
    mpr121WriteRegister(addr, 0x2E, 0x00);
    mpr121WriteRegister(addr, 0x2F, 0x01);
    mpr121WriteRegister(addr, 0x30, 0x05);
    mpr121WriteRegister(addr, 0x31, 0x01);
    mpr121WriteRegister(addr, 0x32, 0x00);
    mpr121WriteRegister(addr, 0x33, 0x00);
    mpr121WriteRegister(addr, 0x34, 0x00);
    mpr121WriteRegister(addr, 0x35, 0x00);

    // Debounce
    mpr121WriteRegister(addr, 0x5B, 0x00);

    // FFI, CDC config
    mpr121WriteRegister(addr, 0x5C, 0x10);
    // CDT, SFI, ESI config
    mpr121WriteRegister(addr, 0x5D, 0x20);

    // Enable all 12 electrodes, set run mode
    mpr121WriteRegister(addr, 0x5E, 0x8F);

    return true;
}

uint16_t mpr121ReadTouch(uint8_t addr) {
    uint8_t lsb = mpr121ReadRegister(addr, 0x00);
    uint8_t msb = mpr121ReadRegister(addr, 0x01);
    return (msb << 8) | lsb;
}

// --- SSD1362 OLED Display (Wire2, 256x64, 4-bit grayscale) ---
const uint8_t OLED_ADDR = 0x3C;
const uint8_t OLED_RST_PIN = 26;
bool oledFound = false;

void oledCommand(uint8_t cmd) {
    Wire2.beginTransmission(OLED_ADDR);
    Wire2.write(0x00);  // Co=0, D/C#=0: command
    Wire2.write(cmd);
    Wire2.endTransmission();
}

void oledCommand2(uint8_t cmd, uint8_t arg) {
    Wire2.beginTransmission(OLED_ADDR);
    Wire2.write(0x00);
    Wire2.write(cmd);
    Wire2.write(arg);
    Wire2.endTransmission();
}

bool oledInit() {
    // Check presence
    Wire2.beginTransmission(OLED_ADDR);
    if (Wire2.endTransmission() != 0) return false;

    // Init sequence from U8g2 SSD1362 256x64 driver
    oledCommand2(0xFD, 0x12);  // Unlock commands
    oledCommand(0xAE);          // Display OFF
    oledCommand2(0x23, 0x00);  // Disable fade mode
    oledCommand2(0x81, 0x9F);  // Contrast
    oledCommand2(0xA0, 0xD0);  // Remap: COM remap + horizontal + SEG split
    oledCommand2(0xA1, 0x00);  // Start line 0
    oledCommand2(0xA2, 0x00);  // Display offset 0
    oledCommand(0xA4);          // Normal display mode
    oledCommand2(0xA8, 0x3F);  // Multiplex ratio 64
    oledCommand2(0xAB, 0x01);  // VDD regulator
    oledCommand2(0xAD, 0x8E);  // External IREF
    oledCommand2(0xB1, 0x22);  // Phase length
    oledCommand2(0xB3, 0xA0);  // Clock divider
    oledCommand2(0xB6, 0x04);  // Second precharge period
    oledCommand(0xB9);          // Linear grayscale lookup table
    oledCommand2(0xBC, 0x1F);  // Precharge voltage
    oledCommand2(0xBD, 0x01);  // Precharge voltage capacitor
    oledCommand2(0xBE, 0x07);  // COM deselect voltage (VCOMH)
    oledCommand(0xAF);          // Display ON
    return true;
}

void oledSetWindow(uint8_t colStart, uint8_t colEnd, uint8_t rowStart, uint8_t rowEnd) {
    // Set column address range (0-127, each column = 2 pixels)
    Wire2.beginTransmission(OLED_ADDR);
    Wire2.write(0x00);
    Wire2.write(0x15);
    Wire2.write(colStart);
    Wire2.write(colEnd);
    Wire2.endTransmission();
    // Set row address range (0-63)
    Wire2.beginTransmission(OLED_ADDR);
    Wire2.write(0x00);
    Wire2.write(0x75);
    Wire2.write(rowStart);
    Wire2.write(rowEnd);
    Wire2.endTransmission();
}

void oledFill(uint8_t value) {
    // Fill entire screen: 128 columns * 64 rows = 8192 bytes
    oledSetWindow(0x00, 0x7F, 0x00, 0x3F);
    for (int row = 0; row < 64; row++) {
        // Send one row (128 bytes) in chunks
        for (int chunk = 0; chunk < 128; chunk += 28) {
            int n = min(28, 128 - chunk);
            Wire2.beginTransmission(OLED_ADDR);
            Wire2.write(0x40);  // Co=0, D/C#=1: data
            for (int i = 0; i < n; i++) {
                Wire2.write(value);
            }
            Wire2.endTransmission();
        }
    }
}

void oledTestPattern() {
    // Vertical gradient: left=dark, right=bright
    oledSetWindow(0x00, 0x7F, 0x00, 0x3F);
    for (int row = 0; row < 64; row++) {
        for (int chunk = 0; chunk < 128; chunk += 28) {
            int n = min(28, 128 - chunk);
            Wire2.beginTransmission(OLED_ADDR);
            Wire2.write(0x40);
            for (int i = 0; i < n; i++) {
                uint8_t col = chunk + i;
                // 4-bit grayscale per pixel, 2 pixels per byte
                uint8_t gray = (col * 15) / 127;
                Wire2.write((gray << 4) | gray);
            }
            Wire2.endTransmission();
        }
    }
}

// --- OLED Text Rendering (2x scaled 5x7 font) ---
// Font: chars 32 (space) through 90 (Z), 5 column bytes each
// Each byte = 1 column, bit 0 = top row, bit 6 = bottom row
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x00,0x00,0x5F,0x00,0x00}, // 33 !
    {0x00,0x07,0x00,0x07,0x00}, // 34 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
    {0x23,0x13,0x08,0x64,0x62}, // 37 %
    {0x36,0x49,0x55,0x22,0x50}, // 38 &
    {0x00,0x05,0x03,0x00,0x00}, // 39 '
    {0x00,0x1C,0x22,0x41,0x00}, // 40 (
    {0x00,0x41,0x22,0x1C,0x00}, // 41 )
    {0x08,0x2A,0x1C,0x2A,0x08}, // 42 *
    {0x08,0x08,0x3E,0x08,0x08}, // 43 +
    {0x00,0x50,0x30,0x00,0x00}, // 44 ,
    {0x08,0x08,0x08,0x08,0x08}, // 45 -
    {0x00,0x60,0x60,0x00,0x00}, // 46 .
    {0x20,0x10,0x08,0x04,0x02}, // 47 /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
    {0x00,0x42,0x7F,0x40,0x00}, // 49 1
    {0x42,0x61,0x51,0x49,0x46}, // 50 2
    {0x21,0x41,0x45,0x4B,0x31}, // 51 3
    {0x18,0x14,0x12,0x7F,0x10}, // 52 4
    {0x27,0x45,0x45,0x45,0x39}, // 53 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
    {0x01,0x71,0x09,0x05,0x03}, // 55 7
    {0x36,0x49,0x49,0x49,0x36}, // 56 8
    {0x06,0x49,0x49,0x29,0x1E}, // 57 9
    {0x00,0x36,0x36,0x00,0x00}, // 58 :
    {0x00,0x56,0x36,0x00,0x00}, // 59 ;
    {0x00,0x08,0x14,0x22,0x41}, // 60 <
    {0x14,0x14,0x14,0x14,0x14}, // 61 =
    {0x41,0x22,0x14,0x08,0x00}, // 62 >
    {0x02,0x01,0x51,0x09,0x06}, // 63 ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
    {0x7F,0x49,0x49,0x49,0x36}, // 66 B
    {0x3E,0x41,0x41,0x41,0x22}, // 67 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
    {0x7F,0x49,0x49,0x49,0x41}, // 69 E
    {0x7F,0x09,0x09,0x01,0x01}, // 70 F
    {0x3E,0x41,0x41,0x51,0x32}, // 71 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
    {0x00,0x41,0x7F,0x41,0x00}, // 73 I
    {0x20,0x40,0x41,0x3F,0x01}, // 74 J
    {0x7F,0x08,0x14,0x22,0x41}, // 75 K
    {0x7F,0x40,0x40,0x40,0x40}, // 76 L
    {0x7F,0x02,0x04,0x02,0x7F}, // 77 M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
    {0x7F,0x09,0x09,0x09,0x06}, // 80 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82 R
    {0x46,0x49,0x49,0x49,0x31}, // 83 S
    {0x01,0x01,0x7F,0x01,0x01}, // 84 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
    {0x7F,0x20,0x18,0x20,0x7F}, // 87 W
    {0x63,0x14,0x08,0x14,0x63}, // 88 X
    {0x03,0x04,0x78,0x04,0x03}, // 89 Y
    {0x61,0x51,0x49,0x45,0x43}, // 90 Z
};

// Display state
char oledLine1[22] = "";
char oledLine2[22] = "";
bool oledLine1Changed = false;
bool oledLine2Changed = false;

// Single-pass line renderer: clears + draws in one go with large I2C chunks
void oledDrawLine(uint8_t row, const char* text) {
    int len = strlen(text);
    if (len > 21) len = 21;
    uint8_t startCol = (128 - len * 6) / 2;
    uint8_t endCol = startCol + len * 6;

    oledSetWindow(0x00, 0x7F, row, row + 15);

    for (int fontRow = 0; fontRow < 8; fontRow++) {
        for (int dup = 0; dup < 2; dup++) {  // vertical 2x
            // Stream full 128-column row in 2 chunks of 64
            for (int chunk = 0; chunk < 128; chunk += 64) {
                Wire2.beginTransmission(OLED_ADDR);
                Wire2.write(0x40);
                for (int i = 0; i < 64; i++) {
                    uint8_t col = chunk + i;
                    if (col >= startCol && col < endCol) {
                        int ci = (col - startCol) / 6;
                        int pi = (col - startCol) % 6;
                        if (pi < 5 && ci < len) {
                            char c = text[ci];
                            if (c >= 'a' && c <= 'z') c -= 32;
                            if (c < 32 || c > 90) c = 32;
                            uint8_t on = (font5x7[c - 32][pi] >> fontRow) & 1 ? 0xF : 0;
                            Wire2.write((on << 4) | on);
                        } else {
                            Wire2.write(0x00);
                        }
                    } else {
                        Wire2.write(0x00);
                    }
                }
                Wire2.endTransmission();
            }
        }
    }
}

void oledUpdateDisplay() {
    if (!oledFound) return;
    if (oledLine1Changed) {
        oledLine1Changed = false;
        oledDrawLine(8, oledLine1);
    }
    if (oledLine2Changed) {
        oledLine2Changed = false;
        oledDrawLine(40, oledLine2);
    }
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
    // Mode2: OUTDRV=0 (open-drain). LED anodes sit on a higher V+ rail than
    // PCA9685 VCC, so a totem-pole HIGH would still leave ~1.5-3 V across
    // the LED and it would glow dim in the "off" state. High-Z kills current
    // completely.
    pca9685WriteRegister(0x01, 0x00);
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

    // --- I2C setup (Wire = pins 18/19, Wire2 = pins 24/25) ---
    Wire.begin();
    Wire.setClock(100000);  // 100kHz standard mode
    Wire.setTimeout(5);     // 5ms timeout to prevent bus hangs
    delay(100);

    // I2C scan - Wire (MPR121, PCA9685)
    Serial.println("I2C scan Wire (pins 18/19):");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("  Found device at 0x");
            Serial.println(addr, HEX);
        }
    }
    Serial.println("I2C Wire scan done");

    // I2C scan - Wire2 (OLED display on pins 24/25)
    Wire2.begin();
    Wire2.setClock(400000);  // 400kHz fast mode for OLED
    Wire2.setTimeout(5);
    delay(100);

    // Reset OLED via pin 26
    pinMode(OLED_RST_PIN, OUTPUT);
    digitalWrite(OLED_RST_PIN, LOW);
    delay(10);
    digitalWrite(OLED_RST_PIN, HIGH);
    delay(100);

    Serial.println("I2C scan Wire2 (pins 24/25):");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire2.beginTransmission(addr);
        if (Wire2.endTransmission() == 0) {
            Serial.print("  Found device at 0x");
            Serial.println(addr, HEX);
        }
    }
    Serial.println("I2C Wire2 scan done");

    // OLED init
    if (oledInit()) {
        oledFound = true;
        Serial.println("SSD1362 OLED initialized!");
        oledFill(0x00);  // clear screen
        snprintf(oledLine1, sizeof(oledLine1), "DOGMA75");
        snprintf(oledLine2, sizeof(oledLine2), "READY");
        oledLine1Changed = true;
        oledLine2Changed = true;
        oledUpdateDisplay();
        Serial.println("OLED initialized and ready");
    } else {
        Serial.println("SSD1362 OLED NOT found");
    }

    // MPR121 init (up to 3 chips)
    for (int i = 0; i < NUM_MPR121; i++) {
        if (mpr121Init(MPR121_ADDR[i])) {
            mpr121Found[i] = true;
            mpr121Count++;
            Serial.print("MPR121 #");
            Serial.print(i);
            Serial.print(" (0x");
            Serial.print(MPR121_ADDR[i], HEX);
            Serial.println(") found!");
        } else {
            Serial.print("MPR121 #");
            Serial.print(i);
            Serial.print(" (0x");
            Serial.print(MPR121_ADDR[i], HEX);
            Serial.println(") NOT found");
        }
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
    Serial.print("6 Encoders + ");
    Serial.print(mpr121Count);
    Serial.println("x MPR121 Touch + PCA9685 LEDs");
}

// Serial command buffer for receiving from DAW
String serialInputBuffer = "";

void processSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            serialInputBuffer.trim();
            // I2C scan command: "SCAN"
            if (serialInputBuffer == "SCAN") {
                Serial.println("I2C scan Wire (pins 18/19):");
                for (uint8_t addr = 1; addr < 127; addr++) {
                    Wire.beginTransmission(addr);
                    if (Wire.endTransmission() == 0) {
                        Serial.print("  Found device at 0x");
                        Serial.println(addr, HEX);
                    }
                }
                Serial.println("I2C scan Wire2 (pins 24/25):");
                for (uint8_t addr = 1; addr < 127; addr++) {
                    Wire2.beginTransmission(addr);
                    if (Wire2.endTransmission() == 0) {
                        Serial.print("  Found device at 0x");
                        Serial.println(addr, HEX);
                    }
                }
                Serial.println("I2C scan done");
            }
            // OLED test commands
            else if (serialInputBuffer == "OLED") {
                if (oledFound) {
                    oledTestPattern();
                    Serial.println("OLED test pattern");
                } else {
                    Serial.println("OLED not found");
                }
            }
            else if (serialInputBuffer == "OLED:ON") {
                if (oledFound) { oledFill(0xFF); Serial.println("OLED filled white"); }
            }
            else if (serialInputBuffer == "OLED:OFF") {
                if (oledFound) { oledFill(0x00); Serial.println("OLED cleared"); }
            }
            // Parse LED commands: "LED:channel:ON" or "LED:channel:OFF"
            else if (serialInputBuffer.startsWith("LED:")) {
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
            // Update OLED
            snprintf(oledLine1, sizeof(oledLine1), "ENCODER %d: %+ld", i + 1, pendingDelta[i]);
            oledLine1Changed = true;
            pendingDelta[i] = 0;
            lastSendTime[i] = nowMicros;
            digitalWrite(LED_BUILTIN, HIGH);
        }
    }
    digitalWrite(LED_BUILTIN, LOW);

    // --- Touch sensors (all MPR121 chips) ---
    for (int chip = 0; chip < NUM_MPR121; chip++) {
        if (!mpr121Found[chip]) continue;
        uint16_t touchState = mpr121ReadTouch(MPR121_ADDR[chip]);
        if (touchState != lastTouchState[chip]) {
            for (int i = 0; i < 12; i++) {
                bool now = touchState & (1 << i);
                bool was = lastTouchState[chip] & (1 << i);
                int touchNum = chip * 12 + i;  // 0-11, 12-23, 24-35
                if (now && !was) {
                    Serial.print("TOUCH:");
                    Serial.println(touchNum);
                    snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d ON", touchNum);
                    oledLine2Changed = true;
                } else if (!now && was) {
                    Serial.print("RELEASE:");
                    Serial.println(touchNum);
                    snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d OFF", touchNum);
                    oledLine2Changed = true;
                }
            }
            lastTouchState[chip] = touchState;
        }
    }

    // --- OLED update (throttled to 20fps max) ---
    static unsigned long lastOledUpdate = 0;
    if ((oledLine1Changed || oledLine2Changed) && (millis() - lastOledUpdate) >= 50) {
        oledUpdateDisplay();
        lastOledUpdate = millis();
    }

    // --- Heartbeat ---
    if (millis() - lastHeartbeat > 5000) {
        Serial.print("heartbeat MPR121:");
        Serial.print(mpr121Count);
        Serial.print("/");
        Serial.print(NUM_MPR121);
        Serial.print(" PCA9685:");
        Serial.println(pca9685Found ? "OK" : "NO");
        lastHeartbeat = millis();
    }

    delayMicroseconds(100);
}
