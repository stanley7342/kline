#pragma once
// Chart geometry builders

// ============================================================
//  Geometry
// ============================================================
static float toW(float p){return WORLD_LO+(p-g_pMin)/(g_pMax-g_pMin)*(WORLD_HI-WORLD_LO);}
static std::vector<float> calcEMA(const std::vector<float>&s,int p){
    float k=2.f/(p+1);std::vector<float>r(s.size());r[0]=s[0];
    for(int i=1;i<(int)s.size();i++)r[i]=s[i]*k+r[i-1]*(1.f-k);return r;}

static void pushBox(std::vector<float>&v,float x0,float x1,float y0,float y1,float z0,float z1){
    auto q=[&](glm::vec3 n,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d){
        auto p=[&](glm::vec3 r){v.insert(v.end(),{r.x,r.y,r.z,n.x,n.y,n.z});};
        p(a);p(b);p(c);p(a);p(c);p(d);};
    glm::vec3 A{x0,y0,z0},B{x1,y0,z0},C{x1,y1,z0},D{x0,y1,z0};
    glm::vec3 E{x0,y0,z1},F{x1,y0,z1},G{x1,y1,z1},H{x0,y1,z1};
    q({0,0,1},E,F,G,H);q({0,0,-1},B,A,D,C);q({-1,0,0},A,E,H,D);
    q({1,0,0},F,B,C,G);q({0,1,0},H,G,C,D);q({0,-1,0},A,B,F,E);}

static void buildCandles(std::vector<float>&gV,std::vector<float>&rV,const Candle*d,int vis){
    for(int i=0;i<vis;i++){
        float cx=i*g_sp;bool bull=(d[i].c>=d[i].o);auto&dst=bull?gV:rV;
        float blo=toW(std::min(d[i].o,d[i].c)),bhi=toW(std::max(d[i].o,d[i].c));
        if(bhi-blo<.04f)bhi=blo+.04f;
        float wh=toW(d[i].h),wl=toW(d[i].l);
        pushBox(dst,cx-g_bHW,cx+g_bHW,blo,bhi,-g_bHW,g_bHW);
        if(wh>bhi+1e-4f)pushBox(dst,cx-g_wHW,cx+g_wHW,bhi,wh,-g_wHW,g_wHW);
        if(wl<blo-1e-4f)pushBox(dst,cx-g_wHW,cx+g_wHW,wl,blo,-g_wHW,g_wHW);}}

// 單根 K 棒帶誇張果凍效果
//   squash  : -1..+1  壓扁/拉長（同時寬度反向縮放）
//   yOffset : 整根棒上下位移量（果凍上下振盪）
static void buildOneCandleSquash(std::vector<float>&dst,const Candle&cd,
                                  int idx,float squash,float yOffset=0.f){
    float cx=idx*g_sp;
    // 整棒平移
    float blo=toW(std::min(cd.o,cd.c))+yOffset;
    float bhi=toW(std::max(cd.o,cd.c))+yOffset;
    if(bhi-blo<.04f)bhi=blo+.04f;
    float wh=toW(cd.h)+yOffset;
    float wl=toW(cd.l)+yOffset;
    float bodyH=bhi-blo, wickH=wh-blo;

    // 高度縮放（從底部 blo 往上壓縮或拉伸）
    float hScale=std::max(0.06f,1.f+squash);
    float newBhi=blo+bodyH*hScale;
    float newWh =blo+wickH*(1.f+squash*0.40f);
    newWh=std::max(newWh,newBhi+1e-4f);

    // 寬度反向縮放（體積守恆）：壓扁→大幅加寬；拉高→收窄
    float widthMult=(squash<0.f)
        ? (1.f+(-squash)*2.2f)   // 壓縮：最多 ~3×
        : (1.f- squash*0.75f);   // 拉高：最窄 0.55×
    widthMult=glm::clamp(widthMult,0.5f,3.2f);
    float bHW2=g_bHW*widthMult;

    pushBox(dst,cx-bHW2,cx+bHW2,blo,newBhi,-bHW2,bHW2);
    if(newWh>newBhi+1e-4f) pushBox(dst,cx-g_wHW,cx+g_wHW,newBhi,newWh,-g_wHW,g_wHW);
    if(wl<blo-1e-4f)       pushBox(dst,cx-g_wHW,cx+g_wHW,wl,blo,  -g_wHW,g_wHW);}

