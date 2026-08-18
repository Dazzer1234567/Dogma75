#include "audio_engine.h"
#include "../util/daw_log.h"
#include "../util/app_paths.h"
#include "../osc/osc_sender.h"
#include "../antelope/antelope_client.h"
#include <iostream>

// Forward-declared logging helper defined further down (uses Track from
// the header). Callers appear both before and after the definition.
static void dawLogTrackState(const char* tag, const std::vector<Track>& tracks);

#include <cmath>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef PORTAUDIO_FOUND
#include <portaudio.h>
#endif

#ifdef LIBSNDFILE_FOUND
#include <sndfile.h>
#endif

#ifdef RTMIDI_FOUND
#include <RtMidi.h>
#endif

AudioEngine::AudioEngine()
    : m_stream(nullptr)
    , m_running(false)
    , m_testToneEnabled(false)
    , m_playing(false)
    , m_outputStereoPair(0)
    , m_playbackPosition(0)
    , m_sampleRate(44100.0)
    , m_bufferSize(256)
    , m_maxOutputChannels(2)
    , m_testTonePhase(0.0)
    , m_testToneFrequency(440.0)
    , m_audioChannels(0)
    , m_midiIn(nullptr)
    , m_midiOut(nullptr)
    , m_currentMidiPort(-1)
    , m_currentDeviceId(-1)
    , m_selectedTrack(-1)
    , m_trackCounter(0)
    , m_waveformZoom(1.0f)
    , m_selectedPark(0)
    , m_controllerMode(1)
    , m_scrubSpeed(1.0f)
    , m_silentScrubSpeed(0.7f)
    , m_scrubRpmThreshold(30.0f)
    , m_fastSpeedMultiplier(5.0f)
    , m_currentEncoderRpm(0.0f)
    , m_rpmAveraging(0.5f)
    , m_scrubbing(false)
    , m_scrubPlaybackRate(0.0f)
    , m_scrubPlaybackPosition(0.0)
    , m_lastScrubTime(0.0)
    , m_faderTouchStartPosition(8192)
    , m_lastFaderPosition(8192)
    , m_faderTouched(false)
    , m_lastFaderTouchTime(0.0)
    , m_faderTapCount(0)
    , m_lastFaderUpdateTime(0.0)
    , m_modifierHeld(false)
    , m_viewScrollDelta(0)
    , m_serialController(nullptr)
{
    for (int i = 0; i < 4; i++) {
        m_markerPositions[i] = 0;
        m_markerEnabled[i] = false;
        m_lastEncoderTime[i] = 0.0;
    }
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    std::cout << "Initializing audio engine..." << std::endl;

#ifdef PORTAUDIO_FOUND
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    int numHostApis = Pa_GetHostApiCount();
    std::cout << "Available Host APIs: " << numHostApis << std::endl;
    for (int i = 0; i < numHostApis; i++) {
        const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(i);
        std::cout << "  [" << i << "] " << apiInfo->name
                  << " (" << apiInfo->deviceCount << " devices)" << std::endl;
    }
    std::cout << std::endl;

    int numDevices = Pa_GetDeviceCount();
    std::cout << "Available audio devices: " << numDevices << std::endl;

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
        std::cout << "  [" << i << "] " << deviceInfo->name
                  << " (in: " << deviceInfo->maxInputChannels
                  << ", out: " << deviceInfo->maxOutputChannels
                  << ") [" << hostApiInfo->name << "]" << std::endl;
    }

    std::cout << "Audio engine initialized successfully" << std::endl;
#else
    std::cout << "Audio engine initialized (PortAudio not available)" << std::endl;
#endif

    // OSC to TotalMix FX. Local TotalMix listens on Remote Controller 1's
    // "Port incoming" â€” 7001 by default. Failure is non-fatal; the chord
    // handler just no-ops if the sender never opened.
    m_osc = std::make_unique<OscSender>();
    if (!m_osc->init("127.0.0.1", 7001)) {
        dawLog("OSC: init to 127.0.0.1:7001 failed");
        m_osc.reset();
    } else {
        dawLog("OSC: sending to 127.0.0.1:7001");
    }

    // Antelope AudioServer TCP client. Used to control mute on the
    // interface's mixer when track input monitors toggle. Failure is
    // non-fatal (the Antelope may not be up yet).
    m_antelope = std::make_unique<AntelopeClient>();
    if (!m_antelope->init("127.0.0.1", 2021)) {
        dawLog("Antelope: init to 127.0.0.1:2021 failed (will retry never)");
    }
    // Follow the mixer as well as drive it: a mute toggled by hand in the
    // Antelope Control Panel now updates the matching track's monitor
    // button. Fires on the client's READER thread, so it only queues â€”
    // updateController() applies it on the main thread where touching
    // m_tracks is safe.
    m_antelope->setMuteChangeCallback([this](int channel, bool muted) {
        std::lock_guard<std::mutex> lock(m_pendingAntelopeMuteMutex);
        m_pendingAntelopeMutes.emplace_back(channel, muted);
    });

    return true;
}

void AudioEngine::shutdown() {
    if (m_running.load()) {
        stopAudio();
    }

    shutdownMidi();

    if (m_osc) {
        m_osc->shutdown();
        m_osc.reset();
    }

    if (m_antelope) {
        m_antelope->shutdown();
        m_antelope.reset();
    }

#ifdef PORTAUDIO_FOUND
    if (m_stream) {
        Pa_CloseStream(static_cast<PaStream*>(m_stream));
        m_stream = nullptr;
    }
    Pa_Terminate();
#endif

    std::cout << "Audio engine shutdown" << std::endl;
}

bool AudioEngine::loadWavFile(const std::string& filepath) {
#ifdef LIBSNDFILE_FOUND
    SF_INFO sfInfo;
    sfInfo.format = 0;

    SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        std::cerr << "Failed to open WAV file: " << filepath << std::endl;
        return false;
    }

    std::cout << "Loading WAV file: " << filepath << std::endl;

    m_audioData.resize(sfInfo.frames * sfInfo.channels);
    m_audioChannels = sfInfo.channels;
    sf_readf_float(file, m_audioData.data(), sfInfo.frames);
    sf_close(file);

    m_loadedFilePath = filepath;
    m_playbackPosition.store(0);

    std::cout << "WAV file loaded successfully" << std::endl;
    return true;
#else
    (void)filepath;
    return false;
#endif
}

void AudioEngine::play() {
    bool hasAudio = false;
    for (const auto& track : m_tracks) {
        if (track.hasAudio()) {
            hasAudio = true;
            break;
        }
    }
    bool anyArmed = false;
    for (const auto& track : m_tracks) {
        if (track.armed) { anyArmed = true; break; }
    }

    dawLog("play() hasAudio=%d anyArmed=%d pos=%zu tracks=%zu maxIn=%d",
           (int)hasAudio, (int)anyArmed,
           (size_t)m_playbackPosition.load(),
           m_tracks.size(), m_maxInputChannels);
    // Start playback unconditionally â€” even with no audio and no armed
    // track, the playhead advances so the user can watch it sweep across
    // an empty arrangement (matches the "empty session" convention).
    {
        m_scrubbing.store(false);
        m_scrubPlaybackRate.store(0.0f);
        // Snapshot the start position so a later stop can return here if the
        // return-on-stop flag is set. Includes stops from any source (button
        // press, end-of-file auto-stop, mod+play rejection, etc.).
        m_playStartPosition.store(m_playbackPosition.load());

        // Prepare record slots for every armed track â€” clear the buffer,
        // fix the channel count for this take. Reserve ~2 minutes so
        // typical takes never reallocate on the audio thread. The
        // track's audioData is CLEARED here so no stale take is drawn
        // during the new take; the snapshot tick fills it in from the
        // record buffer, growing left-to-right in step with the playhead.
        if (anyArmed && m_maxInputChannels > 0) {
            // Record gate: if the record pair is armed AND both markers
            // are placed, capture is restricted to that frame range â€”
            // callback drops samples whose playhead position falls
            // outside, snapshot overlays onto audioData starting at
            // recLeft (not m_playStartPosition). Empty range disables
            // gating (open-ended take from m_playStartPosition).
            size_t gateStart = 0, gateEnd = 0;
            bool recPair = m_recordLeftEnabled.load() && m_recordRightEnabled.load()
                        && isMarkerEnabled(1) && isMarkerEnabled(2);
            if (recPair) {
                gateStart = getMarkerPosition(1);
                gateEnd   = getMarkerPosition(2);
                if (gateEnd <= gateStart) { gateStart = 0; gateEnd = 0; }
            }
            m_recordGateStart.store(gateStart);
            m_recordGateEnd.store(gateEnd);
            dawLog("record start: playStart=%zu gate=[%zu..%zu]",
                   (size_t)m_playStartPosition.load(),
                   gateStart, gateEnd);
            dawLogTrackState("preRec", m_tracks);
            // Attach the pre-recording audio of every armed track to the
            // most recent undo entry so a later Undo can restore the
            // previous take (plus the playhead's pre-record position,
            // which the entry already captured). Remember which entry
            // owns this take so finaliseRecording can attach WAV paths
            // for on-undo cleanup.
            undoStashArmedTrackAudio();
            {
                std::lock_guard<std::mutex> lock(m_undoMutex);
                m_recordingUndoEntryId = m_undoStack.empty() ? 0
                                       : m_undoStack.back().id;
            }

            ensureRecordSlots();
            // Fresh-take highlight anchor: gate's left marker if gated,
            // otherwise the free-run play start. NOT shifted — instead
            // we drop the first m_recordLatencyFrames of captured input
            // (see m_recordLeadingSkipRemaining below). That way
            // slot->buffer[0] corresponds to the sample that aligns
            // with recordStartFrame in the reference, so writing at
            // audioData[recordStartFrame + i] just works, including
            // for record-from-zero cases where a negative anchor would
            // clamp and undo the compensation.
            size_t recordStartFrame = (gateEnd > gateStart) ? gateStart
                                                            : m_playStartPosition.load();
            // Arm the capture-side skip: drop the first N input samples
            // so the first pushed sample represents audio that was
            // actually played at recordStartFrame (mic input from
            // before that moment is pre-record noise / silence).
            //
            // Extra fudge: PortAudio's reported latency underestimates
            // the real round-trip by ~30 samples on the Antelope path
            // (measured 2026-08-17 with a click/clap alignment test).
            // Probably an unreported fixed delay in the driver or the
            // converter's group delay. If a future interface reports
            // differently, expose this as a setting.
            constexpr size_t kRecordLatencyFudgeFrames = 30;
            m_recordLeadingSkipRemaining.store(
                m_recordLatencyFrames.load() + kRecordLatencyFudgeFrames);
            for (size_t i = 0; i < m_tracks.size(); i++) {
                Track& t = m_tracks[i];
                if (!t.armed) continue;
                RecordSlot* slot = m_recordSlots[i].get();
                if (!slot) continue;
                int captureChans = t.inputMono ? 1 : 2;
                {
                    std::unique_lock<std::mutex> lock(slot->mutex);
                    slot->buffer.clear();
                    slot->channels = captureChans;
                    slot->buffer.reserve((size_t)(m_sampleRate * 120.0) * slot->channels);
                }
                // Punch-in: KEEP existing audio (and its channel count).
                // The snapshot tick overlays captured samples over the
                // take's frame range and converts capture-to-track
                // channels as needed so nothing outside the take is
                // affected â€” even when the input is mono and the track
                // is stereo (or vice versa).
                if (t.audioData.empty()) {
                    // First take on this track â€” take the recording's
                    // channel count as the track's channel count.
                    t.channels = captureChans;
                }
                // Stamp the fresh-take range so the renderer paints it
                // in the punch-in colour. Grows as recording progresses.
                t.freshTakeStart = recordStartFrame;
                t.freshTakeEnd   = recordStartFrame;
                t.buildPeakPyramid();
                t.audioVersion++;
            }
            m_lastRecordSnapshotMs = 0;   // force a snapshot on the next tick
            m_recordActive.store(true);
        }

        m_playing.store(true);
        std::cout << "Playback started" << (m_recordActive.load() ? " (recording)" : "") << std::endl;
    }
}

void AudioEngine::stop() {
    dawLog("stop() pos=%zu recordActive=%d",
           (size_t)m_playbackPosition.load(),
           (int)m_recordActive.load());
    m_playing.store(false);
    m_scrubbing.store(false);
    m_scrubPlaybackRate.store(0.0f);
    std::cout << "Playback stopped" << std::endl;

    // If we were recording, freeze capture and hand the buffers to a
    // background finaliser so the WAV write + track reload don't stall
    // the main / reader thread. The audio callback checks m_recordActive
    // each buffer; setting it false here means any callback that starts
    // after this line will skip capture. m_finalising stays TRUE from
    // here until finaliseRecording completes â€” while it's set, the
    // callback keeps skipping armed-track playback so a concurrent WAV
    // read or a late snapshot tick can't race with playback.
    if (m_recordActive.exchange(false)) {
        m_finalising.store(true);
        std::thread([this]() { finaliseRecording(); }).detach();
    }
}

void AudioEngine::ensureRecordSlots() {
    while (m_recordSlots.size() < m_tracks.size()) {
        m_recordSlots.emplace_back(std::make_unique<RecordSlot>());
    }
    while (m_trackMeters.size() < m_tracks.size()) {
        m_trackMeters.emplace_back(std::make_unique<std::atomic<float>>(0.0f));
    }
}

void AudioEngine::setSessionDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(m_sessionDirMutex);
    m_sessionDir = dir;
}

// ==================== UNDO ====================

static void dawLogTrackState(const char* tag, const std::vector<Track>& tracks) {
    for (size_t i = 0; i < tracks.size(); i++) {
        const Track& t = tracks[i];
        dawLog("  %s [%zu] name='%s' chans=%d frames=%zu ver=%d fresh=[%zu..%zu] path='%s'",
               tag, i, t.name.c_str(), t.channels, t.getTotalFrames(),
               t.audioVersion, t.freshTakeStart, t.freshTakeEnd,
               t.filePath.c_str());
    }
}

void AudioEngine::undoSnapshot() {
    UndoEntry e;
    e.tracks.reserve(m_tracks.size());
    for (const Track& t : m_tracks) {
        UndoTrackState s;
        s.name          = t.name;
        s.filePath      = t.filePath;
        s.channels      = t.channels;
        s.volume        = t.volume;
        s.pan           = t.pan;
        s.muted         = t.muted;
        s.solo          = t.solo;
        s.armed         = t.armed;
        s.inputMonitor  = t.inputMonitor;
        s.outputPair    = t.outputPair;
        s.outputMonoChan= t.outputMonoChan;
        s.outputMono    = t.outputMono;
        s.outputLeftChan = t.outputLeftChan;
        s.outputRightChan= t.outputRightChan;
        s.inputPair     = t.inputPair;
        s.inputMonoChan = t.inputMonoChan;
        s.inputMono     = t.inputMono;
        s.inputLeftChan = t.inputLeftChan;
        s.inputRightChan= t.inputRightChan;
        s.color         = t.color;
        e.tracks.push_back(s);
    }
    e.selectedTrack = m_selectedTrack;
    for (int i = 0; i < 4; i++) {
        e.markerPositions[i] = m_markerPositions[i];
        e.markerEnabled[i]   = m_markerEnabled[i];
        e.markerEverSet[i]   = m_markerEverSet[i];
    }
    e.loopEnabled         = m_loopLeftEnabled.load();
    e.recordEnabled       = m_recordLeftEnabled.load();
    e.returnToStartOnStop = m_returnToStartOnStop.load();
    e.playing             = m_playing.load();
    e.playbackPosition    = m_playbackPosition.load();
    // Snapshot bookmarks so add/rename/clear can all be undone.
    {
        std::lock_guard<std::mutex> lock(m_bookmarksMutex);
        e.bookmarks.reserve(m_bookmarkFrames.size());
        for (const auto& bm : m_bookmarkFrames) {
            e.bookmarks.push_back({bm.frame, bm.name});
        }
    }
    if (m_undoGuiCapture) m_undoGuiCapture(e);

    std::lock_guard<std::mutex> lock(m_undoMutex);
    e.id = m_nextUndoEntryId++;
    dawLog("undoSnapshot id=%d stackDepth=%zu tracks=%zu",
           e.id, m_undoStack.size() + 1, e.tracks.size());
    m_undoStack.push_back(std::move(e));
    while (m_undoStack.size() > UNDO_MAX) m_undoStack.pop_front();
}

void AudioEngine::undoSnapshotViewIfBurst() {
    // Activity-based: reset the activity timer on every call, but only
    // snapshot if the previous activity was > COALESCE_MS ago. Ensures a
    // continuous run of rapid calls creates exactly ONE snapshot (the one
    // captured BEFORE the burst began) — as opposed to snapshot-time-based
    // coalescing which re-fires every COALESCE_MS during long gestures.
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    bool newBurst = (m_lastViewActivityMs == 0) ||
                    (nowMs - m_lastViewActivityMs > VIEW_SNAPSHOT_COALESCE_MS);
    m_lastViewActivityMs = nowMs;
    if (newBurst) undoSnapshotView();
}

void AudioEngine::undoSnapshotView() {
    // Lean view-only snapshot: JUST the fields the view-undo path will
    // restore (marker positions, playback position, GUI zoom/scroll).
    // Everything else in UndoEntry is left at default — undoPopView
    // ignores those. Kept in the same UndoEntry struct to avoid a
    // parallel type and to keep the GUI capture hook reusable.
    UndoEntry e;
    for (int i = 0; i < 4; i++) {
        e.markerPositions[i] = m_markerPositions[i];
        e.markerEnabled[i]   = m_markerEnabled[i];
        e.markerEverSet[i]   = m_markerEverSet[i];
    }
    e.playbackPosition = m_playbackPosition.load();
    if (m_undoGuiCapture) m_undoGuiCapture(e);   // fills zoom/scroll/timeline
    std::lock_guard<std::mutex> lock(m_undoMutex);
    e.id = m_nextUndoEntryId++;
    m_undoStackView.push_back(std::move(e));
    while (m_undoStackView.size() > UNDO_MAX) m_undoStackView.pop_front();
    dawLog("undoSnapshotView id=%d viewStackDepth=%zu",
           m_undoStackView.back().id, m_undoStackView.size());
}

void AudioEngine::undoStashArmedTrackAudio() {
    std::lock_guard<std::mutex> lock(m_undoMutex);
    if (m_undoStack.empty()) return;
    UndoEntry& top = m_undoStack.back();
    if (top.tracks.size() < m_tracks.size()) {
        top.tracks.resize(m_tracks.size());
    }
    for (size_t i = 0; i < m_tracks.size(); i++) {
        const Track& t = m_tracks[i];
        if (!t.armed) continue;
        top.tracks[i].hasAudioSnapshot = true;
        top.tracks[i].audioData        = t.audioData;
        top.tracks[i].channels         = t.channels;
    }
}

void AudioEngine::undoStashTrackAudio(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= (int)m_tracks.size()) return;
    std::lock_guard<std::mutex> lock(m_undoMutex);
    if (m_undoStack.empty()) return;
    UndoEntry& top = m_undoStack.back();
    if (top.tracks.size() < m_tracks.size()) {
        top.tracks.resize(m_tracks.size());
    }
    const Track& t = m_tracks[trackIndex];
    top.tracks[trackIndex].hasAudioSnapshot = true;
    top.tracks[trackIndex].audioData        = t.audioData;
    top.tracks[trackIndex].channels         = t.channels;
}

