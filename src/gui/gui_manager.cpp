#include "gui_manager.h"
#include "../audio/audio_engine.h"
#include "../controller/serial_controller.h"
#include "../util/daw_log.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>

// windows.h has to be included BEFORE SDL_syswm.h — the SDL header
// references HWND / HGLRC without pulling windows.h in itself.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#ifdef SDL2_FOUND
#include <SDL.h>
#include <SDL_syswm.h>
#endif

#ifdef IMGUI_FOUND
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#endif

#include <GL/gl.h>

// OpenGL 3.3 function pointers (loaded dynamically)
typedef void (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef GLenum (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, ptrdiff_t size, const void *data, GLenum usage);
typedef void (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const char **string, const GLint *length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const char *name);
typedef void (APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLBUFFERSUBDATAPROC)(GLenum target, ptrdiff_t offset, ptrdiff_t size, const void *data);

static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
static PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC glBufferData = nullptr;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
static PFNGLCREATESHADERPROC glCreateShader = nullptr;
static PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
static PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
static PFNGLATTACHSHADERPROC glAttachShader = nullptr;
static PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
static PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
static PFNGLDELETESHADERPROC glDeleteShader = nullptr;
static PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
static PFNGLUNIFORM1IPROC glUniform1i = nullptr;
static PFNGLUNIFORM1FPROC glUniform1f = nullptr;
static PFNGLUNIFORM2FPROC glUniform2f = nullptr;
static PFNGLUNIFORM3FPROC glUniform3f = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
static PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
static PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;

#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_CLAMP_TO_EDGE 0x812F

static void loadGLFunctions() {
#ifdef SDL2_FOUND
    glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glDeleteVertexArrays");
    glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
    glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");
    glUniform1f = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
    glUniform2f = (PFNGLUNIFORM2FPROC)SDL_GL_GetProcAddress("glUniform2f");
    glUniform3f = (PFNGLUNIFORM3FPROC)SDL_GL_GetProcAddress("glUniform3f");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    glBufferSubData = (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
#endif
}

GUIManager::GUIManager()
    : m_running(false)
    , m_window(nullptr)
    , m_glContext(nullptr)
    , m_audioEngine(nullptr)
    , m_serialController(nullptr)
    , m_displayZoom(1.0f)
    , m_simplifiedWaveform(false)
    , m_editingParkButton(-1)
    , m_selectedParkButton(0)
    , m_zoomSmoothing(false)
    , m_controllerMode(1)  // Default to Custom Mackie
    , m_waveformScrolling(false)
    , m_waveformAutoPage(false)
    , m_waveformVerticalZoom(1.0f)
    , m_trackHeight(80.0f)
    , m_viewCenterPosition(0)
    , m_lastZoom(1.0f)
    , m_colorScheme(0)
    , m_showColorPickers(false)
    , m_editingBgColor(true)
    , m_bloomEnabled(true)
    , m_bloomTextIntensity(0.0f)
    , m_bloomLinesIntensity(1.0f)
    , m_bloomUIIntensity(0.0f)
    , m_sceneFBO(0)
    , m_sceneTexture(0)
    , m_linesFBO(0)
    , m_linesTexture(0)
    , m_waveformFBO(0)
    , m_waveformTexture(0)
    , m_textBlurTexture(0)
    , m_linesBlurTexture(0)
    , m_uiBlurTexture(0)
    , m_blurShader(0)
    , m_maskShader(0)
    , m_compositeShader(0)
    , m_lineShader(0)
    , m_quadVAO(0)
    , m_quadVBO(0)
    , m_lineVAO(0)
    , m_lineVBO(0)
    , m_windowWidth(800)
    , m_windowHeight(600)
    , m_isFullscreen(false)   // we now start maximised, not fullscreen
    , m_showVelocityCurveEditor(false)
    , m_showTestPage(false)
{
    strcpy(m_parkNames[0], "Park 1");
    strcpy(m_parkNames[1], "Park 2");
    strcpy(m_parkNames[2], "Park 3");
    strcpy(m_parkNames[3], "Park 4");

    m_customBgColor[0] = 0.1f;  m_customBgColor[1] = 0.1f;  m_customBgColor[2] = 0.1f;
    m_customTextColor[0] = 0.9f; m_customTextColor[1] = 0.9f; m_customTextColor[2] = 0.9f;
}

GUIManager::~GUIManager() {
    shutdown();
}

// True if the path exists and is a regular file. Used both to pick the
// startup session and to grey out Recent Projects entries whose session has
// been moved or deleted, rather than dropping them from the list.
static bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool GUIManager::initialize(AudioEngine* audioEngine, SerialController* serialController) {
    std::cout << "Initializing GUI with SDL2 + OpenGL..." << std::endl;

    m_audioEngine = audioEngine;
    m_serialController = serialController;

    // Register view-state hooks with the engine's undo system so a
    // pad-26 undo also restores zoom / scroll / timeline extent.
    if (m_audioEngine) {
        m_audioEngine->setUndoGuiHooks(
            [this](UndoEntry& e) {
                e.guiDisplayZoom        = m_displayZoom;
                e.guiViewCenterPosition = m_viewCenterPosition;
                e.guiTimelineFrames     = m_timelineFrames;
            },
            [this](const UndoEntry& e) {
                m_displayZoom        = e.guiDisplayZoom;
                m_viewCenterPosition = e.guiViewCenterPosition;
                m_timelineFrames     = e.guiTimelineFrames;
                if (m_audioEngine) m_audioEngine->setWaveformZoom(e.guiDisplayZoom);
            }
        );
    }

#ifdef SDL2_FOUND
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // OpenGL 3.3 Core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Start MAXIMISED, not fullscreen — an ordinary window filling the
    // screen, keeping its title bar and the taskbar accessible. Fullscreen
    // is still available at runtime via the existing toggle.
    m_window = SDL_CreateWindow(
        "Minimal DAW v0.1.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1600, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED
    );
    m_isFullscreen = false;

    if (!m_window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_GL_MakeCurrent(m_window, m_glContext);
    SDL_GL_SetSwapInterval(0); // VSync off: shaves ~16 ms off input→pixel latency
                               // at the cost of possible tearing on the waveform.

    // Load OpenGL 3.3 functions
    loadGLFunctions();

#ifdef IMGUI_FOUND
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load custom font
    io.Fonts->AddFontFromFileTTF("c:\\0_CODE\\Dogma75\\external\\imgui\\misc\\fonts\\Glass_TTY_VT220.ttf", 16.0f);

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(m_window, m_glContext);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    // Initialize bloom resources
    initBloom();

    // Load saved settings
    loadSettings();

    // Auto-load the most recently used session, skipping any whose file has
    // since been moved or deleted. Replaces a hardcoded path that pointed at
    // one particular session on one particular machine — it did not exist on
    // the second machine, so every launch started empty and logged a failure.
    // Falls through to an empty session if the list is empty or nothing in it
    // still exists.
    for (const std::string& recent : m_recentSessions) {
        if (!fileExists(recent)) continue;
        loadSessionFromFile(recent);
        break;
    }
    retimeArrangement();

    // Startup always lands unmuted, regardless of what the auto-loaded
    // session says. Force the shadow flag off and push MUTEIND:0 + OSC
    // unmute so both TotalMix and the OLED indicator match. User has to
    // explicitly open a session (openSession / revertSession) to restore
    // a saved mute state.
    if (m_audioEngine) {
        m_audioEngine->setTotalMixInputPairMuted(false);
        m_audioEngine->syncTotalMixMuteToHardware();
        // Push each track's monitor->mute mapping to the Antelope mixer
        // so its state matches whatever the default session loaded.
        m_audioEngine->syncAllInputMonitorsToAntelope();
    }

    m_running = true;
    std::cout << "GUI initialized successfully with OpenGL " << glGetString(GL_VERSION) << std::endl;
    return true;
#else
    std::cerr << "SDL2 not found!" << std::endl;
    return false;
#endif
}

void GUIManager::shutdown() {
#ifdef SDL2_FOUND
    // Guard against double shutdown
    if (!m_window && !m_glContext) {
        return;
    }

    cleanupBloom();

#ifdef IMGUI_FOUND
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
#endif

    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
#endif
    m_running = false;
}

void GUIManager::initBloom() {
    if (!glGenFramebuffers) return;

    // Initialize arrays
    m_bloomFBO[0] = m_bloomFBO[1] = 0;
    m_bloomTexture[0] = m_bloomTexture[1] = 0;

    int blurWidth = m_windowWidth / 2;
    int blurHeight = m_windowHeight / 2;

    // Create main scene framebuffer
    glGenFramebuffers(1, &m_sceneFBO);
    glGenTextures(1, &m_sceneTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_windowWidth, m_windowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTexture, 0);

    // Create framebuffer for lines layer (full resolution for accurate rendering)
    glGenFramebuffers(1, &m_linesFBO);
    glGenTextures(1, &m_linesTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, m_linesFBO);
    glBindTexture(GL_TEXTURE_2D, m_linesTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_windowWidth, m_windowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_linesTexture, 0);

    // Create framebuffer for waveform layer (full resolution for accurate rendering)
    glGenFramebuffers(1, &m_waveformFBO);
    glGenTextures(1, &m_waveformTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, m_waveformFBO);
    glBindTexture(GL_TEXTURE_2D, m_waveformTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_windowWidth, m_windowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_waveformTexture, 0);

    // Create ping-pong framebuffers for blur passes
    glGenFramebuffers(2, m_bloomFBO);
    glGenTextures(2, m_bloomTexture);

    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[i]);
        glBindTexture(GL_TEXTURE_2D, m_bloomTexture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blurWidth, blurHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomTexture[i], 0);
    }

    // Create textures to store final blurred results for each layer
    glGenTextures(1, &m_textBlurTexture);
    glBindTexture(GL_TEXTURE_2D, m_textBlurTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blurWidth, blurHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_linesBlurTexture);
    glBindTexture(GL_TEXTURE_2D, m_linesBlurTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blurWidth, blurHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_uiBlurTexture);
    glBindTexture(GL_TEXTURE_2D, m_uiBlurTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blurWidth, blurHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Fullscreen quad
    float quadVertices[] = {
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Common vertex shader
    const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoords;
        out vec2 TexCoords;
        void main() {
            TexCoords = aTexCoords;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    // Gaussian blur shader (two-pass separable blur)
    const char* blurFragSrc = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;
        uniform sampler2D image;
        uniform bool horizontal;
        uniform float texelSize;

        // Gaussian kernel weights for 9-tap blur
        const float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

        void main() {
            vec2 offset = horizontal ? vec2(texelSize, 0.0) : vec2(0.0, texelSize);
            vec3 result = texture(image, TexCoords).rgb * weight[0];

            for(int i = 1; i < 5; ++i) {
                result += texture(image, TexCoords + offset * float(i)).rgb * weight[i];
                result += texture(image, TexCoords - offset * float(i)).rgb * weight[i];
            }
            FragColor = vec4(result, 1.0);
        }
    )";

    // Simple copy shader - just copies a texture (used for downsampling to blur buffer)
    const char* copyFragSrc = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;
        uniform sampler2D source;

        void main() {
            FragColor = texture(source, TexCoords);
        }
    )";

    // Multi-layer composite shader - combines scene with 3 separate blurred layers
    // Uses subtraction to isolate text glow from lines and waveform
    const char* compositeFragSrc = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;
        uniform sampler2D scene;          // Original scene (everything)
        uniform sampler2D sceneBlur;      // Blurred full scene
        uniform sampler2D linesBlur;      // Blurred lines layer
        uniform sampler2D waveformBlur;   // Blurred waveform layer
        uniform float textIntensity;
        uniform float linesIntensity;
        uniform float waveformIntensity;
        uniform vec3 bgColor;

        void main() {
            vec3 sceneColor = texture(scene, TexCoords).rgb;
            vec3 fullGlow = texture(sceneBlur, TexCoords).rgb;
            vec3 linesGlow = texture(linesBlur, TexCoords).rgb;
            vec3 waveformGlow = texture(waveformBlur, TexCoords).rgb;

            // Text glow = full scene glow minus lines and waveform glow
            // This isolates just the text/UI elements
            vec3 textGlow = max(fullGlow - linesGlow - waveformGlow, vec3(0.0));

            // Combine all glow layers with their respective intensities
            vec3 totalGlow = textGlow * textIntensity +
                            linesGlow * linesIntensity +
                            waveformGlow * waveformIntensity;

            // Detect if this is a "content" pixel vs background
            vec3 diff = abs(sceneColor - bgColor);
            float isContent = max(max(diff.r, diff.g), diff.b);

            // Apply glow behind content
            vec3 bloomLayer = bgColor + totalGlow;

            // Smoothly blend: content pixels show scene, background shows glow
            float contentMask = smoothstep(0.01, 0.1, isContent);
            vec3 result = mix(bloomLayer, sceneColor, contentMask);

            FragColor = vec4(result, 1.0);
        }
    )";

    m_blurShader = compileShader(vertexSrc, blurFragSrc);
    m_maskShader = compileShader(vertexSrc, copyFragSrc);  // Reuse maskShader variable for copy shader
    m_compositeShader = compileShader(vertexSrc, compositeFragSrc);

    // Simple 2D shader for drawing lines and shapes
    const char* lineVertSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec4 aColor;
        out vec4 vColor;
        uniform vec2 screenSize;
        void main() {
            // Convert from screen coords to NDC
            vec2 ndc = (aPos / screenSize) * 2.0 - 1.0;
            ndc.y = -ndc.y;  // Flip Y for screen coords
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )";

    const char* lineFragSrc = R"(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )";

    m_lineShader = compileShader(lineVertSrc, lineFragSrc);

    // Create VAO/VBO for dynamic line drawing
    glGenVertexArrays(1, &m_lineVAO);
    glGenBuffers(1, &m_lineVBO);
    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    // Reserve space for vertices (position + color per vertex)
    glBufferData(GL_ARRAY_BUFFER, 6 * 6 * sizeof(float) * 10000, nullptr, GL_DYNAMIC_DRAW);
    // Position attribute (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    // Color attribute (vec4)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void GUIManager::cleanupBloom() {
    if (glDeleteFramebuffers) {
        if (m_sceneFBO) glDeleteFramebuffers(1, &m_sceneFBO);
        if (m_linesFBO) glDeleteFramebuffers(1, &m_linesFBO);
        if (m_waveformFBO) glDeleteFramebuffers(1, &m_waveformFBO);
        if (m_bloomFBO[0]) glDeleteFramebuffers(2, m_bloomFBO);
    }
    if (m_sceneTexture) glDeleteTextures(1, &m_sceneTexture);
    if (m_linesTexture) glDeleteTextures(1, &m_linesTexture);
    if (m_waveformTexture) glDeleteTextures(1, &m_waveformTexture);
    if (m_bloomTexture[0]) glDeleteTextures(2, m_bloomTexture);
    if (m_textBlurTexture) glDeleteTextures(1, &m_textBlurTexture);
    if (m_linesBlurTexture) glDeleteTextures(1, &m_linesBlurTexture);
    if (m_uiBlurTexture) glDeleteTextures(1, &m_uiBlurTexture);
    if (glDeleteVertexArrays && m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (glDeleteBuffers && m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    if (glDeleteVertexArrays && m_lineVAO) glDeleteVertexArrays(1, &m_lineVAO);
    if (glDeleteBuffers && m_lineVBO) glDeleteBuffers(1, &m_lineVBO);
    if (glDeleteProgram) {
        if (m_blurShader) glDeleteProgram(m_blurShader);
        if (m_maskShader) glDeleteProgram(m_maskShader);
        if (m_compositeShader) glDeleteProgram(m_compositeShader);
        if (m_lineShader) glDeleteProgram(m_lineShader);
    }
}

unsigned int GUIManager::compileShader(const char* vertexSrc, const char* fragmentSrc) {
    if (!glCreateShader) return 0;

    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexSrc, nullptr);
    glCompileShader(vertex);

    GLint success;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vertex, 512, nullptr, log);
        std::cerr << "Vertex shader error: " << log << std::endl;
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentSrc, nullptr);
    glCompileShader(fragment);

    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fragment, 512, nullptr, log);
        std::cerr << "Fragment shader error: " << log << std::endl;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(program, 512, nullptr, log);
        std::cerr << "Shader link error: " << log << std::endl;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

void GUIManager::renderBloom() {
    if (!m_bloomEnabled || !m_compositeShader || !m_blurShader || !glBindFramebuffer) return;

    int blurWidth = m_windowWidth / 2;
    int blurHeight = m_windowHeight / 2;

    // Get background color
    float bgR, bgG, bgB;
    if (m_colorScheme == 1) {
        bgR = m_customBgColor[0]; bgG = m_customBgColor[1]; bgB = m_customBgColor[2];
    } else {
        bgR = 0.176f; bgG = 0.176f; bgB = 0.188f;
    }

    int blurPasses = 6;

    // Helper lambda to blur a source texture into a destination texture
    auto blurTexture = [&](unsigned int srcTexture, unsigned int dstTexture) {
        glBindVertexArray(m_quadVAO);

        // First pass: copy source to bloom buffer 0 (downsampling)
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[0]);
        glViewport(0, 0, blurWidth, blurHeight);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_maskShader);  // Using copy shader (stored in m_maskShader)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTexture);
        glUniform1i(glGetUniformLocation(m_maskShader, "source"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Blur passes
        glUseProgram(m_blurShader);
        bool horizontal = true;
        for (int i = 0; i < blurPasses; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[horizontal ? 1 : 0]);
            glUniform1i(glGetUniformLocation(m_blurShader, "horizontal"), horizontal ? 1 : 0);
            float texelSize = horizontal ? (1.0f / blurWidth) : (1.0f / blurHeight);
            texelSize *= 2.0f;
            glUniform1f(glGetUniformLocation(m_blurShader, "texelSize"), texelSize);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_bloomTexture[horizontal ? 0 : 1]);
            glUniform1i(glGetUniformLocation(m_blurShader, "image"), 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            horizontal = !horizontal;
        }

        // Copy result to destination texture
        glBindTexture(GL_TEXTURE_2D, dstTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, blurWidth, blurHeight);
        glBindVertexArray(0);
    };

    // Helper lambda to draw lines/shapes using raw OpenGL
    auto drawLinesRaw = [&]() {
        if (m_lineDrawCmds.empty() || !m_lineShader) return;

        std::vector<float> vertices;
        vertices.reserve(m_lineDrawCmds.size() * 36);  // Up to 6 verts * 6 floats

        for (const auto& cmd : m_lineDrawCmds) {
            // Extract color components
            float r = ((cmd.color >> 0) & 0xFF) / 255.0f;
            float g = ((cmd.color >> 8) & 0xFF) / 255.0f;
            float b = ((cmd.color >> 16) & 0xFF) / 255.0f;
            float a = ((cmd.color >> 24) & 0xFF) / 255.0f;

            if (cmd.isTriangle) {
                // Add triangle as 3 vertices
                vertices.insert(vertices.end(), {cmd.tx1, cmd.ty1, r, g, b, a});
                vertices.insert(vertices.end(), {cmd.tx2, cmd.ty2, r, g, b, a});
                vertices.insert(vertices.end(), {cmd.tx3, cmd.ty3, r, g, b, a});
            } else if (!cmd.isText) {
                // Draw line as a thin quad (2 triangles = 6 vertices)
                float dx = cmd.x2 - cmd.x1;
                float dy = cmd.y2 - cmd.y1;
                float len = std::sqrt(dx*dx + dy*dy);
                if (len > 0.001f) {
                    float nx = -dy / len * cmd.thickness * 0.5f;
                    float ny = dx / len * cmd.thickness * 0.5f;

                    // First triangle
                    vertices.insert(vertices.end(), {cmd.x1 + nx, cmd.y1 + ny, r, g, b, a});
                    vertices.insert(vertices.end(), {cmd.x1 - nx, cmd.y1 - ny, r, g, b, a});
                    vertices.insert(vertices.end(), {cmd.x2 + nx, cmd.y2 + ny, r, g, b, a});
                    // Second triangle
                    vertices.insert(vertices.end(), {cmd.x2 + nx, cmd.y2 + ny, r, g, b, a});
                    vertices.insert(vertices.end(), {cmd.x1 - nx, cmd.y1 - ny, r, g, b, a});
                    vertices.insert(vertices.end(), {cmd.x2 - nx, cmd.y2 - ny, r, g, b, a});
                }
            }
            // Skip text commands - they don't glow separately
        }

        if (vertices.empty()) return;

        glUseProgram(m_lineShader);
        glUniform2f(glGetUniformLocation(m_lineShader, "screenSize"),
                    (float)m_windowWidth, (float)m_windowHeight);

        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vertices.size() / 6));
        glBindVertexArray(0);
    };

    // Helper lambda to draw waveform using raw OpenGL
    auto drawWaveformRaw = [&]() {
        if (m_waveformDrawCmds.empty() || !m_lineShader) return;

        std::vector<float> vertices;
        vertices.reserve(m_waveformDrawCmds.size() * 36);

        for (const auto& cmd : m_waveformDrawCmds) {
            // Extract color components
            float r = ((cmd.color >> 0) & 0xFF) / 255.0f;
            float g = ((cmd.color >> 8) & 0xFF) / 255.0f;
            float b = ((cmd.color >> 16) & 0xFF) / 255.0f;
            float a = ((cmd.color >> 24) & 0xFF) / 255.0f;

            if (cmd.isRect) {
                // Draw filled rect as 2 triangles
                float x1 = cmd.x1, y1 = cmd.y1;
                float x2 = cmd.rectX2, y2 = cmd.rectY2;

                // First triangle
                vertices.insert(vertices.end(), {x1, y1, r, g, b, a});
                vertices.insert(vertices.end(), {x2, y1, r, g, b, a});
                vertices.insert(vertices.end(), {x2, y2, r, g, b, a});
                // Second triangle
                vertices.insert(vertices.end(), {x1, y1, r, g, b, a});
                vertices.insert(vertices.end(), {x2, y2, r, g, b, a});
                vertices.insert(vertices.end(), {x1, y2, r, g, b, a});
            } else {
                // Draw vertical line as thin quad (1 pixel wide)
                float x = cmd.x1;
                float y1 = cmd.y1, y2 = cmd.y2;
                float hw = 0.5f;  // half width

                // First triangle
                vertices.insert(vertices.end(), {x - hw, y1, r, g, b, a});
                vertices.insert(vertices.end(), {x + hw, y1, r, g, b, a});
                vertices.insert(vertices.end(), {x + hw, y2, r, g, b, a});
                // Second triangle
                vertices.insert(vertices.end(), {x - hw, y1, r, g, b, a});
                vertices.insert(vertices.end(), {x + hw, y2, r, g, b, a});
                vertices.insert(vertices.end(), {x - hw, y2, r, g, b, a});
            }
        }

        if (vertices.empty()) return;

        glUseProgram(m_lineShader);
        glUniform2f(glGetUniformLocation(m_lineShader, "screenSize"),
                    (float)m_windowWidth, (float)m_windowHeight);

        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vertices.size() / 6));
        glBindVertexArray(0);
    };

    // ===== LAYER 1: Full scene blur (for text) =====
    // Always blur the full scene - composite shader will subtract lines/waveform
    blurTexture(m_sceneTexture, m_textBlurTexture);

    // ===== LAYER 2: LINES (markers and playhead) =====
    // Always run the clear + blur; skip only the actual draw when there are
    // no line commands. Otherwise the blur target keeps the previous frame's
    // markers and they ghost through the composite pass forever.
    if (m_bloomLinesIntensity > 0.001f) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_linesFBO);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!m_lineDrawCmds.empty()) drawLinesRaw();

        glClearColor(bgR, bgG, bgB, 1.0f);
        blurTexture(m_linesTexture, m_linesBlurTexture);
    }

    // ===== LAYER 3: WAVEFORM =====
    // Same story — deleting the last track cleared the draw commands but
    // left the last blurred waveform in m_uiBlurTexture, so it kept
    // showing through as a ghost. Blur an empty FBO to zero it out.
    if (m_bloomUIIntensity > 0.001f) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_waveformFBO);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!m_waveformDrawCmds.empty()) drawWaveformRaw();

        glClearColor(bgR, bgG, bgB, 1.0f);
        blurTexture(m_waveformTexture, m_uiBlurTexture);
    }

    // ===== Final composite =====
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glBindVertexArray(m_quadVAO);

    glUseProgram(m_compositeShader);

    // Original scene
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTexture);
    glUniform1i(glGetUniformLocation(m_compositeShader, "scene"), 0);

    // Blurred layers
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textBlurTexture);
    glUniform1i(glGetUniformLocation(m_compositeShader, "sceneBlur"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_linesBlurTexture);
    glUniform1i(glGetUniformLocation(m_compositeShader, "linesBlur"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_uiBlurTexture);
    glUniform1i(glGetUniformLocation(m_compositeShader, "waveformBlur"), 3);

    // Intensities
    glUniform1f(glGetUniformLocation(m_compositeShader, "textIntensity"), m_bloomTextIntensity * 2.0f);
    glUniform1f(glGetUniformLocation(m_compositeShader, "linesIntensity"), m_bloomLinesIntensity * 2.0f);
    glUniform1f(glGetUniformLocation(m_compositeShader, "waveformIntensity"), m_bloomUIIntensity * 2.0f);

    // Background color
    glUniform3f(glGetUniformLocation(m_compositeShader, "bgColor"), bgR, bgG, bgB);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glUseProgram(0);
}

void GUIManager::render() {
    // Render is called from processFrame
}

void GUIManager::drawWaveformLayer() {
#ifdef IMGUI_FOUND
    if (m_waveformDrawCmds.empty()) return;

    // Get foreground draw list to draw on top of everything
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    for (const auto& cmd : m_waveformDrawCmds) {
        if (cmd.isRect) {
            drawList->AddRectFilled(
                ImVec2(cmd.x1, cmd.y1),
                ImVec2(cmd.rectX2, cmd.rectY2),
                cmd.color);
        } else {
            drawList->AddLine(
                ImVec2(cmd.x1, cmd.y1),
                ImVec2(cmd.x2, cmd.y2),
                cmd.color);
        }
    }
#endif
}

void GUIManager::drawLinesLayer() {
#ifdef IMGUI_FOUND
    if (m_lineDrawCmds.empty()) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    for (const auto& cmd : m_lineDrawCmds) {
        if (cmd.isTriangle) {
            drawList->AddTriangleFilled(
                ImVec2(cmd.tx1, cmd.ty1),
                ImVec2(cmd.tx2, cmd.ty2),
                ImVec2(cmd.tx3, cmd.ty3),
                cmd.color);
        } else if (cmd.isText) {
            drawList->AddText(ImVec2(cmd.textX, cmd.textY), cmd.color, cmd.text);
        } else {
            drawList->AddLine(
                ImVec2(cmd.x1, cmd.y1),
                ImVec2(cmd.x2, cmd.y2),
                cmd.color, cmd.thickness);
        }
    }
#endif
}

// Native HWND for the SDL window — used as the owner for Win32
// file-dialogs so they stay in front of the DAW and don't cause it to
// drop behind. Returns NULL if SDL can't provide it (headless / non-
// Windows), which falls back to an ownerless dialog.
#ifdef _WIN32
static HWND sdlWindowHwnd(SDL_Window* window) {
    if (!window) return nullptr;
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) return nullptr;
    return wmInfo.info.win.window;
}


