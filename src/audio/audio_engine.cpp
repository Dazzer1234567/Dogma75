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
    , m_scrubSpeed(0.05f)
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
            else if (m_selectedPark == 2) {
                size_t totalFrames = getTotalFrames();
                if (totalFrames > 0) {
                    if (ccNumber == 16 && direction != 0) {
                        float currentRate = m_scrubPlaybackRate.load();
                        float newRate = currentRate + (direction * 0.05f);
                        if (newRate > 6.0f) newRate = 6.0f;
                        if (newRate < -6.0f) newRate = -6.0f;
                        m_currentEncoderRpm = std::abs(newRate) * 100.0f;
                        m_scrubPlaybackRate.store(newRate);
                        m_scrubbing.store(true);
                        m_scrubPlaybackPosition = (double)getPlaybackPosition();
                    }
                }
            }
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

void AudioEngine::handleEncoderDelta(int encoder, long delta, float rpm, float velocityMultiplier) {
    if (encoder == 1) {
        // E1: Playhead scrub
        if (isPlaying()) {
            stop();
            if (m_serialController) m_serialController->sendMessage("LED:0:OFF");
            std::cout << "E1 scrub: stopped playback" << std::endl;
        }

        size_t totalFrames = getTotalFrames();
        if (totalFrames > 0) {
            size_t currentPos = getPlaybackPosition();
            float zoom = getWaveformZoom();
            size_t visibleFrames = (size_t)(totalFrames / zoom);
            if (visibleFrames < 100) visibleFrames = 100;

            VelocityCurve& curve = m_serialController->getVelocityCurve();
            double framesPerPulse = (double)visibleFrames / 2400.0 * curve.baseMultiplier * velocityMultiplier;
            long frameDelta = (long)(delta * framesPerPulse);

            long newPlayPos = (long)currentPos + frameDelta;
            if (newPlayPos < 0) newPlayPos = 0;
            if (newPlayPos > (long)totalFrames) newPlayPos = totalFrames;

            setPlaybackPosition((size_t)newPlayPos);
            printf("ENC: delta=%ld pos=%ld\n", delta, newPlayPos);
        }
    }
    else if (encoder == 2) {
        // E2: Zoom
        float currentZoom = getWaveformZoom();
        float newZoom = currentZoom * std::pow(1.0005f, (float)delta);
        setWaveformZoom(newZoom);
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
    else if (encoder >= 3 && encoder <= 6) {
        // E3-E6: Markers 0-3
        int markerIdx = encoder - 3;
        size_t totalFrames = getTotalFrames();
        if (totalFrames > 0) {
            float zoom = getWaveformZoom();
            size_t visibleFrames = (size_t)(totalFrames / zoom);
            if (visibleFrames < 100) visibleFrames = 100;
            size_t stepSize = (size_t)((double)visibleFrames / 2400.0 * std::abs(delta));
            if (stepSize < 1) stepSize = 1;

            if (!isMarkerEnabled(markerIdx)) {
                size_t pos = getPlaybackPosition();
                // Constrain against neighbors
                if (markerIdx > 0 && isMarkerEnabled(markerIdx - 1) && pos <= getMarkerPosition(markerIdx - 1))
                    pos = getMarkerPosition(markerIdx - 1) + stepSize;
                if (markerIdx < 3 && isMarkerEnabled(markerIdx + 1) && pos >= getMarkerPosition(markerIdx + 1))
                    pos = (getMarkerPosition(markerIdx + 1) > stepSize) ? getMarkerPosition(markerIdx + 1) - stepSize : pos;
                setMarkerPosition(markerIdx, pos);
            } else {
                size_t pos = getMarkerPosition(markerIdx);
                if (delta > 0) {
                    pos = (pos + stepSize < totalFrames) ? pos + stepSize : totalFrames;
                    if (markerIdx < 3 && isMarkerEnabled(markerIdx + 1) && pos >= getMarkerPosition(markerIdx + 1))
                        pos = (getMarkerPosition(markerIdx + 1) > stepSize) ? getMarkerPosition(markerIdx + 1) - stepSize : getMarkerPosition(markerIdx);
                } else {
                    pos = (pos > stepSize) ? pos - stepSize : 0;
                    if (markerIdx > 0 && isMarkerEnabled(markerIdx - 1) && pos <= getMarkerPosition(markerIdx - 1))
                        pos = getMarkerPosition(markerIdx - 1) + stepSize;
                }
                setMarkerPosition(markerIdx, pos);
            }
        }
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
    // Modifier button (pad 26)
    if (pad == 26) {
        m_modifierHeld.store(pressed);
        return;
    }

    // Play/stop: pad 19 is the physical play pad; pad 0 kept as legacy trigger.
    if ((pad == 0 || pad == 19) && pressed) {
        if (isPlaying()) {
            stop();
            if (m_serialController) m_serialController->sendMessage("LED:3:OFF");
        } else {
            play();
            if (m_serialController) m_serialController->sendMessage("LED:3:ON");
        }
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
