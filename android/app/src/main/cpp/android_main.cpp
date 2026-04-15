// ============================================================
//  割韭菜無雙 — Android NDK 版
//  NativeActivity + EGL + OpenGL ES 3.0 + ImGui
// ============================================================
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <time.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_impl_android.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <ctime>
#include <fstream>

#define LOG_TAG "KLineRPG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============================================================
//  Platform compatibility: replace GLFW functions
// ============================================================
static double g_clockStart = 0;
static double glfwGetTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec / 1e9;
    if (g_clockStart == 0) g_clockStart = t;
    return t - g_clockStart;
}
// Stub for glfwGetKey (not used in game logic directly)
#define GLFW_KEY_ESCAPE 256
#define GLFW_PRESS 1
static int glfwGetKey(void*, int) { return 0; }

// Sound stubs (TODO: OpenSL ES)
static void sfxSlash() { LOGI("SFX: slash"); }
static void sfxHit() {}
static void sfxKill() { LOGI("SFX: kill"); }
static void sfxMagic() { LOGI("SFX: magic"); }
static void sfxJump() {}
static void sfxBearHit() {}

// Network stubs
static void loadWatchlist() {}
static void saveWatchlist() {}

// ============================================================
//  Shared game constants
// ============================================================
static const int WIN_W=1280, WIN_H=720;
static const int PANEL_W=0; // no panel on mobile
static float WORLD_LO=10.f, WORLD_HI=22.f;
static float VOL_LO=3.f, VOL_HI=9.f;
static float MACD_LO=-7.f, MACD_HI=1.8f;
static float MACD_MID=(MACD_LO+MACD_HI)*.5f;
static float MACD_HALF=(MACD_HI-MACD_LO)*.5f;

// ============================================================
//  Include shared game headers (order matters)
// ============================================================
#include "game_types.h"    // Candle, TF, TFStore
#include "game_state.h"    // Camera, game state structs
#include "game_mesh.h"     // Shaders, Mesh, pushBox, font
#include "game_chart.h"    // K-line geometry builders
#include "game_chars.h"    // Character builders

// ============================================================
//  Hardcoded stock data (same as desktop)
// ============================================================
static void initHardcoded0050() {
    static const Candle DATA[] = {
        {57.90f,58.75f,57.90f,58.10f,77.3f},{59.00f,59.25f,58.85f,59.20f,73.8f},
        {59.20f,60.10f,59.05f,60.05f,76.1f},{61.00f,61.70f,60.90f,61.70f,138.0f},
        {61.00f,61.15f,60.60f,61.15f,119.7f},{61.85f,61.95f,61.50f,61.60f,76.5f},
        {59.90f,61.00f,59.60f,61.00f,277.6f},{61.85f,62.15f,60.65f,60.70f,163.4f},
        {61.00f,61.80f,60.60f,61.80f,101.9f},{62.10f,63.00f,62.10f,63.00f,115.8f},
        {62.05f,62.30f,61.90f,62.05f,166.2f},{62.35f,63.20f,62.15f,63.10f,105.9f},
        {63.40f,63.65f,63.05f,63.20f,128.8f},{62.60f,62.85f,62.25f,62.70f,124.3f},
        {62.10f,62.40f,61.75f,62.35f,166.7f},{63.75f,63.90f,63.45f,63.80f,104.8f},
        {63.55f,63.70f,63.20f,63.35f,99.8f},{63.70f,64.45f,63.70f,64.40f,67.4f},
        {64.60f,64.80f,64.00f,64.40f,95.1f},{64.60f,64.80f,64.40f,64.75f,56.0f},
        {64.45f,64.80f,63.90f,64.15f,140.2f},{64.50f,64.85f,63.85f,63.95f,88.1f},
        {62.95f,63.15f,62.30f,63.15f,229.4f},{63.35f,63.50f,63.15f,63.30f,68.7f},
        {62.65f,62.75f,62.40f,62.55f,111.2f},{62.80f,63.35f,62.60f,63.35f,83.1f},
        {63.65f,63.70f,62.85f,62.90f,79.7f},{62.95f,63.30f,62.75f,63.20f,62.6f},
        {62.80f,62.95f,62.60f,62.90f,80.5f},{61.65f,61.90f,61.40f,61.70f,223.2f},
    };
    static const int ND = (int)(sizeof(DATA)/sizeof(DATA[0]));
    g_stockCode = "0050"; g_stockName = "元大台灣50";
    g_store[0].clear(); g_store[0].perLbl = false; g_store[0].pStep = 2.f;
    g_store[0].name = "0050 元大台灣50 | 日線";
    g_store[0].cands.assign(DATA, DATA + ND);
    buildTFViews();
}

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
        EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig config; EGLint numConfigs;
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
//  Touch virtual buttons
// ============================================================
struct TouchBtn { float x, y, w, h; const char* label; bool pressed; int action; };
// Actions: 0=left, 1=right, 2=jump, 3=attack, 4=magic
static TouchBtn g_btns[] = {
    {0.02f, 0.72f, 0.10f, 0.14f, "D",    false, 0},
    {0.14f, 0.72f, 0.10f, 0.14f, "F",    false, 1},
    {0.02f, 0.55f, 0.10f, 0.14f, "Jump", false, 2},
    {0.80f, 0.72f, 0.10f, 0.14f, "J",    false, 3},
    {0.80f, 0.55f, 0.10f, 0.14f, "K",    false, 4},
};
static const int NUM_BTNS = 5;

