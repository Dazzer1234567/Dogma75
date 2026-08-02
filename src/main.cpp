#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include "audio/audio_engine.h"
#include "controller/serial_controller.h"
#include "gui/gui_manager.h"

int main(int argc, char** argv) {
    std::cout << "==================================" << std::endl;
    std::cout << "   Minimal DAW v0.1.0" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    // Initialize audio engine
    AudioEngine audioEngine;
    if (!audioEngine.initialize()) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        return 1;
    }

    std::cout << std::endl;

    // Find Orion ASIO device first, then fall back to Hammerfall
    int deviceId = -1;
    int deviceCount = audioEngine.getDeviceCount();

    // First try to find Orion
    for (int i = 0; i < deviceCount; i++) {
        std::string deviceName = audioEngine.getDeviceNameById(i);
        std::string hostApi = audioEngine.getDeviceHostApiById(i);
        if (deviceName.find("Orion") != std::string::npos
            && hostApi.find("ASIO") != std::string::npos) {
            deviceId = i;
            std::cout << "Found Orion ASIO device at index " << i << ": " << deviceName << std::endl;
            break;
        }
    }

    // Fall back to Hammerfall if Orion not found
    if (deviceId == -1) {
        for (int i = 0; i < deviceCount; i++) {
            std::string deviceName = audioEngine.getDeviceNameById(i);
            std::string hostApi = audioEngine.getDeviceHostApiById(i);
            if ((deviceName.find("Hammerfall") != std::string::npos || deviceName.find("HDSP") != std::string::npos)
                && hostApi.find("ASIO") != std::string::npos) {
                deviceId = i;
                std::cout << "Found Hammerfall ASIO device at index " << i << ": " << deviceName << std::endl;
                break;
            }
        }
    }

    if (deviceId == -1) {
        std::cout << "No preferred ASIO device found, using default device" << std::endl;
    }

    // Start audio
    if (!audioEngine.startAudio(deviceId)) {
        std::cerr << "Failed to start audio" << std::endl;
        return 1;
    }

    std::cout << std::endl;

    // Initialize MIDI
    audioEngine.initializeMidi();

    // Initialize Teensy serial controller
    SerialController serialController;
    serialController.autoDetect();

    // Wire serial callbacks to audio engine
    audioEngine.setSerialController(&serialController);
    serialController.setEncoderCallback([&audioEngine](int encoder, long delta, float rpm, float velocityMultiplier) {
        audioEngine.handleEncoderDelta(encoder, delta, rpm, velocityMultiplier);
    });
    serialController.setTouchCallback([&audioEngine](int pad, bool pressed) {
        audioEngine.handleTouch(pad, pressed);
    });
    serialController.setModeCallback([&audioEngine](bool descriptive) {
        audioEngine.handleModeChange(descriptive);
    });
    serialController.setNameCallback([&audioEngine](const std::string& name) {
        audioEngine.handleTrackNameFromController(name);
    });
    serialController.setRenameBufferCallback([&audioEngine](const std::string& buf, int cursor, bool active) {
        audioEngine.handleRenameBuffer(buf, cursor, active);
    });
    serialController.setResyncCallback([&audioEngine]() {
        audioEngine.handleResync();
    });
    serialController.setRenameSyncCallback([&audioEngine](int phaseMs) {
        audioEngine.handleRenameSync(phaseMs);
    });

    std::cout << std::endl;

    // Auto-load WAV file into a track
    std::string defaultWavPath = "C:\\0_CODE\\Audio\\James.wav";
    int trackIndex = audioEngine.addTrack("James");
    if (audioEngine.loadTrackAudio(trackIndex, defaultWavPath)) {
        std::cout << "Auto-loaded: " << defaultWavPath << std::endl;
    } else {
        std::cerr << "Warning: Could not auto-load " << defaultWavPath << std::endl;
    }

    std::cout << std::endl;

    // Initialize GUI
    GUIManager guiManager;
    if (!guiManager.initialize(&audioEngine, &serialController)) {
        std::cerr << "Failed to initialize GUI" << std::endl;
        audioEngine.shutdown();
        return 1;
    }

    // Main loop with per-stage timing so the FPS overlay can point at the
    // stage responsible for a hitch. Each label is a static string; the
    // overlay tracks worst-seen values per label.
    while (guiManager.isRunning()) {
        auto t0 = std::chrono::steady_clock::now();
        audioEngine.processMidiMessages();
        auto t1 = std::chrono::steady_clock::now();
        serialController.processMessages();   // no-op stub
        auto t2 = std::chrono::steady_clock::now();
        audioEngine.updateController();
        auto t3 = std::chrono::steady_clock::now();
        guiManager.processFrame();
        auto t4 = std::chrono::steady_clock::now();

        auto ms = [](auto a, auto b) {
            return std::chrono::duration<float, std::milli>(b - a).count();
        };
        guiManager.reportFrameStats(ms(t0, t4), ms(t0, t1), ms(t2, t3), ms(t3, t4));
    }

    // Cleanup
    guiManager.shutdown();
    // Turn every controller LED off before we close the serial port —
    // otherwise LEDs stay lit at whatever value the last update left
    // them, which looks like a stuck controller after the DAW quits.
    for (int ch = 0; ch < 9; ch++) {
        serialController.sendMessage("LED:" + std::to_string(ch) + ":OFF");
    }
    // Small window for the async writer thread to drain its queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    serialController.shutdown();
    audioEngine.shutdown();

    std::cout << std::endl;
    std::cout << "Shutdown complete" << std::endl;
    return 0;
}
