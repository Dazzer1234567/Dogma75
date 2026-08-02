#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
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
        // Upper limit lifted from 100x to 1,000,000x so the user can zoom
        // all the way in to individual samples. At extreme zoom the detail
        // texture reads raw audio (see uploadWaveformDetailTexture) instead
        // of the peak pyramid, so it stays crisp down to the sample level.
        if (zoom > 1000000.0f) zoom = 1000000.0f;
        m_waveformZoom = zoom;
    }

    // Park mode
    void setSelectedPark(int park) { m_selectedPark = park; }
    int getSelectedPark() const { return m_selectedPark; }

    // Controller mode
    void setControllerMode(int mode) { m_controllerMode = mode; }
    int getControllerMode() const { return m_controllerMode; }

    // Scrub settings
    // Scrub Speed applies to the AUDIO scrub path (pad 24 + E6) — the
    // amount each encoder tick nudges the playback rate.
    void setScrubSpeed(float speed) { m_scrubSpeed = speed; }
    float getScrubSpeed() const { return m_scrubSpeed; }
    // Silent Scrub Speed applies to the NON-AUDIO scrub path (E1 alone) —
    // multiplier on how many frames each E1 tick moves the playhead.
    void setSilentScrubSpeed(float speed) { m_silentScrubSpeed = speed; }
    float getSilentScrubSpeed() const { return m_silentScrubSpeed; }
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
    // Full reset: forget the marker so a later enable places it at the
    // fresh first-time default (33/66 of loop range, etc.) rather than
    // restoring a preserved position.
    void resetMarker(int markerIndex) {
        if (markerIndex < 0 || markerIndex >= 4) return;
        m_markerEnabled[markerIndex]  = false;
        m_markerEverSet[markerIndex]  = false;
        m_markerPositions[markerIndex] = 0;
    }
    // Enable a marker: if it's never been placed, position it at the given
    // fraction of the current viewport; otherwise restore the preserved
    // position saved before it was last cleared.
    void enableMarkerAtDefault(int markerIndex, double fractionFromLeft);

    // Toggle-state accessors for session save/load.
    bool getLoopEnabled() const   { return m_loopLeftEnabled.load(); }
    void setLoopEnabled(bool on)  { m_loopLeftEnabled.store(on); m_loopRightEnabled.store(on); }
    bool getRecordEnabled() const { return m_recordLeftEnabled.load(); }
    void setRecordEnabled(bool on){ m_recordLeftEnabled.store(on); m_recordRightEnabled.store(on); }
    bool getReturnToStartOnStop() const  { return m_returnToStartOnStop.load(); }
    void setReturnToStartOnStop(bool on) { m_returnToStartOnStop.store(on); }
    // Session save/load use this to set a marker's position AND enable state
    // atomically without disturbing the "everSet" tracker's default logic.
    void setMarker(int idx, size_t position, bool enabled) {
        if (idx < 0 || idx >= 4) return;
        m_markerPositions[idx] = position;
        m_markerEnabled[idx]   = enabled;
        m_markerEverSet[idx]   = true;
    }

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
    // User finalised a track name via on-controller entry (pad 21). Applies
    // the name to the last track created via the pad-12 add flow.
    void handleTrackNameFromController(const std::string& name);
    // Live buffer update while the user is typing. `active` is false when
    // the firmware exited text-input mode without a finalise.
    void handleRenameBuffer(const std::string& buffer, int cursorPos, bool active);
    // Called on the firmware's RENAMESYNC message; snapshots the LED-flash
    // phase so the GUI can pulse the rename preview in sync with pads.
    void handleRenameSync(int phaseMs);
    // Firmware requested a full state re-push (hard-reset gesture).
    // Invalidates the LED / PAIRDEF / OLED caches and the startup-push
    // flag so updateController re-sends everything on its next tick.
    void handleResync();
    // Read side for the GUI. Returns "" and false if no rename is active.
    // Also fills `cursorPosOut` with where the DAW should overlay the cursor.
    bool  getRenameBuffer(std::string& out, int& cursorPosOut) const;
    int   getRenameTrackIndex() const { return m_pendingNameTrackIndex.load(); }
    // Current LED-flash brightness (0..1) reconstructed on the DAW side
    // from the last RENAMESYNC + elapsed wall time. Matches the physical
    // pad 20 / pad 21 LED brightness step-for-step.
    float getLedFlashBrightness() const;

    // Called from the main loop each frame. Mirrors internal state onto
    // the controller's LEDs (play LED, orange encoder-enable LED) and
    // handles the scrub-then-resume timer.
    void updateController();

    // Fast-path LED sync. Callable from ANY thread (reader thread from
    // handleTouch, main thread from updateController). Sends the LED
    // command immediately via the async writer + updates the "last
    // sent" cache so updateController's cache-diff won't re-send. Use
    // whenever the DAW-side authoritative state has just changed and
    // we want the LED to track it with reader-thread latency (~ms)
    // instead of waiting for the next frame.
    void syncPlayLedNow();
    void syncLoopPairLedsNow();
    void syncRecordPairLedsNow();

