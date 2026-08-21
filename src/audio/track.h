#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>

#ifdef LIBSNDFILE_FOUND
#include <sndfile.h>
#endif

struct Track {
    std::string name;
    std::string filePath;
    std::vector<float> audioData;  // Interleaved samples
    int channels = 0;
    float volume = 1.0f;
    float pan = 0.0f;       // -1.0 = left, 0.0 = center, 1.0 = right
    bool muted = false;
    bool solo = false;
    int outputPair = 0;     // Legacy adjacent-pair index. Kept for session-load compat;
                            // runtime uses outputLeftChan/outputRightChan even for adjacent pairs.
    int outputMonoChan = 0; // Mono output channel index (0 = channel 1, 1 = channel 2, ...)
    bool outputMono = false;// false = play into outputLeftChan/outputRightChan, true = outputMonoChan
    // Stereo output channel indices (0-based). Independent so a track can
    // play to ANY two hardware output channels — e.g. L=ch1 R=ch7. Defaults
    // to (0,1) which is the same as outputPair=0 for legacy behaviour.
    int outputLeftChan  = 0;
    int outputRightChan = 1;
    int inputPair = 0;      // Legacy adjacent-pair index. Kept for session-load compat only;
                            // runtime uses inputLeftChan/inputRightChan even for the adjacent case.
    int inputMonoChan = 0;  // Mono channel index (0 = channel 1, 1 = channel 2, ...)
    bool inputMono = false; // false = record from inputLeftChan/inputRightChan, true = from inputMonoChan
    // Stereo input channel indices (0-based). Independent so we can record
    // a stereo track from ANY two hardware channels — e.g. left = ch1, right = ch7.
    // Defaults chosen so a fresh Track behaves like inputPair=0 (channels 1-2).
    int inputLeftChan  = 0;
    int inputRightChan = 1;
    bool armed = false;     // Record-arm — armed tracks capture input during playback
    bool inputMonitor = false; // "I" button — reserved for input-monitor wiring

    // ---- Per-track routing (patchbay) ----
    // Each track owns its own routing canvas. Nodes are hardware Input
    // or Output pills the user has dragged in. inputCables lists which
    // Input node feeds each stem — its ORDER defines the stem index
    // (and hence the channel layout of audioData / the recorded WAV).
    // outputCables specify per-stem hardware routing at playback: each
    // entry says "stem s goes to Output node o" (one hw channel).
    // Multiple output cables from the same stem duplicate the signal;
    // multiple stems cabled to the same output sum together.
    struct RoutingNode {
        enum class Kind { Input, Output };
        Kind  kind = Kind::Input;
        int   channelOrTrack = 0;    // hardware channel index (0-based)
        float posX = 0.0f, posY = 0.0f;
    };
    struct OutputCable {
        int stemIdx        = 0;      // 0..(inputCables.size()-1)
        int outputNodeIdx  = -1;     // index into routingNodes[]
    };
    std::vector<RoutingNode>  routingNodes;
    std::vector<int>          routingInputCables;   // per stem → routingNodes[] index of an Input node
    std::vector<OutputCable>  routingOutputCables;
    float routingTrackNodeX  = 0.0f;
    float routingTrackNodeY  = 0.0f;
    // ---- Output mode ----
    // 0 = MULTI (per-stem output cables, uses routingOutputCables)
    // 1 = STEREO (all stems summed to L/R via a stereo mixer)
    // 2 = MONO (all stems summed to a mono mixer)
    // 3 = OFF (routing graph ignored — track uses legacy I/O combos)
    // Each mode keeps its own cable list so switching preserves state.
    int  routingOutputMode = 0;
    std::vector<int> routingMonoOutputs;    // indices into routingNodes[] (Output kind)
    // Per-stem gain in dB applied inside the mono mixer. Range −60 to
    // +12 dB, default 0. Missing entries are treated as 0 dB (unity).
    std::vector<float> routingMonoGainsDb;
    // STEREO mode: independent gain/pan per stem + separate L / R
    // output cable lists.
    std::vector<float> routingStereoGainsDb;
    std::vector<float> routingStereoPans;      // -1..+1, 0 = centre
    std::vector<int>   routingStereoOutputsL;
    std::vector<int>   routingStereoOutputsR;
    float routingMixerX = 0.0f;
    float routingMixerY = 0.0f;
    float routingMixerW = 170.0f;  // resizable width for MONO/STEREO mixer
    // Convenience: a track is "routed" (owned by patchbay) as soon as
    // it has ANY cable, in either direction.
    bool hasRouting() const {
        return !routingInputCables.empty() || !routingOutputCables.empty();
    }
    int stemCount() const { return (int)routingInputCables.size(); }

