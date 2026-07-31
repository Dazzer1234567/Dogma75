#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include "track.h"
#include "../controller/serial_controller.h"

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void shutdown();

    // Audio device management
    bool startAudio(int deviceId = -1);
    void stopAudio();
    bool isRunning() const { return m_running.load(); }

    // Audio callback (will be called from PortAudio)
    int audioCallback(const void* inputBuffer, void* outputBuffer,
                     unsigned long framesPerBuffer,
                     const void* timeInfo,
                     unsigned long statusFlags);

    // Configuration
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }
    void setBufferSize(unsigned int bufferSize) { m_bufferSize = bufferSize; }
    double getSampleRate() const { return m_sampleRate; }
    unsigned int getBufferSize() const { return m_bufferSize; }

    // Device info
    const std::string& getDeviceName() const { return m_deviceName; }
    const std::string& getHostApiName() const { return m_hostApiName; }
    int getCurrentDeviceId() const { return m_currentDeviceId; }

    // Device enumeration
    int getDeviceCount() const;
    std::string getDeviceNameById(int deviceId) const;
    std::string getDeviceHostApiById(int deviceId) const;
    bool switchAudioDevice(int deviceId);

    // ASIO-only device enumeration
    int getAsioDeviceCount() const;
    int getAsioDeviceId(int asioIndex) const;
    std::string getAsioDeviceName(int asioIndex) const;

    // Test tone control
    void setTestToneEnabled(bool enabled) { m_testToneEnabled.store(enabled); }
    bool isTestToneEnabled() const { return m_testToneEnabled.load(); }

    // WAV file playback
    bool loadWavFile(const std::string& filepath);
    void play();
    void stop();
    bool isPlaying() const { return m_playing.load(); }
    const std::string& getLoadedFilePath() const { return m_loadedFilePath; }

    // Waveform data access for visualization
    const std::vector<float>& getAudioData() const { return m_audioData; }
    int getAudioChannels() const { return m_audioChannels; }
    size_t getPlaybackPosition() const { return m_playbackPosition.load(); }
    size_t getTotalFrames() const;
    void setPlaybackPosition(size_t position) { m_playbackPosition.store(position); }

    // MIDI control
    bool initializeMidi();
    void shutdownMidi();
    void processMidiMessages();

    // MIDI device selection
    int getMidiPortCount() const;
    std::string getMidiPortName(int portIndex) const;
    int getCurrentMidiPort() const { return m_currentMidiPort; }
    bool setMidiPort(int portIndex);

    // Output channel selection
    void setOutputStereoPair(int pairIndex) { m_outputStereoPair.store(pairIndex); }
    int getOutputStereoPair() const { return m_outputStereoPair.load(); }
    int getNumStereoPairs() const { return m_maxOutputChannels / 2; }

    // Track management
    int addTrack(const std::string& name = "");
    void deleteTrack(int trackIndex);
    int getTrackCount() const { return static_cast<int>(m_tracks.size()); }
    Track* getTrack(int trackIndex);
    const Track* getTrack(int trackIndex) const;
    int getSelectedTrack() const { return m_selectedTrack; }
    void setSelectedTrack(int trackIndex) { m_selectedTrack = trackIndex; }
    bool loadTrackAudio(int trackIndex, const std::string& filepath);

    // Waveform zoom
    float getWaveformZoom() const { return m_waveformZoom; }
    void setWaveformZoom(float zoom) {
        if (zoom < 1.0f) zoom = 1.0f;
        if (zoom > 100.0f) zoom = 100.0f;
        m_waveformZoom = zoom;
    }

    // Park mode
    void setSelectedPark(int park) { m_selectedPark = park; }
    int getSelectedPark() const { return m_selectedPark; }

    // Controller mode
    void setControllerMode(int mode) { m_controllerMode = mode; }
    int getControllerMode() const { return m_controllerMode; }

    // Scrub settings
    void setScrubSpeed(float speed) { m_scrubSpeed = speed; }
    float getScrubSpeed() const { return m_scrubSpeed; }
    void setScrubRpmThreshold(float rpm) { m_scrubRpmThreshold = rpm; }
    float getScrubRpmThreshold() const { return m_scrubRpmThreshold; }
    void setFastSpeedMultiplier(float mult) { m_fastSpeedMultiplier = mult; }
    float getFastSpeedMultiplier() const { return m_fastSpeedMultiplier; }
    float getCurrentEncoderRpm() const { return m_currentEncoderRpm; }
    void setRpmAveraging(float avg) { m_rpmAveraging = avg; }
    float getRpmAveraging() const { return m_rpmAveraging; }

    // Waveform markers
    void setMarkerPosition(int markerIndex, size_t position) {
        if (markerIndex >= 0 && markerIndex < 4) {
            m_markerPositions[markerIndex] = position;
            m_markerEnabled[markerIndex] = true;
        }
    }
    size_t getMarkerPosition(int markerIndex) const {
        return (markerIndex >= 0 && markerIndex < 4) ? m_markerPositions[markerIndex] : 0;
    }
    bool isMarkerEnabled(int markerIndex) const {
        return (markerIndex >= 0 && markerIndex < 4) ? m_markerEnabled[markerIndex] : false;
    }
    void clearMarker(int markerIndex) {
        if (markerIndex >= 0 && markerIndex < 4) m_markerEnabled[markerIndex] = false;
    }

    // Serial controller reference (set by main.cpp)
    void setSerialController(SerialController* sc) { m_serialController = sc; }
    SerialController* getSerialController() const { return m_serialController; }

    // Called by SerialController callbacks (via main.cpp wiring)
    void handleEncoderDelta(int encoder, long delta, float rpm, float velocityMultiplier);
    void handleTouch(int pad, bool pressed);