bool AudioEngine::undoPop() {
    // Two distinct pre-conditions:
    //   (A) A take is actively capturing right now â€” treat this as
    //       "cancel the take". No WAV is written, no finaliseRecording
    //       is spawned, the record slots are dropped, and the playhead
    //       snaps back to playStart (via the snapshot restore below).
    //   (B) A take just stopped and finaliseRecording is in flight â€”
    //       wait for it so the WAV path lands on the top undo entry
    //       before we pop it (so the pop can also delete the WAV).
    // In both cases we hold m_finalising=true through the whole restore
    // so the audio callback keeps armed-track playback silent while we
    // swap audioData under it.
    bool cancellingLiveTake = false;
    if (m_recordActive.exchange(false)) {
        dawLog("undoPop: cancelling live record");
        cancellingLiveTake = true;
        m_finalising.store(true);
        // Give any in-flight callback / snapshot tick 20 ms to observe
        // the flag change so they don't race us into the audioData swap.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Drop the captured audio â€” nothing to persist.
        for (auto& slot : m_recordSlots) {
            if (!slot) continue;
            std::lock_guard<std::mutex> l(slot->mutex);
            slot->buffer.clear();
            slot->channels = 0;
        }
        // Cancel playback (which record auto-started).
        m_playing.store(false);
        m_scrubbing.store(false);
        m_scrubPlaybackRate.store(0.0f);

        // Discard any undo entries newer than the pre-record entry.
        // Those were captured DURING the take we're now cancelling
        // (each with playing=true snapshotted); popping one of them
        // would call play() during restore and start a new recording.
        // Rewind the stack to the entry that owns the pre-record state.
        int targetId = m_recordingUndoEntryId;
        m_recordingUndoEntryId = 0;
        if (targetId != 0) {
            std::lock_guard<std::mutex> lock(m_undoMutex);
            int dropped = 0;
            while (!m_undoStack.empty() &&
                   m_undoStack.back().id != targetId) {
                m_undoStack.pop_back();
                ++dropped;
            }
            if (dropped > 0) {
                dawLog("undoPop: dropped %d mid-take entries "
                       "(rewound to id=%d)", dropped, targetId);
            }
        }
    } else if (m_finalising.load()) {
        dawLog("undoPop: waiting for finalise");
        int spins = 0;
        while (m_finalising.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (++spins > 1000) {   // 5 s hard cap
                dawLog("undoPop: finalise wait timed out");
                break;
            }
        }
    }

    UndoEntry e;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        if (m_undoStack.empty()) {
            dawLog("undoPop empty");
            if (cancellingLiveTake) m_finalising.store(false);
            return false;
        }
        e = std::move(m_undoStack.back());
        m_undoStack.pop_back();
    }
    dawLog("undoPop id=%d tracks=%zu wavPaths=%zu playing->%d live=%d",
           e.id, e.tracks.size(), e.recordedWavPaths.size(),
           (int)e.playing, (int)cancellingLiveTake);

    // Delete any WAV files that were produced BY the action we're
    // rewinding â€” an unintentional take shouldn't leave garbage on disk.
    for (const std::string& p : e.recordedWavPaths) {
        if (std::remove(p.c_str()) == 0) {
            std::cout << "Undo: deleted " << p << std::endl;
        }
    }

    // Cancel any in-flight modal side-effects of the action we're undoing:
    //   - Firmware text-input mode (flashes LEDs 4/5) â€” entered on add/
    //     rename track. Silent no-op if not active.
    //   - Firmware flashMode (loop-edit, fades LEDs 4-7) â€” entered on
    //     pad 15 tap. Silent no-op if not active.
    //   - Any pending main-thread requests queued from the controller â€”
    //     otherwise a just-undone add would re-fire on the next tick.
    //   - Clear-markers mode on the DAW side.
    //   - Any half-started pad-12 press so a stale long-press can't fire.
    if (m_serialController) {
        m_serialController->sendMessage("CANCELMODES");
    }
    m_pendingAddTrackRequest.store(false);
    m_pendingRenameRequest.store(false);
    m_pendingNameTrackIndex.store(-1);
    m_pendingDeleteTrackRequest.store(false);
    m_pad12PressTimeMs.store(0);
    m_pad12LongPressFired.store(false);
    m_clearMode.store(false);
    // A rename in progress on the firmware is now stale â€” force the OLED
    // back to the current playback state on the next tick.
    m_startupOledPushed = false;

    // Restore marker create/toggle state and playback flags. NOTE we
    // deliberately do NOT restore marker POSITIONS or the playback
    // position here — those are "view" state and belong to undoPopView.
    // Content undo leaves the play line and marker placements alone so
    // an unrelated "undo my recording" doesn't also yank the timeline.
    for (int i = 0; i < 4; i++) {
        m_markerEnabled[i]   = e.markerEnabled[i];
        m_markerEverSet[i]   = e.markerEverSet[i];
    }
    m_loopLeftEnabled.store(e.loopEnabled);
    m_loopRightEnabled.store(e.loopEnabled);
    m_recordLeftEnabled.store(e.recordEnabled);
    m_recordRightEnabled.store(e.recordEnabled);
    m_returnToStartOnStop.store(e.returnToStartOnStop);

    // Bookmarks â€” full replace. Also drop any in-flight bookmark naming
    // so a rewound "add + name" doesn't leave a stale pending index
    // pointing past the restored list.
    m_pendingNameBookmarkIndex.store(-1);
    m_bookmarkNavIdx = -1;
    m_bookmarkNavAccum = 0;
    m_bookmarkScrollMode.store(false);
    {
        std::lock_guard<std::mutex> lock(m_bookmarksMutex);
        m_bookmarkFrames.clear();
        m_bookmarkFrames.reserve(e.bookmarks.size());
        for (const auto& bm : e.bookmarks) {
            m_bookmarkFrames.push_back({bm.frame, bm.name});
        }
    }

    // Restore per-track metadata. Track add/delete is handled by resizing
    // m_tracks to match the snapshot. For added tracks with a file path
    // that no longer exists in-memory, try to reload from disk so the
    // waveform reappears; otherwise the track comes back empty.
    while (m_tracks.size() > e.tracks.size()) {
        m_tracks.pop_back();
        if (!m_recordSlots.empty()) m_recordSlots.pop_back();
    }
    while (m_tracks.size() < e.tracks.size()) {
        m_tracks.emplace_back();
        ensureRecordSlots();
    }
    for (size_t i = 0; i < e.tracks.size(); i++) {
        Track& t          = m_tracks[i];
        const UndoTrackState& s = e.tracks[i];
        bool needAudioReload = (t.filePath != s.filePath) && !s.filePath.empty();
        t.name          = s.name;
        t.filePath      = s.filePath;
        t.volume        = s.volume;
        t.pan           = s.pan;
        t.muted         = s.muted;
        t.solo          = s.solo;
        t.armed         = s.armed;
        t.inputMonitor  = s.inputMonitor;
        t.outputPair    = s.outputPair;
        t.outputMonoChan= s.outputMonoChan;
        t.outputMono    = s.outputMono;
        t.outputLeftChan = s.outputLeftChan;
        t.outputRightChan= s.outputRightChan;
        t.inputPair     = s.inputPair;
        t.inputMonoChan = s.inputMonoChan;
        t.inputMono     = s.inputMono;
        t.inputLeftChan = s.inputLeftChan;
        t.inputRightChan= s.inputRightChan;
        t.color         = s.color;
        if (s.hasAudioSnapshot) {
            // A prior action (recording, clear, load) captured the
            // exact audio bytes â€” restore them directly, no disk I/O.
            // Lock so a still-in-flight snapshot tick or finalise WAV
            // read can't tear the vector out from under us mid-swap.
            std::lock_guard<std::mutex> aLock(m_recordAudioMutex);
            t.audioData = s.audioData;
            t.channels  = s.channels;
            t.buildPeakPyramid();
            t.audioVersion++;
            // The restored audio is the OLD take (pre-record) â€” no fresh
            // take colouring should remain from the just-undone recording.
            t.freshTakeStart = 0;
            t.freshTakeEnd   = 0;
        } else if (needAudioReload) {
            // Best-effort: if the file is gone, loadAudio quietly fails
            // and the track stays with the old (or empty) audio.
            std::string saved = t.name;
            t.loadAudio(s.filePath);
            t.name = saved;
        }
    }
    if (e.selectedTrack >= 0 && e.selectedTrack < (int)m_tracks.size()) {
        m_selectedTrack = e.selectedTrack;
    } else {
        m_selectedTrack = m_tracks.empty() ? -1 : 0;
    }

    // View state (zoom, view center, timeline extent) intentionally NOT
    // restored here — those belong to undoPopView. Content undo leaves
    // the view exactly where the user has it.
    // m_undoGuiRestore is called from undoPopView instead.

    // Every undo ends stopped. Even if the restored entry captured
    // playing=true, we don't want the DAW to jump into playback on undo
    // â€” that's disorienting when the user is walking back several steps.
    // Snapshot's e.playing is intentionally ignored here.
    if (m_playing.load()) {
        stop();
    }

    // When undoing a live take, the playhead ALWAYS snaps back to
    // playStart â€” the entry captured that position, and the take was a
    // mistake so return-on-stop preferences don't apply.
    if (cancellingLiveTake) {
        m_playbackPosition.store(m_playStartPosition.load());
        m_finalising.store(false);   // release armed-track guard
    }

    // Refresh controller LEDs to match the restored state.
    syncLoopPairLedsNow();
    syncRecordPairLedsNow();
    syncSoloMuteLedsNow();
    syncPlayLedNow();
    markSessionDirty();
    return true;
}

bool AudioEngine::undoPopView() {
    // View-only undo. Pops the view stack, restores JUST the fields the
    // view snapshot captured: marker positions, playback position, and
    // GUI zoom / view center / timeline extent (via m_undoGuiRestore).
    // Never touches tracks, mixer state, bookmarks, or arm/mute flags.
    UndoEntry e;
    {
        std::lock_guard<std::mutex> lock(m_undoMutex);
        if (m_undoStackView.empty()) {
            dawLog("undoPopView empty");
            return false;
        }
        e = std::move(m_undoStackView.back());
        m_undoStackView.pop_back();
    }
    dawLog("undoPopView id=%d viewStackDepth=%zu", e.id, m_undoStackView.size());
    for (int i = 0; i < 4; i++) {
        m_markerPositions[i] = e.markerPositions[i];
        // markerEnabled / markerEverSet are content-side toggles; leave
        // them alone so undoing a marker MOVE doesn't also un-create it.
    }
    m_playbackPosition.store(e.playbackPosition);
    if (m_undoGuiRestore) m_undoGuiRestore(e);   // zoom / view center / timeline
    return true;
}

void AudioEngine::finaliseRecording() {
#ifdef LIBSNDFILE_FOUND
    dawLog("finaliseRecording start");
    dawLogTrackState("preFin", m_tracks);
    // Snapshot the target directory. If empty, drop into ./recordings
    // relative to the executable's CWD.
    std::string baseDir;
    {
        std::lock_guard<std::mutex> lock(m_sessionDirMutex);
        baseDir = m_sessionDir;
    }
    if (baseDir.empty()) baseDir = ".";
    std::string recDir = baseDir + "/recordings";
#ifdef _WIN32
    std::string mk = "if not exist \"" + recDir + "\" mkdir \"" + recDir + "\"";
    system(mk.c_str());
#else
    std::string mk = "mkdir -p \"" + recDir + "\"";
    system(mk.c_str());
#endif

    time_t rawTime = time(nullptr);
    struct tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &rawTime);
#else
    localtime_r(&rawTime, &tmBuf);
#endif
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", &tmBuf);

    size_t recordStartFrame = m_playStartPosition.load();
    for (size_t i = 0; i < m_tracks.size() && i < m_recordSlots.size(); i++) {
        Track& t = m_tracks[i];
        if (!t.armed) continue;
        RecordSlot* slot = m_recordSlots[i].get();
        if (!slot) continue;
        std::unique_lock<std::mutex> lock(slot->mutex);
        if (slot->buffer.empty() || slot->channels == 0) continue;

        // Sanitise the track name for a filename.
        std::string safeName;
        for (char c : t.name) {
            safeName += (isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_';
        }
        if (safeName.empty()) safeName = "track";

        char fname[512];
        snprintf(fname, sizeof(fname), "%s/%s_%s.wav",
                 recDir.c_str(), safeName.c_str(), stamp);

        int outChans = t.channels;   // punch-in composite may extend beyond
                                     // the raw capture; we write the whole
                                     // composite (audioData) as the WAV.
        SF_INFO info = {};
        info.samplerate = (int)m_sampleRate;
        info.channels   = outChans;
        info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE* sf = sf_open(fname, SFM_WRITE, &info);
        if (!sf) {
            std::cerr << "sf_open failed for " << fname << std::endl;
            slot->buffer.clear();
            slot->channels = 0;
            continue;
        }

        // Write the whole composite audioData â€” the snapshot tick has
        // already built [existing][captured][existing tail] correctly,
        // so we just persist that. Load-on-session-reopen then puts the
        // exact same content back into the track. Lock so a late
        // snapshot-tick swap can't pull the vector out from under us
        // mid-write.
        {
            std::lock_guard<std::mutex> aLock(m_recordAudioMutex);
            if (!t.audioData.empty() && outChans > 0) {
                sf_writef_float(sf, t.audioData.data(),
                                t.audioData.size() / outChans);
            }
        }
        sf_close(sf);

        std::cout << "Recorded " << (slot->buffer.size() / slot->channels)
                  << " frames (starting at frame " << recordStartFrame
                  << ") to " << fname << std::endl;

        // Register the WAV path with the undo entry that captured the
        // pre-record state, so a later Undo can delete this file from
        // disk (an unintentional take shouldn't leave garbage behind).
        {
            std::lock_guard<std::mutex> uLock(m_undoMutex);
            if (m_recordingUndoEntryId != 0) {
                for (auto& entry : m_undoStack) {
                    if (entry.id == m_recordingUndoEntryId) {
                        entry.recordedWavPaths.emplace_back(fname);
                        break;
                    }
                }
            }
        }

        slot->buffer.clear();
        slot->channels = 0;
        lock.unlock();

        // Track's audioData already IS the finalised composite (built
        // by the snapshot tick). Just persist the WAV path so a future
        // session reload finds the file; skip a redundant loadAudio so
        // the fresh-take range highlight stays intact.
        t.filePath = fname;
        markSessionDirty();
    }
    // This take is done; the next play() will re-arm the id if it starts
    // another recording. Release the armed-track playback guard so
    // subsequent playbacks (including immediately after this take)
    // actually make sound.
    m_recordingUndoEntryId = 0;
    m_finalising.store(false);
    dawLog("finaliseRecording end");
    dawLogTrackState("postFin", m_tracks);
#endif
}

size_t AudioEngine::getTotalFrames() const {
    size_t maxFrames = 0;
    for (const auto& track : m_tracks) {
        size_t trackFrames = track.getTotalFrames();
        if (trackFrames > maxFrames) {
            maxFrames = trackFrames;
        }
    }
    return maxFrames;
}

// ==================== MIDI ====================

bool AudioEngine::initializeMidi() {
#ifdef RTMIDI_FOUND
    try {
        m_midiIn = new RtMidiIn();
        RtMidiIn* midiIn = static_cast<RtMidiIn*>(m_midiIn);

        unsigned int portCount = midiIn->getPortCount();
        std::cout << "MIDI Input Ports: " << portCount << std::endl;

        for (unsigned int i = 0; i < portCount; i++) {
            std::cout << "  [" << i << "] " << midiIn->getPortName(i) << std::endl;
        }

        if (portCount > 0) {
            int portToOpen = 0;
            bool foundDevice = false;

            for (unsigned int i = 0; i < portCount; i++) {
                std::string portName = midiIn->getPortName(i);
                if (portName.find("X-Touch One") != std::string::npos ||
                    portName.find("X-TOUCH ONE") != std::string::npos ||
                    portName.find("xtouch") != std::string::npos) {
                    portToOpen = i;
                    std::cout << "Found X-Touch One MIDI device at index " << i << std::endl;
                    foundDevice = true;
                    break;
                }
            }

            if (!foundDevice) {
                for (unsigned int i = 0; i < portCount; i++) {
                    std::string portName = midiIn->getPortName(i);
                    if (portName.find("ParksTool") != std::string::npos ||
                        portName.find("parkstool") != std::string::npos ||
                        portName.find("Parks") != std::string::npos) {
                        portToOpen = i;
                        std::cout << "Found ParksTool MIDI device at index " << i << std::endl;
                        foundDevice = true;
                        break;
                    }
                }
            }

            midiIn->openPort(portToOpen);
            m_currentMidiPort = portToOpen;
            std::cout << "Opened MIDI input port: " << midiIn->getPortName(portToOpen) << std::endl;
            midiIn->ignoreTypes(false, false, false);

            m_midiOut = new RtMidiOut();
            RtMidiOut* midiOut = static_cast<RtMidiOut*>(m_midiOut);

            unsigned int outPortCount = midiOut->getPortCount();
            std::cout << "MIDI Output Ports: " << outPortCount << std::endl;

            for (unsigned int i = 0; i < outPortCount; i++) {
                std::cout << "  [" << i << "] " << midiOut->getPortName(i) << std::endl;
            }

            std::string inputPortName = midiIn->getPortName(portToOpen);
            int outPortToOpen = -1;
            for (unsigned int i = 0; i < outPortCount; i++) {
                std::string outPortName = midiOut->getPortName(i);
                if ((inputPortName.find("X-Touch") != std::string::npos && outPortName.find("X-Touch") != std::string::npos) ||
                    (inputPortName.find("X-TOUCH") != std::string::npos && outPortName.find("X-TOUCH") != std::string::npos) ||
                    (inputPortName.find("ParksTool") != std::string::npos && outPortName.find("ParksTool") != std::string::npos)) {
                    outPortToOpen = i;
                    break;
                }
            }

            if (outPortToOpen >= 0) {
                midiOut->openPort(outPortToOpen);
                std::cout << "Opened MIDI output port: " << midiOut->getPortName(outPortToOpen) << std::endl;

                std::vector<unsigned char> hostReply = {
                    0xF0, 0x00, 0x00, 0x66, 0x14, 0x03,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0xF7
                };
                midiOut->sendMessage(&hostReply);

                std::vector<unsigned char> online = {
                    0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7
                };
                midiOut->sendMessage(&online);

                m_lastFaderPosition = 0;
                std::vector<unsigned char> faderMsg = { 0xE0, 0x00, 0x00 };
                midiOut->sendMessage(&faderMsg);

                std::cout << "Sent Mackie Control handshake to X-Touch One" << std::endl;
            }

            return true;
        } else {
            std::cout << "No MIDI input ports available" << std::endl;
            return false;
        }
    } catch (RtMidiError& error) {
        std::cerr << "MIDI Error: " << error.getMessage() << std::endl;
        return false;
    }
#else
    std::cout << "RtMidi not available" << std::endl;
    return false;
#endif
}

void AudioEngine::shutdownMidi() {
#ifdef RTMIDI_FOUND
    if (m_midiOut) {
        RtMidiOut* midiOut = static_cast<RtMidiOut*>(m_midiOut);
        midiOut->closePort();
        delete midiOut;
        m_midiOut = nullptr;
    }
    if (m_midiIn) {
        RtMidiIn* midiIn = static_cast<RtMidiIn*>(m_midiIn);
        midiIn->closePort();
        delete midiIn;
        m_midiIn = nullptr;
        std::cout << "MIDI shutdown" << std::endl;
    }
#endif
}

