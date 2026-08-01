#include "audio_engine.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>

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

    return true;
}

void AudioEngine::shutdown() {
    if (m_running.load()) {
        stopAudio();
    }

    shutdownMidi();

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

    if (hasAudio) {
        m_scrubbing.store(false);
        m_scrubPlaybackRate.store(0.0f);
        // Snapshot the start position so a later stop can return here if the
        // return-on-stop flag is set. Includes stops from any source (button
        // press, end-of-file auto-stop, mod+play rejection, etc.).
        m_playStartPosition.store(m_playbackPosition.load());
        m_playing.store(true);
        std::cout << "Playback started" << std::endl;
    }
}

void AudioEngine::stop() {
    m_playing.store(false);
    m_scrubbing.store(false);
    m_scrubPlaybackRate.store(0.0f);
    std::cout << "Playback stopped" << std::endl;
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
            // (MIDI park-2 audio-scrub removed — audio scrubbing is now
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
    int idx = m_pendingNameTrackIndex.exchange(-1);
    if (idx < 0) return;
    Track* t = getTrack(idx);
    if (!t) return;
    if (!name.empty()) t->name = name;
    char line2[32];
    snprintf(line2, sizeof(line2), "TR%d - %s", idx + 1, t->name.c_str());
    oledShow("NAME SET", line2);
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
    // both display modes — used for live status readouts inside interactive
    // modes like clear-markers.
    std::string m1 = "DISPFORCE:1:"; m1 += line1;
    std::string m2 = "DISPFORCE:2:"; m2 += line2;
    m_serialController->sendMessage(m1);
    m_serialController->sendMessage(m2);
}

void AudioEngine::enableMarkerAtDefault(int markerIndex, double fractionFromLeft) {
    if (markerIndex < 0 || markerIndex > 3) return;

    if (m_markerEverSet[markerIndex]) {
        // Restore at preserved position — clearMarker() left it intact.
        setMarkerPosition(markerIndex, getMarkerPosition(markerIndex));
        return;
    }

    // First-time placement: position at a fraction of the current viewport.
    // Fall back to whole track if the GUI hasn't published a viewport yet.
    size_t total  = getTotalFrames();
    size_t start  = m_viewStartFrame.load();
    size_t frames = m_viewVisibleFrames.load();
    if (frames == 0 || start >= total) {
        start = 0;
        frames = total;
    }
    if (start + frames > total) frames = total - start;
    size_t pos = start + (size_t)(fractionFromLeft * (double)frames);
    if (pos > total) pos = total;
    setMarkerPosition(markerIndex, pos);
}