private:
    // MIDI helper methods (extracted from processMidiMessages)
    void handleJogWheel(int speed);
    void handleFaderZoom(int position, int range);
    // handleTouch dispatch helpers. Each is a self-contained handler for
    // one pad or one pad-group; they return true when they've fully
    // handled the touch so handleTouch() can early-return. Pure extraction
    // of what used to be one 340-line switch of nested ifs.
    bool touchHandleDeletePairSentinel(int pad);
    bool touchHandlePad26(bool pressed);
    bool touchHandlePad15Press();
    bool touchHandlePad12(bool pressed);
    void touchHandlePairInClearMode(int pad);   // pads 20/21/22/23 in clear-mode
    void touchHandlePairPad(int markerIdx);     // pads 20/21/22/23 in normal mode
    void* m_stream;
    std::atomic<bool> m_running;
    std::atomic<bool> m_testToneEnabled;
    std::atomic<bool> m_playing;
    std::atomic<int> m_outputStereoPair;
    std::atomic<size_t> m_playbackPosition;

    // Play/stop click-avoidance envelope. Ramps 0<->1 at PLAY_FADE_MS
    // (5 ms) whenever m_playing changes. Owned entirely by the audio
    // callback thread — no atomics needed. Step is computed once when
    // the stream's sample rate is known (in startAudio()).
    float                m_playFadeGain = 0.0f;
    float                m_playFadeStep = 0.0f;
    static constexpr float PLAY_FADE_MS = 5.0f;
    // Audio-thread-only bookkeeping for the "return to start on stop"
    // feature: the position-jump is deferred until AFTER the fade-out
    // has completed, so mid-fade playhead teleports don't reintroduce
    // the click the fade was there to prevent.
    bool                 m_prevCBPlaying     = false;
    bool                 m_returnJumpPending = false;

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
    float m_scrubSpeed;       // audio scrub (pad 24 + E6) sensitivity
    float m_silentScrubSpeed; // non-audio scrub (E1) sensitivity
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

    // Cached "pair defined" flags — updateController pushes PAIRDEF
    // messages to the firmware whenever either changes. Firmware uses
    // these to render "defined but off" as a flashing LED in clear-mode.
    int m_lastLoopPairDefinedSent   = -1;
    int m_lastRecordPairDefinedSent = -1;

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

    // Heartbeat: DAW pings the firmware every ~1 s so the firmware can tell
    // whether it's still connected. Used to reject a switch to descriptive
    // mode when the DAW isn't there to drive it.
    int64_t m_lastHeartbeatSendMs = 0;

    // Cross-thread request: pad 12 handler (reader thread) sets this flag;
    // updateController() (main thread) actually mutates m_tracks. Prevents
    // vector reallocation races with the audio callback.
    std::atomic<bool> m_pendingAddTrackRequest{false};
    // Debounce for pad 12: capacitive touch pads sometimes ripple on/off
    // when the finger is barely grazing them. Reject any add-track request
    // that arrives within ADD_TRACK_MIN_INTERVAL_MS of the previous one.
    std::atomic<int64_t> m_lastAddTrackMs{0};
    static constexpr int64_t ADD_TRACK_MIN_INTERVAL_MS = 500;
    // Index of the most-recently-added track (the one awaiting a name from
    // the controller's on-device entry flow). -1 = no pending name.
    std::atomic<int> m_pendingNameTrackIndex{-1};

    // Long-press detection for pad 12. A short press (< 1 s) creates a new
    // track; holding for >= 1 s cancels the add and instead starts a rename
    // of the currently-selected track. m_pad12PressTimeMs is 0 when pad 12
    // isn't held; m_pad12LongPressFired guards against re-firing during the
    // same hold.
    std::atomic<int64_t> m_pad12PressTimeMs{0};
    std::atomic<bool>    m_pad12LongPressFired{false};
    static constexpr int64_t RENAME_HOLD_MS = 1000;
    // Set by updateController when a long-press fires; consumed on the next
    // tick to actually start the rename OLED / TEXTIN flow.
    std::atomic<bool>    m_pendingRenameRequest{false};
    // Modifier (pad 26) + pad 12 = delete selected track. Queued from the
    // reader thread so the main thread mutates m_tracks safely.
    std::atomic<bool>    m_pendingDeleteTrackRequest{false};

    // Live rename buffer received from the firmware while the user is
    // typing a track name. Read by the GUI to render the live preview.
    // Guarded by m_renameMutex — the buffer arrives on the reader thread
    // and is read on the main thread.
    mutable std::mutex   m_renameMutex;
    std::string          m_renameBuffer;
    std::atomic<int>     m_renameCursorPos{0};
    std::atomic<bool>    m_renameActive{false};
    // LED-flash sync: local-clock ms count at which we saw RENAMESYNC,
    // and the firmware phase (0..499 ms) it reported at that instant.
    std::atomic<int64_t> m_renameSyncLocalMs{0};
    std::atomic<int>     m_renameSyncFwPhaseMs{0};
    static constexpr int FLASH_PERIOD_MS = 500;

    // Audio scrub (pad 24 held + E6). Encoder ticks nudge m_scrubPlaybackRate;
    // if no tick arrives for SCRUB_TIMEOUT_S, updateController stops the
    // scrub, zeros the rate, and syncs the visual playhead to where the
    // audio ended.
    std::atomic<double> m_lastAudioScrubMs{0.0};
    // Very short — as soon as encoder ticks stop arriving, audio stops.
    // Encoder ticks come every 10 ms while turning, so 30 ms is enough
    // margin to bridge slow rotations without introducing "coast" feel.
    static constexpr double SCRUB_TIMEOUT_S = 0.03;

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