// Scope guard: while a common-dialog is open, drop the SDL window out
// of fullscreen (Windows misbehaves badly when a modal dialog stacks
// over a fullscreen-desktop window — the app can end up hidden). On
// destruction, restores fullscreen if we had it.
class DialogFullscreenGuard {
public:
    DialogFullscreenGuard(SDL_Window* w) : m_win(w), m_wasFullscreen(false) {
        if (!m_win) return;
        Uint32 flags = SDL_GetWindowFlags(m_win);
        if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
            m_wasFullscreen = true;
            SDL_SetWindowFullscreen(m_win, 0);
        }
    }
    ~DialogFullscreenGuard() {
        if (m_wasFullscreen && m_win) {
            SDL_SetWindowFullscreen(m_win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
    }
    DialogFullscreenGuard(const DialogFullscreenGuard&) = delete;
    DialogFullscreenGuard& operator=(const DialogFullscreenGuard&) = delete;
private:
    SDL_Window* m_win;
    bool        m_wasFullscreen;
};
#endif

void GUIManager::processFrame() {
#ifdef SDL2_FOUND
#ifdef IMGUI_FOUND
    // Two paths post a jump request via AudioEngine:
    //   * Pad-13 + E1 bookmark nav → anchor is -1 → "recentre so the
    //     target frame sits 15% from the left" (only if off-screen).
    //   * Loop/punch double-tap → anchor is the OLD playhead frame →
    //     pan the view by (target - anchor) so the marker lands under
    //     wherever the play line was drawn. Zoom unchanged in both.
    if (m_audioEngine) {
        int64_t anchorFrame = m_audioEngine->consumeRequestedJumpAnchorFrame();
        int64_t jumpFrame   = m_audioEngine->consumeRequestedJumpFrame();
        if (jumpFrame >= 0 && m_timelineFrames > 0) {
            float zoom = m_displayZoom > 0.0f ? m_displayZoom : 1.0f;
            size_t vis = (size_t)((double)m_timelineFrames / (double)zoom);
            if (vis < 100) vis = 100;

            if (anchorFrame >= 0) {
                // Anchor-pin path: shift viewCenter by exactly the
                // playhead delta so the play line stays at the same
                // screen X while the underlying frame changes.
                long long delta = jumpFrame - anchorFrame;
                long long newCenter = (long long)m_viewCenterPosition + delta;
                // Clamp so we don't overshoot the ends of the timeline.
                long long minCenter = (long long)(vis / 2);
                long long maxCenter = (long long)m_timelineFrames - (long long)(vis / 2);
                if (maxCenter < minCenter) maxCenter = minCenter;
                if (newCenter < minCenter) newCenter = minCenter;
                if (newCenter > maxCenter) newCenter = maxCenter;
                m_viewCenterPosition = (size_t)newCenter;
                dawLog("GUI jump: anchor=%lld target=%lld delta=%+lld → viewCenter=%zu",
                       (long long)anchorFrame, (long long)jumpFrame,
                       delta, m_viewCenterPosition);
            } else {
                // Bookmark-nav path: 15%-from-left if the target is
                // currently off-screen; otherwise leave the view alone.
                size_t curStart = 0;
                if (m_viewCenterPosition > vis / 2)
                    curStart = m_viewCenterPosition - vis / 2;
                if (curStart + vis > m_timelineFrames)
                    curStart = (m_timelineFrames > vis) ? m_timelineFrames - vis : 0;
                size_t curEnd = curStart + vis;
                bool onScreen = ((size_t)jumpFrame >= curStart &&
                                 (size_t)jumpFrame <  curEnd);
                if (onScreen) {
                    dawLog("GUI jump: frame=%lld already on-screen (view=[%zu..%zu]) — no snap",
                           (long long)jumpFrame, curStart, curEnd);
                } else {
                    long long viewStart = (long long)jumpFrame
                                        - (long long)((double)vis * 0.15);
                    if (viewStart < 0) viewStart = 0;
                    if ((size_t)viewStart + vis > m_timelineFrames)
                        viewStart = (m_timelineFrames > vis)
                                  ? (long long)(m_timelineFrames - vis) : 0;
                    m_viewCenterPosition = (size_t)viewStart + vis / 2;
                    dawLog("GUI jump: frame=%lld off-screen → viewCenter=%zu (vis=%zu)",
                           (long long)jumpFrame, m_viewCenterPosition, vis);
                }
            }
        }
    }

    // Handle SDL events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) {
            m_running = false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
            m_running = false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
            int w = event.window.data1;
            int h = event.window.data2;
            // Minimize fires a 0×0 resize on Windows. Recreating bloom
            // FBOs at 0×0 corrupts the GL state and the app never
            // recovers on restore. Ignore any non-positive size — we'll
            // get another RESIZED event with real dimensions when the
            // window is restored.
            if (w > 0 && h > 0) {
                m_windowWidth  = w;
                m_windowHeight = h;
                cleanupBloom();
                initBloom();
            }
        }
    }

    // While the window is minimized, skip the entire render + swap.
    // SDL_GL_SwapWindow on a fullscreen minimized window can stall for
    // seconds waiting for the DWM composition path to become available
    // again, which is what caused the ~10 s black screen after Win+D.
    // Still pump SDL events so the RESTORED event lands, and yield
    // briefly to the OS so we don't burn a core.
    Uint32 winFlags = SDL_GetWindowFlags(m_window);
    if (winFlags & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(16);
        return;
    }

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Clear draw command vectors for multi-pass rendering
    m_waveformDrawCmds.clear();
    m_lineDrawCmds.clear();

    // Handle keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();

    // Spacebar to toggle play/stop
    static bool spaceWasPressed = false;
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    bool spaceIsPressed = keystate[SDL_SCANCODE_SPACE] != 0;
    if (spaceIsPressed && !spaceWasPressed && !io.WantTextInput) {
        if (m_audioEngine->isPlaying()) {
            m_audioEngine->stop();
        } else {
            m_audioEngine->play();
        }
    }
    spaceWasPressed = spaceIsPressed;

    // 'A' key: open a WAV file dialog and load into the selected track.
    // Ctrl+A: clear the audio from the selected track (strip stays).
    // Uses ImGui's event-based IsKeyPressed (not the poll-based SDL keystate)
    // so a quick tap is caught even when the frame rate is low.
    if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !io.WantTextInput && io.KeyCtrl) {
        int idx = m_audioEngine->getSelectedTrack();
        if (idx >= 0 && idx < m_audioEngine->getTrackCount()) {
            Track* t = m_audioEngine->getTrack(idx);
            if (t && t->hasAudio()) {
                // Snapshot + attach the pre-clear audio so undo restores it.
                m_audioEngine->undoSnapshot();
                m_audioEngine->undoStashTrackAudio(idx);
            }
            m_audioEngine->clearTrackAudio(idx);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !io.WantTextInput && !io.KeyCtrl) {
#ifdef _WIN32
        // Drop out of fullscreen for the duration of the dialog —
        // otherwise Windows can hide the DAW behind it.
        DialogFullscreenGuard fsGuard(m_window);
        char filename[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = sdlWindowHwnd(m_window);
        ofn.lpstrFilter = "WAV files\0*.wav\0All files\0*.*\0";
        ofn.lpstrFile   = filename;
        ofn.nMaxFile    = sizeof(filename);
        ofn.lpstrTitle  = "Load WAV into selected track";
        ofn.lpstrInitialDir = m_lastAudioDir.empty() ? nullptr
                                                    : m_lastAudioDir.c_str();
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn)) {
            // Remember the folder so the next audio-load dialog opens
            // here, independent of the session dialog's history.
            const char* slash = strrchr(filename, '\\');
            if (slash) m_lastAudioDir.assign(filename, slash - filename);
            saveSettings();
            int idx = m_audioEngine->getSelectedTrack();
            if (idx < 0 || idx >= m_audioEngine->getTrackCount()) {
                idx = m_audioEngine->addTrack("");
                m_audioEngine->setSelectedTrack(idx);
            }
            // Snapshot + stash the pre-load audio so undo restores what
            // was there before this load (including empty).
            m_audioEngine->undoSnapshot();
            m_audioEngine->undoStashTrackAudio(idx);
            m_audioEngine->loadTrackAudio(idx, filename);
            retimeArrangement();
        }
#endif
    }

    // Zoom interpolation
    float targetZoom = m_audioEngine->getWaveformZoom();
    if (m_zoomSmoothing) {
        float zoomDiff = targetZoom - m_displayZoom;
        if (std::abs(zoomDiff) > 0.001f) {
            m_displayZoom += zoomDiff * 0.15f;
        } else {
            m_displayZoom = targetZoom;
        }
    } else {
        m_displayZoom = targetZoom;
    }

    // Create fullscreen ImGui window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("Minimal DAW", nullptr, window_flags);

    renderToolbar();

    // ==================== MAIN CONTENT AREA ====================
    float trackPanelWidth = 200.0f;
    float transportBarHeight = m_bloomEnabled ? 80.0f : 50.0f;
    float contentHeight = ImGui::GetContentRegionAvail().y - transportBarHeight - 10;

    renderTrackPanel(trackPanelWidth, contentHeight);
    ImGui::SameLine();
    renderWaveform(contentHeight);

    renderTransportBar();
    renderVelocityCurveEditor();
    renderTestPage();

    ImGui::End();
    ImGui::PopStyleVar(3);

    // Rendering
    ImGui::EndFrame();

    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);

    // Clear with background color
    if (m_colorScheme == 1) {
        glClearColor(m_customBgColor[0], m_customBgColor[1], m_customBgColor[2], 1.0f);
    } else {
        glClearColor(0.176f, 0.176f, 0.188f, 1.0f);
    }

    // Update window dimensions for bloom
    m_windowWidth = w;
    m_windowHeight = h;

    // Render to framebuffer if bloom enabled, otherwise direct to screen
    if (m_bloomEnabled && m_sceneFBO && glBindFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Run bloom post-processing pipeline
        renderBloom();
    } else {
        // No bloom - render directly
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    SDL_GL_SwapWindow(m_window);
#endif
#endif
}

// ==================== EXTRACTED RENDER METHODS ====================

void GUIManager::reportFrameStats(float total_ms, float midi_ms, float update_ms, float frame_ms) {
    // Also feed the overlay's stage panel so nothing else needs to change.
    reportStageTime("midi",   midi_ms);
    reportStageTime("update", update_ms);
    reportStageTime("frame",  frame_ms);

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_perfStartMs == 0) m_perfStartMs = nowMs;

    FrameSample& s = m_frameHistory[m_frameHistoryIdx];
    s.tMs    = nowMs - m_perfStartMs;
    s.total  = total_ms;
    s.midi   = midi_ms;
    s.update = update_ms;
    s.frame  = frame_ms;
    m_frameHistoryIdx = (m_frameHistoryIdx + 1) % kFrameHistoryLen;
    if (m_frameHistoryCount < kFrameHistoryLen) m_frameHistoryCount++;

    if (total_ms < SPIKE_LOG_THRESHOLD_MS) return;
    if (nowMs - m_lastSpikeDumpMs < SPIKE_LOG_MIN_GAP_MS) return;
    m_lastSpikeDumpMs = nowMs;

    // Dump the whole ring buffer to perf.log with a SPIKE marker on the
    // triggering frame. Append mode so successive spikes accumulate.
    std::ofstream f("c:\\0_CODE\\Dogma75\\perf.log", std::ios::app);
    if (!f.is_open()) return;
    f << "===== SPIKE " << total_ms << " ms at t=" << (nowMs - m_perfStartMs)
      << " ms (playing=" << (m_audioEngine && m_audioEngine->isPlaying() ? 1 : 0)
      << ", tracks="   << (m_audioEngine ? m_audioEngine->getTrackCount() : 0) << ")\n";
    f << "  t_ms     total    midi    update    frame\n";
    // Walk the ring buffer oldest -> newest.
    int start = (m_frameHistoryCount == kFrameHistoryLen) ? m_frameHistoryIdx : 0;
    for (int k = 0; k < m_frameHistoryCount; k++) {
        const FrameSample& r = m_frameHistory[(start + k) % kFrameHistoryLen];
        bool spike = (r.total >= SPIKE_LOG_THRESHOLD_MS);
        f << (spike ? " * " : "   ")
          << r.tMs << "  "
          << r.total << "  " << r.midi << "  " << r.update << "  " << r.frame << "\n";
    }
    f << "\n";
    f.close();
}

void GUIManager::reportStageTime(const char* stage, float ms) {
    // Find or create a slot. Only kStageStatCount labels supported; extras
    // silently drop, but the caller only ever passes four fixed names.
    for (int i = 0; i < kStageStatCount; i++) {
        if (m_stageStats[i].label == nullptr) {
            m_stageStats[i].label   = stage;
            m_stageStats[i].worstMs = ms;
            m_stageStats[i].lastMs  = ms;
            return;
        }
        if (m_stageStats[i].label == stage) {
            m_stageStats[i].lastMs = ms;
            if (ms > m_stageStats[i].worstMs) m_stageStats[i].worstMs = ms;
            return;
        }
    }
}

