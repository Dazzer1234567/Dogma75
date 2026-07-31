# Project Architecture

## Overview

MinimalDAW is a professional-grade digital audio workstation built with C++ and CMake, focusing on low-latency audio performance and cross-platform compatibility.

## Architecture Principles

### 1. Real-Time Audio Safety
The audio callback must NEVER:
- Allocate or deallocate memory
- Use mutexes or locks
- Call system functions that may block
- Perform disk I/O
- Use virtual functions (in critical path)

### 2. Threading Model

```
┌─────────────────┐
│   Main Thread   │
│   (GUI + App)   │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
┌───▼───┐ ┌──▼────────┐
│ MIDI  │ │   Audio   │
│ Thread│ │  Thread   │
└───┬───┘ └──┬────────┘
    │        │
    │   Lock-Free
    │    Queues
    │        │
    └────────┘
```

**Audio Thread:**
- Real-time priority
- Lock-free communication
- Processes audio in fixed-size buffers
- Sample-accurate timing

**MIDI Thread:**
- Reads MIDI controller input
- Sends events via lock-free queue
- Never blocks audio thread

**GUI Thread:**
- Renders at 60fps target
- Updates from audio thread via lock-free queues
- Can block without affecting audio

### 3. Module Organization

```
src/
├── main.cpp              # Application entry point
├── audio/
│   ├── audio_engine.h    # Audio I/O and processing
│   ├── audio_engine.cpp
│   ├── audio_clip.h      # Audio clip representation
│   ├── audio_buffer.h    # Audio buffer management
│   └── mixer.h           # Multi-track mixing
├── gui/
│   ├── gui_manager.h     # GUI initialization and main loop
│   ├── gui_manager.cpp
│   ├── waveform_view.h   # Waveform rendering
│   ├── timeline_view.h   # Timeline and playback position
│   └── transport.h       # Play/stop/record controls
├── io/
│   ├── wav_file.h        # WAV file reading/writing
│   └── wav_file.cpp
├── midi/
│   ├── midi_manager.h    # MIDI device management
│   └── midi_mapping.h    # MIDI CC to parameter mapping
└── util/
    ├── lock_free_queue.h # Lock-free SPSC queue
    ├── atomic_flag.h     # Atomic operations
    └── timer.h           # High-precision timing
```

## Component Details

### Audio Engine

**Responsibilities:**
- Initialize PortAudio
- Enumerate audio devices
- Open audio streams with low latency settings
- Process audio callback
- Manage playback state

**Key Design Decisions:**
- Uses `std::atomic` for thread-safe state
- Phase accumulation for test tone (will be replaced with real playback)
- Stereo output (2 channels)
- Float32 sample format for precision

**Future Enhancements:**
- Device selection UI
- Sample rate conversion
- Multiple buffer size options
- CPU usage monitoring

### GUI Manager

**Responsibilities:**
- Create platform window (Win32 or Cocoa)
- Initialize Dear ImGui
- Setup rendering backend (DX9 or OpenGL)
- Process window events
- Render GUI at 60fps

**Platform Specifics:**

**Windows:**
- Win32 API for window creation
- DirectX 9 for rendering
- Reasons: Maximum compatibility, minimal dependencies

**macOS:**
- Cocoa (Objective-C++) for window creation
- OpenGL 2.1 for rendering
- Universal binary (x86_64 + ARM64)

### WAV File I/O

**Uses libsndfile:**
- Industry-standard library
- Supports multiple bit depths (16, 24, 32-bit int, 32/64-bit float)
- Handles sample rate metadata
- Robust error handling

**Operations:**
- Load WAV to memory buffer
- Stream large files (future)
- Write mixed output to WAV
- Metadata reading (sample rate, channels, duration)

### MIDI Manager

**Uses RtMidi:**
- Cross-platform MIDI I/O
- Device enumeration
- Lightweight and fast

**MIDI Mapping System:**
- Map MIDI CC to parameters
- MIDI learn functionality
- Save/load mappings
- Support for multiple controllers

## Memory Management

