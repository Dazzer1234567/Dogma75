# DogControl Physical Setup

## Components

1. **Teensy 4.1** - Main microcontroller (3.3V logic)
2. **MCP23017** - I2C I/O expander for buttons and LEDs
3. **E6B2-CWZ6C Encoder** - Industrial rotary encoder (600 P/R, NPN open-collector output)
4. **External Power Supply** - 6.5V+ for encoder (encoder won't work at 5V)

---

## Encoder Wiring (E6B2-CWZ6C)

| Encoder Wire | Color  | Connects To                     |
|--------------|--------|--------------------------------|
| Power (+)    | Brown  | External 6.5V+ supply          |
| Ground (0V)  | Blue   | Common ground (shared with Teensy) |
| Shield       | Shield | Common ground                  |
| Output A     | Black  | Teensy Pin 2 + 4.7k pull-up to 3.3V |
| Output B     | White  | Teensy Pin 3 + 4.7k pull-up to 3.3V |
| Output Z     | Orange | Not connected (index pulse)    |

**Important Notes:**
- Encoder requires 6.5V or higher to operate (knockoff version)
- NPN open-collector outputs need pull-up resistors (4.7k to Teensy 3.3V)
- Encoder ground MUST be connected to same ground as Teensy
- Shield wire helps reduce electrical noise

---

## MCP23017 Wiring

| MCP23017 Pin | Connects To          |
|--------------|---------------------|
| VDD (pin 9)  | Teensy 3.3V         |
| VSS (pin 10) | Ground              |
| SDA (pin 13) | Teensy Pin 18 (SDA) |
| SCL (pin 12) | Teensy Pin 19 (SCL) |
| A0 (pin 15)  | Ground (address bit 0) |
| A1 (pin 16)  | Ground (address bit 1) |
| A2 (pin 17)  | Ground (address bit 2) |
| RESET (pin 18) | Teensy 3.3V (active low, tie high) |

**I2C Address:** 0x20 (all address pins grounded)

---

## Button Wiring (via MCP23017)

| Connection       | Connects To      |
|------------------|-----------------|
| Button Terminal 1 | MCP23017 GPA0 (pin 21) |
| Button Terminal 2 | Ground          |

**Note:** Internal pull-up enabled on MCP23017. Button reads LOW (0) when pressed.

---

## LED Wiring (via MCP23017)

| Connection       | Connects To      |
|------------------|-----------------|
| LED Anode (+)    | MCP23017 GPB0 (pin 1) |
| LED Cathode (-)  | Ground via 220-330 ohm resistor |

---

## Teensy 4.1 Pin Summary

| Teensy Pin | Function           |
|------------|-------------------|
| Pin 2      | Encoder A input   |
| Pin 3      | Encoder B input   |
| Pin 18     | I2C SDA (to MCP23017) |
| Pin 19     | I2C SCL (to MCP23017) |
| LED_BUILTIN | Status indicator  |
| 3.3V       | Power for MCP23017 + pull-ups |
| GND        | Common ground     |

---

## Power Distribution

```
External 6.5V+ Supply
    |
    +-- Brown wire --> Encoder power
    |
    +-- GND ----------> Blue wire (encoder)
                   +--> Shield wire (encoder)
                   +--> Teensy GND
                   +--> MCP23017 VSS

Teensy 3.3V
    |
    +-- MCP23017 VDD
    +-- MCP23017 RESET
    +-- 4.7k resistor --> Encoder Black (Output A)
    +-- 4.7k resistor --> Encoder White (Output B)
```

---

## Expansion Notes

- **Additional encoders:** Use any digital pins (all support interrupts on Teensy 4.1)
- **Additional buttons:** Connect to MCP23017 GPA1-GPA7 (7 more available)
- **Additional LEDs:** Connect to MCP23017 GPB1-GPB7 (7 more available)
- **More I/O:** Add another MCP23017 at address 0x21 (tie A0 to 3.3V)