    // Frame range covered by the most recent recording take. The renderer
    // paints this range in a distinct "fresh take" colour so the user can
    // see punch-in edits at a glance. Zero-length means no fresh take.
    // Not persisted — a session reload starts everything as "old" audio.
    size_t freshTakeStart = 0;
    size_t freshTakeEnd   = 0;
    int color = 0;          // Track color index (for multi-track display)

    // ---- Waveform peak pyramid (like Ableton/Reaper/Cubase .peak files) ----
    // Precomputed min/max summaries at multiple resolutions so the renderer
    // can pick a level near "1 bucket per screen pixel" and read a fixed
    // number of stable values instead of re-mining raw samples every frame.
    // Solves both the flicker-on-zoom problem (buckets are aligned to sample
    // 0 and never move) and the per-frame CPU cost of large-audio drawing.
    struct PeakBucket { float minVal, maxVal; };
    struct PeakLevel  {
        size_t framesPerBucket;
        std::vector<PeakBucket> buckets;
    };
    std::vector<PeakLevel> peakPyramid;
    static constexpr size_t PEAK_BASE_BUCKET = 64;

    // GPU-side pre-rendered waveform. The GUI uploads a wide (~8k) RGBA
    // texture containing the whole track's envelope; drawing then becomes
    // a single stretched textured quad. GPU linear filtering handles all
    // sub-pixel positioning under zoom/pan — eliminates the per-pixel
    // rasterisation flicker of the software renderer. Stored here as a
    // raw GL name so the header stays GL-agnostic; the texture is
    // created/destroyed by GUIManager.
    unsigned int waveformTex        = 0;
    int          waveformTexW       = 0;
    int          waveformTexH       = 0;
    // Bumped every time loadAudio() replaces the samples so the GUI knows
    // to rebuild the texture even if the raw pointer stays the same.
    int          waveformTexVersion = 0;   // last version the GPU tex was built for
    int          audioVersion       = 0;   // incremented in loadAudio()

    // High-detail dynamic texture. Rebuilt on demand to cover the current
    // view window (plus margin) whenever the overview's texels start
    // exceeding one screen pixel — i.e. deep zoom. Also invalidated on
    // audio reload via waveformDetailVersion.
    unsigned int waveformDetailTex     = 0;
    int          waveformDetailW       = 0;
    int          waveformDetailH       = 0;
    size_t       waveformDetailStart   = 0;   // first frame the tex covers (aligned to FPB)
    size_t       waveformDetailEnd     = 0;   // one-past-last frame
    size_t       waveformDetailFPB     = 0;   // samples per texel, stable across pans
    int          waveformDetailVersion = 0;

    // Per-track frame count
    size_t getTotalFrames() const {
        if (audioData.empty() || channels == 0) return 0;
        return audioData.size() / channels;
    }

    // Safe sample access (returns 0 if out of bounds)
    float getSample(size_t frame, int channel) const {
        if (channels == 0 || frame >= getTotalFrames() || channel >= channels) return 0.0f;
        return audioData[frame * channels + channel];
    }

