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
            m_markerEverSet[markerIndex] = true;
        }
    }
    size_t getMarkerPosition(int markerIndex) const {
        return (markerIndex >= 0 && markerIndex < 4) ? m_markerPositions[markerIndex] : 0;
    }
    bool isMarkerEnabled(int markerIndex) const {
        return (markerIndex >= 0 && markerIndex < 4) ? m_markerEnabled[markerIndex] : false;
    }
    bool markerEverSet(int markerIndex) const {
        return (markerIndex >= 0 && markerIndex < 4) ? m_markerEverSet[markerIndex] : false;
    }
    void clearMarker(int markerIndex) {
        if (markerIndex >= 0 && markerIndex < 4) m_markerEnabled[markerIndex] = false;
    }
    // Enable a marker: if it's never been placed, position it at the given
    // fraction of the current viewport; otherwise restore the preserved
    // position saved before it was last cleared.
    void enableMarkerAtDefault(int markerIndex, double fractionFromLeft);

    // GUI pushes the current viewport range each frame so the reader thread
    // can compute first-time marker positions relative to what's on screen.
    void setViewportRange(size_t startFrame, size_t visibleFrames) {
        m_viewStartFrame.store(startFrame);
        m_viewVisibleFrames.store(visibleFrames);
    }

    // Serial controller reference (set by main.cpp)
    void setSerialController(SerialController* sc) { m_serialController = sc; }
    SerialController* getSerialController() const { return m_serialController; }

    // Called by SerialController callbacks (via main.cpp wiring)
    void handleEncoderDelta(int encoder, long delta, float rpm, float velocityMultiplier);
    void handleTouch(int pad, bool pressed);
    // Firmware tells us its display mode. In diagnostic mode we ignore all
    // touch/encoder inputs so the controller does not affect DAW state —
    // it's purely for the user to identify which button is which.
    void handleModeChange(bool descriptive);

    // Called from the main loop each frame. Mirrors internal state onto
    // the controller's LEDs (play LED, orange encoder-enable LED) and
    // handles the scrub-then-resume timer.
    void updateController();

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
    bool m_markerEverSet[4] = {false, false, false, false};

    // Current viewport snapshot (pushed by GUI each frame via
    // setViewportRange). Used to compute first-time default marker positions
    // as a fraction of what's on screen.
    std::atomic<size_t> m_viewStartFrame{0};
    std::atomic<size_t> m_viewVisibleFrames{0};

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

    // Cached LED-3 (play) state. Compared against live isPlaying() in
    // updateController() so we only send corrections when the firmware's
    // predictive toggle diverges from what the DAW knows to be true.
    // Starts at -1 (unknown) so the first tick after DAW startup always
    // force-syncs the LED to actual play state — needed because the user
    // can toggle LED 3 locally on the controller while the DAW is down.
    int m_lastPlayLedState = -1;

    // Loop-left mode (pad 20 / LED 4). When enabled AND marker 0 is set,
    // pressing play jumps to marker 0 before starting playback.
    std::atomic<bool> m_loopLeftEnabled{false};
    int m_lastLoopLeftLedState = -1;  // same -1 startup-sync trick as LED 3

    // The other three mode toggles. Their LEDs mirror DAW state the same way
    // as loop-left; behavior is TBD (specify what each mode should do when
    // enabled and we'll wire it in the same place as loop-left's jump-to-marker).
    std::atomic<bool> m_recordLeftEnabled{false};    // pad 21 / LED 5 (marker 1)
    std::atomic<bool> m_recordRightEnabled{false};   // pad 22 / LED 6 (marker 2)
    std::atomic<bool> m_loopRightEnabled{false};     // pad 23 / LED 7 (marker 3)
    int m_lastRecordLeftLedState  = -1;
    int m_lastRecordRightLedState = -1;
    int m_lastLoopRightLedState   = -1;

    // Clear-markers mode. Firmware runs a continuous fade-flash on LEDs
    // 3/4/5/6/7 while this is true; pad presses re-route to marker clearing
    // instead of the normal toggles. Entered by modifier + play (pad 26 held
    // + pad 19 pressed), exited by tapping pad 26 again.
    std::atomic<bool> m_clearMode{false};

    // Pad 24 pan-modifier. While held, E2 pans the timeline instead of
    // zooming. Firmware lights LED 8 directly on press/release.
    std::atomic<bool> m_panModifierHeld{false};

    // Pad 3 held state — combines with pad 23 to toggle the OLED display
    // mode on the controller (firmware also tracks this locally). While
    // pad 3 is down, a pad 23 press does NOT toggle loop-right on the DAW
    // side either — it's consumed by the display-mode toggle.
    std::atomic<bool> m_pad3Held{false};

    // Pad 14 / pad 15 held state. Combined with pad 19 (play) to set the
    // on-stop-return-to-start flag: 14+19 turns it ON, 15+19 turns it OFF.
    // These are absolute set commands, not toggles.
    std::atomic<bool> m_pad14Held{false};
    std::atomic<bool> m_pad15Held{false};

    // When true, stopping playback (for any reason) returns the playhead to
    // wherever it was when play() started. Captured in play() as
    // m_playStartPosition and applied in updateController() on the play->
    // stop transition.
    std::atomic<bool>   m_returnToStartOnStop{false};
    std::atomic<size_t> m_playStartPosition{0};
    bool                m_wasPlayingLastTick = false;

    // Timed OLED revert. Set to a future steady-clock ms count when a
    // firmware-controlled banner (e.g. display-mode switch) needs to auto-
    // revert to the current playback state after some delay. 0 = no revert
    // pending. Checked every updateController() tick.
    std::atomic<int64_t> m_oledRevertAtMs{0};

    // Firmware-announced display mode. When true, DAW ignores every
    // touch/encoder input — the controller is being used purely as a
    // button-name identifier and must not affect DAW state. Defaults to
    // true because the firmware boots in diagnostic mode; a MODE:DESC
    // message flips it to false as soon as the DAW connects.
    std::atomic<bool> m_diagnosticMode{true};

    // One-shot: push the initial playback state ("STOPPED") to the OLED
    // once, as soon as the DAW is fully up. Firmware silently ignores this
    // in diagnostic mode (its own DIAG header remains), so it only affects
    // the descriptive-mode display.
    bool m_startupOledPushed = false;

    // Push "PLAYING" / "STOPPED" onto the OLED (both display modes) via
    // DISPFORCE so the revert works regardless of diag/desc setting.
    void pushPlaybackStateToOled();

