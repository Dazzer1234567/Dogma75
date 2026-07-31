#pragma once

#include <vector>
#include <string>
#include <cmath>

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

        // Extract filename for display
        size_t lastSlash = filepath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            name = filepath.substr(lastSlash + 1);
        } else {
            name = filepath;
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
