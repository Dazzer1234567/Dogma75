#pragma once

#include <vector>
#include <string>

// Forward declarations
class AudioEngine;
class SerialController;
struct Track;
struct SDL_Window;
typedef void* SDL_GLContext;

// Structures to store draw commands for multi-pass rendering
struct WaveformDrawCmd {
    float x1, y1, x2, y2;
    unsigned int color;
    bool isRect;  // true = filled rect, false = line
    float rectX2, rectY2;  // for rects
};

struct LineDrawCmd {
    float x1, y1, x2, y2;
    unsigned int color;
    float thickness;
    bool isTriangle;
    float tx1, ty1, tx2, ty2, tx3, ty3;  // triangle vertices
    bool isText;
    float textX, textY;
    char text[8];
};

class GUIManager {
public:
    GUIManager();
    ~GUIManager();

    bool initialize(AudioEngine* audioEngine, SerialController* serialController);
    void shutdown();

    bool isRunning() const { return m_running; }
    void processFrame();
    void render();

    // Per-stage timing samples fed in by main.cpp so the FPS overlay can
    // report which stage of the loop stalled. Values are milliseconds.
    void reportStageTime(const char* stage, float ms);
    // One-call-per-frame stats. Feeds the FPS overlay's stage panels AND
    // keeps a rolling ring buffer of the last N frames. When total_ms
    // crosses SPIKE_LOG_THRESHOLD_MS the ring buffer is dumped to
    // perf.log with a "SPIKE" marker, giving Claude a self-serve
    // snapshot of exactly what happened around the hitch.
    void reportFrameStats(float total_ms, float midi_ms, float update_ms, float frame_ms);

private:
    bool m_running;
    SDL_Window* m_window;
    // Rolling worst-frame-time-per-stage bookkeeping, for the FPS overlay.
    // Keyed by a short static-string label so we don't allocate at report time.
    struct StageStat {
        const char* label;
        float worstMs;
        float lastMs;
    };
    static constexpr int kStageStatCount = 6;
    StageStat m_stageStats[kStageStatCount] = {};

    // Rolling frame-time history for the perf.log dumper.
    struct FrameSample {
        int64_t tMs;         // wall-clock ms since app start (Perf lightweight)
        float total, midi, update, frame;
    };
    static constexpr int kFrameHistoryLen = 240;   // ~4 s at 60 fps
    FrameSample m_frameHistory[kFrameHistoryLen] = {};
    int  m_frameHistoryIdx   = 0;
    int  m_frameHistoryCount = 0;
    int64_t m_perfStartMs    = 0;
    int64_t m_lastSpikeDumpMs = 0;
    static constexpr float   SPIKE_LOG_THRESHOLD_MS = 100.0f;
    static constexpr int64_t SPIKE_LOG_MIN_GAP_MS  = 2000;  // rate-limit dumps
    SDL_GLContext m_glContext;
    AudioEngine* m_audioEngine;
    SerialController* m_serialController;

    // Smooth zoom interpolation
    float m_displayZoom;

    // Waveform display mode
    bool m_simplifiedWaveform;

    // Park buttons
    char m_parkNames[4][64];
    int m_editingParkButton;
    int m_selectedParkButton;
    // Left panel view: 0 = Tracks, 1 = Scrubbing. Selected via a dropdown
    // combo at the top of the panel.
    int m_leftPanelMode = 0;

    // Sticky "last folder" for the Windows file dialogs — kept SEPARATE
    // per dialog type so browsing an audio file doesn't move the session
    // dialog's starting folder (and vice versa). Persisted to
    // user_settings.json so they survive DAW restarts.
    std::string m_lastAudioDir;
    std::string m_lastSessionDir;
    // Path of the currently-open session file (populated on successful
    // load or save-as). Empty when nothing has been opened this session,
    // in which case Revert is disabled.
    std::string m_currentSessionPath;
    // Shared vertical scroll offset for the track list — the left panel
    // (track headers/controls) and the arrangement view (waveforms) both
    // apply and read this so they stay in lock-step when the user has
    // more tracks than fit on screen.
    float m_trackScrollY = 0.0f;

    // Zoom smoothing toggle
    bool m_zoomSmoothing;

    // Controller mode: 0 = Custom, 1 = Mackie
    int m_controllerMode;

    // Waveform scroll mode
    bool m_waveformScrolling;
    // When m_waveformScrolling is off, this controls whether the view still
    // auto-pages (jumps to keep the playhead on screen) or stays put and lets
    // the playhead drift off the right edge.
    bool m_waveformAutoPage;
    // Zoom-anchor stickiness: once a zoom operation decides whether to pin
    // the playhead or the view centre, it stays with that choice until the
    // user reverses direction.
    int  m_lastZoomDirection = 0;      // +1 zoom-in, -1 zoom-out, 0 = fresh
    bool m_zoomAnchorPinPlayhead = true;


