# Development Roadmap

## Phase 1: Foundation (Current)

### 1.1 Project Setup ✓
- [x] Create CMake project structure
- [x] Set up cross-platform build configuration
- [x] Create basic source file organization
- [x] Add .gitignore

### 1.2 Dependency Integration
- [ ] Integrate PortAudio
  - [ ] Add to CMake build
  - [ ] Test ASIO device enumeration (Windows)
  - [ ] Test CoreAudio device enumeration (Mac)
- [ ] Integrate libsndfile
  - [ ] Add to CMake build
  - [ ] Test WAV file reading
  - [ ] Test WAV file writing
- [ ] Integrate RtMidi
  - [ ] Add to CMake build
  - [ ] Test MIDI device enumeration
  - [ ] Test MIDI input
- [ ] Integrate Dear ImGui
  - [ ] Add to CMake build
  - [ ] Setup DirectX 9 backend (Windows)
  - [ ] Setup OpenGL backend (Mac)
  - [ ] Create basic window

### 1.3 Audio Engine Foundation
- [ ] Implement PortAudio device selection
- [ ] Implement test tone generation (440 Hz sine wave)
- [ ] Verify low-latency operation (measure actual latency)
- [ ] Implement lock-free audio callback
- [ ] Test on multiple buffer sizes (64, 128, 256, 512 samples)

## Phase 2: Core Audio Features

### 2.1 WAV File Support
- [ ] WAV file loader
- [ ] WAV file metadata reading
- [ ] Audio buffer management
- [ ] WAV file writer
- [ ] Handle different sample rates and bit depths

### 2.2 Basic Playback
- [ ] Audio clip class
- [ ] Simple playback engine (single clip)
- [ ] Transport controls (play, stop, pause)
- [ ] Timeline position tracking
- [ ] Sample-accurate playback

## Phase 3: GUI Development

### 3.1 Basic UI Layout
- [ ] Main window setup
- [ ] Transport control UI (play, stop, pause buttons)
- [ ] Timeline view
- [ ] Track view
- [ ] Status bar (sample rate, buffer size, CPU usage)

### 3.2 Waveform Display
- [ ] Waveform rendering algorithm
- [ ] Zoom controls
- [ ] Scroll controls
- [ ] Waveform caching for performance
- [ ] Peak/RMS visualization

### 3.3 Track Management
- [ ] Track header UI
- [ ] Add/remove tracks
- [ ] Track selection
- [ ] Volume/pan controls per track

## Phase 4: Audio Clip Features

### 4.1 Clip Manipulation
- [ ] Clip placement on timeline
- [ ] Clip selection
- [ ] Clip moving/dragging
- [ ] Clip trimming
- [ ] Clip splitting

### 4.2 Fade In/Out
- [ ] Fade envelope calculation
- [ ] Linear fade algorithm
- [ ] Exponential fade algorithm
- [ ] Fade UI handles
- [ ] Visual fade representation on waveform

## Phase 5: MIDI Controller Integration

### 5.1 MIDI Input
- [ ] MIDI device selection UI
- [ ] MIDI message parsing
- [ ] MIDI CC to parameter mapping
- [ ] MIDI learn functionality

### 5.2 MIDI Mappings
- [ ] Timeline scroll via MIDI
- [ ] Transport control via MIDI
- [ ] Track selection via MIDI
- [ ] Volume control via MIDI
- [ ] Mapping save/load

## Phase 6: Multi-track Mixing

### 6.1 Mixing Engine
- [ ] Multi-track summing
- [ ] Per-track volume control
- [ ] Per-track pan control
- [ ] Master output bus
- [ ] Metering (peak/RMS)

### 6.2 Recording
- [ ] Audio input selection
- [ ] Record enable per track
- [ ] Record to WAV file
- [ ] Punch in/out recording
- [ ] Monitor input while recording

## Phase 7: Performance Optimization

### 7.1 Threading
- [ ] Lock-free audio thread implementation
- [ ] GUI thread separation
- [ ] MIDI thread implementation
- [ ] Lock-free queues for inter-thread communication

### 7.2 Memory Management
- [ ] Audio buffer pooling
- [ ] Waveform cache optimization
- [ ] Minimize allocations in audio callback
- [ ] Memory profiling

### 7.3 Rendering Optimization
- [ ] Waveform rendering optimization
- [ ] Dirty rectangle updates
- [ ] 60fps GUI target
- [ ] GPU acceleration where appropriate

## Phase 8: Stability & Testing

### 8.1 Testing
- [ ] Unit tests for audio engine
- [ ] Unit tests for file I/O
- [ ] Integration tests
- [ ] Stress testing (many tracks, long sessions)
- [ ] Latency benchmarking

### 8.2 Error Handling
- [ ] Graceful audio device failure handling
- [ ] File I/O error handling
- [ ] User error messages
- [ ] Crash recovery

## Phase 9: Polish & Release Prep

### 9.1 User Experience
- [ ] Keyboard shortcuts
- [ ] Undo/redo system
- [ ] Preferences/settings UI
- [ ] Project save/load
- [ ] Recent files list

### 9.2 Documentation
- [ ] User manual
- [ ] Keyboard shortcut reference
- [ ] MIDI mapping guide
- [ ] Build instructions

### 9.3 Distribution
- [ ] Windows installer
- [ ] Mac universal binary (x86_64 + ARM64)
- [ ] Code signing
- [ ] License management

## Future Considerations

### Hardware Control Integration
- [ ] Design protocol for external hardware control
- [ ] Raspberry Pi offload architecture
- [ ] Network communication layer
- [ ] UI separation from audio engine

### Additional Features (Post-MVP)
- [ ] VST plugin support
- [ ] Automation lanes
- [ ] Time stretching
- [ ] Pitch shifting
- [ ] Built-in effects (EQ, compression, reverb)
- [ ] MIDI sequencing
- [ ] Score view

## Current Focus

**Next Immediate Steps:**
1. Run dependency setup script
2. Integrate PortAudio into CMake
3. Get test tone playing through ASIO/CoreAudio
4. Add basic ImGui window