void AudioEngine::processMidiMessages() {
#ifdef RTMIDI_FOUND
    if (!m_midiIn) return;

    RtMidiIn* midiIn = static_cast<RtMidiIn*>(m_midiIn);
    std::vector<unsigned char> message;

    while (true) {
        double timestamp = midiIn->getMessage(&message);
        (void)timestamp;
        if (message.size() == 0) break;

        if (message.size() >= 1) {
            printf("MIDI [mode=%d]: ", m_controllerMode);
            for (size_t i = 0; i < message.size(); i++) {
                printf("%02X ", message[i]);
            }
            printf("\n");
        }

        // ==================== CUSTOM / CUSTOM MACKIE MODE (Modes 0 & 1) ====================
        if (m_controllerMode == 0 || m_controllerMode == 1) {
            if (message.size() >= 3 && (message[0] & 0xF0) == 0xB0) {
                unsigned char ccValue = message[2];
                if (message[1] == 0x3C) {
                    int direction = 0;
                    if (ccValue >= 0x01 && ccValue <= 0x3F) direction = 1;
                    else if (ccValue >= 0x41 && ccValue <= 0x7F) direction = -1;
                    if (direction != 0) handleJogWheel(direction);
                }
            }

            if (message.size() >= 3 && (message[0] & 0xF0) == 0x90) {
                if (message[1] == 0x68) {
                    m_faderTouched = (message[2] > 0);
                    m_faderTouchStartPosition = -1;
                }
            }

            if (message.size() >= 3 && (message[0] & 0xF0) == 0xE0 && m_faderTouched) {
                int pitchbend = (message[2] << 7) | message[1];
                handleFaderZoom(pitchbend, 16383);
            }
        }

        // ==================== MIDIREL MODE (Mode 3) ====================
        if (m_controllerMode == 3) {
            if (message.size() >= 3 && (message[0] & 0xF0) == 0x90) {
                unsigned char note = message[1];
                unsigned char velocity = message[2];

                if (note == 0x6E) {
                    bool wasTouched = m_faderTouched;
                    m_faderTouched = (velocity > 0);
                    printf("MidiRel: Fader touch %s\n", m_faderTouched ? "ON" : "OFF");

                    if (m_faderTouched && !wasTouched) {
                        m_faderTouchStartPosition = -1;
                        auto now = std::chrono::steady_clock::now();
                        double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                        if (currentTime - m_lastFaderTouchTime < 0.4) {
                            m_faderTapCount++;
                            if (m_faderTapCount >= 2) {
                                m_faderTapCount = 0;
                                std::cout << "Double-tap: Recenter fader" << std::endl;
                                if (m_midiOut) {
                                    RtMidiOut* midiOut = static_cast<RtMidiOut*>(m_midiOut);
                                    std::vector<unsigned char> faderMsg = { 0xB0, 0x46, 0x40 };
                                    midiOut->sendMessage(&faderMsg);
                                    m_lastFaderPosition = 64;
                                    m_faderTouchStartPosition = 64;
                                }
                            }
                        } else {
                            m_faderTapCount = 1;
                        }
                        m_lastFaderTouchTime = currentTime;
                    }
                }
            }

            if (message.size() >= 3 && (message[0] & 0xF0) == 0xB0) {
                if (message[1] == 0x46 && m_faderTouched) {
                    printf("MidiRel: Fader CC %d, touched=%d\n", message[2], 1);
                    handleFaderZoom(message[2], 127);
                }
            }
        }

        // ==================== STANDARD MACKIE MODE (Mode 2) ====================
        if (m_controllerMode == 2) {
            if (message.size() >= 3 && (message[0] & 0xF0) == 0x90) {
                unsigned char note = message[1];
                unsigned char velocity = message[2];

                if (velocity > 0) {
                    switch (note) {
                        case 0x5E: play(); break;
                        case 0x5D: stop(); break;
                        case 0x5F: play(); break;
                        case 0x5B: {
                            size_t currentPos = getPlaybackPosition();
                            size_t jumpFrames = (size_t)(m_sampleRate * 5.0);
                            setPlaybackPosition(currentPos > jumpFrames ? currentPos - jumpFrames : 0);
                        } break;
                        case 0x5C: {
                            size_t totalFrames = getTotalFrames();
                            size_t newPos = getPlaybackPosition() + (size_t)(m_sampleRate * 5.0);
                            setPlaybackPosition(newPos > totalFrames ? totalFrames : newPos);
                        } break;
                        case 0x60: setWaveformZoom(getWaveformZoom() * 1.2f); break;
                        case 0x61: setWaveformZoom(getWaveformZoom() / 1.2f); break;
                        case 0x62: {
                            size_t currentPos = getPlaybackPosition();
                            size_t stepFrames = (size_t)(m_sampleRate * 1.0);
                            setPlaybackPosition(currentPos > stepFrames ? currentPos - stepFrames : 0);
                        } break;
                        case 0x63: {
                            size_t totalFrames = getTotalFrames();
                            size_t newPos = getPlaybackPosition() + (size_t)(m_sampleRate * 1.0);
                            setPlaybackPosition(newPos > totalFrames ? totalFrames : newPos);
                        } break;
                    }
                }
            }

            if (message.size() >= 3 && (message[0] & 0xF0) == 0xB0) {
                unsigned char ccValue = message[2];
                if (message[1] == 0x3C) {
                    int speed = 0;
                    if (ccValue >= 0x01 && ccValue <= 0x3F) speed = ccValue;
                    else if (ccValue >= 0x41 && ccValue <= 0x7F) speed = -(int)(ccValue - 0x40);
                    if (speed != 0) handleJogWheel(speed);
                }
            }
        }

        // ==================== PARK ENCODER PROCESSING ====================
        if (message.size() >= 3 && (message[0] & 0xF0) == 0xB0) {
            unsigned char ccNumber = message[1];
            unsigned char ccValue = message[2];

            int direction = 0;
            if (ccValue == 65) direction = 1;
            else if (ccValue == 1) direction = -1;

            auto now = std::chrono::steady_clock::now();
            double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();

            if (m_selectedPark == 0) {
                size_t totalFrames = getTotalFrames();
                float zoom = getWaveformZoom();
                size_t visibleFrames = (size_t)(totalFrames / zoom);
                if (visibleFrames < 100) visibleFrames = 100;

                if (ccNumber == 16 && direction != 0) {
                    if (totalFrames > 0) {
                        size_t currentPos = getPlaybackPosition();
                        double timeDelta = currentTime - m_lastEncoderTime[0];
                        m_lastEncoderTime[0] = currentTime;

                        float rpm = 0.0f;
                        if (timeDelta > 0.001 && timeDelta < 2.0) {
                            rpm = (1.0f / (float)timeDelta) * (60.0f / 24.0f);
                            if (rpm > 200.0f) rpm = 200.0f;
                        }

                        if (m_rpmAveraging > 0.001f) {
                            float alpha = 1.0f - m_rpmAveraging;
                            m_currentEncoderRpm = alpha * rpm + (1.0f - alpha) * m_currentEncoderRpm;
                        } else {
                            m_currentEncoderRpm = rpm;
                        }

                        float speedMultiplier = 1.0f;
                        if (rpm >= m_scrubRpmThreshold) {
                            speedMultiplier = m_fastSpeedMultiplier;
                        }

                        size_t baseStep = visibleFrames / 72;
                        size_t stepSize = (size_t)(baseStep * speedMultiplier * m_scrubSpeed);
                        if (stepSize < 10) stepSize = 10;

                        size_t newPosition = currentPos;
                        if (direction > 0) {
                            newPosition = currentPos + stepSize;
                            if (newPosition > totalFrames) newPosition = totalFrames;
                        } else {
                            newPosition = (currentPos > stepSize) ? currentPos - stepSize : 0;
                        }
                        setPlaybackPosition(newPosition);
                    }
                }

                if (ccNumber == 17 && direction != 0) {
                    if (totalFrames > 0) {
                        size_t currentPos = getPlaybackPosition();
                        size_t stepSize = visibleFrames / 720;
                        if (stepSize < 10) stepSize = 10;

                        size_t newPosition = currentPos;
                        if (direction < 0) {
                            newPosition = currentPos + stepSize;
                            if (newPosition > totalFrames) newPosition = totalFrames;
                        } else {
                            newPosition = (currentPos > stepSize) ? currentPos - stepSize : 0;
                        }
                        setPlaybackPosition(newPosition);
                    }
                }

                if (ccNumber == 18 && direction != 0) {
                    if (totalFrames > 0) {
                        size_t currentPos = getPlaybackPosition();
                        size_t stepSize = visibleFrames / 7200;
                        if (stepSize < 1) stepSize = 1;

                        size_t newPosition = currentPos;
                        if (direction < 0) {
                            newPosition = currentPos + stepSize;
                            if (newPosition > totalFrames) newPosition = totalFrames;
                        } else {
                            newPosition = (currentPos > stepSize) ? currentPos - stepSize : 0;
                        }
                        setPlaybackPosition(newPosition);
                    }
                }

                if (ccNumber == 19 && direction != 0) {
                    float currentZoom = getWaveformZoom();
                    float zoomStep = currentZoom * 0.1f;
                    if (zoomStep < 0.1f) zoomStep = 0.1f;
                    setWaveformZoom(currentZoom + (-direction * zoomStep));
                }
            }
            else if (m_selectedPark == 1) {
                size_t totalFrames = getTotalFrames();
                float zoom = getWaveformZoom();
                size_t visibleFrames = (size_t)(totalFrames / zoom);
                if (visibleFrames < 100) visibleFrames = 100;

                auto calculateStepSize = [&](int encoderIndex) -> size_t {
                    double timeDelta = currentTime - m_lastEncoderTime[encoderIndex];
                    m_lastEncoderTime[encoderIndex] = currentTime;
                    float rpm = 0.0f;
                    if (timeDelta > 0.001 && timeDelta < 2.0) {
                        rpm = (1.0f / (float)timeDelta) * (60.0f / 24.0f);
                        if (rpm > 200.0f) rpm = 200.0f;
                    }
                    float speedMultiplier = 1.0f;
                    if (rpm >= m_scrubRpmThreshold) speedMultiplier = m_fastSpeedMultiplier;
                    size_t baseStep = visibleFrames / 72;
                    size_t stepSize = (size_t)(baseStep * speedMultiplier * m_scrubSpeed);
                    if (stepSize < 10) stepSize = 10;
                    return stepSize;
                };

                if (ccNumber == 16 && direction != 0) {
                    size_t stepSize = calculateStepSize(0);
                    size_t minGap = stepSize;
                    if (!isMarkerEnabled(0)) {
                        size_t pos = getPlaybackPosition();
                        if (isMarkerEnabled(1) && pos >= getMarkerPosition(1))
                            pos = (getMarkerPosition(1) > minGap) ? getMarkerPosition(1) - minGap : 0;
                        setMarkerPosition(0, pos);
                    } else {
                        size_t pos = getMarkerPosition(0);
                        if (direction > 0) {
                            pos = (pos + stepSize < totalFrames) ? pos + stepSize : totalFrames;
                            if (isMarkerEnabled(1) && pos >= getMarkerPosition(1))
                                pos = (getMarkerPosition(1) > minGap) ? getMarkerPosition(1) - minGap : getMarkerPosition(0);
                        } else {
                            pos = (pos > stepSize) ? pos - stepSize : 0;
                        }
                        setMarkerPosition(0, pos);
                    }
                }
                if (ccNumber == 17 && direction != 0) {
                    size_t stepSize = calculateStepSize(1);
                    size_t minGap = stepSize;
                    if (!isMarkerEnabled(1)) {
                        size_t pos = getPlaybackPosition();
                        if (isMarkerEnabled(0) && pos <= getMarkerPosition(0)) pos = getMarkerPosition(0) + minGap;
                        if (isMarkerEnabled(2) && pos >= getMarkerPosition(2)) pos = (getMarkerPosition(2) > minGap) ? getMarkerPosition(2) - minGap : pos;
                        setMarkerPosition(1, pos);
                    } else {
                        size_t pos = getMarkerPosition(1);
                        if (direction < 0) {
                            pos = (pos + stepSize < totalFrames) ? pos + stepSize : totalFrames;
                            if (isMarkerEnabled(2) && pos >= getMarkerPosition(2)) pos = (getMarkerPosition(2) > minGap) ? getMarkerPosition(2) - minGap : getMarkerPosition(1);
                        } else {
                            pos = (pos > stepSize) ? pos - stepSize : 0;
                            if (isMarkerEnabled(0) && pos <= getMarkerPosition(0)) pos = getMarkerPosition(0) + minGap;
                        }
                        setMarkerPosition(1, pos);
                    }
                }
                if (ccNumber == 18 && direction != 0) {
                    size_t stepSize = calculateStepSize(2);
                    size_t minGap = stepSize;
                    if (!isMarkerEnabled(2)) {
                        size_t pos = getPlaybackPosition();
                        if (isMarkerEnabled(1) && pos <= getMarkerPosition(1)) pos = getMarkerPosition(1) + minGap;
                        if (isMarkerEnabled(3) && pos >= getMarkerPosition(3)) pos = (getMarkerPosition(3) > minGap) ? getMarkerPosition(3) - minGap : pos;
                        setMarkerPosition(2, pos);
                    } else {
                        size_t pos = getMarkerPosition(2);
                        if (direction < 0) {
                            pos = (pos + stepSize < totalFrames) ? pos + stepSize : totalFrames;
                            if (isMarkerEnabled(3) && pos >= getMarkerPosition(3)) pos = (getMarkerPosition(3) > minGap) ? getMarkerPosition(3) - minGap : getMarkerPosition(2);
                        } else {
                            pos = (pos > stepSize) ? pos - stepSize : 0;
                            if (isMarkerEnabled(1) && pos <= getMarkerPosition(1)) pos = getMarkerPosition(1) + minGap;
                        }
                        setMarkerPosition(2, pos);
                    }
                }
                if (ccNumber == 19 && direction != 0) {
                    size_t stepSize = calculateStepSize(3);
                    size_t minGap = stepSize;
                    if (!isMarkerEnabled(3)) {
                        size_t pos = getPlaybackPosition();
                        if (isMarkerEnabled(2) && pos <= getMarkerPosition(2)) pos = getMarkerPosition(2) + minGap;
                        setMarkerPosition(3, pos);
                    } else {
                        size_t pos = getMarkerPosition(3);
                        if (direction < 0) {
                            pos = (pos + stepSize < totalFrames) ? pos + stepSize : totalFrames;
                        } else {
                            pos = (pos > stepSize) ? pos - stepSize : 0;
                            if (isMarkerEnabled(2) && pos <= getMarkerPosition(2)) pos = getMarkerPosition(2) + minGap;
                        }
                        setMarkerPosition(3, pos);
                    }
                }
            }
            // (MIDI park-2 audio-scrub removed â€” audio scrubbing is now
            //  driven by pad 24 held + encoder E6 on the physical controller.)
        }
    }

    // Periodic fader position update for Mackie modes
    if ((m_controllerMode == 1 || m_controllerMode == 2) && !m_faderTouched && m_midiOut) {
        auto now = std::chrono::steady_clock::now();
        double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();

        if (currentTime - m_lastFaderUpdateTime > 0.1) {
            m_lastFaderUpdateTime = currentTime;
            RtMidiOut* midiOut = static_cast<RtMidiOut*>(m_midiOut);
            std::vector<unsigned char> faderMsg = {
                0xE0,
                (unsigned char)(m_lastFaderPosition & 0x7F),
                (unsigned char)((m_lastFaderPosition >> 7) & 0x7F)
            };
            midiOut->sendMessage(&faderMsg);
        }
    }
#endif
}

// ==================== MIDI HELPER METHODS ====================

void AudioEngine::handleJogWheel(int speed) {
    size_t totalFrames = getTotalFrames();
    if (totalFrames == 0) return;

    size_t currentPos = getPlaybackPosition();
    float zoom = getWaveformZoom();
    size_t visibleFrames = (size_t)(totalFrames / zoom);
    if (visibleFrames < 100) visibleFrames = 100;

    size_t stepSize;
    if (std::abs(speed) <= 1) {
        // Modes 0/1: fixed step, direction only
        stepSize = visibleFrames / 72;
    } else {
        // Mode 2: speed-proportional step
        stepSize = (size_t)((visibleFrames / 100.0f) * std::abs(speed));
    }
    if (stepSize < 10) stepSize = 10;

    size_t newPos = currentPos;
    if (speed > 0) {
        newPos = currentPos + stepSize;
        if (newPos > totalFrames) newPos = totalFrames;
    } else {
        newPos = (currentPos > stepSize) ? currentPos - stepSize : 0;
    }
    setPlaybackPosition(newPos);
}

void AudioEngine::handleFaderZoom(int position, int range) {
    if (m_faderTouchStartPosition < 0) {
        m_faderTouchStartPosition = position;
    } else {
        int delta = position - m_faderTouchStartPosition;
        float deltaFraction = (float)delta / (float)range;
        float zoomMultiplier = std::pow(4.0f, deltaFraction);
        float newZoom = getWaveformZoom() * zoomMultiplier;
        setWaveformZoom(newZoom);
        m_faderTouchStartPosition = position;
    }
    m_lastFaderPosition = position;
}

// ==================== ENCODER/TOUCH HANDLERS (called by SerialController) ====================

void AudioEngine::syncPlayLedNow() {
    if (!m_serialController) return;
    if (m_bookmarkScrollMode.load()) return;   // LEDs off during scroll mode
    int want = (isPlaying() || m_scrubResumePending.load()) ? 1 : 0;
    if (want == m_lastPlayLedState) return;
    m_serialController->sendMessage(want ? "LED:3:ON" : "LED:3:OFF");
    m_lastPlayLedState = want;
}

void AudioEngine::syncLoopPairLedsNow() {
    if (!m_serialController) return;
    if (m_bookmarkScrollMode.load()) return;   // LEDs off during scroll mode
    int want = m_loopLeftEnabled.load() ? 1 : 0;
    if (want != m_lastLoopLeftLedState) {
        m_serialController->sendMessage(want ? "LED:4:ON" : "LED:4:OFF");
        m_lastLoopLeftLedState = want;
    }
    int wantR = m_loopRightEnabled.load() ? 1 : 0;
    if (wantR != m_lastLoopRightLedState) {
        m_serialController->sendMessage(wantR ? "LED:7:ON" : "LED:7:OFF");
        m_lastLoopRightLedState = wantR;
    }
}

void AudioEngine::syncRecordPairLedsNow() {
    if (!m_serialController) return;
    if (m_bookmarkScrollMode.load()) return;   // LEDs off during scroll mode
    int want = m_recordLeftEnabled.load() ? 1 : 0;
    if (want != m_lastRecordLeftLedState) {
        m_serialController->sendMessage(want ? "LED:5:ON" : "LED:5:OFF");
        m_lastRecordLeftLedState = want;
    }
    int wantR = m_recordRightEnabled.load() ? 1 : 0;
    if (wantR != m_lastRecordRightLedState) {
        m_serialController->sendMessage(wantR ? "LED:6:ON" : "LED:6:OFF");
        m_lastRecordRightLedState = wantR;
    }
}

void AudioEngine::syncSoloMuteLedsNow() {
    if (!m_serialController) return;
    if (m_bookmarkScrollMode.load()) return;   // LEDs off during scroll mode
    const Track* t = getTrack(getSelectedTrack());
    int wantSolo = (t && t->solo)  ? 1 : 0;
    int wantMute = (t && t->muted) ? 1 : 0;
    int wantArm  = (t && t->armed) ? 1 : 0;
    if (wantSolo != m_lastSoloLedState) {
        m_serialController->sendMessage(wantSolo ? "LED:1:ON" : "LED:1:OFF");
        m_lastSoloLedState = wantSolo;
    }
    if (wantMute != m_lastMuteLedState) {
        m_serialController->sendMessage(wantMute ? "LED:0:ON" : "LED:0:OFF");
        m_lastMuteLedState = wantMute;
    }
    if (wantArm != m_lastArmLedState) {
        m_serialController->sendMessage(wantArm ? "LED:2:ON" : "LED:2:OFF");
        m_lastArmLedState = wantArm;
    }
}

void AudioEngine::syncInputMonitorLedNow() {
    if (!m_serialController) return;
    if (m_bookmarkScrollMode.load()) return;   // all LEDs off during scroll mode
    const Track* t = getTrack(getSelectedTrack());
    int want = (t && t->inputMonitor) ? 1 : 0;
    if (want == m_lastInputMonitorLedState) return;
    m_lastInputMonitorLedState = want;
    m_serialController->sendMessage(want ? "LED:9:ON" : "LED:9:OFF");
}

void AudioEngine::syncMuteFlashNow() {
    if (!m_serialController) return;
    int want = m_bookmarkScrollMode.load() ? 1 : 0;
    if (want == m_lastMuteFlashState) return;
    m_lastMuteFlashState = want;
    m_serialController->sendMessage(want ? "MUTEFLASH:1" : "MUTEFLASH:0");
    if (want == 1) {
        // Entering scroll mode â€” turn every other LED off so the only
        // thing lit is the flashing mute LED. Reset "last sent" caches
        // to 0 so on exit, the per-LED sync functions detect divergence
        // vs the true DAW state and repaint the LEDs back to normal.
        for (int ch = 1; ch <= 9; ch++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "LED:%d:OFF", ch);
            m_serialController->sendMessage(buf);
        }
        m_lastSoloLedState        = 0;
        m_lastArmLedState         = 0;
        m_lastPlayLedState        = 0;
        m_lastLoopLeftLedState    = 0;
        m_lastLoopRightLedState   = 0;
        m_lastRecordLeftLedState  = 0;
        m_lastRecordRightLedState = 0;
        m_lastInputMonitorLedState = 0;
    }
}

void AudioEngine::periodicControllerResync() {
    if (!m_serialController) return;
    if (m_bookmarkScrollMode.load()) return;   // scroll mode owns the LEDs
    // Invalidate the LED shadow caches so each sync-fn detects "unknown"
    // and re-emits the current authoritative state. Firmware may have
    // silently drifted (predictive-LED prediction was wrong, brief serial
    // glitch, whatever) — this corrects it every RESYNC_PERIOD_MS.
    // Deliberately does NOT touch:
    //   • held-state trackers (m_pad12Held, m_pad14Held, …) — those reflect
    //     REAL pad state and clearing them would drop mid-press gestures
    //   • m_startupOledPushed — the OLED is a large diff-driven buffer and
    //     forcing a repush every 2 s would visibly re-render
    //   • mute flash / OSC state — separately owned, not LED-shadow-based
    m_lastPlayLedState          = -1;
    m_lastLoopLeftLedState      = -1;
    m_lastLoopRightLedState     = -1;
    m_lastRecordLeftLedState    = -1;
    m_lastRecordRightLedState   = -1;
    m_lastLoopPairDefinedSent   = -1;
    m_lastRecordPairDefinedSent = -1;
    m_lastSoloLedState          = -1;
    m_lastMuteLedState          = -1;
    m_lastArmLedState           = -1;
    m_lastInputMonitorLedState  = -1;
    syncPlayLedNow();
    syncLoopPairLedsNow();
    syncRecordPairLedsNow();
    syncSoloMuteLedsNow();
    syncInputMonitorLedNow();
    // Re-assert modal flags whose loss on the firmware would silently
    // break the current DAW mode. If DAW is in channel-entry, tell
    // firmware again — cheap and idempotent.
    if ((int)m_channelEntryMode.load() != (int)ChannelEntryMode::None) {
        m_serialController->sendMessage("EXCLUSIVE:1");
        m_serialController->sendMessage("PAIRFLASH:1");
    }
}

void AudioEngine::handleResync() {
    // Invalidate all "last sent" caches so the next updateController tick
    // re-sends every piece of controller-facing state. The firmware just
    // wiped its local mirrors and is waiting for us to repaint.
    m_lastPlayLedState          = -1;
    m_lastLoopLeftLedState      = -1;
    m_lastRecordLeftLedState    = -1;
    m_lastRecordRightLedState   = -1;
    m_lastLoopRightLedState     = -1;
    m_lastLoopPairDefinedSent   = -1;
    m_lastRecordPairDefinedSent = -1;
    m_lastSoloLedState          = -1;
    m_lastMuteLedState          = -1;
    m_lastArmLedState           = -1;
    m_lastMuteFlashState        = -1;
    m_lastInputMonitorLedState  = -1;
    m_lastHeartbeatSendMs       = 0;   // force an immediate HB
    m_startupOledPushed         = false; // force SETMODE:DESC + playback push
    // Held-state flags that live on the DAW side but mirror physical pads
    // are wiped too â€” the firmware just cleared its side, so anything we
    // still thought was held is stale.
    m_modifierHeld.store(false);
    m_panModifierHeld.store(false);
    m_pad3Held.store(false);
    m_pad14Held.store(false);
    m_pad15Held.store(false);
    m_clearMode.store(false);
    m_scrubResumePending.store(false);

    // Firmware just booted (RESYNC = fresh controller). Policy: any
    // DAW-controller reconnect defaults mute OFF, regardless of what the
    // shadow flag held â€” that flag only survives an explicit user
    // session-open. Force flag off + push MUTEIND:0 / OSC unmute.
    m_totalMixInputPairMuted.store(false);
    syncTotalMixMuteToHardware();
}

void AudioEngine::handleRenameBuffer(const std::string& buffer, int cursorPos, bool active) {
    std::lock_guard<std::mutex> lock(m_renameMutex);
    m_renameBuffer = buffer;
    m_renameCursorPos.store(cursorPos);
    m_renameActive.store(active);
}

void AudioEngine::handleRenameSync(int phaseMs) {
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_renameSyncLocalMs.store(nowMs);
    m_renameSyncFwPhaseMs.store(phaseMs);
}

