// ============================================================
//  割韭菜無雙 — Android NDK 版
//  使用 NativeActivity + EGL + OpenGL ES 3.0 + ImGui
// ============================================================
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <imgui.h>
#include <imgui_impl_android.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <atomic>

#define LOG_TAG "KLineRPG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================
//  Platform stubs for Android (replace Windows-specific code)
// ============================================================
#define __ANDROID_GAME__

// Stub for glfwGetTime — use clock_gettime
#include <time.h>
static double g_startTime = 0;
static double getTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec / 1e9;
    if (g_startTime == 0) g_startTime = t;
    return t - g_startTime;
}

// Sound stubs (TODO: implement with OpenSL ES)
static void sfxSlash() { LOGI("SFX: slash"); }
static void sfxHit() {}
static void sfxKill() { LOGI("SFX: kill"); }
static void sfxMagic() { LOGI("SFX: magic"); }
static void sfxJump() {}
static void sfxBearHit() {}

// ============================================================
//  EGL Context
// ============================================================
struct EGLState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0, height = 0;
    bool initialized = false;
};
static EGLState g_egl;

static bool initEGL(ANativeWindow* window) {
    g_egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_egl.display, nullptr, nullptr);

    const EGLint attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16, EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(g_egl.display, attrs, &config, 1, &numConfigs);

    g_egl.surface = eglCreateWindowSurface(g_egl.display, config, window, nullptr);

    const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_egl.context = eglCreateContext(g_egl.display, config, EGL_NO_CONTEXT, ctxAttrs);
    eglMakeCurrent(g_egl.display, g_egl.surface, g_egl.surface, g_egl.context);

    eglQuerySurface(g_egl.display, g_egl.surface, EGL_WIDTH, &g_egl.width);
    eglQuerySurface(g_egl.display, g_egl.surface, EGL_HEIGHT, &g_egl.height);

    LOGI("EGL initialized: %dx%d", g_egl.width, g_egl.height);
    g_egl.initialized = true;
    return true;
}

static void termEGL() {
    if (g_egl.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_egl.context != EGL_NO_CONTEXT) eglDestroyContext(g_egl.display, g_egl.context);
        if (g_egl.surface != EGL_NO_SURFACE) eglDestroySurface(g_egl.display, g_egl.surface);
        eglTerminate(g_egl.display);
    }
    g_egl = {};
}

// ============================================================
//  Touch controls (virtual buttons)
// ============================================================
struct TouchButton {
    float x, y, w, h;  // screen-space normalized (0-1)
    const char* label;
    bool pressed;
};

static TouchButton g_buttons[] = {
    {0.02f, 0.7f, 0.12f, 0.12f, "D", false},    // Left
    {0.16f, 0.7f, 0.12f, 0.12f, "F", false},    // Right
    {0.02f, 0.55f, 0.12f, 0.12f, "Jump", false}, // Jump
    {0.78f, 0.7f, 0.12f, 0.12f, "J", false},    // Attack
    {0.78f, 0.55f, 0.12f, 0.12f, "K", false},   // Magic
};
static const int NUM_BUTTONS = 5;

static int hitTestButton(float nx, float ny) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        auto& b = g_buttons[i];
        if (nx >= b.x && nx <= b.x + b.w && ny >= b.y && ny <= b.y + b.h)
            return i;
    }
    return -1;
}

// ============================================================
//  Include the shared game logic
//  (This would #include the shared portions of main.cpp)
//  For now, define the essential game structures
// ============================================================

// TODO: Factor shared game logic from main.cpp into a
// platform-independent header (game_logic.h) that both
// Windows and Android can include.
//
// For now, this file serves as the Android project scaffold.
// The actual game code integration requires:
// 1. Extract pushBox, buildDonChan, buildLeek, etc. to game_logic.h
// 2. Extract Candle, TF, Mesh types to game_types.h
// 3. Replace GLFW calls with EGL + touch input
// 4. Replace WinHTTP with Android HttpURLConnection via JNI

// ============================================================
//  Minimal render loop (scaffold)
// ============================================================
static bool g_appReady = false;

static void drawFrame() {
    if (!g_egl.initialized) return;

    glViewport(0, 0, g_egl.width, g_egl.height);
    glClearColor(0.06f, 0.08f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    // Draw touch buttons
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (int i = 0; i < NUM_BUTTONS; i++) {
        auto& b = g_buttons[i];
        float x0 = b.x * g_egl.width, y0 = b.y * g_egl.height;
        float x1 = (b.x + b.w) * g_egl.width, y1 = (b.y + b.h) * g_egl.height;
        ImU32 col = b.pressed ? IM_COL32(100, 200, 255, 180) : IM_COL32(60, 80, 120, 120);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, 12.f);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(200, 200, 200, 160), 12.f, 0, 2.f);
        ImVec2 tsz = ImGui::CalcTextSize(b.label);
        dl->AddText(ImVec2(x0 + (x1 - x0 - tsz.x) * 0.5f, y0 + (y1 - y0 - tsz.y) * 0.5f),
                    IM_COL32(255, 255, 255, 220), b.label);
    }

    // Placeholder text
    ImGui::SetNextWindowPos(ImVec2(g_egl.width * 0.3f, g_egl.height * 0.4f));
    ImGui::Begin("##info", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "K-Line RPG Android");
    ImGui::Text("Touch controls ready");
    ImGui::Text("Game logic integration pending...");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(g_egl.display, g_egl.surface);
}

// ============================================================
//  Android app lifecycle
// ============================================================
static void handleCmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window && !g_egl.initialized) {
            initEGL(app->window);
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui_ImplAndroid_Init(app->window);
            ImGui_ImplOpenGL3_Init("#version 300 es");
            g_appReady = true;
        }
        break;
    case APP_CMD_TERM_WINDOW:
        g_appReady = false;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        termEGL();
        break;
    case APP_CMD_DESTROY:
        break;
    }
}

static int32_t handleInput(struct android_app* app, AInputEvent* event) {
    if (ImGui_ImplAndroid_HandleInputEvent(event)) return 1;

    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        float nx = x / g_egl.width;
        float ny = y / g_egl.height;

        if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_MOVE) {
            for (int i = 0; i < NUM_BUTTONS; i++) g_buttons[i].pressed = false;
            int hit = hitTestButton(nx, ny);
            if (hit >= 0) g_buttons[hit].pressed = true;
        } else if (action == AMOTION_EVENT_ACTION_UP) {
            for (int i = 0; i < NUM_BUTTONS; i++) g_buttons[i].pressed = false;
        }
        return 1;
    }
    return 0;
}

void android_main(struct android_app* app) {
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;

    while (!app->destroyRequested) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollAll(g_appReady ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }
        if (g_appReady) drawFrame();
    }
}
