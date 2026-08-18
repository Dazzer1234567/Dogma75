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
// Thresholds for pads 20-23 only, which sit next to the big CAP1188 sensor
// strip. Raise these if phantom touches persist; lower them toward the
// normal 6/3 if those four pads start feeling unresponsive to a light touch.
const uint8_t MPR121_TOUCH_TH_NEAR_STRIP   = 12;
const uint8_t MPR121_RELEASE_TH_NEAR_STRIP = 6;
uint16_t lastTouchState[NUM_MPR121] = {0, 0, 0};
bool mpr121Found[NUM_MPR121] = {false, false, false};
int mpr121Count = 0;
// Auto-recovery bookkeeping. Set to true by a failing read; the touch
// scan loop then tries mpr121Init() on that chip once every ~500 ms
// until it comes back. Prevents needing a USB replug after live wire
// changes brief the I2C bus.
bool         mpr121NeedsReinit[NUM_MPR121]    = {false, false, false};
uint32_t     mpr121LastReinitMs[NUM_MPR121]   = {0, 0, 0};
bool         lastMpr121ReadOk                 = true;

void mpr121WriteRegister(uint8_t addr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t mpr121ReadRegister(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { lastMpr121ReadOk = false; return 0; }
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) { lastMpr121ReadOk = false; return 0; }
    lastMpr121ReadOk = true;
    return Wire.read();
}

