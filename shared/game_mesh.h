#pragma once
// Shaders, Mesh types, pushBox, Font

static int  g_tf=0,g_visible=0,g_startIdx=0;
static bool g_playing=false;
static double g_lastStep=0,g_stepInt=1.0/60.0;  // 預設速度 60x（快速播放）
static bool g_dirty=true;
static int  g_prevVW=0,g_prevVH=0; // 上一幀視窗尺寸（偵測 resize）
static bool g_drag=false;
static double g_lx=0,g_ly=0;
static float g_sp=0.4f,g_bHW=0.12f,g_wHW=0.018f;
// ── Phase 1 新增 ──
static float g_hoverWY=0.f;          // 滑鼠 Y 世界座標（十字準線用）
static bool  g_panDragging=false;    // 時間軸平移拖曳中
static double g_panDragStartX=0.0;
static int   g_panDragStartOffset=0;
static int   g_panOffset=0;          // 0=最新; 正值=往左看舊資料
static float g_pMin=0,g_pMax=1;

// ============================================================
//  Shaders
// ============================================================
#ifdef __EMSCRIPTEN__
#define _GV "#version 300 es\nprecision highp float;\n"
#else
#define _GV "#version 330 core\n"
#endif
static const char* VS_LIT= _GV R"(layout(location=0)in vec3 aP;layout(location=1)in vec3 aN;
uniform mat4 uMVP,uMV;uniform mat3 uNM;out vec3 fP,fN;
void main(){fP=vec3(uMV*vec4(aP,1));fN=uNM*aN;gl_Position=uMVP*vec4(aP,1);})";
static const char* FS_LIT= _GV R"(in vec3 fP,fN;uniform vec3 uCol,uLit;out vec4 oC;
void main(){vec3 n=normalize(fN),l=normalize(uLit-fP),h=normalize(l+normalize(-fP));
float d=max(dot(n,l),0.),s=pow(max(dot(n,h),0.),64.)*.6;
oC=vec4((0.22+d+s)*uCol,1.);})";
static const char* VS_FLAT= _GV R"(layout(location=0)in vec3 aP;uniform mat4 uMVP;
void main(){gl_Position=uMVP*vec4(aP,1);})";
static const char* FS_FLAT= _GV R"(uniform vec3 uCol;out vec4 oC;void main(){oC=vec4(uCol,1);})";

// Billboard textured sprite shader
static const char* VS_TEX= _GV R"(layout(location=0)in vec3 aP;layout(location=1)in vec2 aUV;
uniform mat4 uMVP;out vec2 vUV;
void main(){vUV=aUV;gl_Position=uMVP*vec4(aP,1);})";
static const char* FS_TEX= _GV R"(in vec2 vUV;uniform sampler2D uTex;out vec4 oC;
void main(){oC=texture(uTex,vUV);if(oC.a<0.1)discard;})";

static GLuint g_pTex=0; // textured shader program

// Texture loading
static GLuint loadTexture(const char*path){
    int w,h,ch;
    unsigned char*data=stbi_load(path,&w,&h,&ch,4);
    if(!data){std::cerr<<"Failed to load: "<<path<<"\n";return 0;}
    GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    stbi_image_free(data);return tex;}

// Sprite textures
static GLuint g_texBear=0,g_texLeek=0,g_texLobster=0,g_texBox=0;
static GLuint g_texScythe=0,g_texGirl=0,g_texMoney=0,g_texClaw=0;

// Billboard VAO (reusable quad)
static GLuint g_bbVAO=0,g_bbVBO=0;
static void initBillboard(){
    glGenVertexArrays(1,&g_bbVAO);glGenBuffers(1,&g_bbVBO);
    glBindVertexArray(g_bbVAO);glBindBuffer(GL_ARRAY_BUFFER,g_bbVBO);
    float quad[]={
        -0.5f,0.f,0.f, 0.f,1.f,  0.5f,0.f,0.f, 1.f,1.f,
         0.5f,1.f,0.f, 1.f,0.f, -0.5f,0.f,0.f, 0.f,1.f,
         0.5f,1.f,0.f, 1.f,0.f, -0.5f,1.f,0.f, 0.f,0.f};
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,20,0);glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,20,(void*)12);glEnableVertexAttribArray(1);
    glBindVertexArray(0);}

// Draw a billboard sprite at world position (wx,wy) with size (sw,sh)
static void drawSprite(GLuint tex,float wx,float wy,float sw,float sh,const glm::mat4&MVP,const glm::mat4&view){
    if(!tex)return;
    glUseProgram(g_pTex);
    // Billboard: extract camera right and up from view matrix
    glm::vec3 right(view[0][0],view[1][0],view[2][0]);
    glm::vec3 up(0,1,0); // keep upright
    glm::vec3 pos(wx,wy,0);
    // Build model matrix for billboard
    glm::mat4 model(1.f);
    model[0]=glm::vec4(right*sw,0);
    model[1]=glm::vec4(up*sh,0);
    model[2]=glm::vec4(glm::cross(right,up),0);
    model[3]=glm::vec4(pos,1);
    glm::mat4 mvp=MVP*model;
    glUniformMatrix4fv(glGetUniformLocation(g_pTex,"uMVP"),1,GL_FALSE,glm::value_ptr(mvp));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);
    glUniform1i(glGetUniformLocation(g_pTex,"uTex"),0);
    glBindVertexArray(g_bbVAO);glDrawArrays(GL_TRIANGLES,0,6);glBindVertexArray(0);}

