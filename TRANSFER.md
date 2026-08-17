# Moving Dogma75 to another computer

Everything needed to get the DAW, the Teensy controller and the Antelope
mixer control running on a fresh machine. Written after doing this once the
hard way — the gotchas near the bottom are all real ones we hit.

The project no longer depends on living at `c:\0_CODE\Dogma75`; paths are
resolved from the executable's location. Any directory works.

---

## 1. What the repo does and doesn't contain

**In the repo** — the DAW source, the Teensy firmware source
(`Firmware/src/main.cpp`), the Antelope mixer control
(`src/antelope/antelope_client.*`, compiled into the DAW — there is no
separate app or script), `settings/user_settings.json`, and the
reverse-engineering notes in `Workspace/antelope-control-re.md`.

**NOT in the repo** — `external/` is gitignored apart from its
`CMakeLists.txt`. That folder is ~271 MB and holds PortAudio, libsndfile,
RtMidi, Dear ImGui, SDL2 **and the Steinberg ASIO SDK**.

> **Copy `external/` from the old machine.** `setup_dependencies.bat` can
> re-fetch most of it, but **not the ASIO SDK** — that is licence-gated and
> not a public clone. Without it the ASIO build fails. Zip the folder before
> copying; it is tens of thousands of small files and a loose copy over a
> network share is slow and can silently drop files.

---

## 2. Prerequisites

### Build toolchain

```powershell
winget install --id Kitware.CMake --exact --scope machine

winget install --id Microsoft.VisualStudio.BuildTools --exact `
  --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Build Tools asks for a reboot afterwards. Verify:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationVersion
& "C:\Program Files\CMake\bin\cmake.exe" --version
```

You want VS **18.x** (2026) — `CMakePresets.json` and `build.bat` both name
the `Visual Studio 18 2026` generator. CMake is not on PATH in shells that
were already open; either restart them or call it by full path.

### PlatformIO — only needed to reflash the Teensy

Needs Python 3.6+ (`py --version`).

```powershell
Invoke-WebRequest https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -OutFile get-platformio.py
py get-platformio.py
```

Lands at `%USERPROFILE%\.platformio\penv\Scripts\platformio.exe`.

### Antelope software

Install the normal Antelope package so `Antelope-Manager-Service` runs
`AntelopeAudioServer` listening on `127.0.0.1:2021`. The DAW's mixer control
talks to that daemon; without it, mute control simply does nothing (the DAW
still runs).

---

## 3. Build

```powershell
git clone https://github.com/Dazzer1234567/Dogma75.git
# then copy external/ into the clone

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DPA_USE_ASIO=ON
cmake --build build --config Release
```

Output: `build\Release\MinimalDAW.exe`. `portaudio.dll` is copied next to it
automatically by a `POST_BUILD` rule — it is the only shared library; if you
ever see *"portaudio.dll was not found"*, that rule has been lost.

Configure should report all five dependencies `TRUE`:

```
PortAudio: TRUE   libsndfile: TRUE   RtMidi: TRUE   Dear ImGui: TRUE   SDL2: TRUE
```

### Firmware

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --project-dir Firmware --target upload
```

The Teensy auto-detects on COM1–COM20 via a heartbeat scan, so the
`monitor_port` in `Firmware/platformio.ini` does not need to match.

---

## 4. Hardware notes

| Item | Detail |
|---|---|
| Teensy 4.1 | USB serial, 115200. Enumerates as `VID_16C0`; `PID_0483` = serial (our firmware), `PID_0486` = RawHID (factory sketch) |
| MPR121 ×3 | `Wire` pins 18/19, addresses 0x5A–0x5C, pads 0–35 |
| PCA9685 | `Wire` pins 18/19, address 0x40, 9 LEDs, open-drain |
| SSD1362 OLED | `Wire2` pins 24/25, address 0x3C, reset pin 26 |
| CAP1188 | `Wire` pins 18/19, address 0x29. Big undo pad on C1 = pad 36 |

Send `SCAN` over serial to list what actually answers. A healthy rig reports
`0x29 0x40 0x5A 0x5B 0x5C` on Wire and `0x3C` on Wire2.

**The CAP1188's big pad needs a ~30 pF capacitor in series between the pad
and C1.** Wired directly, the strip's capacitance exceeds what the chip can
null during calibration and the delta reads exactly 0 at *every* gain — which
looks identical to a dead chip or a broken wire. Recalibrate with `CAPCAL`
after reconnecting the pad, since the baseline is captured at boot.

Useful serial commands: `SCAN`, `CAPCAL`, `CAPGAIN:n`, `CAPTH:n`, `CAPDBG`,
`CAPRD:<hexreg>`, `CAPREG:<hexreg>:<hexval>`, `LOOPSTAT`, `OLED`.

---

## 5. Gotchas that cost real time

**`external/CMakeLists.txt` is hand-written glue, not a vendored file.** It
sets the `*_FOUND` variables the root `CMakeLists.txt` consumes. It is now
tracked, but if it ever goes missing the build cannot configure at all.

**CMake 4 dropped `cmake_minimum_required(VERSION <3.5)`.** If a vendored
dependency is old enough to declare 2.8 or 3.0, configure hard-errors.
Workaround: `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, or use CMake 3.31.

**PortAudio's WDM-KS backend is disabled deliberately** in
`external/CMakeLists.txt`. `Pa_Initialize()` enumerates every enabled host
API, and WDM-KS *opens* each endpoint to interrogate it — enough to kill an
unrelated app's in-flight microphone stream when the DAW launches. Do not
re-enable it.

**Never send `"authorative": true` to the Antelope daemon.** It leaves the
daemon in a state the Control Panel cannot recover from: dead meters, and the
CP failing to open at all (showing only the device power tile). The damage is
sticky — closing the DAW does *not* fix it, so a "close it and see" test
gives a false negative. `AntelopeClient` sends `false`, and DSP writes still
work.

**If the Antelope Control Panel hangs** on `Executing sync_routing...`, or
opens with only a power button, or loses its meters:

1. Close `oriontb` and `Antelope Launcher`
2. Restart `Antelope-Manager-Service` **as administrator** (a normal shell
   gets `Access is denied`); confirm `AntelopeAudioServer` gets a new PID
3. Relaunch the Launcher, then the Control Panel

Closing the clients alone is not enough — the daemon itself must be recycled.

**The serial port must be opened `FILE_FLAG_OVERLAPPED`.** On a synchronous
handle Windows serialises I/O, so writes queue behind the reader thread's
in-flight `ReadFile` — measured at 610 ms average, up to 5 s. Symptom:
encoders instant, but button presses take about a second to light an LED.

---

## 6. Runtime files

- `settings/user_settings.json` — tracked. Rewrites its `_saved_at`
  timestamp on every run, so it will always show as modified in git.
- `Workspace/SESSIONS/` — default home for sessions, created on demand. Copy
  it across if you want your sessions.
- `daw.log`, `ctrl.log`, `perf.log` — written to the project root, truncated
  per run. `perf.log` only appears when a serial write exceeds 100 ms, so if
  that file is absent the serial path is healthy.
- `imgui.ini` — gitignored window layout, regenerates.

On startup the DAW opens the most recent session from the Recent Projects
list that still exists, and starts empty if none do.
