#pragma once
// Game state structs and global variables

// ============================================================
//  Camera
// ============================================================
struct Camera{
    float r=50.f,yaw=0.f,pitch=15.f; // 正中心3D角度
    glm::vec3 tgt{0,7.5f,0};
    glm::vec3 eye()const{float rp=r*cosf(glm::radians(pitch));
        return tgt+glm::vec3(rp*sinf(glm::radians(yaw)),r*sinf(glm::radians(pitch)),rp*cosf(glm::radians(yaw)));}
    glm::mat4 view()const{return glm::lookAt(eye(),tgt,{0,1,0});}
    void drag(float dx,float dy){yaw+=dx*.4f;pitch=glm::clamp(pitch-dy*.4f,5.f,85.f);}
    void zoom(float d){r=glm::clamp(r-d*2.f,3.f,120.f);}
}g_cam;

// 每個週期 scroll 縮放上下限（對應 1 年份 ~ 5 根）
static const int TF_MIN_VIS[]={5,  5,  3, 2, 1};   // 最少顯示蠟燭數
static const int TF_MAX_VIS[]={252,52,12, 4, 5};    // 最多顯示蠟燭數（~1 年）

static inline int tfMaxVis(int tf){
    return std::min(TF_MAX_VIS[tf], g_tfs[tf].cnt);}

// 副圖顯示開關
static bool g_showVol=true, g_showMACD=true, g_showKD=false;
static bool g_showRSI=false, g_showBB=false;
// 均線顯示開關
static bool g_showMA[5]={true,true,true,false,false}; // MA5,20,60,120,240
// 視口尺寸（每幀更新，recalcLayout 垂直縮放用）
static int  g_vpW=1, g_vpH=720;