    // Get mixed mono sample at frame (averages all channels)
    float getMixedSample(size_t frame) const {
        if (channels == 0 || frame >= getTotalFrames()) return 0.0f;
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ch++) {
            sum += audioData[frame * channels + ch];
        }
        return sum / channels;
    }

    // Get min/max values for waveform drawing over a frame range
    struct PeakData { float minVal; float maxVal; };
    PeakData getWaveformPeak(size_t frameStart, size_t frameEnd) const {
        PeakData peak = { 0.0f, 0.0f };
        size_t total = getTotalFrames();
        if (frameStart >= total) return peak;
        if (frameEnd > total) frameEnd = total;
        for (size_t frame = frameStart; frame < frameEnd; frame++) {
            float sample = getMixedSample(frame);
            if (sample < peak.minVal) peak.minVal = sample;
            if (sample > peak.maxVal) peak.maxVal = sample;
        }
        return peak;
    }

    // Incremental variant of buildPeakPyramid: assumes the pyramid is
    // already valid for the audioData EXCEPT possibly in the frame range
    // [startFrame, endFrame) (which may include the tail if audioData
    // grew). Recomputes only the base buckets that overlap that range
    // then propagates the change up the levels. O(rangeFrames /
    // PEAK_BASE_BUCKET) rather than O(totalFrames), so the recording
    // snapshot tick doesn't stall the main thread.
    void rebuildPeakPyramidRange(size_t startFrame, size_t endFrame) {
        size_t total = getTotalFrames();
        if (endFrame > total) endFrame = total;
        if (startFrame >= endFrame) return;
        if (peakPyramid.empty()) {
            buildPeakPyramid();
            return;
        }

        // ---- Level 0: extend + recompute affected buckets. ----
        PeakLevel& L0 = peakPyramid[0];
        size_t n0Needed = (total + PEAK_BASE_BUCKET - 1) / PEAK_BASE_BUCKET;
        if (L0.buckets.size() < n0Needed) L0.buckets.resize(n0Needed);

        size_t bStart = startFrame / PEAK_BASE_BUCKET;
        size_t bEnd   = (endFrame + PEAK_BASE_BUCKET - 1) / PEAK_BASE_BUCKET;
        if (bEnd > L0.buckets.size()) bEnd = L0.buckets.size();
        for (size_t b = bStart; b < bEnd; b++) {
            size_t s = b * PEAK_BASE_BUCKET;
            size_t e = std::min(s + PEAK_BASE_BUCKET, total);
            float mn = 0.0f, mx = 0.0f;
            for (size_t f = s; f < e; f++) {
                float v = getMixedSample(f);
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            L0.buckets[b] = { mn, mx };
        }

        // ---- Propagate up through existing levels. ----
        size_t affStart = bStart;
        size_t affEnd   = bEnd;
        for (size_t lvl = 1; lvl < peakPyramid.size(); lvl++) {
            PeakLevel& up   = peakPyramid[lvl];
            PeakLevel& low  = peakPyramid[lvl - 1];
            size_t upNeeded = (low.buckets.size() + 1) / 2;
            if (up.buckets.size() < upNeeded) up.buckets.resize(upNeeded);
            size_t upStart = affStart / 2;
            size_t upEnd   = (affEnd + 1) / 2;
            if (upEnd > up.buckets.size()) upEnd = up.buckets.size();
            for (size_t b = upStart; b < upEnd; b++) {
                size_t li = b * 2;
                PeakBucket a = low.buckets[li];
                PeakBucket n = (li + 1 < low.buckets.size()) ? low.buckets[li + 1] : a;
                up.buckets[b] = {
                    std::min(a.minVal, n.minVal),
                    std::max(a.maxVal, n.maxVal)
                };
            }
            affStart = upStart;
            affEnd   = upEnd;
        }

        // ---- Grow new levels if the top exceeds 4 buckets. ----
        while (peakPyramid.back().buckets.size() > 4) {
            const PeakLevel& src = peakPyramid.back();
            PeakLevel next;
            next.framesPerBucket = src.framesPerBucket * 2;
            next.buckets.reserve((src.buckets.size() + 1) / 2);
            for (size_t i = 0; i < src.buckets.size(); i += 2) {
                PeakBucket a = src.buckets[i];
                PeakBucket b = (i + 1 < src.buckets.size()) ? src.buckets[i + 1] : a;
                next.buckets.push_back({
                    std::min(a.minVal, b.minVal),
                    std::max(a.maxVal, b.maxVal)
                });
            }
            peakPyramid.push_back(std::move(next));
        }
    }

    // Build the peak pyramid from the current audioData. Cheap linear pass +
    // log2(N) coalescing passes. Call once after loadAudio(); zero cost per
    // frame after that.
    void buildPeakPyramid() {
        peakPyramid.clear();
        size_t total = getTotalFrames();
        if (total == 0) return;

        // Level 0 — one bucket per PEAK_BASE_BUCKET raw samples.
        PeakLevel L0;
        L0.framesPerBucket = PEAK_BASE_BUCKET;
        size_t n0 = (total + PEAK_BASE_BUCKET - 1) / PEAK_BASE_BUCKET;
        L0.buckets.resize(n0);
        for (size_t b = 0; b < n0; b++) {
            size_t s = b * PEAK_BASE_BUCKET;
            size_t e = std::min(s + PEAK_BASE_BUCKET, total);
            float mn = 0.0f, mx = 0.0f;
            for (size_t f = s; f < e; f++) {
                float v = getMixedSample(f);
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
            L0.buckets[b] = { mn, mx };
        }
        peakPyramid.push_back(std::move(L0));

        // Higher levels — pair-coalesce (min-of-mins, max-of-maxes) until a
        // level fits in ~4 buckets. That's enough to cover any zoom-out.
        while (peakPyramid.back().buckets.size() > 4) {
            size_t idx = peakPyramid.size() - 1;
            size_t prevSize = peakPyramid[idx].buckets.size();
            PeakLevel next;
            next.framesPerBucket = peakPyramid[idx].framesPerBucket * 2;
            next.buckets.reserve((prevSize + 1) / 2);
            for (size_t i = 0; i < prevSize; i += 2) {
                PeakBucket a = peakPyramid[idx].buckets[i];
                PeakBucket b = (i + 1 < prevSize) ? peakPyramid[idx].buckets[i + 1] : a;
                next.buckets.push_back({
                    std::min(a.minVal, b.minVal),
                    std::max(a.maxVal, b.maxVal)
                });
            }
            peakPyramid.push_back(std::move(next));
        }
    }

    // Fill out[numPixels] with min/max pairs covering [startFrame, endFrame).
    // Picks the pyramid level closest to "one bucket per pixel" so results
    // stay stable as the user zooms or pans (bucket boundaries never move —
    // only pixel-to-bucket mapping shifts by at most one bucket per pixel).
    void getPeaks(size_t startFrame, size_t endFrame,
                  PeakBucket* out, int numPixels) const {
        if (numPixels <= 0) return;
        if (peakPyramid.empty() || endFrame <= startFrame) {
            for (int i = 0; i < numPixels; i++) out[i] = { 0.0f, 0.0f };
            return;
        }
        size_t total = getTotalFrames();
        if (endFrame > total) endFrame = total;
        if (startFrame >= endFrame) {
            for (int i = 0; i < numPixels; i++) out[i] = { 0.0f, 0.0f };
            return;
        }

        uint64_t rangeFrames  = (uint64_t)(endFrame - startFrame);
        size_t framesPerPixel = (size_t)(rangeFrames / (uint64_t)numPixels);
        if (framesPerPixel < 1) framesPerPixel = 1;

        // Largest pyramid level whose bucket still fits inside one pixel.
        int level = 0;
        for (int L = 0; L < (int)peakPyramid.size(); L++) {
            if (peakPyramid[L].framesPerBucket <= framesPerPixel) level = L;
            else break;
        }
        const PeakLevel& lvl = peakPyramid[level];
        const size_t fpb = lvl.framesPerBucket;
        const size_t nb  = lvl.buckets.size();

        for (int p = 0; p < numPixels; p++) {
            size_t pStart = startFrame + (size_t)((uint64_t)p       * rangeFrames / (uint64_t)numPixels);
            size_t pEnd   = startFrame + (size_t)((uint64_t)(p + 1) * rangeFrames / (uint64_t)numPixels);
            size_t bStart = pStart / fpb;
            size_t bEnd   = (pEnd  + fpb - 1) / fpb;   // ceil
            if (bStart >= nb) { out[p] = { 0.0f, 0.0f }; continue; }
            if (bEnd > nb) bEnd = nb;
            if (bEnd <= bStart) bEnd = bStart + 1;
            float mn = lvl.buckets[bStart].minVal;
            float mx = lvl.buckets[bStart].maxVal;
            for (size_t b = bStart + 1; b < bEnd; b++) {
                if (lvl.buckets[b].minVal < mn) mn = lvl.buckets[b].minVal;
                if (lvl.buckets[b].maxVal > mx) mx = lvl.buckets[b].maxVal;
            }
            out[p] = { mn, mx };
        }
    }

    // Get RMS value over a frame range (for simplified waveform)
    float getRMS(size_t frameStart, size_t frameEnd) const {
        size_t total = getTotalFrames();
        if (frameStart >= total) return 0.0f;
        if (frameEnd > total) frameEnd = total;
        float sumSquares = 0.0f;
        size_t count = 0;
        for (size_t frame = frameStart; frame < frameEnd; frame++) {
            float sample = getMixedSample(frame);
            sumSquares += sample * sample;
            count++;
        }
        return (count > 0) ? std::sqrt(sumSquares / count) : 0.0f;
    }

    // Load audio from file
    bool loadAudio(const std::string& filepath) {
#ifdef LIBSNDFILE_FOUND
        SF_INFO sfInfo;
        memset(&sfInfo, 0, sizeof(sfInfo));

        SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sfInfo);
        if (!file) {
            return false;
        }

        audioData.resize(sfInfo.frames * sfInfo.channels);
        sf_count_t framesRead = sf_readf_float(file, audioData.data(), sfInfo.frames);
        sf_close(file);

        channels = sfInfo.channels;
        filePath = filepath;

        // Note: track.name is intentionally left untouched — a load must never
        // clobber a user-chosen name. Only if the track had no name yet do we
        // fall back to the filename so something meaningful shows in the UI.
        if (name.empty()) {
            size_t lastSlash = filepath.find_last_of("/\\");
            name = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1)
                                                    : filepath;
        }

        if (framesRead > 0) {
            buildPeakPyramid();
            audioVersion++;   // GUI checks this to rebuild the GPU texture
        }
        return (framesRead > 0);
#else
        (void)filepath;
        return false;
#endif
    }

    bool hasAudio() const {
        return !audioData.empty() && channels > 0;
    }

    // Drop the audio samples and everything derived from them. Keeps the
    // track's name, colour, and mixer state (vol/pan/mute/solo/pair) so the
    // channel strip is preserved and can be re-loaded with new material.
    // Bumps audioVersion so the GUI releases its GPU waveform texture.
    void clearAudio() {
        audioData.clear();
        audioData.shrink_to_fit();
        channels = 0;
        filePath.clear();
        peakPyramid.clear();
        audioVersion++;
    }
};