float AudioEngine::getLedFlashBrightness() const {
    int64_t syncLocal = m_renameSyncLocalMs.load();
    if (syncLocal == 0) return 0.0f;
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int elapsed = (int)((nowMs - syncLocal) + m_renameSyncFwPhaseMs.load());
    int phase = ((elapsed % FLASH_PERIOD_MS) + FLASH_PERIOD_MS) % FLASH_PERIOD_MS;
    int half = FLASH_PERIOD_MS / 2;
    // Same triangle-wave formula as the firmware LED flash: linear ramp
    // 0->1 for the first half of the period, 1->0 for the second half.
    float b = (phase < half)
        ? (float)phase / (float)half
        : (float)(FLASH_PERIOD_MS - phase) / (float)half;
    return b;
}

bool AudioEngine::getRenameBuffer(std::string& out, int& cursorPosOut) const {
    if (!m_renameActive.load()) return false;
    std::lock_guard<std::mutex> lock(m_renameMutex);
    out = m_renameBuffer;
    cursorPosOut = m_renameCursorPos.load();
    return true;
}

void AudioEngine::handleTrackNameFromController(const std::string& name) {
    // Bookmark naming takes precedence â€” a pending bookmark index means
    // pad 13 just requested TEXTIN and we now have the completed name.
    int bmIdx = m_pendingNameBookmarkIndex.exchange(-1);
    if (bmIdx >= 0) {
        {
            std::lock_guard<std::mutex> lock(m_bookmarksMutex);
            if (bmIdx < (int)m_bookmarkFrames.size()) {
                m_bookmarkFrames[bmIdx].name = name;
            }
        }
        markSessionDirty();
        pushPlaybackStateToOled();
        return;
    }
    int idx = m_pendingNameTrackIndex.exchange(-1);
    if (idx < 0) return;
    Track* t = getTrack(idx);
    if (!t) return;
    if (!name.empty()) { t->name = name; markSessionDirty(); }
    // Silent â€” repaint the persistent OLED playback state so the "TRACK N
    // ADDED,NAME:" prompt line is replaced immediately.
    pushPlaybackStateToOled();
}

void AudioEngine::handleModeChange(bool descriptive) {
    m_diagnosticMode.store(!descriptive);
    // Wipe any held-state trackers so descriptive mode starts from a clean
    // slate. Anything the user was pressing during diagnostic mode wasn't
    // seen by the DAW, so we cannot trust the previously-tracked state.
    m_modifierHeld.store(false);
    m_panModifierHeld.store(false);
    m_pad3Held.store(false);
    m_pad14Held.store(false);
    m_pad15Held.store(false);
    m_clearMode.store(false);
    m_scrubResumePending.store(false);
}

void AudioEngine::oledShow(const char* line1, const char* line2) {
    if (!m_serialController) return;
    // Protocol: two "DISP:<line>:<text>" commands so firmware writes both
    // lines back-to-back. Every event replaces the full screen state.
    std::string m1 = "DISP:1:"; m1 += line1;
    std::string m2 = "DISP:2:"; m2 += line2;
    m_serialController->sendMessage(m1);
    m_serialController->sendMessage(m2);
}

void AudioEngine::oledShowHold(const char* line1, const char* line2) {
    if (!m_serialController) return;
    // Protocol: two "DISPHOLD:<line>:<text>" commands. Firmware writes only
    // in descriptive mode and holds both lines for ~3 s (user activity
    // releases the hold).
    std::string m1 = "DISPHOLD:1:"; m1 += line1;
    std::string m2 = "DISPHOLD:2:"; m2 += line2;
    m_serialController->sendMessage(m1);
    m_serialController->sendMessage(m2);
}

void AudioEngine::pushPlaybackStateToOled() {
    oledShowForce(isPlaying() ? "PLAYING" : "STOPPED", " ");
}

void AudioEngine::oledShowForce(const char* line1, const char* line2) {
    if (!m_serialController) return;
    // Protocol: two "DISPFORCE:<line>:<text>" commands. Firmware writes in
    // both display modes â€” used for live status readouts inside interactive
    // modes like clear-markers.
    std::string m1 = "DISPFORCE:1:"; m1 += line1;
    std::string m2 = "DISPFORCE:2:"; m2 += line2;
    m_serialController->sendMessage(m1);
    m_serialController->sendMessage(m2);
}

void AudioEngine::enableMarkerAtDefault(int markerIndex, double fractionFromLeft) {
    if (markerIndex < 0 || markerIndex > 3) return;

    if (m_markerEverSet[markerIndex]) {
        // Restore at preserved position â€” clearMarker() left it intact.
        setMarkerPosition(markerIndex, getMarkerPosition(markerIndex));
        return;
    }

    // First-time placement: position at a fraction of the current viewport.
    // The old code clamped to getTotalFrames(), which is 0 in an empty
    // session â€” that made every marker fall at frame 0. Use the viewport
    // directly if the GUI has published one; only fall back to whole-track
    // if not.
    size_t start  = m_viewStartFrame.load();
    size_t frames = m_viewVisibleFrames.load();
    if (frames == 0) {
        size_t total = getTotalFrames();
        start  = 0;
        frames = total;
    }
    size_t pos = start + (size_t)(fractionFromLeft * (double)frames);
    setMarkerPosition(markerIndex, pos);
}

void AudioEngine::handleEncoderDelta(int encoder, long delta, float rpm, float velocityMultiplier) {
    // Diagnostic mode: controller is inert. Firmware still sends encoder
    // deltas so its OLED can show them, but the DAW must not act on them.
    if (m_diagnosticMode.load()) return;

    // Marker-scroll mode: only encoder 1 (marker nav), encoder 2 (zoom
    // / pan) and encoder 3 (move the selected marker) stay active.
    if (m_bookmarkScrollMode.load() && encoder != 1 && encoder != 2 && encoder != 3) return;

    // Undo coalescing: treat successive deltas from the same encoder as
    // ONE user gesture — captures state BEFORE the burst begins, and
    // stays quiet until the user has been idle for > 500 ms (then the
    // next tick is a new gesture). Note: activity-based, not
    // snapshot-based — mid-gesture ticks don't create new entries
    // however long the gesture runs.
    //
    // View snapshot rather than content, because every encoder branch
    // in this handler is view-mutating (scrub, zoom, pan, marker nudge,
    // audio scrub). The two-stack undo split (pad 36 tap = CONTENT,
    // pad 36 held + pad 20/21/22/23 = VIEW) reads from this stack.
    if (encoder >= 1 && encoder <= 6) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int idx = encoder - 1;
        if (m_encoderLastDeltaMs[idx] == 0 ||
            nowMs - m_encoderLastDeltaMs[idx] > 500) {
            undoSnapshotView();
        }
        m_encoderLastDeltaMs[idx] = nowMs;
    }

    // Nudging any marker encoder (E3-E6) while clear-markers mode is on
    // is an implicit exit â€” the user has moved from pair-management to
    // marker-positioning. Firmware exits its own flashMode symmetrically.
    if (m_clearMode.load() && encoder >= 3 && encoder <= 6) {
        m_clearMode.store(false);
        pushPlaybackStateToOled();
    }

    if (encoder == 1) {
        // Pad-13 + E1: walk through markers in frame order (left-to-right
        // on the timeline), clamped at both ends, no wrap. Show the
        // current marker's name on the OLED and ask the GUI to reposition
        // the view so the marker sits 15% from the left. Requires 200
        // encoder ticks per step so a nudge doesn't skip several markers.
        // Once entered (via encoder move during a pad-13 hold), scroll
        // mode STAYS ACTIVE after pad 13 is released â€” the user exits
        // by pressing pad 13 again.
        if (m_pad13Held.load() || m_bookmarkScrollMode.load()) {
            // (View-undo snapshot handled by the per-encoder burst check.)
            m_pad13UsedAsModifier.store(true);
            m_bookmarkScrollMode.store(true);
            // Snapshot bookmarks with their ORIGINAL indices so E3 can
            // later move the selected marker by original index (stable
            // across frame reshuffles).
            struct SortEntry { int origIdx; size_t frame; std::string name; };
            std::vector<SortEntry> sorted;
            {
                std::lock_guard<std::mutex> lock(m_bookmarksMutex);
                sorted.reserve(m_bookmarkFrames.size());
                for (size_t i = 0; i < m_bookmarkFrames.size(); i++) {
                    sorted.push_back({(int)i,
                                       m_bookmarkFrames[i].frame,
                                       m_bookmarkFrames[i].name});
                }
            }
            if (sorted.empty()) {
                oledShowForce("MARKER:", "(no markers)");
                return;
            }
            std::sort(sorted.begin(), sorted.end(),
                      [](const SortEntry& a, const SortEntry& b) {
                          return a.frame < b.frame;
                      });
            // Find the sort-list position for the current selection (by
            // original index). -1 = no selection yet.
            int curSortPos = -1;
            if (m_bookmarkNavIdx >= 0) {
                for (int i = 0; i < (int)sorted.size(); i++) {
                    if (sorted[i].origIdx == m_bookmarkNavIdx) { curSortPos = i; break; }
                }
            }
            // First encoder tick of a nav session: jump to the marker
            // closest to the playhead REGARDLESS of turn direction. Any
            // delta on this entry-turn is consumed by the entry itself
            // (doesn't count toward the next 400-tick step).
            if (curSortPos < 0) {
                size_t playPos = m_playbackPosition.load();
                int closest = 0;
                size_t bestDist = (size_t)-1;
                for (int i = 0; i < (int)sorted.size(); i++) {
                    size_t d = (sorted[i].frame > playPos)
                             ? sorted[i].frame - playPos
                             : playPos - sorted[i].frame;
                    if (d < bestDist) { bestDist = d; closest = i; }
                }
                m_bookmarkNavIdx   = sorted[closest].origIdx;
                m_bookmarkNavAccum = 0;
                const auto& bm = sorted[closest];
                const char* name = bm.name.empty() ? "(unnamed)" : bm.name.c_str();
                oledShowForce("MARKER:", name);
                m_playbackPosition.store(bm.frame);
                m_requestedJumpFrame.store((int64_t)bm.frame);
                dawLog("bookmark nav (entry) â†’ orig=%d frame=%zu name='%s'",
                       m_bookmarkNavIdx, bm.frame, name);
                return;
            }
            // Subsequent turns: accumulate to 400 ticks per step.
            long prev = m_bookmarkNavAccum;
            if ((prev > 0 && delta < 0) || (prev < 0 && delta > 0)) {
                m_bookmarkNavAccum = 0;
            }
            m_bookmarkNavAccum += delta;
            const long ticksPerStep = 400;
            if (std::abs(m_bookmarkNavAccum) < ticksPerStep) return;
            int step = (m_bookmarkNavAccum > 0) ? 1 : -1;
            m_bookmarkNavAccum = 0;
            int nextSort = curSortPos + step;
            if (nextSort < 0) nextSort = 0;
            if (nextSort >= (int)sorted.size()) nextSort = (int)sorted.size() - 1;
            if (nextSort == curSortPos) return;
            m_bookmarkNavIdx = sorted[nextSort].origIdx;
            const auto& bm = sorted[nextSort];
            const char* name = bm.name.empty() ? "(unnamed)" : bm.name.c_str();
            oledShowForce("MARKER:", name);
            m_playbackPosition.store(bm.frame);
            m_requestedJumpFrame.store((int64_t)bm.frame);
            dawLog("bookmark nav â†’ orig=%d frame=%zu name='%s'",
                   m_bookmarkNavIdx, bm.frame, name);
            return;
        }
        // E1: Playhead scrub. (View-undo snapshot already handled by the
        // per-encoder burst check at the top of this handler.)
        // If we're playing, pause and arm the resume timer so playback
        // picks up 100 ms after the user stops moving.
        double nowSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_lastScrubMoveTime.store(nowSec);

        if (isPlaying()) {
            stop();
            m_scrubResumePending.store(true);
        }

        // Use the shared timeline extent (falls back to totalFrames if
        // the GUI hasn't pushed one yet) â€” otherwise a fresh session with
        // no audio can't scrub the playhead around at all.
        size_t totalFrames = getTimelineFrames();
        if (totalFrames > 0) {
            size_t currentPos = getPlaybackPosition();
            float zoom = getWaveformZoom();
            size_t visibleFrames = (size_t)(totalFrames / zoom);
            if (visibleFrames < 100) visibleFrames = 100;

            VelocityCurve& curve = m_serialController->getVelocityCurve();
            // Silent-scrub (E1) sensitivity from the SILENT SCRUB slider.
            double framesPerPulse = (double)visibleFrames / 2400.0 * curve.baseMultiplier
                                     * velocityMultiplier * (double)m_silentScrubSpeed;
            long frameDelta = (long)(delta * framesPerPulse);

            long newPlayPos = (long)currentPos + frameDelta;
            if (newPlayPos < 0) newPlayPos = 0;
            if (newPlayPos > (long)totalFrames) newPlayPos = totalFrames;

            setPlaybackPosition((size_t)newPlayPos);
            // Keep the "return-on-stop" anchor in sync with the scrubbed
            // position. Without this, the stop() we issued to pause for
            // the scrub would fire a return-jump at fade-out end and
            // clobber the scrub back to wherever we originally hit play.
            m_playStartPosition.store((size_t)newPlayPos);
        }
    }
    else if (encoder == 2) {
        // E2: pan the timeline while pad 24 is held, otherwise zoom.
        // (View-undo snapshot handled by the per-encoder burst check.)
        if (m_panModifierHeld.load()) {
            size_t totalFrames = getTimelineFrames();
            if (totalFrames > 0) {
                float zoom = getWaveformZoom();
                size_t visibleFrames = (size_t)(totalFrames / zoom);
                if (visibleFrames < 100) visibleFrames = 100;
                VelocityCurve& curve = m_serialController->getVelocityCurve();
                long scrollAmount = (long)(-delta * (double)visibleFrames / 2400.0 * curve.baseMultiplier);
                m_viewScrollDelta.fetch_add(scrollAmount);
                printf("[PAN] delta=%ld scroll=%+ld visibleFrames=%zu playhead=%zu playing=%d\n",
                       delta, scrollAmount, visibleFrames, getPlaybackPosition(), (int)isPlaying());
            }
        } else {
            // E2 zoom is 20% faster per encoder tick â€” the base was
            // 1.0005^delta; scaling the exponent by 1.2 gives the same
            // per-tick zoom step 20% larger without changing anything
            // else about the log-scaled feel.
            float currentZoom = getWaveformZoom();
            float newZoom = currentZoom * std::pow(1.0005f, (float)delta * 1.2f);
            setWaveformZoom(newZoom);
        }
    }
    else if (encoder == 3 && m_bookmarkScrollMode.load()) {
        // (View-undo snapshot handled by the per-encoder burst check.)
        // Marker-scroll mode + E3: nudge the currently selected marker's
        // frame. Selection is by original index so a frame edit doesn't
        // shuffle what E1 walks between.
        if (m_bookmarkNavIdx < 0) return;
        size_t totalFrames = getTimelineFrames();
        if (totalFrames == 0) return;
        float zoom = getWaveformZoom();
        size_t visibleFrames = (size_t)((double)totalFrames / (double)zoom);
        if (visibleFrames < 100) visibleFrames = 100;
        // Same responsiveness scaling as scrub: about "screen width /
        // 2400 ticks", so a full rotation moves the marker about one
        // visible-window-width for a typical encoder.
        VelocityCurve& curve = m_serialController->getVelocityCurve();
        double framesPerPulse = (double)visibleFrames / 2400.0
                              * curve.baseMultiplier * velocityMultiplier;
        long frameDelta = (long)((double)delta * framesPerPulse);
        {
            std::lock_guard<std::mutex> lock(m_bookmarksMutex);
            if (m_bookmarkNavIdx < (int)m_bookmarkFrames.size()) {
                long long newFrame = (long long)m_bookmarkFrames[m_bookmarkNavIdx].frame
                                   + (long long)frameDelta;
                if (newFrame < 0) newFrame = 0;
                if ((size_t)newFrame > totalFrames) newFrame = (long long)totalFrames;
                m_bookmarkFrames[m_bookmarkNavIdx].frame = (size_t)newFrame;
                // Snap the playhead and request a view jump if the marker
                // has moved off-screen. GUI decides whether to reposition.
                m_playbackPosition.store((size_t)newFrame);
                m_requestedJumpFrame.store(newFrame);
            }
        }
        markSessionDirty();
        return;
    }
    else if (encoder == 3 && m_modifierHeld.load()) {
        // Mark the pad-26 hold as "used as a modifier" so tap-release
        // doesn't misfire an undo.
        m_pad26Consumed = true;
        // Modifier + E3: Scroll timeline view
        size_t totalFrames = getTimelineFrames();
        if (totalFrames > 0) {
            float zoom = getWaveformZoom();
            size_t visibleFrames = (size_t)(totalFrames / zoom);
            if (visibleFrames < 100) visibleFrames = 100;

            VelocityCurve& curve = m_serialController->getVelocityCurve();
            long scrollAmount = (long)(delta * (double)visibleFrames / 2400.0 * curve.baseMultiplier * velocityMultiplier);
            m_viewScrollDelta.fetch_add(scrollAmount);
        }
    }
    else if (encoder == 6 && m_panModifierHeld.load()) {
        // Pad 24 + E6: audio scrub. Rate is set DIRECTLY from the encoder
        // delta on every tick â€” no accumulator, no coast. When the user
        // stops turning, updateController() zeros the rate within the
        // short SCRUB_TIMEOUT_S window and audio stops immediately.
        //
        // Sensitivity: m_scrubSpeed (Audio Scrub Speed slider). At 1.0 a
        // delta of 1 gives a rate of ~0.5x (half speed). Fast mode kicks
        // in when |delta| >= (m_scrubRpmThreshold / 10) and multiplies by
        // m_fastSpeedMultiplier.
        float rate = m_scrubSpeed * 0.5f * (float)delta;
        if ((float)std::abs(delta) * 10.0f >= m_scrubRpmThreshold) {
            rate *= m_fastSpeedMultiplier;
        }
        if (rate >  8.0f) rate =  8.0f;
        if (rate < -8.0f) rate = -8.0f;
        m_scrubPlaybackRate.store(rate);
        if (!m_scrubbing.load()) {
            m_scrubbing.store(true);
            m_scrubPlaybackPosition = (double)getPlaybackPosition();
        }
        double nowSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_lastAudioScrubMs.store(nowSec);
        return;
    }
    else if (encoder >= 3 && encoder <= 6) {
        // E3-E6: adjust markers 0-3. A disabled/hidden marker is inert here â€”
        // markers can only be created via clear-mode restore (which places
        // them at the pair's default viewport fraction on first use).
        int markerIdx = encoder - 3;
        if (!isMarkerEnabled(markerIdx)) return;

        // Use the shared timeline extent (falls back to audio length when
        // GUI hasn't pushed a value) so markers can still be nudged in an
        // empty session with no audio anywhere.
        size_t totalFrames = getTimelineFrames();
        if (totalFrames == 0) return;

        float zoom = getWaveformZoom();
        size_t visibleFrames = (size_t)(totalFrames / zoom);
        if (visibleFrames < 100) visibleFrames = 100;
        // Marker step is 1/9600 of the visible range per encoder tick â€”
        // half the previous 1/4800 rate â€” so marker placement is finer.
        size_t stepSize = (size_t)((double)visibleFrames / 9600.0 * std::abs(delta));
        if (stepSize < 1) stepSize = 1;

        // Pair-only clamping (loop-left <-> loop-right, record-left <-> record-right).
        size_t lowerBound = 0;
        bool   hasLower   = false;
        size_t upperBound = totalFrames;
        bool   hasUpper   = false;
        int partnerAbove = -1, partnerBelow = -1;
        if      (markerIdx == 0) partnerAbove = 3;
        else if (markerIdx == 3) partnerBelow = 0;
        else if (markerIdx == 1) partnerAbove = 2;
        else if (markerIdx == 2) partnerBelow = 1;
        if (partnerAbove >= 0 && isMarkerEnabled(partnerAbove)) {
            upperBound = getMarkerPosition(partnerAbove);
            hasUpper = true;
        }
        if (partnerBelow >= 0 && isMarkerEnabled(partnerBelow)) {
            lowerBound = getMarkerPosition(partnerBelow);
            hasLower = true;
        }

        size_t pos = getMarkerPosition(markerIdx);
        if (delta > 0) {
            pos = (pos + stepSize < totalFrames) ? pos + stepSize : totalFrames;
        } else {
            pos = (pos > stepSize) ? pos - stepSize : 0;
        }
        if (hasLower && pos <= lowerBound + stepSize)
            pos = lowerBound + stepSize;
        if (hasUpper && pos + stepSize >= upperBound)
            pos = (upperBound > stepSize) ? upperBound - stepSize : lowerBound + stepSize;
        setMarkerPosition(markerIdx, pos);
    }
}

// Controller pad -> PCA9685 LED channel mapping. Established by walking the
// LEDs one at a time and having the user identify each. Not all pads have an
// LED (only the 9 in the transport/marker cluster do).
//
//   pad 16 -> LED 2  (record)
//   pad 17 -> LED 1  (solo)
//   pad 18 -> LED 0  (mute)
//   pad 19 -> LED 3  (play)
//   pad 20 -> LED 4  (loop-left)
//   pad 21 -> LED 5  (record-left)
//   pad 22 -> LED 6  (record-right)
//   pad 23 -> LED 7  (loop-right)
//   pad 24 -> LED 8  (orange)
static int ledChannelForPad(int pad) {
    switch (pad) {
        case 16: return 2;
        case 17: return 1;
        case 18: return 0;
        case 19: return 3;
        case 20: return 4;
        case 21: return 5;
        case 22: return 6;
        case 23: return 7;
        case 24: return 8;
        default: return -1;
    }
}

// -------------- handleTouch dispatch helpers --------------
// Extracted from a 340-line switch of nested ifs. Each helper is either
// a self-contained "was it consumed?" fragment (returns bool) or a
// per-pad worker (void). Behaviour is unchanged â€” pure code motion.