void GUIManager::renderToolbar() {
#ifdef IMGUI_FOUND
    ImGuiIO& io = ImGui::GetIO();

    // File menu — Open / Save / Revert / New in a single dropdown.
    // Uses a button + BeginPopup rather than the main menu bar so it
    // stays visually consistent with the other toolbar buttons.
    static bool sPendingNewSession = false;   // opens modal after popup closes
    if (ImGui::Button("File")) {
        ImGui::OpenPopup("FileMenu");
    }
    static std::string sPendingRecentOpen;   // loaded after the popup closes
    if (ImGui::BeginPopup("FileMenu")) {
        if (ImGui::MenuItem("Open session")) openSession();
        // Recent Projects — hover to pop the list out to the right.
        // Disabled (rather than hidden) when empty, so the entry doesn't
        // appear and disappear as the list fills up.
        if (m_recentSessions.empty()) ImGui::BeginDisabled();
        if (ImGui::BeginMenu("Recent Projects")) {
            for (size_t i = 0; i < m_recentSessions.size(); i++) {
                const std::string& full = m_recentSessions[i];
                size_t slash = full.find_last_of("/\\");
                std::string name = (slash == std::string::npos)
                                 ? full : full.substr(slash + 1);
                // Strip the .json extension — the folder path is in the
                // tooltip, so the menu can show just the project name.
                if (name.size() > 5 &&
                    name.compare(name.size() - 5, 5, ".json") == 0) {
                    name.resize(name.size() - 5);
                }
                // Number them so the keyboard/eye can land on one quickly,
                // and make the label unique for ImGui even if two sessions
                // in different folders share a filename.
                std::string label = std::to_string(i + 1) + "  " + name +
                                    "##recent" + std::to_string(i);

                bool missing = !fileExists(full);
                if (missing) ImGui::BeginDisabled();
                if (ImGui::MenuItem(label.c_str())) {
                    sPendingRecentOpen = full;
                }
                if (missing) ImGui::EndDisabled();
                // Full path on hover — the filename alone is ambiguous
                // once the same project name exists in two folders.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s%s", full.c_str(),
                                      missing ? "\n(file not found)" : "");
                }
            }
            ImGui::EndMenu();
        }
        if (m_recentSessions.empty()) ImGui::EndDisabled();
        if (ImGui::MenuItem("Save"))         saveSession();
        if (ImGui::MenuItem("Save As"))      saveSessionAs();
        // Revert enabled only when a session file is actually open.
        bool canRevert = !m_currentSessionPath.empty();
        if (!canRevert) ImGui::BeginDisabled();
        if (ImGui::MenuItem("Revert")) revertSession();
        if (!canRevert) ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("New session")) {
            // If there are unsaved changes, ask first. Otherwise just wipe.
            if (m_audioEngine && m_audioEngine->isSessionDirty()) {
                sPendingNewSession = true;
            } else {
                closeSession();
            }
        }
        ImGui::EndPopup();
    }
    // Deferred like sPendingNewSession below: loading a session rebuilds
    // tracks and view state, which is not safe to do from inside the popup
    // that is still being drawn this frame.
    if (!sPendingRecentOpen.empty()) {
        std::string path = sPendingRecentOpen;
        sPendingRecentOpen.clear();
        loadSessionFromFile(path);
        retimeArrangement();
    }
    // Popup must be opened *after* the menu popup has closed, otherwise
    // ImGui's popup stack refuses to nest a modal on top of a normal one.
    if (sPendingNewSession) {
        ImGui::OpenPopup("Unsaved changes");
        sPendingNewSession = false;
    }
    // The modal itself — Save / Don't Save / Cancel.
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current session has unsaved changes.");
        ImGui::TextUnformatted("Save before starting a new session?");
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            saveSession();
            // saveSession() calls clearSessionDirty on success; only wipe
            // if the user actually completed the save dialog.
            if (m_audioEngine && !m_audioEngine->isSessionDirty()) {
                closeSession();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
            closeSession();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();

    // Current session name — filename only, no path or .json extension.
    // "*" suffix when there are unsaved edits since the last save/load.
    // "(no session)" when nothing has been opened this run.
    {
        std::string label = "(no session)";
        if (!m_currentSessionPath.empty()) {
            size_t slash = m_currentSessionPath.find_last_of("/\\");
            label = (slash == std::string::npos)
                  ? m_currentSessionPath
                  : m_currentSessionPath.substr(slash + 1);
            size_t dot = label.find_last_of('.');
            if (dot != std::string::npos) label.resize(dot);
        }
        if (m_audioEngine && m_audioEngine->isSessionDirty()) label += " *";
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
    }

    // ASIO Device dropdown
    ImGui::Text("ASIO:");
    ImGui::SameLine();

    int asioDeviceCount = m_audioEngine->getAsioDeviceCount();
    int selectedAsioIndex = -1;
    int currentDeviceId = m_audioEngine->getCurrentDeviceId();
    for (int i = 0; i < asioDeviceCount; i++) {
        if (m_audioEngine->getAsioDeviceId(i) == currentDeviceId) {
            selectedAsioIndex = i;
            break;
        }
    }

    std::string currentAsioName = (selectedAsioIndex >= 0) ? m_audioEngine->getAsioDeviceName(selectedAsioIndex) : "Select ASIO Device";
    if (ImGui::BeginCombo("##AsioDevice", currentAsioName.c_str(), ImGuiComboFlags_WidthFitPreview)) {
        for (int i = 0; i < asioDeviceCount; i++) {
            std::string deviceName = m_audioEngine->getAsioDeviceName(i);
            bool isSelected = (selectedAsioIndex == i);
            if (ImGui::Selectable(deviceName.c_str(), isSelected)) {
                int deviceId = m_audioEngine->getAsioDeviceId(i);
                m_audioEngine->switchAudioDevice(deviceId);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Output stereo pair dropdown
    ImGui::Text("Output:");
    ImGui::SameLine();

    int numPairs = m_audioEngine->getNumStereoPairs();
    int currentPair = m_audioEngine->getOutputStereoPair();
    char pairLabel[32];
    sprintf(pairLabel, "Ch %d-%d", (currentPair * 2) + 1, (currentPair * 2) + 2);

    if (ImGui::BeginCombo("##OutputPair", pairLabel, ImGuiComboFlags_WidthFitPreview)) {
        for (int p = 0; p < numPairs; p++) {
            bool isSelected = (currentPair == p);
            char label[32];
            sprintf(label, "Ch %d-%d", (p * 2) + 1, (p * 2) + 2);
            if (ImGui::Selectable(label, isSelected)) {
                m_audioEngine->setOutputStereoPair(p);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // MIDI Input dropdown
    ImGui::Text("MIDI:");
    ImGui::SameLine();

    int midiPortCount = m_audioEngine->getMidiPortCount();
    int currentMidiPort = m_audioEngine->getCurrentMidiPort();
    std::string currentMidiName = (currentMidiPort >= 0 && currentMidiPort < midiPortCount)
        ? m_audioEngine->getMidiPortName(currentMidiPort)
        : "None";

    if (ImGui::BeginCombo("##MidiInput", currentMidiName.c_str(), ImGuiComboFlags_WidthFitPreview)) {
        bool noneSelected = (currentMidiPort < 0);
        if (ImGui::Selectable("None", noneSelected)) {
            m_audioEngine->setMidiPort(-1);
        }
        if (noneSelected) {
            ImGui::SetItemDefaultFocus();
        }

        for (int i = 0; i < midiPortCount; i++) {
            std::string portName = m_audioEngine->getMidiPortName(i);
            bool isSelected = (currentMidiPort == i);
            if (ImGui::Selectable(portName.c_str(), isSelected)) {
                m_audioEngine->setMidiPort(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Audio status indicator
    if (m_audioEngine && m_audioEngine->isRunning()) {
        ImVec4 statusColor = (m_colorScheme == 1)
            ? ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f)
            : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        ImGui::TextColored(statusColor, "%.0fHz", m_audioEngine->getSampleRate());
    } else {
        ImVec4 statusColor = (m_colorScheme == 1)
            ? ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f)
            : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImGui::TextColored(statusColor, "STOPPED");
    }
    ImGui::SameLine();

    // Test Page button
    ImGui::SameLine();
    if (m_showTestPage) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.7f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
    if (ImGui::Button("Test")) {
        m_showTestPage = !m_showTestPage;
    }
    if (m_showTestPage) {
        ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hardware test page - shows all encoders and touchpads");
    }

    // One-shot: push the persisted brightness values to the firmware on the
    // first toolbar render after loadSettings, so the LEDs come up at the
    // user's tuned levels without a slider drag.
    if (m_ledBrightnessNeedsPush && m_serialController) {
        for (int ch = 0; ch < 9; ch++) {
            uint16_t pca = (uint16_t)((1.0f - m_ledBrightness[ch]) * 4095.0f);
            char buf[32];
            snprintf(buf, sizeof(buf), "LEDBRT:%d:%d", ch, (int)pca);
            m_serialController->sendMessage(buf);
        }
        m_ledBrightnessNeedsPush = false;
    }

    // LED brightness popup — one slider per channel + an "identify" button
    // beside each so the user can locate which physical LED is which. The
    // popup is anchored to the "LED" toolbar button; toggle it with a click.
    ImGui::SameLine();
    if (ImGui::Button("LED")) {
        ImGui::OpenPopup("LEDPopup");
    }
    // All-ON / All-OFF mode belongs to the popup — kept out here so we
    // can cancel it the moment the popup closes.
    // 0 = none, 1 = all on, 2 = all off
    static int sAllMode = 0;
    auto pushAllForce = [this](const char* verb, int active) {
        if (!m_serialController) return;
        for (int ch = 0; ch < 9; ch++) {
            char buf[40];
            snprintf(buf, sizeof(buf), "LEDFORCE%s:%d:%d", verb, ch, active);
            m_serialController->sendMessage(buf);
        }
    };

    if (ImGui::BeginPopup("LEDPopup")) {
        // "All ON" / "All OFF" — mutually-exclusive toggle buttons that
        // hold every LED steady on / off for the physical wiring check.
        // Click one to activate, click again to deactivate. Clicking the
        // other flips the state and cancels the first.
        {
            auto pushBtnColor = [](ImVec4 c) {
                ImGui::PushStyleColor(ImGuiCol_Button,        c);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c);
            };

            if (sAllMode == 1) pushBtnColor(ImVec4(0.00f, 0.60f, 0.25f, 1.0f));
            if (ImGui::Button("All ON", ImVec2(90, 0))) {
                if (sAllMode == 1) {
                    // Deactivate.
                    pushAllForce("ON", 0);
                    sAllMode = 0;
                } else {
                    // Cancel other, activate this.
                    if (sAllMode == 2) pushAllForce("OFF", 0);
                    pushAllForce("ON", 1);
                    sAllMode = 1;
                }
            }
            if (sAllMode == 1) ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (sAllMode == 2) pushBtnColor(ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
            if (ImGui::Button("All OFF", ImVec2(90, 0))) {
                if (sAllMode == 2) {
                    pushAllForce("OFF", 0);
                    sAllMode = 0;
                } else {
                    if (sAllMode == 1) pushAllForce("ON", 0);
                    pushAllForce("OFF", 1);
                    sAllMode = 2;
                }
            }
            if (sAllMode == 2) ImGui::PopStyleColor(3);

            ImGui::Separator();
        }

        const char* ledNames[9] = {
            "0  mute",    "1  solo",     "2  arm",
            "3  play",    "4  loop L",   "5  rec L",
            "6  rec R",   "7  loop R",   "8  orange"
        };
        for (int ch = 0; ch < 9; ch++) {
            ImGui::PushID(ch);
            ImGui::TextUnformatted(ledNames[ch]);
            ImGui::SameLine(90);
            ImGui::SetNextItemWidth(320);   // roughly 2× the original width
            if (ImGui::SliderFloat("##bright", &m_ledBrightness[ch], 0.0f, 1.0f, "%.2f")) {
                uint16_t pca = (uint16_t)((1.0f - m_ledBrightness[ch]) * 4095.0f);
                char buf[32];
                snprintf(buf, sizeof(buf), "LEDBRT:%d:%d", ch, (int)pca);
                if (m_serialController) m_serialController->sendMessage(buf);
            }
            // Save whenever the drag ends so a graceful shutdown isn't
            // required to persist the tune.
            if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();

            ImGui::SameLine();
            // Identify: while the button is held down, ask the firmware to
            // flash this channel. Release ends identify and restores state.
            ImGui::Button("id");
            bool active = ImGui::IsItemActive();
            if (active && m_ledIdentifyChannel != ch) {
                // Turn off any previous identify first.
                if (m_ledIdentifyChannel >= 0 && m_serialController) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "LEDID:%d:0", m_ledIdentifyChannel);
                    m_serialController->sendMessage(buf);
                }
                m_ledIdentifyChannel = ch;
                if (m_serialController) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "LEDID:%d:1", ch);
                    m_serialController->sendMessage(buf);
                }
            } else if (!active && m_ledIdentifyChannel == ch) {
                m_ledIdentifyChannel = -1;
                if (m_serialController) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "LEDID:%d:0", ch);
                    m_serialController->sendMessage(buf);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    } else {
        // Popup was closed — cancel any diagnostic overrides so the
        // controller LEDs snap back to reflecting real application state.
        if (m_ledIdentifyChannel >= 0) {
            if (m_serialController) {
                char buf[32];
                snprintf(buf, sizeof(buf), "LEDID:%d:0", m_ledIdentifyChannel);
                m_serialController->sendMessage(buf);
            }
            m_ledIdentifyChannel = -1;
        }
        if (sAllMode == 1) {
            pushAllForce("ON", 0);
            sAllMode = 0;
        } else if (sAllMode == 2) {
            pushAllForce("OFF", 0);
            sAllMode = 0;
        }
    }

    // Fullscreen/Close buttons (right-aligned)
    ImGui::SameLine();
    float rightButtonsX = io.DisplaySize.x - 80;
    ImGui::SetCursorPosX(rightButtonsX);

    const char* fsLabel = m_isFullscreen ? "[]" : "[#]";
    const char* fsTooltip = m_isFullscreen ? "Switch to windowed mode" : "Switch to fullscreen";
    if (ImGui::Button(fsLabel, ImVec2(30, 0))) {
        m_isFullscreen = !m_isFullscreen;
        if (m_isFullscreen) {
            SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else {
            SDL_SetWindowFullscreen(m_window, 0);
            SDL_MaximizeWindow(m_window);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", fsTooltip);
    }

    ImGui::SameLine();

    if (ImGui::Button("X", ImVec2(30, 0))) {
        m_running = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Close application");
    }

    ImGui::Separator();
#endif
}

void GUIManager::renderTrackPanel(float width, float height) {
#ifdef IMGUI_FOUND
    ImGui::BeginChild("TrackPanel", ImVec2(width, height), true);

    // Panel-view dropdown at the top of the panel.
    static const char* modeNames[] = { "TRACKS", "SCRUBBING", "WAVEFORM" };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##LeftPanelMode", modeNames[m_leftPanelMode])) {
        for (int i = 0; i < IM_ARRAYSIZE(modeNames); i++) {
            bool sel = (m_leftPanelMode == i);
            if (ImGui::Selectable(modeNames[i], sel)) m_leftPanelMode = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    // 15 px gap so this separator lines up with the arrangement's
    // top separator (which sits 15 px below its time-ruler baseline).
    ImGui::Dummy(ImVec2(1.0f, 15.0f));
    ImGui::Separator();

    // -------- SCRUBBING view --------
    if (m_leftPanelMode == 1) {
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));

        // Inline lambda copied from the track view — needed for slider glow.
        auto storeSliderGrab = [this](float value, float minVal, float maxVal) {
            ImVec2 sliderMin = ImGui::GetItemRectMin();
            ImVec2 sliderMax = ImGui::GetItemRectMax();
            float t = (value - minVal) / (maxVal - minVal);
            float grabWidth = 8.0f;
            float grabX = sliderMin.x + t * (sliderMax.x - sliderMin.x - grabWidth);
            WaveformDrawCmd cmd;
            cmd.x1 = grabX; cmd.y1 = sliderMin.y;
            cmd.isRect = true;
            cmd.rectX2 = grabX + grabWidth; cmd.rectY2 = sliderMax.y;
            ImU32 col;
            if (m_colorScheme == 1) {
                col = IM_COL32((int)(m_customTextColor[0] * 255),
                               (int)(m_customTextColor[1] * 255),
                               (int)(m_customTextColor[2] * 255), 255);
            } else {
                col = IM_COL32(51, 102, 204, 255);
            }
            cmd.color = col;
            m_waveformDrawCmds.push_back(cmd);
        };

        ImGui::Text("SILENT SCRUB (E1)");
        ImGui::Separator();
        float silentScrubSpeed = m_audioEngine->getSilentScrubSpeed();
        ImGui::Text("Scrub Speed");
        if (ImGui::SliderFloat("##SilentScrubSpeed", &silentScrubSpeed, 0.1f, 4.0f, "%.2f")) {
            m_audioEngine->setSilentScrubSpeed(silentScrubSpeed);
        }
        storeSliderGrab(silentScrubSpeed, 0.1f, 4.0f);

        ImGui::Spacing();
        ImGui::Text("AUDIO SCRUB (Pad 24 + E6)");
        ImGui::Separator();

        float scrubSpeed = m_audioEngine->getScrubSpeed();
        ImGui::Text("Scrub Speed");
        if (ImGui::SliderFloat("##ScrubSpeed", &scrubSpeed, 0.1f, 4.0f, "%.2f")) {
            m_audioEngine->setScrubSpeed(scrubSpeed);
        }
        storeSliderGrab(scrubSpeed, 0.1f, 4.0f);

        float rpmThreshold = m_audioEngine->getScrubRpmThreshold();
        ImGui::Text("RPM Threshold");
        if (ImGui::SliderFloat("##RpmThreshold", &rpmThreshold, 5.0f, 100.0f, "%.0f RPM")) {
            m_audioEngine->setScrubRpmThreshold(rpmThreshold);
        }
        storeSliderGrab(rpmThreshold, 5.0f, 100.0f);

        float fastMult = m_audioEngine->getFastSpeedMultiplier();
        ImGui::Text("Fast Speed");
        if (ImGui::SliderFloat("##FastSpeed", &fastMult, 1.0f, 20.0f, "%.1fx")) {
            m_audioEngine->setFastSpeedMultiplier(fastMult);
        }
        storeSliderGrab(fastMult, 1.0f, 20.0f);

        float rpmAveraging = m_audioEngine->getRpmAveraging();
        ImGui::Text("RPM Smoothing");
        if (ImGui::SliderFloat("##RpmAveraging", &rpmAveraging, 0.0f, 0.99f, "%.2f")) {
            m_audioEngine->setRpmAveraging(rpmAveraging);
        }
        storeSliderGrab(rpmAveraging, 0.0f, 0.99f);

        ImGui::PopStyleColor(2);

        float currentRpm = m_audioEngine->getCurrentEncoderRpm();
        int displayRpm = (int)currentRpm;
        if (displayRpm > 999) displayRpm = 999;
        ImVec4 rpmColor;
        if (m_colorScheme == 1) {
            rpmColor = ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f);
        } else {
            rpmColor = (currentRpm >= rpmThreshold)
                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                : ImVec4(0.6f, 0.6f, 1.0f, 1.0f);
        }
        const char* zoneText = (currentRpm >= rpmThreshold) ? "FAST" : "SLOW";
        ImGui::TextColored(rpmColor, "Knob 1: %03d RPM (%s)", displayRpm, zoneText);

        ImGui::EndChild();
        return;
    }

    // -------- WAVEFORM view --------
    if (m_leftPanelMode == 2) {
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));

        auto storeSliderGrab = [this](float value, float minVal, float maxVal) {
            ImVec2 sliderMin = ImGui::GetItemRectMin();
            ImVec2 sliderMax = ImGui::GetItemRectMax();
            float t = (value - minVal) / (maxVal - minVal);
            float grabWidth = 8.0f;
            float grabX = sliderMin.x + t * (sliderMax.x - sliderMin.x - grabWidth);
            WaveformDrawCmd cmd;
            cmd.x1 = grabX; cmd.y1 = sliderMin.y;
            cmd.isRect = true;
            cmd.rectX2 = grabX + grabWidth; cmd.rectY2 = sliderMax.y;
            ImU32 col;
            if (m_colorScheme == 1) {
                col = IM_COL32((int)(m_customTextColor[0] * 255),
                               (int)(m_customTextColor[1] * 255),
                               (int)(m_customTextColor[2] * 255), 255);
            } else {
                col = IM_COL32(51, 102, 204, 255);
            }
            cmd.color = col;
            m_waveformDrawCmds.push_back(cmd);
        };

        ImGui::Text("Vertical Zoom");
        ImGui::SliderFloat("##VertZoom", &m_waveformVerticalZoom, 0.5f, 4.0f, "%.1fx");
        storeSliderGrab(m_waveformVerticalZoom, 0.5f, 4.0f);

        ImGui::Text("Track Height");
        ImGui::SliderFloat("##TrackHeight", &m_trackHeight, 40.0f, 300.0f, "%.0f");
        storeSliderGrab(m_trackHeight, 40.0f, 300.0f);

        ImGui::Spacing();
        ImGui::Text("View");
        ImGui::Separator();
        if (ImGui::Button(m_simplifiedWaveform ? "Detailed" : "Simplified", ImVec2(-1, 0))) {
            m_simplifiedWaveform = !m_simplifiedWaveform;
        }
        ImGui::Checkbox("Smooth Zoom", &m_zoomSmoothing);
        if (ImGui::Checkbox("Scroll Waveform", &m_waveformScrolling)) {
            if (!m_waveformScrolling) {
                m_viewCenterPosition = m_audioEngine->getPlaybackPosition();
            }
        }
        ImGui::BeginDisabled(m_waveformScrolling);
        ImGui::Checkbox("Auto-Page", &m_waveformAutoPage);
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Text("Glow");
        ImGui::Separator();
        ImGui::Checkbox("Enable Glow", &m_bloomEnabled);
        if (m_bloomEnabled) {
            ImGui::Text("Glow Text");
            ImGui::SliderFloat("##bloomText", &m_bloomTextIntensity, 0.0f, 1.0f, "%.2f");
            storeSliderGrab(m_bloomTextIntensity, 0.0f, 1.0f);
            ImGui::Text("Glow Lines");
            ImGui::SliderFloat("##bloomLines", &m_bloomLinesIntensity, 0.0f, 1.0f, "%.2f");
            storeSliderGrab(m_bloomLinesIntensity, 0.0f, 1.0f);
            ImGui::Text("Glow UI");
            ImGui::SliderFloat("##bloomUI", &m_bloomUIIntensity, 0.0f, 1.0f, "%.2f");
            storeSliderGrab(m_bloomUIIntensity, 0.0f, 1.0f);
        }

        ImGui::PopStyleColor(2);

        ImGui::EndChild();
        return;
    }

    // -------- TRACKS view (default) --------
    // Cubase/Ableton-style track headers: each track is its own inline block
    // with name + volume/pan sliders + M/S/R buttons + output dropdown,
    // separated by a horizontal rule. Click the name to select the track;
    // double-click to rename inline.
    int selectedTrack = m_audioEngine->getSelectedTrack();
    int trackCount = m_audioEngine->getTrackCount();
    if (trackCount == 0) {
        ImVec4 hintColor = (m_colorScheme == 1)
            ? ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f)
            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(hintColor, "No tracks");
        ImGui::TextColored(hintColor, "Click '+ Add Track'");
    } else {
        static int editingTrackName = -1;
        static char nameBuffer[128] = "";
        int numPairs = m_audioEngine->getNumStereoPairs();

        // Per-track slot height: auto-fit up to FIT_LIMIT tracks (each
        // gets 1/N of the available space), then LOCK at 1/FIT_LIMIT and
        // let the outer BeginChild's scrollbar handle track FIT_LIMIT+1
        // onward. Both this panel AND the arrangement view use the
        // identical formula so the horizontal separators between tracks
        // line up perfectly regardless of track count.
        static constexpr int    FIT_LIMIT    = 5;
        static constexpr float  MIN_TRACK_H  = 60.0f;
        float availableHeight = ImGui::GetContentRegionAvail().y;
        float separatorH = ImGui::GetTextLineHeightWithSpacing() * 0.5f;
        int   divisor    = (trackCount < FIT_LIMIT) ? trackCount : FIT_LIMIT;
        if (divisor < 1) divisor = 1;   // avoid div-by-zero when trackCount==0
        float perTrackH  = availableHeight / (float)divisor;
        if (perTrackH < MIN_TRACK_H) perTrackH = MIN_TRACK_H;
        float perTrackBlockH = perTrackH - separatorH;

        // Outer scrollable container. Apply the shared scroll offset from
        // the last frame so the two panels start in sync; we'll capture
        // any wheel-scroll that happens here back into it at the end.
        ImGui::BeginChild("trackListScroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollY(m_trackScrollY);

        for (int i = 0; i < trackCount; i++) {
            Track* track = m_audioEngine->getTrack(i);
            if (!track) continue;
            bool isSelected = (selectedTrack == i);

            ImGui::PushID(i);

            // Blue for the currently-selected track, white/gray otherwise.
            // Applies to text, slider grabs, slider/combo frames, buttons,
            // AND the horizontal Separator drawn after the block so the
            // whole row (including its bottom divider) reads as one colour.
            ImVec4 tint = isSelected
                ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f)   // selected = white
                : ImVec4(0.30f, 0.55f, 0.80f, 1.0f);  // unselected = blue
            ImVec4 tintDim = ImVec4(tint.x * 0.35f, tint.y * 0.35f, tint.z * 0.35f, 1.0f);
            ImVec4 tintHi  = ImVec4(tint.x * 1.15f, tint.y * 1.15f, tint.z * 1.15f, 1.0f);
            if (tintHi.x > 1.0f) tintHi.x = 1.0f;
            if (tintHi.y > 1.0f) tintHi.y = 1.0f;
            if (tintHi.z > 1.0f) tintHi.z = 1.0f;
            ImGui::PushStyleColor(ImGuiCol_Text,            tint);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,         tintDim);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  ImVec4(tintDim.x * 1.3f, tintDim.y * 1.3f, tintDim.z * 1.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,   ImVec4(tintDim.x * 1.6f, tintDim.y * 1.6f, tintDim.z * 1.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,      tint);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,tintHi);
            ImGui::PushStyleColor(ImGuiCol_Button,          tintDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,   ImVec4(tintDim.x * 1.3f, tintDim.y * 1.3f, tintDim.z * 1.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,    ImVec4(tintDim.x * 1.6f, tintDim.y * 1.6f, tintDim.z * 1.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Separator,       tint);
            ImGui::PushStyleColor(ImGuiCol_Header,          tintDim);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,   ImVec4(tintDim.x * 1.3f, tintDim.y * 1.3f, tintDim.z * 1.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,    ImVec4(tintDim.x * 1.6f, tintDim.y * 1.6f, tintDim.z * 1.6f, 1.0f));
            const int kTrackStyleCount = 13;

            ImGui::BeginChild("trackBlock", ImVec2(-1, perTrackBlockH),
                              false, ImGuiWindowFlags_NoScrollbar);

            // --- Track name row ---
            if (editingTrackName == i) {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll)) {
                    m_audioEngine->undoSnapshot();
                    track->name = nameBuffer;
                    m_audioEngine->markSessionDirty();
                    editingTrackName = -1;
                }
                if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
                    editingTrackName = -1;
                }
                if (ImGui::IsItemVisible() && !ImGui::IsItemActive()) {
                    ImGui::SetKeyboardFocusHere(-1);
                }
            } else {
                // If this track is the one currently being renamed via the
                // hardware controller, show a live-typing preview: red
                // background fill, live text from the firmware buffer, and
                // a blinking cursor. Falls through to the normal name
                // display otherwise.
                std::string liveBuf;
                int liveCursor = 0;
                bool renaming = (m_audioEngine->getRenameTrackIndex() == i) &&
                                m_audioEngine->getRenameBuffer(liveBuf, liveCursor);
                if (renaming) {
                    // Fill pulses on the same triangle-wave phase as the
                    // physical pad-20 / pad-21 LEDs — we reconstruct that
                    // phase on the DAW side from the RENAMESYNC message.
                    float b = m_audioEngine->getLedFlashBrightness();  // 0..1
                    // Cursor character follows the same phase: shown while
                    // the LEDs are on the brighter half of the cycle.
                    bool cursorOn = (b > 0.5f);
                    // Build display: overlay a '_' at cursor pos when
                    // cursorOn; when off, show the underlying letter (or a
                    // space if cursor is past the end).
                    std::string disp = liveBuf;
                    int cp = liveCursor;
                    if (cp < 0) cp = 0;
                    if (cp > (int)disp.size()) disp.resize((size_t)cp, ' ');
                    if (cursorOn) {
                        char cursorChar = '_';
                        if (cp < (int)disp.size()) disp[(size_t)cp] = cursorChar;
                        else                       disp.push_back(cursorChar);
                    } else if (cp >= (int)disp.size()) {
                        disp.push_back(' ');
                    }
                    char selLbl[128];
                    snprintf(selLbl, sizeof(selLbl), "TR%d - %s",
                             i + 1, disp.c_str());
                    // Interpolate between dim and bright red by brightness.
                    ImVec4 redDim    = ImVec4(0.30f, 0.04f, 0.04f, 1.0f);
                    ImVec4 redBright = ImVec4(1.00f, 0.10f, 0.10f, 1.0f);
                    ImVec4 fill(
                        redDim.x + (redBright.x - redDim.x) * b,
                        redDim.y + (redBright.y - redDim.y) * b,
                        redDim.z + (redBright.z - redDim.z) * b,
                        1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Header,        fill);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, fill);
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  fill);
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1,1,1,1));
                    // Always-selected flag paints the row background using
                    // ImGuiCol_Header, which we've just made red.
                    ImGui::Selectable(selLbl, true, ImGuiSelectableFlags_Disabled);
                    ImGui::PopStyleColor(4);
                } else {
                    char selLbl[128];
                    snprintf(selLbl, sizeof(selLbl), "TR%d - %s", i + 1, track->name.c_str());
                    if (ImGui::Selectable(selLbl, isSelected)) {
                        m_audioEngine->setSelectedTrack(i);
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        editingTrackName = i;
                        strncpy(nameBuffer, track->name.c_str(), sizeof(nameBuffer) - 1);
                        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    }
                }
            }

            // --- Volume slider ---
            ImGui::Text("Vol");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##vol", &track->volume, 0.0f, 1.0f, "%.2f"))
                m_audioEngine->markSessionDirty();
            // Snapshot on drag start so a whole drag coalesces to one undo.
            if (ImGui::IsItemActivated()) m_audioEngine->undoSnapshot();
            // Double-click to reset to unity gain.
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                m_audioEngine->undoSnapshot();
                track->volume = 1.0f;
                m_audioEngine->markSessionDirty();
            }

            // --- Pan slider ---
            ImGui::Text("Pan");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##pan", &track->pan, -1.0f, 1.0f, "%.2f"))
                m_audioEngine->markSessionDirty();
            if (ImGui::IsItemActivated()) m_audioEngine->undoSnapshot();
            // Double-click to recentre.
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                m_audioEngine->undoSnapshot();
                track->pan = 0.0f;
                m_audioEngine->markSessionDirty();
            }

            // --- Mute / Solo / Record-arm buttons ---
            // Push all three button-state colours (Button + Hovered + Active)
            // so an active state (mute/solo/arm) reads the same colour no
            // matter whether the mouse is still hovering. Without this, the
            // ButtonHovered override from the row's tint would show through
            // until the pointer left the button.
            ImVec2 msrBtnSize(30, 22);
            auto pushBtnColor = [](ImVec4 c) {
                ImGui::PushStyleColor(ImGuiCol_Button,        c);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c);
            };
            // Mute — green when active (mirrors the controller's LED 0)
            if (track->muted) pushBtnColor(ImVec4(0.00f, 0.70f, 0.30f, 1.0f));
            if (ImGui::Button("M", msrBtnSize)) { m_audioEngine->undoSnapshot(); track->muted = !track->muted; m_audioEngine->markSessionDirty(); }
            if (track->muted) ImGui::PopStyleColor(3);
            ImGui::SameLine();
            // Solo — yellow when active (mirrors the controller's LED 1)
            if (track->solo) pushBtnColor(ImVec4(0.85f, 0.75f, 0.00f, 1.0f));
            if (ImGui::Button("S", msrBtnSize)) { m_audioEngine->undoSnapshot(); track->solo = !track->solo; m_audioEngine->markSessionDirty(); }
            if (track->solo) ImGui::PopStyleColor(3);
            ImGui::SameLine();
            // Record arm
            if (track->armed) pushBtnColor(ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
            if (ImGui::Button("R", msrBtnSize)) { m_audioEngine->undoSnapshot(); track->armed = !track->armed; m_audioEngine->markSessionDirty(); }
            if (track->armed) ImGui::PopStyleColor(3);
            ImGui::SameLine();
            // Input monitor — cyan/teal when active. Just a state toggle for
            // now; audio path wiring can come later.
            if (track->inputMonitor) pushBtnColor(ImVec4(0.10f, 0.65f, 0.85f, 1.0f));
            if (ImGui::Button("I", msrBtnSize)) {
                m_audioEngine->undoSnapshot();
                // setTrackInputMonitor pushes the mute state to the
                // Antelope mixer and updates LED 9 in one step.
                m_audioEngine->setTrackInputMonitor(i, !track->inputMonitor);
            }
            if (track->inputMonitor) ImGui::PopStyleColor(3);

            // --- Input & output pair dropdowns, side by side ---
            //   Display format: "i = 1&2"   "o = 1&2"
            // Input pair count comes from the device's advertised input
            // channels (may be 0 for output-only devices; dropdown just
            // hides in that case).
            int numInputPairs = m_audioEngine->getNumInputStereoPairs();
            float halfW = ImGui::GetContentRegionAvail().x * 0.5f - 4.0f;

            if (numInputPairs > 0) {
                // Stereo takes the pair index (e.g. "I = 1-2"); mono takes
                // an absolute channel index (e.g. "I = 1"). Whichever is
                // active drives the combo preview.
                char inLabel[32];
                if (track->inputMono) {
                    sprintf(inLabel, "I = %d", track->inputMonoChan + 1);
                } else {
                    sprintf(inLabel, "I = %d-%d", (track->inputPair * 2) + 1,
                                                  (track->inputPair * 2) + 2);
                }
                ImGui::SetNextItemWidth(halfW);
                if (ImGui::BeginCombo("##in", inLabel, ImGuiComboFlags_NoArrowButton)) {
                    // Stereo pairs first.
                    for (int p = 0; p < numInputPairs; p++) {
                        bool sel = (!track->inputMono && track->inputPair == p);
                        char item[32];
                        sprintf(item, "I = %d-%d", (p * 2) + 1, (p * 2) + 2);
                        if (ImGui::Selectable(item, sel)) {
                            m_audioEngine->undoSnapshot();
                            track->inputMono = false;
                            track->inputPair = p;
                            m_audioEngine->markSessionDirty();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    // Then mono channels, separated so it's clear these are
                    // single-channel takes.
                    int numMono = m_audioEngine->getNumInputStereoPairs() * 2;
                    if (numMono > 0) {
                        ImGui::Separator();
                        for (int c = 0; c < numMono; c++) {
                            bool sel = (track->inputMono && track->inputMonoChan == c);
                            char item[32];
                            sprintf(item, "I = %d", c + 1);
                            if (ImGui::Selectable(item, sel)) {
                                m_audioEngine->undoSnapshot();
                                track->inputMono     = true;
                                track->inputMonoChan = c;
                                m_audioEngine->markSessionDirty();
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
            }

            if (numPairs > 0) {
                // Mono routes to a single output channel; stereo routes to
                // a stereo pair. Preview label mirrors whichever is active.
                char outLabel[32];
                if (track->outputMono) {
                    sprintf(outLabel, "O = %d", track->outputMonoChan + 1);
                } else {
                    sprintf(outLabel, "O = %d-%d",
                            (track->outputPair * 2) + 1,
                            (track->outputPair * 2) + 2);
                }
                ImGui::SetNextItemWidth(numInputPairs > 0 ? halfW : -1.0f);
                if (ImGui::BeginCombo("##out", outLabel, ImGuiComboFlags_NoArrowButton)) {
                    // Stereo pairs first.
                    for (int p = 0; p < numPairs; p++) {
                        bool sel = (!track->outputMono && track->outputPair == p);
                        char item[32];
                        sprintf(item, "O = %d-%d", (p * 2) + 1, (p * 2) + 2);
                        if (ImGui::Selectable(item, sel)) {
                            m_audioEngine->undoSnapshot();
                            track->outputMono = false;
                            track->outputPair = p;
                            m_audioEngine->markSessionDirty();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    // Then mono channels — a single output channel is
                    // useful for routing to a single hardware output that
                    // the audio interface's mixer will then handle.
                    int numMonoOut = numPairs * 2;
                    if (numMonoOut > 0) {
                        ImGui::Separator();
                        for (int c = 0; c < numMonoOut; c++) {
                            bool sel = (track->outputMono && track->outputMonoChan == c);
                            char item[32];
                            sprintf(item, "O = %d", c + 1);
                            if (ImGui::Selectable(item, sel)) {
                                m_audioEngine->undoSnapshot();
                                track->outputMono     = true;
                                track->outputMonoChan = c;
                                m_audioEngine->markSessionDirty();
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            // --- Per-track level meter ---
            // Sits directly below the I/O row. Reads the audio thread's
            // peak (input signal when the "I" monitor is on, playback
            // when off) and applies attack-fast / decay-slow smoothing.
            // Colour bands mirror a standard channel-strip meter:
            //   0.0 .. -18 dB  → green
            //   -18 .. -6 dB   → yellow
            //   -6  .. 0  dB   → red (clip zone)
            {
                if ((int)m_trackMeterDecay.size() <= (int)i) m_trackMeterDecay.resize(i + 1, 0.0f);
                float raw = m_audioEngine->getTrackMeter((int)i);
                if (raw > 1.0f) raw = 1.0f;
                float prev = m_trackMeterDecay[i];
                // Attack: snap up. Decay: exponential toward 0 (~600 ms
                // to fall by e), independent of frame rate via dt.
                float v = (raw > prev) ? raw
                                       : prev + (raw - prev) * std::min(1.0f,
                                                    ImGui::GetIO().DeltaTime * 4.0f);
                m_trackMeterDecay[i] = v;

                // Log-ish mapping so the bar reflects perceived level
                // instead of linear amplitude: -60 dB → 0 %, 0 dB → 100 %.
                float bar = 0.0f;
                if (v > 1e-4f) {
                    float db = 20.0f * std::log10(v);
                    bar = 1.0f + db / 60.0f;   // -60 → 0, 0 → 1
                    if (bar < 0.0f) bar = 0.0f;
                    if (bar > 1.0f) bar = 1.0f;
                }

                float avail = ImGui::GetContentRegionAvail().x;
                float meterH = 8.0f;
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1(p0.x + avail, p0.y + meterH);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 20, 255), 2.0f);

                // Fill up to `bar`, coloured by which dB band the tip is in.
                float fillW = avail * bar;
                ImU32 col;
                if (bar >= 1.0f - 6.0f/60.0f)       col = IM_COL32(220,  60,  40, 255); // -6..0
                else if (bar >= 1.0f - 18.0f/60.0f) col = IM_COL32(210, 180,  40, 255); // -18..-6
                else                                col = IM_COL32( 40, 180,  70, 255); // below -18
                if (fillW > 1.0f) {
                    dl->AddRectFilled(p0, ImVec2(p0.x + fillW, p1.y), col, 2.0f);
                }
                dl->AddRect(p0, p1, IM_COL32(60, 60, 60, 255), 2.0f);

                ImGui::Dummy(ImVec2(avail, meterH));
            }

            ImGui::EndChild();

            // Horizontal separator between tracks — visually carries into the
            // arrangement view (which also uses Separator at matching heights).
            ImGui::Separator();

            ImGui::PopStyleColor(kTrackStyleCount);
            ImGui::PopID();
        }
        // Capture any wheel-scroll into the shared offset so the
        // arrangement view stays in lock-step next frame.
        m_trackScrollY = ImGui::GetScrollY();
        ImGui::EndChild();   // trackListScroll
    }

    // Helper lambda to draw slider grab as stroke outline and store for UI glow
    auto storeSliderGrab = [this](float value, float minVal, float maxVal) {
        ImVec2 sliderMin = ImGui::GetItemRectMin();
        ImVec2 sliderMax = ImGui::GetItemRectMax();
        float t = (value - minVal) / (maxVal - minVal);
        float grabWidth = 8.0f;
        float grabX = sliderMin.x + t * (sliderMax.x - sliderMin.x - grabWidth);

        ImU32 grabColor;
        if (m_colorScheme == 1) {
            grabColor = IM_COL32(
                (int)(m_customTextColor[0] * 255),
                (int)(m_customTextColor[1] * 255),
                (int)(m_customTextColor[2] * 255), 255);
        } else {
            grabColor = IM_COL32(100, 150, 200, 255);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float x1 = grabX, y1 = sliderMin.y;
        float x2 = grabX + grabWidth, y2 = sliderMax.y;
        drawList->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), grabColor, 0.0f, 0, 1.0f);

        WaveformDrawCmd cmdTop;
        cmdTop.x1 = x1; cmdTop.y1 = y1; cmdTop.x2 = x2; cmdTop.y2 = y1;
        cmdTop.isRect = false; cmdTop.color = grabColor;
        m_waveformDrawCmds.push_back(cmdTop);
        WaveformDrawCmd cmdBot;
        cmdBot.x1 = x1; cmdBot.y1 = y2; cmdBot.x2 = x2; cmdBot.y2 = y2;
        cmdBot.isRect = false; cmdBot.color = grabColor;
        m_waveformDrawCmds.push_back(cmdBot);
        WaveformDrawCmd cmdLeft;
        cmdLeft.x1 = x1; cmdLeft.y1 = y1; cmdLeft.x2 = x1; cmdLeft.y2 = y2;
        cmdLeft.isRect = false; cmdLeft.color = grabColor;
        m_waveformDrawCmds.push_back(cmdLeft);
        WaveformDrawCmd cmdRight;
        cmdRight.x1 = x2; cmdRight.y1 = y1; cmdRight.x2 = x2; cmdRight.y2 = y2;
        cmdRight.isRect = false; cmdRight.color = grabColor;
        m_waveformDrawCmds.push_back(cmdRight);
    };

    ImGui::EndChild();
#endif
}

void GUIManager::renderWaveform(float height) {
#ifdef IMGUI_FOUND
    ImGui::BeginChild("MainArea", ImVec2(0, height), true);

    // ---- View-state pre-pass ----
    // Run scroll-delta consume + zoom-pin math ONCE at the start of the
    // frame, so both the ruler ABOVE and the per-track loop BELOW read
    // the same viewStart / visibleFrames. Without this pre-pass the
    // ruler would use LAST frame's m_viewCenterPosition (per-track
    // updated it AFTER the ruler drew), which during a zoom gesture
    // sends the marker triangle out of sync with the marker's
    // full-height line in the arrangement overlay.
    if (m_audioEngine && m_timelineFrames > 0) {
        double totalFramesD = (double)m_timelineFrames;
        float  zoom          = m_displayZoom > 0.0f ? m_displayZoom : 1.0f;
        double visD          = totalFramesD / (double)zoom;
        if (visD < 100.0) visD = 100.0;
        size_t playPos       = m_audioEngine->getPlaybackPosition();

        // E3 + modifier scroll — same handling per-track used to do.
        long scrollDelta = m_audioEngine->consumeViewScrollDelta();
        if (scrollDelta != 0) {
            long newCenter = (long)m_viewCenterPosition + scrollDelta;
            if (newCenter < 0) newCenter = 0;
            if (newCenter > (long)m_timelineFrames) newCenter = (long)m_timelineFrames;
            m_viewCenterPosition = (size_t)newCenter;
        }

        // Zoom-change detection + stable-fraction pin math (double).
        bool zoomChanged = (zoom != m_lastZoom);
        if (zoomChanged) {
            double oldVisD = totalFramesD / (double)m_lastZoom;
            if (oldVisD < 100.0) oldVisD = 100.0;
            double oldViewStartD = (double)m_viewCenterPosition - oldVisD / 2.0;
            if (oldViewStartD < 0.0) oldViewStartD = 0.0;
            if (oldViewStartD + oldVisD > totalFramesD)
                oldViewStartD = totalFramesD - oldVisD;
            if (oldViewStartD < 0.0) oldViewStartD = 0.0;

            int currentDir = (zoom > m_lastZoom) ? +1 : -1;
            bool directionChanged = (m_lastZoomDirection != 0 &&
                                     currentDir != m_lastZoomDirection);
            if (m_lastZoomDirection == 0 || directionChanged) {
                bool playheadVisible = ((double)playPos >= oldViewStartD) &&
                                       ((double)playPos <  oldViewStartD + oldVisD);
                m_zoomAnchorPinPlayhead = playheadVisible;
                if (playheadVisible && oldVisD > 0.0) {
                    double f = ((double)playPos - oldViewStartD) / oldVisD;
                    if (f < 0.0) f = 0.0;
                    if (f > 1.0) f = 1.0;
                    m_zoomPinScreenFraction = f;
                }
            }
            if (m_zoomAnchorPinPlayhead) {
                double newVs = (double)playPos - m_zoomPinScreenFraction * visD;
                if (newVs < 0.0) newVs = 0.0;
                if (newVs + visD > totalFramesD) newVs = totalFramesD - visD;
                if (newVs < 0.0) newVs = 0.0;
                m_viewStartD         = newVs;
                m_viewCenterPosition = (size_t)(newVs + visD / 2.0);
            } else {
                m_viewStartD = (double)m_viewCenterPosition - visD / 2.0;
                if (m_viewStartD < 0.0) m_viewStartD = 0.0;
                if (m_viewStartD + visD > totalFramesD)
                    m_viewStartD = totalFramesD - visD;
                if (m_viewStartD < 0.0) m_viewStartD = 0.0;
            }
            m_lastZoomDirection = currentDir;
            m_lastZoom          = zoom;
        } else {
            m_viewStartD = (double)m_viewCenterPosition - visD / 2.0;
            if (m_viewStartD < 0.0) m_viewStartD = 0.0;
            if (m_viewStartD + visD > totalFramesD)
                m_viewStartD = totalFramesD - visD;
            if (m_viewStartD < 0.0) m_viewStartD = 0.0;
        }
        m_visibleFramesD = visD;

        // Publish the viewport to the reader thread so encoder marker
        // moves and enableMarkerAtDefault (loop/rec pair press) can
        // compute frame positions from the CURRENT view, not from an
        // outdated one — even when no track has any audio (the per-track
        // publish path used to be skipped in that case).
        m_audioEngine->setViewportRange((size_t)m_viewStartD,
                                        (size_t)m_visibleFramesD);
    }

    // ---- Time ruler ----
    // Same left/width as the arrangement child below, so tick x-positions
    // line up with the waveform frames underneath. Tick interval scales
    // with zoom — at 1 second per pixel we show 1-min marks with minor
    // 10-sec ticks; zooming in walks that down to 1-second increments.
    {
        // View state was already computed by the pre-pass above, so we
        // just read the shared m_viewStartD / m_visibleFramesD values.
        // Both the ruler AND the per-track loop / marker overlay work
        // off these, so the marker triangle, playhead line, and
        // full-height marker line all stay in exact sync during zoom.
        double sampleRate = m_audioEngine ? m_audioEngine->getSampleRate() : 44100.0;
        if (sampleRate < 1.0) sampleRate = 44100.0;
        double viewStartD    = m_viewStartD;
        double visD          = m_visibleFramesD;
        size_t visibleFrames = (size_t)visD;
        if (visibleFrames < 100) visibleFrames = 100;
        size_t viewStart = (size_t)viewStartD;
        size_t viewEnd   = viewStart + visibleFrames;

        // Ruler layout — major ticks span the full ruler height, labels
        // sit vertically centred right next to their tick. The reserved
        // vertical space matches ImGui::GetFrameHeight() so the trailing
        // Separator ends up at the SAME Y as the left panel's Separator
        // (which sits directly under its "TRACKS / SCRUBBING / WAVEFORM"
        // combo — same frame height).
        const float rulerH        = ImGui::GetFrameHeight();
        const float labelH        = ImGui::GetTextLineHeight();
        const float labelY        = (rulerH - labelH) * 0.5f;     // centred
        const float tickTop       = 0.0f;
        const float tickBottom    = rulerH;
        const float minorTickTop  = tickBottom - 5.0f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float width   = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Advance the layout cursor past the ruler so the arrangement
        // child below starts where the tracks used to start.
        ImGui::Dummy(ImVec2(width, rulerH));
        // 15 px gap between the ruler baseline and the blue separator
        // line below. The left panel adds the same gap after its combo
        // so the two separators line up horizontally.
        ImGui::Dummy(ImVec2(1.0f, 15.0f));

        // Feed the tick / bookmark X math the full-precision viewStart
        // and visibleFrames — using the truncated size_t versions would
        // re-introduce the same 1-frame jitter we just eliminated.
        double startSec = viewStartD / sampleRate;
        double endSec   = (viewStartD + visD) / sampleRate;
        double spanSec  = endSec - startSec;
        if (spanSec > 0.0 && width > 1.0f) {
            double pxPerSec = (double)width / spanSec;
            // Pick the smallest interval from the ladder below that gives
            // at least ~70 px between major ticks — labels stay readable.
            const double ladder[] = { 0.1, 0.5, 1, 5, 10, 30, 60, 300, 600, 1800, 3600 };
            const int nLadder = sizeof(ladder) / sizeof(ladder[0]);
            double major = ladder[nLadder - 1];
            for (int i = 0; i < nLadder; i++) {
                if (ladder[i] * pxPerSec >= 70.0) { major = ladder[i]; break; }
            }
            // Minor ticks: 5 subdivisions of the major (unless major<1).
            double minor = major / 5.0;
            if (minor < 0.1) minor = major;   // don't overdraw at sub-second majors

            ImU32 majorCol = IM_COL32(200, 200, 200, 220);
            ImU32 minorCol = IM_COL32(120, 120, 120, 160);
            ImU32 labelCol = IM_COL32(220, 220, 220, 255);

            // Baseline of the ruler.
            dl->AddLine(ImVec2(origin.x,         origin.y + tickBottom),
                        ImVec2(origin.x + width, origin.y + tickBottom),
                        majorCol, 1.0f);

            // Minor ticks first (shorter, dimmer), then majors + labels.
            double firstMinor = std::floor(startSec / minor) * minor;
            for (double t = firstMinor; t <= endSec + 1e-6; t += minor) {
                if (t < startSec - 1e-6) continue;
                float x = origin.x + (float)((t - startSec) * pxPerSec);
                if (x < origin.x || x > origin.x + width) continue;
                double frac = t / major;
                bool isMajor = std::abs(frac - std::round(frac)) < 1e-4;
                if (isMajor) continue;
                dl->AddLine(ImVec2(x, origin.y + minorTickTop),
                            ImVec2(x, origin.y + tickBottom),
                            minorCol, 1.0f);
            }
            double firstMajor = std::floor(startSec / major) * major;
            for (double t = firstMajor; t <= endSec + 1e-6; t += major) {
                if (t < startSec - 1e-6) continue;
                float x = origin.x + (float)((t - startSec) * pxPerSec);
                if (x < origin.x || x > origin.x + width) continue;
                dl->AddLine(ImVec2(x, origin.y + tickTop),
                            ImVec2(x, origin.y + tickBottom),
                            majorCol, 1.0f);
                char buf[16];
                int totalDeci = (int)std::round(t * 10.0);
                int mins  = totalDeci / 600;
                int secs  = (totalDeci / 10) % 60;
                int deci  = totalDeci % 10;
                if (major >= 1.0) snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
                else              snprintf(buf, sizeof(buf), "%d:%02d.%d", mins, secs, deci);
                dl->AddText(ImVec2(x + 3, origin.y + labelY), labelCol, buf);
            }

            // ---- Bookmarks (pad 13) ----
            // Each pad-13 release appends a bookmark and opens the
            // controller's TEXTIN naming flow. Draw a white downward
            // triangle for each bookmark and, next to it, the name
            // (or the live-typing preview + blinking cursor if the
            // firmware is currently naming this specific bookmark).
            auto bookmarks = m_audioEngine
                ? m_audioEngine->getBookmarks()
                : std::vector<AudioEngine::Bookmark>();
            if (!bookmarks.empty()) {
                float  itemSp    = ImGui::GetStyle().ItemSpacing.y;
                float  blueLineY = origin.y + rulerH + 15.0f + itemSp;
                float  topY      = origin.y + rulerH + 6.0f;
                float  tipY      = blueLineY + 2.0f;
                float  halfW     = 6.0f;
                ImU32  white     = IM_COL32(255, 255, 255, 255);
                int renamingBm   = m_audioEngine->getRenameBookmarkIndex();
                std::string liveBuf;
                int liveCursor = 0;
                bool liveActive = m_audioEngine->getRenameBuffer(liveBuf, liveCursor);
                float flashB = m_audioEngine->getLedFlashBrightness();
                bool cursorOn = (flashB > 0.5f);
                for (int i = 0; i < (int)bookmarks.size(); i++) {
                    const auto& bm = bookmarks[i];
                    if (bm.frame < viewStart || bm.frame >= viewEnd) continue;
                    double bmSec = (double)bm.frame / sampleRate;
                    float  bx    = origin.x + (float)((bmSec - startSec) * pxPerSec);
                    dl->AddTriangleFilled(
                        ImVec2(bx - halfW, topY),
                        ImVec2(bx + halfW, topY),
                        ImVec2(bx,         tipY),
                        white);
                    // Label to the right of the triangle. When this
                    // bookmark is being renamed, show the live buffer
                    // with a phase-synced blinking underscore cursor.
                    std::string label = bm.name;
                    if (i == renamingBm && liveActive) {
                        label = liveBuf;
                        int cp = liveCursor;
                        if (cp < 0) cp = 0;
                        if (cp > (int)label.size()) label.resize((size_t)cp, ' ');
                        if (cursorOn) {
                            if (cp < (int)label.size()) label[(size_t)cp] = '_';
                            else                        label.push_back('_');
                        } else if (cp >= (int)label.size()) {
                            label.push_back(' ');
                        }
                    }
                    if (!label.empty()) {
                        // Right of the triangle's right vertex, inside
                        // the gap between the ruler baseline and the
                        // blue separator (not inside the ruler itself).
                        // Offset 4 px lower for a bit more clearance
                        // from the ruler line above.
                        float bmLabelY = origin.y + rulerH +
                                         (blueLineY - (origin.y + rulerH) - labelH) * 0.5f
                                         + 4.0f;
                        dl->AddText(ImVec2(bx + halfW + 3.0f, bmLabelY),
                                    white, label.c_str());
                    }
                }
            }
        }
    }
    ImGui::Separator();

    int selectedTrack = m_audioEngine->getSelectedTrack();
    int trackCount = m_audioEngine->getTrackCount();

    // Per-track slot height — MUST match the left panel exactly so the
    // horizontal separators line up. Auto-fit up to FIT_LIMIT tracks,
    // then lock at 1/FIT_LIMIT and let the scrollbar handle overflow.
    static constexpr int    FIT_LIMIT_ARR   = 5;
    static constexpr float  MIN_TRACK_H_ARR = 60.0f;
    float availableHeight_arr = ImGui::GetContentRegionAvail().y;
    float separatorH_arr = ImGui::GetTextLineHeightWithSpacing() * 0.5f;
    int   divisor_arr    = (trackCount < FIT_LIMIT_ARR) ? trackCount : FIT_LIMIT_ARR;
    if (divisor_arr < 1) divisor_arr = 1;   // avoid div by zero when trackCount==0
    float perTrackH_arr  = availableHeight_arr / (float)divisor_arr;
    if (perTrackH_arr < MIN_TRACK_H_ARR) perTrackH_arr = MIN_TRACK_H_ARR;
    float perTrackWaveformHeight = perTrackH_arr - separatorH_arr;

    ImGui::BeginChild("arrangementScroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoBackground);
    ImGui::SetScrollY(m_trackScrollY);

    // First drawn track owns the marker triangle; every track below extends
    // its marker/playhead line into the separator gap so the vertical stack
    // reads as one unbroken line rather than a series of segments.
    bool isFirstArrangementTrack = true;
    // Captured from the FIRST drawn track: X-axis mapping (frame → pixel)
    // used by the after-loop overlay that draws markers, playhead, the
    // top triangles, and the bottom horizontal bar. The overlay pins those
    // to the arrangement child's screen bounds so scrolling the tracks
    // never scrolls the triangles/bar out of view.
    bool   arrHasDrawnTrack     = false;
    float  arrLastCursorX       = 0.0f;
    float  arrLastWaveW         = 0.0f;
    size_t arrLastViewStart     = 0;
    size_t arrLastViewEnd       = 0;
    size_t arrLastVisibleFrames = 1;

    for (int i = 0; i < trackCount; i++) {
        const Track* track = m_audioEngine->getTrack(i);
        if (track && !track->audioData.empty()) {
            bool isSelected = (selectedTrack == i);

            // (Track name intentionally not drawn here — the left panel's
            // TR# label is the single source of truth for name/state.)

            const std::vector<float>& audioData = track->audioData;
            int channels = track->channels;
            // Defensive: never divide by zero. audioData being non-empty
            // with channels==0 would be a bug in loadAudio / recording
            // setup — treat it as "no audio" for this frame so the render
            // doesn't crash, and log the inconsistency exactly once.
            if (channels <= 0) {
                static bool sLoggedOnce = false;
                if (!sLoggedOnce) {
                    fprintf(stderr, "!! render: track %d has channels=%d audioSize=%zu\n",
                            i, channels, audioData.size());
                    sLoggedOnce = true;
                }
                ImGui::InvisibleButton(("track_" + std::to_string(i)).c_str(),
                                       ImVec2(ImGui::GetContentRegionAvail().x,
                                              perTrackWaveformHeight));
                ImGui::Separator();
                continue;
            }
            // trackFrames = this track's own audio length (used for texture
            // uv mapping and clip). totalFrames = the shared arrangement
            // timeline used for view / zoom / scroll math — independent of
            // any single track so recording never resizes the view.
            size_t trackFrames = audioData.size() / channels;
            size_t totalFrames = (m_timelineFrames > 0) ? m_timelineFrames : trackFrames;
            if (totalFrames == 0) totalFrames = 1;   // avoid div by 0
            size_t playbackPos = m_audioEngine->getPlaybackPosition();

            float availableWidth = ImGui::GetContentRegionAvail().x;
            float labelPadding = 0.0f;   // no track-name label here anymore
            ImVec2 waveformSize(availableWidth, perTrackWaveformHeight);
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            cursorPos.y += labelPadding;

            ImGui::InvisibleButton(("track_" + std::to_string(i)).c_str(), ImVec2(waveformSize.x, waveformSize.y + labelPadding));
            if (ImGui::IsItemClicked()) {
                m_audioEngine->setSelectedTrack(i);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 bgColor;
            if (m_colorScheme == 1) {
                bgColor = IM_COL32(
                    (int)(m_customBgColor[0] * 255),
                    (int)(m_customBgColor[1] * 255),
                    (int)(m_customBgColor[2] * 255), 255);
            } else {
                bgColor = isSelected ? IM_COL32(40, 50, 60, 255) : IM_COL32(30, 30, 30, 255);
            }
            drawList->AddRectFilled(cursorPos,
                                   ImVec2(cursorPos.x + waveformSize.x, cursorPos.y + waveformSize.y),
                                   bgColor);

            float centerY = cursorPos.y + waveformSize.y * 0.5f;
            ImU32 centerLineColor;
            if (m_colorScheme == 1) {
                centerLineColor = IM_COL32(
                    (int)(m_customTextColor[0] * 255 * 0.3f),
                    (int)(m_customTextColor[1] * 255 * 0.3f),
                    (int)(m_customTextColor[2] * 255 * 0.3f), 255);
            } else {
                centerLineColor = IM_COL32(60, 60, 60, 255);
            }
            drawList->AddLine(ImVec2(cursorPos.x, centerY),
                             ImVec2(cursorPos.x + waveformSize.x, centerY),
                             centerLineColor);

            float zoom = m_displayZoom;
            size_t visibleFrames = (size_t)(totalFrames / zoom);
            if (visibleFrames < 100) visibleFrames = 100;

            size_t viewStart = 0;
            size_t viewEnd = visibleFrames;

            if (m_waveformScrolling) {
                if (playbackPos > visibleFrames / 2) {
                    viewStart = playbackPos - visibleFrames / 2;
                }
                if (viewStart + visibleFrames > totalFrames) {
                    viewStart = (totalFrames > visibleFrames) ? totalFrames - visibleFrames : 0;
                }
                viewEnd = viewStart + visibleFrames;
                if (viewEnd > totalFrames) viewEnd = totalFrames;
            } else {
                // Scroll-delta + zoom-pin math has already run in the
                // pre-pass at the top of renderWaveform, so
                // m_viewCenterPosition / m_viewStartD / m_visibleFramesD
                // are already up-to-date. The duplicated per-track
                // pin logic used to run here — kept as a false branch
                // just to preserve the structure below without divergence.
                long   scrollDelta = 0;
                bool   zoomChanged = false;
                if (false) {
                    // Compute where the view was BEFORE the zoom.
                    size_t oldVisibleFrames = (size_t)(totalFrames / m_lastZoom);
                    if (oldVisibleFrames < 100) oldVisibleFrames = 100;
                    size_t oldViewStart = (m_viewCenterPosition > oldVisibleFrames / 2)
                        ? m_viewCenterPosition - oldVisibleFrames / 2 : 0;
                    if (oldViewStart + oldVisibleFrames > totalFrames) {
                        oldViewStart = (totalFrames > oldVisibleFrames)
                            ? totalFrames - oldVisibleFrames : 0;
                    }

                    // Determine anchor mode. Sticky within a zoom operation:
                    // we only re-evaluate on direction reversal or first zoom
                    // ever. Continuing zoom in the same direction keeps the
                    // previous anchor mode — so a zoom-out that reveals the
                    // playhead mid-operation keeps zooming around the view
                    // centre until the user reverses direction.
                    int currentDir = (zoom > m_lastZoom) ? +1 : -1;
                    bool directionChanged = (m_lastZoomDirection != 0 &&
                                             currentDir != m_lastZoomDirection);
                    if (m_lastZoomDirection == 0 || directionChanged) {
                        bool playheadVisible = (playbackPos >= oldViewStart) &&
                                               (playbackPos <  oldViewStart + oldVisibleFrames);
                        m_zoomAnchorPinPlayhead = playheadVisible;
                        if (playheadVisible && oldVisibleFrames > 0) {
                            // Capture the STABLE on-screen fraction where
                            // the playhead currently sits (0 = left edge,
                            // 1 = right edge). Reused each subsequent
                            // zoom frame in the same direction, so the
                            // pin math doesn't drift from truncated
                            // feedback.
                            double f = (double)((long long)playbackPos - (long long)oldViewStart)
                                       / (double)oldVisibleFrames;
                            if (f < 0.0) f = 0.0;
                            if (f > 1.0) f = 1.0;
                            m_zoomPinScreenFraction = f;
                        }
                    }

                    if (m_zoomAnchorPinPlayhead) {
                        // Pin the playhead using the captured stable
                        // fraction — no accumulated truncation error.
                        // Compute visibleFrames as DOUBLE (not the
                        // truncated size_t) so the pin math stays
                        // sub-frame accurate. Store the double result
                        // for the ruler / marker overlay to consume
                        // directly and avoid re-truncation jitter.
                        double visD = (double)totalFrames / (double)zoom;
                        if (visD < 100.0) visD = 100.0;
                        double newViewStartD = (double)playbackPos
                                             - m_zoomPinScreenFraction * visD;
                        if (newViewStartD < 0.0) newViewStartD = 0.0;
                        m_viewStartD         = newViewStartD;
                        m_visibleFramesD     = visD;
                        m_viewCenterPosition = (size_t)(newViewStartD + visD / 2.0);
                    }
                    // Otherwise: leave m_viewCenterPosition alone so the zoom
                    // happens around the middle of the current view.

                    m_lastZoomDirection = currentDir;
                    m_lastZoom = zoom;
                }

                size_t currentViewStart = m_viewCenterPosition > visibleFrames / 2 ?
                    m_viewCenterPosition - visibleFrames / 2 : 0;
                size_t currentViewEnd = currentViewStart + visibleFrames;

                bool needsRecenter = (m_viewCenterPosition == 0) ||
                    (playbackPos < currentViewStart) ||
                    (playbackPos >= currentViewEnd);

                // Only auto-page (jump view to keep playhead visible) when
                // the user has that turned on AND playback is actually
                // running. Skip on zoom-change ticks so we don't undo the
                // playhead-pinning we just did above. And skip while paused
                // — otherwise panning the view (with pad 24 + E2) so the
                // playhead ends up off-screen would trigger a jump back
                // to the playhead the moment you release the encoder.
                if (needsRecenter && scrollDelta == 0 && m_waveformAutoPage
                    && !zoomChanged && m_audioEngine->isPlaying()) {
                    printf("[AUTOPAGE] recenter viewCenter=%zu->%zu playhead=%zu view=[%zu..%zu] vis=%zu\n",
                           m_viewCenterPosition, playbackPos, playbackPos,
                           currentViewStart, currentViewEnd, visibleFrames);
                    m_viewCenterPosition = playbackPos;
                }
                // Log any other unexpected recenter path
                if (zoomChanged) {
                    printf("[ZOOM] viewCenter=%zu playhead=%zu vis=%zu (pinning)\n",
                           m_viewCenterPosition, playbackPos, visibleFrames);
                }

                size_t centerPos = (m_viewCenterPosition > 0) ? m_viewCenterPosition : playbackPos;

                if (centerPos > visibleFrames / 2) {
                    viewStart = centerPos - visibleFrames / 2;
                } else {
                    viewStart = 0;
                }
                if (viewStart + visibleFrames > totalFrames) {
                    viewStart = (totalFrames > visibleFrames) ? totalFrames - visibleFrames : 0;
                }
                viewEnd = viewStart + visibleFrames;
                if (viewEnd > totalFrames) viewEnd = totalFrames;
            }

            // Publish the current viewport so the reader thread can compute
            // first-time default marker positions relative to what's on screen.
            m_audioEngine->setViewportRange(viewStart, viewEnd - viewStart);

            size_t framesPerPixel = visibleFrames / (size_t)waveformSize.x;
            if (framesPerPixel < 1) framesPerPixel = 1;

            ImU32 waveColor = isSelected
                ? IM_COL32(240, 240, 240, 255)   // selected = white
                : IM_COL32(77, 140, 204, 255);   // unselected = blue

            if (m_simplifiedWaveform) {
                int numBars = 50;
                float barWidth = waveformSize.x / numBars;
                size_t framesPerBar = visibleFrames / numBars;
                if (framesPerBar < 1) framesPerBar = 1;

                for (int bar = 0; bar < numBars; bar++) {
                    size_t barFrameStart = viewStart + bar * framesPerBar;
                    size_t barFrameEnd = barFrameStart + framesPerBar;
                    if (barFrameEnd > totalFrames) barFrameEnd = totalFrames;
                    if (barFrameStart >= totalFrames) break;

                    float sumSquares = 0.0f;
                    size_t sampleCount = 0;
                    for (size_t frame = barFrameStart; frame < barFrameEnd; frame++) {
                        // Belt-and-braces bound check — trackFrames is
                        // audioData.size()/channels so frame*channels+ch
                        // should always fit, but guard anyway to survive
                        // any concurrent audioData replacement.
                        size_t lastIdx = frame * (size_t)channels + (size_t)(channels - 1);
                        if (lastIdx >= audioData.size()) break;
                        float sample = 0.0f;
                        for (int ch = 0; ch < channels; ch++) {
                            sample += audioData[frame * channels + ch];
                        }
                        sample /= channels;
                        sumSquares += sample * sample;
                        sampleCount++;
                    }
                    float rms = (sampleCount > 0) ? std::sqrt(sumSquares / sampleCount) : 0.0f;

                    float barHeight = rms * waveformSize.y * 0.9f * m_waveformVerticalZoom;
                    float barX = cursorPos.x + bar * barWidth + 1;
                    float barY1 = centerY - barHeight / 2;
                    float barY2 = centerY + barHeight / 2;

                    float trackTop = cursorPos.y;
                    float trackBottom = cursorPos.y + waveformSize.y;
                    if (barY1 < trackTop) barY1 = trackTop;
                    if (barY2 > trackBottom) barY2 = trackBottom;

                    drawList->AddRectFilled(
                        ImVec2(barX, barY1),
                        ImVec2(barX + barWidth - 2, barY2),
                        waveColor);

                    WaveformDrawCmd cmd;
                    cmd.x1 = barX; cmd.y1 = barY1;
                    cmd.isRect = true;
                    cmd.rectX2 = barX + barWidth - 2; cmd.rectY2 = barY2;
                    cmd.color = waveColor;
                    m_waveformDrawCmds.push_back(cmd);
                }
            } else {
                // Pre-rendered GPU texture path — sub-pixel smooth under
                // zoom / pan because the GPU linear-filters the texture.
                // Two levels of detail:
                //   overview  : whole track, built once at load time
                //   detail    : covers only the current view window (+25%
                //               margin), rebuilt when the view exits the
                //               window. Chosen automatically when the
                //               overview's per-texel sample count would
                //               exceed one screen pixel.
                Track* tMut = m_audioEngine->getTrack(i);
                if (tMut && (tMut->waveformTex == 0 ||
                             tMut->waveformTexVersion != tMut->audioVersion)) {
                    uploadWaveformTexture(tMut);
                }
                if (tMut && tMut->waveformTex && trackFrames > 0) {
                    // Track's audio occupies world frames [0, trackFrames].
                    // Draw only the portion that overlaps the shared view
                    // [viewStart, viewEnd] — a track shorter than the
                    // timeline (e.g. a fresh recording mid-take) occupies
                    // just its left portion, not the whole row width.
                    size_t drawStartFrame = viewStart;
                    size_t drawEndFrame   = (viewEnd < trackFrames) ? viewEnd : trackFrames;
                    if (drawEndFrame > drawStartFrame) {

                    // Framing / vertical zoom (same for both LODs).
                    float halfH  = waveformSize.y * 0.5f * m_waveformVerticalZoom;
                    float top    = centerY - halfH;
                    float bottom = centerY + halfH;
                    if (top    < cursorPos.y)                  top    = cursorPos.y;
                    if (bottom > cursorPos.y + waveformSize.y) bottom = cursorPos.y + waveformSize.y;

                    // Pixel bounds for the visible portion of the track's
                    // audio — mapped through the SHARED timeline view.
                    float pxLeft  = cursorPos.x + (float)((double)(drawStartFrame - viewStart) / (double)visibleFrames) * waveformSize.x;
                    float pxRight = cursorPos.x + (float)((double)(drawEndFrame   - viewStart) / (double)visibleFrames) * waveformSize.x;

                    // Decide overview vs detail based on the VISIBLE
                    // portion of the track: how many track frames per
                    // screen pixel across the drawn area.
                    size_t overviewFPT = trackFrames / (size_t)tMut->waveformTexW;
                    if (overviewFPT < 1) overviewFPT = 1;
                    size_t drawFrames = drawEndFrame - drawStartFrame;
                    size_t drawPixels = (size_t)std::max(1.0f, pxRight - pxLeft);
                    size_t viewFPP    = drawFrames / drawPixels;
                    if (viewFPP < 1) viewFPP = 1;
                    bool useDetail = (viewFPP < overviewFPT);

                    unsigned int texId = tMut->waveformTex;
                    float u1 = 0.0f, u2 = 1.0f;
                    if (useDetail) {
                        // Pick a STABLE framesPerBucket: largest power of 2
                        // <= viewFPP. Because bucket size and bucket
                        // boundaries are aligned to sample 0, panning by
                        // fewer than one bucket doesn't shift any texel
                        // content, and panning past the covered edge just
                        // re-uploads the same texels for a different aligned
                        // window — no per-rebuild content jitter.
                        size_t fpbTarget = 1;
                        while (fpbTarget * 2 <= viewFPP) fpbTarget *= 2;

                        // Hysteresis: only change fpb if it drifts by 2x or
                        // more. Prevents rapid ping-pong at the boundary.
                        size_t fpbUse = tMut->waveformDetailFPB;
                        if (fpbUse == 0 ||
                            fpbTarget >= fpbUse * 2 ||
                            fpbTarget * 2 <= fpbUse) {
                            fpbUse = fpbTarget;
                        }

                        size_t detailW = 8192;
                        size_t coveredRange = detailW * fpbUse;
                        bool inRange = (tMut->waveformDetailTex != 0) &&
                                       (drawStartFrame >= tMut->waveformDetailStart) &&
                                       (drawEndFrame   <= tMut->waveformDetailEnd)   &&
                                       (tMut->waveformDetailFPB == fpbUse) &&
                                       (tMut->waveformDetailVersion == tMut->audioVersion);
                        if (!inRange) {
                            // Centre the window on the drawn range so pan
                            // margin is available in both directions.
                            size_t centreFrame = (drawStartFrame + drawEndFrame) / 2;
                            size_t half        = coveredRange / 2;
                            size_t dStart      = (centreFrame > half) ? centreFrame - half : 0;
                            if (dStart + coveredRange > trackFrames)
                                dStart = (trackFrames > coveredRange) ? trackFrames - coveredRange : 0;
                            uploadWaveformDetailTexture(tMut, dStart, fpbUse);
                        }
                        if (tMut->waveformDetailTex) {
                            texId = tMut->waveformDetailTex;
                            uint64_t dRange = (uint64_t)(tMut->waveformDetailEnd - tMut->waveformDetailStart);
                            if (dRange > 0) {
                                u1 = (float)((double)((int64_t)drawStartFrame - (int64_t)tMut->waveformDetailStart) / (double)dRange);
                                u2 = (float)((double)((int64_t)drawEndFrame   - (int64_t)tMut->waveformDetailStart) / (double)dRange);
                            }
                        }
                    } else {
                        u1 = (float)((double)drawStartFrame / (double)trackFrames);
                        u2 = (float)((double)drawEndFrame   / (double)trackFrames);
                    }

                    drawList->AddImage(
                        (ImTextureID)(uintptr_t)texId,
                        ImVec2(pxLeft,  top),
                        ImVec2(pxRight, bottom),
                        ImVec2(u1, 0.0f), ImVec2(u2, 1.0f),
                        waveColor);
                    // Bloom layer 3 bounding rect.
                    WaveformDrawCmd cmd;
                    cmd.x1     = pxLeft;
                    cmd.y1     = top;
                    cmd.isRect = true;
                    cmd.rectX2 = pxRight;
                    cmd.rectY2 = bottom;
                    cmd.color  = waveColor;
                    m_waveformDrawCmds.push_back(cmd);

                    // Fresh-take overlay — paint the frame range covered
                    // by the most recent recording in orange, on top of
                    // the standard blue/white waveform. Uses the same
                    // texture (uv clipped to the take range) so the shape
                    // stays perfectly aligned with what's underneath.
                    // The uv basis depends on which texture we picked
                    // above: OVERVIEW covers [0, trackFrames), DETAIL
                    // covers [waveformDetailStart, waveformDetailEnd).
                    size_t fstart = track->freshTakeStart;
                    size_t fend   = track->freshTakeEnd;
                    if (fend > fstart && fstart < drawEndFrame && fend > drawStartFrame) {
                        size_t fs = (fstart > drawStartFrame) ? fstart : drawStartFrame;
                        size_t fe = (fend   < drawEndFrame)   ? fend   : drawEndFrame;
                        if (fe > fs) {
                            float fx1 = cursorPos.x + (float)((double)(fs - viewStart) / (double)visibleFrames) * waveformSize.x;
                            float fx2 = cursorPos.x + (float)((double)(fe - viewStart) / (double)visibleFrames) * waveformSize.x;
                            float fu1, fu2;
                            if (useDetail && tMut->waveformDetailTex) {
                                uint64_t dRange = (uint64_t)(tMut->waveformDetailEnd - tMut->waveformDetailStart);
                                if (dRange == 0) { fu1 = 0.0f; fu2 = 0.0f; }
                                else {
                                    fu1 = (float)((double)((int64_t)fs - (int64_t)tMut->waveformDetailStart) / (double)dRange);
                                    fu2 = (float)((double)((int64_t)fe - (int64_t)tMut->waveformDetailStart) / (double)dRange);
                                }
                            } else {
                                fu1 = (float)((double)fs / (double)trackFrames);
                                fu2 = (float)((double)fe / (double)trackFrames);
                            }
                            ImU32 freshColor = IM_COL32(255, 140, 20, 255);
                            drawList->AddImage(
                                (ImTextureID)(uintptr_t)texId,
                                ImVec2(fx1, top), ImVec2(fx2, bottom),
                                ImVec2(fu1, 0.0f), ImVec2(fu2, 1.0f),
                                freshColor);
                            WaveformDrawCmd fresh;
                            fresh.x1 = fx1; fresh.y1 = top;
                            fresh.isRect = true;
                            fresh.rectX2 = fx2; fresh.rectY2 = bottom;
                            fresh.color = freshColor;
                            m_waveformDrawCmds.push_back(fresh);
                        }
                    }
                    }
                }
            }

            // Marker / playhead / triangle / horizontal-bar drawing is
            // deferred until after the per-track loop so it can be pinned
            // to the arrangement child's screen bounds (see below) instead
            // of scrolling with the track content. We only capture the
            // first drawn track's X mapping here.
            if (isFirstArrangementTrack) {
                arrHasDrawnTrack     = true;
                arrLastCursorX       = cursorPos.x;
                arrLastWaveW         = waveformSize.x;
                arrLastViewStart     = viewStart;
                arrLastViewEnd       = viewEnd;
                arrLastVisibleFrames = visibleFrames > 0 ? visibleFrames : 1;
            }
            isFirstArrangementTrack = false;

            // Horizontal separator between tracks — visually pairs with the
            // matching Separator in the left panel's TRACKS view, and picks
            // up the track's blue/gray colour like the rest of the row.
            ImGui::PushStyleColor(ImGuiCol_Separator,
                isSelected ? ImVec4(0.95f, 0.95f, 0.95f, 1.0f)
                           : ImVec4(0.30f, 0.55f, 0.80f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor();
        }
        else if (track) {
            // Empty track (no audio loaded yet) — show a placeholder slot so
            // the user can see the track exists in the arrangement. Name
            // is intentionally not drawn here; only in the left panel.
            bool isSelected = (selectedTrack == i);

            float availableWidth = ImGui::GetContentRegionAvail().x;
            float labelPadding = 0.0f;   // no track-name label here anymore
            ImVec2 waveformSize(availableWidth, perTrackWaveformHeight);
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            cursorPos.y += labelPadding;

            ImGui::InvisibleButton(("track_" + std::to_string(i)).c_str(),
                                   ImVec2(waveformSize.x, waveformSize.y + labelPadding));
            if (ImGui::IsItemClicked()) {
                m_audioEngine->setSelectedTrack(i);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 bgColor = (m_colorScheme == 1)
                ? IM_COL32((int)(m_customBgColor[0] * 255),
                           (int)(m_customBgColor[1] * 255),
                           (int)(m_customBgColor[2] * 255), 255)
                : (isSelected ? IM_COL32(40, 50, 60, 255) : IM_COL32(30, 30, 30, 255));
            drawList->AddRectFilled(cursorPos,
                                    ImVec2(cursorPos.x + waveformSize.x, cursorPos.y + waveformSize.y),
                                    bgColor);
            // Center-line and empty-state label so it doesn't look broken.
            float centerY = cursorPos.y + waveformSize.y * 0.5f;
            drawList->AddLine(ImVec2(cursorPos.x, centerY),
                              ImVec2(cursorPos.x + waveformSize.x, centerY),
                              IM_COL32(80, 80, 80, 255));
            const char* emptyText = "(no audio)";
            ImVec2 textSize = ImGui::CalcTextSize(emptyText);
            drawList->AddText(ImVec2(cursorPos.x + (waveformSize.x - textSize.x) * 0.5f,
                                     centerY - textSize.y * 0.5f - 12),
                              IM_COL32(120, 120, 120, 255), emptyText);
            ImGui::Separator();

            // Register this empty slot for the shared timeline overlay
            // so the playhead / markers still draw over it (matching the
            // behaviour of a loaded track). Uses the same shared timeline
            // math as the drawn-audio branch above.
            if (isFirstArrangementTrack) {
                size_t tlFrames = (m_timelineFrames > 0) ? m_timelineFrames : 1;
                float  zoomL    = m_displayZoom;
                size_t visFrames = (size_t)((double)tlFrames / (double)zoomL);
                if (visFrames < 100) visFrames = 100;
                size_t viewStartL = 0;
                if (m_viewCenterPosition > visFrames / 2)
                    viewStartL = m_viewCenterPosition - visFrames / 2;
                if (viewStartL + visFrames > tlFrames)
                    viewStartL = (tlFrames > visFrames) ? tlFrames - visFrames : 0;
                size_t viewEndL = viewStartL + visFrames;
                if (viewEndL > tlFrames) viewEndL = tlFrames;

                arrHasDrawnTrack     = true;
                arrLastCursorX       = cursorPos.x;
                arrLastWaveW         = waveformSize.x;
                arrLastViewStart     = viewStartL;
                arrLastViewEnd       = viewEndL;
                arrLastVisibleFrames = visFrames;
            }
            isFirstArrangementTrack = false;
        }
    }

    // A session with NO tracks draws no track strips, so nothing above
    // captured the frame→pixel mapping and the whole overlay below would be
    // skipped: no loop or punch markers, no play line, nothing to aim at.
    // A fresh session should still be a usable timeline you can place
    // markers on and run the playhead over — there is simply no audio.
    //
    // Synthesise the mapping from the arrangement child's own width and the
    // shared view state (m_viewStartD / m_visibleFramesD), which the pre-pass
    // at the top of renderWaveform maintains regardless of track count.
    if (!arrHasDrawnTrack) {
        const float pad = ImGui::GetStyle().WindowPadding.x;
        float w = ImGui::GetWindowSize().x - pad * 2.0f;
        if (w < 1.0f) w = 1.0f;
        arrHasDrawnTrack     = true;
        arrLastCursorX       = ImGui::GetWindowPos().x + pad;
        arrLastWaveW         = w;
        arrLastViewStart     = (size_t)m_viewStartD;
        arrLastVisibleFrames = (m_visibleFramesD > 1.0) ? (size_t)m_visibleFramesD : 1;
        arrLastViewEnd       = arrLastViewStart + arrLastVisibleFrames;
    }

    // Marker + playhead overlay — pinned to the arrangement child's
    // screen bounds so triangles, vertical lines, and the loop/rec bar
    // stay fixed at the top/bottom edges regardless of vertical scroll.
    if (arrHasDrawnTrack) {
        ImVec2 childPos  = ImGui::GetWindowPos();   // fixed in screen space
        ImVec2 childSize = ImGui::GetWindowSize();
        float overlayTop    = childPos.y;
        float overlayBottom = childPos.y + childSize.y - 3;

        size_t playbackPos = m_audioEngine->getPlaybackPosition();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 yellowColor  = IM_COL32(255, 220, 0, 255);
        ImU32 redColor     = IM_COL32(255,  50, 50, 255);
        ImU32 playheadColor = IM_COL32(50, 220, 50, 255);

        auto storeLineCmd = [this](float x1, float y1, float x2, float y2, ImU32 color, float thickness) {
            LineDrawCmd cmd;
            cmd.x1 = x1; cmd.y1 = y1; cmd.x2 = x2; cmd.y2 = y2;
            cmd.color = color;
            cmd.thickness = thickness;
            cmd.isTriangle = false;
            cmd.isText = false;
            m_lineDrawCmds.push_back(cmd);
        };

        auto frameToX = [&](size_t frame) {
            return arrLastCursorX +
                   ((float)(frame - arrLastViewStart) / (float)arrLastVisibleFrames) * arrLastWaveW;
        };

        // Vertical marker lines + top triangles + labels.
        const char* markerLabels[4] = {"L", "L", "R", "R"};
        for (int m = 0; m < 4; m++) {
            if (!m_audioEngine->isMarkerEnabled(m)) continue;
            size_t markerPos = m_audioEngine->getMarkerPosition(m);
            if (markerPos < arrLastViewStart || markerPos >= arrLastViewEnd) continue;

            float markerX     = frameToX(markerPos);
            ImU32 markerColor = (m == 0 || m == 3) ? yellowColor : redColor;

            drawList->AddLine(ImVec2(markerX, overlayTop),
                              ImVec2(markerX, overlayBottom),
                              markerColor, 2.0f);
            storeLineCmd(markerX, overlayTop, markerX, overlayBottom, markerColor, 2.0f);

            float arrowTop    = overlayTop + 2;
            float arrowBottom = overlayTop + 14;
            float arrowWidth  = 6;
            drawList->AddTriangleFilled(
                ImVec2(markerX,               arrowBottom),
                ImVec2(markerX - arrowWidth,  arrowTop),
                ImVec2(markerX + arrowWidth,  arrowTop),
                markerColor);

            LineDrawCmd triCmd;
            triCmd.isTriangle = true;
            triCmd.isText     = false;
            triCmd.tx1 = markerX;              triCmd.ty1 = arrowBottom;
            triCmd.tx2 = markerX - arrowWidth; triCmd.ty2 = arrowTop;
            triCmd.tx3 = markerX + arrowWidth; triCmd.ty3 = arrowTop;
            triCmd.color = markerColor;
            m_lineDrawCmds.push_back(triCmd);

            float labelY = overlayTop + 2;
            drawList->AddText(ImVec2(markerX - 4, labelY), markerColor, markerLabels[m]);

            LineDrawCmd textCmd;
            textCmd.isTriangle = false;
            textCmd.isText     = true;
            textCmd.textX = markerX - 4;
            textCmd.textY = labelY;
            textCmd.color = markerColor;
            strncpy(textCmd.text, markerLabels[m], 7);
            textCmd.text[7] = '\0';
            m_lineDrawCmds.push_back(textCmd);
        }

        // Playhead — same fixed vertical span as the markers. If it
        // lands on top of an enabled loop/punch marker (within a couple
        // of pixels — screen-space, not frame-space, since one pixel
        // covers many frames when zoomed out), switch to dashed so the
        // marker's yellow/red bar shows through the gaps and you can
        // tell they're stacked instead of losing the marker under the
        // solid green line.
        if (playbackPos >= arrLastViewStart && playbackPos < arrLastViewEnd) {
            float playbackX = frameToX(playbackPos);
            bool overMarker = false;
            for (int m = 0; m < 4; m++) {
                if (!m_audioEngine->isMarkerEnabled(m)) continue;
                size_t mp = m_audioEngine->getMarkerPosition(m);
                if (mp < arrLastViewStart || mp >= arrLastViewEnd) continue;
                if (std::fabs(frameToX(mp) - playbackX) < 3.0f) {
                    overMarker = true;
                    break;
                }
            }
            if (overMarker) {
                // Dashed pattern: 6 px dash, 4 px gap. Same colour and
                // thickness as the solid playhead. Each segment fed to
                // both the ImGui drawList (visible immediately) and the
                // stored line list (for the bloom pass).
                const float dashLen = 18.0f;
                const float gapLen  = 12.0f;
                float y = overlayTop;
                while (y < overlayBottom) {
                    float y2 = y + dashLen;
                    if (y2 > overlayBottom) y2 = overlayBottom;
                    drawList->AddLine(ImVec2(playbackX, y),
                                      ImVec2(playbackX, y2),
                                      playheadColor, 2.0f);
                    storeLineCmd(playbackX, y, playbackX, y2, playheadColor, 2.0f);
                    y = y2 + gapLen;
                }
            } else {
                drawList->AddLine(ImVec2(playbackX, overlayTop),
                                  ImVec2(playbackX, overlayBottom),
                                  playheadColor, 2.0f);
                storeLineCmd(playbackX, overlayTop, playbackX, overlayBottom, playheadColor, 2.0f);
            }
        }

        // Marker-scroll mode: a full-height white line marks the
        // currently-selected bookmark so its position (and any E3
        // edits) are easy to track across every track.
        if (m_audioEngine->isBookmarkScrollMode()) {
            int selIdx = m_audioEngine->getSelectedBookmarkIndex();
            auto allBms = m_audioEngine->getBookmarks();
            if (selIdx >= 0 && selIdx < (int)allBms.size()) {
                size_t selFrame = allBms[selIdx].frame;
                if (selFrame >= arrLastViewStart && selFrame < arrLastViewEnd) {
                    float sx = frameToX(selFrame);
                    ImU32 white = IM_COL32(255, 255, 255, 255);
                    drawList->AddLine(ImVec2(sx, overlayTop),
                                      ImVec2(sx, overlayBottom),
                                      white, 2.0f);
                    storeLineCmd(sx, overlayTop, sx, overlayBottom, white, 2.0f);
                }
            }
        }

        // Loop/rec horizontal bars — pinned to the bottom edge of the
        // arrangement child window (does not scroll away). When BOTH
        // markers of a pair are defined but the pair is currently OFF
        // (isMarkerEnabled false, markerEverSet true), the bar draws
        // dashed to indicate the region still exists but is inactive.
        auto drawDashed = [&](float x1, float x2, float y, ImU32 color, float thickness) {
            const float dashLen = 8.0f;
            const float gapLen  = 5.0f;
            float x = x1;
            while (x < x2) {
                float xe = x + dashLen;
                if (xe > x2) xe = x2;
                drawList->AddLine(ImVec2(x, y), ImVec2(xe, y), color, thickness);
                storeLineCmd(x, y, xe, y, color, thickness);
                x = xe + gapLen;
            }
        };
        // Draws the bottom bar segment between two markers, either solid
        // (segment is "on") or dashed (segment is "defined but off").
        // Whether it draws at all is left to the caller (loop/record
        // states are independent — the yellow segments follow the loop
        // pair, the red middle follows the record pair).
        auto drawSegment = [&](size_t posA, size_t posB, ImU32 color, bool solid) {
            size_t drawStart = (posA < arrLastViewStart) ? arrLastViewStart : posA;
            size_t drawEnd   = (posB > arrLastViewEnd)   ? arrLastViewEnd   : posB;
            if (drawStart >= drawEnd)             return;
            if (drawEnd   <= arrLastViewStart)    return;
            if (drawStart >= arrLastViewEnd)      return;
            float x1 = frameToX(drawStart);
            float x2 = frameToX(drawEnd);
            if (solid) {
                drawList->AddLine(ImVec2(x1, overlayBottom), ImVec2(x2, overlayBottom), color, 3.0f);
                storeLineCmd(x1, overlayBottom, x2, overlayBottom, color, 3.0f);
            } else {
                drawDashed(x1, x2, overlayBottom, color, 3.0f);
            }
        };

        bool loopDef = m_audioEngine->markerEverSet(0) &&
                       m_audioEngine->markerEverSet(3);
        bool loopOn  = m_audioEngine->isMarkerEnabled(0) &&
                       m_audioEngine->isMarkerEnabled(3);
        bool recDef  = m_audioEngine->markerEverSet(1) &&
                       m_audioEngine->markerEverSet(2);
        bool recOn   = m_audioEngine->isMarkerEnabled(1) &&
                       m_audioEngine->isMarkerEnabled(2);

        // Yellow — loop pair. Split around the record region when it
        // exists, otherwise draw one continuous line loop-left → loop-right.
        if (loopDef) {
            size_t p0 = m_audioEngine->getMarkerPosition(0);
            size_t p3 = m_audioEngine->getMarkerPosition(3);
            if (recDef) {
                size_t p1 = m_audioEngine->getMarkerPosition(1);
                size_t p2 = m_audioEngine->getMarkerPosition(2);
                drawSegment(p0, p1, yellowColor, loopOn);
                drawSegment(p2, p3, yellowColor, loopOn);
            } else {
                drawSegment(p0, p3, yellowColor, loopOn);
            }
        }
        // Red — record pair, whatever the loop is doing.
        if (recDef) {
            size_t p1 = m_audioEngine->getMarkerPosition(1);
            size_t p2 = m_audioEngine->getMarkerPosition(2);
            drawSegment(p1, p2, redColor, recOn);
        }
    }

    // Capture the wheel-scroll for next frame so the left panel and
    // arrangement view stay in lock-step.
    m_trackScrollY = ImGui::GetScrollY();
    ImGui::EndChild();   // arrangementScroll

    if (trackCount == 0) {
        ImVec4 hintColor = (m_colorScheme == 1)
            ? ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f)
            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(hintColor, "Add a track and load audio to see waveforms here");
    }

    ImGui::EndChild();
#endif
}

void GUIManager::renderTransportBar() {
#ifdef IMGUI_FOUND
    ImGui::Separator();

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float buttonWidth = 50.0f;
    float buttonHeight = 35.0f;
    float transportWidth = buttonWidth * 3 + 20;
    float startX = (windowWidth - transportWidth) * 0.5f;

    ImGui::Dummy(ImVec2(0, 5));

    if (ImGui::Button("Quit", ImVec2(60, buttonHeight))) {
        m_running = false;
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", ImVec2(50, buttonHeight))) {
        saveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Save settings to settings/user_settings.json");
    }
    ImGui::SameLine();

    bool testToneEnabled = m_audioEngine->isTestToneEnabled();
    if (ImGui::Checkbox("Test Tone", &testToneEnabled)) {
        m_audioEngine->setTestToneEnabled(testToneEnabled);
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(startX);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

    if (ImGui::Button("|<", ImVec2(buttonWidth, buttonHeight))) {
        m_audioEngine->stop();
        m_audioEngine->setPlaybackPosition(0);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stop and rewind to beginning");
    }
    ImGui::SameLine();

    bool isPlaying = m_audioEngine->isPlaying();
    if (isPlaying) {
        if (m_colorScheme == 1) {
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
        }
        if (ImGui::Button("[]", ImVec2(buttonWidth, buttonHeight))) {
            m_audioEngine->stop();
        }
        if (m_colorScheme != 1) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stop");
        }
    } else {
        if (m_colorScheme == 1) {
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
        }
        if (ImGui::Button(">", ImVec2(buttonWidth, buttonHeight))) {
            m_audioEngine->play();
        }
        if (m_colorScheme != 1) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Play");
        }
    }
    ImGui::SameLine();

    if (ImGui::Button(">|", ImVec2(buttonWidth, buttonHeight))) {
        size_t totalFrames = m_audioEngine->getTotalFrames();
        if (totalFrames > 0) {
            m_audioEngine->setPlaybackPosition(totalFrames - 1);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Go to end");
    }

    ImGui::PopStyleVar();

    ImGui::SameLine();
    float rightSideX = windowWidth - 500;
    if (ImGui::GetCursorPosX() < rightSideX) {
        ImGui::SetCursorPosX(rightSideX);
    }

    size_t playbackPos = m_audioEngine->getPlaybackPosition();
    size_t totalFrames = m_audioEngine->getTotalFrames();
    double sampleRate = m_audioEngine->getSampleRate();

    if (totalFrames > 0 && sampleRate > 0) {
        double currentTime = (double)playbackPos / sampleRate;
        double totalTime = (double)totalFrames / sampleRate;
        int curMin = (int)(currentTime / 60);
        int curSec = (int)currentTime % 60;
        int totMin = (int)(totalTime / 60);
        int totSec = (int)totalTime % 60;
        ImGui::Text("%02d:%02d / %02d:%02d", curMin, curSec, totMin, totSec);
    } else {
        ImGui::Text("--:-- / --:--");
    }

    ImGui::SameLine();

    const char* schemeNames[] = { "Dark", "Custom" };
    if (ImGui::Button(schemeNames[m_colorScheme], ImVec2(60, 0))) {
        m_colorScheme = (m_colorScheme + 1) % 2;
        applyColorScheme(m_colorScheme);
    }

    if (m_colorScheme == 1) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(m_customBgColor[0], m_customBgColor[1], m_customBgColor[2], 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(m_customBgColor[0], m_customBgColor[1], m_customBgColor[2], 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f));
        if (ImGui::Button("BG", ImVec2(30, 0))) {
            m_editingBgColor = true;
            m_showColorPickers = true;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(m_customBgColor[0], m_customBgColor[1], m_customBgColor[2], 1.0f));
        if (ImGui::Button("FG", ImVec2(30, 0))) {
            m_editingBgColor = false;
            m_showColorPickers = true;
        }
        ImGui::PopStyleColor(3);
    }

    // (Glow controls moved to left panel → WAVEFORM view.)

    // Color picker popup
    if (m_colorScheme == 1 && m_showColorPickers) {
        ImGui::SetNextWindowSize(ImVec2(200, 220), ImGuiCond_FirstUseEver);
        const char* pickerTitle = m_editingBgColor ? "Background" : "Foreground";
        if (ImGui::Begin(pickerTitle, &m_showColorPickers, ImGuiWindowFlags_NoCollapse)) {
            float* colorToEdit = m_editingBgColor ? m_customBgColor : m_customTextColor;
            if (ImGui::ColorPicker3("##color", colorToEdit, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoAlpha)) {
                applyColorScheme(m_colorScheme);
            }
        }
        ImGui::End();
    }
#endif
}

void GUIManager::renderVelocityCurveEditor() {
#ifdef IMGUI_FOUND
    if (!m_showVelocityCurveEditor) return;

    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Encoder Velocity Curve", &m_showVelocityCurveEditor, ImGuiWindowFlags_NoCollapse)) {
        VelocityCurve& curve = m_serialController->getVelocityCurve();

        float currentRpm = m_serialController->getCurrentRpm();
        float normalizedInput = currentRpm / curve.maxInputRpm;
        float currentOutput = curve.evaluate(normalizedInput);
        ImGui::Text("Current: %.1f RPM -> %.2fx multiplier", currentRpm, currentOutput);

        ImGui::Separator();

        ImGui::Text("Max Input RPM:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##maxRpm", &curve.maxInputRpm, 30.0f, 300.0f, "%.0f");

        ImGui::SameLine();
        if (ImGui::Checkbox("Smooth", &curve.smoothed)) {}
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Apply smooth curve interpolation");
        }

        ImGui::Text("RPM Window:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##rpmWindow", &curve.rpmWindowMs, 10.0f, 200.0f, "%.0f ms");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Time window for RPM calculation.\nSmaller = more responsive but noisier.\nLarger = smoother but slower to react.");
        }

        ImGui::Text("Max Multiplier:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("##maxMult", &curve.maxMultiplier, 0.5f, 5.0f, "%.1fx");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum speed multiplier.\nLimits how fast the playhead can move\neven when spinning the encoder quickly.");
        }

        ImGui::Separator();

        // Draw curve editor
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 availableSize = ImGui::GetContentRegionAvail();
        float canvasHeight = std::max(150.0f, availableSize.y - 50.0f);
        ImVec2 canvasSize = ImVec2(availableSize.x, canvasHeight);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##curveCanvas", canvasSize);
        bool canvasHovered = ImGui::IsItemHovered();

        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                IM_COL32(30, 30, 30, 255));
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                          IM_COL32(100, 100, 100, 255));

        for (int i = 1; i < 4; i++) {
            float x = canvasPos.x + canvasSize.x * i / 4.0f;
            float y = canvasPos.y + canvasSize.y * i / 4.0f;
            drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y),
                              IM_COL32(60, 60, 60, 255));
            drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y),
                              IM_COL32(60, 60, 60, 255));
        }

        ImVec2 prevPoint;
        for (int i = 0; i <= 100; i++) {
            float t = i / 100.0f;
            float output = curve.evaluate(t);
            float x = canvasPos.x + t * canvasSize.x;
            float y = canvasPos.y + canvasSize.y - (output * canvasSize.y / 2.0f);
            y = std::max(canvasPos.y, std::min(canvasPos.y + canvasSize.y, y));

            if (i > 0) {
                drawList->AddLine(prevPoint, ImVec2(x, y), IM_COL32(100, 200, 100, 255), 2.0f);
            }
            prevPoint = ImVec2(x, y);
        }

        static int draggingPoint = -1;
        ImGuiIO& curveIO = ImGui::GetIO();

        for (int i = 0; i < curve.numPoints; i++) {
            float px = canvasPos.x + curve.points[i].x * canvasSize.x;
            float py = canvasPos.y + canvasSize.y - (curve.points[i].y * canvasSize.y / 2.0f);
            py = std::max(canvasPos.y, std::min(canvasPos.y + canvasSize.y, py));

            ImVec2 pointPos(px, py);
            float pointRadius = 8.0f;

            bool hovered = canvasHovered &&
                (curveIO.MousePos.x >= px - pointRadius && curveIO.MousePos.x <= px + pointRadius &&
                 curveIO.MousePos.y >= py - pointRadius && curveIO.MousePos.y <= py + pointRadius);

            if (hovered && ImGui::IsMouseClicked(0)) {
                draggingPoint = i;
            }

            ImU32 pointColor = hovered || draggingPoint == i
                ? IM_COL32(255, 255, 100, 255)
                : IM_COL32(255, 200, 100, 255);
            drawList->AddCircleFilled(pointPos, pointRadius, pointColor);
            drawList->AddCircle(pointPos, pointRadius, IM_COL32(255, 255, 255, 255));
        }

        if (draggingPoint >= 0 && ImGui::IsMouseDown(0)) {
            float newX = (curveIO.MousePos.x - canvasPos.x) / canvasSize.x;
            float newY = (canvasPos.y + canvasSize.y - curveIO.MousePos.y) / canvasSize.y * 2.0f;

            if (draggingPoint == 0) {
                newX = 0.0f;
            } else if (draggingPoint == curve.numPoints - 1) {
                newX = 1.0f;
            } else {
                newX = std::max(curve.points[draggingPoint-1].x + 0.01f,
                               std::min(curve.points[draggingPoint+1].x - 0.01f, newX));
            }

            newY = std::max(0.0f, std::min(2.0f, newY));

            curve.points[draggingPoint].x = newX;
            curve.points[draggingPoint].y = newY;
        }

        if (ImGui::IsMouseReleased(0)) {
            draggingPoint = -1;
        }

        if (normalizedInput > 0) {
            float indicatorX = canvasPos.x + std::min(1.0f, normalizedInput) * canvasSize.x;
            drawList->AddLine(ImVec2(indicatorX, canvasPos.y),
                             ImVec2(indicatorX, canvasPos.y + canvasSize.y),
                             IM_COL32(255, 100, 100, 200), 2.0f);
        }

        ImGui::Text("X: Input Speed (0=stopped, 1=max RPM)");
        ImGui::Text("Y: Output Multiplier (0=none, 1=normal, 2=2x)");

        ImGui::Separator();

        if (ImGui::Button("Linear")) {
            curve.points[0] = {0.0f, 0.0f};
            curve.points[1] = {0.33f, 0.33f};
            curve.points[2] = {0.66f, 0.66f};
            curve.points[3] = {1.0f, 1.0f};
            curve.numPoints = 4;
        }
        ImGui::SameLine();
        if (ImGui::Button("Slow Start")) {
            curve.points[0] = {0.0f, 0.0f};
            curve.points[1] = {0.33f, 0.1f};
            curve.points[2] = {0.66f, 0.5f};
            curve.points[3] = {1.0f, 1.0f};
            curve.numPoints = 4;
        }
        ImGui::SameLine();
        if (ImGui::Button("Fast Start")) {
            curve.points[0] = {0.0f, 0.0f};
            curve.points[1] = {0.33f, 0.6f};
            curve.points[2] = {0.66f, 0.9f};
            curve.points[3] = {1.0f, 1.0f};
            curve.numPoints = 4;
        }
        ImGui::SameLine();
        if (ImGui::Button("Precision")) {
            curve.points[0] = {0.0f, 0.0f};
            curve.points[1] = {0.5f, 0.2f};
            curve.points[2] = {0.8f, 0.5f};
            curve.points[3] = {1.0f, 1.5f};
            curve.numPoints = 4;
        }
    }
    ImGui::End();
#endif
}

void GUIManager::renderTestPage() {
#ifdef IMGUI_FOUND
    if (!m_showTestPage) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Hardware Test Page", &m_showTestPage, ImGuiWindowFlags_NoCollapse)) {
        double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        ImGui::Text("ENCODERS");
        ImGui::Separator();
        for (int i = 0; i < 6; i++) {
            if (i > 0) ImGui::SameLine();
            double lastAct = m_serialController->getEncoderLastActivity(i);
            bool active = (now - lastAct) < 0.3;
            long lastDelta = m_serialController->getEncoderLastDelta(i);

            ImVec2 boxSize(90, 70);
            ImVec2 cursor = ImGui::GetCursorScreenPos();

            ImU32 bgColor = active ? IM_COL32(220, 200, 0, 255) : IM_COL32(60, 60, 60, 255);
            ImU32 textColor = active ? IM_COL32(0, 0, 0, 255) : IM_COL32(160, 160, 160, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(cursor,
                ImVec2(cursor.x + boxSize.x, cursor.y + boxSize.y), bgColor, 6.0f);
            ImGui::GetWindowDrawList()->AddRect(cursor,
                ImVec2(cursor.x + boxSize.x, cursor.y + boxSize.y),
                IM_COL32(100, 100, 100, 255), 6.0f);

            char label[16];
            snprintf(label, sizeof(label), "E%d", i + 1);
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(cursor.x + (boxSize.x - labelSize.x) * 0.5f, cursor.y + 8),
                textColor, label);

            if (active) {
                char deltaStr[32];
                snprintf(deltaStr, sizeof(deltaStr), "%+ld", lastDelta);
                ImVec2 deltaSize = ImGui::CalcTextSize(deltaStr);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(cursor.x + (boxSize.x - deltaSize.x) * 0.5f, cursor.y + 40),
                    textColor, deltaStr);
            }

            ImGui::Dummy(boxSize);
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("TOUCHPADS (0-26)");
        ImGui::Separator();

        for (int pad = 0; pad < 27; pad++) {
            int col = pad % 9;
            if (col > 0) ImGui::SameLine();

            bool touched = m_serialController->isTouched(pad);
            ImVec2 boxSize(60, 50);
            ImVec2 cursor = ImGui::GetCursorScreenPos();

            ImU32 bgColor = touched ? IM_COL32(220, 200, 0, 255) : IM_COL32(60, 60, 60, 255);
            ImU32 textColor = touched ? IM_COL32(0, 0, 0, 255) : IM_COL32(160, 160, 160, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(cursor,
                ImVec2(cursor.x + boxSize.x, cursor.y + boxSize.y), bgColor, 4.0f);
            ImGui::GetWindowDrawList()->AddRect(cursor,
                ImVec2(cursor.x + boxSize.x, cursor.y + boxSize.y),
                IM_COL32(100, 100, 100, 255), 4.0f);

            char label[8];
            snprintf(label, sizeof(label), "%d", pad);
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(cursor.x + (boxSize.x - labelSize.x) * 0.5f,
                       cursor.y + (boxSize.y - labelSize.y) * 0.5f),
                textColor, label);

            ImGui::Dummy(boxSize);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Serial: %s  |  MPR121 chips: 3  |  Encoders: 6",
                    m_serialController->isConnected() ? "Connected" : "Disconnected");
    }
    ImGui::End();
#endif
}

void GUIManager::applyColorScheme(int scheme) {
#ifdef IMGUI_FOUND
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    if (scheme == 0) {
        ImGui::StyleColorsDark();
    } else if (scheme == 1) {
        ImVec4 bg = ImVec4(m_customBgColor[0], m_customBgColor[1], m_customBgColor[2], 1.0f);
        ImVec4 fg = ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f);
        ImVec4 transparent = ImVec4(0, 0, 0, 0);

        colors[ImGuiCol_Text] = fg;
        colors[ImGuiCol_TextDisabled] = fg;
        colors[ImGuiCol_WindowBg] = bg;
        colors[ImGuiCol_ChildBg] = bg;
        colors[ImGuiCol_PopupBg] = bg;
        colors[ImGuiCol_Border] = fg;
        colors[ImGuiCol_BorderShadow] = transparent;
        colors[ImGuiCol_FrameBg] = bg;
        colors[ImGuiCol_FrameBgHovered] = bg;
        colors[ImGuiCol_FrameBgActive] = bg;
        colors[ImGuiCol_TitleBg] = bg;
        colors[ImGuiCol_TitleBgActive] = bg;
        colors[ImGuiCol_TitleBgCollapsed] = bg;
        colors[ImGuiCol_MenuBarBg] = bg;
        colors[ImGuiCol_ScrollbarBg] = bg;
        colors[ImGuiCol_ScrollbarGrab] = fg;
        colors[ImGuiCol_ScrollbarGrabHovered] = fg;
        colors[ImGuiCol_ScrollbarGrabActive] = fg;
        colors[ImGuiCol_CheckMark] = fg;
        colors[ImGuiCol_SliderGrab] = fg;
        colors[ImGuiCol_SliderGrabActive] = fg;
        colors[ImGuiCol_Button] = bg;
        colors[ImGuiCol_ButtonHovered] = bg;
        colors[ImGuiCol_ButtonActive] = bg;
        colors[ImGuiCol_Header] = bg;
        colors[ImGuiCol_HeaderHovered] = bg;
        colors[ImGuiCol_HeaderActive] = bg;
        colors[ImGuiCol_Separator] = fg;
        colors[ImGuiCol_SeparatorHovered] = fg;
        colors[ImGuiCol_SeparatorActive] = fg;
        colors[ImGuiCol_ResizeGrip] = fg;
        colors[ImGuiCol_ResizeGripHovered] = fg;
        colors[ImGuiCol_ResizeGripActive] = fg;
        colors[ImGuiCol_Tab] = bg;
        colors[ImGuiCol_TabHovered] = bg;
        colors[ImGuiCol_TabActive] = bg;
        colors[ImGuiCol_TabUnfocused] = bg;
        colors[ImGuiCol_TabUnfocusedActive] = bg;
        colors[ImGuiCol_PlotLines] = fg;
        colors[ImGuiCol_PlotLinesHovered] = fg;
        colors[ImGuiCol_PlotHistogram] = fg;
        colors[ImGuiCol_PlotHistogramHovered] = fg;
        colors[ImGuiCol_TableHeaderBg] = bg;
        colors[ImGuiCol_TableBorderStrong] = fg;
        colors[ImGuiCol_TableBorderLight] = fg;
        colors[ImGuiCol_TableRowBg] = bg;
        colors[ImGuiCol_TableRowBgAlt] = bg;
        colors[ImGuiCol_TextSelectedBg] = fg;
        colors[ImGuiCol_DragDropTarget] = fg;
        colors[ImGuiCol_NavHighlight] = fg;
        colors[ImGuiCol_NavWindowingHighlight] = fg;
        colors[ImGuiCol_NavWindowingDimBg] = bg;
        colors[ImGuiCol_ModalWindowDimBg] = bg;
    }
#endif
}

