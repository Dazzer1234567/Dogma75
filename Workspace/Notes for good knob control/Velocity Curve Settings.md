# Velocity Curve Settings for Teensy Encoder

This document explains the velocity curve settings used to control how the encoder knob movement translates to playhead movement in Dogma75.

## Overview

The velocity curve system allows you to customize how fast the playhead moves based on how quickly you turn the encoder knob. This gives you fine control for slow, precise movements and faster traversal when spinning the knob quickly.

## Settings

### Max Input RPM (default: 120)
The encoder speed (in revolutions per minute) that maps to x=1.0 on the curve.

- **Lower values** (e.g., 60): The curve reaches maximum output at slower turning speeds
- **Higher values** (e.g., 200): You need to spin faster to reach maximum output

**Recommendation:** Set this to roughly the maximum speed you typically spin the knob.

---

### RPM Window (default: 50ms)
The time window used to calculate the current RPM. The system accumulates encoder pulses over this window and calculates the average speed.

- **Smaller values** (10-30ms): More responsive but noisier RPM readings
- **Larger values** (100-200ms): Smoother RPM but slower to react to speed changes

**Trade-off:**
- Small window = feels more immediate but may cause jittery multiplier changes
- Large window = smoother acceleration curve but slight lag when changing speed

**Recommendation:** Start with 50ms and adjust based on feel.

---

### Max Multiplier (default: 2.0x)
Caps the maximum speed multiplier regardless of what the curve outputs. This prevents excessive playhead movement even when spinning the encoder very fast.

- **Lower values** (0.5-1.0x): Limits acceleration, keeps movement controlled
- **Higher values** (3.0-5.0x): Allows faster traversal when spinning quickly

**Recommendation:** Set this to limit "runaway" behavior when you spin too fast.

---

### Smooth Checkbox
When enabled, uses Catmull-Rom spline interpolation between control points instead of linear interpolation.

- **Off**: Hard angles at control points (direct, predictable)
- **On**: Smooth curves through control points (gradual transitions)

---

### Curve Control Points
The graph shows input speed (X-axis: 0=stopped, 1=max RPM) vs output multiplier (Y-axis: 0=no movement, 1=normal, 2=2x speed).

**Default linear curve:**
- (0.0, 0.0) - No movement when stopped
- (0.33, 0.33) - 1/3 speed at 1/3 max RPM
- (0.66, 0.66) - 2/3 speed at 2/3 max RPM
- (1.0, 1.0) - Full speed at max RPM

**Preset curves:**
- **Linear**: Direct 1:1 relationship
- **Slow Start**: More precision at low speeds, accelerates at higher speeds
- **Fast Start**: Quick initial response, tapers off at high speeds
- **Precision**: Very slow at low speeds for fine control

---

## How It Works

1. **Encoder sends delta pulses** to DAW via serial (2400 pulses per revolution)
2. **RPM is calculated** over the configured time window
3. **RPM is normalized** by dividing by Max Input RPM (gives 0-1 range)
4. **Curve is evaluated** at the normalized input to get a multiplier
5. **Multiplier is capped** at Max Multiplier
6. **Playhead moves** based on: delta * (visible_frames / 2400) * multiplier

---

## Current Recommended Settings

Based on testing, these settings provide good control:

| Setting | Value | Notes |
|---------|-------|-------|
| Max Input RPM | 120 | Adjust to your typical max spin speed |
| RPM Window | 50ms | Good balance of responsiveness and smoothness |
| Max Multiplier | 2.0x | Prevents runaway at high speeds |
| Smooth | Off | Use On for gentler transitions |

---

## Tips

1. **For precise editing**: Use a "Slow Start" curve with low Max Multiplier
2. **For fast navigation**: Use higher Max Multiplier and Max Input RPM
3. **If RPM feels jittery**: Increase RPM Window
4. **If response feels laggy**: Decrease RPM Window
5. **If playhead moves too fast when spinning**: Lower Max Multiplier

---

## Technical Details

- **Encoder resolution**: 600 PPR x 4 (quadrature) = 2400 counts per revolution
- **Serial baud rate**: 115200
- **Teensy loop delay**: 100 microseconds
- **DAW polling**: 4x per frame (~4ms effective latency at 60fps)