    // Waveform vertical zoom (amplitude scaling) and track height
    float m_waveformVerticalZoom;  // 1.0 = normal, 2.0 = 2x amplitude
    float m_trackHeight;           // Height in pixels (default 80)

    // Stationary mode view position
    size_t m_viewCenterPosition;
    float m_lastZoom;

    // Color scheme
    int m_colorScheme;
    float m_customBgColor[3];
    float m_customTextColor[3];
    bool m_showColorPickers;
    bool m_editingBgColor;
    void applyColorScheme(int scheme);

    // Bloom/glow effect - separate intensities for different element types
    bool m_bloomEnabled;
    float m_bloomTextIntensity;    // Glow for text/labels
    float m_bloomLinesIntensity;   // Glow for marker lines and playhead
    float m_bloomUIIntensity;      // Glow for waveform, sliders, solid UI elements

    // Bloom framebuffer objects - multi-layer system
    // Layer 0: Full scene (for text/UI glow)
    // Layer 1: Lines only (markers, playhead)
    // Layer 2: Waveform only
    unsigned int m_sceneFBO;           // Main scene framebuffer
    unsigned int m_sceneTexture;       // Main scene texture
    unsigned int m_linesFBO;           // Lines-only framebuffer
    unsigned int m_linesTexture;       // Lines-only texture
    unsigned int m_waveformFBO;        // Waveform-only framebuffer
    unsigned int m_waveformTexture;    // Waveform-only texture
    unsigned int m_bloomFBO[2];        // Ping-pong framebuffers for blur
    unsigned int m_bloomTexture[2];    // Ping-pong textures for blur
    unsigned int m_textBlurTexture;    // Final blurred text layer
    unsigned int m_linesBlurTexture;   // Final blurred lines layer
    unsigned int m_uiBlurTexture;      // Final blurred UI/waveform layer
    unsigned int m_blurShader;         // Gaussian blur
    unsigned int m_maskShader;         // Mask extraction shader (now copy shader)
    unsigned int m_compositeShader;    // Final multi-layer composite
    unsigned int m_lineShader;         // Simple 2D line/shape shader
    unsigned int m_quadVAO;
    unsigned int m_quadVBO;
    unsigned int m_lineVAO;            // VAO for drawing lines/shapes
    unsigned int m_lineVBO;            // VBO for drawing lines/shapes
    int m_windowWidth;
    int m_windowHeight;

    // Fullscreen mode
    bool m_isFullscreen;

    // Velocity curve editor window
    bool m_showVelocityCurveEditor;

    // Hardware test page
    bool m_showTestPage;

    // Stored draw commands for multi-pass rendering
    std::vector<WaveformDrawCmd> m_waveformDrawCmds;
    std::vector<LineDrawCmd> m_lineDrawCmds;

    // UI rendering methods (extracted from processFrame)
    void renderToolbar();
    void renderTrackPanel(float width, float height);
    void renderWaveform(float height);
    void renderTransportBar();
    void renderVelocityCurveEditor();
    void renderTestPage();

    void initBloom();
    void cleanupBloom();
    void renderBloom();
    void drawWaveformLayer();
    void drawLinesLayer();
    unsigned int compileShader(const char* vertexSrc, const char* fragmentSrc);
    void saveSettings();
    void loadSettings();
    // Build (or rebuild) a wide RGBA texture containing the full waveform
    // envelope of `t`. Called lazily from renderWaveform when the track
    // has been (re)loaded since the last upload.
    void uploadWaveformTexture(Track* t);
    // Build the on-demand high-detail texture. Texel size is fixed at
    // `framesPerBucket` samples and boundaries are aligned to sample 0 so
    // that panning across the texture edge only shifts which aligned
    // window we show — texel contents themselves are always identical.
    // This is what keeps the render flicker-free during pan.
    void uploadWaveformDetailTexture(Track* t, size_t startFrame, size_t framesPerBucket);
    // Serialize the current DAW session (tracks + their metadata) to a JSON
    // file. Save writes to m_currentSessionPath directly (no dialog); if no
    // session has been saved yet this run it falls through to Save As.
    // Save As always prompts for a filename. saveSessionToPath does the
    // actual write.
    void saveSession();
    void saveSessionAs();
    void saveSessionToPath(const std::string& filename);
    // Load a session file written by saveSession(). Replaces the current
    // set of tracks and re-loads each track's audio from its stored path.
    void openSession();                                        // shows file dialog
    void loadSessionFromFile(const std::string& path);         // no dialog
    // File menu helpers.
    void revertSession();  // reload m_currentSessionPath from disk
    void closeSession();   // wipe tracks + markers, keep app running
};