void GUIManager::saveSettings() {
    // Create settings directory if it doesn't exist
    #ifdef _WIN32
    system("if not exist \"c:\\0_CODE\\Dogma75\\settings\" mkdir \"c:\\0_CODE\\Dogma75\\settings\"");
    #endif

    std::ofstream file("c:\\0_CODE\\Dogma75\\settings\\user_settings.json");
    if (!file.is_open()) {
        std::cerr << "Failed to save settings!" << std::endl;
        return;
    }

    // Get audio engine settings
    float scrubSpeed = m_audioEngine ? m_audioEngine->getScrubSpeed() : 1.0f;
    float silentScrubSpeed = m_audioEngine ? m_audioEngine->getSilentScrubSpeed() : 0.7f;
    float rpmThreshold = m_audioEngine ? m_audioEngine->getScrubRpmThreshold() : 30.0f;
    float fastSpeedMult = m_audioEngine ? m_audioEngine->getFastSpeedMultiplier() : 4.0f;
    float rpmAveraging = m_audioEngine ? m_audioEngine->getRpmAveraging() : 0.7f;

    file << "{\n";
    file << "  \"_comment\": \"Dogma75 User Settings - Give this file to Claude to update defaults\",\n";
    file << "  \"_saved_at\": \"" << __DATE__ << " " << __TIME__ << "\",\n";
    file << "\n";
    file << "  \"gui\": {\n";
    file << "    \"colorScheme\": " << m_colorScheme << ",\n";
    file << "    \"customBgColor\": [" << m_customBgColor[0] << ", " << m_customBgColor[1] << ", " << m_customBgColor[2] << "],\n";
    file << "    \"customTextColor\": [" << m_customTextColor[0] << ", " << m_customTextColor[1] << ", " << m_customTextColor[2] << "],\n";
    file << "    \"bloomEnabled\": " << (m_bloomEnabled ? "true" : "false") << ",\n";
    file << "    \"bloomTextIntensity\": " << m_bloomTextIntensity << ",\n";
    file << "    \"bloomLinesIntensity\": " << m_bloomLinesIntensity << ",\n";
    file << "    \"bloomUIIntensity\": " << m_bloomUIIntensity << ",\n";
    file << "    \"simplifiedWaveform\": " << (m_simplifiedWaveform ? "true" : "false") << ",\n";
    file << "    \"zoomSmoothing\": " << (m_zoomSmoothing ? "true" : "false") << ",\n";
    file << "    \"waveformScrolling\": " << (m_waveformScrolling ? "true" : "false") << ",\n";
    file << "    \"waveformAutoPage\": " << (m_waveformAutoPage ? "true" : "false") << ",\n";
    file << "    \"waveformVerticalZoom\": " << m_waveformVerticalZoom << ",\n";
    file << "    \"trackHeight\": " << m_trackHeight << ",\n";
    file << "    \"controllerMode\": " << m_controllerMode << "\n";
    file << "  },\n";
    file << "\n";
    file << "  \"audio\": {\n";
    file << "    \"scrubSpeed\": " << scrubSpeed << ",\n";
    file << "    \"silentScrubSpeed\": " << silentScrubSpeed << ",\n";
    file << "    \"rpmThreshold\": " << rpmThreshold << ",\n";
    file << "    \"fastSpeedMultiplier\": " << fastSpeedMult << ",\n";
    file << "    \"rpmAveraging\": " << rpmAveraging << "\n";
    file << "  },\n";
    file << "\n";
    file << "  \"parkButtons\": {\n";
    file << "    \"park1\": \"" << m_parkNames[0] << "\",\n";
    file << "    \"park2\": \"" << m_parkNames[1] << "\",\n";
    file << "    \"park3\": \"" << m_parkNames[2] << "\",\n";
    file << "    \"park4\": \"" << m_parkNames[3] << "\",\n";
    file << "    \"selectedPark\": " << m_selectedParkButton << "\n";
    file << "  },\n";
    file << "\n";
    // Sticky per-dialog last folders. JSON strings; backslashes escaped
    // so Windows paths round-trip cleanly through the ad-hoc parser.
    auto jsonEscape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\\' || c == '"') { out += '\\'; out += c; }
            else out += c;
        }
        return out;
    };
    file << "  \"dialogs\": {\n";
    file << "    \"lastAudioDir\":   \"" << jsonEscape(m_lastAudioDir)   << "\",\n";
    file << "    \"lastSessionDir\": \"" << jsonEscape(m_lastSessionDir) << "\"\n";
    file << "  },\n";
    file << "\n";

    // Recently-opened sessions, newest first, for File > Recent Projects.
    file << "  \"recentSessions\": [";
    for (size_t i = 0; i < m_recentSessions.size(); i++) {
        file << "\"" << jsonEscape(m_recentSessions[i]) << "\"";
        if (i + 1 < m_recentSessions.size()) file << ", ";
    }
    file << "],\n";
    file << "\n";

    // Per-channel PCA9685 LED brightness (0.0 dark .. 1.0 max), pushed to
    // the controller as inverted 0-4095 PCA values on startup and on drag.
    file << "  \"ledBrightness\": [";
    for (int i = 0; i < 9; i++) {
        file << m_ledBrightness[i];
        if (i < 8) file << ", ";
    }
    file << "],\n";
    file << "\n";

    // Save velocity curve settings
    if (m_serialController) {
        const VelocityCurve& curve = m_serialController->getVelocityCurve();
        file << "  \"velocityCurve\": {\n";
        file << "    \"enabled\": " << (curve.enabled ? "true" : "false") << ",\n";
        file << "    \"smoothed\": " << (curve.smoothed ? "true" : "false") << ",\n";
        file << "    \"maxInputRpm\": " << curve.maxInputRpm << ",\n";
        file << "    \"rpmWindowMs\": " << curve.rpmWindowMs << ",\n";
        file << "    \"maxMultiplier\": " << curve.maxMultiplier << ",\n";
        file << "    \"baseMultiplier\": " << curve.baseMultiplier << ",\n";
        file << "    \"numPoints\": " << curve.numPoints << ",\n";
        file << "    \"points\": [\n";
        for (int i = 0; i < curve.numPoints; i++) {
            file << "      {\"x\": " << curve.points[i].x << ", \"y\": " << curve.points[i].y << "}";
            if (i < curve.numPoints - 1) file << ",";
            file << "\n";
        }
        file << "    ]\n";
        file << "  }\n";
    }

    file << "}\n";

    file.close();
    std::cout << "Settings saved to c:\\0_CODE\\Dogma75\\settings\\user_settings.json" << std::endl;
}

