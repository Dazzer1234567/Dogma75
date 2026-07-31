#include "gui_manager.h"
#include "../audio/audio_engine.h"
#include "../controller/serial_controller.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>

#ifdef SDL2_FOUND
#include <SDL.h>
#endif

#ifdef IMGUI_FOUND
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#endif

// OpenGL headers
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
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
    , m_waveformVerticalZoom(1.0f)
    , m_trackHeight(80.0f)
    , m_viewCenterPosition(0)
    , m_lastZoom(1.0f)
    , m_colorScheme(0)
    , m_showColorPickers(false)
    , m_editingBgColor(true)
    , m_bloomEnabled(false)
    , m_bloomTextIntensity(0.5f)
    , m_bloomLinesIntensity(0.5f)
    , m_bloomUIIntensity(0.5f)
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
    , m_isFullscreen(true)
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

bool GUIManager::initialize(AudioEngine* audioEngine, SerialController* serialController) {
    std::cout << "Initializing GUI with SDL2 + OpenGL..." << std::endl;

    m_audioEngine = audioEngine;
    m_serialController = serialController;

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

    // Start in true fullscreen (borderless fullscreen desktop)
    m_window = SDL_CreateWindow(
        "Minimal DAW v0.1.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP
    );
    m_isFullscreen = true;

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
    // Render lines layer to its own FBO using raw OpenGL, then blur
    if (m_bloomLinesIntensity > 0.001f && !m_lineDrawCmds.empty()) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_linesFBO);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        drawLinesRaw();

        glClearColor(bgR, bgG, bgB, 1.0f);
        blurTexture(m_linesTexture, m_linesBlurTexture);
    }

    // ===== LAYER 3: WAVEFORM =====
    // Render waveform layer to its own FBO using raw OpenGL, then blur
    if (m_bloomUIIntensity > 0.001f && !m_waveformDrawCmds.empty()) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_waveformFBO);
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        drawWaveformRaw();

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

void GUIManager::processFrame() {
#ifdef SDL2_FOUND
#ifdef IMGUI_FOUND
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
            m_windowWidth = event.window.data1;
            m_windowHeight = event.window.data2;
            // Recreate bloom buffers at new size
            cleanupBloom();
            initBloom();
        }
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