static void buildGrid(std::vector<float>&L,int nC){
    float x0=-g_sp,x1=nC*g_sp,zR=std::min(2.f,g_sp*.8f),bz=-zR-.05f;
    for(float z=-zR;z<=zR+.01f;z+=zR*.5f)L.insert(L.end(),{x0,WORLD_LO,z,x1,WORLD_LO,z});
    for(float x=x0;x<=x1+.01f;x+=g_sp*5)L.insert(L.end(),{x,WORLD_LO,-zR,x,WORLD_LO,zR});
    float st=(WORLD_HI-WORLD_LO)/6.f;
    for(float y=WORLD_LO;y<=WORLD_HI+.01f;y+=st)L.insert(L.end(),{x0,y,bz,x1,y,bz});
    for(float x=x0;x<=x1+.01f;x+=g_sp*5)L.insert(L.end(),{x,WORLD_LO,bz,x,WORLD_HI,bz});
    L.insert(L.end(),{x0,WORLD_LO,bz,x1,WORLD_LO,bz});L.insert(L.end(),{x0,WORLD_HI,bz,x1,WORLD_HI,bz});
    float sep1=(VOL_HI+WORLD_LO)*.5f,sep2=(MACD_HI+VOL_LO)*.5f;
    L.insert(L.end(),{x0,sep1,bz,x1,sep1,bz});L.insert(L.end(),{x0,sep1,-zR,x1,sep1,zR});
    L.insert(L.end(),{x0,sep2,bz,x1,sep2,bz});L.insert(L.end(),{x0,sep2,-zR,x1,sep2,zR});
    for(float x=x0;x<=x1+.01f;x+=g_sp*5)L.insert(L.end(),{x,VOL_LO,bz,x,VOL_HI,bz});
    L.insert(L.end(),{x0,VOL_LO,bz,x1,VOL_LO,bz});L.insert(L.end(),{x0,VOL_HI,bz,x1,VOL_HI,bz});
    L.insert(L.end(),{x0,MACD_MID,bz,x1,MACD_MID,bz});
    L.insert(L.end(),{x0,MACD_LO,bz,x1,MACD_LO,bz});L.insert(L.end(),{x0,MACD_HI,bz,x1,MACD_HI,bz});
    for(float x=x0;x<=x1+.01f;x+=g_sp*5)L.insert(L.end(),{x,MACD_LO,bz,x,MACD_HI,bz});}

static void buildSMA(std::vector<float>&L,const Candle*d,int vis,int period){
    if(vis<period)return;
    for(int i=period-1;i<vis;i++){
        float sum=0;for(int j=i-period+1;j<=i;j++)sum+=d[j].c;float y=toW(sum/period);
        if(i>period-1){float sum2=0;for(int j=i-period;j<=i-1;j++)sum2+=d[j].c;
            L.insert(L.end(),{(i-1)*g_sp,toW(sum2/period),0.f,(float)i*g_sp,y,0.f});}}}

static void buildVolume(std::vector<float>&gV,std::vector<float>&rV,const Candle*d,int vis){
    float maxV=0.01f;for(int i=0;i<vis;i++)maxV=std::max(maxV,d[i].v);
    float dh=g_bHW*.5f;
    for(int i=0;i<vis;i++){float cx=i*g_sp;bool bull=(d[i].c>=d[i].o);auto&dst=bull?gV:rV;
        pushBox(dst,cx-g_bHW*.85f,cx+g_bHW*.85f,VOL_LO,VOL_LO+d[i].v/maxV*(VOL_HI-VOL_LO),-dh,dh);}}

static void buildMACD(std::vector<float>&hG,std::vector<float>&hR,
                      std::vector<float>&difL,std::vector<float>&deaL,
                      const Candle*d,int vis){
    if(vis<2)return;
    std::vector<float>cl(vis);for(int i=0;i<vis;i++)cl[i]=d[i].c;
    auto e12=calcEMA(cl,12),e26=calcEMA(cl,26);
    std::vector<float>dif(vis);for(int i=0;i<vis;i++)dif[i]=e12[i]-e26[i];
    auto dea=calcEMA(dif,9);
    float mx=0.01f;for(int i=0;i<vis;i++){mx=std::max(mx,std::abs(dif[i]));mx=std::max(mx,std::abs(dif[i]-dea[i]));}
    float sc=MACD_HALF*.85f/mx,dh=g_bHW*.3f;
    for(int i=0;i<vis;i++){float hist=dif[i]-dea[i];float yb=MACD_MID,yt=MACD_MID+hist*sc;
        if(yt<yb)std::swap(yb,yt);if(yt-yb<.02f)yt=yb+.02f;
        float cx=i*g_sp;pushBox((hist>=0)?hG:hR,cx-g_bHW*.7f,cx+g_bHW*.7f,yb,yt,-dh,dh);}
    for(int i=1;i<vis;i++){
        float y0=glm::clamp(MACD_MID+dif[i-1]*sc,MACD_LO,MACD_HI);
        float y1=glm::clamp(MACD_MID+dif[i]*sc,MACD_LO,MACD_HI);
        difL.insert(difL.end(),{(i-1)*g_sp,y0,0.f,(float)i*g_sp,y1,0.f});
        y0=glm::clamp(MACD_MID+dea[i-1]*sc,MACD_LO,MACD_HI);
        y1=glm::clamp(MACD_MID+dea[i]*sc,MACD_LO,MACD_HI);
        deaL.insert(deaL.end(),{(i-1)*g_sp,y0,0.f,(float)i*g_sp,y1,0.f});}}