void GUIManager::saveSession() {
    // Overwrite the current session file with no prompt. Only falls back
    // to Save As if we don't yet have a target path (fresh session, or a
    // session loaded from disk hasn't been re-saved yet — actually load
    // sets m_currentSessionPath too, so in practice this branch fires
    // only when nothing has been opened or saved this run).
    if (m_currentSessionPath.empty()) {
        saveSessionAs();
        return;
    }
    saveSessionToPath(m_currentSessionPath);
}

// Standard home for sessions. A brand-new session (nothing opened or
// saved yet, so m_currentSessionPath is empty) always offers to save
// here; once a session lives somewhere else, that becomes its home and
// Save As reopens there instead.
static const char kDefaultSessionDir[] =
    "c:\\0_CODE\\Dogma75\\Workspace\\SESSIONS";

#ifdef _WIN32
// Folder the session dialogs should open in.
static std::string sessionDialogDir(const std::string& currentSessionPath) {
    if (!currentSessionPath.empty()) {
        size_t slash = currentSessionPath.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string dir = currentSessionPath.substr(0, slash);
            DWORD attrs = GetFileAttributesA(dir.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                return dir;
            }
            // Fall through if the session's folder has since been moved
            // or deleted — better the default than a dead path.
        }
    }
    // Create on demand: the common-dialog silently IGNORES an
    // lpstrInitialDir that doesn't exist and drops the user next to the
    // .exe instead, which is what used to happen with the stale paths
    // carried over in user_settings.json.
    CreateDirectoryA("c:\\0_CODE\\Dogma75\\Workspace", nullptr);
    CreateDirectoryA(kDefaultSessionDir, nullptr);
    return kDefaultSessionDir;
}
#endif