void AudioEngine::handleEncoderDelta(int encoder, long delta, float rpm, float velocityMultiplier) {
    // Diagnostic mode: controller is inert. Firmware still sends encoder
    // deltas so its OLED can show them, but the DAW must not act on them.
    if (m_diagnosticMode.load()) return;

    // Nudging any marker encoder (E3-E6) while clear-markers mode is on
    // is an implicit exit — the user has moved from pair-management to
    // marker-positioning. Firmware exits its own flashMode symmetrically.
    if (m_clearMode.load() && encoder >= 3 && encoder <= 6) {
        m_clearMode.store(false);
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_oledRevertAtMs.store(nowMs + 2000);
    }

    if (encoder == 1) {
        // E1: Playhead scrub. If we're playing, pause and arm the resume
        // timer so playback picks up 100 ms after the user stops moving.
        double nowSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_lastScrubMoveTime.store(nowSec);

        if (isPlaying()) {
            stop();
            m_scrubResumePending.store(true);
        }

        size_t totalFrames = getTotalFrames();
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
        }
    }
    else if (encoder == 2) {
        // E2: pan the timeline while pad 24 is held, otherwise zoom.
        if (m_panModifierHeld.load()) {
            size_t totalFrames = getTotalFrames();
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
            float currentZoom = getWaveformZoom();
            float newZoom = currentZoom * std::pow(1.0005f, (float)delta);
            setWaveformZoom(newZoom);
        }
    }
    else if (encoder == 3 && m_modifierHeld.load()) {
        // Modifier + E3: Scroll timeline view
        size_t totalFrames = getTotalFrames();
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
        // delta on every tick — no accumulator, no coast. When the user
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
        // E3-E6: adjust markers 0-3. A disabled/hidden marker is inert here —
        // markers can only be created via clear-mode restore (which places
        // them at the pair's default viewport fraction on first use).
        int markerIdx = encoder - 3;
        if (!isMarkerEnabled(markerIdx)) return;

        size_t totalFrames = getTotalFrames();
        if (totalFrames == 0) return;

        float zoom = getWaveformZoom();
        size_t visibleFrames = (size_t)(totalFrames / zoom);
        if (visibleFrames < 100) visibleFrames = 100;
        // Marker step is 1/9600 of the visible range per encoder tick —
        // half the previous 1/4800 rate — so marker placement is finer.
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

void AudioEngine::handleTouch(int pad, bool pressed) {
    // Sentinel pads from SerialController for DELETEPAIR combos —
    // 200 = wipe loop pair (markers 0, 3), 201 = wipe record pair (1, 2).
    // Runs even in diagnostic mode: the gesture arrived via its own serial
    // message and is a deliberate user action.
    if (pressed && (pad == 200 || pad == 201)) {
        if (pad == 200) {
            resetMarker(0); resetMarker(3);
            m_loopLeftEnabled.store(false);
            m_loopRightEnabled.store(false);
            oledShow("LOOP", "DELETED");
        } else {
            resetMarker(1); resetMarker(2);
            m_recordLeftEnabled.store(false);
            m_recordRightEnabled.store(false);
            oledShow("REC LOOP", "DELETED");
        }
        return;
    }
    // Diagnostic mode: controller is inert. Firmware keeps sending events so
    // its OLED can show button numbers, but the DAW must not react — no
    // play/stop, no marker jumps, no state changes at all.
    if (m_diagnosticMode.load()) return;

    // Runs on the SerialController reader thread. Firmware toggles LEDs 3-8
    // predictively for instant response; the DAW only reasserts state via
    // updateController when its own state diverges from what firmware assumed
    // (e.g. play() no-op with no audio loaded, or audio-callback auto-stop).

    // Pad 26 (modifier). A press while already in clear-markers mode exits
    // the mode instead of setting the modifier bit (matches firmware exit).
    if (pad == 26) {
        if (pressed && m_clearMode.load()) {
            m_clearMode.store(false);
            // Firmware locally writes "EXIT" and hard-locks the screen for
            // 2 s on its side. Schedule the playback-state push for right
            // after that lock expires.
            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            m_oledRevertAtMs.store(nowMs + 2000);
        } else {
            m_modifierHeld.store(pressed);
            // Pad 26 is silent when tapped alone — it only ever acts as a
            // modifier for other keys, so it should never overwrite the
            // OLED with its own status. Combos (26 + 19 = clear-markers,
            // etc.) push their own display when they fire.
        }
        return;
    }

    // Pad 24 (pan modifier). Held while turning E2 = pan instead of zoom.
    if (pad == 24) {
        m_panModifierHeld.store(pressed);
        oledShow("PAN MOD", pressed ? "HOLD" : "OFF");
        return;
    }

    // Pad 3 (display-mode modifier). Tracked here so we can consume the
    // pad 23 press that follows and not flip the loop-right toggle.
    if (pad == 3) {
        m_pad3Held.store(pressed);
        return;
    }

    // Pad 14 — held while pressing pad 19 to set the on-stop return-to-
    // start flag ON. Absolute set command, not a toggle.
    if (pad == 14) { m_pad14Held.store(pressed); return; }
    // Pad 15 — single tap toggles loop-edit / clear-markers mode. Firmware
    // mirrors the same toggle locally on press so LEDs update instantly.
    if (pad == 15) {
        if (pressed) {
            bool nowOn = !m_clearMode.load();
            m_clearMode.store(nowOn);
            if (!nowOn) {
                // Exiting: schedule the OLED playback-state revert so the
                // firmware's local "EXIT" banner is replaced cleanly.
                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                m_oledRevertAtMs.store(nowMs + 2000);
            } else {
                // Entering: push the live status readout the firmware
                // shows in clear mode (LOOP: ON/OFF, REC: ON/OFF).
                oledShowForce(m_loopLeftEnabled.load()   ? "LOOP: ON" : "LOOP: OFF",
                              m_recordLeftEnabled.load() ? "REC: ON"  : "REC: OFF");
            }
        }
        return;
    }

    if (pressed && pad == 23 && m_pad3Held.load()) {
        // Display-mode toggle — firmware writes the new mode's header to
        // line 1 with no lock. Diagnostic keeps it forever; descriptive
        // gets it replaced by the DAW's next push on the user's next real
        // interaction. Nothing to schedule DAW-side.
        return;
    }

    // Pad 12 (add track): fire on RELEASE, not press. Reader-thread request;
    // the actual m_tracks mutation happens on the main thread in
    // updateController() to avoid racing with the audio callback.
    // Also rate-limited to one add per ADD_TRACK_MIN_INTERVAL_MS so light
    // grazes that ripple through the MPR121 as many touch/release cycles
    // don't spawn a burst of tracks.
    if (pad == 12) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (pressed) {
            // Modifier (pad 26) + pad 12 = delete the selected track.
            // Queue the delete for the main thread; mark long-press as
            // already-fired so the release doesn't also create a new track.
            if (m_modifierHeld.load()) {
                m_pendingDeleteTrackRequest.store(true);
                m_pad12LongPressFired.store(true);
                m_pad12PressTimeMs.store(0);
                m_modifierHeld.store(false);   // consume the modifier
                return;
            }
            m_pad12PressTimeMs.store(nowMs);
            m_pad12LongPressFired.store(false);
        } else {
            m_pad12PressTimeMs.store(0);
            // Release after a long-press: rename was already fired; do NOT
            // also create a new track.
            if (m_pad12LongPressFired.exchange(false)) return;
            // Short press: queue an add-track (with the usual debounce).
            int64_t lastMs = m_lastAddTrackMs.load();
            if (nowMs - lastMs >= ADD_TRACK_MIN_INTERVAL_MS) {
                m_lastAddTrackMs.store(nowMs);
                m_pendingAddTrackRequest.store(true);
            }
        }
        return;
    }

    if (!pressed) return;  // rest is press-only

    // Combo: pad 14 + play => turn on "return playhead to start on stop".
    // The old 15+19 (turn OFF) is retired since pad 15 alone now toggles
    // loop-edit mode; use 14+19 or the settings panel to switch off.
    if ((pad == 19) && m_pad14Held.load()) {
        bool on = true;
        m_returnToStartOnStop.store(on);
        // Short two-line status (both display modes).
        oledShow("ON STOP", on ? "RETURN" : "STAY");
        // Verbose 3-second explanation for descriptive mode. Firmware
        // silently ignores DISPHOLD in diagnostic mode.
        oledShowHold("ON STOP:", on ? "RETURN TO START" : "STAY IN PLACE");
        return;
    }

    // ---- Clear/restore-markers mode: pad presses toggle the pair. ----
    // If a pair is currently enabled, clear it (disable markers + toggles →
    // hidden from waveform). If a pair is currently disabled, restore it
    // (re-enable markers at their preserved positions + toggles on).
    if (m_clearMode.load()) {
        if (pad == 20 || pad == 23) {
            bool wasOn = m_loopLeftEnabled.load();
            if (wasOn) {
                clearMarker(0);
                clearMarker(3);
                m_loopLeftEnabled.store(false);
                m_loopRightEnabled.store(false);
                m_lastLoopLeftLedState  = 0;
                m_lastLoopRightLedState = 0;
            } else {
                enableMarkerAtDefault(0, 0.15);
                enableMarkerAtDefault(3, 0.85);
                m_loopLeftEnabled.store(true);
                m_loopRightEnabled.store(true);
                m_lastLoopLeftLedState  = 1;
                m_lastLoopRightLedState = 1;
            }
        } else if (pad == 21 || pad == 22) {
            bool wasOn = m_recordLeftEnabled.load();
            if (wasOn) {
                clearMarker(1);
                clearMarker(2);
                m_recordLeftEnabled.store(false);
                m_recordRightEnabled.store(false);
                m_lastRecordLeftLedState  = 0;
                m_lastRecordRightLedState = 0;
            } else {
                // First-time record-pair placement: anchor to the loop
                // pair if it's already enabled — 33% and 66% between the
                // loop-left and loop-right markers. Fall back to a
                // viewport-fraction placement otherwise. Re-enable
                // (previously cleared) restores preserved positions via
                // enableMarkerAtDefault's normal branch.
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
                m_lastRecordLeftLedState  = 1;
                m_lastRecordRightLedState = 1;
            }
        }
        // Refresh the live clear-mode status display after any toggle.
        oledShowForce(m_loopLeftEnabled.load()   ? "LOOP: ON" : "LOOP: OFF",
                      m_recordLeftEnabled.load() ? "REC: ON"  : "REC: OFF");
        return;
    }

    // (26+19 clear-markers combo retired — pad 15 alone toggles the mode.)
    // (pad 12 add-track handled earlier, on RELEASE)

    // ---- Pads 20-23: jump-to-marker triggers ----
    // When a marker is enabled, pressing its pad moves the playhead to that
    // marker. Playback continues seamlessly if it was already playing (the
    // audio callback just reads from the new position). If the marker isn't
    // enabled, the press does nothing — use clear-markers mode to turn a
    // marker on. LEDs 4/5/6/7 are driven only by marker enabled state, so
    // they don't flicker on jump presses.
    auto jumpToMarker = [&](int markerIdx, const char* label) {
        if (!isMarkerEnabled(markerIdx)) {
            oledShow("NO MARKER", "SET");
            return;
        }
        setPlaybackPosition(getMarkerPosition(markerIdx));
        oledShow("JUMP TO", label);
    };
    if (pad == 20) { jumpToMarker(0, "LOOP LEFT");  return; }
    if (pad == 21) { jumpToMarker(1, "REC LEFT");   return; }
    if (pad == 22) { jumpToMarker(2, "REC RIGHT");  return; }
    if (pad == 23) { jumpToMarker(3, "LOOP RIGHT"); return; }

    // ---- Track selection: pad 0 = previous, pad 4 = next. Clamped at
    // both ends — no wrap-around, so pad 4 on the last track and pad 0 on
    // the first are silent no-ops. OLED still updates so the user sees
    // the current selection when they press the button.
    if (pad == 0 || pad == 4) {
        int n = getTrackCount();
        if (n <= 0) return;
        int cur = getSelectedTrack();
        if (cur < 0) cur = 0;
        int next = cur;
        if (pad == 0 && cur > 0)     next = cur - 1;
        if (pad == 4 && cur < n - 1) next = cur + 1;
        setSelectedTrack(next);
        Track* t = getTrack(next);
        char line2[32];
        if (t) snprintf(line2, sizeof(line2), "TR%d - %s", next + 1, t->name.c_str());
        else   snprintf(line2, sizeof(line2), " ");
        oledShow(pad == 0 ? "TRACK UP" : "TRACK DN", line2);
        return;
    }

    // ---- Normal play/stop (pad 19). ----
    if (pad == 19) {
        m_scrubResumePending.store(false);
        m_lastPlayLedState = 1 - m_lastPlayLedState;
        if (isPlaying()) {
            stop();
            oledShow("STOPPED", " ");
        } else {
            play();
            oledShow(isPlaying() ? "PLAYING" : "NO AUDIO", " ");
        }
    }
}

