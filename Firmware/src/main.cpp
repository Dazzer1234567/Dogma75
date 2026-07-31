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

    // Set touch/release thresholds for all 12 electrodes. Lowered from 12/6
    // so that marginal side-touches still register when the modifier pad is
    // being held (holding another pad shifts the whole-chip baseline and cuts
    // the effective delta at other pads). If this causes false triggers, back
    // off toward 8/4.
    for (uint8_t i = 0; i < 12; i++) {
        mpr121WriteRegister(addr, 0x41 + i * 2, 6);   // Touch threshold
        mpr121WriteRegister(addr, 0x42 + i * 2, 3);   // Release threshold
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

// --- Non-blocking OLED renderer ---
// A full line render is 32 I2C chunks of 64 bytes each. At 400 kHz that's
// ~1.6 ms per chunk => ~50 ms if done in one call, which starved serial and
// touch handling and made LED commands feel laggy and variable. The renderer
// below emits ONE chunk per oledStep() call, so the main loop always stays
// responsive to incoming serial commands within ~1.6 ms of any chunk boundary.
uint8_t oledDrawing = 0;      // 0=idle, 1=line1, 2=line2
uint8_t oledDrawStep = 0;     // 0..31
char    oledRenderBuf[22];    // snapshot so mid-render text updates don't tear
uint8_t oledRenderRow = 0;    // top physical row of the line being drawn
uint8_t oledRenderLen = 0;
uint8_t oledRenderStart = 0;
uint8_t oledRenderEnd = 0;

void oledStep() {
    if (!oledFound) return;

    // Idle: latch new work if a line is dirty.
    if (oledDrawing == 0) {
        const char* src = nullptr;
        if (oledLine1Changed) {
            oledLine1Changed = false;
            oledDrawing = 1;
            oledRenderRow = 8;
            src = oledLine1;
        } else if (oledLine2Changed) {
            oledLine2Changed = false;
            oledDrawing = 2;
            oledRenderRow = 40;
            src = oledLine2;
        } else {
            return;
        }
        strncpy(oledRenderBuf, src, sizeof(oledRenderBuf) - 1);
        oledRenderBuf[sizeof(oledRenderBuf) - 1] = '\0';
        oledRenderLen = strlen(oledRenderBuf);
        if (oledRenderLen > 21) oledRenderLen = 21;
        oledRenderStart = (128 - oledRenderLen * 6) / 2;
        oledRenderEnd = oledRenderStart + oledRenderLen * 6;
        oledDrawStep = 0;
        oledSetWindow(0x00, 0x7F, oledRenderRow, oledRenderRow + 15);
    }

    // Emit one 64-byte chunk. Step encodes (fontRow, dup, chunkHalf):
    //   fontRow = step / 4   dup = (step/2) % 2   chunkHalf = step % 2
    // Only fontRow matters for the pixel calculation; the OLED's column
    // auto-increment consumes the bytes into the right position in the
    // pre-set window.
    int fontRow = oledDrawStep / 4;
    int chunkOffset = (oledDrawStep % 2) * 64;

    Wire2.beginTransmission(OLED_ADDR);
    Wire2.write(0x40);
    for (int i = 0; i < 64; i++) {
        uint8_t col = chunkOffset + i;
        if (col >= oledRenderStart && col < oledRenderEnd) {
            int ci = (col - oledRenderStart) / 6;
            int pi = (col - oledRenderStart) % 6;
            if (pi < 5 && ci < oledRenderLen) {
                char c = oledRenderBuf[ci];
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

    oledDrawStep++;
    if (oledDrawStep >= 32) {
        oledDrawing = 0;  // finished; next call will latch new work if pending
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

// Forward declaration so ledSet() and flashStep() can call it before the
// definition further down.
void pca9685SetPWM(uint8_t channel, uint16_t value);

// Convenience wrapper: LED ON/OFF for open-drain wiring (PWM=0 sinks, 4095 = high-Z).
inline void ledSet(uint8_t channel, bool on) {
    pca9685SetPWM(channel, on ? 0 : 4095);
}

// ---- Local UI state owned by the firmware ----
// These give the controller instant LED response without a USB round-trip to
// the DAW. The DAW can still override any LED via LED:N:ON/OFF, and the
// mirror below is kept in sync when it does so.
// pad 24 is a momentary pan-modifier now: LED 8 lights only while it's held.
// No persistent state to track locally beyond the LED itself.
bool playLedLocal     = false; // pad 19 -> LED 3. Just predictive; DAW is authoritative.
bool loopLeftLocal    = false; // pad 20 -> LED 4. Just predictive; DAW is authoritative.
bool recordLeftLocal  = false; // pad 21 -> LED 5. Just predictive; DAW is authoritative.
bool recordRightLocal = false; // pad 22 -> LED 6. Just predictive; DAW is authoritative.
bool loopRightLocal   = false; // pad 23 -> LED 7. Just predictive; DAW is authoritative.
bool modifierHeld     = false; // pad 26. Also tracked DAW-side; we mirror here to
                               // decide whether pad 19 should flash-reject instead
                               // of toggling the play LED.

// OLED display mode. In diagnostic mode (default) the firmware writes raw
// "BUTTON N ON" / "ENCODER N: delta" lines on every touch/encoder event. In
// descriptive mode the firmware stops auto-updating the OLED and only shows
// text the DAW pushes via DISP:1:text / DISP:2:text. Toggle with pad 3 held
// + pad 23 pressed.
bool     displayMode     = false; // false = diagnostic, true = descriptive
bool     pad3Held        = false;
// Held-state trackers for the "on-stop behaviour" combos. Pad 14 + play sets
// return-to-start-on-stop; pad 15 + play sets keep-position-on-stop. While
// either is held, a pad 19 press does NOT toggle LED 3 — the press is
// consumed by the DAW as a mode command.
bool     pad14Held       = false;
bool     pad15Held       = false;
// OLED write-lock. Two flavours:
//   HARD (isSoft = false): mode-switch banner; blocks everything for its
//                          duration regardless of user activity.
//   SOFT (isSoft = true):  DISPHOLD text from the DAW; blocks other writes,
//                          but any touch/encoder event on the controller
//                          clears the lock so the next event's feedback can
//                          appear immediately.
uint32_t oledLockUntilMs = 0;
bool     oledLockIsSoft  = false;

// Clear-markers mode. Entered by holding pad 26 (modifier) and pressing pad
// 19 (play). Continuously fade-flashes the play LED plus the four
// loop/record LEDs at 2 Hz. Clicking either loop LED (pads 20/23) clears both
// loop markers and stops the loop pair flashing; clicking either record LED
// (pads 21/22) does the same for the record pair. Tapping pad 26 again exits.
static const uint32_t FLASH_PERIOD_MS = 500;   // 2 Hz
static const uint32_t FLASH_TICK_MS   = 20;    // 50 Hz PWM refresh
bool     flashMode          = false;
uint32_t flashStartMs       = 0;               // reference for phase calc
uint32_t flashLastUpdateMs  = 0;

// Called once per loop() while flashMode is active. Emits at most one PCA9685
// write batch every FLASH_TICK_MS so the fade stays smooth without hogging I2C.
void flashStep() {
    if (!flashMode) return;
    uint32_t nowMs = millis();
    if (nowMs - flashLastUpdateMs < FLASH_TICK_MS) return;
    flashLastUpdateMs = nowMs;

    // Triangle wave: brightness ramps 0 -> 1 over the first half of each
    // period, then 1 -> 0 over the second half.
    uint32_t elapsed = nowMs - flashStartMs;
    uint32_t phase = elapsed % FLASH_PERIOD_MS;
    uint32_t halfPeriod = FLASH_PERIOD_MS / 2;
    uint32_t brightness;  // 0..4095
    if (phase < halfPeriod) {
        brightness = (phase * 4095UL) / halfPeriod;
    } else {
        brightness = ((FLASH_PERIOD_MS - phase) * 4095UL) / halfPeriod;
    }
    // Our wiring: PWM=0 => fully on, PWM=4095 => off. Invert.
    uint16_t pwm = (uint16_t)(4095 - brightness);

    // Play LED: always flashes while clear-markers mode is active — it is
    // the mode indicator.
    pca9685SetPWM(3, pwm);
    // Loop / record pair LEDs: solid on when their pair is ON, flashing
    // when OFF, so the user can toggle a pair off and still see the LED
    // (and toggle again to bring it back).
    pca9685SetPWM(4, loopLeftLocal    ? 0 : pwm);
    pca9685SetPWM(7, loopRightLocal   ? 0 : pwm);
    pca9685SetPWM(5, recordLeftLocal  ? 0 : pwm);
    pca9685SetPWM(6, recordRightLocal ? 0 : pwm);
}

// Apply a display-mode change (from either the pad 3+pad 23 combo or a
// DAW-side SETMODE command). Idempotent: safe to call with the same value
// we're already in — it re-writes the header, announces to the DAW, and
// resets held-state trackers.
void setDisplayMode(bool desc) {
    displayMode = desc;
    snprintf(oledLine1, sizeof(oledLine1),
             displayMode ? "DISPLAY DESCRIPTIVE" : "DISPLAY DIAGNOSTIC");
    snprintf(oledLine2, sizeof(oledLine2), " ");
    oledLine1Changed = true;
    oledLine2Changed = true;
    oledLockUntilMs  = 0;
    oledLockIsSoft   = false;
    Serial.println(displayMode ? "MODE:DESC" : "MODE:DIAG");
    modifierHeld = false;
    pad14Held    = false;
    pad15Held    = false;
}

void enterFlashMode() {
    flashMode         = true;
    flashStartMs      = millis();
    flashLastUpdateMs = 0;
    // Consume the modifier — a subsequent release of pad 26 shouldn't matter.
    modifierHeld      = false;
}

void exitFlashMode() {
    flashMode = false;
    // Restore all flash-affected LEDs to whatever local mirrors say they
    // should be. The DAW will re-assert via LED:N commands next tick if any
    // of these disagree with its authoritative state.
    ledSet(3, playLedLocal);
    ledSet(4, loopLeftLocal);
    ledSet(5, recordLeftLocal);
    ledSet(6, recordRightLocal);
    ledSet(7, loopRightLocal);
    // Show "EXIT" for 2 s in DESCRIPTIVE mode only — diagnostic mode is a
    // pure button/encoder-name display and shouldn't get status banners.
    // The DAW schedules a playback-state push for after this lock expires.
    if (displayMode) {
        snprintf(oledLine1, sizeof(oledLine1), "EXIT");
        snprintf(oledLine2, sizeof(oledLine2), " ");
        oledLine1Changed = true;
        oledLine2Changed = true;
        oledLockUntilMs  = millis() + 2000;
        oledLockIsSoft   = false;
    }
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
        // Default is diagnostic mode — line 1 is the persistent mode header,
        // line 2 shows the last button/encoder event.
        snprintf(oledLine1, sizeof(oledLine1), "DISPLAY DIAGNOSTIC");
        snprintf(oledLine2, sizeof(oledLine2), "READY");
        oledLine1Changed = true;
        oledLine2Changed = true;
        // Actual rendering happens incrementally from loop() via oledStep().
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
        // Push initial local UI LED states. LED 8 (pad-24 pan modifier) is
        // off until the user physically holds the button.
        ledSet(8, false);
        ledSet(3, playLedLocal);
        ledSet(4, loopLeftLocal);
        ledSet(5, recordLeftLocal);
        ledSet(6, recordRightLocal);
        ledSet(7, loopRightLocal);
        Serial.println("LED test done");
    } else {
        Serial.println("PCA9685 NOT found");
    }

    delay(500);
    Serial.println("DogControl Ready!");
    Serial.print("6 Encoders + ");
    Serial.print(mpr121Count);
    Serial.println("x MPR121 Touch + PCA9685 LEDs");
    // Announce initial display mode so the DAW knows to ignore inputs.
    Serial.println(displayMode ? "MODE:DESC" : "MODE:DIAG");
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
            // DAW-initiated display-mode switch. Idempotent — DAW uses this
            // on startup to force a known-good state regardless of what mode
            // the controller was in from a previous session.
            else if (serialInputBuffer == "SETMODE:DIAG") {
                setDisplayMode(false);
            }
            else if (serialInputBuffer == "SETMODE:DESC") {
                setDisplayMode(true);
            }
            // DISP:1:text / DISP:2:text — DAW-pushed OLED line text. Only
            // takes effect in DESCRIPTIVE mode (diagnostic mode shows raw
            // button numbers only, uniformly for every pad). Also blocked by
            // an active mode-message lock.
            else if (serialInputBuffer.startsWith("DISP:1:") || serialInputBuffer.startsWith("DISP:2:")) {
                if (displayMode && millis() >= oledLockUntilMs) {
                    int line = serialInputBuffer.charAt(5) - '0';
                    String text = serialInputBuffer.substring(7);
                    if (line == 1) {
                        strncpy(oledLine1, text.c_str(), sizeof(oledLine1) - 1);
                        oledLine1[sizeof(oledLine1) - 1] = '\0';
                        oledLine1Changed = true;
                    } else if (line == 2) {
                        strncpy(oledLine2, text.c_str(), sizeof(oledLine2) - 1);
                        oledLine2[sizeof(oledLine2) - 1] = '\0';
                        oledLine2Changed = true;
                    }
                }
            }
            // DISPFORCE:1:text / DISPFORCE:2:text — DAW-pushed OLED text
            // used for live status readouts (e.g. clear-markers mode). Still
            // gated on DESCRIPTIVE mode: diagnostic mode is intentionally
            // pure — only button/encoder names appear there, nothing from
            // the DAW.
            else if (serialInputBuffer.startsWith("DISPFORCE:1:") || serialInputBuffer.startsWith("DISPFORCE:2:")) {
                if (displayMode && millis() >= oledLockUntilMs) {
                    int line = serialInputBuffer.charAt(10) - '0';
                    String text = serialInputBuffer.substring(12);
                    if (line == 1) {
                        strncpy(oledLine1, text.c_str(), sizeof(oledLine1) - 1);
                        oledLine1[sizeof(oledLine1) - 1] = '\0';
                        oledLine1Changed = true;
                    } else if (line == 2) {
                        strncpy(oledLine2, text.c_str(), sizeof(oledLine2) - 1);
                        oledLine2[sizeof(oledLine2) - 1] = '\0';
                        oledLine2Changed = true;
                    }
                }
            }
            // DISPHOLD:1:text / DISPHOLD:2:text — verbose text held on screen
            // for 3 s (a "soft" lock). Only takes effect in descriptive mode;
            // ignored in diagnostic mode so the button-event auto-updates
            // still get through. Any subsequent touch or encoder activity on
            // the controller clears the soft lock so the next event's
            // feedback appears immediately.
            else if (serialInputBuffer.startsWith("DISPHOLD:1:") || serialInputBuffer.startsWith("DISPHOLD:2:")) {
                if (displayMode) {
                    int line = serialInputBuffer.charAt(9) - '0';
                    String text = serialInputBuffer.substring(11);
                    if (line == 1) {
                        strncpy(oledLine1, text.c_str(), sizeof(oledLine1) - 1);
                        oledLine1[sizeof(oledLine1) - 1] = '\0';
                        oledLine1Changed = true;
                    } else if (line == 2) {
                        strncpy(oledLine2, text.c_str(), sizeof(oledLine2) - 1);
                        oledLine2[sizeof(oledLine2) - 1] = '\0';
                        oledLine2Changed = true;
                    }
                    oledLockUntilMs = millis() + 3000;
                    oledLockIsSoft  = true;
                }
            }
            // Parse LED commands: "LED:channel:ON" or "LED:channel:OFF"
            else if (serialInputBuffer.startsWith("LED:")) {
                int firstColon = 3;  // position of first ':'
                int secondColon = serialInputBuffer.indexOf(':', firstColon + 1);
                if (secondColon > 0) {
                    int channel = serialInputBuffer.substring(firstColon + 1, secondColon).toInt();
                    String state = serialInputBuffer.substring(secondColon + 1);
                    if (pca9685Found && channel >= 0 && channel < 16) {
                        bool on = (state == "ON");
                        if (on || state == "OFF") {
                            pca9685SetPWM(channel, on ? 0 : 4095);
                            // Keep local mirrors in sync so the next local
                            // toggle starts from the state the DAW just set.
                            if (channel == 3) playLedLocal     = on;
                            if (channel == 4) loopLeftLocal    = on;
                            if (channel == 5) recordLeftLocal  = on;
                            if (channel == 6) recordRightLocal = on;
                            if (channel == 7) loopRightLocal   = on;
                            // channel 8: no state — pad 24 is a momentary modifier
                            // and the firmware drives LED 8 directly on press/release.
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
        // Send accumulated delta every 10ms (filters out ±1 oscillations).
        if (pendingDelta[i] != 0 && (nowMicros - lastSendTime[i]) >= 10000) {
            // Any encoder activity releases a soft OLED lock so the next
            // event can display its feedback immediately.
            if (oledLockIsSoft && millis() < oledLockUntilMs) {
                oledLockUntilMs = 0;
                oledLockIsSoft  = false;
            }
            Serial.print("E");
            Serial.print(i + 1);
            Serial.print(":");
            Serial.println(pendingDelta[i]);
            // Auto-update OLED only in diagnostic mode + no active mode-lock.
            // Diagnostic mode's line 1 is a persistent header ("DISPLAY
            // DIAGNOSTIC"); only line 2 gets updated with the latest
            // button or encoder event.
            if (!displayMode && millis() >= oledLockUntilMs) {
                snprintf(oledLine2, sizeof(oledLine2), "ENCODER %d: %+ld", i + 1, pendingDelta[i]);
                oledLine2Changed = true;
            }
            digitalWrite(LED_BUILTIN, HIGH);
            pendingDelta[i] = 0;
            lastSendTime[i] = nowMicros;
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
                    // Any touch press releases a soft OLED lock so the next
                    // action's feedback appears immediately.
                    if (oledLockIsSoft && millis() < oledLockUntilMs) {
                        oledLockUntilMs = 0;
                        oledLockIsSoft  = false;
                    }
                    bool suppressTouchSend = false;
                    // Local UI reactions: predictive LED toggles so the user
                    // sees the response instantly, before the DAW even sees
                    // the touch event.
                    if (flashMode) {
                        // Clear/restore-markers mode: only certain pads act.
                        // Loop and record pair clicks toggle their pair on/off.
                        // flashStep() re-renders LEDs every tick based on the
                        // local mirrors, so no LED writes are needed here —
                        // the change is picked up within ~20 ms.
                        if (touchNum == 26) {
                            exitFlashMode();
                        } else if (touchNum == 20 || touchNum == 23) {
                            bool newState = !loopLeftLocal;
                            loopLeftLocal  = newState;
                            loopRightLocal = newState;
                        } else if (touchNum == 21 || touchNum == 22) {
                            bool newState = !recordLeftLocal;
                            recordLeftLocal  = newState;
                            recordRightLocal = newState;
                        }
                        // Other pads ignored while in flash mode.
                    } else {
                        // Normal mode.
                        if (touchNum == 3) {
                            pad3Held = true;
                        } else if (touchNum == 14) {
                            pad14Held = true;
                        } else if (touchNum == 15) {
                            pad15Held = true;
                        } else if (touchNum == 23 && pad3Held) {
                            // Display-mode toggle via helper (same code path
                            // as the DAW's SETMODE command).
                            setDisplayMode(!displayMode);
                            // Don't tell the DAW about this pad 23 press:
                            // it's consumed by the mode switch, and forwarding
                            // it would cause a spurious loop-right jump on
                            // the newly-active descriptive side.
                            suppressTouchSend = true;
                        } else if (touchNum == 26) {
                            modifierHeld = true;
                        } else if (touchNum == 24) {
                            // Momentary pan-modifier: light LED 8 while held.
                            ledSet(8, true);
                        } else if (touchNum == 19) {
                            if (modifierHeld) {
                                // Modifier + play => enter clear-markers mode.
                                enterFlashMode();
                            } else if (pad14Held || pad15Held) {
                                // Mode-setting combo (return-on-stop). DAW
                                // handles it; leave LED 3 alone.
                            } else {
                                playLedLocal = !playLedLocal;
                                ledSet(3, playLedLocal);
                            }
                        }
                        // Pads 20-23 in normal mode: no local action. Pressing
                        // them is now a "jump playhead to marker" trigger
                        // handled by the DAW — LEDs 4/5/6/7 reflect marker
                        // enabled state and only change via clear-markers mode.
                    }
                    if (!suppressTouchSend) {
                        Serial.print("TOUCH:");
                        Serial.println(touchNum);
                    }
                    // Diagnostic auto-write: only in diagnostic mode + no
                    // active mode-lock. Always shows button events (including
                    // during clear-markers mode) — diagnostic mode is a pure
                    // event log.
                    if (!displayMode && millis() >= oledLockUntilMs) {
                        snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d ON", touchNum);
                        oledLine2Changed = true;
                    }
                } else if (!now && was) {
                    if (touchNum == 26) modifierHeld = false;
                    if (touchNum == 24) ledSet(8, false);  // pan-mod released
                    if (touchNum == 3)  pad3Held  = false;
                    if (touchNum == 14) pad14Held = false;
                    if (touchNum == 15) pad15Held = false;
                    Serial.print("RELEASE:");
                    Serial.println(touchNum);
                    if (!displayMode && millis() >= oledLockUntilMs) {
                        snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d OFF", touchNum);
                        oledLine2Changed = true;
                    }
                }
            }
            lastTouchState[chip] = touchState;
        }
    }

    // --- OLED: emit at most one I2C chunk per loop iteration ---
    // Each call is ~1.6 ms max of I2C wire time, so the loop stays responsive
    // to serial and touch/encoder events even while a line is being redrawn.
    oledStep();

    // --- Play-LED fade flash (mod+play rejection indicator) ---
    flashStep();

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