bool mpr121Init(uint8_t addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) return false;

    // Soft reset
    mpr121WriteRegister(addr, 0x80, 0x63);
    delay(10);

    // Set touch/release thresholds per electrode. The normal pads run at 6/3,
    // lowered from 12/6 so that marginal side-touches still register when the
    // modifier pad is being held (holding another pad shifts the whole-chip
    // baseline and cuts the effective delta at other pads).
    //
    // The pads listed below are the exception. They sit near the 30x3 cm
    // CAP1188 sensor strip, which couples into them hard enough to cross a
    // 6/3 threshold with nobody touching anything — measured at ~16 phantom
    // touches on pad 23 in 9 seconds, vanishing the moment the strip's C1
    // wire is unplugged. They get extra noise margin instead.
    //
    // Pads 20-23 (loop/punch) were the first found. Pads 16 and 35 were
    // added later: 35 is the input-monitor button, and its phantom touches
    // were conspicuous because it is the only pad wired to something outside
    // the DAW — each one audibly muted an Antelope channel. The others were
    // firing too, just invisibly. Phantom presses are recognisable in the
    // log by their duration: 60-90 ms, where a real tap is 100-300 ms.
    //
    // If a pad here starts feeling unresponsive to a light touch, drop it
    // back; if a new pad starts chattering, add it. Everything else stays at
    // the sensitive 6/3 needed for side-touches while a modifier is held.
    auto nearStripPad = [](int pad) {
        return (pad >= 20 && pad <= 23) || pad == 16 || pad == 35;
    };
    for (uint8_t i = 0; i < 12; i++) {
        int chip = 0;
        for (int c = 0; c < NUM_MPR121; c++) {
            if (MPR121_ADDR[c] == addr) { chip = c; break; }
        }
        const int pad = chip * 12 + i;
        const bool nearStrip = nearStripPad(pad);
        mpr121WriteRegister(addr, 0x41 + i * 2,
                            nearStrip ? MPR121_TOUCH_TH_NEAR_STRIP   : 6);
        mpr121WriteRegister(addr, 0x42 + i * 2,
                            nearStrip ? MPR121_RELEASE_TH_NEAR_STRIP : 3);
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

// --- CAP1188 capacitive controller (Wire, one big 30x3 cm pad on C1) ---
//
// Only C1 is wired. C2-C8 are left floating, so their status bits are
// ignored in software rather than disabled in hardware — the sensor-enable
// register isn't documented in the sources used to write this, and masking
// achieves the same result with no risk of writing a wrong register.
const uint8_t CAP1188_ADDR = 0x29;   // default with the AD pin left floating

// Register map (verified against Adafruit's CAP1188 libraries)
const uint8_t CAP1188_REG_MAIN        = 0x00;  // bit0 = INT, cleared to ack
const uint8_t CAP1188_REG_STATUS      = 0x03;  // one bit per sensor input
const uint8_t CAP1188_REG_SENSITIVITY = 0x1F;  // bits[6:4] = gain index
const uint8_t CAP1188_REG_CAL_ACTIVATE= 0x26;  // bit per channel, self-clears
const uint8_t CAP1188_REG_MULTI_TOUCH = 0x2A;  // 0x00 = allow simultaneous
const uint8_t CAP1188_REG_THRESHOLD_1 = 0x30;  // C1 threshold, 0-127
const uint8_t CAP1188_REG_STANDBY_CFG = 0x41;
const uint8_t CAP1188_REG_LED_LINK    = 0x72;  // 0x00 = DAW owns the LEDs
const uint8_t CAP1188_REG_PRODID      = 0xFD;  // expect 0x50
const uint8_t CAP1188_REG_MANUID      = 0xFE;  // expect 0x5D

// Gain index into (128,64,32,16,8,4,2,1)x — index 0 is the MOST sensitive.
// Index 7 (1x) is the lowest, and it is what this pad wants: 30x3 cm of
// copper produces an enormous signal, and anything more sensitive saturates
// the delta at +/-127 and latches the button on.
//
// Measured on the finished hardware (pad via a 30 pF series capacitor):
// resting delta ~0, peak on touch ~42-61. A threshold of 20 gave 16 clean
// press/release pairs out of 16 taps. Lower it toward 15 if light taps ever
// get missed; raise it if the pad ever self-triggers.
//
// NOTE the series capacitor is not optional. Wired directly, the strip's raw
// capacitance exceeds what the chip can null during calibration and the delta
// reads exactly 0 at EVERY gain setting — the button simply never responds.
uint8_t cap1188GainIndex = 7;                  // = 1x, least sensitive
// Was 20, which reliably caught firm taps (peaks 42-61) but dropped light
// ones. 12 is still ~12x the measured resting floor of +/-1, so there is
// plenty of margin left before phantom presses return. Prefer lowering
// this over raising the gain: gain amplifies the noise floor too, and the
// clean floor is what keeps this pad from chattering into its neighbours.
uint8_t cap1188Threshold = 12;                 // C1 touch threshold, 0-127

const uint8_t CAP1188_REG_DELTA_1 = 0x10;      // signed 8-bit delta, C1
const uint8_t CAP1188_REG_AVG_SAMP = 0x24;     // AVG[6:4] / SAMP_TIME / CYCLE

// Averaging and Sampling Configuration. Power-on default is 0x39, which is
// AVG=011 (average EIGHT consecutive samples before reporting), SAMP_TIME
// 1.28 ms, CYCLE_TIME as-is. Eight samples is far too slow for a button:
// a quick tap can begin and end before an averaged result is ever produced,
// so the press is never seen at all.
//
// 0x09 keeps SAMP_TIME and CYCLE_TIME but sets AVG=000 — report on the
// first sample. Undo is a button you hit fast, so responsiveness matters
// more here than the noise rejection averaging buys, and the pad's signal
// (peaks 42-61 against a floor of +/-1) has margin to spare.
const uint8_t CAP1188_AVG_SAMP_FAST = 0x09;

// Pad number reported to the DAW. 0-35 belong to the three MPR121s.
const int CAP1188_PAD_NUMBER = 36;

bool    cap1188Found     = false;
uint8_t cap1188LastState = 0;

// Defined much further down, next to the MPR121 touch handling.
void handleTouchPressed(int touchNum);
void handleTouchReleased(int touchNum);

void cap1188WriteRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(CAP1188_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

bool cap1188ReadOk = true;
uint8_t cap1188ReadRegister(uint8_t reg) {
    Wire.beginTransmission(CAP1188_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) { cap1188ReadOk = false; return 0; }
    if (Wire.requestFrom(CAP1188_ADDR, (uint8_t)1) != 1) { cap1188ReadOk = false; return 0; }
    cap1188ReadOk = true;
    return Wire.read();
}

bool cap1188Init() {
    Wire.beginTransmission(CAP1188_ADDR);
    if (Wire.endTransmission() != 0) return false;

    // Identify before configuring, so a different chip at 0x29 can't be
    // silently misprogrammed.
    if (cap1188ReadRegister(CAP1188_REG_PRODID) != 0x50) return false;
    if (cap1188ReadRegister(CAP1188_REG_MANUID) != 0x5D) return false;

    // Allow simultaneous touches. The default BLOCKS all but one, which
    // would silently break every chorded gesture this controller relies on
    // (26+19 loop edit, 19+14 return-on-stop, 24+E6 scrub, ...).
    cap1188WriteRegister(CAP1188_REG_MULTI_TOUCH, 0x00);

    // Low gain for a large pad. Low nibble is the base shift; 0x0F is the
    // power-on default and is left alone.
    cap1188WriteRegister(CAP1188_REG_SENSITIVITY,
                         (uint8_t)((cap1188GainIndex << 4) | 0x0F));
    cap1188WriteRegister(CAP1188_REG_THRESHOLD_1, cap1188Threshold);
    cap1188WriteRegister(CAP1188_REG_AVG_SAMP,    CAP1188_AVG_SAMP_FAST);
    cap1188WriteRegister(CAP1188_REG_STANDBY_CFG, 0x30);

    // Don't let the chip drive its own LEDs from touch — the DAW is the
    // source of truth for every LED on this controller.
    cap1188WriteRegister(CAP1188_REG_LED_LINK, 0x00);

    // Force a fresh baseline. Matters because the pad may have been plugged
    // in after power-up, in which case the boot calibration captured the
    // wrong reference and the pad would read as permanently pressed.
    cap1188WriteRegister(CAP1188_REG_CAL_ACTIVATE, 0x01);
    delay(50);

    cap1188LastState = 0;
    return true;
}

// Poll C1 and emit through the same handlers as the MPR121 pads, so the big
// button behaves identically to every other pad (diagnostic display,
// modifier combos, DAW protocol).
// The CAP1188 samples on its own ~35 ms cycle. Polling far faster than that
// starves it — measured: 5408 reads in 15 s produced ZERO status hits, while
// reading the same register every 100 ms saw touches reliably. 25 ms keeps
// the button feeling instant while leaving the chip time to convert.
const uint32_t CAP1188_POLL_INTERVAL_MS = 25;
uint32_t cap1188LastPollMs = 0;

void cap1188Scan() {
    if (!cap1188Found) return;
    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - cap1188LastPollMs) < CAP1188_POLL_INTERVAL_MS) return;
    cap1188LastPollMs = nowMs;
    uint8_t status = cap1188ReadRegister(CAP1188_REG_STATUS);
    if (!cap1188ReadOk) return;

    if (status) {
        // Ack the interrupt latch, else status never clears on release.
        uint8_t main = cap1188ReadRegister(CAP1188_REG_MAIN);
        cap1188WriteRegister(CAP1188_REG_MAIN, main & ~0x01);
    }

    uint8_t now = status & 0x01;          // C1 only; ignore floating C2-C8
    if (now != cap1188LastState) {
        if (now) handleTouchPressed(CAP1188_PAD_NUMBER);
        else     handleTouchReleased(CAP1188_PAD_NUMBER);
        cap1188LastState = now;
    }
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

// --- OSC-mute status indicator ---
// A 4-pixel-wide vertical bar on the far-left of the OLED that lights up
// when the DAW's OSC mute is active. Sent from the DAW via MUTEIND:1 / :0.
// Redrawn on state changes AND after every text-line render (the text
// renderer's full-width window would otherwise blank the indicator).
bool muteIndOn = false;
bool muteIndDirty = true;   // paint on first idle tick so boot state is defined

void oledDrawMuteIndicator() {
    if (!oledFound) return;
    // 2 cols (each = 2 pixels) x 64 rows = 128 bytes. Chunk to stay under
    // the Wire2 buffer, matching the existing full-screen fill pattern.
    oledSetWindow(0x00, 0x01, 0, 63);
    uint8_t val = muteIndOn ? 0xFF : 0x00;
    int total = 2 * 64;
    for (int off = 0; off < total; off += 28) {
        int n = min(28, total - off);
        Wire2.beginTransmission(OLED_ADDR);
        Wire2.write(0x40);
        for (int i = 0; i < n; i++) Wire2.write(val);
        Wire2.endTransmission();
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
    {0x00,0x7F,0x41,0x41,0x00}, // 91 [
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ]
    {0x04,0x02,0x01,0x02,0x04}, // 94 ^
    {0x40,0x40,0x40,0x40,0x40}, // 95 _  (cursor char)
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
// One-character grayscale "fade" for the pending letter during name entry.
// oledPendingCharCol is the char index within oledLine2 (or -1 for none);
// oledPendingBrightness is 0..15 (matches the SSD1362 4-bit level, 0xF=max).
// The values are snapshotted into oledRenderPendingCol / -Brightness when a
// line-2 render is latched, so a mid-render brightness update doesn't tear.
int8_t   oledPendingCharCol       = -1;
uint8_t  oledPendingBrightness    = 15;
int8_t   oledRenderPendingCol     = -1;
uint8_t  oledRenderPendingBrightness = 15;

void oledStep() {
    if (!oledFound) return;

    // Idle: latch new work if a line is dirty.
    if (oledDrawing == 0) {
        // Repaint the far-left mute indicator first — text renders overlap
        // its columns, so any pending text draw would clobber it if we
        // deferred the repair.
        if (muteIndDirty) {
            oledDrawMuteIndicator();
            muteIndDirty = false;
            return;
        }
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
            // Snapshot the pending-letter fade state for this render pass so
            // the whole line uses a consistent brightness value.
            oledRenderPendingCol        = oledPendingCharCol;
            oledRenderPendingBrightness = oledPendingBrightness;
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
                if (c < 32 || c > 95) c = 32;
                uint8_t bright = 0xF;
                if (oledDrawing == 2 && (int8_t)ci == oledRenderPendingCol) {
                    bright = oledRenderPendingBrightness & 0x0F;
                }
                uint8_t on = (font5x7[c - 32][pi] >> fontRow) & 1 ? bright : 0;
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
        // Text render zero'd cols 0-1 across the line's row band, so the
        // left-column mute indicator needs to be redrawn if it was on.
        if (muteIndOn) muteIndDirty = true;
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

// Per-channel "on" PWM value. 0 = max brightness, 4095 = essentially off.
// DAW pushes these via LEDBRT:ch:value at startup and whenever the user
// tweaks a slider. Defaults are max brightness so a fresh Teensy behaves
// as before if it comes up without any DAW-side push.
uint16_t ledBrightness[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Identify mode: when true for a channel, the physical LED flashes at a
// fixed 2 Hz using its configured brightness, ignoring the normal on/off
// state. Used by the DAW's LED popup's per-channel "identify" button so
// the user can find which physical LED corresponds to each slider.
bool     ledIdentify[9]      = { false, false, false, false, false, false, false, false, false };
uint32_t ledIdentifyLastMs   = 0;
bool     ledIdentifyPhase    = false;

// Force-on / force-off: while true the physical LED is held steady at
// its brightness (force-on) or dark (force-off), ignoring mirror state.
// Used by the DAW's "All ON" / "All OFF" test buttons. Takes precedence
// over identify so a channel is never simultaneously flashing and forced.
// The DAW enforces mutual exclusion — both should never be true at once.
bool     ledForceOn[9]       = { false, false, false, false, false, false, false, false, false };
bool     ledForceOff[9]      = { false, false, false, false, false, false, false, false, false };

// Convenience wrapper: LED ON/OFF for open-drain wiring (0..brightness sinks,
// 4095 = high-Z / dark). If identify OR force-on OR force-off owns the
// channel, the physical write is suppressed — those modes paint it directly.
inline void ledSet(uint8_t channel, bool on) {
    if (channel < 9 && (ledIdentify[channel] || ledForceOn[channel] || ledForceOff[channel])) return;
    uint16_t onVal = (channel < 9) ? ledBrightness[channel] : 0;
    pca9685SetPWM(channel, on ? onVal : 4095);
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
bool muteLedLocal     = false; // pad 18 -> LED 0. Predictive on tap-release; DAW confirms.
bool soloLedLocal     = false; // pad 17 -> LED 1. Predictive on tap-release; DAW confirms.
bool armLedLocal      = false; // pad 16 -> LED 2. Predictive on tap-release; DAW confirms.
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
// Set to millis() whenever we receive any command from the DAW (HB, LED,
// DISP, DISPFORCE, DISPHOLD, SETMODE, ...). Used to detect a disconnected
// DAW when the user tries to switch to descriptive mode.
uint32_t lastDawHeartbeatMs = 0;
static const uint32_t DAW_HEARTBEAT_TIMEOUT_MS = 5000;
// True while the DAW's heartbeat is fresh. Flipped false when we go
// DAW_HEARTBEAT_TIMEOUT_MS without a message. Used to detect the exact
// transition from connected -> disconnected so we can show the banner
// once (not every loop iteration).
bool dawConnected = false;
// Latched after a disconnect banner. Cleared by the next user touch/encoder
// event, which also flips the display to diagnostic mode. Until then the
// "DAW DISCONNECTED" message stays on the OLED.
bool pendingSwitchToDiagOnActivity = false;

// Text-input mode: line 2 shows a text buffer with a flashing cursor at
// the end. Turned on by TEXTIN:starttext from the DAW, off by TEXTIN:OFF.
// Cursor toggles visibility every CURSOR_BLINK_MS.
bool     textInputMode      = false;
char     textInputBuffer[22] = "";
bool     cursorVisible      = true;
uint32_t cursorLastToggleMs = 0;
static const uint32_t CURSOR_BLINK_MS = 500;
// Character cycler for name-entry. 0 = not cycling (cursor blinks); otherwise
// the letter currently displayed at the cursor position (fades in and out),
// committed via pad 20 (advance) or pad 21 (finalise).
char     textInputPending      = 0;
int8_t   textInputCurrentPad   = -1;   // pad whose letter-cycle is active
uint8_t  textInputCurrentIdx   = 0;    // index within that pad's letter set
// Cursor position within textInputBuffer, 0..strlen(buffer). Advanced/
// retreated by E1 while text-input is active (100 encoder ticks = 1 step).
int8_t   textInputCursorPos    = 0;
long     textInputE1Accum      = 0;    // running E1 delta pending threshold
static const long TEXTIN_E1_STEP = 100;

// Letter-pad mapping. 9 pads × up to 3 letters each = 26 letters.
struct LetterPad { uint8_t pad; const char* letters; };
static const LetterPad LETTER_PADS[] = {
    { 1,  "ABC" }, {  2, "DEF" }, {  3, "GHI" },
    { 5,  "JKL" }, {  6, "MNO" }, {  7, "PQR" },
    { 9,  "STU" }, { 10, "VWX" }, { 11, "YZ"  },
};
static const int NUM_LETTER_PADS = sizeof(LETTER_PADS) / sizeof(LETTER_PADS[0]);

// Returns the LETTER_PADS index for a touched pad, or -1 if it isn't a
// letter pad. Small linear scan — cheap and inlined by the compiler.
static int letterPadIndex(int pad) {
    for (int i = 0; i < NUM_LETTER_PADS; i++) {
        if (LETTER_PADS[i].pad == (uint8_t)pad) return i;
    }
    return -1;
}
// Text-input flash for pads 20 / 21 LEDs (channels 4 / 5). Independent
// from the clear-markers flashMode so the two modes don't stomp on each
// other's LED state.
bool     textInputFlashActive   = false;
uint32_t textInputFlashLastMs   = 0;
// Phase reference for the pad-20/21 flash. Previously the phase came from
// the free-running clock (millis() % FLASH_PERIOD_MS), so entering name-entry
// picked up wherever that happened to be — and phase 0 is fully OFF, meaning
// the LEDs could sit dark for up to a quarter-second before visibly starting
// to pulse. That read as the controller being sluggish to respond.
//
// Anchored to the moment of entry instead, offset by half a period so entry
// lands on the PEAK of the triangle: the LEDs come on at full brightness
// immediately. The offset keeps the wave shape identical, so the DAW's
// rename-preview (which reuses the same triangle formula from the phase we
// broadcast in RENAMESYNC) stays in sync with no change on its side.
uint32_t textInputFlashStartMs  = 0;
// Loop-time stats, reported and reset by the LOOPSTAT serial command.
uint32_t loopMaxUs   = 0;
uint64_t loopSumUs   = 0;
uint32_t loopCount   = 0;
// Held-state trackers for the "on-stop behaviour" combos. Pad 14 + play sets
// return-to-start-on-stop; pad 15 + play sets keep-position-on-stop. While
// either is held, a pad 19 press does NOT toggle LED 3 — the press is
// consumed by the DAW as a mode command.
bool     pad14Held       = false;
bool     pad15Held       = false;
// Double-tap detection on pad 15 (toggles loop playback). Window chosen to
// be comfortably longer than a deliberate double tap but short enough that
// two separate loop-edit toggles are not mistaken for one.
uint32_t pad15LastTapMs  = 0;
const uint32_t PAD15_DOUBLE_TAP_MS = 400;
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

// "Defined" state per pair, pushed from DAW via PAIRDEF messages. A pair
// is defined when both its markers exist (markerEverSet in the DAW),
// even if the pair is currently disabled. Used inside clear-markers mode
// to differentiate "off but defined" (flashing LEDs) from "not defined"
// (LEDs stay dark).
bool     loopPairDefined       = false;
bool     recordPairDefined     = false;

// Delete-arm within clear-markers mode. Toggled by pad 24 while flashMode
// is active. When armed, any DEFINED pair's LEDs switch from the normal
// triangle-wave flash to a fast on/off square wave, and pressing a
// marker pad in the pair wipes it. Pad-24 LED (channel 8) mirrors the
// armed state. Auto-disarms once neither pair is defined.
bool     flashModeDeleteArmed  = false;
static const uint32_t FAST_FLASH_HALFPERIOD_MS = 50;   // 10 Hz square wave

// Pad-19-modifier combo lock. When 19 is held and the user then presses
// 14 (return-to-start ON) or 15 (return-to-start OFF / "stop in place"),
// the combo fires and takes exclusive control of the controller: every
// other press is dropped until BOTH combo pads are released. Prevents
// accidental other-key presses while the user's fingers are still on
// the combo pair.
bool comboFired = false;

// DAW-exclusive input mode. Toggled via serial EXCLUSIVE:1 / EXCLUSIVE:0.
// While set, handleTouchPressed / handleTouchReleased forward raw TOUCH:N
// and RELEASE:N to the DAW WITHOUT running any local predictive-LED
// changes, gesture-to-command translations (LOOPPLAY, DELETEPAIR, etc.),
// combo locks, or mode dispatch (text-input / flash mode). The DAW owns
// every semantic reaction. Used by DAW modes like channel-entry where a
// stray local behaviour on pad 15/19/etc. would collide with the DAW's
// interpretation of the same tap.
//
// NOT set for track-rename entry — rename relies on the firmware's local
// letter-cycling (via PAIRFLASH + textInputMode) and DAW-exclusive would
// disable it. Use PAIRFLASH:1 on its own for that case.
bool dawExclusiveMode = false;

// Pad-15 hold-to-delete-all-loops. Short tap toggles loop-edit mode
// (deferred until release so the two behaviours can share the pad); a
// hold of DELETE_ALL_HOLD_MS wipes both marker pairs and runs a
// 1-second fade-out on LEDs 4/5/6/7 as the acknowledgement.
static const uint32_t DELETE_ALL_HOLD_MS   = 1000;
static const uint32_t DELETE_ALL_FADE_MS   = 1000;
uint32_t pad15PressMs        = 0;
bool     pad15LongPressFired = false;
uint32_t deleteAllFadeStartMs = 0;   // 0 = idle; else fade in progress

// Hard-reset gesture: hold pad 18 for 3 s. All 9 LEDs light + OLED shows
// "RESET" for as long as pad 18 stays held; on release, LEDs are
// repainted from the mirrors (which the DAW has been updating during the
// hold via RESYNC-triggered pushes) and the OLED lock lifts.
static const uint32_t RESET_HOLD_MS = 3000;
uint32_t pad18PressMs    = 0;
bool     pad18ResetFired = false;
bool     resetShowActive = false;

// Pad 19 used as a modifier for the "delete marker pair" gesture: hold 19
// and tap 20/23 to wipe the loop pair, or tap 21/22 to wipe the record
// pair. Pad 19 handling is deferred to release so a tapped combo doesn't
// also toggle play/stop. On release, if no combo fired, we replay the
// normal play/stop press-action.
bool     pad19Held             = false;
bool     pad19UsedAsModifier   = false;

// 3-quick-blink acknowledgement on the pair whose markers were just
// deleted. 6 transitions * 50 ms = ~300 ms total, then LEDs go out (the
// DAW's LED:N:OFF arrives shortly after and keeps them there).
static const uint32_t MARKER_DELETE_BLINK_MS   = 50;
static const uint8_t  MARKER_DELETE_BLINK_TICKS = 6;
uint8_t  markerDeleteFlashPair     = 0;   // 0 = idle, 1 = loop, 2 = rec
uint8_t  markerDeleteFlashCount    = 0;   // remaining transitions
uint32_t markerDeleteFlashLastMs   = 0;
bool     markerDeleteFlashOn       = false;

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

    // Play LED (3) AND pad-24 LED (8) both flash in-phase while clear-
    // markers mode is active — 24 is now also a mode indicator alongside
    // 19. Delete-arm overrides LED 8 to solid on (see below).
    pca9685SetPWM(3, pwm);
    if (!flashModeDeleteArmed) pca9685SetPWM(8, pwm);
    if (flashModeDeleteArmed) {
        // Delete-arm mode: DEFINED pairs get a fast square-wave "warning"
        // flash; undefined pairs stay dark. Enabled/disabled pair state
        // is irrelevant here — the user is about to wipe the pair.
        bool fastOn = (nowMs % (FAST_FLASH_HALFPERIOD_MS * 2)) < FAST_FLASH_HALFPERIOD_MS;
        uint16_t fastPwm = fastOn ? 0 : 4095;
        uint16_t loopPwm = loopPairDefined   ? fastPwm : 4095;
        uint16_t recPwm  = recordPairDefined ? fastPwm : 4095;
        pca9685SetPWM(4, loopPwm);
        pca9685SetPWM(7, loopPwm);
        pca9685SetPWM(5, recPwm);
        pca9685SetPWM(6, recPwm);
    } else {
        // Loop / record pair LEDs: three-state per pair.
        //   solid ON  — pair is currently enabled (loopLeftLocal / recordLeftLocal)
        //   flashing  — pair is defined but disabled (markers exist, pair off)
        //   OFF       — pair is not defined at all (markers never set / deleted)
        auto pairPwm = [&](bool enabled, bool defined) -> uint16_t {
            if (enabled) return 0;               // solid on
            if (defined) return pwm;             // triangle-wave flash
            return 4095;                          // fully off
        };
        uint16_t loopPwm = pairPwm(loopLeftLocal,   loopPairDefined);
        uint16_t recPwm  = pairPwm(recordLeftLocal, recordPairDefined);
        pca9685SetPWM(4, loopPwm);
        pca9685SetPWM(7, loopPwm);
        pca9685SetPWM(5, recPwm);
        pca9685SetPWM(6, recPwm);
    }
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
    Serial.println("DEBUG:FLASH:ON");
}

// Build the line-2 string for the current text-input state, left-justified.
// The OLED font renderer centers whatever text it's given, so to make the
// text look left-aligned we pad the line to the full 21-char width — a
// full-width string is "centered" flush with the left edge.
// If textInputPending is non-zero it takes the cursor position (the user
// is mid-cycle through A/B/C); otherwise the underscore blinks there.
static void textInputBuildLine(bool showCursor) {
    int cap = (int)sizeof(oledLine2) - 1;  // 21 usable + null
    int len = (int)strlen(textInputBuffer);
    if (len > cap) len = cap;
    int cur = textInputCursorPos;
    if (cur < 0) cur = 0;
    if (cur > len) cur = len;
    if (cur > cap - 1) cur = cap - 1;
    oledPendingCharCol = -1;
    // Fill each visible slot: normal letter, or the cursor overlay when
    // `i == cur`. Cursor overlay is the pending letter (mid-cycle) or a
    // blinking underscore that toggles the underlying letter on/off.
    int i = 0;
    for (; i < len || i == cur; i++) {
        if (i >= cap) break;
        if (i == cur) {
            if (textInputPending) {
                oledLine2[i]       = textInputPending;
                oledPendingCharCol = (int8_t)i;
            } else {
                oledLine2[i] = showCursor
                    ? '_'
                    : (i < len ? textInputBuffer[i] : ' ');
            }
        } else if (i < len) {
            oledLine2[i] = textInputBuffer[i];
        } else {
            oledLine2[i] = ' ';
        }
    }
    for (; i < cap; i++) oledLine2[i] = ' ';
    oledLine2[cap] = '\0';
    oledLine2Changed = true;
}

// Mute-LED marker-scroll indicator. DAW toggles via MUTEFLASH:1 / :0.
// Same triangle-wave timing as textInputFlash / clear-mode flash.
bool     muteFlashActive = false;
uint32_t muteFlashLastMs = 0;
void muteFlashStep() {
    if (!muteFlashActive) return;
    uint32_t nowMs = millis();
    if (nowMs - muteFlashLastMs < FLASH_TICK_MS) return;
    muteFlashLastMs = nowMs;
    uint32_t phase = nowMs % FLASH_PERIOD_MS;
    uint32_t halfPeriod = FLASH_PERIOD_MS / 2;
    uint32_t brightness = (phase < halfPeriod)
        ? (phase * 4095UL) / halfPeriod
        : ((FLASH_PERIOD_MS - phase) * 4095UL) / halfPeriod;
    uint16_t pwm = (uint16_t)(4095 - brightness);
    pca9685SetPWM(0, pwm);
}

// Flash LEDs 4 (pad 20) and 5 (pad 21) with the same triangle-wave used
// by the clear-markers flashMode. Independent state so a name-entry
// session doesn't collide with clear-markers mode.
// Current flash phase, anchored to entry (see textInputFlashStartMs). Used
// by both the LED step and the RENAMESYNC broadcasts so they cannot disagree.
static inline uint32_t textInputFlashPhase() {
    return (uint32_t)(millis() - textInputFlashStartMs) % FLASH_PERIOD_MS;
}

void textInputFlashStep() {
    if (!textInputFlashActive) return;
    uint32_t nowMs = millis();
    if (nowMs - textInputFlashLastMs < FLASH_TICK_MS) return;
    textInputFlashLastMs = nowMs;
    uint32_t phase = textInputFlashPhase();
    uint32_t halfPeriod = FLASH_PERIOD_MS / 2;
    uint32_t brightness = (phase < halfPeriod)
        ? (phase * 4095UL) / halfPeriod
        : ((FLASH_PERIOD_MS - phase) * 4095UL) / halfPeriod;
    uint16_t pwm = (uint16_t)(4095 - brightness);   // wiring: 0 = fully on
    pca9685SetPWM(4, pwm);
    pca9685SetPWM(5, pwm);
}

// Enter / exit text-input mode. Enter starts the pad-20/21 flash and
// resets the character cycler; exit restores the LEDs to their DAW-driven
// Flash-only helpers — used by BOTH textInputEnter/Exit (track rename)
// and the DAW's PAIRFLASH:1/:0 command (channel-entry mode). Keeps the
// LED behaviour on pads 20 & 21 identical in both cases without dragging
// the letter-cycling state into channel-entry.
void pairFlashStart() {
    textInputFlashActive = true;
    textInputFlashLastMs = 0;   // 0 = let the next flashStep run immediately
    // Anchor the flash to NOW, offset half a period so we start at the
    // triangle's peak — LEDs light at full brightness the instant the mode
    // is entered, instead of possibly starting from dark.
    textInputFlashStartMs = millis() - (FLASH_PERIOD_MS / 2);
    // Paint them immediately rather than waiting for the next 20 ms tick.
    pca9685SetPWM(4, 0);   // wiring: 0 = fully on
    pca9685SetPWM(5, 0);
    // Only pads 20 (LED 4) and 21 (LED 5) should visibly flash. Force the
    // other marker LEDs (6 = record-right, 7 = loop-right) OFF here
    // regardless of whether the DAW has them enabled — the paired-flash
    // pattern would otherwise be misleading.
    ledSet(6, false);
    ledSet(7, false);
    // Broadcast the current LED-flash phase (0 .. FLASH_PERIOD_MS-1) to
    // the DAW so its red-fill pulse can sync with the physical LEDs.
    Serial.print("RENAMESYNC:");
    Serial.println(textInputFlashPhase());
}
void pairFlashStop() {
    textInputFlashActive = false;
    // Restore all four marker LEDs from the mirrored authoritative state.
    ledSet(4, loopLeftLocal);
    ledSet(5, recordLeftLocal);
    ledSet(6, recordRightLocal);
    ledSet(7, loopRightLocal);
}

// state and clears the pending cycle.
void textInputEnter() {
    textInputMode        = true;
    textInputPending     = 0;
    textInputCurrentPad  = -1;
    textInputCursorPos   = (int8_t)strlen(textInputBuffer);
    textInputE1Accum     = 0;
    pairFlashStart();
}
void textInputExit() {
    bool wasActive       = textInputMode;
    textInputMode        = false;
    textInputPending     = 0;
    textInputCurrentPad  = -1;
    pairFlashStop();
    if (wasActive) Serial.println("RENAMEEND");
}

// Text-input mode: called each loop iteration. When active, toggles the
// underscore cursor at the end of textInputBuffer every CURSOR_BLINK_MS.
void cursorStep() {
    if (!textInputMode) return;
    // If a letter is currently being cycled at the cursor position, the
    // fade stepper owns that column — don't blink underneath it.
    if (textInputPending) return;
    uint32_t now = millis();
    if (now - cursorLastToggleMs < CURSOR_BLINK_MS) return;
    cursorLastToggleMs = now;
    cursorVisible = !cursorVisible;
    textInputBuildLine(cursorVisible);
}

// Grayscale fade for the pending letter. Uses the SSD1362's 4-bit levels
// (0..15) on a triangle wave matching the LED-flash period, so the letter
// pulses on/off in sync with the pad-20 / pad-21 LEDs.
static uint32_t textInputFadeLastMs = 0;
static const uint32_t TEXTIN_FADE_TICK_MS = 60;   // ~16 fps redraw of line 2
void textInputFadeStep() {
    if (!textInputMode || textInputPending == 0) return;
    uint32_t now = millis();
    if (now - textInputFadeLastMs < TEXTIN_FADE_TICK_MS) return;
    textInputFadeLastMs = now;
    uint32_t phase = now % FLASH_PERIOD_MS;
    uint32_t half  = FLASH_PERIOD_MS / 2;
    uint32_t b     = (phase < half)
        ? (phase * 15UL) / half
        : ((FLASH_PERIOD_MS - phase) * 15UL) / half;
    uint8_t newBright = (uint8_t)b;
    if (newBright != oledPendingBrightness) {
        oledPendingBrightness = newBright;
        oledLine2Changed = true;   // re-render line 2 at new brightness
    }
}

// Broadcast the current committed buffer + pending letter to the DAW so
// its GUI can render a live-updating preview of what the user is typing.
// Sent as "RENAMEBUF:<committed><pending>" — a single string. On text-input
// exit we send "RENAMEEND" so the DAW can drop the preview UI.
// Delete both loop and record pairs (pad-15 hold gesture). Wipes local
// state, tells the DAW via DELETEPAIR messages, and starts a 1 second
// fade-out on the four pair LEDs (4/5/6/7) as visual confirmation.
void performDeleteAllLoops() {
    // Clear firmware-side mirrors and definition flags.
    loopLeftLocal     = false;
    loopRightLocal    = false;
    recordLeftLocal   = false;
    recordRightLocal  = false;
    loopPairDefined   = false;
    recordPairDefined = false;
    if (flashMode) { flashMode = false; flashModeDeleteArmed = false; ledSet(8, false); }
    // Tell DAW.
    Serial.println("DELETEPAIR:LOOP");
    Serial.println("DELETEPAIR:REC");
    // Kick off the LED fade — the stepper below drives the four pair
    // channels every loop() tick until the fade time elapses.
    deleteAllFadeStartMs = millis();
    // Snap all four to full-on at the start of the fade.
    pca9685SetPWM(4, 0);
    pca9685SetPWM(5, 0);
    pca9685SetPWM(6, 0);
    pca9685SetPWM(7, 0);
}

// Fade LEDs 4/5/6/7 from full-on to off linearly over DELETE_ALL_FADE_MS.
// While the fade is active, incoming DAW LED commands for these channels
// are suppressed (see the LED handler) so the animation doesn't stutter.
void deleteAllFadeStep() {
    if (deleteAllFadeStartMs == 0) return;
    uint32_t elapsed = millis() - deleteAllFadeStartMs;
    if (elapsed >= DELETE_ALL_FADE_MS) {
        // Land at off and stop.
        pca9685SetPWM(4, 4095);
        pca9685SetPWM(5, 4095);
        pca9685SetPWM(6, 4095);
        pca9685SetPWM(7, 4095);
        deleteAllFadeStartMs = 0;
        return;
    }
    // Linear fade: brightness = 1 - elapsed/FADE. PWM inverted (0=on).
    uint32_t brightness = (uint32_t)((uint64_t)(DELETE_ALL_FADE_MS - elapsed) * 4095 / DELETE_ALL_FADE_MS);
    uint16_t pwm = (uint16_t)(4095 - brightness);
    pca9685SetPWM(4, pwm);
    pca9685SetPWM(5, pwm);
    pca9685SetPWM(6, pwm);
    pca9685SetPWM(7, pwm);
}

// Hard reset. Called when pad 18 has been held for RESET_HOLD_MS. Lights
// all 9 LEDs solid, shows "RESET" on the OLED, and asks the DAW to
// re-push its authoritative state. After RESET_SHOW_MS elapses,
// resetShowFinish() drops the LEDs to whatever mirrors say (which are
// blank until the DAW's next updateController tick paints them back).
void performReset() {
    // Exit anything modal that's local to the firmware.
    if (flashMode) { flashMode = false; flashModeDeleteArmed = false; }
    if (textInputMode) textInputExit();
    // Clear held-state trackers. LED mirrors reset too — they'll be
    // repainted by the LED:N:ON/OFF and PAIRDEF:*:* messages the DAW
    // sends after we've told it to RESYNC.
    modifierHeld = false;
    pad3Held = false;
    pad14Held = false;
    pad15Held = false;
    pad19Held = false;
    pad19UsedAsModifier = false;
    playLedLocal      = false;
    loopLeftLocal     = false;
    loopRightLocal    = false;
    recordLeftLocal   = false;
    recordRightLocal  = false;
    muteLedLocal      = false;
    soloLedLocal      = false;
    armLedLocal       = false;
    loopPairDefined   = false;
    recordPairDefined = false;
    // All LEDs solid on. Hardware only — mirrors stay at their reset
    // value so nothing accidentally re-lights them via ledSet().
    for (int ch = 0; ch < 9; ch++) pca9685SetPWM(ch, 0);
    // OLED "RESET" held until pad-18 release. Very-far-future hard lock
    // so DAW OLED pushes are deferred throughout the hold.
    snprintf(oledLine1, sizeof(oledLine1), "RESET");
    snprintf(oledLine2, sizeof(oledLine2), " ");
    oledLine1Changed = true;
    oledLine2Changed = true;
    oledLockUntilMs = 0xFFFFFFFFu;   // effectively forever; cleared on release
    oledLockIsSoft  = false;
    resetShowActive = true;
    // Tell the DAW to invalidate its LED/OLED caches and re-push
    // everything so its mirrors here fill in during the hold.
    Serial.println("RESYNC");
}

// Called on pad 18 release when resetShowActive is set — repaints the
// LEDs from the mirrors the DAW has been filling in during the hold and
// releases the OLED lock so DAW pushes can update it again.
void resetShowFinish() {
    if (!resetShowActive) return;
    resetShowActive = false;
    ledSet(3, playLedLocal);
    ledSet(4, loopLeftLocal);
    ledSet(5, recordLeftLocal);
    ledSet(6, recordRightLocal);
    ledSet(7, loopRightLocal);
    pca9685SetPWM(8, 4095);   // pan-modifier LED is momentary; end off
    pca9685SetPWM(0, 4095);   // LEDs 0-2 unused, ensure off
    pca9685SetPWM(1, 4095);
    pca9685SetPWM(2, 4095);
    oledLockUntilMs = 0;
    oledLockIsSoft  = false;
    // Blank OLED lines — DAW's next OLED push (STOPPED / PLAYING / etc.)
    // will paint the real state within a frame or two.
    snprintf(oledLine1, sizeof(oledLine1), " ");
    snprintf(oledLine2, sizeof(oledLine2), " ");
    oledLine1Changed = true;
    oledLine2Changed = true;
}

// Kick off a 3-blink acknowledgement on the specified pair (1 = loop
// LEDs 4+7, 2 = record LEDs 5+6). Also pre-clears the corresponding
// local mirrors so no stray "on" flashes through after the sequence.
void startMarkerDeleteFlash(uint8_t pair) {
    markerDeleteFlashPair   = pair;
    markerDeleteFlashCount  = MARKER_DELETE_BLINK_TICKS;
    markerDeleteFlashLastMs = 0;
    markerDeleteFlashOn     = false;
    if (pair == 1) { loopLeftLocal   = false; loopRightLocal   = false; }
    if (pair == 2) { recordLeftLocal = false; recordRightLocal = false; }
}

void markerDeleteFlashStep() {
    if (markerDeleteFlashPair == 0 || markerDeleteFlashCount == 0) return;
    uint32_t nowMs = millis();
    if (nowMs - markerDeleteFlashLastMs < MARKER_DELETE_BLINK_MS) return;
    markerDeleteFlashLastMs = nowMs;
    markerDeleteFlashOn = !markerDeleteFlashOn;
    uint16_t pwm = markerDeleteFlashOn ? 0 : 4095;
    if (markerDeleteFlashPair == 1) {
        pca9685SetPWM(4, pwm);
        pca9685SetPWM(7, pwm);
    } else {
        pca9685SetPWM(5, pwm);
        pca9685SetPWM(6, pwm);
    }
    markerDeleteFlashCount--;
    if (markerDeleteFlashCount == 0) {
        // Land in the OFF state and hand back to the DAW's LED control.
        if (markerDeleteFlashPair == 1) { pca9685SetPWM(4, 4095); pca9685SetPWM(7, 4095); }
        else                            { pca9685SetPWM(5, 4095); pca9685SetPWM(6, 4095); }
        markerDeleteFlashPair = 0;
    }
}

// Called on pad 19 release when the tap wasn't consumed by a delete
// combo. Predictively toggles the play LED locally so the user gets
// instant feedback on tap, then notifies the DAW. DAW's cache-diff
// next tick sends LED:3:X and re-syncs the mirror — if it turns out
// play() no-op'd (no audio loaded, etc.) the DAW's authoritative send
// undoes our predictive toggle within ~1 frame.
void firePad19Deferred() {
    // Suppress the predictive play-LED toggle while marker-scroll mode
    // owns the LED strip — LED 3 must stay dark. The DAW ignores pad-19
    // events during scroll mode too, so the transport doesn't change.
    if (!pad14Held && !muteFlashActive) {
        playLedLocal = !playLedLocal;
        ledSet(3, playLedLocal);
    }
    Serial.println("TOUCH:19");
    Serial.println("RELEASE:19");
}

void textInputBroadcast() {
    // Format: RENAMEBUF:<cursorPos>:<display_text>
    // display_text = committed buffer, with the pending letter substituted
    // at cursorPos if the user is mid-cycle. DAW renders its own blinking
    // cursor overlay on top at cursorPos.
    Serial.print("RENAMEBUF:");
    Serial.print((int)textInputCursorPos);
    Serial.print(':');
    int len = (int)strlen(textInputBuffer);
    int cur = textInputCursorPos;
    for (int i = 0; i < len; i++) {
        if (i == cur && textInputPending) Serial.write((char)textInputPending);
        else                              Serial.write(textInputBuffer[i]);
    }
    if (cur >= len && textInputPending) Serial.write((char)textInputPending);
    Serial.println();
}

// Render text-input line immediately (for entering the mode or after a
// buffer change), rather than waiting for the next cursor blink tick.
// Also broadcasts the live buffer to the DAW.
void textInputRender() {
    cursorVisible      = true;
    cursorLastToggleMs = millis();
    textInputBuildLine(true);
    textInputBroadcast();
}

void exitFlashMode() {
    flashMode = false;
    Serial.println("DEBUG:FLASH:OFF");
    // Clear the in-mode delete-arm state and its LED.
    if (flashModeDeleteArmed) {
        flashModeDeleteArmed = false;
        ledSet(8, false);
    }
    // Restore all flash-affected LEDs to whatever local mirrors say they
    // should be. The DAW will re-assert via LED:N commands next tick if any
    // of these disagree with its authoritative state.
    ledSet(3, playLedLocal);
    ledSet(4, loopLeftLocal);
    ledSet(5, recordLeftLocal);
    ledSet(6, recordRightLocal);
    ledSet(7, loopRightLocal);
    // LED 8 was flashing in-sync with the mode indicator (or solid on if
    // delete-arm was engaged). Force off now — pan-mod press/release will
    // reassert it if the user actually holds pad 24 later.
    ledSet(8, false);
    // No "EXIT" banner — DAW's playback-state push (STOPPED / PLAYING)
    // is the persistent display; nothing transient to announce.
}

void pca9685SetPWM(uint8_t channel, uint16_t value) {
    // value: 0-4095.  With our open-drain wiring (LED anodes on a rail
    // higher than PCA9685 VCC), the PWM "HIGH" phase = high-Z = LED dark,
    // and the "LOW" phase = sinking = LED lit. So brightness scales as
    // (4096 - OFF) / 4096 — value=0 is fully lit, value=4095 is nearly
    // dark. The problem is "nearly": at OFF=4095 the LED still gets one
    // count of sinking per cycle, which shows up as a faint permanent
    // glow. The chip's LED_FULL_ON flag (bit 4 of LEDn_ON_H) forces the
    // output permanently HIGH — genuinely off with no PWM glitch. We use
    // it for the two boundary cases and normal PWM in between (fades).
    uint8_t reg = 0x06 + 4 * channel;
    uint16_t onValue, offValue;
    if (value >= 4095) {
        // Full OFF — permanently HIGH-Z, zero LED current.
        onValue  = 0x1000;    // FULL_ON flag
        offValue = 0x0000;
    } else if (value == 0) {
        // Full ON — permanently LOW/sinking. FULL_OFF flag guarantees
        // this across chip revisions (the ON=OFF=0 special case is not
        // universally documented as "always LOW").
        onValue  = 0x0000;
        offValue = 0x1000;    // FULL_OFF flag
    } else {
        // Intermediate PWM (fades). Standard ON=0, OFF=value.
        onValue  = 0x0000;
        offValue = value;
    }
    Wire.beginTransmission(PCA9685_ADDR);
    Wire.write(reg);
    Wire.write(onValue  & 0xFF);
    Wire.write(onValue  >> 8);
    Wire.write(offValue & 0xFF);
    Wire.write(offValue >> 8);
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

    // CAP1188 init (one large pad on C1, reported as pad 36)
    if (cap1188Init()) {
        cap1188Found = true;
        Serial.print("CAP1188 (0x");
        Serial.print(CAP1188_ADDR, HEX);
        Serial.print(") found! big pad = pad ");
        Serial.println(CAP1188_PAD_NUMBER);
    } else {
        Serial.println("CAP1188 NOT found");
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
            // Any DAW command counts as a heartbeat.
            if (serialInputBuffer.length() > 0) {
                lastDawHeartbeatMs = millis();
            }
            // HB — pure heartbeat, no other effect.
            if (serialInputBuffer == "HB") {
                serialInputBuffer = "";
                continue;
            }
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
            // --- CAP1188 tuning, so gain/threshold can be found without a
            // reflash for every trial. CAPDBG streams the raw signed delta
            // on C1; press the pad while it runs and the numbers show how
            // much headroom the current threshold actually has.
            else if (serialInputBuffer == "CAPDBG") {
                if (!cap1188Found) {
                    Serial.println("CAPDBG: CAP1188 not present");
                } else {
                    Serial.print("CAPDBG gain=");   Serial.print(cap1188GainIndex);
                    Serial.print(" thresh=");       Serial.println(cap1188Threshold);
                    for (int i = 0; i < 60; i++) {
                        int16_t d = (int8_t)cap1188ReadRegister(CAP1188_REG_DELTA_1);
                        uint8_t s = cap1188ReadRegister(CAP1188_REG_STATUS);
                        Serial.print("CAPDELTA "); Serial.print(d);
                        Serial.print(" status=");  Serial.println(s & 0x01);
                        delay(100);
                    }
                    Serial.println("CAPDBG done");
                }
            }
            else if (serialInputBuffer.startsWith("CAPGAIN:")) {
                int g = serialInputBuffer.substring(8).toInt();
                if (g < 0) g = 0; if (g > 7) g = 7;
                cap1188GainIndex = (uint8_t)g;
                cap1188WriteRegister(CAP1188_REG_SENSITIVITY,
                                     (uint8_t)((cap1188GainIndex << 4) | 0x0F));
                cap1188WriteRegister(CAP1188_REG_CAL_ACTIVATE, 0x01);
                Serial.print("CAPGAIN set to index "); Serial.println(cap1188GainIndex);
            }
            else if (serialInputBuffer.startsWith("CAPTH:")) {
                int t = serialInputBuffer.substring(6).toInt();
                if (t < 1)   t = 1; if (t > 127) t = 127;
                cap1188Threshold = (uint8_t)t;
                cap1188WriteRegister(CAP1188_REG_THRESHOLD_1, cap1188Threshold);
                Serial.print("CAPTH set to "); Serial.println(cap1188Threshold);
            }
            // Raw register access, for working out the chip's timing
            // config empirically instead of trusting a datasheet we can't
            // read cleanly. CAPRD:<hexreg> / CAPREG:<hexreg>:<hexval>.
            else if (serialInputBuffer.startsWith("CAPRD:")) {
                long r = strtol(serialInputBuffer.substring(6).c_str(), nullptr, 16);
                uint8_t v = cap1188ReadRegister((uint8_t)r);
                Serial.print("CAPRD 0x"); Serial.print((int)r, HEX);
                Serial.print(" = 0x");    Serial.print(v, HEX);
                Serial.print(" (");       Serial.print(v);      Serial.println(")");
            }
            else if (serialInputBuffer.startsWith("CAPREG:")) {
                int colon = serialInputBuffer.indexOf(':', 7);
                if (colon > 0) {
                    long r = strtol(serialInputBuffer.substring(7, colon).c_str(), nullptr, 16);
                    long v = strtol(serialInputBuffer.substring(colon + 1).c_str(), nullptr, 16);
                    cap1188WriteRegister((uint8_t)r, (uint8_t)v);
                    uint8_t rb = cap1188ReadRegister((uint8_t)r);
                    Serial.print("CAPREG 0x"); Serial.print((int)r, HEX);
                    Serial.print(" <- 0x");    Serial.print((int)v, HEX);
                    Serial.print(" readback 0x"); Serial.println(rb, HEX);
                }
            }
            else if (serialInputBuffer == "LOOPSTAT") {
                Serial.print("LOOPSTAT count="); Serial.print(loopCount);
                Serial.print(" meanUs=");
                Serial.print(loopCount ? (uint32_t)(loopSumUs / loopCount) : 0);
                Serial.print(" maxUs="); Serial.println(loopMaxUs);
                loopMaxUs = 0; loopSumUs = 0; loopCount = 0;
            }
            else if (serialInputBuffer == "CAPCAL") {
                cap1188WriteRegister(CAP1188_REG_CAL_ACTIVATE, 0x01);
                Serial.println("CAPCAL: recalibrated C1");
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
            // PAIRDEF:LOOP:1 / :0 and PAIRDEF:REC:1 / :0 tell the firmware
            // whether each marker pair currently exists (any position ever
            // set). Used by flashStep() to render the "defined but off"
            // state as a flashing LED in clear-markers mode.
            else if (serialInputBuffer == "PAIRDEF:LOOP:1") { loopPairDefined   = true;  }
            else if (serialInputBuffer == "PAIRDEF:LOOP:0") { loopPairDefined   = false; }
            else if (serialInputBuffer == "PAIRDEF:REC:1")  { recordPairDefined = true;  }
            else if (serialInputBuffer == "PAIRDEF:REC:0")  { recordPairDefined = false; }
            else if (serialInputBuffer == "SETMODE:DIAG") {
                // Skip while the reset show is up — setDisplayMode would
                // overwrite line 1 (with "DISPLAY DIAGNOSTIC") and clear
                // our lock, letting DISPFORCE:STOPPED erase "RESET".
                if (!resetShowActive) setDisplayMode(false);
            }
            else if (serialInputBuffer == "SETMODE:DESC") {
                if (!resetShowActive) setDisplayMode(true);
            }
            // TEXTIN:OFF   — leave text-input mode (cursor stops blinking).
            // TEXTIN:text  — enter text-input mode on line 2 with initial
            //                text (may be empty). A "_" cursor at the end
            //                flashes every CURSOR_BLINK_MS.
            else if (serialInputBuffer == "TEXTIN:OFF") {
                textInputExit();
            }
            // CANCELMODES — hard exit from every firmware-side modal UI
            // (text-input for track naming, flashMode for loop-edit,
            // mute-scroll flash). Used by the DAW's undo path so a
            // rewound action can't leave the controller with stale
            // flashing LEDs.
            else if (serialInputBuffer == "CANCELMODES") {
                if (textInputMode) textInputExit();
                if (flashMode)     exitFlashMode();
                if (muteFlashActive) {
                    muteFlashActive = false;
                    pca9685SetPWM(0, muteLedLocal ? ledBrightness[0] : 4095);
                }
            }
            // MUTEFLASH:1 / :0 — DAW toggles a triangle-wave flash on LED 0
            // to indicate marker-scroll mode is active.
            else if (serialInputBuffer == "MUTEFLASH:1") {
                muteFlashActive = true;
                muteFlashLastMs = 0;
            }
            else if (serialInputBuffer == "MUTEFLASH:0") {
                muteFlashActive = false;
                pca9685SetPWM(0, muteLedLocal ? ledBrightness[0] : 4095);
            }
            // MUTEIND:1 / :0 — DAW toggles the far-left OSC-mute indicator
            // bar on the OLED. Just latches state; next idle oledStep()
            // renders it.
            else if (serialInputBuffer == "MUTEIND:1") {
                muteIndOn    = true;
                muteIndDirty = true;
            }
            else if (serialInputBuffer == "MUTEIND:0") {
                muteIndOn    = false;
                muteIndDirty = true;
            }
            else if (serialInputBuffer.startsWith("TEXTIN:") && displayMode) {
                String text = serialInputBuffer.substring(7);
                strncpy(textInputBuffer, text.c_str(), sizeof(textInputBuffer) - 1);
                textInputBuffer[sizeof(textInputBuffer) - 1] = '\0';
                textInputEnter();
                textInputRender();
            }
            // PAIRFLASH:1 / :0 — toggle the same pad-20/21 LED flash the
            // rename flow uses, without any letter-cycling state. Sent by
            // the DAW when channel-entry mode enters/exits so the user gets
            // the same visual cue that they're in a "typing" mode.
            else if (serialInputBuffer == "PAIRFLASH:1") {
                pairFlashStart();
            }
            else if (serialInputBuffer == "PAIRFLASH:0") {
                pairFlashStop();
            }
            // EXCLUSIVE:1 / :0 — DAW claims exclusive ownership of every
            // pad. Firmware turns into a dumb TOUCH/RELEASE forwarder:
            // no predictive LED toggles, no gesture-to-command translation
            // (LOOPPLAY, DELETEPAIR, RETURNONSTOP), no local mode dispatch.
            // Used by DAW modes that reinterpret pads (channel-entry).
            else if (serialInputBuffer == "EXCLUSIVE:1") {
                dawExclusiveMode = true;
            }
            else if (serialInputBuffer == "EXCLUSIVE:0") {
                dawExclusiveMode = false;
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
                        // Any line-2 write cancels text-input mode (otherwise
                        // the cursor blink would clobber the new message).
                        if (textInputMode) textInputExit();
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
                        // Any line-2 write cancels text-input mode (otherwise
                        // the cursor blink would clobber the new message).
                        if (textInputMode) textInputExit();
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
                        // Any line-2 write cancels text-input mode (otherwise
                        // the cursor blink would clobber the new message).
                        if (textInputMode) textInputExit();
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
                            // Always update the local mirror so state stays
                            // in sync — but suppress the physical write for
                            // marker LEDs while text-input mode owns them.
                            if (channel == 0) muteLedLocal     = on;
                            if (channel == 1) soloLedLocal     = on;
                            if (channel == 2) armLedLocal      = on;
                            if (channel == 3) playLedLocal     = on;
                            if (channel == 4) loopLeftLocal    = on;
                            if (channel == 5) recordLeftLocal  = on;
                            if (channel == 6) recordRightLocal = on;
                            if (channel == 7) loopRightLocal   = on;
                            bool inDeleteAllFade = (deleteAllFadeStartMs != 0 &&
                                                    (channel == 4 || channel == 5 ||
                                                     channel == 6 || channel == 7));
                            bool suppressWrite =
                                (textInputMode &&
                                 (channel == 4 || channel == 5 ||
                                  channel == 6 || channel == 7))
                                || resetShowActive
                                || inDeleteAllFade;
                            if (!suppressWrite) {
                                // Use ledSet so identify-owned channels are
                                // respected and the per-channel brightness is
                                // applied consistently with every other write.
                                ledSet((uint8_t)channel, on);
                            }
                            // channel 8: no state — pad 24 is a momentary modifier
                            // and the firmware drives LED 8 directly on press/release.
                        }
                    }
                }
            }
            // LEDBRT:channel:value  (value 0-4095, 0 = brightest, 4095 = darkest)
            // — sets a per-channel PWM value used whenever that LED is ON.
            //   If the LED is currently ON via its mirror, re-apply immediately
            //   so brightness slider drags respond in real time.
            else if (serialInputBuffer.startsWith("LEDBRT:")) {
                int firstColon  = 6;
                int secondColon = serialInputBuffer.indexOf(':', firstColon + 1);
                if (secondColon > 0) {
                    int channel = serialInputBuffer.substring(firstColon + 1, secondColon).toInt();
                    int value   = serialInputBuffer.substring(secondColon + 1).toInt();
                    if (pca9685Found && channel >= 0 && channel < 9) {
                        if (value < 0)    value = 0;
                        if (value > 4095) value = 4095;
                        ledBrightness[channel] = (uint16_t)value;
                        // Re-apply if this LED is currently lit via its mirror
                        // (excluding channel 8 which has no mirror — pad24 owns it).
                        bool mirrorOn = false;
                        switch (channel) {
                            case 0: mirrorOn = muteLedLocal;     break;
                            case 1: mirrorOn = soloLedLocal;     break;
                            case 2: mirrorOn = armLedLocal;      break;
                            case 3: mirrorOn = playLedLocal;     break;
                            case 4: mirrorOn = loopLeftLocal;    break;
                            case 5: mirrorOn = recordLeftLocal;  break;
                            case 6: mirrorOn = recordRightLocal; break;
                            case 7: mirrorOn = loopRightLocal;   break;
                            default: break;
                        }
                        // Re-apply live if the LED is currently lit — via its
                        // mirror, its force-on state, or its identify state.
                        if (ledForceOn[channel] ||
                            (mirrorOn && !ledIdentify[channel])) {
                            pca9685SetPWM((uint8_t)channel, ledBrightness[channel]);
                        }
                    }
                }
            }
            // LEDFORCEON:channel:1 / 0  or  LEDFORCEOFF:channel:1 / 0 —
            // hold the LED steady on / off / release. Used by the DAW's
            // "All ON" / "All OFF" test buttons. Steady state, no flash.
            // Setting one clears the other (mutual exclusion).
            else if (serialInputBuffer.startsWith("LEDFORCEON:") ||
                     serialInputBuffer.startsWith("LEDFORCEOFF:")) {
                bool  isOn      = serialInputBuffer.startsWith("LEDFORCEON:");
                int   firstColon  = isOn ? 10 : 11;
                int   secondColon = serialInputBuffer.indexOf(':', firstColon + 1);
                if (secondColon > 0) {
                    int channel = serialInputBuffer.substring(firstColon + 1, secondColon).toInt();
                    int active  = serialInputBuffer.substring(secondColon + 1).toInt();
                    if (pca9685Found && channel >= 0 && channel < 9) {
                        if (active) {
                            if (isOn) {
                                ledForceOn[channel]  = true;
                                ledForceOff[channel] = false;
                                pca9685SetPWM((uint8_t)channel, ledBrightness[channel]);
                            } else {
                                ledForceOff[channel] = true;
                                ledForceOn[channel]  = false;
                                pca9685SetPWM((uint8_t)channel, 4095);
                            }
                        } else {
                            // Release only the corresponding force flag.
                            if (isOn) ledForceOn[channel]  = false;
                            else      ledForceOff[channel] = false;
                            // If the other force is still active, don't touch
                            // the LED — it stays under the other's control.
                            if (ledForceOn[channel] || ledForceOff[channel]) {
                                // Nothing to do; other force flag owns the LED.
                            } else {
                                // Restore via mirror. Channel 8 has no mirror.
                                bool mirrorOn = false;
                                switch (channel) {
                                    case 0: mirrorOn = muteLedLocal;     break;
                                    case 1: mirrorOn = soloLedLocal;     break;
                                    case 2: mirrorOn = armLedLocal;      break;
                                    case 3: mirrorOn = playLedLocal;     break;
                                    case 4: mirrorOn = loopLeftLocal;    break;
                                    case 5: mirrorOn = recordLeftLocal;  break;
                                    case 6: mirrorOn = recordRightLocal; break;
                                    case 7: mirrorOn = loopRightLocal;   break;
                                    default: break;
                                }
                                pca9685SetPWM((uint8_t)channel,
                                              mirrorOn ? ledBrightness[channel] : 4095);
                            }
                        }
                    }
                }
            }
            // LEDID:channel:1  or  LEDID:channel:0  — begin / end identify.
            // While identify is on, the loop() ticker flashes that channel at
            // ~2 Hz. On end, we restore the normal mirror state.
            else if (serialInputBuffer.startsWith("LEDID:")) {
                int firstColon  = 5;
                int secondColon = serialInputBuffer.indexOf(':', firstColon + 1);
                if (secondColon > 0) {
                    int channel = serialInputBuffer.substring(firstColon + 1, secondColon).toInt();
                    int on      = serialInputBuffer.substring(secondColon + 1).toInt();
                    if (pca9685Found && channel >= 0 && channel < 9) {
                        if (on) {
                            ledIdentify[channel] = true;
                            // Force phase to on so the LED lights immediately.
                            ledIdentifyPhase   = true;
                            ledIdentifyLastMs  = millis();
                            pca9685SetPWM((uint8_t)channel, ledBrightness[channel]);
                        } else {
                            ledIdentify[channel] = false;
                            // Restore via mirror. Channel 8 has no mirror — just
                            // set to off; pad 24 will re-light it if held.
                            bool mirrorOn = false;
                            switch (channel) {
                                case 0: mirrorOn = muteLedLocal;     break;
                                case 1: mirrorOn = soloLedLocal;     break;
                                case 2: mirrorOn = armLedLocal;      break;
                                case 3: mirrorOn = playLedLocal;     break;
                                case 4: mirrorOn = loopLeftLocal;    break;
                                case 5: mirrorOn = recordLeftLocal;  break;
                                case 6: mirrorOn = recordRightLocal; break;
                                case 7: mirrorOn = loopRightLocal;   break;
                                default: break;
                            }
                            pca9685SetPWM((uint8_t)channel,
                                          mirrorOn ? ledBrightness[channel] : 4095);
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

// ==================== TOUCH DISPATCH HELPERS ====================
// Pure extraction of what used to be one giant nested if/else inside
// loop(). Behaviour is identical — same suppressTouchSend rules, same
// per-pad side effects — the shape is just split into named functions
// so new modes and new pads can be added without swelling loop().

// Text-input (naming) mode press handler. Consumes the letter pads plus
// pads 15 / 20 / 21 WHEN text-input mode is active; all other pads (or
// any pad when text-input is off) fall out to the flashMode / normal
// dispatcher. Always suppresses the raw TOUCH send when it consumes.
// Returns true when the pad was consumed here.
bool handleTouchTextInputMode(int touchNum) {
    if (!textInputMode) return false;
    int letterIdx = letterPadIndex(touchNum);
    if (letterIdx < 0 &&
        touchNum != 20 && touchNum != 21 && touchNum != 15) {
        return false;
    }
    if (letterIdx >= 0) {
        // Letter-pad cycle. Fresh pad starts on its first letter; the
        // same pad again advances to the next in its 2-3 letter set.
        const LetterPad& lp = LETTER_PADS[letterIdx];
        int nLetters = (int)strlen(lp.letters);
        if (textInputCurrentPad == (int8_t)lp.pad) {
            textInputCurrentIdx = (textInputCurrentIdx + 1) % nLetters;
        } else {
            textInputCurrentPad = (int8_t)lp.pad;
            textInputCurrentIdx = 0;
        }
        textInputPending = lp.letters[textInputCurrentIdx];
        textInputRender();
    } else if (touchNum == 20) {
        // Commit pending (or a space) at cursor position; advance cursor.
        // In the middle of the buffer this REPLACES; at the end it
        // appends and grows the buffer.
        int len = (int)strlen(textInputBuffer);
        int cap = (int)sizeof(textInputBuffer) - 1;
        char c = (textInputPending != 0) ? textInputPending : ' ';
        if (textInputCursorPos < len) {
            textInputBuffer[textInputCursorPos] = c;
            textInputCursorPos++;
        } else if (len < cap) {
            textInputBuffer[len]     = c;
            textInputBuffer[len + 1] = '\0';
            textInputCursorPos = (int8_t)(len + 1);
        }
        textInputPending    = 0;
        textInputCurrentPad = -1;
        textInputRender();
    } else if (touchNum == 21) {
        // Finalise: auto-commit pending at cursor, send NAME to DAW, exit.
        if (textInputPending != 0) {
            int len = (int)strlen(textInputBuffer);
            int cap = (int)sizeof(textInputBuffer) - 1;
            if (textInputCursorPos < len) {
                textInputBuffer[textInputCursorPos] = textInputPending;
            } else if (len < cap) {
                textInputBuffer[len]     = textInputPending;
                textInputBuffer[len + 1] = '\0';
            }
            textInputPending    = 0;
            textInputCurrentPad = -1;
        }
        Serial.print("NAME:");
        Serial.println(textInputBuffer);
        textInputExit();
    } else if (touchNum == 15) {
        // Clear letter at cursor — blanks the slot so the flashing
        // cursor is the only thing visible there.
        int len = (int)strlen(textInputBuffer);
        if (textInputCursorPos < len) {
            textInputBuffer[textInputCursorPos] = ' ';
        }
        textInputPending    = 0;
        textInputCurrentPad = -1;
        textInputRender();
    }
    return true;
}

// Clear/restore-markers mode ("flashMode") press handler.
// Returns true when the raw TOUCH send should be suppressed (locally-
// consumed presses like pad 24's arm toggle or the pair-delete combos).
bool handleTouchFlashMode(int touchNum) {
    if (touchNum == 15) {
        // Exit flashMode. Natural TOUCH:15 below toggles DAW clearMode.
        exitFlashMode();
        return false;
    }
    if (touchNum == 24) {
        // Toggle delete-arm; only allowed if something's actually there
        // to delete. Local-only — DAW never sees the tap.
        if (flashModeDeleteArmed) {
            flashModeDeleteArmed = false;
            ledSet(8, false);
        } else if (loopPairDefined || recordPairDefined) {
            flashModeDeleteArmed = true;
            ledSet(8, true);
        }
        return true;
    }
    if (flashModeDeleteArmed &&
        (touchNum == 20 || touchNum == 23) && loopPairDefined) {
        // Delete the loop pair. Local mirrors updated so LED animation
        // is snappy; DAW gets DELETEPAIR:LOOP. Suppress TOUCH:N —
        // otherwise the clear-mode toggle handler would re-enable it.
        loopLeftLocal   = false;
        loopRightLocal  = false;
        loopPairDefined = false;
        Serial.println("DELETEPAIR:LOOP");
        if (!loopPairDefined && !recordPairDefined) {
            flashModeDeleteArmed = false;
            ledSet(8, false);
        }
        return true;
    }
    if (flashModeDeleteArmed &&
        (touchNum == 21 || touchNum == 22) && recordPairDefined) {
        recordLeftLocal   = false;
        recordRightLocal  = false;
        recordPairDefined = false;
        Serial.println("DELETEPAIR:REC");
        if (!loopPairDefined && !recordPairDefined) {
            flashModeDeleteArmed = false;
            ledSet(8, false);
        }
        return true;
    }
    // Other pads: no local action; TOUCH:N goes to DAW to toggle pair
    // enable / trigger jump / etc. LED updates come back via LED:N:X.
    return false;
}

// Normal-mode press handler (neither text-input nor flashMode).
// Returns true when the raw TOUCH send should be suppressed.
bool handleTouchNormalMode(int touchNum) {
    if (touchNum == 3) {
        pad3Held = true;
        return false;
    }
    if (touchNum == 18) {
        // Start the 3-second reset timer; fire is decided in loop().
        pad18PressMs    = millis();
        pad18ResetFired = false;
        return false;
    }
    if (touchNum == 14) {
        // 19 + 14 = return-to-start ON. Otherwise just track held state.
        if (pad19Held) {
            Serial.println("RETURNONSTOP:1");
            pad19UsedAsModifier = true;
            comboFired          = true;
            pad14Held           = true;
            return true;
        }
        pad14Held = true;
        return false;
    }
    if (touchNum == 15) {
        // 19 + 15 = return-to-start OFF ("stop in place"). Otherwise
        // record press time — short tap toggles loop-edit, hold = delete-all.
        if (pad19Held) {
            Serial.println("RETURNONSTOP:0");
            pad19UsedAsModifier = true;
            comboFired          = true;
            pad15Held           = true;
            return true;
        }
        pad15PressMs        = millis();
        pad15LongPressFired = false;
        return true;
    }
    if (touchNum == 23 && pad3Held) {
        // Pad 3 + pad 23 = display-mode switch. Refuse switching to
        // descriptive if the DAW isn't sending heartbeats — descriptive
        // is useless without OLED text pushes.
        bool switchingToDesc = !displayMode;
        bool dawConnected = (millis() - lastDawHeartbeatMs)
                             < DAW_HEARTBEAT_TIMEOUT_MS;
        if (switchingToDesc && !dawConnected) {
            snprintf(oledLine2, sizeof(oledLine2), "DAW NOT CONNECTED");
            oledLine2Changed = true;
            oledLockUntilMs  = millis() + 3000;
            oledLockIsSoft   = false;
        } else {
            setDisplayMode(!displayMode);
        }
        // Consumed by the mode switch — forwarding would fire a spurious
        // loop-right jump on the freshly-active descriptive side.
        return true;
    }
    if (touchNum == 26) {
        modifierHeld = true;
        return false;
    }
    if (touchNum == 24) {
        // Momentary pan-modifier: LED 8 on while held. Suppressed in
        // marker-scroll mode so the LED strip stays dark apart from the
        // mute flash.
        if (!muteFlashActive) ledSet(8, true);
        return false;
    }
    if (touchNum == 19) {
        // Defer press action to release so holding 19 as a delete-
        // modifier doesn't fire play/stop. firePad19Deferred() replays.
        pad19Held           = true;
        pad19UsedAsModifier = false;
        return true;
    }
    if ((touchNum == 20 || touchNum == 23) && pad19Held) {
        pad19UsedAsModifier = true;
        startMarkerDeleteFlash(1);
        Serial.println("DELETEPAIR:LOOP");
        return true;
    }
    if ((touchNum == 21 || touchNum == 22) && pad19Held) {
        pad19UsedAsModifier = true;
        startMarkerDeleteFlash(2);
        Serial.println("DELETEPAIR:REC");
        return true;
    }
    // Pads 20-23 without pad 19 held: no local action.
    return false;
}

// Top-level press dispatcher. Handles cross-cutting bookkeeping (soft
// lock release, disconnect-to-diag flip), calls whichever mode handler
// owns the press, then either forwards TOUCH:N to the DAW or not, and
// finally writes the diagnostic-mode auto-status line.
void handleTouchPressed(int touchNum) {
    if (oledLockIsSoft && millis() < oledLockUntilMs) {
        oledLockUntilMs = 0;
        oledLockIsSoft  = false;
    }
    if (pendingSwitchToDiagOnActivity) {
        pendingSwitchToDiagOnActivity = false;
        setDisplayMode(false);
    }
    // DAW-exclusive short-circuit: forward the raw TOUCH and skip every
    // local behaviour. See comment at dawExclusiveMode declaration.
    // Still emit the diagnostic "BUTTON N ON" OLED update — that's a
    // pure display echo, not a local pad behaviour, and losing it makes
    // the diagnostic mode useless.
    if (dawExclusiveMode) {
        Serial.print("TOUCH:");
        Serial.println(touchNum);
        if (!displayMode && millis() >= oledLockUntilMs) {
            snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d ON", touchNum);
            oledLine2Changed = true;
        }
        return;
    }
    // Combo lock: after 19+14 or 19+15 has fired, every other press is
    // suppressed until both combo pads are physically released. Prevents
    // stray taps while the user still has both fingers down.
    if (comboFired) return;
    bool suppressTouchSend = false;
    if (handleTouchTextInputMode(touchNum)) {
        suppressTouchSend = true;
    } else if (flashMode) {
        suppressTouchSend = handleTouchFlashMode(touchNum);
    } else {
        suppressTouchSend = handleTouchNormalMode(touchNum);
    }
    if (!suppressTouchSend) {
        Serial.print("TOUCH:");
        Serial.println(touchNum);
    }
    if (!displayMode && millis() >= oledLockUntilMs) {
        snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d ON", touchNum);
        oledLine2Changed = true;
    }
}

// Release dispatcher. Resets per-pad held-state trackers, closes long-
// press windows, replays the deferred pad-19 action, and forwards a
// RELEASE:N to the DAW for pads that don't need special release handling.
void handleTouchReleased(int touchNum) {
    // DAW-exclusive short-circuit: forward the raw RELEASE and skip every
    // local behaviour. Symmetric to handleTouchPressed — keep the
    // diagnostic OLED echo so `!displayMode` still shows the taps.
    if (dawExclusiveMode) {
        Serial.print("RELEASE:");
        Serial.println(touchNum);
        if (!displayMode && millis() >= oledLockUntilMs) {
            snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d OFF", touchNum);
            oledLine2Changed = true;
        }
        return;
    }
    if (touchNum == 26) modifierHeld = false;
    // Pad 24 release: only turn off LED 8 outside flash mode. Inside
    // flashMode LED 8 tracks the delete-arm toggle, not the physical hold.
    if (touchNum == 24 && !flashMode) ledSet(8, false);
    if (touchNum == 3)  pad3Held  = false;
    if (touchNum == 14) pad14Held = false;
    if (touchNum == 15) {
        pad15Held = false;
        // Short tap = toggle loop-edit. Long-press already fired the
        // delete-all gesture (clears pad15PressMs); skip in that case.
        // (When DAW is in exclusive mode the dispatcher up top has
        // already returned before reaching this handler, so pad 15 just
        // forwards RELEASE:15 without touching any of this.)
        if (!pad15LongPressFired && pad15PressMs != 0) {
            if (flashMode) exitFlashMode();
            else           enterFlashMode();
            Serial.println("TOUCH:15");
            Serial.println("RELEASE:15");

            // DOUBLE TAP = toggle loop playback on/off.
            //
            // This needs no delay on the single tap: the second tap toggles
            // loop-edit straight back off again, so the two cancel and the
            // mode ends where it started. We just emit an extra command
            // alongside. LOOPPLAY is deliberately separate from the
            // markers existing — it turns wrap-around playback on and off
            // while leaving the loop points in place.
            uint32_t nowMs = millis();
            if (pad15LastTapMs != 0 &&
                (uint32_t)(nowMs - pad15LastTapMs) <= PAD15_DOUBLE_TAP_MS) {
                Serial.println("LOOPPLAY");
                pad15LastTapMs = 0;      // don't let a third tap re-fire
            } else {
                pad15LastTapMs = nowMs;
            }
        }
        pad15PressMs        = 0;
        pad15LongPressFired = false;
    }
    // Pad 18's 3-second hold triggers a local reset gesture. When that
    // fires we must suppress the RELEASE:18 forward, otherwise the DAW's
    // pad-18 tap-release handler would immediately toggle mute on top of
    // the reset.
    bool suppressReleaseSend = false;
    if (touchNum == 18) {
        if (pad18ResetFired) {
            resetShowFinish();
            suppressReleaseSend = true;
        } else if (!muteFlashActive) {
            // Tap release: predictively toggle mute LED so it responds
            // instantly instead of waiting for the DAW's LED:0 round-trip.
            // Skipped in marker-scroll mode — the DAW uses pad 18 to
            // exit scroll mode and LED 0 is owned by the flash animation.
            muteLedLocal = !muteLedLocal;
            ledSet(0, muteLedLocal);
        }
        pad18PressMs    = 0;
        pad18ResetFired = false;
    }
    if (touchNum == 17 && !muteFlashActive) {
        // Tap release: predictive solo LED toggle, same responsiveness
        // trick as pad 18 (mute) and pad 19 (play). Skipped in scroll mode.
        soloLedLocal = !soloLedLocal;
        ledSet(1, soloLedLocal);
    }
    if (touchNum == 16 && !muteFlashActive) {
        // Tap release: predictive record-arm LED toggle. Skipped in scroll mode.
        armLedLocal = !armLedLocal;
        ledSet(2, armLedLocal);
    }
    if (touchNum == 19) {
        // Replay deferred press-action if the tap wasn't consumed by a
        // delete combo. RELEASE:19 is sent from within firePad19Deferred.
        bool wasModifier = pad19UsedAsModifier;
        pad19Held           = false;
        pad19UsedAsModifier = false;
        if (!wasModifier) firePad19Deferred();
    } else if (!suppressReleaseSend) {
        Serial.print("RELEASE:");
        Serial.println(touchNum);
    }
    if (!displayMode && millis() >= oledLockUntilMs) {
        snprintf(oledLine2, sizeof(oledLine2), "BUTTON %d OFF", touchNum);
        oledLine2Changed = true;
    }
    // Combo lock: cleared once both combo pads (19 + 14 or 19 + 15) are
    // physically released. Checked at the very end of the release handler
    // so all held-state flags for this release have been updated first.
    if (comboFired && !pad19Held && !pad14Held && !pad15Held) {
        comboFired = false;
    }
}

void loop() {
    static long lastPosition[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
    static long pendingDelta[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
    static unsigned long lastSendTime[NUM_ENCODERS] = {0, 0, 0, 0, 0, 0};
    static unsigned long lastHeartbeat = 0;

    // Loop-time instrumentation. The DAW's writes to us were measured
    // blocking for 600 ms on average and up to 5 s, which only happens if
    // we are too slow to drain the USB RX endpoint. Report via LOOPSTAT.
    {
        static uint32_t loopLastUs = 0;
        uint32_t nowUs = micros();
        if (loopLastUs != 0) {
            uint32_t d = nowUs - loopLastUs;
            if (d > loopMaxUs) loopMaxUs = d;
            loopSumUs += d;
            loopCount++;
        }
        loopLastUs = nowUs;
    }

    // --- Serial commands from DAW ---
    processSerialCommands();

    // --- LED identify flasher ---
    // For every channel with identify=true, toggle the physical LED at
    // ~2 Hz using the channel's brightness. Cheap: one 250 ms guard + a
    // 9-slot check per loop iteration.
    {
        uint32_t now = millis();
        if (now - ledIdentifyLastMs >= 250) {
            ledIdentifyLastMs = now;
            ledIdentifyPhase  = !ledIdentifyPhase;
            for (uint8_t ch = 0; ch < 9; ch++) {
                if (ledIdentify[ch]) {
                    pca9685SetPWM(ch, ledIdentifyPhase ? ledBrightness[ch] : 4095);
                }
            }
        }
    }

    // --- DAW connection watchdog ---
    // Detect the connected->disconnected transition once and display the
    // banner. Cleared on user activity (see touch/encoder handlers below).
    {
        bool nowConnected = (millis() - lastDawHeartbeatMs) < DAW_HEARTBEAT_TIMEOUT_MS;
        // Reconnect: clear the stale "switch to diag on next activity" flag
        // from the previous disconnect, otherwise the first touch after the
        // DAW comes back would drop us into diagnostic mode.
        if (!dawConnected && nowConnected) {
            pendingSwitchToDiagOnActivity = false;
        }
        if (dawConnected && !nowConnected) {
            // Exit text-input mode on disconnect — otherwise the cursor
            // blink would immediately overwrite the banner and then keep
            // clobbering any diagnostic event after user activity. Also
            // stops the pad-20/21 name-entry flash if it was active.
            if (textInputMode) textInputExit();
            // Clean-sweep every LED and mirror so the controller doesn't
            // sit with LEDs stuck in the last-known state after the DAW
            // vanishes. The DAW normally sends LED:*:OFF on graceful
            // shutdown; this covers the crash / kill / unplug paths.
            for (int ch = 0; ch < 9; ch++) pca9685SetPWM(ch, 4095);
            playLedLocal      = false;
            loopLeftLocal     = false;
            loopRightLocal    = false;
            recordLeftLocal   = false;
            recordRightLocal  = false;
            loopPairDefined   = false;
            recordPairDefined = false;
            if (flashMode) { flashMode = false; flashModeDeleteArmed = false; }
            // Any DAW-owned exclusive mode was for a session that just
            // vanished. Clear it so pads regain their local behaviours
            // (including the "BUTTON N ON" OLED echo in diagnostic mode).
            dawExclusiveMode = false;
            snprintf(oledLine1, sizeof(oledLine1), "DAW DISCONNECTED");
            snprintf(oledLine2, sizeof(oledLine2), " ");
            oledLine1Changed = true;
            oledLine2Changed = true;
            oledLockUntilMs  = 0;   // no lock — user activity will overwrite
            oledLockIsSoft   = false;
            pendingSwitchToDiagOnActivity = true;
        }
        dawConnected = nowConnected;
    }

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
            // First activity after DAW disconnect flips to diagnostic mode.
            if (pendingSwitchToDiagOnActivity) {
                pendingSwitchToDiagOnActivity = false;
                setDisplayMode(false);
            }
            // E1 while text-input mode is active is locally consumed as
            // cursor navigation, not sent to the DAW as a scrub. 100
            // ticks per single cursor step to tame the encoder sensitivity.
            if (i == 0 && textInputMode) {
                textInputE1Accum += pendingDelta[i];
                int len = (int)strlen(textInputBuffer);
                while (textInputE1Accum >= TEXTIN_E1_STEP) {
                    textInputE1Accum -= TEXTIN_E1_STEP;
                    if (textInputCursorPos < len) textInputCursorPos++;
                }
                while (textInputE1Accum <= -TEXTIN_E1_STEP) {
                    textInputE1Accum += TEXTIN_E1_STEP;
                    if (textInputCursorPos > 0)   textInputCursorPos--;
                }
                textInputRender();
                pendingDelta[i] = 0;
                lastSendTime[i] = nowMicros;
                digitalWrite(LED_BUILTIN, LOW);
                continue;
            }
            // Encoders E3-E6 (i == 2..5) move markers. Any movement while
            // clear-markers mode is on is treated as an implicit exit —
            // the user is nudging a marker's position, not managing pairs.
            if (flashMode && i >= 2 && i <= 5) {
                exitFlashMode();
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
        // Recovery path: a previous read failed → try to re-init the
        // chip once every 500 ms. Live electrode wire changes and ESD
        // events sometimes hang the MPR121 or its I2C ACK; without
        // this, only a USB replug would bring it back.
        if (!mpr121Found[chip]) {
            if (mpr121NeedsReinit[chip] &&
                (uint32_t)(millis() - mpr121LastReinitMs[chip]) > 500) {
                mpr121LastReinitMs[chip] = millis();
                if (mpr121Init(MPR121_ADDR[chip])) {
                    mpr121Found[chip]        = true;
                    mpr121NeedsReinit[chip]  = false;
                    lastTouchState[chip]     = 0;
                    Serial.print("MPR121 #"); Serial.print(chip);
                    Serial.println(" recovered");
                }
            }
            continue;
        }
        uint16_t touchState = mpr121ReadTouch(MPR121_ADDR[chip]);
        if (!lastMpr121ReadOk) {
            // Chip stopped ACKing — take it offline and schedule reinit.
            mpr121Found[chip]        = false;
            mpr121NeedsReinit[chip]  = true;
            mpr121LastReinitMs[chip] = millis();
            Serial.print("MPR121 #"); Serial.print(chip);
            Serial.println(" hung; will retry");
            continue;
        }
        if (touchState != lastTouchState[chip]) {
            for (int i = 0; i < 12; i++) {
                bool now = touchState & (1 << i);
                bool was = lastTouchState[chip] & (1 << i);
                int touchNum = chip * 12 + i;  // 0-11, 12-23, 24-35
                if (now && !was) {
                    handleTouchPressed(touchNum);
                } else if (!now && was) {
                    handleTouchReleased(touchNum);
                }
                // (all handling moved into handleTouchPressed / handleTouchReleased above)
            }
            lastTouchState[chip] = touchState;
        }
    }

    // Big CAP1188 pad, polled on the same cadence as the MPR121 pads.
    cap1188Scan();

    // --- OLED: emit at most one I2C chunk per loop iteration ---
    // Each call is ~1.6 ms max of I2C wire time, so the loop stays responsive
    // to serial and touch/encoder events even while a line is being redrawn.
    oledStep();

    // --- Play-LED fade flash (mod+play rejection indicator) ---
    flashStep();

    // --- Text-input cursor blink (when TEXTIN mode is active) ---
    cursorStep();

    // --- Text-input LED flash on pads 20 / 21 (name-entry mode) ---
    textInputFlashStep();
    muteFlashStep();

    // --- Text-input pending-letter OLED fade ---
    textInputFadeStep();

    // --- 3-blink acknowledgement for a marker-pair delete ---
    markerDeleteFlashStep();

    // --- Pad 15 long-press: delete both loop and record pairs ---
    if (pad15PressMs != 0 && !pad15LongPressFired &&
        (millis() - pad15PressMs) >= DELETE_ALL_HOLD_MS) {
        pad15LongPressFired = true;
        performDeleteAllLoops();
    }
    // Drive the 1 s fade-out of the four pair LEDs after a delete-all.
    deleteAllFadeStep();

    // --- Hard reset: pad 18 held for RESET_HOLD_MS ---
    // The "RESET" show stays up as long as pad 18 is held; on release
    // resetShowFinish() (from the touch release handler) repaints from
    // the mirrors the DAW filled in during the hold.
    if (pad18PressMs != 0 && !pad18ResetFired &&
        (millis() - pad18PressMs) >= RESET_HOLD_MS) {
        pad18ResetFired = true;
        performReset();
    }

    // --- Text-input phase re-sync (every ~150 ms while typing) ---
    // Prevents DAW-side drift or initial-send latency from putting the
    // DAW's red-fill pulse out of phase with the physical LEDs. Cheap:
    // one 20-byte serial write per tick.
    static uint32_t lastRenameSyncMs = 0;
    // Broadcast phase whenever the pad-20/21 flash is running — both the
    // rename flow (textInputMode) and DAW-driven channel-entry mode via
    // PAIRFLASH share the same flag, so keying the sync off it covers
    // both cases without a separate broadcast path.
    if (textInputFlashActive && millis() - lastRenameSyncMs > 150) {
        lastRenameSyncMs = millis();
        Serial.print("RENAMESYNC:");
        Serial.println(textInputFlashPhase());
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