void AudioEngine::updateController() {
    if (!m_serialController) return;

    // One-shot: on the first tick after DAW startup, force the controller
    // into descriptive mode (regardless of what it was doing before) and
    // seed the OLED with the current playback state ("STOPPED"). The DAW
    // is always the authority on session start.
    if (!m_startupOledPushed) {
        m_startupOledPushed = true;
        m_serialController->sendMessage("SETMODE:DESC");
        pushPlaybackStateToOled();
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
            Track* t = getTrack(idx);
            char line2[32];
            snprintf(line2, sizeof(line2), "TR%d - %s", idx + 1,
                     t ? t->name.c_str() : " ");
            deleteTrack(idx);
            // deleteTrack may have shifted the selection; clamp for OLED.
            int newSel = getSelectedTrack();
            if (newSel >= getTrackCount()) setSelectedTrack(getTrackCount() - 1);
            oledShow("DELETED", line2);
        } else {
            oledShow("DELETE", "NO TRACK");
        }
    }
    if (m_pendingRenameRequest.exchange(false)) {
        int idx = getSelectedTrack();
        int n = getTrackCount();
        if (idx >= 0 && idx < n) {
            char line1[32];
            snprintf(line1, sizeof(line1), "TR%d RENAME,NAME:", idx + 1);
            oledShowForce(line1, " ");
            // Start with an empty buffer for now — cursor navigation of the
            // existing name will come later.
            m_serialController->sendMessage("TEXTIN:");
            m_pendingNameTrackIndex.store(idx);
        } else {
            oledShow("RENAME", "NO TRACK");
        }
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

    // Timed OLED revert — currently used to auto-clear the mode-switch
    // banner after ~3 s so the screen doesn't get stuck on "DISPLAY MODE".
    int64_t revertAt = m_oledRevertAtMs.load();
    if (revertAt != 0) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowMs >= revertAt) {
            m_oledRevertAtMs.store(0);
            pushPlaybackStateToOled();
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

    // Return-on-stop: detect a play->stop transition and, if the flag is set,
    // yank the playhead back to where playback last started. Catches stops
    // from every path (button, end-of-file, etc.) because we sample the
    // authoritative isPlaying() state.
    bool nowPlaying = isPlaying();
    if (m_wasPlayingLastTick && !nowPlaying && m_returnToStartOnStop.load()) {
        setPlaybackPosition(m_playStartPosition.load());
    }
    m_wasPlayingLastTick = nowPlaying;

    // Play LED (channel 3): on only during active playback. Firmware toggles
    // predictively on pad 19 press; this catches auto-stops (end of file) and
    // corrects any mismatch from play()-no-op cases.
    int wantPlay = isPlaying() ? 1 : 0;
    if (wantPlay != m_lastPlayLedState) {
        m_serialController->sendMessage(wantPlay ? "LED:3:ON" : "LED:3:OFF");
        m_lastPlayLedState = wantPlay;
    }

    // Mode LEDs (channels 4/5/6/7 — loop-left, record-left, record-right,
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

    // Pan-modifier LED (channel 8) is fully firmware-owned — no host sync.

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
}

