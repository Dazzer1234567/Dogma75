# Build Instructions

## Quick Start

### 1. Install Prerequisites

#### Windows
- **Visual Studio 2017 or later** (with C++ desktop development workload)
- **CMake 3.15 or later** - [Download here](https://cmake.org/download/)
- **Git** - [Download here](https://git-scm.com/downloads)

#### macOS
- **Xcode 10 or later** (with command line tools)
- **CMake 3.15 or later** - Install via Homebrew: `brew install cmake`
- **Git** - Comes with Xcode command line tools

### 2. Download Dependencies

Run the setup script to clone all required libraries:

**Windows:**
```bash
setup_dependencies.bat
```

**macOS/Linux:**
```bash
chmod +x setup_dependencies.sh
./setup_dependencies.sh
```

This will clone:
- PortAudio (for low-latency audio I/O)
- libsndfile (for WAV file support)
- RtMidi (for MIDI controller input)
- Dear ImGui (for GUI)

### 3. Build the Project

#### Option A: Command Line (Cross-platform)

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

For Release build:
```bash
cmake --build . --config Release
```

#### Option B: Visual Studio (Windows)

1. Open Visual Studio
2. Choose "Open a local folder"
3. Select the `Dogma75` folder
4. Visual Studio will automatically detect CMake and configure the project
5. Press F5 to build and run

#### Option C: Xcode (macOS)

```bash
mkdir build
cd build
cmake -G Xcode ..
open MinimalDAW.xcodeproj
```

## Running the Application

After building:

**Windows:**
```bash
cd build
.\Debug\MinimalDAW.exe
```

**macOS:**
```bash
cd build
./MinimalDAW
```

The application will:
1. Initialize PortAudio
2. List all available audio devices
3. Start playing a 440 Hz test tone
4. Wait for you to press Enter to exit

## Troubleshooting

### CMake can't find dependencies
- Make sure you ran the `setup_dependencies` script
- Check that the `external/` folder contains: `portaudio`, `libsndfile`, `rtmidi`, and `imgui` directories

### Build fails with PortAudio errors
- On Windows, make sure you have ASIO SDK installed (optional for ASIO support)
- Try cleaning the build: `rm -rf build` and rebuild

### No audio output
- Check that your audio device is properly connected
- On Windows, make sure you have audio drivers installed
- The test tone plays at 20% volume, increase your system volume if needed

### Link errors on macOS
- Make sure Xcode command line tools are installed: `xcode-select --install`
- Update CMake to the latest version

## Build Configuration

### Custom Build Options

You can customize the build with CMake options:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### Sample Rate and Buffer Size

The default configuration is:
- Sample Rate: 44100 Hz
- Buffer Size: 256 samples
- This gives approximately 5.8ms latency at 44.1kHz

You can modify these in [audio_engine.h](src/audio/audio_engine.h).

## Platform-Specific Notes

### Windows
- The project uses DirectX 9 for GUI rendering (widely compatible)
- ASIO support is automatic if PortAudio detects ASIO drivers
- Minimum: Windows 7

### macOS
- The project uses OpenGL 2.1 for GUI rendering
- Builds as a universal binary (x86_64 + ARM64)
- Minimum: macOS 10.13 (High Sierra)

## Next Steps

Once you have the test tone working:
1. Integrate Dear ImGui for the GUI
2. Add WAV file loading with libsndfile
3. Implement waveform display
4. Add MIDI controller support with RtMidi

See [DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md) for the full development plan.