// Move `path` to the front of the recent list, dropping any earlier entry
// for the same file so re-opening a session doesn't duplicate it. Paths are
// compared case-insensitively because Windows treats them that way and the
// same session can easily arrive with different casing from the dialog, the
// hardcoded startup path, and a Save As.
void GUIManager::addRecentSession(const std::string& path) {
    if (path.empty()) return;

    auto sameFile = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            char ca = a[i], cb = b[i];
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            // Treat / and \ as equivalent separators.
            if (ca == '/') ca = '\\';
            if (cb == '/') cb = '\\';
            if (ca != cb) return false;
        }
        return true;
    };

    m_recentSessions.erase(
        std::remove_if(m_recentSessions.begin(), m_recentSessions.end(),
                       [&](const std::string& e) { return sameFile(e, path); }),
        m_recentSessions.end());
    m_recentSessions.insert(m_recentSessions.begin(), path);
    if (m_recentSessions.size() > MAX_RECENT_SESSIONS) {
        m_recentSessions.resize(MAX_RECENT_SESSIONS);
    }
    saveSettings();
}

void GUIManager::saveSessionAs() {
#ifdef _WIN32
    DialogFullscreenGuard fsGuard(m_window);
    const std::string initialDir = sessionDialogDir(m_currentSessionPath);
    char filename[MAX_PATH] = "session.json";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = sdlWindowHwnd(m_window);
    ofn.lpstrFilter = "DAW session (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = sizeof(filename);
    ofn.lpstrTitle  = "Save DAW Session As";
    ofn.lpstrDefExt = "json";
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return;
    {
        const char* slash = strrchr(filename, '\\');
        if (slash) m_lastSessionDir.assign(filename, slash - filename);
        saveSettings();
    }
    saveSessionToPath(filename);
#endif
}