static GLuint cSh(GLenum t,const char*s){
    GLuint sh=glCreateShader(t);glShaderSource(sh,1,&s,nullptr);glCompileShader(sh);
    GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){char b[512];glGetShaderInfoLog(sh,512,nullptr,b);std::cerr<<b;}return sh;}
static GLuint mkP(const char*vs,const char*fs){
    GLuint p=glCreateProgram(),v=cSh(GL_VERTEX_SHADER,vs),f=cSh(GL_FRAGMENT_SHADER,fs);
    glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    glDeleteShader(v);glDeleteShader(f);return p;}

struct Mesh{GLuint vao=0,vbo=0;int n=0;
    void init(const std::vector<float>&d,bool nr){
        if(d.empty())return;n=(int)d.size()/(nr?6:3);
        glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,d.size()*4,d.data(),GL_STATIC_DRAW);
        int st=nr?6:3;
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,st*4,0);glEnableVertexAttribArray(0);
        if(nr){glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);glEnableVertexAttribArray(1);}
        glBindVertexArray(0);}
    void draw(GLenum m=GL_TRIANGLES){if(n){glBindVertexArray(vao);glDrawArrays(m,0,n);glBindVertexArray(0);}}
    void del(){if(vao){glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);vao=0;n=0;}}};
struct DynMesh{GLuint vao=0,vbo=0;int n=0;
    void init(){glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,0,nullptr,GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,0);glEnableVertexAttribArray(0);
        glBindVertexArray(0);}
    void upload(const std::vector<float>&d){n=(int)d.size()/3;
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,d.size()*4,d.data(),GL_DYNAMIC_DRAW);}
    void draw(){if(n){glBindVertexArray(vao);glDrawArrays(GL_LINES,0,n);glBindVertexArray(0);}}
    void del(){if(vao){glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);vao=0;n=0;}}};
// 動態 lit mesh（帶法向量，給拉拉熊用）
struct DynLitMesh{GLuint vao=0,vbo=0;int n=0;
    void init(){glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,0,nullptr,GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,0);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);glEnableVertexAttribArray(1);
        glBindVertexArray(0);}
    void upload(const std::vector<float>&d){n=(int)d.size()/6;
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,d.size()*4,d.data(),GL_DYNAMIC_DRAW);}
    void draw(){if(n){glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,n);glBindVertexArray(0);}}
    void del(){if(vao){glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);vao=0;n=0;}}};

// ============================================================
//  Font
// ============================================================
using Seg=std::array<float,4>;
static const std::map<char,std::vector<Seg>> FONT={
    {'0',{{0,0,4,0},{0,0,0,6},{4,0,4,6},{0,6,4,6}}},{'1',{{2,0,2,6}}},
    {'2',{{0,6,4,6},{4,6,4,3},{0,3,4,3},{0,3,0,0},{0,0,4,0}}},
    {'3',{{0,6,4,6},{4,6,4,0},{0,3,4,3},{0,0,4,0}}},{'4',{{0,6,0,3},{0,3,4,3},{4,6,4,0}}},
    {'5',{{0,6,4,6},{0,6,0,3},{0,3,4,3},{4,3,4,0},{0,0,4,0}}},
    {'6',{{4,6,0,6},{0,6,0,0},{0,0,4,0},{4,0,4,3},{0,3,4,3}}},{'7',{{0,6,4,6},{4,6,4,0}}},
    {'8',{{0,0,4,0},{4,0,4,6},{0,6,4,6},{0,6,0,0},{0,3,4,3}}},
    {'9',{{0,6,4,6},{4,6,4,0},{0,0,4,0},{0,6,0,3},{0,3,4,3}}},
    {'.',{{1,0,3,0},{3,0,3,2},{3,2,1,2},{1,2,1,0}}},{'-',{{0,3,4,3}}},{'/',{{4,0,0,6}}},
    {'A',{{0,0,0,3},{0,3,2,6},{2,6,4,3},{4,3,4,0},{0,3,4,3}}},
    {'C',{{4,6,0,6},{0,6,0,0},{0,0,4,0}}},
    {'D',{{0,0,0,6},{0,6,3,6},{3,6,4,5},{4,5,4,1},{4,1,3,0},{3,0,0,0}}},
    {'E',{{0,0,0,6},{0,6,4,6},{0,3,4,3},{0,0,4,0}}},
    {'F',{{0,0,0,6},{0,6,4,6},{0,3,3,3}}},
    {'I',{{2,0,2,6}}},{'L',{{0,6,0,0},{0,0,4,0}}},
    {'M',{{0,0,0,6},{0,6,2,3},{2,3,4,6},{4,6,4,0}}},
    {'O',{{0,0,4,0},{4,0,4,6},{4,6,0,6},{0,6,0,0}}},
    {'V',{{0,6,2,0},{2,0,4,6}}},
    {'K',{{0,0,0,6},{0,3,4,6},{0,3,4,0}}},{' ',{}},
};
static void appendText(std::vector<float>&L,const char*str,glm::vec3 an,
                       glm::vec3 R,glm::vec3 U,float sc,float ax=0.f){
    int len=(int)strlen(str);float tw=len*5.f*sc,sx=-tw*ax;
    for(int ci=0;ci<len;ci++){
        auto it=FONT.find(str[ci]);if(it==FONT.end())continue;
        float ox=sx+ci*5.f*sc;
        for(const Seg&s:it->second){
            glm::vec3 p0=an+R*(ox+s[0]*sc)+U*(s[1]*sc);
            glm::vec3 p1=an+R*(ox+s[2]*sc)+U*(s[3]*sc);
            L.insert(L.end(),{p0.x,p0.y,p0.z,p1.x,p1.y,p1.z});}}}

