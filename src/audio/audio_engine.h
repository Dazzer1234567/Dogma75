#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <deque>
#include <functional>
#include "track.h"
#include "../controller/serial_controller.h"

class OscSender;
class AntelopeClient;

// ---- Undo history ----
// One entry captures the "user-visible session state" before an action ran.
// Audio content (per-track audioData / peak pyramid) is intentionally not
// snapshotted — undo only restores metadata + transport + view state.
struct UndoTrackState {
    std::string name;
    std::string filePath;
    int         channels;
    float       volume;
    float       pan;
    bool        muted;
    bool        solo;
    bool        armed;
    bool        inputMonitor;
    int         outputPair;
    int         outputMonoChan;
    bool        outputMono;
    int         outputLeftChan;
    int         outputRightChan;
    int         inputPair;
    int         inputMonoChan;
    bool        inputMono;
    int         inputLeftChan;
    int         inputRightChan;
    int         color;
    // Optional audio snapshot — populated ONLY for tracks that are about
    // to have their audio replaced (record start, clear, load). Otherwise
    // stays empty so undo doesn't pay the memory cost for every action.
    bool               hasAudioSnapshot = false;
    std::vector<float> audioData;
};
struct UndoBookmark {
    size_t      frame;
    std::string name;
};
struct UndoEntry {
    // Stable monotonic id — lets finaliseRecording find its originating
    // entry after stop even if the deque has shifted underneath it.
    int    id = 0;
    // WAV paths produced by the recording take this entry captured the
    // pre-record state of. Filled in by finaliseRecording; deleted from
    // disk on undoPop so an unintentional take doesn't leave garbage.
    std::vector<std::string> recordedWavPaths;
    std::vector<UndoTrackState> tracks;
    std::vector<UndoBookmark>   bookmarks;
    int    selectedTrack     = -1;
    size_t markerPositions[4] = {0,0,0,0};
    bool   markerEnabled[4]   = {false,false,false,false};
    bool   markerEverSet[4]   = {false,false,false,false};
    bool   loopEnabled        = false;
    bool   recordEnabled      = false;
    bool   returnToStartOnStop= false;
    bool   playing            = false;
    size_t playbackPosition   = 0;
    // GUI-owned view state — filled / restored via callbacks so
    // AudioEngine doesn't need to know GUIManager internals.
    float  guiDisplayZoom          = 1.0f;
    size_t guiViewCenterPosition   = 0;
    size_t guiTimelineFrames       = 0;
};

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
    // True while a take is being captured. Set on play() when any track
    // is armed, cleared on stop().
    bool isRecording() const { return m_recordActive.load(); }
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
    // Input channel count comes straight from the ASIO device's advertised
    // max input channels. We don't open an input stream yet — this only
    // exists so the track UI can offer an input-pair dropdown that gets
    // persisted per-track for future record wiring.
    int getNumInputStereoPairs() const { return m_maxInputChannels / 2; }
    // Per-input user-editable names (routing page). Empty string until
    // the user sets one; the routing UI falls back to "Input N" for
    // any name that's blank.
    const std::string& getInputName(int chan) const;
    void setInputName(int chan, const std::string& name);
    // Per-output user-editable names (routing page). Same shape as
    // input names: empty until set, UI falls back to "Output N".
    const std::string& getOutputName(int chan) const;
    void setOutputName(int chan, const std::string& name);

    // ---- Routing (patchbay) — per-track ----
    // Each Track owns its own routing graph (see Track::routingNodes /
    // routingInputCables / routingOutputCables). Audio callback and UI
    // both touch these; access is guarded by m_routingMutex.
    struct RoutingLock { std::unique_lock<std::mutex> lk; };
    RoutingLock lockRouting() const;
    // Resolves the track's input-cable node refs to hardware channels,
    // in stem order. Empty when the track has no input cables.
    void trackInputChannels(int trackIdx, std::vector<int>& outChans) const;
    // Per-stem hardware output list — one entry per output cable.
    struct StemOutputRoute { int stemIdx; int hwChan; };
    void trackOutputRoutes(int trackIdx,
                           std::vector<StemOutputRoute>& outRoutes) const;
    // True if the track has any routing cabling at all.
    bool trackHasRouting(int trackIdx) const;

    // Track management
    int addTrack(const std::string& name = "");
    void deleteTrack(int trackIndex);
    int getTrackCount() const { return static_cast<int>(m_tracks.size()); }
    Track* getTrack(int trackIndex);
    const Track* getTrack(int trackIndex) const;
    // Current instantaneous peak (0..1) for a track's channel-strip
    // meter. Meters whatever the track is set to hear: input signal if
    // inputMonitor is on, playback output otherwise. Callers apply
    // their own decay/smoothing when displaying.
    float getTrackMeter(int trackIndex) const;
    // Instantaneous peak level (0..1) for a hardware input channel.
    // Returns 0 for out-of-range or when no input stream is active.
    // Routing-page state (set from GUIManager each frame). When true,
    // controller pads 0/4 stop navigating tracks and instead queue a
    // nav intent for the GUI to consume (moves the routing canvas's
    // Input-node selection up/down by vertical position).
    void setRoutingPageActive(bool active) { m_routingPageActive.store(active); }
    bool getRoutingPageActive() const      { return m_routingPageActive.load(); }
    int  consumeRoutingNavIntent()         { return m_routingNavIntent.exchange(0); }
    float getInputMeter(int chan) const {
        if (chan < 0 || chan >= (int)m_inputMeters.size()) return 0.0f;
        const auto& m = m_inputMeters[chan];
        return m ? m->load() : 0.0f;
    }
    float getInputMeterRMS(int chan) const {
        if (chan < 0 || chan >= (int)m_inputMetersRMS.size()) return 0.0f;
        const auto& m = m_inputMetersRMS[chan];
        return m ? m->load() : 0.0f;
    }
    // Per-track per-stem playback peak (updated every audio callback,
    // reads 0 when not playing).
    float getTrackStemMeter(int trackIdx, int stem) const {
        if (trackIdx < 0 || trackIdx >= (int)m_trackStemPeaks.size()) return 0.0f;
        const auto& row = m_trackStemPeaks[trackIdx];
        if (stem < 0 || stem >= (int)row.size()) return 0.0f;
        return row[stem] ? row[stem]->load() : 0.0f;
    }
    int getSelectedTrack() const { return m_selectedTrack; }
    void setSelectedTrack(int trackIndex) { m_selectedTrack = trackIndex; }
    bool loadTrackAudio(int trackIndex, const std::string& filepath);
    // Wipe audio from a track but keep the strip (name, mixer state, etc.).
    // Used by the Ctrl+A "clear audio" hotkey.
    void clearTrackAudio(int trackIndex);
    // GUI plumbing: tells the engine where the current session lives so
    // recorded takes can be written to <sessionDir>/recordings/*.wav.
    // Passing an empty string reverts to the default <exe>/recordings path.
    void setSessionDir(const std::string& dir);

    // Bookmarks — lightweight positional markers dropped by pad 13. Each
    // release of pad 13 appends the current playhead position with an
    // empty name and immediately opens the controller's text-input mode
    // so the user can type a name (same flow as track rename).
    struct Bookmark {
        size_t      frame;
        std::string name;
    };
    std::vector<Bookmark> getBookmarks() const {
        std::lock_guard<std::mutex> lock(m_bookmarksMutex);
        return m_bookmarkFrames;
    }
    void clearBookmarks() {
        std::lock_guard<std::mutex> lock(m_bookmarksMutex);
        m_bookmarkFrames.clear();
    }
    // Loader entry point — appends a fully-formed bookmark. Used by
    // session load; doesn't touch the naming flow.
    void addBookmark(size_t frame, const std::string& name) {
        std::lock_guard<std::mutex> lock(m_bookmarksMutex);
        m_bookmarkFrames.push_back({frame, name});
    }
    // Which bookmark index is currently being named via the controller;
    // -1 when none. GUI uses this to render the live-typing preview at
    // that bookmark's triangle instead of at a track row.
    int getRenameBookmarkIndex() const { return m_pendingNameBookmarkIndex.load(); }
    // The bookmark currently selected in scroll mode (index into
    // m_bookmarkFrames, or -1). The GUI renders a full-height white
    // vertical line at its frame position while nav is active so E3
    // adjustments are easy to see across the whole arrangement.
    int getSelectedBookmarkIndex() const { return m_bookmarkNavIdx; }
    bool isBookmarkScrollMode() const { return m_bookmarkScrollMode.load(); }
    // Pad-13-held + encoder-1 bookmark navigation posts a request here
    // for the GUI to reposition its view around a specific frame. The
    // GUI consumes the value (resets to -1) once per frame.
    int64_t consumeRequestedJumpFrame() {
        return m_requestedJumpFrame.exchange(-1);
    }
    // Companion to consumeRequestedJumpFrame: when >= 0, the GUI should
    // pan the view so the target frame appears at the same screen X as
    // this anchor frame WAS before the jump (i.e. shift by target -
    // anchor). Only used by the loop/punch double-tap path; regular
    // bookmark nav leaves this at -1 and gets the default "15% from
    // left" reposition.
    int64_t consumeRequestedJumpAnchorFrame() {
        return m_requestedJumpAnchorFrame.exchange(-1);
    }
    // GUI pushes the arrangement's timeline extent so encoder scrub /
    // pan / zoom can work even when no track has any audio yet (fresh
    // session). Falls back to getTotalFrames() when nothing has been
    // pushed. Thread-safe (atomic).
    void   setTimelineFrames(size_t frames) { m_timelineFramesHint.store(frames); }
    size_t getTimelineFrames() const {
        size_t hint = m_timelineFramesHint.load();
        size_t audioMax = getTotalFrames();
        return audioMax > hint ? audioMax : hint;
    }

    // ---- Undo ----
    // GUI registers a pair of hooks that fill / restore the view-state
    // fields on an UndoEntry (zoom, scroll, timeline extent). AudioEngine
    // calls these while it captures / restores its own fields.
    using UndoGuiCapture = std::function<void(UndoEntry&)>;
    using UndoGuiRestore = std::function<void(const UndoEntry&)>;
    void setUndoGuiHooks(UndoGuiCapture cap, UndoGuiRestore res) {
        m_undoGuiCapture = std::move(cap);
        m_undoGuiRestore = std::move(res);
    }
    // Two independent undo histories:
    //   CONTENT — tracks, mixer state, bookmarks, mute state, arm state.
    //             What the user REC'd / added / deleted / edited.
    //   VIEW    — playback position, marker positions (loop/punch), zoom,
    //             view center, timeline frames. Where the eye is looking
    //             and where the play line / markers are placed on the
    //             timeline. Doesn't touch audio content or track state.
    //
    // Split so a "I moved my markers by mistake" undo doesn't discard the
    // recording the user did between now and then, and vice versa.
    void undoSnapshot();                // shorthand for undoSnapshot(CONTENT)
    void undoSnapshotView();
    // Same as undoSnapshotView but suppressed if we already snapshotted
    // within the last VIEW_SNAPSHOT_COALESCE_MS — collapses a scrub /
    // zoom / pan gesture (dozens of encoder ticks) into a single stack
    // entry so view-undo pops walk back gestures, not individual ticks.
    void undoSnapshotViewIfBurst();
    // Copy the current audio buffer of every armed track into the top
    // (content) undo entry so a later undo can restore the pre-recording
    // take. Called from play() when a take is starting.
    void undoStashArmedTrackAudio();
    // Same for a single track index — used by clear-audio / load-audio
    // paths where the caller has already snapshotted metadata and now
    // wants the pre-change bytes attached.
    void undoStashTrackAudio(int trackIndex);
    // Pop most recent CONTENT snapshot; leaves view/playhead/markers alone.
    // Wired to pad 36 tap-release. Returns true if something was restored.
    bool undoPop();                     // pops CONTENT stack
    // Pop most recent VIEW snapshot; leaves tracks/mixer/bookmarks alone.
    // Wired to pad 36 held + pad 20/21/22/23 press.
    bool undoPopView();

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
            markSessionDirty();
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
        if (markerIndex >= 0 && markerIndex < 4) {
            m_markerEnabled[markerIndex] = false;
            markSessionDirty();
        }
    }
    // Full reset: forget the marker so a later enable places it at the
    // fresh first-time default (33/66 of loop range, etc.) rather than
    // restoring a preserved position.
    void resetMarker(int markerIndex) {
        if (markerIndex < 0 || markerIndex >= 4) return;
        m_markerEnabled[markerIndex]  = false;
        m_markerEverSet[markerIndex]  = false;
        m_markerPositions[markerIndex] = 0;
        markSessionDirty();
    }
    // Enable a marker: if it's never been placed, position it at the given
    // fraction of the current viewport; otherwise restore the preserved
    // position saved before it was last cleared.
    void enableMarkerAtDefault(int markerIndex, double fractionFromLeft);

    // Toggle-state accessors for session save/load.
    bool getLoopEnabled() const   { return m_loopLeftEnabled.load(); }
    // Whether playback wraps at the loop markers. Separate from the markers
    // being enabled, so looping can be switched off (double-tap pad 15)
    // without losing the loop points. Defaults ON, which preserves the old
    // behaviour where enabling the loop pair made playback loop.
    bool getLoopPlaybackEnabled() const { return m_loopPlaybackEnabled.load(); }
    void setLoopPlaybackEnabled(bool on) { m_loopPlaybackEnabled.store(on); }
    void setLoopEnabled(bool on)  { m_loopLeftEnabled.store(on); m_loopRightEnabled.store(on); markSessionDirty(); }
    bool getRecordEnabled() const { return m_recordLeftEnabled.load(); }
    void setRecordEnabled(bool on){ m_recordLeftEnabled.store(on); m_recordRightEnabled.store(on); markSessionDirty(); }
    bool getReturnToStartOnStop() const  { return m_returnToStartOnStop.load(); }
    void setReturnToStartOnStop(bool on) { m_returnToStartOnStop.store(on); markSessionDirty(); }
    // Session save/load use this to set a marker's position AND enable state
    // atomically without disturbing the "everSet" tracker's default logic.
    void setMarker(int idx, size_t position, bool enabled) {
        if (idx < 0 || idx >= 4) return;
        m_markerPositions[idx] = position;
        m_markerEnabled[idx]   = enabled;
        m_markerEverSet[idx]   = true;
        markSessionDirty();
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
    // Periodic (non-destructive) LED / mode-flag re-assert. Called from
    // updateController every RESYNC_PERIOD_MS to correct any drift
    // between the DAW's shadow model and the firmware's actual LED /
    // mode state. Unlike handleResync this does NOT wipe held-state
    // trackers (which reflect real physical pad state).
    void periodicControllerResync();
    // Wall-clock ms at which we last did a periodic resync push. Used
    // by updateController to fire it every RESYNC_PERIOD_MS.
    int64_t m_lastPeriodicResyncMs = 0;
    static constexpr int64_t RESYNC_PERIOD_MS = 2000;
    // Read side for the GUI. Returns "" and false if no rename is active.
    // Also fills `cursorPosOut` with where the DAW should overlay the cursor.
    bool  getRenameBuffer(std::string& out, int& cursorPosOut) const;
    int   getRenameTrackIndex() const { return m_pendingNameTrackIndex.load(); }
    // Current LED-flash brightness (0..1) reconstructed on the DAW side
    // from the last RENAMESYNC + elapsed wall time. Matches the physical
    // pad 20 / pad 21 LED brightness step-for-step.
    float getLedFlashBrightness() const;

    // Channel-entry read side for the GUI. Returns the current mode
    // (0=off, 1=input, 2=output) and populates `bufOut` with the live
    // digit buffer formatted like "1 & 7" (or "_" if empty). Also
    // returns a 0..1 brightness for the red pulse fill so the input /
    // output field can throb in step with the LED-8 flash.
    int   getChannelEntryMode() const { return m_channelEntryMode.load(); }
    bool  getChannelEntryBuffer(std::string& bufOut) const;
    float getChannelEntryPulse() const;

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
    // Pushes the selected track's mute / solo / arm state to LEDs 0 / 1
    // / 2. Called on toggle and on track-change so the physical LEDs
    // always mirror the DAW's per-track flags.
    void syncSoloMuteLedsNow();
    // Push LED 9 to match the selected track's inputMonitor flag. Called
    // on pad 25 press, track select, and every updateController tick so
    // GUI "I" button toggles and track switches stay reflected.
    void syncInputMonitorLedNow();
    // Push MUTEFLASH:1 / :0 to the firmware whenever marker-scroll
    // mode transitions, so LED 0 flashes while nav is active.
    void syncMuteFlashNow();

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
    // Pad 13 held + pad 0/4: step through bookmarks in frame order.
    // direction = +1 (next / rightward) or -1 (previous / leftward).
    // No wrap — clamps at both ends. On first nav press after a fresh
    // session, jumps to the bookmark nearest the current playhead.
    void navigateBookmarks(int direction);
    void* m_stream;
    std::atomic<bool> m_running;
    std::atomic<bool> m_testToneEnabled;
    std::atomic<bool> m_playing;
    std::atomic<int> m_outputStereoPair;
    std::atomic<size_t> m_playbackPosition;
    // Peak meter — audio callback writes the max |sample| of each block
    // into m_lastPeak. GUI reads via getLastPeak() to display a level +
    // clip indicator. Reset via clearPeak() after a display flush.
public:
    float getLastPeak() const     { return m_lastPeak.load(); }
    void  clearPeak()             { m_lastPeak.store(0.0f); }
    // Session-dirty flag. Set from every mutation point that would
    // otherwise be lost on a "New Session" without save. Cleared by
    // saveSession/loadSessionFromFile after a successful round-trip.
    bool  isSessionDirty() const  { return m_sessionDirty.load(); }
    void  markSessionDirty()      { m_sessionDirty.store(true); }
    void  clearSessionDirty()     { m_sessionDirty.store(false); }
private:
    std::atomic<float> m_lastPeak{0.0f};
    std::atomic<bool>  m_sessionDirty{false};

    // Master output gain applied to the mixed L/R sums right before they
    // hit the PortAudio buffer. Default 0.5 (-6 dB) — safe headroom for
    // ~4-8 stems summed at unity without slamming the output.
public:
    float getMasterGain() const           { return m_masterGain.load(); }
    void  setMasterGain(float g) {
        if (g < 0.0f)  g = 0.0f;
        if (g > 2.0f)  g = 2.0f;   // +6 dB ceiling; user rarely wants more
        m_masterGain.store(g);
    }
private:
    std::atomic<float>  m_masterGain{0.5f};

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
    int m_maxInputChannels = 0;
    mutable std::vector<std::string> m_inputNames;
    mutable std::vector<std::string> m_outputNames;
    // Guards read/write access to Track::routingNodes / routingInputCables
    // / routingOutputCables from both the UI thread and the audio thread.
    mutable std::mutex        m_routingMutex;

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

    // Record slots — one per track, parallel to m_tracks. Stored via
    // unique_ptr because the slot contains a mutex, and putting a mutex
    // directly in Track would forbid the vector-of-Track from moving on
    // reallocation. Slots stay allocated across takes; buffers get reset
    // at record-start and drained at finalise.
    struct RecordSlot {
        std::vector<float> buffer;
        std::mutex         mutex;
        int                channels = 0;   // 1 (mono) or 2 (stereo) this take
    };
    std::vector<std::unique_ptr<RecordSlot>> m_recordSlots;

    // Per-track peak meter, parallel to m_tracks. Written by the audio
    // thread (peak-detect over each callback's samples — input signal
    // when the track's monitor is on, playback when off) and read by
    // the GUI. Stored via unique_ptr because atomic<float> is neither
    // copyable nor movable, so a bare vector wouldn't support push_back.
    std::vector<std::unique_ptr<std::atomic<float>>> m_trackMeters;
    // Per-track per-stem playback peak meters — set each callback from
    // the actual samples routed through the track's routing (mixer
    // faders applied). Read by the routing page's mixer meters.
    // Fixed 32-slot rows per track; stems beyond that read 0.
    static constexpr int STEM_METERS_MAX = 32;
    std::vector<std::vector<std::unique_ptr<std::atomic<float>>>> m_trackStemPeaks;
    // Scratch used by the audio callback to gather per-stem peaks
    // across the buffer before storing them atomically at the end.
    mutable std::vector<std::vector<float>>                       m_cbStemPeaks;
    // Routing page focus + nav intent (set by pad 0/4 when the page
    // is active, consumed by GUIManager).
    std::atomic<bool> m_routingPageActive{false};
    std::atomic<int>  m_routingNavIntent{0};   // -1 up, +1 down, 0 none
    // Per-input-channel instantaneous peaks + per-buffer RMS —
    // populated each buffer in the audio callback, read by the routing
    // page for its meters (peak marker + RMS bar respectively).
    std::vector<std::unique_ptr<std::atomic<float>>> m_inputMeters;
    std::vector<std::unique_ptr<std::atomic<float>>> m_inputMetersRMS;

    std::atomic<bool>  m_recordActive{false};   // callback captures while true
    // If the record pair (markers 1 & 2) is enabled when a take begins,
    // capture is gated to the frame range [recLeft, recRight) — the
    // callback drops any input samples whose absolute playhead position
    // falls outside, and the snapshot tick overlays samples onto
    // audioData starting at recLeft. Zero-length range disables gating
    // (unbounded take from m_playStartPosition).
    std::atomic<size_t> m_recordGateStart{0};
    // Round-trip latency between a sample leaving the DAW and the same
    // moment's captured audio arriving in the callback (sum of ASIO's
    // reported input+output latency, in frames). Populated when the
    // audio stream opens; zero when no stream is running.
    std::atomic<size_t> m_recordLatencyFrames{0};
    // Countdown of leading input frames to drop after record-start.
    // Armed to m_recordLatencyFrames when a take begins so the first
    // pushed sample represents mic audio from the moment the reference
    // reached recordStartFrame — anything earlier is in-flight lag.
    std::atomic<size_t> m_recordLeadingSkipRemaining{0};
    std::atomic<size_t> m_recordGateEnd{0};

    // Bookmark positions (each pad-13 release appends the playhead).
    mutable std::mutex     m_bookmarksMutex;
    std::vector<Bookmark>  m_bookmarkFrames;
    // Index of the bookmark whose name the controller is currently
    // typing. -1 when we're not naming a bookmark.
    std::atomic<int>       m_pendingNameBookmarkIndex{-1};
    // True when the pending name is for a JUST-CREATED bookmark (pad
    // 13 tap), false for a long-press rename of an existing one. Determines
    // whether a repeat pad-13 press cancels by ALSO deleting the fresh
    // bookmark, or just closes the naming dialog.
    std::atomic<bool>      m_pendingBookmarkIsFreshAdd{false};
    // Pad-13 as modifier: while held, encoder 1 walks through markers
    // instead of scrubbing. Modifier flag suppresses the "release adds
    // a new bookmark" behaviour when the hold was actually used to
    // navigate. Scroll mode is a STICKY variant — once the encoder has
    // been turned during the hold we stay in nav mode after release,
    // and pad 13's next press exits nav mode (without adding a marker).
    std::atomic<bool>      m_pad13Held{false};
    std::atomic<bool>      m_pad13UsedAsModifier{false};
    // Long-press on pad 13 (2 s hold) opens a rename flow for the
    // bookmark at the exact playhead frame — mirrors pad 12's
    // add-vs-rename split. Timer starts on press, cleared on any nav
    // use of the hold (pad 0/4 combo) so a modifier hold doesn't
    // fire rename underneath it.
    std::atomic<int64_t>   m_pad13PressTimeMs{0};
    std::atomic<bool>      m_pad13LongPressFired{false};
    // Queued from the reader thread's long-press detection; consumed by
    // updateController on the main thread to open the bookmark rename.
    std::atomic<bool>      m_pendingBookmarkRenameRequest{false};
    std::atomic<bool>      m_bookmarkScrollMode{false};
    // Selection while navigating: index into m_bookmarkFrames (original
    // insertion order). Reset to -1 outside navigation sessions. Encoder
    // 3 uses this to know WHICH marker to move.
    int                    m_bookmarkNavIdx = -1;
    // Encoder-tick accumulator for bookmark nav — 200 ticks per step so
    // a small nudge doesn't jump multiple markers.
    long                   m_bookmarkNavAccum = 0;
    // Set by encoder-driven bookmark nav; GUI consumes and repositions
    // its view so this frame sits 15% from the left of the arrangement.
    std::atomic<int64_t>   m_requestedJumpFrame{-1};
    // Optional companion — see consumeRequestedJumpAnchorFrame() docs.
    std::atomic<int64_t>   m_requestedJumpAnchorFrame{-1};
    // Double-tap detection state for the loop/punch pair pads (20-23).
    // Same pad tapped twice within 400 ms → recenter the view on the
    // marker, not just jump the playhead.
    // m_lastPairPadAnchor: playhead position BEFORE the last tap moved
    // it. On a double-tap the anchor is what we need — the position at
    // tap 1, since tap 1's playhead-move has already run by the time
    // we're in tap 2 and current playhead == marker.
    int                    m_lastPairPadIdx    = -1;
    double                 m_lastPairPadTapSec = 0.0;
    size_t                 m_lastPairPadAnchor = 0;
    // True from stop() until finaliseRecording completes. Callback keeps
    // skipping armed tracks during this window so a concurrent WAV read
    // or a late snapshot tick can't race with playback of the same buffer.
    std::atomic<bool>  m_finalising{false};
    // Serialises writes to armed-track audioData from the snapshot tick
    // and reads from finaliseRecording. Callback doesn't touch this mutex
    // — the m_finalising flag keeps it out of the danger zone entirely.
    std::mutex         m_recordAudioMutex;
    // Wall-clock timestamp of the last live-record snapshot into track.audioData,
    // used to rate-limit the copy + peak-pyramid rebuild in updateController.
    int64_t            m_lastRecordSnapshotMs = 0;

    // ---- Undo state ----
    static constexpr size_t UNDO_MAX = 50;
    std::deque<UndoEntry>   m_undoStack;      // CONTENT history
    std::deque<UndoEntry>   m_undoStackView;  // VIEW history — playhead, markers, zoom, view center
    std::mutex              m_undoMutex;
    // Wall-clock ms of the last view-activity call to undoSnapshotViewIfBurst.
    // Activity-based (updated on every call, not just successful snapshots)
    // so a continuous burst produces exactly one entry — the pre-burst state.
    int64_t                 m_lastViewActivityMs = 0;
    static constexpr int64_t VIEW_SNAPSHOT_COALESCE_MS = 400;
    // Monotonic id assigned to each new entry. Never reused so the
    // finalise-recording thread can always locate its entry, or safely
    // skip if the deque has since dropped it.
    int                     m_nextUndoEntryId       = 1;
    // Id of the undo entry that owns the pre-record audio for the
    // currently-running take. Zero when no take is running.
    int                     m_recordingUndoEntryId  = 0;
    UndoGuiCapture          m_undoGuiCapture;
    UndoGuiRestore          m_undoGuiRestore;
    // Per-encoder coalescing: an encoder is treated as continuing the
    // same "burst" if a new delta arrives within 500 ms of the previous
    // one. Only the FIRST delta of a burst pushes an undo snapshot.
    int64_t                 m_encoderLastDeltaMs[6] = {0,0,0,0,0,0};
    // Guard so pad 26 tap-release fires undo only when the modifier
    // wasn't used to gate another action during the hold.
    bool                    m_pad26Consumed = false;
    // Directory where new WAV takes are written on finalise. Populated by
    // GUIManager via setSessionDir() whenever the session path changes;
    // empty falls back to <exe>/recordings.
    std::mutex   m_sessionDirMutex;
    std::string  m_sessionDir;
    // GUI-pushed arrangement extent hint (samples). Never authoritative —
    // getTimelineFrames() returns max(this, actual max track frames).
    std::atomic<size_t> m_timelineFramesHint{0};
    // Ensures record slots exist for every track (called from addTrack /
    // deleteTrack / load). Grows m_recordSlots to match m_tracks.size().
    void ensureRecordSlots();
    // Flushes any tracks with a populated recordBuffer to WAV on stop.
    // Called after m_recordActive is cleared, on a worker thread.
    void finaliseRecording();

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
    // Set by the pad-36 undo handler, consumed by updateController() so the
    // "UNDO" banner is written AFTER undoPop's forced playback-state push
    // rather than racing it.
    std::atomic<bool> m_pendingUndoOled{false};
    // Pad 36 (big undo pad) held-state + combo fired flag. The tap-
    // release path fires CONTENT undo unless a held-combo (pad 36 held
    // while pressing pad 20/21/22/23 for VIEW undo) already consumed
    // the press.
    std::atomic<bool> m_pad36Held{false};
    std::atomic<bool> m_pad36ComboFired{false};
    // Pair-pad (20/21/22/23) tap-on-release state. Mirrors pad 36's
    // pattern: press just arms; release fires touchHandlePairPad ONLY
    // if the press was seen AND no combo consumed it during the hold.
    // Indexed 0..3 for pads 20/21/22/23 respectively.
    std::atomic<bool> m_pairPadPressSeen[4]{};
    std::atomic<bool> m_pairPadComboFired[4]{};

    // Zoom lock. Held pad 23 + turn E6 toggles the lock. While locked,
    // E2 zoom is a no-op (E2 pan via pad 24 still works). Persist state
    // is intentionally in-memory only — the lock always starts OFF on
    // fresh session/DAW launch to avoid a confused "why can't I zoom".
    // m_zoomLockToggledThisHold prevents a single pad-23 hold from
    // toggling multiple times if the user turns E6 several ticks.
public:
    bool isZoomLocked() const { return m_zoomLocked.load(); }
private:
    std::atomic<bool> m_zoomLocked{false};
    std::atomic<bool> m_zoomLockToggledThisHold{false};
    // Wall-clock ms at which pad 23 was last released. Used to
    // suppress E6 marker-nudge for a short window after — otherwise
    // any residual encoder motion right after releasing pad 23 (from
    // the same finger that was mid-turn during the zoom-lock combo)
    // would accidentally nudge the loop-right marker.
    std::atomic<int64_t> m_pad23ReleaseMs{0};
    static constexpr int64_t PAD23_MARKER_LOCKOUT_MS = 500;
    // Same idea for pad 22 (held while turning E5 moves a bookmark).
    // Prevents E5 from nudging marker 2 (punch-R) for a brief window
    // after pad 22 release.
    std::atomic<int64_t> m_pad22ReleaseMs{0};
    static constexpr int64_t PAD22_MARKER_LOCKOUT_MS = 500;
    // Loop playback (wrap-around) on/off, toggled by double-tapping pad 15.
    // Defaults true so existing sessions behave exactly as before.
    std::atomic<bool> m_loopPlaybackEnabled{true};
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

    // Mute (pad 18 / LED 0), solo (pad 17 / LED 1), record-arm (pad 16 /
    // LED 2) mirror the selected track's per-track flags. Cached so we
    // only send LED updates when the effective state actually changes
    // (track switch or toggle).
    int m_lastSoloLedState = -1;
    int m_lastMuteLedState = -1;
    int m_lastArmLedState  = -1;
    int m_lastInputMonitorLedState = -1;   // LED 9 shadow (see syncInputMonitorLedNow)
    // Cached last MUTEFLASH state — cheap tick in updateController
    // re-syncs when scroll mode transitions.
    int m_lastMuteFlashState = -1;

    // Clear-markers mode. Firmware runs a continuous fade-flash on LEDs
    // 3/4/5/6/7 while this is true; pad presses re-route to marker clearing
    // instead of the normal toggles. Entered by modifier + play (pad 26 held
    // + pad 19 pressed), exited by tapping pad 26 again.
    std::atomic<bool> m_clearMode{false};

    // Pad 24 pan-modifier. While held, E2 pans the timeline instead of
    // zooming. Firmware lights LED 8 directly on press/release.
    std::atomic<bool> m_panModifierHeld{false};
    // Pad-24 toggled "Vol/Pan" mode: while active, encoders E1/E2 stop
    // their normal jobs and instead adjust the selected track's volume
    // (E1) and pan (E2). LED 8 flashes at ~2 Hz to signal the mode.
    std::atomic<bool> m_volPanMode{false};
    int64_t           m_volPanFlashLastMs = 0;
    bool              m_volPanFlashOn     = false;
    // Fine-grained encoder accumulator for E1 (volume). One dB step is
    // applied only when the accumulated (scaled) delta crosses ±1 —
    // needs ~100 ticks per dB, so a tiny encoder movement no longer
    // slams the fader to its limit.
    float             m_volPanE1Accum     = 0.0f;

    // Pad 3 held state — combines with pad 23 to toggle the OLED display
    // mode on the controller (firmware also tracks this locally). While
    // pad 3 is down, a pad 23 press does NOT toggle loop-right on the DAW
    // side either — it's consumed by the display-mode toggle.
    std::atomic<bool> m_pad3Held{false};

    // Local shadow of the TotalMix mute state for the input pair we're
    // driving — we don't process OSC feedback yet, so this is just a
    // toggle flag. Assumes we always mute the same pair per session.
    std::atomic<bool> m_totalMixInputPairMuted{false};
    // OSC sender to TotalMix FX (default 127.0.0.1:7001). Held as
    // unique_ptr so audio_engine.h stays free of Winsock includes.
    std::unique_ptr<OscSender> m_osc;
    // TCP client to the Antelope AudioServer (127.0.0.1:2021). Used to
    // mute/unmute Antelope mixer channels when a track's input-monitor
    // flag flips — the actual monitoring happens on the interface, not
    // in the DAW.
    std::unique_ptr<AntelopeClient> m_antelope;
public:
    // Toggle a track's inputMonitor flag AND push the resulting mute
    // state to the Antelope mixer for the channel(s) that track routes
    // to. Sync sites (pad 35, GUI "I" button, session load) all funnel
    // through this so the CP and our LED stay consistent.
    void setTrackInputMonitor(int trackIndex, bool on);
    // Volume is a hardware-mixer control — the DAW plays audio at
    // unity through ASIO and the Antelope mixer applies the level.
    // Call this whenever a track's `volume` changes (slider drag,
    // session load, undo restore) to push the new level to every
    // hw output channel this track routes to.
    void pushTrackVolume(int trackIndex);
    // Re-push every track's monitor→mute mapping to Antelope. Call after
    // AntelopeClient connects or a session loads.
    void syncAllInputMonitorsToAntelope();
private:
    // Mute changes observed on the Antelope mixer, queued by the client's
    // reader thread and drained on the main thread by updateController().
    // Lets a mute toggled by hand in the Control Panel update the matching
    // track's monitor button instead of silently drifting out of sync.
    std::mutex                        m_pendingAntelopeMuteMutex;
    std::vector<std::pair<int, bool>> m_pendingAntelopeMutes;  // channel, muted
    void applyPendingAntelopeMutes();
    // Level changes from the Antelope reader thread — same queue-then
    // -drain pattern as mutes. Second value is the 0..95 dB scale.
    std::mutex                                          m_pendingAntelopeLevelMutex;
    struct PendingLevel { int mixerId; int channel; int level; };
    std::vector<PendingLevel>                           m_pendingAntelopeLevels;
    void applyPendingAntelopeLevels();
    // Toggle one TotalMix strip mute via /1/mute/1/<stripIndex>. TotalMix
    // strips are already stereo-linked in the mixer, so one message mutes
    // the whole pair. Strip index maps to the current bank layout — with
    // stereo linking, strip 1 = AES 1+2, strip 2 = MADI 1+2, strip 3 =
    // MADI 3+4, etc. Strip indices above 8 need bank navigation (not
    // implemented yet). `label` shows on the OLED (e.g. "MADI 1-2").
    void toggleTotalMixStripMute(int stripIndex, const char* label);
public:
    // Session persistence + startup/load sync. The DAW doesn't listen for
    // OSC feedback yet, so restarts would otherwise drift: TotalMix and
    // the DAW could disagree on whether MADI 1-2 is muted. syncTotalMix…
    // FORCES TotalMix + the controller OLED to match our shadow flag —
    // call after any load path so external state is aligned.
    bool getTotalMixInputPairMuted() const { return m_totalMixInputPairMuted.load(); }
    void setTotalMixInputPairMuted(bool m) { m_totalMixInputPairMuted.store(m); }
    void syncTotalMixMuteToHardware();
private:

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
    // True when the pending name is for a JUST-CREATED track (pad 12
    // short-tap add), false for a long-press rename of an existing
    // track. Determines whether a repeat pad-12 press cancels by also
    // deleting the fresh track, or just closes the naming dialog.
    std::atomic<bool> m_pendingTrackIsFreshAdd{false};

    // Long-press detection for pad 12. A short press (< 1 s) creates a new
    // track; holding for >= 1 s cancels the add and instead starts a rename
    // of the currently-selected track. m_pad12PressTimeMs is 0 when pad 12
    // isn't held; m_pad12LongPressFired guards against re-firing during the
    // same hold. m_pad12Held tracks the current down state so combo gestures
    // (pad 12 + pad 0 for input channel entry) know when to activate.
    std::atomic<int64_t> m_pad12PressTimeMs{0};
    std::atomic<bool>    m_pad12LongPressFired{false};
    std::atomic<bool>    m_pad12Held{false};
    // Hold duration to enter rename mode. Used for BOTH:
    //   • pad 12 (long-press → rename selected track)
    //   • pad 13 (long-press with playhead on a bookmark → rename it)
    // Kept unified so the two feel identical.
    static constexpr int64_t RENAME_HOLD_MS = 1300;

    // ---- Channel-entry mode ----
    // Hold pad 12 + tap pad 0 → enter INPUT channel entry (pad 4 → OUTPUT).
    // The DAW-side input/output field for the selected track paints red
    // and pulses while active. User types digits on the number pads
    // (1,2,3,5,6,7,9,10,11 → 1..9, pad 15 → 0), accumulating a decimal
    // number for the current channel. Pad 20 commits the current number
    // as the LEFT channel and switches accumulation to the RIGHT.
    // Pad 21 commits everything: one number = mono, two numbers = stereo
    // (possibly non-adjacent, e.g. 10 & 11). Applied to the selected track.
    enum class ChannelEntryMode { None, Input, Output };
    std::atomic<int>     m_channelEntryMode{0};   // ChannelEntryMode
    mutable std::mutex   m_channelEntryMutex;
    std::string          m_channelEntryLeftBuf;   // decimal digits, "10"
    std::string          m_channelEntryRightBuf;
    bool                 m_channelEntryOnRight = false;
    void enterChannelEntry(ChannelEntryMode mode);
    void appendChannelDigit(int digit);
    void switchChannelEntrySide();  // pad 20: move accumulator to right channel
    void commitChannelEntry();
    void cancelChannelEntry();
    void refreshChannelEntryOled();
    // Number-pad → digit (0..9); returns -1 if `pad` isn't a number pad.
    static int numberPadDigit(int pad);
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