public:
    // Push a full two-line OLED state. Either line can be an empty string /
    // single space to blank it. Sends both lines so the screen never carries
    // a stale mixed message from a previous event. Truncated to 21 chars
    // per line by the firmware. Silently drops if there's no serial link.
    void oledShow(const char* line1, const char* line2);
    // Same, but held on screen for ~3 s in descriptive mode (ignored in
    // diagnostic mode). Any subsequent touch/encoder activity clears the
    // hold immediately.
    void oledShowHold(const char* line1, const char* line2);
    // Force-write both lines regardless of display mode. For interactive
    // modes where the OLED is a live status board (e.g. clear-markers).
    void oledShowForce(const char* line1, const char* line2);

    // Scrub-then-resume: E1 movement while playing pauses playback and
    // arms a resume that fires 100 ms after the last movement.
    std::atomic<bool> m_scrubResumePending{false};
    std::atomic<double> m_lastScrubMoveTime{0.0};  // steady_clock seconds
    static constexpr double SCRUB_RESUME_DELAY_S = 0.100;

    // Serial controller (owned by main, not by us)
    SerialController* m_serialController;

public:
    bool isModifierHeld() const { return m_modifierHeld.load(); }
    long consumeViewScrollDelta() {
        return m_viewScrollDelta.exchange(0);
    }
};