private:
    // MIDI helper methods (extracted from processMidiMessages)
    void handleJogWheel(int speed);
    void handleFaderZoom(int position, int range);
    void* m_stream;
    std::atomic<bool> m_running;
    std::atomic<bool> m_testToneEnabled;
    std::atomic<bool> m_playing;
    std::atomic<int> m_outputStereoPair;
    std::atomic<size_t> m_playbackPosition;

    double m_sampleRate;
    unsigned int m_bufferSize;
    int m_maxOutputChannels;

    // Device info
    std::string m_deviceName;
    std::string m_hostApiName;
    int m_currentDeviceId;

    // Test tone
    double m_testTonePhase;
    double m_testToneFrequency;

    // WAV file data (legacy, kept for loadWavFile)
    std::vector<float> m_audioData;
    int m_audioChannels;
    std::string m_loadedFilePath;

    // MIDI
    void* m_midiIn;
    void* m_midiOut;
    int m_currentMidiPort;

    // Tracks
    std::vector<Track> m_tracks;
    int m_selectedTrack;
    int m_trackCounter;

    // Waveform display
    float m_waveformZoom;

    // Park mode and controller
    int m_selectedPark;
    int m_controllerMode;
    float m_scrubSpeed;
    float m_scrubRpmThreshold;
    float m_fastSpeedMultiplier;
    float m_currentEncoderRpm;
    float m_rpmAveraging;

    // Markers
    size_t m_markerPositions[4];
    bool m_markerEnabled[4];

    // Encoder velocity tracking (for MIDI park encoders)
    double m_lastEncoderTime[4];

    // Audio scrubbing
    std::atomic<bool> m_scrubbing;
    std::atomic<float> m_scrubPlaybackRate;
    double m_scrubPlaybackPosition;
    double m_lastScrubTime;

    // Fader
    int m_faderTouchStartPosition;
    int m_lastFaderPosition;
    bool m_faderTouched;
    double m_lastFaderTouchTime;
    int m_faderTapCount;
    double m_lastFaderUpdateTime;

    // Modifier button (pad 26)
    std::atomic<bool> m_modifierHeld;

    // View scroll request (consumed by GUI each frame)
    std::atomic<long> m_viewScrollDelta;

    // Serial controller (owned by main, not by us)
    SerialController* m_serialController;

public:
    bool isModifierHeld() const { return m_modifierHeld.load(); }
    long consumeViewScrollDelta() {
        return m_viewScrollDelta.exchange(0);
    }
};