bool AudioEngine::touchHandleDeletePairSentinel(int pad) {
    // Sentinel 202/203 = RETURNONSTOP:1/0 combo from firmware.
    if (pad == 202) { m_returnToStartOnStop.store(true);  return true; }
    if (pad == 203) { m_returnToStartOnStop.store(false); return true; }
    // Sentinel 204 = double-tap on pad 15: toggle loop playback. Distinct
    // from the loop markers existing â€” the markers stay exactly where they
    // are and stay visible; only the wrap-around is switched off.
    if (pad == 204) {
        bool now = !m_loopPlaybackEnabled.load();
        m_loopPlaybackEnabled.store(now);
        markSessionDirty();
        oledShow(now ? "LOOP PLAY: ON" : "LOOP PLAY: OFF", " ");
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_oledRevertAtMs.store(nowMs + 1500);
        dawLog("Loop playback %s", now ? "ON" : "OFF");
        return true;
    }
    // Sentinel pads 200/201 arrive from SerialController when the
    // firmware sends DELETEPAIR:LOOP or DELETEPAIR:REC. Runs even in
    // diagnostic mode because it's a deliberate user gesture.
    if (pad != 200 && pad != 201) return false;
    if (pad == 200) {
        resetMarker(0); resetMarker(3);
        m_loopLeftEnabled.store(false);
        m_loopRightEnabled.store(false);
        syncLoopPairLedsNow();
    } else {
        resetMarker(1); resetMarker(2);
        m_recordLeftEnabled.store(false);
        m_recordRightEnabled.store(false);
        syncRecordPairLedsNow();
    }
    // Refresh the clear-mode status readout if we're still in it.
    if (m_clearMode.load()) {
        bool loopDef = markerEverSet(0) && markerEverSet(3);
        bool recDef  = markerEverSet(1) && markerEverSet(2);
        const char* loopTxt = m_loopLeftEnabled.load()   ? "LOOP: ON"
                            : (loopDef                    ? "LOOP: OFF" : "LOOP: NONE");
        const char* recTxt  = m_recordLeftEnabled.load() ? "REC: ON"
                            : (recDef                     ? "REC: OFF"  : "REC: NONE");
        oledShowForce(loopTxt, recTxt);
    }
    return true;
}

bool AudioEngine::touchHandlePad26(bool pressed) {
    // A press while already in clear-markers mode exits the mode instead
    // of setting the modifier bit (matches firmware exit gesture). Also
    // counts as a "consumed" use so the tap-release doesn't undo.
    if (pressed && m_clearMode.load()) {
        m_clearMode.store(false);
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_oledRevertAtMs.store(nowMs + 2000);
        m_pad26Consumed = true;
        return true;
    }

    if (pressed) {
        // Start of a new hold â€” reset the "was it used as a modifier?"
        // guard so a bare tap can fire undo on release.
        m_modifierHeld.store(true);
        m_pad26Consumed = false;
    } else {
        // Release: if nobody used the modifier during this hold, treat
        // the tap as an undo request.
        bool consumed = m_pad26Consumed;
        m_modifierHeld.store(false);
        m_pad26Consumed = false;
        if (!consumed) {
            undoPop();
        }
    }
    return true;
}

bool AudioEngine::touchHandlePad15Press() {
    // Toggles loop-edit / clear-markers mode. Firmware mirrors the same
    // toggle locally on press so LEDs update instantly.
    bool wasOn = m_clearMode.load();
    bool nowOn = !wasOn;
    m_clearMode.store(nowOn);
    {
        // Debug: record the clearMode transition to ctrl.log on the same
        // session-relative timeline as the serial log.
        static int64_t s_startMs = 0;
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (s_startMs == 0) s_startMs = nowMs;
        std::ofstream lg(appPath("ctrl.log"), std::ios::app);
        if (lg.is_open())
            lg << (nowMs - s_startMs) << " !! DAW pad15 press: clearMode "
               << wasOn << " -> " << nowOn << "\n";
    }
    if (!nowOn) {
        pushPlaybackStateToOled();
    } else {
        bool loopDef = markerEverSet(0) && markerEverSet(3);
        bool recDef  = markerEverSet(1) && markerEverSet(2);
        const char* loopTxt = m_loopLeftEnabled.load()   ? "LOOP: ON"
                            : (loopDef                    ? "LOOP: OFF" : "LOOP: NONE");
        const char* recTxt  = m_recordLeftEnabled.load() ? "REC: ON"
                            : (recDef                     ? "REC: OFF"  : "REC: NONE");
        oledShowForce(loopTxt, recTxt);
    }
    return true;
}

bool AudioEngine::touchHandlePad12(bool pressed) {
    // Pad 12 â€” short tap = add track (queued for main thread), long-press
    // triggers rename, and modifier+12 = delete. Rate-limited so light
    // MPR121 grazes don't spawn a burst of tracks.
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (pressed) {
        m_pad12Held.store(true);
        if (m_modifierHeld.load()) {
            m_pad26Consumed = true;   // pad 26 tap-release must not undo
            m_pendingDeleteTrackRequest.store(true);
            m_pad12LongPressFired.store(true);
            m_pad12PressTimeMs.store(0);
            m_modifierHeld.store(false);
            return true;
        }
        m_pad12PressTimeMs.store(nowMs);
        m_pad12LongPressFired.store(false);
    } else {
        m_pad12Held.store(false);
        m_pad12PressTimeMs.store(0);
        if (m_pad12LongPressFired.exchange(false)) return true;
        int64_t lastMs = m_lastAddTrackMs.load();
        if (nowMs - lastMs >= ADD_TRACK_MIN_INTERVAL_MS) {
            m_lastAddTrackMs.store(nowMs);
            m_pendingAddTrackRequest.store(true);
        }
    }
    return true;
}

void AudioEngine::touchHandlePairInClearMode(int pad) {
    // Toggle pair on/off in clear-markers mode. Loop pair (20/23) or
    // record pair (21/22). First-time enable places at defaults; toggle
    // off preserves marker positions for later restore.
    if (pad == 20 || pad == 23) {
        bool wasOn = m_loopLeftEnabled.load();
        if (wasOn) {
            clearMarker(0);
            clearMarker(3);
            m_loopLeftEnabled.store(false);
            m_loopRightEnabled.store(false);
        } else {
            enableMarkerAtDefault(0, 0.15);
            enableMarkerAtDefault(3, 0.85);
            m_loopLeftEnabled.store(true);
            m_loopRightEnabled.store(true);
        }
        syncLoopPairLedsNow();
    } else if (pad == 21 || pad == 22) {
        bool wasOn = m_recordLeftEnabled.load();
        if (wasOn) {
            clearMarker(1);
            clearMarker(2);
            m_recordLeftEnabled.store(false);
            m_recordRightEnabled.store(false);
        } else {
            // First-time record placement anchors to the loop pair if
            // it's already set (33% and 66% between markers 0 and 3).
            bool loopSet = isMarkerEnabled(0) && isMarkerEnabled(3);
            bool rec1First = !markerEverSet(1);
            bool rec2First = !markerEverSet(2);
            if (loopSet && (rec1First || rec2First)) {
                size_t a = getMarkerPosition(0);
                size_t b = getMarkerPosition(3);
                if (b > a) {
                    size_t range = b - a;
                    if (rec1First) setMarker(1, a + (size_t)(range * 0.33), true);
                    else           enableMarkerAtDefault(1, 0.25);
                    if (rec2First) setMarker(2, a + (size_t)(range * 0.66), true);
                    else           enableMarkerAtDefault(2, 0.75);
                } else {
                    enableMarkerAtDefault(1, 0.25);
                    enableMarkerAtDefault(2, 0.75);
                }
            } else {
                enableMarkerAtDefault(1, 0.25);
                enableMarkerAtDefault(2, 0.75);
            }
            m_recordLeftEnabled.store(true);
            m_recordRightEnabled.store(true);
        }
        syncRecordPairLedsNow();
    }
    // Refresh the live clear-mode status display after any toggle.
    {
        bool loopDef = markerEverSet(0) && markerEverSet(3);
        bool recDef  = markerEverSet(1) && markerEverSet(2);
        const char* loopTxt = m_loopLeftEnabled.load()   ? "LOOP: ON"
                            : (loopDef                    ? "LOOP: OFF" : "LOOP: NONE");
        const char* recTxt  = m_recordLeftEnabled.load() ? "REC: ON"
                            : (recDef                     ? "REC: OFF"  : "REC: NONE");
        oledShowForce(loopTxt, recTxt);
    }
}

void AudioEngine::touchHandlePairPad(int markerIdx) {
    // Pad 20/21/22/23 in normal mode. Four-state:
    //   not defined      -> create pair at defaults + enable, NO jump
    //   defined disabled -> re-enable at preserved positions, NO jump
    //   defined enabled  -> jump playhead to that marker
    //   defined enabled + double-tap same pad within 400 ms
    //                    -> also recenter the arrangement view on the
    //                       marker (via m_requestedJumpFrame â€” same
    //                       mechanism bookmark nav uses)
    bool loopSide = (markerIdx == 0 || markerIdx == 3);
    int a = loopSide ? 0 : 1;
    int b = loopSide ? 3 : 2;
    std::atomic<bool>& leftEnabled  = loopSide ? m_loopLeftEnabled   : m_recordLeftEnabled;
    std::atomic<bool>& rightEnabled = loopSide ? m_loopRightEnabled  : m_recordRightEnabled;
    bool defined = markerEverSet(a) && markerEverSet(b);
    bool wasEnabled = leftEnabled.load();
    if (!defined) {
        if (loopSide) {
            enableMarkerAtDefault(0, 0.15);
            enableMarkerAtDefault(3, 0.85);
        } else {
            bool loopSet = isMarkerEnabled(0) && isMarkerEnabled(3);
            if (loopSet) {
                size_t la = getMarkerPosition(0);
                size_t lb = getMarkerPosition(3);
                if (lb > la) {
                    size_t range = lb - la;
                    setMarker(1, la + (size_t)(range * 0.33), true);
                    setMarker(2, la + (size_t)(range * 0.66), true);
                } else {
                    enableMarkerAtDefault(1, 0.25);
                    enableMarkerAtDefault(2, 0.75);
                }
            } else {
                enableMarkerAtDefault(1, 0.25);
                enableMarkerAtDefault(2, 0.75);
            }
        }
        leftEnabled.store(true);
        rightEnabled.store(true);
    } else if (!wasEnabled) {
        enableMarkerAtDefault(a, loopSide ? 0.15 : 0.25);
        enableMarkerAtDefault(b, loopSide ? 0.85 : 0.75);
        leftEnabled.store(true);
        rightEnabled.store(true);
    } else if (isMarkerEnabled(markerIdx)) {
        size_t markerPos = getMarkerPosition(markerIdx);
        // View-undo snapshot before jumping the playhead to the marker
        // (and possibly panning the view on a double-tap). Coalesced so
        // a rapid double-tap counts as one entry.
        undoSnapshotViewIfBurst();
        // Position BEFORE this tap's playhead move. Stored for the NEXT
        // tap so that if a double-tap sequence lands within 400 ms, we
        // can still recover the "pre-first-tap" playhead position to
        // use as the view-pan anchor.
        size_t prevPos = getPlaybackPosition();
        double nowSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        bool isDoubleTap = (markerIdx == m_lastPairPadIdx &&
                            (nowSec - m_lastPairPadTapSec) < 0.4);
        setPlaybackPosition(markerPos);
        // Double-tap: pan the arrangement view so the marker sits under
        // wherever the play line was BEFORE the first of the two taps
        // moved it (m_lastPairPadAnchor was captured then). Zoom
        // unchanged. Anchor==target on a first-tap is what we want on a
        // fresh single tap â€” no view shift there.
        if (isDoubleTap) {
            m_requestedJumpAnchorFrame.store((int64_t)m_lastPairPadAnchor);
            m_requestedJumpFrame.store((int64_t)markerPos);
        }
        m_lastPairPadIdx    = markerIdx;
        m_lastPairPadTapSec = nowSec;
        m_lastPairPadAnchor = prevPos;
    }
    if (loopSide) syncLoopPairLedsNow();
    else          syncRecordPairLedsNow();
}

void AudioEngine::toggleTotalMixStripMute(int stripIndex, const char* label) {
    bool muted = !m_totalMixInputPairMuted.load();
    m_totalMixInputPairMuted.store(muted);
    float v = muted ? 1.0f : 0.0f;
    if (m_osc) {
        char addr[32];
        std::snprintf(addr, sizeof(addr), "/1/mute/1/%d", stripIndex);
        m_osc->sendFloat(addr, v);
    }
    oledShowForce(label, muted ? "MUTED" : "UNMUTED");
    // Push a persistent bar on the far-left of the OLED that outlives the
    // 2-line text render (see firmware oledDrawMuteIndicator).
    if (m_serialController) {
        m_serialController->sendMessage(muted ? "MUTEIND:1" : "MUTEIND:0");
    }
    markSessionDirty();
    dawLog("OSC mute strip %d (%s) = %s (osc=%s)",
           stripIndex, label,
           muted ? "MUTE" : "UNMUTE",
           m_osc ? "on" : "off");
}

void AudioEngine::syncTotalMixMuteToHardware() {
    // Same wire messages as a toggle, minus the flag flip + OLED text.
    // Used to force external state to match the shadow flag (startup,
    // after session load). Silent if OSC / serial aren't up â€” the caller
    // is expected to retry via the toggle path if the user acts.
    bool muted = m_totalMixInputPairMuted.load();
    if (m_osc) m_osc->sendFloat("/1/mute/1/2", muted ? 1.0f : 0.0f);
    if (m_serialController) {
        m_serialController->sendMessage(muted ? "MUTEIND:1" : "MUTEIND:0");
    }
    dawLog("OSC sync: MADI 1-2 mute = %s (osc=%s serial=%s)",
           muted ? "MUTE" : "UNMUTE",
           m_osc            ? "on" : "off",
           m_serialController ? "on" : "off");
}

// -------------- Antelope input-monitor plumbing --------------

// Return the 1-based mixer channel id(s) that a track's output routes
// to. `out[0]` and `out[1]` are filled; return value is how many were
// populated (1 for mono, 2 for stereo).
static int trackOutputChannels(const Track& t, int (&out)[2]) {
    if (t.outputMono) {
        out[0] = t.outputMonoChan + 1;
        return 1;
    }
    // 1-based channel labels (matches the Antelope CP), so shift by +1.
    out[0] = t.outputLeftChan  + 1;
    out[1] = t.outputRightChan + 1;
    return 2;
}

// Aggregate the desired mute state for a mixer channel across ALL
// tracks that route audio to it. Rule: if ANY track with a monitor
// button ON routes to this channel we UNMUTE it; only when every
// track routing to it has monitor OFF do we mute. Otherwise two
// tracks sharing a channel would fight â€” the later one's write would
// silently override the earlier one's, which was the fingerprint of
// the startup-sync bug.
static bool channelShouldBeMuted(const std::vector<Track>& tracks, int channelId1Based) {
    bool anyRoutedHere = false;
    for (const auto& t : tracks) {
        int chans[2];
        int nc = trackOutputChannels(t, chans);
        for (int i = 0; i < nc; i++) {
            if (chans[i] != channelId1Based) continue;
            anyRoutedHere = true;
            if (t.inputMonitor) return false;  // ANY unmuted wins
        }
    }
    // If no track routes to this channel we don't touch it. Caller
    // shouldn't call us in that case, but be safe: default to "muted"
    // (a no-op relative to whatever the interface has for that ch).
    return anyRoutedHere ? true : true;
}

void AudioEngine::setTrackInputMonitor(int trackIndex, bool on) {
    Track* t = getTrack(trackIndex);
    if (!t) return;
    t->inputMonitor = on;
    markSessionDirty();
    // Push to the Antelope mixer, re-aggregating across ALL tracks so
    // shared channels don't get stomped by whichever track we handled
    // last. Only touches the channels this track routes to â€” other
    // channels stay at whatever state the previous sync left them.
    if (m_antelope) {
        int chans[2];
        int nc = trackOutputChannels(*t, chans);
        for (int i = 0; i < nc; i++) {
            bool mute = channelShouldBeMuted(m_tracks, chans[i]);
            m_antelope->setChannelMute(chans[i], mute);
        }
    }
    // Refresh LED 9 immediately so the pad-side feedback matches state.
    syncInputMonitorLedNow();
}

void AudioEngine::syncAllInputMonitorsToAntelope() {
    if (!m_antelope) {
        dawLog("syncAllInputMonitorsToAntelope: skipped (Antelope client not initialised)");
        return;
    }
    // Build the unique set of channels that any track routes to, then
    // send ONE mute command per channel â€” aggregating monitor states
    // across all tracks routed to it. Otherwise two tracks sharing a
    // channel race and the second one's write always wins, which was
    // the startup-sync bug.
    std::set<int> uniqueChans;
    for (const auto& t : m_tracks) {
        int chans[2];
        int nc = trackOutputChannels(t, chans);
        for (int i = 0; i < nc; i++) uniqueChans.insert(chans[i]);
    }
    int nMuted = 0, nUnmuted = 0;
    for (int ch : uniqueChans) {
        bool mute = channelShouldBeMuted(m_tracks, ch);
        m_antelope->setChannelMute(ch, mute);
        if (mute) nMuted++; else nUnmuted++;
    }
    dawLog("syncAllInputMonitorsToAntelope: %zu tracks â†’ %d channels muted, %d unmuted (unique chans=%zu)",
           m_tracks.size(), nMuted, nUnmuted, uniqueChans.size());
}

// -------------- Channel-entry mode --------------

// Map a physical pad to a 0..9 digit for channel-number entry.
// Uses the same 3×3 letter-pad layout the firmware defines
// (pads 1,2,3 / 5,6,7 / 9,10,11 — pads 0/4/8 are per-row spacers with
// unrelated functions). Pad 15 (normally loop-edit) doubles as "0" while
// entry mode is active. Returns -1 for any pad not in this map.
int AudioEngine::numberPadDigit(int pad) {
    switch (pad) {
        case 1:  return 1;  case 2:  return 2;  case 3:  return 3;
        case 5:  return 4;  case 6:  return 5;  case 7:  return 6;
        case 9:  return 7;  case 10: return 8;  case 11: return 9;
        case 15: return 0;
        default: return -1;
    }
}

void AudioEngine::enterChannelEntry(ChannelEntryMode mode) {
    {
        std::lock_guard<std::mutex> lock(m_channelEntryMutex);
        m_channelEntryMode.store((int)mode);
        m_channelEntryLeftBuf.clear();
        m_channelEntryRightBuf.clear();
        m_channelEntryOnRight = false;
    }
    dawLog("channel entry: enter %s",
           mode == ChannelEntryMode::Input ? "INPUT" : "OUTPUT");
    // Kick off the same pad-20/21 LED flash the rename flow uses AND
    // put the firmware into DAW-exclusive input mode so its local
    // predictive LED changes and gesture-to-command translations
    // (LOOPPLAY, DELETEPAIR, RETURNONSTOP, mute/solo/arm LED prediction)
    // are suppressed for the whole gesture. The DAW is the sole
    // interpreter of every pad while entry mode is active.
    if (m_serialController) {
        m_serialController->sendMessage("EXCLUSIVE:1");
        m_serialController->sendMessage("PAIRFLASH:1");
    }
    refreshChannelEntryOled();
}

void AudioEngine::appendChannelDigit(int digit) {
    if (digit < 0 || digit > 9) return;
    {
        std::lock_guard<std::mutex> lock(m_channelEntryMutex);
        if ((int)m_channelEntryMode.load() == (int)ChannelEntryMode::None) return;
        std::string& buf = m_channelEntryOnRight ? m_channelEntryRightBuf
                                                 : m_channelEntryLeftBuf;
        if (buf.size() >= 2) return;   // cap at 2 decimal digits per channel
        buf.push_back(char('0' + digit));
    }
    refreshChannelEntryOled();
}

void AudioEngine::switchChannelEntrySide() {
    {
        std::lock_guard<std::mutex> lock(m_channelEntryMutex);
        if ((int)m_channelEntryMode.load() == (int)ChannelEntryMode::None) return;
        // Only meaningful once: pressing pad 20 again after already
        // being on the right does nothing.
        if (m_channelEntryOnRight) return;
        // Need SOMETHING typed for the left channel — otherwise there's
        // nothing to commit as a left value and the right would be
        // orphaned. Silently ignore stray pad-20 taps in that case.
        if (m_channelEntryLeftBuf.empty()) return;
        m_channelEntryOnRight = true;
    }
    refreshChannelEntryOled();
}

void AudioEngine::cancelChannelEntry() {
    {
        std::lock_guard<std::mutex> lock(m_channelEntryMutex);
        if ((int)m_channelEntryMode.load() == (int)ChannelEntryMode::None) return;
        dawLog("channel entry: cancelled");
        m_channelEntryMode.store((int)ChannelEntryMode::None);
        m_channelEntryLeftBuf.clear();
        m_channelEntryRightBuf.clear();
        m_channelEntryOnRight = false;
    }
    if (m_serialController) {
        m_serialController->sendMessage("PAIRFLASH:0");
        m_serialController->sendMessage("EXCLUSIVE:0");
    }
    pushPlaybackStateToOled();
}