void GUIManager::saveSessionToPath(const std::string& filenameStr) {
#ifdef _WIN32
    const char* filename = filenameStr.c_str();
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Session save failed: " << filename << std::endl;
        return;
    }

    int trackCount = m_audioEngine->getTrackCount();
    file << "{\n";
    file << "  \"selectedTrack\":       " << m_audioEngine->getSelectedTrack()   << ",\n";
    file << "  \"playbackPosition\":    " << m_audioEngine->getPlaybackPosition()<< ",\n";
    file << "  \"waveformZoom\":        " << m_audioEngine->getWaveformZoom()    << ",\n";
    file << "  \"viewCenter\":          " << m_viewCenterPosition                << ",\n";
    file << "  \"waveformVerticalZoom\":" << m_waveformVerticalZoom              << ",\n";
    file << "  \"trackHeight\":         " << m_trackHeight                       << ",\n";
    file << "  \"simplifiedWaveform\":  " << (m_simplifiedWaveform ? "true" : "false") << ",\n";
    file << "  \"zoomSmoothing\":       " << (m_zoomSmoothing      ? "true" : "false") << ",\n";
    file << "  \"waveformScrolling\":   " << (m_waveformScrolling  ? "true" : "false") << ",\n";
    file << "  \"waveformAutoPage\":    " << (m_waveformAutoPage   ? "true" : "false") << ",\n";
    file << "  \"leftPanelMode\":       " << m_leftPanelMode                     << ",\n";
    file << "  \"silentScrubSpeed\":    " << m_audioEngine->getSilentScrubSpeed()<< ",\n";
    file << "  \"scrubSpeed\":          " << m_audioEngine->getScrubSpeed()      << ",\n";
    file << "  \"loopEnabled\":         " << (m_audioEngine->getLoopEnabled()          ? "true" : "false") << ",\n";
    file << "  \"loopPlayback\":        " << (m_audioEngine->getLoopPlaybackEnabled()  ? "true" : "false") << ",\n";
    file << "  \"recordEnabled\":       " << (m_audioEngine->getRecordEnabled()        ? "true" : "false") << ",\n";
    file << "  \"returnToStartOnStop\": " << (m_audioEngine->getReturnToStartOnStop()  ? "true" : "false") << ",\n";
    file << "  \"totalMixMuted\":       " << (m_audioEngine->getTotalMixInputPairMuted() ? "true" : "false") << ",\n";
    file << "  \"markers\": [\n";
    for (int mi = 0; mi < 4; mi++) {
        // "everSet" round-trips whether the marker has EVER been placed,
        // so a session with no record loop restores to no record loop
        // (and the loop-edit LED stays dark instead of flashing).
        file << "    { \"position\": " << m_audioEngine->getMarkerPosition(mi)
             << ", \"enabled\": " << (m_audioEngine->isMarkerEnabled(mi) ? "true" : "false")
             << ", \"everSet\": " << (m_audioEngine->markerEverSet(mi)   ? "true" : "false")
             << " }" << (mi < 3 ? "," : "") << "\n";
    }
    file << "  ],\n";
    file << "  \"tracks\": [\n";
    for (int i = 0; i < trackCount; i++) {
        const Track* t = m_audioEngine->getTrack(i);
        if (!t) continue;
        auto escape = [](const std::string& s) {
            std::string out;
            for (char c : s) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (c == '\n') out += "\\n";
                else out += c;
            }
            return out;
        };
        file << "    {\n";
        file << "      \"name\":       \"" << escape(t->name)     << "\",\n";
        file << "      \"filePath\":   \"" << escape(t->filePath) << "\",\n";
        file << "      \"volume\":     " << t->volume      << ",\n";
        file << "      \"pan\":        " << t->pan         << ",\n";
        file << "      \"muted\":      " << (t->muted ? "true" : "false") << ",\n";
        file << "      \"solo\":       " << (t->solo  ? "true" : "false") << ",\n";
        file << "      \"armed\":      " << (t->armed ? "true" : "false") << ",\n";
        file << "      \"outputPair\":    " << t->outputPair << ",\n";
        file << "      \"outputMono\":    " << (t->outputMono ? "true" : "false") << ",\n";
        file << "      \"outputMonoChan\":" << t->outputMonoChan << ",\n";
        file << "      \"inputPair\":     " << t->inputPair  << ",\n";
        file << "      \"inputMono\":     " << (t->inputMono ? "true" : "false") << ",\n";
        file << "      \"inputMonoChan\": " << t->inputMonoChan << ",\n";
        file << "      \"inputMonitor\":  " << (t->inputMonitor ? "true" : "false") << "\n";
        file << "    }" << (i + 1 < trackCount ? "," : "") << "\n";
    }
    file << "  ],\n";

    // Bookmarks — timeline markers dropped via pad 13. Each entry has
    // a frame position + a user-typed name. Read back on session load.
    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n')         out += "\\n";
            else                        out += c;
        }
        return out;
    };
    auto bookmarks = m_audioEngine->getBookmarks();
    file << "  \"bookmarks\": [\n";
    for (size_t bi = 0; bi < bookmarks.size(); bi++) {
        file << "    { \"frame\": " << bookmarks[bi].frame
             << ", \"name\": \"" << escape(bookmarks[bi].name) << "\" }"
             << (bi + 1 < bookmarks.size() ? "," : "") << "\n";
    }
    file << "  ]\n";
    file << "}\n";
    file.close();
    std::cout << "Session saved to " << filename << std::endl;
    dawLog("Session SAVED: %s (totalMixMuted=%s)",
           filename,
           m_audioEngine->getTotalMixInputPairMuted() ? "true" : "false");
    m_currentSessionPath = filename;   // enables Revert
    addRecentSession(m_currentSessionPath);
    if (m_audioEngine) {
        m_audioEngine->clearSessionDirty();
        // Tell the engine where to write recorded takes.
        std::string sdir = filename;
        size_t slash = sdir.find_last_of("/\\");
        if (slash != std::string::npos) sdir.resize(slash);
        m_audioEngine->setSessionDir(sdir);
    }
#endif
}

void GUIManager::openSession() {
#ifdef _WIN32
    DialogFullscreenGuard fsGuard(m_window);
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = sdlWindowHwnd(m_window);
    ofn.lpstrFilter = "DAW session (*.json)\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = sizeof(filename);
    ofn.lpstrTitle  = "Open DAW Session";
    ofn.lpstrDefExt = "json";
    ofn.lpstrInitialDir = m_lastSessionDir.empty() ? nullptr
                                                   : m_lastSessionDir.c_str();
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return;
    {
        const char* slash = strrchr(filename, '\\');
        if (slash) m_lastSessionDir.assign(filename, slash - filename);
        saveSettings();
    }
    loadSessionFromFile(filename);
    // User-triggered open — the loaded mute state IS authoritative here.
    // Push it to TotalMix + the OLED indicator (loadSessionFromFile
    // deliberately leaves this to the caller so startup auto-load can
    // opt out).
    if (m_audioEngine) {
        m_audioEngine->syncTotalMixMuteToHardware();
        m_audioEngine->syncAllInputMonitorsToAntelope();
    }
#endif
}