// KD 隨機指標（9 日）→ 映射至 MACD 區間（0→LO, 100→HI）
static void buildKD(std::vector<float>&kL,std::vector<float>&dL,
                    const Candle*d,int vis,int period=9){
    if(vis<period+1)return;
    std::vector<float>K(vis,50.f),D(vis,50.f);
    for(int i=period-1;i<vis;i++){
        float lo=d[i].l,hi=d[i].h;
        for(int j=i-period+1;j<i;j++){lo=std::min(lo,d[j].l);hi=std::max(hi,d[j].h);}
        float rsv=(hi-lo>1e-6f)?(d[i].c-lo)/(hi-lo)*100.f:50.f;
        K[i]=(i>period-1)?K[i-1]*2.f/3.f+rsv/3.f:rsv;
        D[i]=(i>period-1)?D[i-1]*2.f/3.f+K[i]/3.f:K[i];}
    float range=MACD_HI-MACD_LO;
    auto mapV=[&](float v){return MACD_LO+glm::clamp(v,0.f,100.f)/100.f*range;};
    for(int i=period;i<vis;i++){
        kL.insert(kL.end(),{(i-1)*g_sp,mapV(K[i-1]),0.f,(float)i*g_sp,mapV(K[i]),0.f});
        dL.insert(dL.end(),{(i-1)*g_sp,mapV(D[i-1]),0.f,(float)i*g_sp,mapV(D[i]),0.f});}}

// RSI（14日 Wilder 平滑法）→ 映射至 MACD 區間
static void buildRSI(std::vector<float>&L,const Candle*d,int vis,int period=14){
    if(vis<period+1)return;
    float avgG=0.f,avgL=0.f;
    for(int i=1;i<=period;i++){
        float df=d[i].c-d[i-1].c;
        if(df>0)avgG+=df; else avgL-=df;}
    avgG/=period; avgL/=period;
    std::vector<float>rsi(vis,50.f);
    {float rs=avgL>1e-10f?avgG/avgL:100.f;rsi[period]=100.f-100.f/(1.f+rs);}
    for(int i=period+1;i<vis;i++){
        float df=d[i].c-d[i-1].c;
        float g=df>0?df:0.f,l=df<0?-df:0.f;
        avgG=(avgG*(period-1)+g)/period; avgL=(avgL*(period-1)+l)/period;
        float rs=avgL>1e-10f?avgG/avgL:100.f;
        rsi[i]=100.f-100.f/(1.f+rs);}
    float range=MACD_HI-MACD_LO;
    auto mapV=[&](float v){return MACD_LO+glm::clamp(v,0.f,100.f)/100.f*range;};
    for(int i=period+1;i<vis;i++)
        L.insert(L.end(),{(i-1)*g_sp,mapV(rsi[i-1]),0.f,(float)i*g_sp,mapV(rsi[i]),0.f});}

// 布林通道（20日 MA ± 2σ）→ 主圖
static void buildBollinger(std::vector<float>&upL,std::vector<float>&dnL,
                           std::vector<float>&midL,const Candle*d,int vis,
                           int period=20,float mult=2.f){
    if(vis<period)return;
    std::vector<float>ma(vis,0.f),sd(vis,0.f);
    for(int i=period-1;i<vis;i++){
        float sum=0;for(int j=i-period+1;j<=i;j++)sum+=d[j].c;
        ma[i]=sum/period;
        float var=0;for(int j=i-period+1;j<=i;j++)var+=(d[j].c-ma[i])*(d[j].c-ma[i]);
        sd[i]=sqrtf(var/period);}
    for(int i=period;i<vis;i++){
        float cx=(float)i*g_sp,px=(float)(i-1)*g_sp;
        upL.insert(upL.end(),{px,toW(ma[i-1]+mult*sd[i-1]),0.f,cx,toW(ma[i]+mult*sd[i]),0.f});
        dnL.insert(dnL.end(),{px,toW(ma[i-1]-mult*sd[i-1]),0.f,cx,toW(ma[i]-mult*sd[i]),0.f});
        midL.insert(midL.end(),{px,toW(ma[i-1]),0.f,cx,toW(ma[i]),0.f});}}