void AudioEngine::commitChannelEntry() {
    ChannelEntryMode mode;
    std::string leftBuf, rightBuf;
    bool onRight;
    {
        std::lock_guard<std::mutex> lock(m_channelEntryMutex);
        mode = (ChannelEntryMode)m_channelEntryMode.load();
        if (mode == ChannelEntryMode::None) return;
        leftBuf  = m_channelEntryLeftBuf;
        rightBuf = m_channelEntryRightBuf;
        onRight  = m_channelEntryOnRight;
    }
    // Interpret buffers as decimal channel numbers.
    //   left="5"                  → mono ch 5
    //   left="10"                 → mono ch 10
    //   left="10", right="11"     → stereo ch 10 & 11
    //   left="5",  right="6"      → stereo ch 5 & 6
    // If the user pressed pad 20 (onRight true) but never typed a right
    // digit, treat it as mono using just the left value.
    auto parseChan = [](const std::string& s) -> int {
        if (s.empty()) return -1;
        try { return std::stoi(s); } catch (...) { return -1; }
    };
    int ch1 = parseChan(leftBuf);
    int ch2 = onRight ? parseChan(rightBuf) : -1;
    if (ch1 <= 0) {
        dawLog("channel entry: no left value, ignored");
    } else {
        int sel = getSelectedTrack();
        Track* t = getTrack(sel);
        if (t) {
            undoSnapshot();
            if (mode == ChannelEntryMode::Input) {
                if (ch2 <= 0) {
                    t->inputMono     = true;
                    t->inputMonoChan = ch1 - 1;
                    dawLog("channel entry: track %d input MONO ch=%d",
                           sel, ch1);
                } else {
                    t->inputMono      = false;
                    t->inputLeftChan  = ch1 - 1;
                    t->inputRightChan = ch2 - 1;
                    if (t->inputRightChan == t->inputLeftChan + 1 &&
                        (t->inputLeftChan % 2) == 0) {
                        t->inputPair = t->inputLeftChan / 2;
                    }
                    dawLog("channel entry: track %d input STEREO L=%d R=%d",
                           sel, ch1, ch2);
                }
            } else {   // Output
                if (ch2 <= 0) {
                    t->outputMono     = true;
                    t->outputMonoChan = ch1 - 1;
                    dawLog("channel entry: track %d output MONO ch=%d",
                           sel, ch1);
                } else {
                    t->outputMono       = false;
                    t->outputLeftChan   = ch1 - 1;
                    t->outputRightChan  = ch2 - 1;
                    // Legacy outputPair on-disk hint when the chans form
                    // an adjacent even/odd pair — kept so pre-L/R sessions
                    // continue to load without change.
                    if (t->outputRightChan == t->outputLeftChan + 1 &&
                        (t->outputLeftChan % 2) == 0) {
                        t->outputPair = t->outputLeftChan / 2;
                    }
                    dawLog("channel entry: track %d output STEREO L=%d R=%d",
                           sel, ch1, ch2);
                }
            }
            markSessionDirty();
        }
    }
    cancelChannelEntry();
}

bool AudioEngine::getChannelEntryBuffer(std::string& bufOut) const {
    if ((int)m_channelEntryMode.load() == (int)ChannelEntryMode::None) {
        bufOut.clear();
        return false;
    }
    std::lock_guard<std::mutex> lock(m_channelEntryMutex);
    // Layout examples:
    //   left="" onRight=false             → "_"        (nothing typed yet)
    //   left="1"                          → "1"
    //   left="10"                         → "10"
    //   left="10" onRight=true right=""   → "10 & _"   (just pressed pad 20)
    //   left="10" onRight=true right="11" → "10 & 11"
    std::string left  = m_channelEntryLeftBuf.empty()  ? "_" : m_channelEntryLeftBuf;
    if (!m_channelEntryOnRight) {
        bufOut = left;
    } else {
        std::string right = m_channelEntryRightBuf.empty() ? "_" : m_channelEntryRightBuf;
        bufOut = left + " & " + right;
    }
    return true;
}

float AudioEngine::getChannelEntryPulse() const {
    // Reuse the firmware-broadcast rename LED phase — same triangle
    // wave the physical pads 20/21 are running under PAIRFLASH — so
    // the DAW's red fill pulses in perfect sync with the hardware.
    return getLedFlashBrightness();
}

void AudioEngine::refreshChannelEntryOled() {
    if (!m_serialController) return;
    ChannelEntryMode mode = (ChannelEntryMode)m_channelEntryMode.load();
    std::string buf;
    if (!getChannelEntryBuffer(buf)) return;
    const char* line1 = (mode == ChannelEntryMode::Input)
                      ? "INPUT CHAN:" : "OUTPUT CHAN:";
    oledShowForce(line1, buf.c_str());
}

// -------------- Top-level touch dispatcher --------------

void AudioEngine::handleTouch(int pad, bool pressed) {
    // DELETEPAIR sentinels bypass diagnostic-mode gating.
    if (pressed && touchHandleDeletePairSentinel(pad)) return;
    // Diagnostic mode: controller is inert â€” silently drop everything.
    if (m_diagnosticMode.load()) return;

    // Marker-scroll mode: everything except pad 13 (press-to-exit),
    // pad 18 (release-to-exit) and pad 24 (pan modifier for E2) is
    // inert. No undo snapshots, no playback / mixer side-effects.
    if (m_bookmarkScrollMode.load()) {
        if (pad == 18 && !pressed) {
            m_bookmarkScrollMode.store(false);
            m_bookmarkNavIdx        = -1;
            m_bookmarkNavAccum      = 0;
            m_pad13Held.store(false);
            m_pad13UsedAsModifier.store(false);
            pushPlaybackStateToOled();
            return;
        }
        if (pad == 24) {
            // Pan modifier â€” mirror the normal handler so E2 can pan
            // while nav mode is on. Skips the LED-8 toggle since all
            // non-mute LEDs are dark in scroll mode.
            m_panModifierHeld.store(pressed);
            return;
        }
        if (pad != 13) return;
        // pad 13: fall through to the existing press/release handling
        // below (which knows how to exit scroll mode on press).
    }

    // Channel-entry mode: while active, the number pads type digits into
    // the current channel accumulator (multi-digit decimal), pad 20 moves
    // to the RIGHT channel, and pad 21 commits + exits. Everything else
    // — including pad 24 (formerly the commit), pad 12, and any other
    // pad — is swallowed so it can't fire its normal function or mess
    // with the accumulator. Undo isn't snapshotted per keystroke; the
    // commit does one snapshot for the whole gesture.
    if (m_channelEntryMode.load() != (int)ChannelEntryMode::None) {
        if (pressed) {
            if (pad == 21) { commitChannelEntry();      return; }
            if (pad == 20) { switchChannelEntrySide();  return; }
            int digit = numberPadDigit(pad);   // 0..9 or -1
            if (digit >= 0) { appendChannelDigit(digit); return; }
            // Any other pad while in entry mode is silently ignored so
            // it doesn't accidentally trigger its normal behaviour mid-
            // entry. The user cancels by pressing pad 21 with an empty
            // buffer, or just types garbage that fails commit validation.
        }
        return;   // swallow all other traffic including releases
    }

    // Combo gesture: pad 12 held + pad 0 press → enter INPUT channel
    // entry; + pad 4 → OUTPUT. Marks pad 12's long-press as fired so
    // its release does NOT trigger add-track.
    if (pressed && m_pad12Held.load() && (pad == 0 || pad == 4)) {
        m_pad12LongPressFired.store(true);
        enterChannelEntry(pad == 0 ? ChannelEntryMode::Input
                                   : ChannelEntryMode::Output);
        return;
    }

    // Undo snapshot: any pad-press that reaches this dispatcher can
    // mutate state, so we capture BEFORE the action runs. Excluded: the
    // pure modifier pads (26 = undo owner, 24 = pan modifier, 3/14 =
    // held-state trackers) â€” those get their own handling below and
    // don't need a snapshot on the modifier press itself. Also exclude
    // solo/mute/arm on release (pad 16/17/18) â€” for those we snapshot
    // on the press instead of the release so the sequence undoes cleanly.
    // Pad 36 (the big CAP1188 strip) is also excluded: it IS the undo
    // button, and snapshotting on its press would push a new entry onto
    // the stack that the very next undo would then have to pop.
    if (pressed &&
        pad != 26 && pad != 24 && pad != 3 && pad != 14 && pad != 36) {
        undoSnapshot();
    }

    // Pad 36 â€” big undo pad. TWO modes:
    //   • Held while user presses pad 20/21/22/23 → VIEW undo (rewinds
    //     the last playhead / marker / zoom / view-pan gesture, leaves
    //     tracks alone). Handled in the combo block below.
    //   • Tap-release with no combo → CONTENT undo (rewinds the last
    //     record / delete / mixer change, leaves the view alone).
    // Deferring content-undo to release lets the held-combo take
    // precedence without needing a "cancel" mechanism.
    if (pad == 36) {
        if (pressed) {
            m_pad36Held.store(true);
            m_pad36ComboFired.store(false);
        } else {
            m_pad36Held.store(false);
            if (!m_pad36ComboFired.exchange(false)) {
                undoPop();
                // Do NOT write the OLED here. undoPop() clears
                // m_startupOledPushed, which makes the next
                // updateController() tick re-push SETMODE:DESC +
                // the playback state â€” landing on top of anything
                // we write now. Queue it instead and let
                // updateController emit it AFTER that forced push.
                m_pendingUndoOled.store(true);
            }
        }
        return;
    }
    // Pad 36 held + pad 20/21/22/23 press → VIEW undo. Marks combo
    // fired so pad 36's own release doesn't ALSO fire a content undo.
    if (pressed && m_pad36Held.load() &&
        (pad == 20 || pad == 21 || pad == 22 || pad == 23)) {
        m_pad36ComboFired.store(true);
        undoPopView();
        m_pendingUndoOled.store(true);
        return;
    }

    // Simple modifier pads (both press and release update state):
    if (pad == 26) { touchHandlePad26(pressed); return; }
    if (pad == 24) { m_panModifierHeld.store(pressed); return; }
    if (pad == 3)  { m_pad3Held.store(pressed);        return; }
    if (pad == 14) { m_pad14Held.store(pressed);       return; }

    // Pad 35 â€” toggle input monitor on the selected track. Delegates to
    // setTrackInputMonitor so the Antelope mixer mute stays in sync,
    // LED 9 mirrors the state, and undoSnapshot at the top of handleTouch
    // already captured the pre-press state so undo works.
    if (pad == 35 && pressed) {
        int sel = getSelectedTrack();
        Track* t = getTrack(sel);
        if (t) {
            setTrackInputMonitor(sel, !t->inputMonitor);
        }
        return;
    }
    if (pad == 35) return;  // swallow release

    // (Pad 8 OSC-mute hotkey retired â€” the OSC / MUTEIND plumbing stays
    // wired so we can hook it up to a different pad later without another
    // refactor. toggleTotalMixStripMute + syncTotalMixMuteToHardware are
    // still called from the session-load path.)

    // Loop-edit toggle â€” press-only.
    if (pad == 15) {
        if (pressed) touchHandlePad15Press();
        return;
    }

    // Pad 3 + pad 23 = display-mode toggle (firmware-owned; DAW no-op).
    if (pressed && pad == 23 && m_pad3Held.load()) return;

    // Pad 12 handles both press and release (add-track, rename, delete).
    if (pad == 12) { touchHandlePad12(pressed); return; }

    // Pad 13 â€” press starts a "bookmark modifier" window. Release
    // decides between two behaviours based on whether the hold was
    // actually used as a modifier (e.g. pad13 + encoder1 to walk
    // through markers):
    //   * hold + encoder navigation â†’ don't create a marker on release
    //   * tap-release alone         â†’ append a new marker at playhead +
    //                                   open the text-input naming flow
    if (pad == 13 && pressed) {
        if (m_bookmarkScrollMode.load()) {
            // Sticky scroll-mode: this press ONLY exits the mode. No hold,
            // no marker on release. Modifier flag ensures the release
            // does nothing.
            m_bookmarkScrollMode.store(false);
            m_pad13Held.store(false);
            m_pad13UsedAsModifier.store(true);
            m_bookmarkNavIdx   = -1;
            m_bookmarkNavAccum = 0;
            pushPlaybackStateToOled();   // clear "MARKER:" line from OLED
            return;
        }
        m_pad13Held.store(true);
        m_pad13UsedAsModifier.store(false);
        return;
    }
    if (pad == 13 && !pressed) {
        bool wasModifier = m_pad13UsedAsModifier.exchange(false);
        m_pad13Held.store(false);
        // Only reset the nav state when we are NOT staying in scroll
        // mode â€” scroll mode keeps its selection between encoder events
        // after the physical pad has been released.
        if (!m_bookmarkScrollMode.load()) {
            m_bookmarkNavIdx   = -1;
            m_bookmarkNavAccum = 0;
        }
        if (wasModifier) return;   // nav-only or exit-press release, no marker
        size_t frame = (size_t)m_playbackPosition.load();
        int bmIdx = -1;
        {
            std::lock_guard<std::mutex> lock(m_bookmarksMutex);
            m_bookmarkFrames.push_back({frame, std::string()});
            bmIdx = (int)m_bookmarkFrames.size() - 1;
        }
        if (m_pendingNameTrackIndex.load() < 0) {
            m_pendingNameBookmarkIndex.store(bmIdx);
            // Prompt line on the OLED, same pattern as add-track. Line 2
            // will be replaced by the flashing cursor once TEXTIN: is
            // received on the firmware side.
            oledShowForce("MARKER ADDED,NAME:", " ");
            if (m_serialController) m_serialController->sendMessage("TEXTIN:");
        }
        markSessionDirty();
        return;
    }

    // Record-arm (16) / Solo (17) / Mute (18) toggle on tap release.
    // Firmware suppresses RELEASE:18 when its 3-second reset fired, so
    // a long-press-to-reset doesn't also toggle mute.
    if (!pressed && (pad == 16 || pad == 17 || pad == 18)) {
        int sel = getSelectedTrack();
        Track* t = getTrack(sel);
        if (!t) return;
        if      (pad == 17) t->solo  = !t->solo;
        else if (pad == 18) t->muted = !t->muted;
        else                t->armed = !t->armed;
        markSessionDirty();
        syncSoloMuteLedsNow();
        return;
    }

    if (!pressed) return;  // remainder is press-only

    // (14+19 / 15+19 return-on-stop combos are retired â€” firmware now
    // handles 19+14 / 19+15 end-to-end and sends RETURNONSTOP:1/0.)

    // Marker pads in clear-mode: toggle pair. In normal mode: pair-aware
    // create / restore / jump (touchHandlePairPad).
    if (m_clearMode.load()) {
        touchHandlePairInClearMode(pad);
        return;
    }
    if (pad == 20) { touchHandlePairPad(0); return; }
    if (pad == 21) { touchHandlePairPad(1); return; }
    if (pad == 22) { touchHandlePairPad(2); return; }
    if (pad == 23) { touchHandlePairPad(3); return; }

    // Track selection: pad 0 = previous, pad 4 = next, clamped.
    if (pad == 0 || pad == 4) {
        int n = getTrackCount();
        if (n <= 0) return;
        int cur = getSelectedTrack();
        if (cur < 0) cur = 0;
        int next = cur;
        if (pad == 0 && cur > 0)     next = cur - 1;
        if (pad == 4 && cur < n - 1) next = cur + 1;
        setSelectedTrack(next);
        syncSoloMuteLedsNow();   // reflect the newly-selected track's flags
        return;
    }

    // Normal play/stop.
    if (pad == 19) {
        m_scrubResumePending.store(false);
        if (isPlaying()) {
            stop();
            oledShow("STOPPED", " ");
        } else {
            play();
            oledShow(isPlaying() ? "PLAYING" : "NO AUDIO", " ");
        }
        syncPlayLedNow();
    }
}

// Apply mute changes observed on the Antelope mixer to the matching track
// monitor buttons. Runs on the main thread.
//
// The critical property is idempotence. Every mute WE send is broadcast
// back to us, so this runs for our own writes too. When the desired state
// already matches we must change nothing and â€” above all â€” send nothing
// back, otherwise the DAW and the mixer ping-pong forever. Hence the
// deliberate use of the raw t.inputMonitor field here rather than
// setTrackInputMonitor(), which would push straight back to the Antelope.
void AudioEngine::applyPendingAntelopeMutes() {
    std::vector<std::pair<int, bool>> pending;
    {
        std::lock_guard<std::mutex> lock(m_pendingAntelopeMuteMutex);
        if (m_pendingAntelopeMutes.empty()) return;
        pending.swap(m_pendingAntelopeMutes);
    }

    bool changedAny = false;
    for (const auto& pm : pending) {
        const int  channel = pm.first;
        const bool muted   = pm.second;
        // Mixer channel muted  -> monitoring off for tracks routed there.
        const bool wantMonitor = !muted;

        for (Track& t : m_tracks) {
            int chans[2];
            int nc = trackOutputChannels(t, chans);
            bool routedHere = false;
            for (int i = 0; i < nc; i++) {
                if (chans[i] == channel) { routedHere = true; break; }
            }
            if (!routedHere) continue;
            if (t.inputMonitor == wantMonitor) continue;   // echo of our own write

            t.inputMonitor = wantMonitor;
            changedAny = true;
            dawLog("Antelope -> DAW: ch=%d %s, track '%s' monitor %s",
                   channel, muted ? "MUTED" : "UNMUTED",
                   t.name.c_str(), wantMonitor ? "ON" : "OFF");
        }
    }

    if (changedAny) {
        markSessionDirty();
        syncInputMonitorLedNow();   // keep pad 35's LED in step
    }
}