// 依開關重算各子圖的 Y 區間，保持頂部 (WORLD_HI=22) 不動
static void recalcLayout(){
    const float TOP   = 22.f;
    // 基準比例：1280×720 視窗，panel=290 → viewport≈990×720，aspect≈1.375
    // 視窗越高（aspect越小）→ TOTAL 越大，讓 K 線在世界座標也佔更多垂直空間
    const float BASE_ASPECT = 1.375f;
    float curAspect = (float)std::max(g_vpW,1) / (float)std::max(g_vpH,1);
    float TOTAL = 30.f * (BASE_ASPECT / std::max(0.4f, curAspect));
    TOTAL = glm::clamp(TOTAL, 20.f, 80.f); // 防極端值
    const float GAP   = 0.8f;   // 面板間隙

    bool hasSub = g_showMACD || g_showKD || g_showRSI;

    // 各子圖權重（主圖大，副圖小）
    float wMain = 6.0f;
    float wVol  = g_showVol ? 1.0f : 0.f;
    float wSub  = hasSub     ? 1.2f : 0.f;
    float wTot  = wMain + wVol + wSub;

    int nGaps = (g_showVol?1:0) + (hasSub?1:0);
    float usable = TOTAL - nGaps * GAP;

    float hMain = usable * wMain / wTot;
    float hVol  = usable * wVol  / wTot;

// ============================================================
//  Mesh
// ============================================================
static Mesh mGreen,mRed,mGrid,mMA5,mMA20,mMA60,mMA120,mMA240;
static Mesh mVolG,mVolR,mMHG,mMHR,mDIF,mDEA,mKK,mKDLine;
static Mesh mRSI,mBBUpper,mBBLower,mBBMid;
static DynMesh mLabel,mHoverLine;
// ── 拉拉熊 ──
static DynLitMesh mChr0,mChr1,mChr2,mChr3; // 棕/奶褐/淡奶/深棕
static DynLitMesh mChr4;                    // 劍（金色）
static DynLitMesh mHitBar;               // 被踩彈跳的 K 棒
static DynLitMesh mLeekG,mLeekW,mLeekD;  // 韭菜（綠/白/深色）
static int    g_hitBarIdx    = -1;       // 彈跳中的 K 棒索引（-1=無）
static double g_hitBarStartT = 0.0;      // 彈跳開始時間
static bool   g_hitBarBull   = false;    // 是否為紅K
static float  g_prevBearPhase= 1.0f;     // 上一幀 phase（偵測落地瞬間）
static bool   g_bearActive   = true;
static bool   g_bearNeedsInit= true;
static double g_bearPhase    = 0.4;  // 0..1
static double g_bearLastTime = 0.0;
static int    g_bearIdx      = 0;
static int    g_bearPrevIdx  = 0;
static float  g_bearFromY    = 0.f;
static float  g_bearToY      = 0.f;
static float  g_bearScreenX  = -1.f; // 每幀更新，-1=不顯示
static float  g_bearScreenY  = -1.f;

// ── 遊戲模式（F=左, J=右, 巨蟻怪物）──────────────────────────────
static bool   g_gameMode     = true;
static int    g_bearFaceDir  = -1;    // -1=左, 0=正面, +1=右（預設朝左）
static double g_bearJumpT    = -9.0;  // 跳躍開始時間（空白鍵）
static int    g_gameMoveDir  = 0;     // -1/0/+1 （F/stay/J）

struct AntMonster {
    float  x;           // 世界 X 座標
    bool   alive;
    bool   facingLeft;
    double spawnT;      // 出生時間（走路循環用）
    double deathT;      // 死亡時間（負 = 未死）
    float  hp;          // 生命（滿=50）
    double lastHitT;    // 上次被擊中時間（避免同一刀多段傷害）
};
static std::vector<AntMonster> g_ants;
static double g_lastAntSpawn = -99.0;
static double g_slashStartT  = -9.0;  // 揮劍開始時間
static double g_slashTextT   = -9.0;  // 「割韭菜」文字顯示時間
// 鈔票投射物
static double g_cashProjT    = -9.0;  // 投射開始時間
static float  g_cashProjX    = 0.f;   // 起點 X
static float  g_cashProjY    = 0.f;   // 起點 Y
static int    g_cashProjDir  = 1;     // -1=左, +1=右
static float  g_cashProjEndX = 0.f;  // 最遠目標 X
static int    g_killCount     = 0;     // 擊殺數
static float  g_bearHP        = 100.f; // 熊的血量
static float  g_bearMP        = 500.f; // 魔法值（滿=500）
static double g_bearHitT      = -9.0;   // 熊上次被打時間
static double g_magicT        = -9.0;   // 魔法開始時間
static bool   g_bearDead      = false;  // 熊死亡狀態
static double g_bearDeadT     = -9.0;   // 死亡時間
static DynLitMesh mBoom;               // 爆炸粒子
static DynLitMesh mTornado;            // 龍捲風
static DynLitMesh mLobR,mLobD;        // 龍蝦（紅/深色）
static DynLitMesh mBoxB,mBoxD;        // 紙箱（箱色/深色）
static DynLitMesh mClaw;              // 龍蝦丟螯投射物

struct ClawProj {
    float x, y;       // 世界座標
    float vx, vy;     // 初始速度
    float ox, oy;     // 發射源（龍蝦位置，迴力鏢返回點）
    float tx, ty;     // 目標位置
    double spawnT;
    bool  alive;
    bool  returning;  // 是否在回程
};
static std::vector<ClawProj> g_claws;

// 飄浮傷害數字
struct DmgFloat{float x,y;int dmg;double t;bool crit;};
static std::vector<DmgFloat>g_dmgFloats;
static void addDmgFloat(float x,float y,int dmg,bool crit=false){
    g_dmgFloats.push_back({x,y,dmg,glfwGetTime(),crit});}

// 斬擊軌跡
struct SlashTrail{float x,y,ang;double t;};
static std::vector<SlashTrail>g_trails;

struct Lobster {
    float  x, y;        // 世界座標
    bool   alive;
    bool   facingLeft;
    double spawnT;
    double deathT;
    float  hp;          // 80
    double lastHitT;
    float  flySpeed;    // 隨機飛行速度
    float  flyAng;      // 飛行方向角
};
static std::vector<Lobster> g_lobs;
static double g_lastLobSpawn = -99.0;

struct BoxMon {
    float  x;
    bool   alive;
    double spawnT, deathT;
    float  hp;          // 200
    double lastHitT;
};
static std::vector<BoxMon> g_boxes;
static double g_lastBoxSpawn = -99.0;
struct HPBarInfo{float sx,sy,frac;};   // 血條螢幕座標
static std::vector<HPBarInfo>g_hpBars; // 每幀更新