### Audio Buffers
- Pre-allocated at initialization
- Fixed-size buffers for real-time safety
- Interleaved stereo format: `[L, R, L, R, ...]`
- Buffer pool to avoid allocation during playback

### Waveform Data
- Separate peak/RMS cache for rendering
- Multiple zoom levels pre-computed
- Lazy loading for large files
- LRU cache for memory efficiency

## Data Flow

### Playback Path

```
WAV File → Audio Buffer → Mixer → Audio Engine → PortAudio → Audio Device
                ↑                      ↓
            Fade/Gain              Lock-free
             Processing             Position
                                      Queue
                                      ↓
                                   GUI Thread
                                      ↓
                                 Timeline View
```

### Recording Path

```
Audio Device → PortAudio → Audio Engine → Ring Buffer → GUI Thread → WAV File
```

### MIDI Input Path

```
MIDI Device → RtMidi → MIDI Manager → Event Queue → Parameter Update
                                                          ↓
                                                    Timeline Scroll
                                                    Transport Control
                                                    Volume/Pan
```

## Synchronization Strategy

### Audio → GUI Communication
```cpp
// Lock-free queue for playback position
LockFreeQueue<PlaybackPosition> positionQueue;

// In audio callback:
if (frameCount % updateInterval == 0) {
    positionQueue.push(currentPosition);
}

// In GUI thread:
PlaybackPosition pos;
while (positionQueue.pop(pos)) {
    updateTimeline(pos);
}
```

### GUI → Audio Communication
```cpp
// Atomic flags for simple commands
std::atomic<bool> playRequested{false};
std::atomic<bool> stopRequested{false};

// GUI thread:
playButton.onClick = [&]() {
    playRequested.store(true);
};

// Audio callback:
if (playRequested.exchange(false)) {
    startPlayback();
}
```

### Complex Commands
Use lock-free SPSC (Single Producer Single Consumer) queue:
```cpp
struct AudioCommand {
    enum Type { SetVolume, SetPan, LoadClip, ... };
    Type type;
    // Union or variant for parameters
};

LockFreeQueue<AudioCommand> commandQueue;
```

## Performance Targets

### Audio
- **Latency**: < 10ms round-trip (ASIO/CoreAudio)
- **Buffer sizes**: 64, 128, 256, 512 samples
- **CPU usage**: < 50% of one core at 44.1kHz with 10 tracks
- **Dropouts**: Zero underruns during normal operation

### GUI
- **Frame rate**: 60fps target, 30fps minimum
- **Waveform rendering**: < 16ms per frame
- **Memory**: < 500MB for typical session
- **Startup time**: < 2 seconds

## Build System

### CMake Structure
- Main `CMakeLists.txt`: Project configuration
- `external/CMakeLists.txt`: Dependency management
- Platform detection via CMake variables
- Conditional compilation with `#ifdef` guards

### Dependency Integration
- Static linking for all dependencies
- No runtime DLL dependencies
- Libraries built as part of main project
- Platform-specific settings per library

## Future Architecture Considerations

### Plugin Support (VST)
- VST3 SDK integration
- Plugin scanner (separate process for safety)
- Plugin state serialization
- Preset management

### Hardware Control Device
- Network protocol (OSC or custom)
- UI offload to Raspberry Pi
- Low-latency control messages
- Bidirectional feedback

### Multicore Scaling
- Per-track processing on separate threads
- Work-stealing scheduler
- SIMD optimization (SSE, AVX, NEON)
- Lock-free work queues

## Testing Strategy

### Unit Tests
- Audio buffer operations
- WAV file reading/writing
- MIDI message parsing
- Lock-free queue correctness

### Integration Tests
- Full audio path (load → play → output)
- MIDI to parameter mapping
- File I/O with various formats

### Performance Tests
- Audio callback timing
- CPU usage under load
- Memory allocation tracking
- Latency measurement

### Platform Tests
- Windows 7, 10, 11
- macOS 10.13 through latest
- Various audio interfaces
- Multiple MIDI controllers
