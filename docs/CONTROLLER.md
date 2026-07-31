# Dogma75 Controller Reference

Every mapped control on the physical Teensy 4.1 controller — pads, encoders,
LEDs, modes. Anything not listed here has no assigned function yet.

---

## Hardware layout at a glance

- **6 rotary encoders**: `E1`–`E6`
- **36 capacitive touch pads** (three MPR121 chips): pads `0`–`35`. Only the
  handful listed below are wired to functions; the rest are unassigned.
- **9 LEDs** (PCA9685 channels `0`–`8`): each associated with a pad below.
- **SSD1362 OLED**: firmware-owned status display; not user-programmable.

---

## Touch pads

| Pad | LED | Name         | On press (normal mode)                                          |
| --- | --- | ------------ | --------------------------------------------------------------- |
| 16  | 2   | record       | *(no function yet)*                                             |
| 17  | 1   | solo         | *(no function yet)*                                             |
| 18  | 0   | mute         | *(no function yet)*                                             |
| 19  | 3   | **play**     | Toggle playback (LED 3 on = playing). Also cancels scrub-resume. |
| 20  | 4   | loop-left    | Toggle loop-left mode (armed = LED 4 on).                       |
| 21  | 5   | record-left  | Toggle record-left mode.                                        |
| 22  | 6   | record-right | Toggle record-right mode.                                       |
| 23  | 7   | loop-right   | Toggle loop-right mode.                                         |
| 24  | 8   | **pan mod**  | *Momentary modifier*: LED 8 on while held. Used with E2.        |
| 26  | —   | **modifier** | *Momentary modifier*. No LED. Used with E3, and with play.      |

`pad 0` also triggers play/stop as a legacy fallback; the physically wired
play pad is `pad 19`.

---

## Encoders

| Encoder | Alone                                             | With a modifier                                                  |
| ------- | ------------------------------------------------- | ---------------------------------------------------------------- |
| **E1**  | Scrub the playhead. Pauses playback and auto-resumes 100 ms after the last movement (if it was playing). | —                                                                |
| **E2**  | Waveform **zoom** (in / out).                     | With **pad 24** held → **pan** the timeline horizontally.        |
| **E3**  | Adjust **loop-left** marker (marker 0).           | With **pad 26** held → **scroll** the timeline view.             |
| **E4**  | Adjust **record-left** marker (marker 1).         | —                                                                |
| **E5**  | Adjust **record-right** marker (marker 2).        | —                                                                |
| **E6**  | Adjust **loop-right** marker (marker 3).          | —                                                                |

**Encoder rules:**

- An encoder only affects its marker if that marker is currently **enabled**.
  Encoders do not create markers — see "Clear-markers mode" below for how to
  turn a marker on.
- **Pair-only clamping**: loop-left cannot cross loop-right, record-left
  cannot cross record-right, and vice versa. No cross-pair constraints —
  a loop marker is free to sit between the record markers, etc.

---

## Play button behaviours

**Plain tap on pad 19:**
- If audio is playing → stop.
- If audio is stopped → start playback from the current playhead position.
- If **loop-left mode is armed** (LED 4 on) and marker 0 is placed, playback
  jumps to marker 0 first, then starts.

**Scrub-then-resume:**
- Turning E1 while playing pauses playback and arms a 100 ms resume timer.
- Each further encoder tick resets the timer.
- 100 ms after the last tick, playback resumes from wherever you parked the
  playhead.
- Any explicit press of play (or pad 0) cancels the pending resume.

**Modifier + play** (hold pad 26, tap pad 19):
- Does **not** start / stop playback.
- Enters **clear-markers mode** — see below.

---

## Clear-markers mode

The mode that lets you hide (clear) and reveal (restore) marker pairs.

**Enter:** hold **pad 26** (modifier), tap **pad 19** (play).

**While active:**
- LEDs 3, 4, 5, 6, 7 fade-flash together at 2 Hz.
- Play button, encoder-enable, and all other pads do nothing.
- **Tap a loop LED** (pad 20 or 23): the loop pair *toggles*.
  - If it was on → both markers cleared, LEDs 4 and 7 go **off**, pair
    disappears from the waveform.
  - If it was off → both markers restored. On the very first turn-on they're
    placed at 15 % / 85 % of the currently visible waveform range; on later
    turn-ons they return to the exact positions they held before being
    cleared.
- **Tap a record LED** (pad 21 or 22): same toggle behaviour for the record
  pair. First-time defaults: 25 % / 75 % of the visible range.
- Once a pair has been handled in a session, its LEDs stop flashing —
  further taps toggle silently at solid on / off.

**Exit:** tap **pad 26** again. All flash-affected LEDs restore to their
normal state. The exit press does not itself trigger the modifier.

---

## LED state summary

Outside of clear-markers mode:

| LED | Reflects                                                                    |
| --- | --------------------------------------------------------------------------- |
| 3   | Playback state — on while audio is actively playing.                        |
| 4   | Loop-left mode armed.                                                        |
| 5   | Record-left mode armed.                                                      |
| 6   | Record-right mode armed.                                                     |
| 7   | Loop-right mode armed.                                                       |
| 8   | Pad 24 (pan modifier) held.                                                  |
| 0/1/2 | Unused (mapped to mute / solo / record pads that don't have functions yet). |

The firmware drives every LED locally for instant response; the DAW asserts
corrections via `LED:N:ON` / `LED:N:OFF` only when its own state diverges
from what the firmware showed.

---

## Modifier keys — quick reference

| Modifier      | Held with       | Effect                                                                     |
| ------------- | --------------- | -------------------------------------------------------------------------- |
| **pad 26**    | E3              | Scroll the waveform view horizontally.                                     |
| **pad 26**    | pad 19 (tap)    | Enter clear-markers mode (does not play).                                  |
| **pad 24**    | E2              | Pan the timeline horizontally (E2 zooms without the modifier).             |

---

## Marker default positions

When a marker pair is turned on for the very first time via clear-markers
mode, the two markers are placed relative to the current visible waveform
range:

| Marker              | First-time position               |
| ------------------- | --------------------------------- |
| Loop-left (0)       | 15 % into view                    |
| Record-left (1)     | 25 % into view                    |
| Record-right (2)    | 75 % into view (25 % from right)  |
| Loop-right (3)      | 85 % into view (15 % from right)  |

Once placed, marker positions are preserved across clear / restore cycles —
subsequent restores return each marker exactly where it was last set.

---

## Startup behaviour

- **Teensy boot:** all LEDs off, all mode toggles off (loop-left,
  record-left, record-right, loop-right, play).
- **DAW startup:** first `updateController()` tick pushes each mode LED to
  its authoritative state — if any pad had been left toggled on locally
  before the DAW started, the DAW forces it back off to match its own boot
  state.
- **Pan-modifier LED (8):** stays off until you physically hold pad 24.

---

## Notes

- Touch sensitivity is set to threshold **6 / 3** (touch / release) on all
  MPR121 electrodes. This is more sensitive than the MPR121 default to keep
  marginal side-touches registering even when you're already holding another
  pad (which shifts the whole-chip baseline).
- PCA9685 outputs are configured **open-drain**, so LEDs are truly off
  (high-impedance) rather than dim (driven to VCC while the anode sits on a
  higher V+ rail).
- The controller needs a **6.5 V** external supply on the PCA9685 LED anode
  rail for the LEDs to light. USB alone powers the Teensy and I²C chips but
  not the LED rail.