void GUIManager::loadSessionFromFile(const std::string& path) {
#ifdef _WIN32
    const char* filename = path.c_str();
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Session open failed: " << filename << std::endl;
        return;
    }
    std::stringstream buf; buf << file.rdbuf();
    std::string json = buf.str();

    // Minimal JSON walker — we only read strings, numbers, and booleans from
    // fixed keys the saver wrote. Each track object starts at a "{" nested
    // inside the "tracks" array; we scan sequentially.
    auto findKeyAfter = [&](const std::string& key, size_t from) -> size_t {
        return json.find("\"" + key + "\"", from);
    };
    auto readString = [&](size_t keyPos) -> std::string {
        size_t colon = json.find(':', keyPos);
        if (colon == std::string::npos) return "";
        size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) return "";
        std::string out;
        for (size_t i = q1 + 1; i < json.size(); i++) {
            char c = json[i];
            if (c == '\\' && i + 1 < json.size()) { out += json[i + 1]; i++; continue; }
            if (c == '"') break;
            out += c;
        }
        return out;
    };
    auto readNumber = [&](size_t keyPos) -> double {
        size_t colon = json.find(':', keyPos);
        if (colon == std::string::npos) return 0.0;
        return atof(json.c_str() + colon + 1);
    };
    auto readBool = [&](size_t keyPos) -> bool {
        size_t colon = json.find(':', keyPos);
        if (colon == std::string::npos) return false;
        size_t t = json.find_first_not_of(" \t\r\n", colon + 1);
        return t != std::string::npos && json[t] == 't';
    };

    // Clear existing tracks (delete from the back so indices stay valid).
    for (int i = m_audioEngine->getTrackCount() - 1; i >= 0; i--) {
        m_audioEngine->deleteTrack(i);
    }

    size_t tracksKey = json.find("\"tracks\"");
    size_t cursor    = (tracksKey == std::string::npos) ? std::string::npos : tracksKey;
    while (cursor != std::string::npos) {
        size_t objStart = json.find('{', cursor);
        if (objStart == std::string::npos) break;
        size_t objEnd = json.find('}', objStart);
        if (objEnd == std::string::npos) break;

        // Read each field only within [objStart, objEnd].
        auto keyIn = [&](const std::string& key) -> size_t {
            size_t p = findKeyAfter(key, objStart);
            return (p != std::string::npos && p < objEnd) ? p : std::string::npos;
        };

        std::string name     = keyIn("name")     != std::string::npos ? readString(keyIn("name"))     : "";
        std::string filePath = keyIn("filePath") != std::string::npos ? readString(keyIn("filePath")) : "";
        double vol           = keyIn("volume")     != std::string::npos ? readNumber(keyIn("volume"))     : 1.0;
        double pan           = keyIn("pan")        != std::string::npos ? readNumber(keyIn("pan"))        : 0.0;
        bool muted           = keyIn("muted")      != std::string::npos ? readBool(keyIn("muted"))        : false;
        bool solo            = keyIn("solo")       != std::string::npos ? readBool(keyIn("solo"))         : false;
        bool armed           = keyIn("armed")      != std::string::npos ? readBool(keyIn("armed"))        : false;
        int outputPair       = keyIn("outputPair") != std::string::npos ? (int)readNumber(keyIn("outputPair")) : 0;
        bool outputMono      = keyIn("outputMono") != std::string::npos ? readBool(keyIn("outputMono"))         : false;
        int outputMonoChan   = keyIn("outputMonoChan") != std::string::npos ? (int)readNumber(keyIn("outputMonoChan")) : 0;
        int inputPair        = keyIn("inputPair")  != std::string::npos ? (int)readNumber(keyIn("inputPair"))  : 0;
        bool inputMono       = keyIn("inputMono")  != std::string::npos ? readBool(keyIn("inputMono"))         : false;
        int inputMonoChan    = keyIn("inputMonoChan") != std::string::npos ? (int)readNumber(keyIn("inputMonoChan")) : 0;
        bool inputMonitor    = keyIn("inputMonitor") != std::string::npos ? readBool(keyIn("inputMonitor"))    : false;

        int newIdx = m_audioEngine->addTrack(name);
        Track* t = m_audioEngine->getTrack(newIdx);
        if (t) {
            t->name          = name;   // preserve exactly (addTrack may have munged)
            t->volume        = (float)vol;
            t->pan           = (float)pan;
            t->muted         = muted;
            t->solo          = solo;
            t->armed         = armed;
            t->outputPair    = outputPair;
            t->outputMono    = outputMono;
            t->outputMonoChan= outputMonoChan;
            t->inputPair     = inputPair;
            t->inputMono     = inputMono;
            t->inputMonoChan = inputMonoChan;
            t->inputMonitor  = inputMonitor;
        }
        if (!filePath.empty()) {
            m_audioEngine->loadTrackAudio(newIdx, filePath);
            if (t) t->name = name;   // load must not change the stored name
        }

        cursor = objEnd + 1;
        // Stop if the next non-space char is ']' (end of tracks array).
        size_t nxt = json.find_first_not_of(" \t\r\n,", cursor);
        if (nxt == std::string::npos || json[nxt] == ']') break;
    }

    // Top-level scalar restore. Each is optional — missing keys leave the
    // current value in place, which is the right behaviour for older
    // session files that pre-date some of these fields.
    auto readTop = [&](const std::string& key) -> size_t {
        return findKeyAfter(key, 0);
    };
    size_t k;
    if ((k = readTop("playbackPosition"))     != std::string::npos) m_audioEngine->setPlaybackPosition((size_t)readNumber(k));
    if ((k = readTop("waveformZoom"))         != std::string::npos) m_audioEngine->setWaveformZoom((float)readNumber(k));
    if ((k = readTop("viewCenter"))           != std::string::npos) m_viewCenterPosition = (size_t)readNumber(k);
    if ((k = readTop("waveformVerticalZoom")) != std::string::npos) m_waveformVerticalZoom = (float)readNumber(k);
    if ((k = readTop("trackHeight"))          != std::string::npos) m_trackHeight          = (float)readNumber(k);
    if ((k = readTop("simplifiedWaveform"))   != std::string::npos) m_simplifiedWaveform   = readBool(k);
    if ((k = readTop("zoomSmoothing"))        != std::string::npos) m_zoomSmoothing        = readBool(k);
    if ((k = readTop("waveformScrolling"))    != std::string::npos) m_waveformScrolling    = readBool(k);
    if ((k = readTop("waveformAutoPage"))     != std::string::npos) m_waveformAutoPage     = readBool(k);
    if ((k = readTop("leftPanelMode"))        != std::string::npos) m_leftPanelMode        = (int)readNumber(k);
    if ((k = readTop("silentScrubSpeed"))     != std::string::npos) m_audioEngine->setSilentScrubSpeed((float)readNumber(k));
    if ((k = readTop("scrubSpeed"))           != std::string::npos) m_audioEngine->setScrubSpeed((float)readNumber(k));
    if ((k = readTop("loopEnabled"))          != std::string::npos) m_audioEngine->setLoopEnabled(readBool(k));
    // Absent in sessions saved before loop playback became separable from
    // the markers — default ON so they behave as they always did.
    m_audioEngine->setLoopPlaybackEnabled(
        (k = readTop("loopPlayback")) != std::string::npos ? readBool(k) : true);
    if ((k = readTop("recordEnabled"))        != std::string::npos) m_audioEngine->setRecordEnabled(readBool(k));
    if ((k = readTop("returnToStartOnStop"))  != std::string::npos) m_audioEngine->setReturnToStartOnStop(readBool(k));

    // Markers array — walk it the same way as the tracks array.
    size_t markersKey = json.find("\"markers\"");
    if (markersKey != std::string::npos) {
        size_t mcur = markersKey;
        for (int mi = 0; mi < 4; mi++) {
            size_t objStart = json.find('{', mcur);
            if (objStart == std::string::npos) break;
            size_t objEnd = json.find('}', objStart);
            if (objEnd == std::string::npos) break;
            auto mKeyIn = [&](const std::string& key) -> size_t {
                size_t p = findKeyAfter(key, objStart);
                return (p != std::string::npos && p < objEnd) ? p : std::string::npos;
            };
            size_t pos = mKeyIn("position") != std::string::npos ? (size_t)readNumber(mKeyIn("position")) : 0;
            bool   en  = mKeyIn("enabled")  != std::string::npos ? readBool(mKeyIn("enabled"))            : false;
            // Prefer the explicit "everSet" field written by newer saves.
            // For older sessions without it, infer: a marker at position 0
            // with enabled=false was almost certainly never placed.
            bool ever;
            if (mKeyIn("everSet") != std::string::npos) {
                ever = readBool(mKeyIn("everSet"));
            } else {
                ever = (pos != 0) || en;
            }
            if (ever) m_audioEngine->setMarker(mi, pos, en);
            else      m_audioEngine->resetMarker(mi);
            mcur = objEnd + 1;
        }
    }

    // OSC mute shadow — restore silently (no OSC push here; the syncTotalMix
    // call at the end of this function forces external state to match).
    {
        size_t k = json.find("\"totalMixMuted\"");
        if (k != std::string::npos) {
            size_t colon = json.find(':', k);
            if (colon != std::string::npos) {
                size_t v = json.find_first_not_of(" \t\r\n", colon + 1);
                if (v != std::string::npos) {
                    bool muted = json.compare(v, 4, "true") == 0;
                    m_audioEngine->setTotalMixInputPairMuted(muted);
                }
            }
        }
    }

    // Bookmarks array — clear then repopulate.
    m_audioEngine->clearBookmarks();
    size_t bookmarksKey = json.find("\"bookmarks\"");
    if (bookmarksKey != std::string::npos) {
        size_t arrOpen  = json.find('[', bookmarksKey);
        size_t arrClose = (arrOpen == std::string::npos)
                        ? std::string::npos : json.find(']', arrOpen);
        size_t cur = (arrOpen == std::string::npos) ? std::string::npos : arrOpen;
        while (cur != std::string::npos && cur < arrClose) {
            size_t objStart = json.find('{', cur);
            if (objStart == std::string::npos || objStart >= arrClose) break;
            size_t objEnd = json.find('}', objStart);
            if (objEnd == std::string::npos) break;
            auto bKeyIn = [&](const std::string& key) -> size_t {
                size_t p = findKeyAfter(key, objStart);
                return (p != std::string::npos && p < objEnd) ? p : std::string::npos;
            };
            size_t frame = 0;
            std::string name;
            if (bKeyIn("frame") != std::string::npos) {
                frame = (size_t)readNumber(bKeyIn("frame"));
            }
            size_t namePos = bKeyIn("name");
            if (namePos != std::string::npos) {
                // Walk to the value string and read until the closing quote.
                size_t q1 = json.find('"', json.find(':', namePos));
                if (q1 != std::string::npos) {
                    q1++;
                    size_t q2 = q1;
                    while (q2 < objEnd) {
                        if (json[q2] == '\\' && q2 + 1 < objEnd) { q2 += 2; continue; }
                        if (json[q2] == '"') break;
                        q2++;
                    }
                    name = json.substr(q1, q2 - q1);
                }
            }
            m_audioEngine->addBookmark(frame, name);
            cur = objEnd + 1;
        }
    }

    // Selected track last so it wins over anything the loader did.
    size_t selKey = json.find("\"selectedTrack\"");
    if (selKey != std::string::npos) {
        int sel = (int)readNumber(selKey);
        if (sel >= 0 && sel < m_audioEngine->getTrackCount()) {
            m_audioEngine->setSelectedTrack(sel);
        }
    }

    std::cout << "Session loaded from " << filename << std::endl;
    dawLog("Session LOADED: %s (totalMixMuted=%s)",
           filename,
           m_audioEngine->getTotalMixInputPairMuted() ? "true" : "false");
    m_currentSessionPath = filename;   // enables Revert
    addRecentSession(m_currentSessionPath);
    if (m_audioEngine) {
        m_audioEngine->clearSessionDirty();
        // Tell the engine where to write recorded takes.
        std::string sdir = filename;
        size_t slash = sdir.find_last_of("/\\");
        if (slash != std::string::npos) sdir.resize(slash);
        m_audioEngine->setSessionDir(sdir);
        // Mute-to-hardware sync is intentionally NOT called here. Startup
        // auto-load must always land unmuted regardless of what the file
        // has — only user-triggered opens (openSession / revertSession)
        // are allowed to activate mute. Those paths call sync explicitly
        // right after this returns.
    }
    retimeArrangement();
#endif
}

void GUIManager::revertSession() {
    // Re-load the currently-open session file, discarding any unsaved
    // changes since the last open / save. No-op if no session has been
    // opened this DAW run.
    if (m_currentSessionPath.empty()) return;
    loadSessionFromFile(m_currentSessionPath);
    // Same as openSession — user-triggered load pushes the reloaded mute
    // state to hardware.
    if (m_audioEngine) {
        m_audioEngine->syncTotalMixMuteToHardware();
        m_audioEngine->syncAllInputMonitorsToAntelope();
    }
}

void GUIManager::closeSession() {
    // Wipe all tracks, markers, and per-session state — leaves the DAW
    // running with a blank slate ready for a new session or fresh audio.
    if (!m_audioEngine) return;
    for (int i = m_audioEngine->getTrackCount() - 1; i >= 0; i--) {
        m_audioEngine->deleteTrack(i);
    }
    for (int m = 0; m < 4; m++) m_audioEngine->resetMarker(m);
    m_audioEngine->clearBookmarks();
    m_audioEngine->setLoopEnabled(false);
    m_audioEngine->setRecordEnabled(false);
    m_audioEngine->setPlaybackPosition(0);
    m_currentSessionPath.clear();
    m_audioEngine->clearSessionDirty();
    retimeArrangement();
}

void GUIManager::retimeArrangement() {
    if (!m_audioEngine) return;
    size_t maxFrames = m_audioEngine->getTotalFrames();
    // Fresh sessions default to a 2-minute wide arrangement so the
    // playhead / markers have room to move around before any audio
    // exists. Larger sessions extend to fit their longest track.
    size_t minExtent = (size_t)(m_audioEngine->getSampleRate() * 120.0);
    if (minExtent < 44100 * 120) minExtent = 44100 * 120;   // fallback pre-audio
    m_timelineFrames = (maxFrames > minExtent) ? maxFrames : minExtent;
    // Mirror to the engine so encoder scrub / pan / zoom (which run on
    // the reader thread, unaware of GUI state) can operate on the same
    // extent even when no track has audio.
    m_audioEngine->setTimelineFrames(m_timelineFrames);
}

void GUIManager::uploadWaveformTexture(Track* t) {
    if (!t) return;
    size_t total = t->getTotalFrames();
    // No audio (freshly cleared or never loaded): release any stale GPU
    // handles so subsequent hasAudio() checks and (waveformTex == 0) tests
    // agree, then bail. Marking the version as up-to-date prevents this
    // path from re-firing every frame while the strip stays empty.
    if (total == 0) {
        if (t->waveformTex) {
            GLuint old = (GLuint)t->waveformTex;
            glDeleteTextures(1, &old);
            t->waveformTex = 0;
        }
        if (t->waveformDetailTex) {
            GLuint old = (GLuint)t->waveformDetailTex;
            glDeleteTextures(1, &old);
            t->waveformDetailTex = 0;
            t->waveformDetailW = 0;
            t->waveformDetailH = 0;
            t->waveformDetailFPB = 0;
            t->waveformDetailStart = 0;
            t->waveformDetailEnd = 0;
        }
        t->waveformTexVersion = t->audioVersion;
        t->waveformDetailVersion = t->audioVersion;
        return;
    }
    // Delete previous texture if it exists (audio was reloaded).
    if (t->waveformTex) {
        GLuint old = (GLuint)t->waveformTex;
        glDeleteTextures(1, &old);
        t->waveformTex = 0;
    }
    // Wide, short texture: enough horizontal resolution that GPU linear
    // filtering never shows individual texels even at deep zoom, and just
    // tall enough that vertical scaling looks smooth. 16384 × 256 × 4 =
    // 16 MB per track — comfortable on any GPU from the last decade.
    const int W = 16384;
    const int H = 256;
    std::vector<uint32_t> pixels(W * H, 0);
    for (int x = 0; x < W; x++) {
        size_t fStart = (size_t)((uint64_t)x       * (uint64_t)total / (uint64_t)W);
        size_t fEnd   = (size_t)((uint64_t)(x + 1) * (uint64_t)total / (uint64_t)W);
        if (fStart >= total) continue;
        if (fEnd > total) fEnd = total;
        if (fEnd <= fStart) fEnd = fStart + 1;
        // Reuse the peak pyramid: much faster than raw-sample scan on load.
        Track::PeakBucket peak;
        t->getPeaks(fStart, fEnd, &peak, 1);
        float centerY = H * 0.5f;
        int yTop = (int)(centerY - peak.maxVal * (H * 0.45f));
        int yBot = (int)(centerY - peak.minVal * (H * 0.45f));
        if (yTop < 0)  yTop = 0;
        if (yBot >= H) yBot = H - 1;
        if (yTop > yBot) std::swap(yTop, yBot);
        // Ensure at least one visible pixel so silent regions still show
        // a hairline through the middle.
        if (yBot == yTop) {
            int mid = (int)centerY;
            yTop = mid; yBot = mid;
        }
        for (int y = yTop; y <= yBot; y++) {
            pixels[y * W + x] = 0xFFFFFFFF;   // opaque white; ImGui tints via vertex colour
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t->waveformTex        = tex;
    t->waveformTexW       = W;
    t->waveformTexH       = H;
    t->waveformTexVersion = t->audioVersion;
}

void GUIManager::uploadWaveformDetailTexture(Track* t, size_t startFrame, size_t framesPerBucket) {
    if (!t) return;
    size_t total = t->getTotalFrames();
    if (total == 0 || framesPerBucket == 0) return;
    const int W = 8192;
    const int H = 256;
    // Snap startFrame down to a framesPerBucket boundary — key invariant
    // that keeps texel content identical across rebuilds.
    startFrame = (startFrame / framesPerBucket) * framesPerBucket;
    size_t endFrame = startFrame + (size_t)W * framesPerBucket;
    if (endFrame > total) endFrame = total;
    if (startFrame >= endFrame) return;

    bool needAlloc = (t->waveformDetailTex == 0 ||
                      t->waveformDetailW != W ||
                      t->waveformDetailH != H ||
                      t->waveformDetailVersion != t->audioVersion);
    static thread_local std::vector<uint32_t> pixels;
    if ((int)pixels.size() < W * H) pixels.assign(W * H, 0);
    else std::fill(pixels.begin(), pixels.begin() + W * H, 0);

    // At extreme zoom, framesPerBucket can drop below the peak pyramid's
    // base bucket size (64 samples) — the pyramid would then return the
    // coarser 64-sample level, blurring what should be sample-accurate.
    // Read raw samples directly in that regime so the render stays crisp
    // all the way down to one sample per texel.
    bool rawMode = (framesPerBucket < Track::PEAK_BASE_BUCKET);
    for (int x = 0; x < W; x++) {
        size_t fs = startFrame + (size_t)x       * framesPerBucket;
        size_t fe = fs + framesPerBucket;
        if (fs >= total) break;
        if (fe > total) fe = total;
        float mn = 0.0f, mx = 0.0f;
        if (rawMode) {
            for (size_t f = fs; f < fe; f++) {
                float v = t->getMixedSample(f);
                if (v < mn) mn = v;
                if (v > mx) mx = v;
            }
        } else {
            Track::PeakBucket peak;
            t->getPeaks(fs, fe, &peak, 1);
            mn = peak.minVal; mx = peak.maxVal;
        }
        float centerY = H * 0.5f;
        int yTop = (int)(centerY - mx * (H * 0.45f));
        int yBot = (int)(centerY - mn * (H * 0.45f));
        if (yTop < 0) yTop = 0;
        if (yBot >= H) yBot = H - 1;
        if (yTop > yBot) std::swap(yTop, yBot);
        if (yBot == yTop) { int m = (int)centerY; yTop = m; yBot = m; }
        for (int y = yTop; y <= yBot; y++) pixels[y * W + x] = 0xFFFFFFFF;
    }
    if (needAlloc) {
        if (t->waveformDetailTex) {
            GLuint old = (GLuint)t->waveformDetailTex;
            glDeleteTextures(1, &old);
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        t->waveformDetailTex     = tex;
        t->waveformDetailW       = W;
        t->waveformDetailH       = H;
        t->waveformDetailVersion = t->audioVersion;
    } else {
        glBindTexture(GL_TEXTURE_2D, (GLuint)t->waveformDetailTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    t->waveformDetailStart = startFrame;
    t->waveformDetailEnd   = endFrame;
    t->waveformDetailFPB   = framesPerBucket;
}

void GUIManager::loadSettings() {
    std::ifstream file("c:\\0_CODE\\Dogma75\\settings\\user_settings.json");
    if (!file.is_open()) {
        std::cout << "No settings file found, using defaults" << std::endl;
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    // Simple JSON parsing (no external library needed)
    auto getValue = [&json](const std::string& key) -> std::string {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        size_t end = json.find_first_of(",}\n", pos);
        if (end == std::string::npos) return "";
        std::string value = json.substr(pos, end - pos);
        // Trim whitespace
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
            value.pop_back();
        return value;
    };

    auto getFloat = [&getValue](const std::string& key, float defaultVal) -> float {
        std::string val = getValue(key);
        if (val.empty()) return defaultVal;
        try { return std::stof(val); } catch (...) { return defaultVal; }
    };

    auto getInt = [&getValue](const std::string& key, int defaultVal) -> int {
        std::string val = getValue(key);
        if (val.empty()) return defaultVal;
        try { return std::stoi(val); } catch (...) { return defaultVal; }
    };

    auto getBool = [&getValue](const std::string& key, bool defaultVal) -> bool {
        std::string val = getValue(key);
        if (val.empty()) return defaultVal;
        return val.find("true") != std::string::npos;
    };

    auto getFloatArray = [&json](const std::string& key, float* arr, int count) {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return;
        pos = json.find("[", pos);
        if (pos == std::string::npos) return;
        pos++;
        for (int i = 0; i < count; i++) {
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
            size_t end = json.find_first_of(",]", pos);
            if (end == std::string::npos) return;
            std::string val = json.substr(pos, end - pos);
            try { arr[i] = std::stof(val); } catch (...) {}
            pos = end + 1;
        }
    };

    // Load GUI settings
    m_colorScheme = getInt("colorScheme", 0);
    getFloatArray("customBgColor", m_customBgColor, 3);
    getFloatArray("customTextColor", m_customTextColor, 3);
    m_bloomEnabled = getBool("bloomEnabled", true);
    m_bloomTextIntensity = getFloat("bloomTextIntensity", 0.2f);
    m_bloomLinesIntensity = getFloat("bloomLinesIntensity", 0.8f);
    m_bloomUIIntensity = getFloat("bloomUIIntensity", 0.2f);
    m_simplifiedWaveform = getBool("simplifiedWaveform", false);
    m_zoomSmoothing = getBool("zoomSmoothing", false);
    m_waveformScrolling = getBool("waveformScrolling", false);
    m_waveformAutoPage  = getBool("waveformAutoPage", false);
    m_waveformVerticalZoom = getFloat("waveformVerticalZoom", 1.0f);
    m_trackHeight = getFloat("trackHeight", 80.0f);
    m_controllerMode = getInt("controllerMode", 1);  // Default to Custom Mackie
    // Per-channel LED brightness. Missing key → defaults stay at 1.0 (max).
    getFloatArray("ledBrightness", m_ledBrightness, 9);
    // Mark for one-shot push to firmware after the serial controller is up.
    m_ledBrightnessNeedsPush = true;

    // Load audio settings (will be applied after audio engine is ready)
    if (m_audioEngine) {
        m_audioEngine->setControllerMode(m_controllerMode);  // Sync controller mode
        m_audioEngine->setScrubSpeed(getFloat("scrubSpeed", 1.0f));
        m_audioEngine->setSilentScrubSpeed(getFloat("silentScrubSpeed", 0.7f));
        m_audioEngine->setScrubRpmThreshold(getFloat("rpmThreshold", 30.0f));
        m_audioEngine->setFastSpeedMultiplier(getFloat("fastSpeedMultiplier", 4.0f));
        m_audioEngine->setRpmAveraging(getFloat("rpmAveraging", 0.7f));
    }

    // Load park button names
    auto getString = [&json](const std::string& key) -> std::string {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find("\"", pos + key.length() + 2);
        if (pos == std::string::npos) return "";
        pos++;
        size_t end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    // Per-dialog last folders. Unescape JSON backslashes so Windows
    // paths (which we escaped on save) round-trip correctly.
    auto unescape = [](const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) { out += s[i + 1]; i++; }
            else out += s[i];
        }
        return out;
    };
    m_lastAudioDir   = unescape(getString("lastAudioDir"));
    m_lastSessionDir = unescape(getString("lastSessionDir"));

    // Recent sessions array — walk the quoted strings between [ and ].
    // Deliberately does NOT drop entries whose file is missing: a session
    // on a disconnected drive should still be listed (greyed out) rather
    // than silently disappearing from the menu.
    m_recentSessions.clear();
    {
        size_t pos = json.find("\"recentSessions\"");
        if (pos != std::string::npos) {
            size_t open  = json.find('[', pos);
            size_t close = json.find(']', open == std::string::npos ? pos : open);
            if (open != std::string::npos && close != std::string::npos) {
                size_t p = open + 1;
                while (p < close && m_recentSessions.size() < MAX_RECENT_SESSIONS) {
                    size_t q1 = json.find('"', p);
                    if (q1 == std::string::npos || q1 > close) break;
                    // Find the closing quote, skipping escaped ones.
                    size_t q2 = q1 + 1;
                    while (q2 < close && json[q2] != '"') {
                        if (json[q2] == '\\') q2++;
                        q2++;
                    }
                    if (q2 >= close) break;
                    std::string entry = unescape(json.substr(q1 + 1, q2 - q1 - 1));
                    if (!entry.empty()) m_recentSessions.push_back(entry);
                    p = q2 + 1;
                }
            }
        }
    }

    std::string park1 = getString("park1");
    std::string park2 = getString("park2");
    std::string park3 = getString("park3");
    std::string park4 = getString("park4");
    if (!park1.empty()) strncpy(m_parkNames[0], park1.c_str(), 63);
    if (!park2.empty()) strncpy(m_parkNames[1], park2.c_str(), 63);
    if (!park3.empty()) strncpy(m_parkNames[2], park3.c_str(), 63);
    if (!park4.empty()) strncpy(m_parkNames[3], park4.c_str(), 63);
    m_selectedParkButton = getInt("selectedPark", 0);

    // Load velocity curve settings
    if (m_serialController) {
        VelocityCurve& curve = m_serialController->getVelocityCurve();
        curve.enabled = getBool("enabled", true);
        curve.smoothed = getBool("smoothed", false);
        curve.maxInputRpm = getFloat("maxInputRpm", 120.0f);
        curve.rpmWindowMs = getFloat("rpmWindowMs", 50.0f);
        curve.maxMultiplier = getFloat("maxMultiplier", 2.0f);
        curve.baseMultiplier = getFloat("baseMultiplier", 1.0f);
        int numPoints = getInt("numPoints", 4);
        if (numPoints >= 2 && numPoints <= VelocityCurve::MAX_POINTS) {
            curve.numPoints = numPoints;
        }

        // Parse points array - look for velocityCurve section
        size_t curvePos = json.find("\"velocityCurve\"");
        if (curvePos != std::string::npos) {
            size_t pointsPos = json.find("\"points\"", curvePos);
            if (pointsPos != std::string::npos) {
                size_t arrayStart = json.find("[", pointsPos);
                if (arrayStart != std::string::npos) {
                    for (int i = 0; i < curve.numPoints; i++) {
                        // Find next point object
                        size_t objStart = json.find("{", arrayStart);
                        if (objStart == std::string::npos) break;

                        // Find x value
                        size_t xPos = json.find("\"x\"", objStart);
                        if (xPos != std::string::npos) {
                            size_t colonPos = json.find(":", xPos);
                            if (colonPos != std::string::npos) {
                                size_t valEnd = json.find_first_of(",}", colonPos + 1);
                                std::string xVal = json.substr(colonPos + 1, valEnd - colonPos - 1);
                                try { curve.points[i].x = std::stof(xVal); } catch (...) {}
                            }
                        }

                        // Find y value
                        size_t yPos = json.find("\"y\"", objStart);
                        if (yPos != std::string::npos) {
                            size_t colonPos = json.find(":", yPos);
                            if (colonPos != std::string::npos) {
                                size_t valEnd = json.find_first_of(",}", colonPos + 1);
                                std::string yVal = json.substr(colonPos + 1, valEnd - colonPos - 1);
                                try { curve.points[i].y = std::stof(yVal); } catch (...) {}
                            }
                        }

                        // Move past this object for next iteration
                        size_t objEnd = json.find("}", objStart);
                        if (objEnd != std::string::npos) {
                            arrayStart = objEnd + 1;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }

    // Apply color scheme
    applyColorScheme(m_colorScheme);

    std::cout << "Settings loaded from c:\\0_CODE\\Dogma75\\settings\\user_settings.json" << std::endl;
}
