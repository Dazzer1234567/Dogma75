#pragma once

#include <vector>

// Forward declarations
class AudioEngine;
class SerialController;
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

private:
    bool m_running;
    SDL_Window* m_window;
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

    // Zoom smoothing toggle
    bool m_zoomSmoothing;

    // Controller mode: 0 = Custom, 1 = Mackie
    int m_controllerMode;

    // Waveform scroll mode
    bool m_waveformScrolling;

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
};