// ============================================================
//  Mesh objects (same as desktop)
// ============================================================
static Mesh mGreen, mRed, mGrid, mMA5, mMA20, mMA60, mMA120, mMA240;
static Mesh mVolG, mVolR, mMHG, mMHR, mDIF, mDEA, mKK, mKDLine;
static Mesh mRSI, mBBUpper, mBBLower, mBBMid;
static DynMesh mLabel, mHoverLine;
static DynLitMesh mChr0, mChr1, mChr2, mChr3, mChr4;
static DynLitMesh mHitBar;
static DynLitMesh mLeekG, mLeekW, mLeekD, mBoom, mTornado;
static DynLitMesh mLobR, mLobD, mBoxB, mBoxD, mClaw;

// GL uniform helpers
static void uM4(GLuint p, const char* n, const glm::mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void uM3(GLuint p, const char* n, const glm::mat3& m) {
    glUniformMatrix3fv(glGetUniformLocation(p, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void uV3(GLuint p, const char* n, const glm::vec3& v) {
    glUniform3fv(glGetUniformLocation(p, n), 1, glm::value_ptr(v));
}

static GLuint g_pLit = 0, g_pFlat = 0;
static bool g_gameInited = false;

// ============================================================
//  rebuildAll (same logic as desktop)
// ============================================================
static void rebuildAll(const TF& tf, int vis) {
    recalcLayout();
    if (!tf.data || tf.cnt < 1) return;
    g_startIdx = std::max(0, tf.cnt - vis);
    const Candle* d = tf.data + g_startIdx;
    int n = std::min(vis, tf.cnt - g_startIdx);
    g_sp = std::min(1.6f, 50.f / std::max(1, n));
    g_bHW = g_sp * .32f; g_wHW = g_sp * .05f;
    g_pMin = d[0].l; g_pMax = d[0].h;
    for (int i = 1; i < n; i++) { g_pMin = std::min(g_pMin, d[i].l); g_pMax = std::max(g_pMax, d[i].h); }
    float pad = (g_pMax - g_pMin) * .06f; g_pMin -= pad; g_pMax += pad;
    std::vector<float> gV, rV, gr;
    buildCandles(gV, rV, d, n); buildGrid(gr, n);
    mGreen.init(gV, true); mRed.init(rV, true); mGrid.init(gr, false);
}

// ============================================================
//  Init game
// ============================================================
static void initGame() {
    g_pLit = mkP(VS_LIT, FS_LIT);
    g_pFlat = mkP(VS_FLAT, FS_FLAT);
    mLabel.init(); mHoverLine.init();
    mChr0.init(); mChr1.init(); mChr2.init(); mChr3.init(); mChr4.init();
    mLeekG.init(); mLeekW.init(); mLeekD.init(); mBoom.init(); mTornado.init();
    mLobR.init(); mLobD.init(); mBoxB.init(); mBoxD.init(); mClaw.init();

    initHardcoded0050();
    recalcLayout();
    g_tf = 0; g_visible = g_tfs[0].cnt;
    rebuildAll(g_tfs[0], g_visible);

    // Init bear
    g_bearActive = true; g_gameMode = true;
    g_bearIdx = g_visible / 2; g_bearPrevIdx = g_bearIdx;
    const Candle* bd = g_tfs[0].data;
    g_bearFromY = g_bearToY = toW(bd[g_bearIdx].h);
    g_bearPhase = 1.0;

    g_gameInited = true;
    LOGI("Game initialized: %d candles", g_visible);
}

// ============================================================
//  Process touch button actions
// ============================================================
static void processTouchActions() {
    double now = glfwGetTime();
    for (int i = 0; i < NUM_BTNS; i++) {
        if (!g_btns[i].pressed) continue;
        switch (g_btns[i].action) {
        case 0: // D - left
            if (g_bearPhase >= 1.0) {
                g_gameMoveDir = -1; g_bearFaceDir = -1; sfxJump();
            } break;
        case 1: // F - right
            if (g_bearPhase >= 1.0) {
                g_gameMoveDir = +1; g_bearFaceDir = +1; sfxJump();
            } break;
        case 2: // Jump
            if (now - g_bearJumpT > 0.6) { g_bearJumpT = now; sfxJump(); }
            break;
        case 3: // J - attack
            if (now - g_slashStartT > 0.45) {
                g_slashStartT = now; g_slashTextT = now; sfxSlash();
            } break;
        case 4: // K - magic
            if (g_bearMP >= 75.f && !g_bearDead) {
                g_bearMP -= 75.f;
                g_cashProjT = now;
                g_cashProjX = g_bearIdx * g_sp;
                g_cashProjY = 0.f;
                g_cashProjDir = g_bearFaceDir; if (g_cashProjDir == 0) g_cashProjDir = 1;
                g_magicT = now; sfxMagic();
            } break;
        }
        g_btns[i].pressed = false; // one-shot
    }
}

// ============================================================
//  Draw frame (main render loop body)
// ============================================================
static void drawFrame() {
    if (!g_egl.initialized || !g_gameInited) return;

    int fw = g_egl.width, fh = g_egl.height;
    int vw = fw;
    g_vpW = vw; g_vpH = fh;

    processTouchActions();

    glViewport(0, 0, vw, fh);
    glClearColor(.06f, .08f, .13f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glm::mat4 proj = glm::perspective(glm::radians(45.f), (float)vw / std::max(1, fh), .1f, 500.f);
    glm::mat4 view = g_cam.view(), MVP = proj * view, MV = view;
    glm::mat3 NM = glm::mat3(glm::transpose(glm::inverse(MV)));
    glm::vec3 litV = glm::vec3(view * glm::vec4(g_cam.tgt + glm::vec3(8, 20, 12), 1.f));

    // Draw K-bars
    glUseProgram(g_pLit);
    uM4(g_pLit, "uMVP", MVP); uM4(g_pLit, "uMV", MV); uM3(g_pLit, "uNM", NM); uV3(g_pLit, "uLit", litV);
    uV3(g_pLit, "uCol", {.90f, .22f, .22f}); mGreen.draw();
    uV3(g_pLit, "uCol", {.15f, .85f, .42f}); mRed.draw();

    // Draw grid
    glUseProgram(g_pFlat); uM4(g_pFlat, "uMVP", MVP);
    uV3(g_pFlat, "uCol", {.18f, .22f, .30f}); mGrid.draw(GL_LINES);

    // Bear animation
    if (g_bearActive && g_visible > 0 && g_tfs[g_tf].data) {
        const Candle* bd = g_tfs[g_tf].data + g_startIdx;
        int bn = std::min(g_visible, g_tfs[g_tf].cnt - g_startIdx);
        if (bn > 0) {
            double bnow = glfwGetTime();
            double bdt = 0.016; // ~60fps

            // Bear movement (same as desktop)
            if (g_bearPhase < 1.0) g_bearPhase += bdt / 0.30;
            if (g_bearPhase >= 1.0 && g_bearPhase < 1.01) {
                g_bearPhase = 1.0;
                g_bearPrevIdx = g_bearIdx;
                g_bearFromY = g_bearToY = toW(bd[g_bearIdx].h);
            }
            if (g_bearPhase >= 1.0 && g_gameMoveDir != 0) {
                g_bearPhase = 0.0;
                g_bearPrevIdx = g_bearIdx;
                int nxt = g_bearIdx + g_gameMoveDir * 10;
                nxt = std::max(0, std::min(bn - 1, nxt));
                g_bearIdx = nxt; g_gameMoveDir = 0;
                g_bearFromY = toW(bd[g_bearPrevIdx].h);
                g_bearToY = toW(bd[g_bearIdx].h);
            }
            g_bearIdx = std::min(g_bearIdx, bn - 1);

            float sc = std::max(0.50f, (WORLD_HI - WORLD_LO) * 0.22f);
            float bearX, bearY, jumpT = 0.f, squashT = 0.f;
            if (g_bearPhase >= 1.0) {
                bearX = (float)g_bearIdx * g_sp;
                bearY = toW(bd[g_bearIdx].h);
            } else {
                float t = (float)g_bearPhase;
                float e = t * t * (3.f - 2.f * t);
                bearX = g_bearPrevIdx * g_sp + (g_bearIdx * g_sp - g_bearPrevIdx * g_sp) * e;
                bearY = g_bearFromY + (g_bearToY - g_bearFromY) * e;
            }
            bearY += sc * 0.02f;

            // Jump
            float jumpElapsed = (float)(bnow - g_bearJumpT);
            if (jumpElapsed >= 0.f && jumpElapsed < 0.55f) {
                float arc = sinf(jumpElapsed / 0.55f * 3.14159f);
                bearY += sc * 3.5f * arc;
                jumpT = arc;
            }

            // Slash
            float slashElapsed = (float)(bnow - g_slashStartT);
            float slashT3 = (slashElapsed < 0.f) ? 0.f : std::min(1.f, slashElapsed / 0.45f);

            // Render bear
            bool happy = !g_bearDead && (bnow - g_bearHitT > 0.8);
            float actionSquash = squashT, actionX = bearX, actionY = bearY;
            if (slashT3 > 0.f && slashT3 < 1.f) {
                float sw2;
                if (slashT3 < 0.35f) sw2 = slashT3 / 0.35f * 0.3f;
                else if (slashT3 < 0.55f) sw2 = 0.3f + (slashT3 - 0.35f) / 0.20f * 0.7f;
                else sw2 = 1.f - (slashT3 - 0.55f) / 0.45f;
                actionSquash += sw2 * 0.28f;
                actionX += g_bearFaceDir * sc * 0.12f * sw2;
                actionY -= sc * 0.06f * sw2;
            }

            std::vector<float> c0, c1, c2, c3, c4;
            buildDonChan(c0, c1, c2, c3, c4, actionX, actionY, sc, jumpT, happy, actionSquash, slashT3, g_bearFaceDir);
            mChr0.upload(c0); mChr1.upload(c1); mChr2.upload(c2); mChr3.upload(c3); mChr4.upload(c4);

            glUseProgram(g_pLit);
            uM4(g_pLit, "uMVP", MVP); uM4(g_pLit, "uMV", MV); uM3(g_pLit, "uNM", NM); uV3(g_pLit, "uLit", litV);
            uV3(g_pLit, "uCol", {.74f, .54f, .34f}); mChr0.draw();
            uV3(g_pLit, "uCol", {.91f, .82f, .66f}); mChr1.draw();
            uV3(g_pLit, "uCol", {.97f, .94f, .88f}); mChr2.draw();
            uV3(g_pLit, "uCol", {.16f, .10f, .07f}); mChr3.draw();
            uV3(g_pLit, "uCol", {.82f, .84f, .88f}); mChr4.draw();

            // Camera follow bear
            float midX = bearX;
            g_cam.tgt = {midX, (WORLD_HI + WORLD_LO) * 0.5f, 0.f};
            g_cam.r = std::max((float)g_visible * g_sp * 0.55f + 10.f, 18.f);
        }
    }

    // ImGui overlay (HUD + touch buttons)
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Touch buttons
    for (int i = 0; i < NUM_BTNS; i++) {
        auto& b = g_btns[i];
        float x0 = b.x * fw, y0 = b.y * fh;
        float x1 = (b.x + b.w) * fw, y1 = (b.y + b.h) * fh;
        ImU32 col = b.pressed ? IM_COL32(100, 200, 255, 180) : IM_COL32(60, 80, 120, 100);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, 14.f);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(200, 200, 200, 140), 14.f, 0, 2.f);
        ImVec2 tsz = ImGui::CalcTextSize(b.label);
        dl->AddText(ImVec2(x0 + (x1 - x0 - tsz.x) * 0.5f, y0 + (y1 - y0 - tsz.y) * 0.5f),
                    IM_COL32(255, 255, 255, 220), b.label);
    }

    // HUD
    float hudX = 10.f, hudY = 8.f, barW = fw * 0.25f, barH = 18.f;
    float hpFrac = glm::clamp(g_bearHP / 100.f, 0.f, 1.f);
    dl->AddRectFilled(ImVec2(hudX, hudY), ImVec2(hudX + barW, hudY + barH), IM_COL32(40, 12, 12, 220), 4.f);
    dl->AddRectFilled(ImVec2(hudX, hudY), ImVec2(hudX + barW * hpFrac, hudY + barH), IM_COL32(200, 40, 40, 255), 4.f);
    dl->AddRect(ImVec2(hudX, hudY), ImVec2(hudX + barW, hudY + barH), IM_COL32(200, 200, 200, 180), 4.f);
    char hpBuf[24]; snprintf(hpBuf, sizeof(hpBuf), "HP %d/100", (int)g_bearHP);
    dl->AddText(ImVec2(hudX + 6, hudY + 2), IM_COL32(255, 255, 255, 240), hpBuf);

    float mpY = hudY + barH + 4;
    float mpFrac = glm::clamp(g_bearMP / 500.f, 0.f, 1.f);
    dl->AddRectFilled(ImVec2(hudX, mpY), ImVec2(hudX + barW, mpY + barH), IM_COL32(10, 15, 40, 220), 4.f);
    dl->AddRectFilled(ImVec2(hudX, mpY), ImVec2(hudX + barW * mpFrac, mpY + barH), IM_COL32(60, 160, 255, 255), 4.f);
    char mpBuf[24]; snprintf(mpBuf, sizeof(mpBuf), "MP %d/500", (int)g_bearMP);
    dl->AddText(ImVec2(hudX + 6, mpY + 2), IM_COL32(255, 255, 255, 240), mpBuf);

    char killBuf[24]; snprintf(killBuf, sizeof(killBuf), "Kills: %d", g_killCount);
    dl->AddText(ImVec2(hudX, mpY + barH + 6), IM_COL32(200, 210, 230, 220), killBuf);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(g_egl.display, g_egl.surface);
}

// ============================================================
//  Android input handling
// ============================================================
static int32_t handleInput(struct android_app* app, AInputEvent* event) {
    if (ImGui_ImplAndroid_HandleInputEvent(event)) return 1;

    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        float nx = x / g_egl.width;
        float ny = y / g_egl.height;

        if (action == AMOTION_EVENT_ACTION_DOWN) {
            for (int i = 0; i < NUM_BTNS; i++) {
                auto& b = g_btns[i];
                if (nx >= b.x && nx <= b.x + b.w && ny >= b.y && ny <= b.y + b.h)
                    b.pressed = true;
            }
        } else if (action == AMOTION_EVENT_ACTION_UP) {
            for (int i = 0; i < NUM_BTNS; i++) g_btns[i].pressed = false;
        }
        return 1;
    }
    return 0;
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
            initGame();
        }
        break;
    case APP_CMD_TERM_WINDOW:
        g_gameInited = false;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        termEGL();
        break;
    }
}

void android_main(struct android_app* app) {
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;

    while (!app->destroyRequested) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollAll(g_gameInited ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }
        if (g_gameInited) drawFrame();
    }
}
