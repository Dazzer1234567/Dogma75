#include "serial_controller.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// ==================== VELOCITY CURVE IMPLEMENTATION ====================

float VelocityCurve::evaluate(float normalizedInput) const {
    if (numPoints < 2) return normalizedInput;

    normalizedInput = std::max(0.0f, std::min(1.0f, normalizedInput));

    for (int i = 0; i < numPoints - 1; i++) {
        if (normalizedInput >= points[i].x && normalizedInput <= points[i + 1].x) {
            float t = (normalizedInput - points[i].x) / (points[i + 1].x - points[i].x);
            if (smoothed) {
                return evaluateSmooth(normalizedInput);
            }
            return points[i].y + t * (points[i + 1].y - points[i].y);
        }
    }

    return points[numPoints - 1].y;
}

float VelocityCurve::evaluateSmooth(float normalizedInput) const {
    if (numPoints < 2) return normalizedInput;

    normalizedInput = std::max(0.0f, std::min(1.0f, normalizedInput));

    int seg = 0;
    for (int i = 0; i < numPoints - 1; i++) {
        if (normalizedInput >= points[i].x && normalizedInput <= points[i + 1].x) {
            seg = i;
            break;
        }
        if (i == numPoints - 2) seg = i;
    }

    VelocityCurvePoint p0, p1, p2, p3;
    p1 = points[seg];
    p2 = points[seg + 1];

    if (seg == 0) {
        p0 = {p1.x - (p2.x - p1.x), p1.y - (p2.y - p1.y)};
    } else {
        p0 = points[seg - 1];
    }

    if (seg >= numPoints - 2) {
        p3 = {p2.x + (p2.x - p1.x), p2.y + (p2.y - p1.y)};
    } else {
        p3 = points[seg + 2];
    }

    float t = 0.0f;
    if (p2.x != p1.x) {
        t = (normalizedInput - p1.x) / (p2.x - p1.x);
    }

    float t2 = t * t;
    float t3 = t2 * t;

    float y = 0.5f * (
        (2.0f * p1.y) +
        (-p0.y + p2.y) * t +
        (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3
    );

    return std::max(0.0f, y);
}

// ==================== SERIAL CONTROLLER ====================

SerialController::SerialController()
    : m_serialHandle(nullptr)
{
    memset(m_rpmWindow, 0, sizeof(m_rpmWindow));
}

SerialController::~SerialController() {
    shutdown();
}

bool SerialController::initialize(const std::string& portName) {
#ifdef _WIN32
    printf("SERIAL: Attempting to open %s...\n", portName.c_str());

    shutdown();

    std::string fullPortName = "\\\\.\\" + portName;

    HANDLE hSerial = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("SERIAL ERROR: Failed to open %s (Windows error %lu)\n", portName.c_str(), error);
        return false;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Failed to get serial port state" << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Failed to set serial port parameters" << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    // Block ReadFile until a byte arrives (returns immediately once any bytes
    // are present), with a 50 ms ceiling so the reader thread can poll the
    // stop flag during shutdown.
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 50;
    // Keep writes bounded — worst case ~15 ms so a stalled Teensy can't wedge
    // the reader thread (which shares this handle for LED sendMessage calls).
    timeouts.WriteTotalTimeoutConstant = 10;
    timeouts.WriteTotalTimeoutMultiplier = 1;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "Failed to set serial port timeouts" << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    m_serialHandle = hSerial;
    m_portName = portName;
    m_buttonPressed = false;
    m_serialBuffer.clear();

    std::cout << "Teensy serial port opened: " << portName << " at 115200 baud" << std::endl;

    // Spawn the reader thread now that the handle is valid.
    m_readerStop.store(false);
    m_readerThread = std::thread(&SerialController::readerLoop, this);

    return true;
#else
    (void)portName;
    std::cerr << "Serial port not supported on this platform" << std::endl;
    return false;
#endif
}

bool SerialController::autoDetect() {
#ifdef _WIN32
    printf("SERIAL: Auto-detecting Teensy...\n");

    for (int port = 1; port <= 20; port++) {
        std::string portName = "COM" + std::to_string(port);
        std::string fullPortName = "\\\\.\\" + portName;

        HANDLE hSerial = CreateFileA(
            fullPortName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
        );

        if (hSerial == INVALID_HANDLE_VALUE) continue;

        // Configure port
        DCB dcb = {0};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(hSerial, &dcb);
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        if (!SetCommState(hSerial, &dcb)) {
            CloseHandle(hSerial);
            continue;
        }

        // Short read timeout for scanning
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 100;
        timeouts.ReadTotalTimeoutConstant = 2000;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        SetCommTimeouts(hSerial, &timeouts);

        // Read for up to 6 seconds looking for heartbeat
        printf("SERIAL: Trying %s...\n", portName.c_str());
        std::string buffer;
        char readBuf[256];
        DWORD bytesRead;
        auto startTime = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(6)) {
            if (ReadFile(hSerial, readBuf, sizeof(readBuf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                readBuf[bytesRead] = '\0';
                buffer += readBuf;
                if (buffer.find("heartbeat") != std::string::npos ||
                    buffer.find("DogControl") != std::string::npos) {
                    CloseHandle(hSerial);
                    printf("SERIAL: Teensy found on %s!\n", portName.c_str());
                    return initialize(portName);
                }
            }
        }
        CloseHandle(hSerial);
    }

    printf("SERIAL: Teensy not found on any COM port\n");
    return false;
#else
    return false;
#endif
}

void SerialController::shutdown() {
#ifdef _WIN32
    // Signal the reader thread and wait for it to exit before closing the
    // handle it's reading from.
    m_readerStop.store(true);
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
    if (m_serialHandle) {
        CloseHandle(static_cast<HANDLE>(m_serialHandle));
        m_serialHandle = nullptr;
        std::cout << "Teensy serial port closed" << std::endl;
    }
#endif
}

void SerialController::sendMessage(const std::string& message) {
#ifdef _WIN32
    if (!m_serialHandle) return;
    HANDLE hSerial = static_cast<HANDLE>(m_serialHandle);
    DWORD bytesWritten;
    std::string msg = message + "\n";
    std::lock_guard<std::mutex> lock(m_sendMutex);
    WriteFile(hSerial, msg.c_str(), (DWORD)msg.size(), &bytesWritten, NULL);
#else
    (void)message;
#endif
}

// No-op: the reader thread now dispatches callbacks the instant bytes arrive.
// Kept so main.cpp's polling call site stays valid without changes.
void SerialController::processMessages() {}

void SerialController::readerLoop() {
#ifdef _WIN32
    if (!m_serialHandle) return;
    HANDLE hSerial = static_cast<HANDLE>(m_serialHandle);
    char buffer[256];
    DWORD bytesRead;

    while (!m_readerStop.load()) {
        // Blocks until any bytes arrive (or 50 ms elapses, per COMMTIMEOUTS
        // in initialize()). Sub-ms wake once the Teensy sends anything.
        if (!ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            // Port likely closed / device unplugged.
            break;
        }
        if (bytesRead == 0) continue;

        buffer[bytesRead] = '\0';
        m_serialBuffer += buffer;

        size_t newlinePos;
        while ((newlinePos = m_serialBuffer.find('\n')) != std::string::npos) {
            std::string line = m_serialBuffer.substr(0, newlinePos);
            m_serialBuffer = m_serialBuffer.substr(newlinePos + 1);

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Callbacks fire on this thread. AudioEngine's touch/encoder
            // handlers touch only atomic playback state, so this is safe.
            processLine(line);
        }
    }
#endif
}

void SerialController::calculateRpmAndVelocity(long delta, float& rpm, float& velocityMultiplier) {
    auto now = std::chrono::steady_clock::now();
    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();

    m_lastDeltaTime = currentTime;

    // Add sample to sliding window
    m_rpmWindow[m_rpmWindowHead].time = currentTime;
    m_rpmWindow[m_rpmWindowHead].pulses = delta;
    m_rpmWindowHead = (m_rpmWindowHead + 1) % RPM_WINDOW_SIZE;
    if (m_rpmWindowCount < RPM_WINDOW_SIZE) {
        m_rpmWindowCount++;
    }

    // Calculate RPM over the configured time window
    double windowSeconds = m_velocityCurve.rpmWindowMs / 1000.0;
    double windowStart = currentTime - windowSeconds;
    long totalPulses = 0;
    double oldestTime = currentTime;
    double newestTime = 0;
    int samplesInWindow = 0;

    for (int i = 0; i < m_rpmWindowCount; i++) {
        int idx = (m_rpmWindowHead - 1 - i + RPM_WINDOW_SIZE) % RPM_WINDOW_SIZE;
        if (m_rpmWindow[idx].time >= windowStart) {
            totalPulses += std::abs(m_rpmWindow[idx].pulses);
            if (m_rpmWindow[idx].time < oldestTime) oldestTime = m_rpmWindow[idx].time;
            if (m_rpmWindow[idx].time > newestTime) newestTime = m_rpmWindow[idx].time;
            samplesInWindow++;
        }
    }

    rpm = 0.0f;
    double actualWindow = newestTime - oldestTime;
    if (samplesInWindow >= 2 && actualWindow > 0.001) {
        float pulsesPerSecond = (float)totalPulses / (float)actualWindow;
        rpm = (pulsesPerSecond / 2400.0f) * 60.0f;
    } else if (samplesInWindow == 1) {
        float pulsesPerSecond = std::abs((float)delta) / 0.01f;
        rpm = (pulsesPerSecond / 2400.0f) * 60.0f;
    }

    // Light smoothing
    m_currentRpm = m_currentRpm * 0.5f + rpm * 0.5f;

    // Apply velocity curve
    velocityMultiplier = 1.0f;
    if (m_velocityCurve.enabled) {
        float normalizedInput = m_currentRpm / m_velocityCurve.maxInputRpm;
        velocityMultiplier = m_velocityCurve.evaluate(normalizedInput);
        if (velocityMultiplier > m_velocityCurve.maxMultiplier) {
            velocityMultiplier = m_velocityCurve.maxMultiplier;
        }
    }
}

void SerialController::processLine(const std::string& line) {
    // Parse encoder messages: E1-E6
    for (int enc = 1; enc <= 6; enc++) {
        std::string prefix = "E" + std::to_string(enc) + ":";
        if (line.find(prefix) == 0) {
            std::string numStr = line.substr(prefix.size());
            try {
                long delta = std::stol(numStr);
                int idx = enc - 1;
                m_encoderLastActivity[idx] = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                m_encoderLastDelta[idx] = delta;

                if (delta != 0 && m_encoderCallback) {
                    float rpm = 0.0f;
                    float velocityMultiplier = 1.0f;

                    // Only calculate RPM/velocity for E1 (the main scrub encoder)
                    if (enc == 1) {
                        calculateRpmAndVelocity(delta, rpm, velocityMultiplier);
                    }

                    m_encoderCallback(enc, delta, rpm, velocityMultiplier);
                }
            } catch (...) {
                printf("E%d: parse error for '%s'\n", enc, numStr.c_str());
            }
            return;
        }
    }

    // Also handle legacy "Delta:" prefix for E1
    if (line.find("Delta:") == 0) {
        std::string numStr = line.substr(6);
        try {
            long delta = std::stol(numStr);
            m_encoderLastActivity[0] = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            m_encoderLastDelta[0] = delta;

            if (delta != 0 && m_encoderCallback) {
                float rpm = 0.0f, vel = 1.0f;
                calculateRpmAndVelocity(delta, rpm, vel);
                m_encoderCallback(1, delta, rpm, vel);
            }
        } catch (...) {}
        return;
    }

    // Parse touch messages: "TOUCH:N"
    if (line.find("TOUCH:") == 0) {
        std::string numStr = line.substr(6);
        try {
            int touchNum = std::stoi(numStr);
            if (touchNum >= 0 && touchNum < 36) {
                m_touchState[touchNum] = true;
            }
            if (touchNum == 0) {
                m_buttonPressed = true;
            }
            if (m_touchCallback) {
                m_touchCallback(touchNum, true);
            }
        } catch (...) {}
        return;
    }

    // Parse display-mode messages: firmware announces its current display
    // mode so the DAW can ignore user events while in diagnostic mode.
    if (line == "MODE:DIAG" || line == "MODE:DESC") {
        if (m_modeCallback) {
            m_modeCallback(line == "MODE:DESC");
        }
        return;
    }

    // Parse release messages: "RELEASE:N"
    if (line.find("RELEASE:") == 0) {
        std::string numStr = line.substr(8);
        try {
            int touchNum = std::stoi(numStr);
            if (touchNum >= 0 && touchNum < 36) {
                m_touchState[touchNum] = false;
            }
            if (touchNum == 0) {
                m_buttonPressed = false;
            }
            if (m_touchCallback) {
                m_touchCallback(touchNum, false);
            }
        } catch (...) {}
        return;
    }

    // Legacy button messages
    if (line.find("Button PRESSED") != std::string::npos) {
        m_buttonPressed = true;
        if (m_touchCallback) {
            m_touchCallback(0, true);
        }
        return;
    }
    if (line.find("Button released") != std::string::npos) {
        m_buttonPressed = false;
        if (m_touchCallback) {
            m_touchCallback(0, false);
        }
        return;
    }
}