void GUIManager::renderToolbar() {
#ifdef IMGUI_FOUND
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::Button("+ Add Track")) {
        m_audioEngine->addTrack();
    }
    ImGui::SameLine();

    int selectedTrack = m_audioEngine->getSelectedTrack();
    if (selectedTrack >= 0) {
        if (ImGui::Button("- Delete Track")) {
            m_audioEngine->deleteTrack(selectedTrack);
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("- Delete Track");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

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

    // Controller mode dropdown
    ImGui::Text("Mode:");
    ImGui::SameLine();
    const char* modeNames[] = { "Custom", "Custom Mackie", "Mackie", "MidiRel" };
    if (ImGui::BeginCombo("##ControllerMode", modeNames[m_controllerMode], ImGuiComboFlags_WidthFitPreview)) {
        for (int i = 0; i < 4; i++) {
            bool isSelected = (m_controllerMode == i);
            if (ImGui::Selectable(modeNames[i], isSelected)) {
                m_controllerMode = i;
                m_audioEngine->setControllerMode(m_controllerMode);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    m_audioEngine->setControllerMode(m_controllerMode);

    // Encoder curve toggle and button
    ImGui::SameLine();
    VelocityCurve& curve = m_serialController->getVelocityCurve();

    ImVec2 checkboxPos = ImGui::GetCursorScreenPos();
    float checkboxSize = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(checkboxPos.x - 1, checkboxPos.y - 1),
        ImVec2(checkboxPos.x + checkboxSize + 1, checkboxPos.y + checkboxSize + 1),
        IM_COL32(128, 128, 128, 255),
        0.0f, 0, 1.0f
    );

    ImGui::Checkbox("##curveEnabled", &curve.enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable/disable velocity curve effect");
    }
    ImGui::SameLine();
    if (ImGui::Button("Curve")) {
        m_showVelocityCurveEditor = !m_showVelocityCurveEditor;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Encoder velocity curve editor");
    }

    // Encoder sensitivity slider
    ImGui::SameLine();
    ImGui::Text("Sens:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("##baseMult", &curve.baseMultiplier, 0.33f, 3.0f, "%.2fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Base encoder sensitivity.\nAdjusts ratio of turns to screen movement.\n0.33x = slower, 3x = faster");
    }

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

    ImGui::Text("TRACKS");
    ImGui::Separator();

    int selectedTrack = m_audioEngine->getSelectedTrack();
    int trackCount = m_audioEngine->getTrackCount();
    if (trackCount == 0) {
        ImVec4 hintColor = (m_colorScheme == 1)
            ? ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f)
            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(hintColor, "No tracks");
        ImGui::TextColored(hintColor, "Click '+ Add Track'");
    } else {
        for (int i = 0; i < trackCount; i++) {
            const Track* track = m_audioEngine->getTrack(i);
            if (track) {
                bool isSelected = (selectedTrack == i);
                if (ImGui::Selectable(track->name.c_str(), isSelected)) {
                    m_audioEngine->setSelectedTrack(i);
                }
            }
        }

        ImGui::Separator();

        if (selectedTrack >= 0) {
            Track* track = m_audioEngine->getTrack(selectedTrack);
            if (track) {
                ImGui::Text("PROPERTIES");
                ImGui::Separator();

                static char nameBuffer[128];
                strncpy(nameBuffer, track->name.c_str(), sizeof(nameBuffer) - 1);
                nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##TrackName", nameBuffer, sizeof(nameBuffer))) {
                    track->name = nameBuffer;
                }

                ImGui::Spacing();

                ImGui::Text("Volume");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##Volume", &track->volume, 0.0f, 1.0f);

                ImGui::Text("Pan");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##Pan", &track->pan, -1.0f, 1.0f);

                ImGui::Spacing();

                ImGui::Checkbox("Mute", &track->muted);
                ImGui::SameLine();
                ImGui::Checkbox("Solo", &track->solo);

                ImGui::Spacing();

                int numPairs = m_audioEngine->getNumStereoPairs();
                if (numPairs > 0) {
                    ImGui::Text("Output");
                    static char pairLabel[32];
                    sprintf(pairLabel, "Ch %d & %d", (track->outputPair * 2) + 1, (track->outputPair * 2) + 2);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##Output", pairLabel)) {
                        for (int p = 0; p < numPairs; p++) {
                            bool isSelectedPair = (track->outputPair == p);
                            char label[32];
                            sprintf(label, "Ch %d & %d", (p * 2) + 1, (p * 2) + 2);
                            if (ImGui::Selectable(label, isSelectedPair)) {
                                track->outputPair = p;
                            }
                            if (isSelectedPair) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::Spacing();

                static char filePathBuffer[512] = "";
                ImGui::Text("Audio File");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##FilePath", filePathBuffer, sizeof(filePathBuffer));
                if (ImGui::Button("Load Audio", ImVec2(-1, 0))) {
                    if (strlen(filePathBuffer) > 0) {
                        m_audioEngine->loadTrackAudio(selectedTrack, filePathBuffer);
                    }
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Waveform View");
    if (ImGui::Button(m_simplifiedWaveform ? "Detailed" : "Simplified", ImVec2(-1, 0))) {
        m_simplifiedWaveform = !m_simplifiedWaveform;
    }

    ImGui::Checkbox("Smooth Zoom", &m_zoomSmoothing);

    if (ImGui::Checkbox("Scroll Waveform", &m_waveformScrolling)) {
        if (!m_waveformScrolling) {
            m_viewCenterPosition = m_audioEngine->getPlaybackPosition();
        }
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

    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));

    ImGui::Text("Vertical Zoom");
    ImGui::SliderFloat("##VertZoom", &m_waveformVerticalZoom, 0.5f, 4.0f, "%.1fx");
    storeSliderGrab(m_waveformVerticalZoom, 0.5f, 4.0f);

    ImGui::Text("Track Height");
    ImGui::SliderFloat("##TrackHeight", &m_trackHeight, 40.0f, 300.0f, "%.0f");
    storeSliderGrab(m_trackHeight, 40.0f, 300.0f);

    ImGui::Separator();
    ImGui::Text("SCRUB");
    ImGui::Separator();

    float scrubSpeed = m_audioEngine->getScrubSpeed();
    ImGui::Text("Scrub Speed");
    if (ImGui::SliderFloat("##ScrubSpeed", &scrubSpeed, 0.05f, 2.0f, "%.2f")) {
        m_audioEngine->setScrubSpeed(scrubSpeed);
    }
    storeSliderGrab(scrubSpeed, 0.05f, 2.0f);

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
    const char* zoneText = "SLOW";
    ImVec4 rpmColor;
    if (m_colorScheme == 1) {
        rpmColor = ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f);
    } else {
        rpmColor = ImVec4(0.6f, 0.6f, 1.0f, 1.0f);
        if (currentRpm >= rpmThreshold) {
            rpmColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        }
    }
    if (currentRpm >= rpmThreshold) {
        zoneText = "FAST";
    }
    ImGui::TextColored(rpmColor, "Knob 1: %03d RPM (%s)", displayRpm, zoneText);

    ImGui::Separator();
    ImGui::Text("PARK");
    ImGui::Separator();

    for (int i = 0; i < 4; i++) {
        ImGui::PushID(i);

        if (m_editingParkButton == i) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##ParkEdit", m_parkNames[i], sizeof(m_parkNames[i]),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                m_editingParkButton = -1;
            }
            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
                m_editingParkButton = -1;
            }
            if (ImGui::IsItemVisible() && !ImGui::IsItemActive()) {
                ImGui::SetKeyboardFocusHere(-1);
            }
        } else {
            bool isSelected = (m_selectedParkButton == i);
            if (isSelected) {
                if (m_colorScheme == 1) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(m_customBgColor[0], m_customBgColor[1], m_customBgColor[2], 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
                }
            }
            if (ImGui::Button(m_parkNames[i], ImVec2(-1, 0))) {
                m_selectedParkButton = i;
                m_audioEngine->setSelectedPark(i);
            }
            if (isSelected) {
                ImVec2 btnMin = ImGui::GetItemRectMin();
                ImVec2 btnMax = ImGui::GetItemRectMax();
                ImU32 btnColor;
                if (m_colorScheme == 1) {
                    btnColor = IM_COL32(
                        (int)(m_customTextColor[0] * 255),
                        (int)(m_customTextColor[1] * 255),
                        (int)(m_customTextColor[2] * 255), 255);
                } else {
                    btnColor = IM_COL32(51, 102, 204, 255);
                }
                WaveformDrawCmd cmd;
                cmd.x1 = btnMin.x; cmd.y1 = btnMin.y;
                cmd.isRect = true;
                cmd.rectX2 = btnMax.x; cmd.rectY2 = btnMax.y;
                cmd.color = btnColor;
                m_waveformDrawCmds.push_back(cmd);
            }
            if (isSelected) {
                if (m_colorScheme == 1) {
                    ImGui::PopStyleColor(3);
                } else {
                    ImGui::PopStyleColor(2);
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                m_editingParkButton = i;
            }
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
#endif
}

void GUIManager::renderWaveform(float height) {
#ifdef IMGUI_FOUND
    ImGui::BeginChild("MainArea", ImVec2(0, height), true);

    ImGui::Text("TIMELINE");
    ImGui::Separator();

    int selectedTrack = m_audioEngine->getSelectedTrack();
    int trackCount = m_audioEngine->getTrackCount();

    for (int i = 0; i < trackCount; i++) {
        const Track* track = m_audioEngine->getTrack(i);
        if (track && !track->audioData.empty()) {
            bool isSelected = (selectedTrack == i);

            ImVec4 headerColor;
            if (m_colorScheme == 1) {
                headerColor = ImVec4(m_customTextColor[0], m_customTextColor[1], m_customTextColor[2], 1.0f);
            } else {
                headerColor = isSelected ? ImVec4(0.3f, 0.5f, 0.7f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
            }
            ImGui::TextColored(headerColor, "%s", track->name.c_str());

            const std::vector<float>& audioData = track->audioData;
            int channels = track->channels;
            size_t totalFrames = audioData.size() / channels;
            size_t playbackPos = m_audioEngine->getPlaybackPosition();

            float availableWidth = ImGui::GetContentRegionAvail().x;
            float labelPadding = 20.0f;
            ImVec2 waveformSize(availableWidth, m_trackHeight);
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
                // Apply encoder scroll delta (modifier + E3)
                long scrollDelta = m_audioEngine->consumeViewScrollDelta();
                if (scrollDelta != 0) {
                    long newCenter = (long)m_viewCenterPosition + scrollDelta;
                    if (newCenter < 0) newCenter = 0;
                    if (newCenter > (long)totalFrames) newCenter = totalFrames;
                    m_viewCenterPosition = (size_t)newCenter;
                }

                bool zoomChanged = (zoom != m_lastZoom);
                if (zoomChanged) {
                    m_lastZoom = zoom;
                    m_viewCenterPosition = playbackPos;
                }

                size_t currentViewStart = m_viewCenterPosition > visibleFrames / 2 ?
                    m_viewCenterPosition - visibleFrames / 2 : 0;
                size_t currentViewEnd = currentViewStart + visibleFrames;

                bool needsRecenter = (m_viewCenterPosition == 0) ||
                    (playbackPos < currentViewStart) ||
                    (playbackPos >= currentViewEnd);

                if (needsRecenter && scrollDelta == 0) {
                    m_viewCenterPosition = playbackPos;
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

            size_t framesPerPixel = visibleFrames / (size_t)waveformSize.x;
            if (framesPerPixel < 1) framesPerPixel = 1;

            ImU32 waveColor;
            if (m_colorScheme == 1) {
                waveColor = IM_COL32(
                    (int)(m_customTextColor[0] * 255),
                    (int)(m_customTextColor[1] * 255),
                    (int)(m_customTextColor[2] * 255), 255);
            } else if (track->solo) {
                waveColor = IM_COL32(255, 165, 0, 255);
            } else if (track->muted) {
                waveColor = IM_COL32(100, 100, 100, 255);
            } else {
                waveColor = IM_COL32(100, 200, 100, 255);
            }

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
                for (int x = 0; x < (int)waveformSize.x; x++) {
                    size_t frameStart = viewStart + x * framesPerPixel;
                    size_t frameEnd = frameStart + framesPerPixel;
                    if (frameEnd > totalFrames) frameEnd = totalFrames;
                    if (frameStart >= totalFrames) break;

                    float minVal = 0.0f, maxVal = 0.0f;
                    for (size_t frame = frameStart; frame < frameEnd; frame++) {
                        float sample = 0.0f;
                        for (int ch = 0; ch < channels; ch++) {
                            sample += audioData[frame * channels + ch];
                        }
                        sample /= channels;
                        if (sample < minVal) minVal = sample;
                        if (sample > maxVal) maxVal = sample;
                    }

                    float y1 = centerY - (maxVal * waveformSize.y * 0.45f * m_waveformVerticalZoom);
                    float y2 = centerY - (minVal * waveformSize.y * 0.45f * m_waveformVerticalZoom);

                    float trackTop = cursorPos.y;
                    float trackBottom = cursorPos.y + waveformSize.y;
                    if (y1 < trackTop) y1 = trackTop;
                    if (y2 > trackBottom) y2 = trackBottom;

                    drawList->AddLine(ImVec2(cursorPos.x + x, y1),
                                     ImVec2(cursorPos.x + x, y2),
                                     waveColor);

                    WaveformDrawCmd cmd;
                    cmd.x1 = cursorPos.x + x; cmd.y1 = y1;
                    cmd.x2 = cursorPos.x + x; cmd.y2 = y2;
                    cmd.isRect = false;
                    cmd.color = waveColor;
                    m_waveformDrawCmds.push_back(cmd);
                }
            }

            // Draw markers
            float bottomY = cursorPos.y + waveformSize.y - 3;
            ImU32 yellowColor = IM_COL32(255, 220, 0, 255);
            ImU32 redColor = IM_COL32(255, 50, 50, 255);

            auto storeLineCmd = [this](float x1, float y1, float x2, float y2, ImU32 color, float thickness) {
                LineDrawCmd cmd;
                cmd.x1 = x1; cmd.y1 = y1; cmd.x2 = x2; cmd.y2 = y2;
                cmd.color = color;
                cmd.thickness = thickness;
                cmd.isTriangle = false;
                cmd.isText = false;
                m_lineDrawCmds.push_back(cmd);
            };

            // Marker lines between markers
            if (m_audioEngine->isMarkerEnabled(0) && m_audioEngine->isMarkerEnabled(1)) {
                size_t pos0 = m_audioEngine->getMarkerPosition(0);
                size_t pos1 = m_audioEngine->getMarkerPosition(1);
                size_t drawStart = (pos0 < viewStart) ? viewStart : pos0;
                size_t drawEnd = (pos1 > viewEnd) ? viewEnd : pos1;
                if (drawStart < drawEnd && drawEnd > viewStart && drawStart < viewEnd) {
                    float x1 = cursorPos.x + ((float)(drawStart - viewStart) / visibleFrames) * waveformSize.x;
                    float x2 = cursorPos.x + ((float)(drawEnd - viewStart) / visibleFrames) * waveformSize.x;
                    drawList->AddLine(ImVec2(x1, bottomY), ImVec2(x2, bottomY), yellowColor, 3.0f);
                    storeLineCmd(x1, bottomY, x2, bottomY, yellowColor, 3.0f);
                }
            }

            if (m_audioEngine->isMarkerEnabled(1) && m_audioEngine->isMarkerEnabled(2)) {
                size_t pos1 = m_audioEngine->getMarkerPosition(1);
                size_t pos2 = m_audioEngine->getMarkerPosition(2);
                size_t drawStart = (pos1 < viewStart) ? viewStart : pos1;
                size_t drawEnd = (pos2 > viewEnd) ? viewEnd : pos2;
                if (drawStart < drawEnd && drawEnd > viewStart && drawStart < viewEnd) {
                    float x1 = cursorPos.x + ((float)(drawStart - viewStart) / visibleFrames) * waveformSize.x;
                    float x2 = cursorPos.x + ((float)(drawEnd - viewStart) / visibleFrames) * waveformSize.x;
                    drawList->AddLine(ImVec2(x1, bottomY), ImVec2(x2, bottomY), redColor, 3.0f);
                    storeLineCmd(x1, bottomY, x2, bottomY, redColor, 3.0f);
                }
            }

            if (m_audioEngine->isMarkerEnabled(2) && m_audioEngine->isMarkerEnabled(3)) {
                size_t pos2 = m_audioEngine->getMarkerPosition(2);
                size_t pos3 = m_audioEngine->getMarkerPosition(3);
                size_t drawStart = (pos2 < viewStart) ? viewStart : pos2;
                size_t drawEnd = (pos3 > viewEnd) ? viewEnd : pos3;
                if (drawStart < drawEnd && drawEnd > viewStart && drawStart < viewEnd) {
                    float x1 = cursorPos.x + ((float)(drawStart - viewStart) / visibleFrames) * waveformSize.x;
                    float x2 = cursorPos.x + ((float)(drawEnd - viewStart) / visibleFrames) * waveformSize.x;
                    drawList->AddLine(ImVec2(x1, bottomY), ImVec2(x2, bottomY), yellowColor, 3.0f);
                    storeLineCmd(x1, bottomY, x2, bottomY, yellowColor, 3.0f);
                }
            }

            // Draw marker labels
            const char* markerLabels[4] = {"L", "L", "R", "R"};
            for (int m = 0; m < 4; m++) {
                if (m_audioEngine->isMarkerEnabled(m)) {
                    size_t markerPos = m_audioEngine->getMarkerPosition(m);
                    if (markerPos >= viewStart && markerPos < viewEnd) {
                        float markerX = cursorPos.x + ((float)(markerPos - viewStart) / visibleFrames) * waveformSize.x;

                        ImU32 markerColor = (m == 0 || m == 3) ? yellowColor : redColor;

                        drawList->AddLine(ImVec2(markerX, cursorPos.y),
                                         ImVec2(markerX, cursorPos.y + waveformSize.y),
                                         markerColor, 2.0f);
                        storeLineCmd(markerX, cursorPos.y, markerX, cursorPos.y + waveformSize.y, markerColor, 2.0f);

                        float arrowTop = cursorPos.y + 2;
                        float arrowBottom = cursorPos.y + 14;
                        float arrowWidth = 6;
                        drawList->AddTriangleFilled(
                            ImVec2(markerX, arrowBottom),
                            ImVec2(markerX - arrowWidth, arrowTop),
                            ImVec2(markerX + arrowWidth, arrowTop),
                            markerColor);

                        LineDrawCmd triCmd;
                        triCmd.isTriangle = true;
                        triCmd.isText = false;
                        triCmd.tx1 = markerX; triCmd.ty1 = arrowBottom;
                        triCmd.tx2 = markerX - arrowWidth; triCmd.ty2 = arrowTop;
                        triCmd.tx3 = markerX + arrowWidth; triCmd.ty3 = arrowTop;
                        triCmd.color = markerColor;
                        m_lineDrawCmds.push_back(triCmd);

                        float labelY = cursorPos.y - labelPadding + 2;
                        drawList->AddText(ImVec2(markerX - 4, labelY), markerColor, markerLabels[m]);

                        LineDrawCmd textCmd;
                        textCmd.isTriangle = false;
                        textCmd.isText = true;
                        textCmd.textX = markerX - 4;
                        textCmd.textY = labelY;
                        textCmd.color = markerColor;
                        strncpy(textCmd.text, markerLabels[m], 7);
                        textCmd.text[7] = '\0';
                        m_lineDrawCmds.push_back(textCmd);
                    }
                }
            }

            // Draw playhead
            if (playbackPos >= viewStart && playbackPos < viewEnd) {
                float playbackX = cursorPos.x + ((float)(playbackPos - viewStart) / visibleFrames) * waveformSize.x;
                ImU32 playheadColor = IM_COL32(50, 220, 50, 255);
                drawList->AddLine(ImVec2(playbackX, cursorPos.y),
                                 ImVec2(playbackX, cursorPos.y + waveformSize.y),
                                 playheadColor, 2.0f);
                storeLineCmd(playbackX, cursorPos.y, playbackX, cursorPos.y + waveformSize.y, playheadColor, 2.0f);
            }

            ImGui::Spacing();
        }
    }

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

    // Bloom controls
    ImGui::SameLine();
    ImGui::Checkbox("Glow", &m_bloomEnabled);

    if (m_bloomEnabled) {
        float availableWidth = ImGui::GetContentRegionAvail().x;
        float sliderWidth = 120.0f;
        float labelWidth = 70.0f;
        float spacing = (availableWidth - 3 * (labelWidth + sliderWidth)) / 4.0f;
        if (spacing < 20.0f) spacing = 20.0f;

        ImGui::SetCursorPosX(spacing);
        ImGui::Text("Glow Text");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##bloomText", &m_bloomTextIntensity, 0.0f, 1.0f, "%.2f");

        ImGui::SameLine();
        ImGui::SetCursorPosX(spacing + labelWidth + sliderWidth + spacing);
        ImGui::Text("Glow Lines");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##bloomLines", &m_bloomLinesIntensity, 0.0f, 1.0f, "%.2f");

        ImGui::SameLine();
        ImGui::SetCursorPosX(spacing + 2 * (labelWidth + sliderWidth + spacing));
        ImGui::Text("Glow UI");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##bloomUI", &m_bloomUIIntensity, 0.0f, 1.0f, "%.2f");
    }

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
    float scrubSpeed = m_audioEngine ? m_audioEngine->getScrubSpeed() : 0.25f;
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
    file << "    \"waveformVerticalZoom\": " << m_waveformVerticalZoom << ",\n";
    file << "    \"trackHeight\": " << m_trackHeight << ",\n";
    file << "    \"controllerMode\": " << m_controllerMode << "\n";
    file << "  },\n";
    file << "\n";
    file << "  \"audio\": {\n";
    file << "    \"scrubSpeed\": " << scrubSpeed << ",\n";
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
    m_bloomEnabled = getBool("bloomEnabled", false);
    m_bloomTextIntensity = getFloat("bloomTextIntensity", 0.5f);
    m_bloomLinesIntensity = getFloat("bloomLinesIntensity", 0.5f);
    m_bloomUIIntensity = getFloat("bloomUIIntensity", 0.5f);
    m_simplifiedWaveform = getBool("simplifiedWaveform", false);
    m_zoomSmoothing = getBool("zoomSmoothing", false);
    m_waveformScrolling = getBool("waveformScrolling", false);
    m_waveformVerticalZoom = getFloat("waveformVerticalZoom", 1.0f);
    m_trackHeight = getFloat("trackHeight", 80.0f);
    m_controllerMode = getInt("controllerMode", 1);  // Default to Custom Mackie

    // Load audio settings (will be applied after audio engine is ready)
    if (m_audioEngine) {
        m_audioEngine->setControllerMode(m_controllerMode);  // Sync controller mode
        m_audioEngine->setScrubSpeed(getFloat("scrubSpeed", 0.25f));
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