void AudioEngine::updateController() {
    if (!m_serialController) return;

    // Fold in anything the Antelope mixer changed under us.
    applyPendingAntelopeMutes();

    // Periodic controller-state re-assert. Corrects any drift between
    // the DAW's LED shadow model and the firmware's actual pad LEDs
    // (predictive-LED mispredictions, missed serial writes, etc.).
    {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowMs - m_lastPeriodicResyncMs >= RESYNC_PERIOD_MS) {
            m_lastPeriodicResyncMs = nowMs;
            periodicControllerResync();
        }
    }

    // One-shot: on the first tick after DAW startup, force the controller
    // into descriptive mode (regardless of what it was doing before) and
    // seed the OLED with the current playback state ("STOPPED"). The DAW
    // is always the authority on session start.
    if (!m_startupOledPushed) {
        m_startupOledPushed = true;
        m_serialController->sendMessage("SETMODE:DESC");
        pushPlaybackStateToOled();
        syncSoloMuteLedsNow();   // paint the selected track's flags on 0/1
        syncInputMonitorLedNow();
    }

    // Pad-36 undo banner. Emitted HERE, after the block above, because
    // undoPop() deliberately clears m_startupOledPushed to force the OLED
    // back to the playback state â€” writing UNDO from the pad handler would
    // simply be overwritten by that push a moment later.
    if (m_pendingUndoOled.exchange(false)) {
        oledShow("UNDO", " ");
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_oledRevertAtMs.store(nowMs + 1000);
        dawLog("OLED: UNDO shown, revert scheduled +1000ms");
    }

    // Cheap tick: the DAW's own S/M buttons (or session load) can flip
    // solo/mute without going through pad 17/18, so poll for divergence
    // once per updateController tick and sync when it happens.
    syncSoloMuteLedsNow();
    syncInputMonitorLedNow();
    // Same story for the mute-LED marker-scroll flash â€” reflects the
    // AudioEngine-side m_bookmarkScrollMode state to the firmware.
    syncMuteFlashNow();

    // ---- Live record snapshot ----
    // ~5 Hz copy of every armed track's record buffer into that track's
    // audioData so the arrangement view can render the growing take.
    // The captured samples are placed at the frame position where play
    // started (m_playStartPosition) so the waveform appears under the
    // playhead's actual location, not at the beginning of the track.
    if (m_recordActive.load()) {
        int64_t nowSnapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        // 100 ms tick â€” cheap now that the pyramid update is incremental,
        // giving ~10 fps waveform-grow updates without slamming the GPU
        // texture rebuild (which alloc + uploads a 16 MB overview texture
        // whenever audioVersion bumps).
        if (nowSnapMs - m_lastRecordSnapshotMs >= 100) {
            m_lastRecordSnapshotMs = nowSnapMs;
            // Effective start = record gate's left marker if gated,
            // otherwise the free-run playhead start. slot->buffer[0]
            // corresponds to this absolute frame regardless of when the
            // playhead entered the gate.
            size_t gateStart_snap = m_recordGateStart.load();
            size_t gateEnd_snap   = m_recordGateEnd.load();
            bool gatedSnap = (gateEnd_snap > gateStart_snap);
            // Anchor is un-shifted — latency compensation happens on
            // the capture side (see m_recordLeadingSkipRemaining in
            // the audio callback).
            size_t recordStartFrame = gatedSnap ? gateStart_snap
                                                : m_playStartPosition.load();
            for (size_t i = 0; i < m_tracks.size() && i < m_recordSlots.size(); i++) {
                Track& t = m_tracks[i];
                if (!t.armed) continue;
                RecordSlot* slot = m_recordSlots[i].get();
                if (!slot) continue;

                // Read the CURRENT captured length (in frames) â€” the
                // callback has been push_back-ing into slot->buffer
                // since play(). Anything between prev and now is the
                // delta we need to overlay this tick.
                size_t availableFrames = 0;
                int    captureChans    = 0;
                {
                    std::lock_guard<std::mutex> lock(slot->mutex);
                    if (slot->buffer.empty() || slot->channels == 0) continue;
                    captureChans    = slot->channels;
                    availableFrames = slot->buffer.size() / (size_t)captureChans;
                }
                int trackChans = t.channels;
                if (trackChans == 0) trackChans = captureChans;

                size_t prevCaptureFrames = (t.freshTakeEnd >= t.freshTakeStart)
                                         ? (t.freshTakeEnd - t.freshTakeStart) : 0;
                if (availableFrames <= prevCaptureFrames) continue;
                size_t newFrames = availableFrames - prevCaptureFrames;

                // Grow audioData just enough to fit the new tail â€” the
                // existing samples before the fresh-take range and any
                // audio past it survive untouched.
                size_t neededSamples = (recordStartFrame + availableFrames)
                                     * (size_t)trackChans;
                {
                    std::lock_guard<std::mutex> aLock(m_recordAudioMutex);
                    if (t.audioData.size() < neededSamples) {
                        t.audioData.resize(neededSamples, 0.0f);
                    }
                    t.channels = trackChans;

                    // Delta write â€” only the freshly-captured samples.
                    // slot->buffer pointer is stable (pre-reserved 120 s
                    // in play()), so we can memcpy from it without
                    // holding slot->mutex; the callback only appends.
                    size_t writeFrame = recordStartFrame + prevCaptureFrames;
                    const float* src = slot->buffer.data()
                                     + prevCaptureFrames * (size_t)captureChans;
                    float*       dst = t.audioData.data()
                                     + writeFrame * (size_t)trackChans;
                    if (captureChans == trackChans) {
                        std::memcpy(dst, src,
                                    newFrames * (size_t)trackChans * sizeof(float));
                    } else if (captureChans == 1 && trackChans == 2) {
                        for (size_t f = 0; f < newFrames; f++) {
                            float s = src[f];
                            dst[f * 2 + 0] = s;
                            dst[f * 2 + 1] = s;
                        }
                    } else if (captureChans == 2 && trackChans == 1) {
                        for (size_t f = 0; f < newFrames; f++) {
                            dst[f] = 0.5f * (src[f * 2 + 0] + src[f * 2 + 1]);
                        }
                    } else {
                        int minC = std::min(captureChans, trackChans);
                        for (size_t f = 0; f < newFrames; f++) {
                            for (int c = 0; c < minC; c++) {
                                dst[f * trackChans + c] =
                                    src[f * captureChans + c];
                            }
                        }
                    }
                    t.freshTakeEnd = recordStartFrame + availableFrames;
                    // Incremental pyramid update over the delta only â€”
                    // O(newFrames / PEAK_BASE_BUCKET). Much cheaper than
                    // scanning the whole track.
                    t.rebuildPeakPyramidRange(writeFrame,
                                              writeFrame + newFrames);
                    t.audioVersion++;
                }
                dawLog("snapshot[%zu] +frames=%zu total=%zu trackChans=%d "
                       "captureChans=%d fresh=[%zu..%zu]",
                       i, newFrames, availableFrames, trackChans, captureChans,
                       t.freshTakeStart, t.freshTakeEnd);
            }
        }
    }

    // Heartbeat every ~1 s so the firmware can detect DAW disconnection and
    // block a switch to descriptive mode when nothing's listening.
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (nowMs - m_lastHeartbeatSendMs >= 1000) {
        m_serialController->sendMessage("HB");
        m_lastHeartbeatSendMs = nowMs;
    }

    // Process any pad-triggered track-add requests here on the main thread
    // so we never mutate m_tracks from the serial reader thread while the
    // audio callback is iterating it. Show "TRACK N ADDED - NAME:" on line
    // 1 and enter text-input mode (flashing cursor) on line 2 for the user
    // to type a name.
    if (m_pendingAddTrackRequest.exchange(false)) {
        int newIndex = addTrack("");
        int trackNum = getTrackCount();
        char line1[32];
        snprintf(line1, sizeof(line1), "TRACK %d ADDED,NAME:", trackNum);
        // Line 1 announce (line 2 will be replaced by the cursor immediately).
        oledShowForce(line1, " ");
        // Enter text-input mode on line 2 with an empty starting buffer.
        m_serialController->sendMessage("TEXTIN:");
        // Remember which track the incoming NAME:... belongs to.
        m_pendingNameTrackIndex.store(newIndex);
    }

    // --- Pad 12 long-press: detect & fire rename entry ---
    // Runs on main thread so we don't touch the controller from the reader.
    {
        int64_t pressedAt = m_pad12PressTimeMs.load();
        if (pressedAt != 0 && !m_pad12LongPressFired.load()) {
            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (nowMs - pressedAt >= RENAME_HOLD_MS) {
                m_pad12LongPressFired.store(true);
                m_pendingRenameRequest.store(true);
            }
        }
    }
    if (m_pendingDeleteTrackRequest.exchange(false)) {
        int idx = getSelectedTrack();
        int n = getTrackCount();
        if (idx >= 0 && idx < n) {
            deleteTrack(idx);
            // deleteTrack may have shifted the selection; clamp for OLED.
            int newSel = getSelectedTrack();
            if (newSel >= getTrackCount()) setSelectedTrack(getTrackCount() - 1);
        }
        // No OLED banner â€” deletion is visible in the DAW GUI.
    }
    if (m_pendingRenameRequest.exchange(false)) {
        int idx = getSelectedTrack();
        int n = getTrackCount();
        if (idx >= 0 && idx < n) {
            char line1[32];
            snprintf(line1, sizeof(line1), "TR%d RENAME,NAME:", idx + 1);
            oledShowForce(line1, " ");
            // Start with an empty buffer for now â€” cursor navigation of the
            // existing name will come later.
            m_serialController->sendMessage("TEXTIN:");
            m_pendingNameTrackIndex.store(idx);
        }
        // If no track was selected, silently ignore the rename request.
    }

    // Audio-scrub timeout: if E6 (pad 24 held) hasn't fired in a while, stop
    // scrubbing and snap the visual playhead to wherever the audio ended up.
    if (m_scrubbing.load()) {
        double nowSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowSec - m_lastAudioScrubMs.load() > SCRUB_TIMEOUT_S) {
            m_playbackPosition.store((size_t)m_scrubPlaybackPosition);
            m_scrubbing.store(false);
            m_scrubPlaybackRate.store(0.0f);
        }
    }

    // Timed OLED revert â€” currently used to auto-clear the mode-switch
    // banner after ~3 s so the screen doesn't get stuck on "DISPLAY MODE".
    int64_t revertAt = m_oledRevertAtMs.load();
    if (revertAt != 0) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowMs >= revertAt) {
            m_oledRevertAtMs.store(0);
            pushPlaybackStateToOled();
            dawLog("OLED: revert fired (%lld ms late)",
                   (long long)(nowMs - revertAt));
        }
    }

    // Scrub-then-resume: 100 ms after the last E1 movement, resume playback
    // from wherever the user parked the playhead.
    if (m_scrubResumePending.load()) {
        double nowSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowSec - m_lastScrubMoveTime.load() >= SCRUB_RESUME_DELAY_S) {
            m_scrubResumePending.store(false);
            play();
        }
    }

    // (Return-on-stop is now applied inside the audio callback, after the
    // fade-out completes â€” otherwise a mid-fade playhead teleport would
    // reintroduce the click the fade was there to prevent.)

    // Play LED (channel 3): on only during active playback. Firmware toggles
    // predictively on pad 19 press; this catches auto-stops (end of file) and
    // corrects any mismatch from play()-no-op cases.
    // NB: while a scrub-resume is pending we're "conceptually playing" â€”
    // playback was internally paused for the scrub and will resume 100 ms
    // after the encoder stops. Keep the LED on across the whole window so
    // it doesn't flicker off/on with every scrub.
    int wantPlay = (isPlaying() || m_scrubResumePending.load()) ? 1 : 0;
    if (wantPlay != m_lastPlayLedState) {
        m_serialController->sendMessage(wantPlay ? "LED:3:ON" : "LED:3:OFF");
        m_lastPlayLedState = wantPlay;
    }

    // Mode LEDs (channels 4/5/6/7 â€” loop-left, record-left, record-right,
    // loop-right). Normally the firmware toggle keeps the cache in sync via
    // handleTouch, so this only fires on startup (cache is -1) to force-sync
    // the physical LEDs to the DAW's boot state (all disabled).
    struct ModeLed { int ch; std::atomic<bool>* state; int* cache; };
    ModeLed modeLeds[] = {
        { 4, &m_loopLeftEnabled,    &m_lastLoopLeftLedState },
        { 5, &m_recordLeftEnabled,  &m_lastRecordLeftLedState },
        { 6, &m_recordRightEnabled, &m_lastRecordRightLedState },
        { 7, &m_loopRightEnabled,   &m_lastLoopRightLedState },
    };
    for (auto& m : modeLeds) {
        int want = m.state->load() ? 1 : 0;
        if (want != *m.cache) {
            char msg[16];
            snprintf(msg, sizeof(msg), "LED:%d:%s", m.ch, want ? "ON" : "OFF");
            m_serialController->sendMessage(msg);
            *m.cache = want;
        }
    }

    // Pan-modifier LED (channel 8) is fully firmware-owned â€” no host sync.

    // Push PAIRDEF whenever a pair's "defined" state changes. Defined ==
    // both markers have ever been placed (markerEverSet); goes back to
    // false via the pad 19 + marker-pad delete gesture (resetMarker).
    int loopDef = (markerEverSet(0) && markerEverSet(3)) ? 1 : 0;
    if (loopDef != m_lastLoopPairDefinedSent) {
        m_serialController->sendMessage(loopDef ? "PAIRDEF:LOOP:1" : "PAIRDEF:LOOP:0");
        m_lastLoopPairDefinedSent = loopDef;
    }
    int recDef = (markerEverSet(1) && markerEverSet(2)) ? 1 : 0;
    if (recDef != m_lastRecordPairDefinedSent) {
        m_serialController->sendMessage(recDef ? "PAIRDEF:REC:1" : "PAIRDEF:REC:0");
        m_lastRecordPairDefinedSent = recDef;
    }

    // (STATE:<byte> drift-correct retired â€” firmware no longer does
    // predictive LED toggles, so the DAW's cache-diff sends are the sole
    // authoritative LED path and can't diverge.)
}

// ==================== AUDIO CALLBACK ====================

// Breadcrumb â€” the callback bumps this on entry to each of its blocks.
// The SEH handler reads it after a crash so we know which region faulted.
static std::atomic<int> s_audioCallbackRegion{0};
static const char* audioCallbackRegionName(int r) {
    switch (r) {
        case 0:  return "idle";
        case 1:  return "record-capture";
        case 2:  return "input-monitor";
        case 3:  return "output-zero";
        case 4:  return "scrub";
        case 5:  return "playback";
        case 6:  return "test-tone";
        default: return "unknown";
    }
}

static int portAudioCallback(const void* inputBuffer, void* outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData) {
    AudioEngine* engine = static_cast<AudioEngine*>(userData);
#ifdef _MSC_VER
    // SEH wrap so a segfault inside the callback (bad audioData access
    // during a race, out-of-bounds index, etc.) gets logged with the
    // last-known state before the process dies.
    __try {
        return engine->audioCallback(inputBuffer, outputBuffer,
                                     framesPerBuffer, timeInfo, statusFlags);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        int r = s_audioCallbackRegion.load();
        dawLog("!! SEH CRASH in audioCallback code=0x%08lx frames=%lu "
               "lastRegion=%d(%s)",
               (unsigned long)GetExceptionCode(), framesPerBuffer,
               r, audioCallbackRegionName(r));
        dawLogFlush();
        // Emit silence and continue â€” better than tearing down the
        // PortAudio stream mid-buffer.
        if (outputBuffer) {
            float* out = static_cast<float*>(outputBuffer);
            std::memset(out, 0, framesPerBuffer * engine->getNumStereoPairs() * 2 * sizeof(float));
        }
        return 0;
    }
#else
    return engine->audioCallback(inputBuffer, outputBuffer,
                                 framesPerBuffer, timeInfo, statusFlags);
#endif
}