// ==================== AUDIO CALLBACK ====================

static int portAudioCallback(const void* inputBuffer, void* outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData) {
    AudioEngine* engine = static_cast<AudioEngine*>(userData);
    return engine->audioCallback(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
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

    outputParameters.channelCount = deviceInfo->maxOutputChannels;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    double streamSampleRate = m_sampleRate;
    if (std::string(hostApiInfo->name).find("ASIO") != std::string::npos) {
        streamSampleRate = deviceInfo->defaultSampleRate;
    }

    PaError err = Pa_OpenStream(
        reinterpret_cast<PaStream**>(&m_stream),
        nullptr, &outputParameters,
        streamSampleRate,
        paFramesPerBufferUnspecified,
        paClipOff, portAudioCallback, this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    m_sampleRate = streamSampleRate;

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

    int stereoPair = m_outputStereoPair.load();
    bool toneEnabled = m_testToneEnabled.load();
    bool playing = m_playing.load();

    int leftChan = stereoPair * 2;
    int rightChan = stereoPair * 2 + 1;

    const int totalSamples = framesPerBuffer * m_maxOutputChannels;
    for (int i = 0; i < totalSamples; i++) {
        out[i] = 0.0f;
    }

    bool scrubbing = m_scrubbing.load();
    float scrubRate = m_scrubPlaybackRate.load();

    if (scrubbing && !m_tracks.empty() && std::abs(scrubRate) > 0.001f) {
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

                    int trackLeftChan = track.outputPair * 2;
                    int trackRightChan = track.outputPair * 2 + 1;

                    if (trackLeftChan < m_maxOutputChannels)
                        out[i * m_maxOutputChannels + trackLeftChan] += leftSample;
                    if (trackRightChan < m_maxOutputChannels)
                        out[i * m_maxOutputChannels + trackRightChan] += rightSample;
                }

                pos += scrubRate;
            }

            m_scrubPlaybackPosition = pos;
            // Keep the visible playhead in step with the audio scrub position.
            m_playbackPosition.store((size_t)pos);
            m_playbackPosition.store((size_t)pos);
        }
    }
    else if (playing && !m_tracks.empty()) {
        size_t playPos = m_playbackPosition.load();

        size_t maxTotalFrames = 0;
        for (const auto& track : m_tracks) {
            size_t tf = track.getTotalFrames();
            if (tf > maxTotalFrames) maxTotalFrames = tf;
        }

        if (maxTotalFrames == 0) {
            m_playing.store(false);
        } else {
            bool anySolo = false;
            for (const auto& track : m_tracks) {
                if (track.solo) { anySolo = true; break; }
            }

            // If the loop pair (markers 0 and 3) is armed, wrap the playhead
            // from loop-right back to loop-left every time it crosses the
            // end. Hoisted outside the inner loop so it's a single check
            // per buffer, not per sample.
            bool loopActive = m_loopLeftEnabled.load() && m_loopRightEnabled.load()
                              && isMarkerEnabled(0) && isMarkerEnabled(3);
            size_t loopStart = loopActive ? getMarkerPosition(0) : 0;
            size_t loopEnd   = loopActive ? getMarkerPosition(3) : 0;
            if (loopActive && loopStart >= loopEnd) loopActive = false;  // safety

            for (unsigned long i = 0; i < framesPerBuffer; i++) {
                if (playPos >= maxTotalFrames) {
                    m_playing.store(false);
                    break;
                }

                for (const auto& track : m_tracks) {
                    if (!track.hasAudio()) continue;
                    if (track.muted) continue;
                    if (anySolo && !track.solo) continue;

                    size_t trackFrames = track.getTotalFrames();
                    if (playPos >= trackFrames) continue;

                    float leftSample = track.getSample(playPos, 0) * track.volume;
                    float rightSample = (track.channels > 1) ?
                        track.getSample(playPos, 1) * track.volume : leftSample;

                    float panLeft = (track.pan <= 0) ? 1.0f : (1.0f - track.pan);
                    float panRight = (track.pan >= 0) ? 1.0f : (1.0f + track.pan);
                    leftSample *= panLeft;
                    rightSample *= panRight;

                    int trackLeftChan = track.outputPair * 2;
                    int trackRightChan = track.outputPair * 2 + 1;

                    if (trackLeftChan < m_maxOutputChannels)
                        out[i * m_maxOutputChannels + trackLeftChan] += leftSample;
                    if (trackRightChan < m_maxOutputChannels)
                        out[i * m_maxOutputChannels + trackRightChan] += rightSample;
                }

                playPos++;
                if (loopActive && playPos >= loopEnd) {
                    playPos = loopStart;
                }
            }

            m_playbackPosition.store(playPos);
        }
    }
    else if (toneEnabled && !scrubbing) {
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
    return newIndex;
}

void AudioEngine::deleteTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) return;

    std::cout << "Deleting track: " << m_tracks[trackIndex].name << std::endl;
    m_tracks.erase(m_tracks.begin() + trackIndex);

    if (m_tracks.empty()) {
        m_selectedTrack = -1;
    } else if (m_selectedTrack >= static_cast<int>(m_tracks.size())) {
        m_selectedTrack = static_cast<int>(m_tracks.size()) - 1;
    }
}

Track* AudioEngine::getTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) return nullptr;
    return &m_tracks[trackIndex];
}

const Track* AudioEngine::getTrack(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) return nullptr;
    return &m_tracks[trackIndex];
}

bool AudioEngine::loadTrackAudio(int trackIndex, const std::string& filepath) {
    Track* track = getTrack(trackIndex);
    if (!track) return false;

    bool result = track->loadAudio(filepath);
    if (result) {
        std::cout << "Loaded audio to track " << trackIndex << ": " << track->name
                  << " (" << track->getTotalFrames() << " frames, " << track->channels << " channels)" << std::endl;
    } else {
        std::cerr << "Failed to load audio: " << filepath << std::endl;
    }
    return result;
}
