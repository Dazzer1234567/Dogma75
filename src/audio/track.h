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
    int outputPair = 0;     // Which stereo pair to output to
    bool armed = false;     // For future recording
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
};