bool AudioEngine::startAudio(int deviceId) {
    std::cout << "Starting audio stream..." << std::endl;

#ifdef PORTAUDIO_FOUND
    PaStreamParameters outputParameters;

    if (deviceId >= 0) {
        int numDevices = Pa_GetDeviceCount();
        if (deviceId >= numDevices) {
            std::cerr << "Invalid device ID: " << deviceId << std::endl;
            return false;
        }
        outputParameters.device = deviceId;
    } else {
        outputParameters.device = Pa_GetDefaultOutputDevice();
    }

    if (outputParameters.device == paNoDevice) {
        std::cerr << "No output device found" << std::endl;
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(outputParameters.device);
    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    std::cout << "Device: " << deviceInfo->name << " (" << deviceInfo->maxOutputChannels << " channels)" << std::endl;
    std::cout << "Audio API: " << hostApiInfo->name << std::endl;

    m_currentDeviceId = outputParameters.device;
    m_deviceName = deviceInfo->name;
    m_hostApiName = hostApiInfo->name;
    m_maxOutputChannels = deviceInfo->maxOutputChannels;
    m_maxInputChannels  = deviceInfo->maxInputChannels;

    outputParameters.channelCount = deviceInfo->maxOutputChannels;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    double streamSampleRate = m_sampleRate;
    if (std::string(hostApiInfo->name).find("ASIO") != std::string::npos) {
        streamSampleRate = deviceInfo->defaultSampleRate;
    }

    // If the ASIO device advertises input channels, open a full-duplex
    // stream so the audio callback receives interleaved input samples too.
    // Recording just consumes what's already there â€” no extra thread, no
    // cross-clock issues since input and output share the ASIO device's
    // sample clock. If maxInputChannels == 0 we fall back to output-only.
    PaStreamParameters inputParameters;
    PaStreamParameters* inputParamsPtr = nullptr;
    if (deviceInfo->maxInputChannels > 0) {
        inputParameters.device                    = outputParameters.device;
        inputParameters.channelCount              = deviceInfo->maxInputChannels;
        inputParameters.sampleFormat              = paFloat32;
        inputParameters.suggestedLatency          = deviceInfo->defaultLowInputLatency;
        inputParameters.hostApiSpecificStreamInfo = nullptr;
        inputParamsPtr                            = &inputParameters;
    }

    // Drop paClipOff so PortAudio clamps out-of-range floats itself
    // (previously we handed the driver raw samples and let it decide â€”
    // some drivers wrap-around into nasty distortion instead of clipping
    // cleanly, which could sound harsher than the actual over-level).
    PaError err = Pa_OpenStream(
        reinterpret_cast<PaStream**>(&m_stream),
        inputParamsPtr, &outputParameters,
        streamSampleRate,
        paFramesPerBufferUnspecified,
        paNoFlag, portAudioCallback, this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    m_sampleRate = streamSampleRate;
    // Per-sample step for the play/stop fade envelope: unity gain reached
    // in PLAY_FADE_MS milliseconds.
    m_playFadeStep = 1.0f / ((PLAY_FADE_MS / 1000.0f) * (float)m_sampleRate);

    // Record latency compensation. PortAudio reports the driver's actual
    // input + output latency (in seconds); the sum is the round-trip
    // delay between a sample being played and its recording appearing in
    // the audio callback. Shifting recorded samples back by this amount
    // makes overdubs land in time with the reference — without this a
    // clap on the click track lands 30-60 ms late.
    {
        const PaStreamInfo* si = Pa_GetStreamInfo(static_cast<PaStream*>(m_stream));
        if (si) {
            double rt = si->inputLatency + si->outputLatency;
            size_t frames = (size_t)std::llround(rt * m_sampleRate);
            m_recordLatencyFrames.store(frames);
            std::cout << "Record latency compensation: "
                      << (rt * 1000.0) << " ms = " << frames << " frames"
                      << " (in=" << (si->inputLatency * 1000.0) << " ms"
                      << " out=" << (si->outputLatency * 1000.0) << " ms)"
                      << std::endl;
            dawLog("record latency: in=%.2f ms out=%.2f ms total=%.2f ms = %zu frames",
                   si->inputLatency * 1000.0, si->outputLatency * 1000.0,
                   rt * 1000.0, frames);
        }
    }

    err = Pa_StartStream(static_cast<PaStream*>(m_stream));
    if (err != paNoError) {
        std::cerr << "Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(static_cast<PaStream*>(m_stream));
        m_stream = nullptr;
        return false;
    }

    m_running.store(true);
    std::cout << "Audio stream started (" << m_sampleRate << " Hz)" << std::endl;
    return true;
#else
    (void)deviceId;
    m_running.store(true);
    return true;
#endif
}

void AudioEngine::stopAudio() {
    m_running.store(false);
#ifdef PORTAUDIO_FOUND
    if (m_stream) {
        Pa_StopStream(static_cast<PaStream*>(m_stream));
        Pa_CloseStream(static_cast<PaStream*>(m_stream));
        m_stream = nullptr;
    }
#endif
}

int AudioEngine::audioCallback(const void* inputBuffer, void* outputBuffer,
                               unsigned long framesPerBuffer,
                               const void* timeInfo,
                               unsigned long statusFlags) {
    float* out = static_cast<float*>(outputBuffer);
    const float* in = static_cast<const float*>(inputBuffer);   // may be null if no input

    // Playback position at the START of this buffer, captured before any
    // branch advances it. Used by the per-track meter pass at the end so
    // the meter reflects the samples actually rendered THIS callback.
    const size_t bufStartPlayPos = m_playbackPosition.load();

    // ---- Record capture ----
    // While m_recordActive, for each armed track with a valid input
    // channel/pair on this device, append input samples to the track's
    // record slot. Uses try_lock so the callback never blocks; the
    // finaliser holds the slot's mutex while draining and reloading, so
    // captures during that window are dropped (rare â€” only at stop).
    if (m_recordActive.load() && in != nullptr && m_maxInputChannels > 0) {
        s_audioCallbackRegion.store(1);   // record-capture
        // Frame-position gate. When the record pair is enabled, gateStart <
        // gateEnd and we only keep samples where the absolute playhead
        // position is inside [gateStart, gateEnd). Otherwise (gateEnd == 0)
        // the take is open-ended from m_playStartPosition and every sample
        // is kept â€” same as before this change.
        const size_t gateStart = m_recordGateStart.load();
        const size_t gateEnd   = m_recordGateEnd.load();
        const bool   gated     = (gateEnd > gateStart);
        const size_t basePos   = m_playbackPosition.load();

        // Latency-compensation head skip. The first N input samples
        // after record-start represent audio the mic captured BEFORE
        // the playhead reached the record start (round-trip delay is
        // in-flight). Dropping them makes slot->buffer[0] correspond
        // to the sample that was actually played at recordStartFrame,
        // so writes at audioData[recordStartFrame + i] land in sync
        // with the reference. Counter is armed in play() (or gate
        // entry, see below) and decremented per input frame here.
        size_t skipLeft = m_recordLeadingSkipRemaining.load();
        size_t skipHere = std::min<size_t>(skipLeft, framesPerBuffer);
        unsigned long startF = static_cast<unsigned long>(skipHere);
        if (skipHere > 0) {
            m_recordLeadingSkipRemaining.store(skipLeft - skipHere);
        }

        size_t nTracks = m_tracks.size();
        for (size_t ti = 0; ti < nTracks && ti < m_recordSlots.size(); ti++) {
            const Track& t = m_tracks[ti];
            if (!t.armed) continue;
            RecordSlot* slot = m_recordSlots[ti].get();
            if (!slot || slot->channels == 0) continue;
            std::unique_lock<std::mutex> lock(slot->mutex, std::try_to_lock);
            if (!lock.owns_lock()) continue;
            if (slot->channels == 1) {
                int ch = t.inputMonoChan;
                if (ch < 0 || ch >= m_maxInputChannels) continue;
                for (unsigned long f = startF; f < framesPerBuffer; f++) {
                    size_t pos = basePos + f;
                    if (gated && (pos < gateStart || pos >= gateEnd)) continue;
                    slot->buffer.push_back(in[f * m_maxInputChannels + ch]);
                }
            } else {
                // Independent L/R channel indices — supports non-adjacent
                // stereo like L=ch1 R=ch7. inputPair is legacy on-disk only.
                int chL = t.inputLeftChan;
                int chR = t.inputRightChan;
                if (chL < 0 || chL >= m_maxInputChannels ||
                    chR < 0 || chR >= m_maxInputChannels) continue;
                for (unsigned long f = startF; f < framesPerBuffer; f++) {
                    size_t pos = basePos + f;
                    if (gated && (pos < gateStart || pos >= gateEnd)) continue;
                    slot->buffer.push_back(in[f * m_maxInputChannels + chL]);
                    slot->buffer.push_back(in[f * m_maxInputChannels + chR]);
                }
            }
        }
    }

    int stereoPair = m_outputStereoPair.load();
    bool toneEnabled = m_testToneEnabled.load();
    bool playing = m_playing.load();
    bool recordingNow = m_recordActive.load();
    bool finalisingNow = m_finalising.load();
    // Anything that reads / writes an armed track's audioData off the
    // audio thread (snapshot tick, finaliseRecording) sets one of these
    // flags. The callback treats both the same way â€” it keeps armed
    // tracks silent so no torn read of audioData is possible.
    bool armedGuardActive = recordingNow || finalisingNow;
    // Punch region â€” when recording with the record pair enabled, we
    // only silence the armed track WITHIN [gateStart, gateEnd). Outside
    // that range the original audio still plays. Finalising always
    // fully silences (audioData is mid-swap on the main thread).
    size_t recGateStart = m_recordGateStart.load();
    size_t recGateEnd   = m_recordGateEnd.load();
    bool   recGated     = (recGateEnd > recGateStart);

    // ---- Input monitor ----
    // Historically the DAW itself performed input monitoring by mixing
    // each armed input straight to its track's output. That path is
    // retired: monitoring now happens on the Antelope interface (via
    // its own mixer), and the track's `inputMonitor` flag just controls
    // the corresponding mixer-channel mute on the Antelope through
    // setTrackInputMonitor(). We still zero the output buffer here so
    // subsequent playback paths can mix with += the way they did before.
    if (in != nullptr && m_maxInputChannels > 0) {
        s_audioCallbackRegion.store(2);   // (kept slot number for parity)
        for (int i = 0; i < (int)(framesPerBuffer * m_maxOutputChannels); i++) {
            out[i] = 0.0f;
        }
    }
    // Detect the play->stop transition and queue a return-to-start jump
    // to be executed AFTER the fade-out has finished (in the branch
    // below). Doing it here would teleport the playhead mid-fade and
    // reintroduce the click the fade was there to prevent.
    if (m_prevCBPlaying && !playing) m_returnJumpPending = true;
    m_prevCBPlaying = playing;

    int leftChan = stereoPair * 2;
    int rightChan = stereoPair * 2 + 1;

    const int totalSamples = framesPerBuffer * m_maxOutputChannels;
    // The input-monitor block above has already zeroed the buffer when it
    // ran; only clear here if it didn't (no input on this device or no
    // input pointer). Playback paths below sum into out with +=.
    if (in == nullptr || m_maxInputChannels <= 0) {
        s_audioCallbackRegion.store(3);   // output-zero
        for (int i = 0; i < totalSamples; i++) {
            out[i] = 0.0f;
        }
    }

    bool scrubbing = m_scrubbing.load();
    float scrubRate = m_scrubPlaybackRate.load();

    if (scrubbing && !m_tracks.empty() && std::abs(scrubRate) > 0.001f) {
        s_audioCallbackRegion.store(4);   // scrub
        size_t maxTotalFrames = 0;
        for (const auto& track : m_tracks) {
            size_t tf = track.getTotalFrames();
            if (tf > maxTotalFrames) maxTotalFrames = tf;
        }

        if (maxTotalFrames > 0) {
            bool anySolo = false;
            for (const auto& track : m_tracks) {
                if (track.solo) { anySolo = true; break; }
            }

            double pos = m_scrubPlaybackPosition;

            for (unsigned long i = 0; i < framesPerBuffer; i++) {
                if (pos < 0) pos = 0;
                if (pos >= maxTotalFrames - 1) pos = maxTotalFrames - 2;

                size_t pos0 = (size_t)pos;
                size_t pos1 = pos0 + 1;
                float frac = (float)(pos - pos0);

                if (scrubRate < 0 && pos0 > 0) {
                    pos1 = pos0;
                    pos0 = pos0 - 1;
                    frac = 1.0f - frac;
                }

                for (const auto& track : m_tracks) {
                    if (!track.hasAudio()) continue;
                    if (track.muted) continue;
                    if (anySolo && !track.solo) continue;
                    // Armed-track mute rules during a take:
                    //   â€¢ finalising: always skip (audioData is mid-swap)
                    //   â€¢ recording + punch region: silence only inside
                    //     [gateStart, gateEnd) â€” outside, original audio
                    //     plays as normal (scrubbing over the region too)
                    //   â€¢ recording without a region: silence for the
                    //     whole take (open-ended from playStart)
                    if (track.armed) {
                        if (finalisingNow) continue;
                        if (recordingNow) {
                            if (recGated) {
                                if (pos0 >= recGateStart && pos0 < recGateEnd) continue;
                            } else {
                                continue;
                            }
                        }
                    }

                    size_t trackFrames = track.getTotalFrames();
                    if (pos1 >= trackFrames) continue;

                    float left0 = track.getSample(pos0, 0);
                    float left1 = track.getSample(pos1, 0);
                    float leftSample = (left0 + frac * (left1 - left0)) * track.volume;

                    float rightSample;
                    if (track.channels > 1) {
                        float right0 = track.getSample(pos0, 1);
                        float right1 = track.getSample(pos1, 1);
                        rightSample = (right0 + frac * (right1 - right0)) * track.volume;
                    } else {
                        rightSample = leftSample;
                    }

                    float panLeft = (track.pan <= 0) ? 1.0f : (1.0f - track.pan);
                    float panRight = (track.pan >= 0) ? 1.0f : (1.0f + track.pan);
                    leftSample *= panLeft;
                    rightSample *= panRight;

                    if (track.outputMono) {
                        int c = track.outputMonoChan;
                        if (c >= 0 && c < m_maxOutputChannels)
                            out[i * m_maxOutputChannels + c] += (leftSample + rightSample) * 0.5f;
                    } else {
                        // Independent L/R channel indices — supports non-adjacent
                        // stereo output like L=ch1 R=ch7. outputPair is legacy on-disk only.
                        int trackLeftChan  = track.outputLeftChan;
                        int trackRightChan = track.outputRightChan;
                        if (trackLeftChan >= 0 && trackLeftChan < m_maxOutputChannels)
                            out[i * m_maxOutputChannels + trackLeftChan] += leftSample;
                        if (trackRightChan >= 0 && trackRightChan < m_maxOutputChannels)
                            out[i * m_maxOutputChannels + trackRightChan] += rightSample;
                    }
                }

                pos += scrubRate;
            }

            m_scrubPlaybackPosition = pos;
            // Keep the visible playhead in step with the audio scrub position.
            m_playbackPosition.store((size_t)pos);
            m_playbackPosition.store((size_t)pos);
        }
    }
    // Enter the playback branch while `playing` OR while the fade
    // envelope is still winding down â€” that's what actually renders the
    // stop-side fade-out. Playhead is only advanced while `playing`.
    // Runs even with no tracks / no audio so the playhead still moves
    // during recording (empty armed track) and in a fresh session.
    else if (playing || m_playFadeGain > 0.0f) {
        s_audioCallbackRegion.store(5);   // playback
        size_t playPos = m_playbackPosition.load();

        size_t maxTotalFrames = 0;
        for (const auto& track : m_tracks) {
            size_t tf = track.getTotalFrames();
            if (tf > maxTotalFrames) maxTotalFrames = tf;
        }

        // Only "no audio, not recording, not playing anything" collapses
        // out entirely. Recording (armed track being captured) extends the
        // effective length indefinitely.
        if (maxTotalFrames == 0 && !recordingNow) {
            // Still advance the playhead so a fresh session's playhead
            // moves visibly through the arrangement, but skip all the
            // per-sample mixing since there's nothing to play.
            if (playing) playPos += framesPerBuffer;
            // On the play â†’ stop transition the return-jump flag was set
            // upstream. Apply it here too â€” the mixing branch does this
            // inside its fade path but we skip that in the empty-session
            // case, so we'd otherwise lose the "return-on-stop" behaviour
            // entirely when no track has any audio.
            if (!playing && m_returnJumpPending) {
                if (m_returnToStartOnStop.load()) {
                    playPos = m_playStartPosition.load();
                }
                m_returnJumpPending = false;
            }
            m_playbackPosition.store(playPos);
            m_playFadeGain = playing ? 1.0f : 0.0f;
        } else {
            bool anySolo = false;
            for (const auto& track : m_tracks) {
                if (track.solo) { anySolo = true; break; }
            }

            // If the loop pair (markers 0 and 3) is armed, wrap the playhead
            // from loop-right back to loop-left every time it crosses the
            // end. Hoisted outside the inner loop so it's a single check
            // per buffer, not per sample.
            bool loopActive = m_loopPlaybackEnabled.load()
                              && m_loopLeftEnabled.load() && m_loopRightEnabled.load()
                              && isMarkerEnabled(0) && isMarkerEnabled(3);
            size_t loopStart = loopActive ? getMarkerPosition(0) : 0;
            size_t loopEnd   = loopActive ? getMarkerPosition(3) : 0;
            if (loopActive && loopStart >= loopEnd) loopActive = false;  // safety

            const float targetGain = playing ? 1.0f : 0.0f;

            for (unsigned long i = 0; i < framesPerBuffer; i++) {
                // Auto-stop when the playhead runs off the end of every
                // track's audio â€” unless a take is being captured, since
                // recording extends the effective length past what any
                // existing audio covers.
                if (playing && !recordingNow && playPos >= maxTotalFrames) {
                    m_playing.store(false);
                    playing = false;
                }

                // Step the fade envelope one sample toward its target.
                if (m_playFadeGain < targetGain) {
                    m_playFadeGain += m_playFadeStep;
                    if (m_playFadeGain > targetGain) m_playFadeGain = targetGain;
                } else if (m_playFadeGain > targetGain) {
                    m_playFadeGain -= m_playFadeStep;
                    if (m_playFadeGain < targetGain) m_playFadeGain = targetGain;
                }
                // Fully faded out and not playing â€” apply any pending
                // return-to-start jump NOW (safe: no more samples get
                // read this callback), then stop emitting.
                if (!playing && m_playFadeGain <= 0.0f) {
                    if (m_returnJumpPending && m_returnToStartOnStop.load()) {
                        m_playbackPosition.store(m_playStartPosition.load());
                        playPos = m_playStartPosition.load();
                    }
                    m_returnJumpPending = false;
                    break;
                }

                for (const auto& track : m_tracks) {
                    if (!track.hasAudio()) continue;
                    if (track.muted) continue;
                    if (anySolo && !track.solo) continue;
                    // Armed-track mute rules during a take (see the scrub
                    // branch above for the full breakdown).
                    if (track.armed) {
                        if (finalisingNow) continue;
                        if (recordingNow) {
                            if (recGated) {
                                if (playPos >= recGateStart && playPos < recGateEnd) continue;
                            } else {
                                continue;
                            }
                        }
                    }

                    size_t trackFrames = track.getTotalFrames();
                    if (playPos >= trackFrames) continue;

                    float leftSample = track.getSample(playPos, 0) * track.volume;
                    float rightSample = (track.channels > 1) ?
                        track.getSample(playPos, 1) * track.volume : leftSample;

                    float panLeft = (track.pan <= 0) ? 1.0f : (1.0f - track.pan);
                    float panRight = (track.pan >= 0) ? 1.0f : (1.0f + track.pan);
                    leftSample  *= panLeft  * m_playFadeGain;
                    rightSample *= panRight * m_playFadeGain;

                    if (track.outputMono) {
                        int c = track.outputMonoChan;
                        if (c >= 0 && c < m_maxOutputChannels)
                            out[i * m_maxOutputChannels + c] += (leftSample + rightSample) * 0.5f;
                    } else {
                        // Independent L/R channel indices — supports non-adjacent
                        // stereo output like L=ch1 R=ch7. outputPair is legacy on-disk only.
                        int trackLeftChan  = track.outputLeftChan;
                        int trackRightChan = track.outputRightChan;
                        if (trackLeftChan >= 0 && trackLeftChan < m_maxOutputChannels)
                            out[i * m_maxOutputChannels + trackLeftChan] += leftSample;
                        if (trackRightChan >= 0 && trackRightChan < m_maxOutputChannels)
                            out[i * m_maxOutputChannels + trackRightChan] += rightSample;
                    }
                }

                // Advance playhead whether we're actively playing OR just
                // fading out â€” audio continues underneath the fade
                // envelope so the transition at the callback boundary is
                // sample-continuous (no click). Playhead just runs a
                // little past the "stop" point during the ~5 ms fade.
                // Loop wrap only fires on the ENTRY into loopEnd from
                // below, not on every sample that happens to sit at or
                // past loopEnd. So starting playback exactly at the
                // right loop marker (e.g. after a jump-to-marker with
                // pad 23) plays forward without wrapping. Standard
                // playback still wraps when the playhead reaches
                // loopEnd from inside the loop range.
                size_t prevPos = playPos;
                playPos++;
                if (loopActive && playPos >= loopEnd && prevPos < loopEnd) {
                    playPos = loopStart;
                }
            }

            m_playbackPosition.store(playPos);
        }
    }
    else if (toneEnabled && !scrubbing) {
        s_audioCallbackRegion.store(6);   // test-tone
        const double phaseIncrement = 2.0 * M_PI * m_testToneFrequency / m_sampleRate;

        for (unsigned long i = 0; i < framesPerBuffer; i++) {
            float sample = static_cast<float>(std::sin(m_testTonePhase) * 0.2);

            if (leftChan < m_maxOutputChannels)
                out[i * m_maxOutputChannels + leftChan] = sample;
            if (rightChan < m_maxOutputChannels)
                out[i * m_maxOutputChannels + rightChan] = sample;

            m_testTonePhase += phaseIncrement;
            if (m_testTonePhase >= 2.0 * M_PI)
                m_testTonePhase -= 2.0 * M_PI;
        }
    }

    // Peak-hold: scan the buffer for the max |sample| and update the
    // atomic peak meter so the GUI can display a level / clip indicator.
    // Cheap even for large buffers â€” one pass over the interleaved output.
    {
        float peak = 0.0f;
        for (int i = 0; i < totalSamples; i++) {
            float a = std::fabs(out[i]);
            if (a > peak) peak = a;
        }
        // Overwrite (peak-hold decay handled on the GUI side).
        m_lastPeak.store(peak);
    }

    // Per-track meters: one entry per track, atomic so the GUI thread
    // can read without a lock. Meters whatever the track is set to hear:
    //   inputMonitor on â†’ the raw ASIO input for the track's input pair
    //                    / mono channel (unaffected by mute/solo â€” it's
    //                    what the Antelope is monitoring on that channel)
    //   inputMonitor off â†’ the track's playback samples at the current
    //                     playhead, gated by mute/solo like the mixer.
    // Both include the track's volume so the meter tracks audible level.
    {
        bool anySolo = false;
        for (const auto& track : m_tracks) {
            if (track.solo) { anySolo = true; break; }
        }
        bool playbackActive = playing || m_playFadeGain > 0.0f;

        for (size_t ti = 0; ti < m_tracks.size() && ti < m_trackMeters.size(); ti++) {
            const Track& t = m_tracks[ti];
            float peak = 0.0f;

            if (t.inputMonitor && in != nullptr && m_maxInputChannels > 0) {
                if (t.inputMono) {
                    int ch = t.inputMonoChan;
                    if (ch >= 0 && ch < m_maxInputChannels) {
                        for (unsigned long f = 0; f < framesPerBuffer; f++) {
                            float a = std::fabs(in[f * m_maxInputChannels + ch]);
                            if (a > peak) peak = a;
                        }
                    }
                } else {
                    int chL = t.inputLeftChan;
                    int chR = t.inputRightChan;
                    if (chL >= 0 && chL < m_maxInputChannels &&
                        chR >= 0 && chR < m_maxInputChannels) {
                        for (unsigned long f = 0; f < framesPerBuffer; f++) {
                            float aL = std::fabs(in[f * m_maxInputChannels + chL]);
                            float aR = std::fabs(in[f * m_maxInputChannels + chR]);
                            if (aL > peak) peak = aL;
                            if (aR > peak) peak = aR;
                        }
                    }
                }
            } else if (t.hasAudio() && playbackActive && !t.muted &&
                       !(anySolo && !t.solo)) {
                // Match the playback branch's mute/solo gating so a
                // silenced track meters silent â€” otherwise a soloed-out
                // track would still show its source signal level.
                size_t trackFrames = t.getTotalFrames();
                for (unsigned long f = 0; f < framesPerBuffer; f++) {
                    size_t p = bufStartPlayPos + f;
                    if (p >= trackFrames) break;
                    float aL = std::fabs(t.getSample(p, 0)) * t.volume;
                    if (aL > peak) peak = aL;
                    if (t.channels > 1) {
                        float aR = std::fabs(t.getSample(p, 1)) * t.volume;
                        if (aR > peak) peak = aR;
                    }
                }
            }
            m_trackMeters[ti]->store(peak);
        }
    }

    return 0;
}

// ==================== DEVICE ENUMERATION ====================

int AudioEngine::getMidiPortCount() const {
#ifdef RTMIDI_FOUND
    if (m_midiIn) {
        return static_cast<RtMidiIn*>(m_midiIn)->getPortCount();
    }
#endif
    return 0;
}

std::string AudioEngine::getMidiPortName(int portIndex) const {
#ifdef RTMIDI_FOUND
    if (m_midiIn) {
        RtMidiIn* midiIn = static_cast<RtMidiIn*>(m_midiIn);
        if (portIndex >= 0 && portIndex < (int)midiIn->getPortCount()) {
            return midiIn->getPortName(portIndex);
        }
    }
#endif
    return "";
}

bool AudioEngine::setMidiPort(int portIndex) {
#ifdef RTMIDI_FOUND
    if (!m_midiIn) return false;
    RtMidiIn* midiIn = static_cast<RtMidiIn*>(m_midiIn);
    try {
        if (m_currentMidiPort >= 0) midiIn->closePort();
        if (portIndex >= 0 && portIndex < (int)midiIn->getPortCount()) {
            midiIn->openPort(portIndex);
            m_currentMidiPort = portIndex;
            midiIn->ignoreTypes(false, false, false);
            return true;
        }
    } catch (RtMidiError& error) {
        std::cerr << "Error switching MIDI port: " << error.getMessage() << std::endl;
    }
#else
    (void)portIndex;
#endif
    return false;
}

int AudioEngine::getDeviceCount() const {
#ifdef PORTAUDIO_FOUND
    return Pa_GetDeviceCount();
#endif
    return 0;
}

std::string AudioEngine::getDeviceNameById(int deviceId) const {
#ifdef PORTAUDIO_FOUND
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (info) return info->name;
#else
    (void)deviceId;
#endif
    return "";
}

std::string AudioEngine::getDeviceHostApiById(int deviceId) const {
#ifdef PORTAUDIO_FOUND
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (info) {
        const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(info->hostApi);
        if (hostInfo) return hostInfo->name;
    }
#else
    (void)deviceId;
#endif
    return "";
}

bool AudioEngine::switchAudioDevice(int deviceId) {
    int previousDeviceId = m_currentDeviceId;
    stopAudio();
    bool result = startAudio(deviceId);
    if (!result && previousDeviceId >= 0) {
        startAudio(previousDeviceId);
    }
    return result;
}

int AudioEngine::getAsioDeviceCount() const {
#ifdef PORTAUDIO_FOUND
    int count = 0;
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(info->hostApi);
            if (hostInfo && std::string(hostInfo->name).find("ASIO") != std::string::npos) count++;
        }
    }
    return count;
#else
    return 0;
#endif
}

int AudioEngine::getAsioDeviceId(int asioIndex) const {
#ifdef PORTAUDIO_FOUND
    int count = 0;
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(info->hostApi);
            if (hostInfo && std::string(hostInfo->name).find("ASIO") != std::string::npos) {
                if (count == asioIndex) return i;
                count++;
            }
        }
    }
#else
    (void)asioIndex;
#endif
    return -1;
}

std::string AudioEngine::getAsioDeviceName(int asioIndex) const {
#ifdef PORTAUDIO_FOUND
    int deviceId = getAsioDeviceId(asioIndex);
    if (deviceId >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
        if (info) return info->name;
    }
#else
    (void)asioIndex;
#endif
    return "";
}

// ==================== TRACK MANAGEMENT ====================

int AudioEngine::addTrack(const std::string& name) {
    Track track;
    if (name.empty()) {
        m_trackCounter++;
        track.name = "Track " + std::to_string(m_trackCounter);
    } else {
        track.name = name;
    }
    track.outputPair = 0;

    m_tracks.push_back(track);
    int newIndex = static_cast<int>(m_tracks.size()) - 1;
    m_selectedTrack = newIndex;

    std::cout << "Added track: " << track.name << " (index " << newIndex << ")" << std::endl;
    dawLog("addTrack idx=%d name='%s' totalTracks=%zu",
           newIndex, track.name.c_str(), m_tracks.size());
    ensureRecordSlots();
    markSessionDirty();
    return newIndex;
}

void AudioEngine::deleteTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) return;

    std::cout << "Deleting track: " << m_tracks[trackIndex].name << std::endl;
    dawLog("deleteTrack idx=%d name='%s' remaining=%zu",
           trackIndex, m_tracks[trackIndex].name.c_str(), m_tracks.size() - 1);
    m_tracks.erase(m_tracks.begin() + trackIndex);
    if (trackIndex < (int)m_recordSlots.size()) {
        m_recordSlots.erase(m_recordSlots.begin() + trackIndex);
    }
    if (trackIndex < (int)m_trackMeters.size()) {
        m_trackMeters.erase(m_trackMeters.begin() + trackIndex);
    }

    if (m_tracks.empty()) {
        m_selectedTrack = -1;
    } else if (m_selectedTrack >= static_cast<int>(m_tracks.size())) {
        m_selectedTrack = static_cast<int>(m_tracks.size()) - 1;
    }
    markSessionDirty();
}

Track* AudioEngine::getTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) return nullptr;
    return &m_tracks[trackIndex];
}

const Track* AudioEngine::getTrack(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) return nullptr;
    return &m_tracks[trackIndex];
}

float AudioEngine::getTrackMeter(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_trackMeters.size())) return 0.0f;
    const auto& m = m_trackMeters[trackIndex];
    return m ? m->load() : 0.0f;
}

bool AudioEngine::loadTrackAudio(int trackIndex, const std::string& filepath) {
    Track* track = getTrack(trackIndex);
    if (!track) return false;

    bool result = track->loadAudio(filepath);
    if (result) {
        std::cout << "Loaded audio to track " << trackIndex << ": " << track->name
                  << " (" << track->getTotalFrames() << " frames, " << track->channels << " channels)" << std::endl;
        // A freshly-loaded file is all "old" audio â€” no punch-in region.
        track->freshTakeStart = 0;
        track->freshTakeEnd   = 0;
        markSessionDirty();
    } else {
        std::cerr << "Failed to load audio: " << filepath << std::endl;
    }
    return result;
}

void AudioEngine::clearTrackAudio(int trackIndex) {
    Track* track = getTrack(trackIndex);
    if (!track) return;
    if (!track->hasAudio()) return;
    std::cout << "Cleared audio from track " << trackIndex << ": " << track->name << std::endl;
    track->clearAudio();
    markSessionDirty();
}

