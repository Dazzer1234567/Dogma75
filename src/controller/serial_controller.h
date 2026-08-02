#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

// Velocity curve (moved from audio_engine.h)
struct VelocityCurvePoint {
    float x;  // Input: 0.0 = stopped, 1.0 = max input speed (normalized)
    float y;  // Output: multiplier
};

struct VelocityCurve {
    static const int MAX_POINTS = 8;
    VelocityCurvePoint points[MAX_POINTS];
    int numPoints = 4;
    bool enabled = true;
    bool smoothed = false;
    float maxInputRpm = 120.0f;
    float rpmWindowMs = 50.0f;
    float maxMultiplier = 2.0f;
    float baseMultiplier = 1.0f;

    VelocityCurve() {
        points[0] = {0.0f, 0.0f};
        points[1] = {0.33f, 0.33f};
        points[2] = {0.66f, 0.66f};
        points[3] = {1.0f, 1.0f};
        numPoints = 4;
    }

    float evaluate(float normalizedInput) const;
    float evaluateSmooth(float normalizedInput) const;
};

// Callback types for events from the Teensy
using EncoderCallback = std::function<void(int encoder, long delta, float rpm, float velocityMultiplier)>;
using TouchCallback = std::function<void(int pad, bool pressed)>;
// Fires when the firmware announces its display mode: descriptive == true
// means the DAW should respond to user inputs; false = diagnostic mode
// where the DAW must ignore all touch/encoder events.
using ModeCallback = std::function<void(bool descriptive)>;
// Fires when the firmware sends "NAME:<text>" — the user has finalised
// a track name via the on-controller text-entry flow (pad 21).
using NameCallback = std::function<void(const std::string& name)>;
// Fires on live text-input state changes. `buffer` is the on-screen text
// (committed + any pending letter substituted at cursorPos); `cursorPos`
// is where the DAW should overlay a blinking cursor; `active` is false
// when the firmware left text-input mode (RENAMEEND) so the GUI can drop
// the preview.
using RenameBufferCallback = std::function<void(const std::string& buffer, int cursorPos, bool active)>;
// Fires once at the start of a rename with the firmware's current LED-
// flash phase (0 .. 499 ms). The DAW uses this to pulse its own preview
// in sync with the physical LEDs.
using RenameSyncCallback = std::function<void(int phaseMs)>;
// Fires when the firmware asks the DAW to re-push its authoritative
// state (LEDs, mode, OLED). Sent after the pad-18 hard-reset gesture.
using ResyncCallback = std::function<void()>;

class SerialController {
public:
    SerialController();
    ~SerialController();

    // Connection
    bool initialize(const std::string& portName = "COM6");
    bool autoDetect();  // Scan COM ports for Teensy
    void shutdown();
    bool isConnected() const { return m_serialHandle != nullptr; }
    const std::string& getPortName() const { return m_portName; }

    // Message processing (no-op stub; the reader thread now dispatches callbacks
    // directly. Kept for source compatibility with the main loop's polling call.)
    void processMessages();
    void sendMessage(const std::string& message);

    // Set callbacks for events
    void setEncoderCallback(EncoderCallback cb) { m_encoderCallback = cb; }
    void setTouchCallback(TouchCallback cb) { m_touchCallback = cb; }
    void setModeCallback(ModeCallback cb) { m_modeCallback = cb; }
    void setNameCallback(NameCallback cb) { m_nameCallback = cb; }
    void setRenameBufferCallback(RenameBufferCallback cb) { m_renameBufferCallback = cb; }
    void setRenameSyncCallback(RenameSyncCallback cb) { m_renameSyncCallback = cb; }
    void setResyncCallback(ResyncCallback cb)         { m_resyncCallback = cb; }

    // Test page state (read by GUI)
    bool isTouched(int pad) const { return (pad >= 0 && pad < 36) ? m_touchState[pad] : false; }
    double getEncoderLastActivity(int enc) const { return (enc >= 0 && enc < 6) ? m_encoderLastActivity[enc] : 0.0; }
    long getEncoderLastDelta(int enc) const { return (enc >= 0 && enc < 6) ? m_encoderLastDelta[enc] : 0; }

    // Velocity curve access
    VelocityCurve& getVelocityCurve() { return m_velocityCurve; }
    const VelocityCurve& getVelocityCurve() const { return m_velocityCurve; }
    float getCurrentRpm() const { return m_currentRpm; }

    // Button state
    bool isButtonPressed() const { return m_buttonPressed; }

private:
    // Serial port
    void* m_serialHandle;
    std::string m_portName;
    std::string m_serialBuffer;

    // Reader thread (dispatches callbacks the moment bytes arrive, bypassing
    // the ~4 ms main-loop polling window that used to sit between the byte
    // hitting the OS buffer and handleTouch/handleEncoderDelta running).
    std::thread m_readerThread;
    std::atomic<bool> m_readerStop{false};
    void readerLoop();

    // Writer thread: WriteFile on the Teensy's USB-CDC port routinely blocks
    // for hundreds of ms — sometimes seconds — despite COMMTIMEOUTS, when
    // the Teensy stops draining its endpoint (busy with I2C / OLED). Any
    // blocking write on the main thread stalls rendering and freezes the
    // playhead. So sendMessage() enqueues here; the writer thread does the
    // WriteFile calls off the main loop.
    std::thread m_writerThread;
    std::atomic<bool> m_writerStop{false};
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<std::string> m_sendQueue;
    static constexpr size_t MAX_QUEUE_SIZE = 256;
    void writerLoop();

    // Callbacks
    EncoderCallback m_encoderCallback;
    TouchCallback m_touchCallback;
    ModeCallback m_modeCallback;
    NameCallback m_nameCallback;
    RenameBufferCallback m_renameBufferCallback;
    RenameSyncCallback   m_renameSyncCallback;
    ResyncCallback       m_resyncCallback;

    // Touch state (for test page display)
    bool m_touchState[36] = {};
    bool m_buttonPressed = false;

    // Encoder activity tracking (for test page display)
    double m_encoderLastActivity[6] = {};
    long m_encoderLastDelta[6] = {};

    // Velocity curve and RPM calculation
    VelocityCurve m_velocityCurve;
    float m_currentRpm = 0.0f;
    double m_lastDeltaTime = 0.0;

    // Sliding window for RPM calculation
    static const int RPM_WINDOW_SIZE = 32;
    struct EncoderSample {
        double time;
        long pulses;
    };
    EncoderSample m_rpmWindow[RPM_WINDOW_SIZE];
    int m_rpmWindowHead = 0;
    int m_rpmWindowCount = 0;

    // Scrub timeout tracking
    std::atomic<bool>* m_scrubbingPtr = nullptr;
    std::atomic<float>* m_scrubRatePtr = nullptr;

    // Process a complete line from serial
    void processLine(const std::string& line);

    // Calculate RPM and velocity multiplier for E1 encoder
    void calculateRpmAndVelocity(long delta, float& rpm, float& velocityMultiplier);
};
