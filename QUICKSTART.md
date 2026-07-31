# Quick Start Guide

## Get Up and Running in 5 Minutes

### Step 1: Download Dependencies (2 minutes)

**Windows:**
```bash
setup_dependencies.bat
```

**Mac/Linux:**
```bash
chmod +x setup_dependencies.sh
./setup_dependencies.sh
```

This downloads:
- PortAudio (audio I/O)
- libsndfile (WAV files)
- RtMidi (MIDI input)
- Dear ImGui (GUI)

### Step 2: Build (2 minutes)

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

**Windows users:** You can also open the folder in Visual Studio and press F5.

### Step 3: Run (1 minute)

**Windows:**
```bash
.\Debug\MinimalDAW.exe
```

**Mac/Linux:**
```bash
./MinimalDAW
```

You should hear a **440 Hz test tone** (the musical note A4).

Press **Enter** to stop and exit.

## What You Just Built

✅ **Professional low-latency audio engine** using PortAudio
✅ **ASIO support** on Windows (if you have ASIO drivers)
✅ **CoreAudio support** on macOS
✅ **Cross-platform C++ codebase** that compiles on Windows and Mac
✅ **Real-time audio callback** with lock-free design
✅ **Test tone generator** playing through your audio interface

## Verify It's Working

When you run the app, you should see:

```
==================================
   Minimal DAW v0.1.0
==================================

Initializing audio engine...
Available audio devices: X
  [0] Device Name (in: 2, out: 2)
  [1] Another Device (in: 0, out: 2)
  ...
Audio engine initialized successfully

Starting audio stream...
Using device: Your Audio Device
Audio stream started (Sample rate: 44100 Hz, Buffer size: 256 samples)
Playing 440 Hz test tone...

Press Enter to stop the test tone and exit...
```

## Troubleshooting

### "PortAudio not found"
Run the setup script: `setup_dependencies.bat` or `./setup_dependencies.sh`

### "CMake version too old"
Update CMake to 3.15 or later: https://cmake.org/download/

### "No audio output"
- Check your audio device is connected
- Increase system volume (test tone is at 20% volume)
- Try a different audio device

### Build errors
- Windows: Make sure Visual Studio 2017+ is installed with C++ support
- Mac: Install Xcode command line tools: `xcode-select --install`

## Next Steps

Now that audio works, you can:

1. **Add GUI** - Integrate Dear ImGui for visual interface
2. **Load WAV files** - Use libsndfile to play actual audio files
3. **Draw waveforms** - Visualize audio on timeline
4. **Add MIDI** - Control with MIDI controllers

See [STATUS.md](STATUS.md) for current progress and next milestones.

See [DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md) for the full feature plan.

## File Guide

- **[README.md](README.md)** - Project overview
- **[BUILD.md](BUILD.md)** - Detailed build instructions
- **[STATUS.md](STATUS.md)** - Current status and next steps
- **[docs/PROJECT_ARCHITECTURE.md](docs/PROJECT_ARCHITECTURE.md)** - Architecture deep dive
- **[docs/DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md)** - Phased development plan

## Key Files to Explore

| File | What It Does |
|------|-------------|
| [CMakeLists.txt](CMakeLists.txt) | Main build configuration |
| [src/main.cpp](src/main.cpp) | Application entry point |
| [src/audio/audio_engine.h](src/audio/audio_engine.h) | Audio engine interface |
| [src/audio/audio_engine.cpp](src/audio/audio_engine.cpp) | Audio implementation with PortAudio |
| [external/CMakeLists.txt](external/CMakeLists.txt) | Dependency configuration |

## Technical Details

**Audio Settings:**
- Sample Rate: 44100 Hz
- Buffer Size: 256 samples
- Latency: ~5.8ms (256 / 44100)
- Format: 32-bit float, stereo
- Test Tone: 440 Hz sine wave at 20% amplitude

**Architecture:**
- Language: C++17
- Build: CMake 3.15+
- Threading: Lock-free audio callback
- Platform: Windows 7+ / macOS 10.13+

## Success! 🎉

If you can hear the test tone, **your audio pipeline is working perfectly** and you're ready to build a full DAW!

The hardest part (low-latency audio) is done. Everything from here is incremental features.
