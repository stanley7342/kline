// ============================================================
//  3D K 線圖 (台股)  OpenGL 3.3 / ES 3.0 + Dear ImGui
// ============================================================
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifndef __EMSCRIPTEN__
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"winmm.lib")
#endif

#include <iostream>
#include <vector>
#include <array>
#include <map>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif
#include <atomic>
#include <ctime>
#include <fstream>

static const int WIN_W=1280,WIN_H=720;
static const int PANEL_W=290;   // ImGui panel width (px)
// 版面區間（動態，由 recalcLayout() 更新）
static float WORLD_LO=10.f,WORLD_HI=22.f;
static float VOL_LO=3.f,VOL_HI=9.f;
static float MACD_LO=-7.f,MACD_HI=1.8f;
static float MACD_MID=(MACD_LO+MACD_HI)*.5f;
static float MACD_HALF=(MACD_HI-MACD_LO)*.5f;

struct Candle{float o,h,l,c,v;};

// ============================================================
//  Dynamic data
// ============================================================
struct Mark{int idx;const char* lbl;};

struct TFStore{
    std::string name;
    std::vector<Candle> cands;
    std::vector<std::string> dates;      // one date string per candle (may be empty)
    std::vector<std::string> markStrs;
    std::vector<Mark> marks;
    std::vector<std::string> lblStrs;
    std::vector<const char*> lblPtrs;
    float pStep=2.f;
    bool perLbl=false;

    void clear(){cands.clear();dates.clear();markStrs.clear();marks.clear();lblStrs.clear();lblPtrs.clear();}
    void addMark(int i,const std::string&s){markStrs.push_back(s);marks.push_back({i,nullptr});}
    void addLabel(const std::string&s){lblStrs.push_back(s);}
    void rebuildPtrs(){
        for(int i=0;i<(int)marks.size();i++)
            marks[i].lbl=i<(int)markStrs.size()?markStrs[i].c_str():"";
        lblPtrs.clear();
        for(auto&s:lblStrs)lblPtrs.push_back(s.c_str());}
};
static TFStore g_store[5];

struct TF{const char*name;const Candle*data;const char*const*labels;
          int cnt;float pStep;const Mark*marks;int nmarks;};
static TF g_tfs[5];

static void buildTFViews(){
    for(int i=0;i<5;i++){
        TFStore&s=g_store[i];s.rebuildPtrs();
        g_tfs[i]={s.name.c_str(),
                  s.cands.empty()?nullptr:s.cands.data(),
                  nullptr,(int)s.cands.size(),s.pStep,nullptr,0};
        if(s.perLbl&&!s.lblPtrs.empty())g_tfs[i].labels=s.lblPtrs.data();
        else if(!s.marks.empty()){g_tfs[i].marks=s.marks.data();g_tfs[i].nmarks=(int)s.marks.size();}}}

// ============================================================
//  App state
// ============================================================
static std::string g_stockCode="2330";
static std::string g_stockName="台積電";
static std::atomic<int>  g_loadProgress{0},g_loadTotal{60};
static std::atomic<bool> g_loadDone{false},g_loadFail{false};

enum AppMode{MENU,NORMAL,LOADING}; // MENU=啟動選股
static AppMode g_mode=MENU; // 啟動時先選股

static int  g_hoveredCandle=-1;   // index of candle under mouse (-1=none)

static GLFWwindow* g_win=nullptr;

// ============================================================
//  WinHTTP GET (HTTPS)
// ============================================================
//  Sound effects（非同步，不卡主線程）
// ============================================================
#ifdef __EMSCRIPTEN__
static void _jsBeep(int f,int d){
    char buf[256];
    snprintf(buf,sizeof(buf),
        "try{var a=new AudioContext();var o=a.createOscillator();var g=a.createGain();"
        "o.connect(g);g.connect(a.destination);o.frequency.value=%d;g.gain.value=0.06;"
        "o.start();o.stop(a.currentTime+%d/1000.0);}catch(e){}",f,d);
    emscripten_run_script(buf);
}
static void sfxSlash(){_jsBeep(800,60);_jsBeep(1200,40);}
static void sfxHit(){_jsBeep(300,50);}
static void sfxKill(){_jsBeep(1400,30);_jsBeep(1800,30);_jsBeep(2200,40);}
static void sfxMagic(){_jsBeep(600,60);_jsBeep(1000,60);_jsBeep(2000,80);}
static void sfxJump(){_jsBeep(600,30);_jsBeep(900,30);}
static void sfxBearHit(){_jsBeep(200,80);}
#else
static void sfxSlash(){std::thread([]{Beep(800,60);Beep(1200,40);}).detach();}
static void sfxHit(){std::thread([]{Beep(300,50);}).detach();}
static void sfxKill(){std::thread([]{Beep(1400,30);Beep(1800,30);Beep(2200,40);}).detach();}
static void sfxMagic(){std::thread([]{
    for(int i=0;i<8;i++){Beep(400+i*150,40);}Beep(2000,80);}).detach();}
static void sfxJump(){std::thread([]{Beep(600,30);Beep(900,30);}).detach();}
static void sfxBearHit(){std::thread([]{Beep(200,80);}).detach();}
#endif

// ============================================================
#ifndef __EMSCRIPTEN__
static std::string httpGet(const std::wstring&host,const std::wstring&path,
                           const std::wstring&hdrs=L""){
    std::string res;
    HINTERNET hS=WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hS)return res;
    HINTERNET hC=WinHttpConnect(hS,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!hC){WinHttpCloseHandle(hS);return res;}
    HINTERNET hR=WinHttpOpenRequest(hC,L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!hR){WinHttpCloseHandle(hC);WinHttpCloseHandle(hS);return res;}
    const wchar_t*h=hdrs.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:hdrs.c_str();
    DWORD hl=hdrs.empty()?0:(DWORD)-1;
    if(WinHttpSendRequest(hR,h,hl,WINHTTP_NO_REQUEST_DATA,0,0,0)&&
       WinHttpReceiveResponse(hR,nullptr)){
        DWORD av=0;
        while(WinHttpQueryDataAvailable(hR,&av)&&av>0){
            std::vector<char>buf(av+1,0);DWORD rd=0;
            WinHttpReadData(hR,buf.data(),av,&rd);res.append(buf.data(),rd);}}
    WinHttpCloseHandle(hR);WinHttpCloseHandle(hC);WinHttpCloseHandle(hS);
    return res;}

// ============================================================
//  TWSE parser
// ============================================================
static float stripCommas(const std::string&s){
    std::string c;for(char x:s)if(x!=',')c+=x;
    try{return std::stof(c);}catch(...){return 0.f;}}

static bool parseTWSE(const std::string&json,std::vector<Candle>&out,
                      std::vector<std::string>&dates,std::string&title){
    if(json.find("\"OK\"")==std::string::npos)return false;
    auto exStr=[&](const std::string&key)->std::string{
        std::string q="\""+key+"\":\"";
        size_t p=json.find(q);if(p==std::string::npos)return "";
        p+=q.size();size_t e=json.find('"',p);
        return e==std::string::npos?"":json.substr(p,e-p);};
    title=exStr("title");
    size_t dp=json.find("\"data\":");if(dp==std::string::npos)return false;
    size_t pos=json.find('[',dp+7);if(pos==std::string::npos)return false;
    while(true){
        size_t rs=json.find('[',pos+1);if(rs==std::string::npos)break;
        size_t re=json.find(']',rs);if(re==std::string::npos)break;
        std::string row=json.substr(rs+1,re-rs-1);
        std::vector<std::string>f;size_t fp=0;
        while(fp<row.size()){
            size_t qs=row.find('"',fp);if(qs==std::string::npos)break;
            size_t qe=row.find('"',qs+1);if(qe==std::string::npos)break;
            f.push_back(row.substr(qs+1,qe-qs-1));fp=qe+1;}
        if(f.size()>=7){
            Candle c;c.o=stripCommas(f[3]);c.h=stripCommas(f[4]);
            c.l=stripCommas(f[5]);c.c=stripCommas(f[6]);
            c.v=stripCommas(f[1])/1e6f;
            if(c.o>0&&c.h>0&&c.l>0&&c.c>0){out.push_back(c);dates.push_back(f[0]);}}
        pos=re;}
    return !out.empty();}

// ============================================================
//  Aggregation
// ============================================================
static std::string dateLabel(const std::string&d){
    int roc=std::stoi(d.substr(0,d.find('/')));
    int m  =std::stoi(d.substr(d.find('/')+1,2));
    char b[8];snprintf(b,sizeof(b),"%d/%02d",m,(roc+1911)%100);return b;}
static std::string monthKey(const std::string&d){return d.substr(0,d.rfind('/'));}
static std::string qtrKey(const std::string&mk){
    int roc=std::stoi(mk.substr(0,mk.find('/')));
    int m  =std::stoi(mk.substr(mk.find('/')+1));
    char b[8];snprintf(b,sizeof(b),"%02d-Q%d",(roc+1911)%100,(m-1)/3+1);return b;}
static std::string yrKey(const std::string&mk){
    int roc=std::stoi(mk.substr(0,mk.find('/')));
    char b[6];snprintf(b,sizeof(b),"%4d",roc+1911);return b;}

struct AggC{std::string key;Candle c;};
static void aggPush(std::vector<AggC>&v,const std::string&k,const Candle&c){
    if(v.empty()||v.back().key!=k)v.push_back({k,c});
    else{auto&a=v.back().c;a.h=std::max(a.h,c.h);a.l=std::min(a.l,c.l);a.c=c.c;a.v+=c.v;}}

static void buildAllTFs(const std::vector<Candle>&daily,
                        const std::vector<std::string>&dates,
                        const std::string&code,const std::string&nameIn,
                        TFStore out[5]){
    int n=(int)daily.size();
    // TF0: daily last 1 year
    int d0=std::max(0,n-252);
    out[0].clear();out[0].perLbl=false;out[0].pStep=2.f;
    out[0].name=code+" "+nameIn+" | 日線 1年";
    int lastM=-1,lastY=-1;
    for(int i=d0;i<n;i++){
        out[0].cands.push_back(daily[i]);
        if(i<(int)dates.size()){
            // store display date "MM/DD/YYYY" from ROC "114/10/01"
            const std::string&rd=dates[i];
            int roc=std::stoi(rd.substr(0,rd.find('/')));
            int m  =std::stoi(rd.substr(rd.find('/')+1,2));
            int d  =std::stoi(rd.substr(rd.rfind('/')+1));
            char ds[16];snprintf(ds,sizeof(ds),"%04d/%02d/%02d",roc+1911,m,d);
            out[0].dates.push_back(ds);
            if(m!=lastM||roc+1911!=lastY){lastM=m;lastY=roc+1911;
                out[0].addMark((int)out[0].cands.size()-1,dateLabel(dates[i]));}}}
    // Aggregate monthly
    std::vector<AggC> byMon;
    for(int i=0;i<n;i++)aggPush(byMon,i<(int)dates.size()?monthKey(dates[i]):"?",daily[i]);
    // TF1: weekly last ~52 weeks
    out[1].clear();out[1].perLbl=false;out[1].pStep=2.f;
    out[1].name=code+" "+nameIn+" | 週線";
    {int w0=std::max(0,n-260);int lm=-1,ly=-1;
     for(int i=w0;i<n;i+=5){
         int e=std::min(i+5,n);Candle wk=daily[i];wk.v=0;
         for(int j=i;j<e;j++){wk.h=std::max(wk.h,daily[j].h);wk.l=std::min(wk.l,daily[j].l);wk.c=daily[j].c;wk.v+=daily[j].v;}
         out[1].cands.push_back(wk);
         if(i<(int)dates.size()){int roc=std::stoi(dates[i].substr(0,dates[i].find('/')));
             int m=std::stoi(dates[i].substr(dates[i].find('/')+1,2));
             if(m!=lm||roc+1911!=ly){lm=m;ly=roc+1911;out[1].addMark((int)out[1].cands.size()-1,dateLabel(dates[i]));}}}
    }
    // TF2: monthly
    out[2].clear();out[2].perLbl=false;out[2].pStep=5.f;
    out[2].name=code+" "+nameIn+" | 月線";
    {int ly2=-1;for(int i=0;i<(int)byMon.size();i++){
        out[2].cands.push_back(byMon[i].c);
        int roc=std::stoi(byMon[i].key.substr(0,byMon[i].key.find('/')));int yr=roc+1911;
        if(yr!=ly2){ly2=yr;char b[6];snprintf(b,sizeof(b),"%d",yr);out[2].addMark(i,b);}}}
    // TF3: quarterly
    out[3].clear();out[3].perLbl=true;out[3].pStep=5.f;
    out[3].name=code+" "+nameIn+" | 季線";
    {std::vector<AggC>bq;for(auto&m:byMon)aggPush(bq,qtrKey(m.key),m.c);
     for(auto&q:bq){out[3].cands.push_back(q.c);out[3].addLabel(q.key);}out[3].rebuildPtrs();}
    // TF4: yearly
    out[4].clear();out[4].perLbl=true;out[4].pStep=10.f;
    out[4].name=code+" "+nameIn+" | 年線";
    {std::vector<AggC>by;for(auto&m:byMon)aggPush(by,yrKey(m.key),m.c);
     for(auto&y:by){out[4].cands.push_back(y.c);out[4].addLabel(y.key);}out[4].rebuildPtrs();}}

// ============================================================
//  Yahoo Finance 解析器
// ============================================================
// 從 JSON 字串中抽出 "key":[...] 的數字陣列（含 null）
static std::vector<double> yfArr(const std::string&j,const std::string&key){
    std::vector<double>r;
    std::string q="\""+key+"\":[";
    size_t p=j.find(q);if(p==std::string::npos)return r;
    p+=q.size();size_t e=j.find(']',p);if(e==std::string::npos)return r;
    size_t pos=p;
    while(pos<e){
        while(pos<e&&(j[pos]==' '||j[pos]==','||j[pos]=='\n'||j[pos]=='\r'))pos++;
        if(pos>=e)break;
        if(j[pos]=='n'){r.push_back(std::numeric_limits<double>::quiet_NaN());pos+=4;continue;}
        try{size_t used;double v=std::stod(j.substr(pos,e-pos),&used);r.push_back(v);pos+=used;}
        catch(...){break;}}
    return r;}
static std::string yfStr(const std::string&j,const std::string&key){
    std::string q="\""+key+"\":\"";
    size_t p=j.find(q);if(p==std::string::npos)return"";
    p+=q.size();size_t e=j.find('"',p);
    return e==std::string::npos?"":j.substr(p,e-p);}
static bool parseYahoo(const std::string&json,std::vector<Candle>&out,
                       std::vector<std::string>&dates,std::string&title){
    if(json.empty())return false;
    if(json.find("\"result\":null")!=std::string::npos)return false;
    std::string ln=yfStr(json,"longName"),sn=yfStr(json,"shortName");
    title=ln.empty()?sn:ln;
    auto ts=yfArr(json,"timestamp");if(ts.empty())return false;
    auto op=yfArr(json,"open"),hi=yfArr(json,"high"),
         lo=yfArr(json,"low"),cl=yfArr(json,"close"),vo=yfArr(json,"volume");
    if(op.empty()||hi.empty()||lo.empty()||cl.empty())return false;
    int n=(int)std::min({ts.size(),op.size(),hi.size(),lo.size(),cl.size()});
    for(int i=0;i<n;i++){
        if(std::isnan(op[i])||std::isnan(hi[i])||std::isnan(lo[i])||std::isnan(cl[i]))continue;
        if(op[i]<=0||cl[i]<=0)continue;
        Candle c;c.o=(float)op[i];c.h=(float)hi[i];c.l=(float)lo[i];c.c=(float)cl[i];
        c.v=(!vo.empty()&&i<(int)vo.size()&&!std::isnan(vo[i]))?(float)(vo[i]/1e6):0.f;
        out.push_back(c);
        // Unix timestamp → 台灣日期 (UTC+8)
        time_t t=(time_t)ts[i]+8*3600;struct tm lt{};gmtime_s(&lt,&t);
        char buf[12];snprintf(buf,sizeof(buf),"%04d/%02d/%02d",lt.tm_year+1900,lt.tm_mon+1,lt.tm_mday);
        dates.push_back(buf);}
    return !out.empty();}

// ============================================================
//  Fetch worker  (Yahoo → TWSE 備援)
// ============================================================
static void fetchWorker(std::string code){
    g_loadProgress=0;
    std::vector<Candle>allD;std::vector<std::string>allDt;std::string stockName;
    bool ok=false;

    // 從 TWSE title 中萃取中文名稱（取代碼後第一個詞）
    auto extractTWSEName=[&](const std::string&ttl)->std::string{
        size_t p=ttl.find(code);if(p==std::string::npos)return"";
        p+=code.size();while(p<ttl.size()&&ttl[p]==' ')p++;
        size_t e=ttl.find(' ',p);
        return ttl.substr(p,e==std::string::npos?std::string::npos:e-p);};

    // ── 嘗試 Yahoo Finance（價格資料快，一次抓完）──
    g_loadTotal=2;
    static const wchar_t*yfHosts[]={L"query1.finance.yahoo.com",L"query2.finance.yahoo.com"};
    static const char*sfx[]={"TW","TWO"};
    for(auto&sf:sfx){if(ok)break;
        std::string sym=code+"."+sf;
        std::wstring ws(sym.begin(),sym.end());
        std::wstring path=L"/v8/finance/chart/"+ws+L"?interval=1d&range=5y&includePrePost=false";
        for(auto h:yfHosts){if(ok)break;
            std::string resp=httpGet(h,path,
                L"Accept: application/json\r\nAccept-Language: zh-TW,zh;q=0.9\r\n");
            g_loadProgress=1;
            if(parseYahoo(resp,allD,allDt,stockName))ok=true;}}

    // ── Yahoo 成功後，補抓 TWSE 取中文名稱 ──
    if(ok){
        time_t now2=time(nullptr);struct tm lt2{};localtime_s(&lt2,&now2);
        char ds2[9];snprintf(ds2,sizeof(ds2),"%04d%02d01",lt2.tm_year+1900,lt2.tm_mon+1);
        std::wstring wp(ds2,ds2+8),wc2(code.begin(),code.end());
        std::wstring pp=L"/exchangeReport/STOCK_DAY?response=json&date="+wp+L"&stockNo="+wc2;
        std::string rp=httpGet(L"www.twse.com.tw",pp);
        std::vector<Candle>mc;std::vector<std::string>md;std::string ttl;
        if(parseTWSE(rp,mc,md,ttl)){
            std::string cn=extractTWSEName(ttl);
            if(!cn.empty())stockName=cn;}
        g_loadProgress=2;}

    // ── TWSE 備援（Yahoo 完全失敗時）──
    if(!ok){
        time_t now=time(nullptr);struct tm lt{};localtime_s(&lt,&now);
        int curY=lt.tm_year+1900,curM=lt.tm_mon+1;
        int total=60;g_loadTotal=total;g_loadProgress=0;
        for(int i=total-1;i>=0;i--){
            int m=curM-i,y=curY;while(m<=0){m+=12;y--;}
            char ds[9];snprintf(ds,sizeof(ds),"%04d%02d01",y,m);
            std::wstring ws(ds,ds+8),wc(code.begin(),code.end());
            std::wstring path=L"/exchangeReport/STOCK_DAY?response=json&date="+ws+L"&stockNo="+wc;
            std::string resp=httpGet(L"www.twse.com.tw",path);
            std::vector<Candle>mc;std::vector<std::string>md;std::string ttl;
            if(parseTWSE(resp,mc,md,ttl)){
                if(stockName.empty()){
                    std::string cn=extractTWSEName(ttl);
                    if(!cn.empty())stockName=cn;}
                allD.insert(allD.end(),mc.begin(),mc.end());
                allDt.insert(allDt.end(),md.begin(),md.end());ok=true;}
            g_loadProgress=total-i;Sleep(250);}}

    if(!ok||allD.empty()){g_loadFail=true;return;}
    if(stockName.empty())stockName=code;
    static TFStore tmp[5];
    buildAllTFs(allD,allDt,code,stockName,tmp);
    g_stockCode=code;g_stockName=stockName;
    for(int i=0;i<5;i++)g_store[i]=std::move(tmp[i]);
    g_loadDone=true;}
#endif // !__EMSCRIPTEN__

// ============================================================
//  Hardcoded 0050 init
// ============================================================
static void initHardcoded0050(){
    static const Candle DATA[]={
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
     {62.00f,62.20f,61.80f,61.90f,91.9f},{61.30f,61.30f,60.30f,60.45f,265.0f},
     {60.15f,60.50f,59.90f,60.05f,151.4f},{61.55f,61.95f,61.35f,61.90f,84.0f},
     {60.10f,60.15f,59.50f,59.85f,327.7f},{60.10f,60.10f,59.45f,59.55f,162.7f},
     {60.70f,60.75f,60.10f,60.35f,64.4f},{61.10f,61.50f,61.00f,61.35f,75.8f},
     {61.80f,62.10f,61.65f,61.90f,53.9f},{61.95f,62.40f,61.85f,62.35f,68.4f},
     {61.80f,61.95f,61.40f,61.50f,103.0f},{61.80f,62.20f,61.65f,61.70f,43.2f},
     {62.40f,62.50f,62.10f,62.30f,46.5f},{62.40f,62.50f,62.00f,62.20f,40.7f},
     {62.45f,62.75f,62.35f,62.75f,47.3f},{63.00f,63.70f,62.95f,63.70f,78.7f},
     {63.80f,63.85f,63.55f,63.60f,50.7f},{63.70f,64.20f,63.65f,64.05f,66.7f},
     {64.30f,64.50f,63.20f,63.35f,101.2f},{63.55f,63.80f,63.30f,63.45f,60.2f},
     {62.50f,62.85f,62.40f,62.80f,114.9f},{62.10f,62.25f,61.70f,62.25f,169.4f},
     {62.15f,62.45f,61.85f,61.90f,98.5f},{61.80f,62.10f,61.55f,61.90f,118.0f},
     {62.40f,62.75f,62.35f,62.50f,81.0f},{63.30f,63.45f,63.05f,63.40f,93.9f},
     {63.50f,63.80f,63.50f,63.70f,101.9f},{63.95f,64.05f,63.75f,63.85f,64.1f},
     {64.20f,64.50f,64.10f,64.40f,70.8f},{64.65f,65.35f,64.60f,65.25f,86.5f},
     {64.90f,65.30f,64.75f,65.15f,61.1f},{65.10f,65.85f,64.90f,65.60f,71.9f},
     {66.00f,67.00f,65.80f,66.95f,81.1f},{68.10f,70.05f,68.05f,69.50f,232.9f},
     {69.45f,70.40f,68.95f,70.40f,130.2f},{70.40f,70.45f,69.70f,69.85f,122.3f},
     {69.65f,70.35f,69.40f,70.10f,74.9f},{69.80f,70.25f,68.90f,69.85f,113.3f},
     {70.50f,70.55f,70.05f,70.35f,84.3f},{71.10f,71.20f,70.30f,70.70f,103.6f},
     {70.95f,71.10f,70.65f,70.75f,86.3f},{70.70f,70.70f,70.15f,70.65f,97.5f},
     {71.75f,72.10f,71.20f,72.00f,101.1f},{71.70f,72.80f,71.45f,72.60f,123.0f},
     {71.90f,72.65f,71.85f,72.60f,111.0f},{71.80f,72.50f,71.70f,71.85f,173.7f},
     {71.90f,72.05f,71.60f,71.80f,101.7f},{72.10f,72.30f,71.80f,72.25f,71.0f},
     {72.25f,72.65f,72.20f,72.25f,88.0f},{72.40f,73.15f,72.35f,73.05f,73.1f},
     {73.55f,74.20f,73.50f,74.20f,90.9f},{74.40f,74.50f,73.50f,73.75f,119.9f},
     {73.15f,73.20f,72.30f,72.60f,175.3f},
     {71.55f,71.75f,70.90f,71.50f,234.3f},{73.00f,73.05f,72.40f,72.95f,73.7f},
     {72.40f,73.15f,72.30f,72.90f,59.7f},{72.05f,72.25f,71.75f,72.00f,107.6f},
     {71.40f,72.25f,70.80f,71.90f,110.9f},{74.05f,74.15f,73.75f,73.95f,122.3f},
     {74.65f,75.50f,74.60f,75.50f,102.3f},{75.90f,77.45f,75.90f,77.20f,120.6f},
     {78.45f,78.45f,77.20f,77.40f,227.9f},{77.95f,79.65f,77.85f,79.40f,150.4f},
     {80.35f,81.80f,80.25f,81.10f,167.4f},{81.25f,81.50f,80.80f,81.15f,133.5f},
     {79.50f,80.80f,79.15f,80.35f,224.7f},{80.05f,80.40f,78.65f,78.75f,324.3f},
     {77.40f,77.45f,75.50f,75.60f,505.2f},{78.30f,78.85f,76.70f,77.40f,207.0f},
     {76.80f,77.30f,76.30f,76.85f,151.6f},{73.15f,73.60f,71.65f,73.60f,542.2f},
     {75.90f,76.25f,74.30f,75.20f,173.9f},{76.30f,78.50f,76.10f,78.20f,151.8f},
     {77.10f,77.60f,76.35f,76.60f,131.5f},{75.15f,76.55f,75.00f,75.95f,115.1f},
     {76.30f,76.70f,75.45f,75.60f,105.4f},{76.45f,77.00f,76.30f,76.65f,64.0f},
     {77.75f,78.05f,77.45f,77.80f,78.0f},{76.60f,76.80f,75.95f,76.00f,141.7f},
     {76.20f,76.40f,75.50f,75.90f,72.6f},{73.50f,74.45f,73.30f,74.25f,233.4f},
     {75.50f,75.60f,73.80f,74.40f,90.5f},{76.35f,76.65f,76.00f,76.20f,94.3f},
     {76.30f,76.85f,75.60f,75.80f,80.1f},{74.45f,75.10f,74.10f,75.00f,115.8f},
     {73.30f,74.05f,72.80f,73.90f,175.5f},{73.35f,73.45f,72.15f,72.35f,257.7f},
     {75.00f,75.60f,74.60f,75.45f,130.7f},{75.90f,76.05f,73.80f,73.95f,114.0f},
     {75.15f,75.45f,74.65f,75.30f,80.4f},{78.10f,79.25f,78.10f,79.20f,235.7f},
     {79.30f,79.40f,78.80f,79.15f,95.1f},{80.00f,80.80f,79.95f,80.75f,120.8f},
     {80.60f,81.00f,80.40f,80.60f,91.7f},{81.75f,83.20f,81.75f,83.10f,129.1f},
    };
    static const int ND=(int)(sizeof(DATA)/sizeof(DATA[0]));
    static const Candle D_MON[]={
     {47.0f,49.3f,46.0f,48.7f,1800.f},{48.7f,49.5f,45.0f,46.2f,2000.f},
     {46.2f,48.5f,44.8f,47.5f,1700.f},{47.5f,48.5f,44.5f,45.5f,1900.f},
     {45.5f,46.5f,43.5f,44.8f,2200.f},{44.8f,48.5f,44.5f,47.8f,2100.f},
     {47.8f,49.8f,47.0f,49.0f,1800.f},{49.0f,50.5f,48.0f,50.0f,1500.f},
     {50.0f,50.8f,47.5f,48.5f,2200.f},{48.5f,49.0f,45.0f,47.0f,2500.f},
     {47.0f,47.5f,44.0f,45.5f,2800.f},{45.5f,46.0f,41.5f,42.5f,3200.f},
     {42.5f,43.0f,38.0f,39.5f,3800.f},{39.5f,40.0f,34.5f,35.8f,4500.f},
     {35.8f,38.5f,35.0f,37.8f,4000.f},{37.8f,40.5f,37.0f,39.5f,3500.f},
     {39.5f,40.0f,35.5f,36.5f,3800.f},{36.5f,37.5f,34.0f,36.0f,3500.f},
     {36.0f,37.5f,35.0f,37.0f,3000.f},{37.0f,38.5f,36.0f,38.0f,2800.f},
     {38.0f,40.5f,37.5f,40.0f,3200.f},{40.0f,42.0f,39.0f,41.5f,2800.f},
     {41.5f,43.5f,41.0f,43.0f,2500.f},{43.0f,44.5f,42.0f,44.0f,2200.f},
     {44.0f,45.5f,43.0f,45.0f,2000.f},{45.0f,46.5f,44.0f,46.0f,1800.f},
     {46.0f,47.5f,45.0f,46.5f,2000.f},{46.5f,47.5f,44.5f,45.5f,2200.f},
     {45.5f,46.0f,43.5f,44.0f,2400.f},{44.0f,45.0f,42.5f,43.5f,2200.f},
     {43.5f,45.5f,43.0f,45.0f,2000.f},{45.0f,47.5f,44.5f,47.0f,1800.f},
     {47.0f,50.0f,46.5f,49.5f,2500.f},{49.5f,52.5f,49.0f,52.0f,2200.f},
     {52.0f,54.5f,51.5f,54.0f,2000.f},{54.0f,55.0f,51.5f,52.5f,2800.f},
     {52.5f,55.5f,52.0f,55.0f,2500.f},{55.0f,60.0f,54.5f,59.5f,3000.f},
     {59.5f,61.0f,57.5f,58.5f,2800.f},{58.5f,61.5f,57.0f,60.5f,2500.f},
     {60.5f,63.0f,59.5f,62.5f,2200.f},{62.5f,64.5f,61.5f,63.5f,2000.f},
     {63.5f,66.5f,63.0f,65.5f,2200.f},{65.5f,67.5f,64.5f,66.5f,2000.f},
     {66.5f,68.5f,65.5f,67.5f,2200.f},{67.5f,69.5f,66.5f,68.5f,2000.f},
     {68.5f,70.0f,66.0f,67.0f,2800.f},{67.0f,67.5f,60.0f,62.5f,3500.f},
     {62.5f,65.0f,60.5f,63.5f,2500.f},{63.5f,65.5f,61.5f,62.5f,2200.f},
     {62.5f,64.0f,60.5f,62.0f,2000.f},{62.0f,63.5f,59.5f,61.5f,2200.f},
     {61.5f,62.5f,58.5f,59.5f,2500.f},
     {57.90f,64.80f,57.90f,64.75f,2433.f},{64.45f,64.85f,59.45f,62.35f,2000.f},
     {61.80f,65.85f,61.40f,65.60f,2000.f},{66.00f,74.50f,65.80f,72.60f,2100.f},
     {71.55f,81.80f,70.80f,81.15f,1800.f},{79.50f,80.80f,71.65f,72.35f,3900.f},
     {75.00f,83.20f,73.80f,83.10f,1100.f},
    };
    static const int NM=(int)(sizeof(D_MON)/sizeof(D_MON[0]));
    g_stockCode="0050";g_stockName="元大台灣50";
    g_store[0].clear();g_store[0].perLbl=false;g_store[0].pStep=2.f;
    g_store[0].name="0050 元大台灣50 | 日線 1年";
    g_store[0].cands.assign(DATA,DATA+ND);
    {static const struct{int i;const char*l;}DM[]={
        {0,"10/25"},{20,"11/25"},{40,"12/25"},{62,"1/26"},{83,"2/26"},{95,"3/26"},{117,"4/26"}};
     for(auto&m:DM)g_store[0].addMark(m.i,m.l);}
    static const Candle DW[]={
        {79.50f,80.80f,75.50f,76.85f,600.f},{73.15f,78.50f,71.65f,75.95f,900.f},
        {76.30f,78.05f,75.45f,75.90f,500.f},{73.50f,76.85f,73.30f,75.00f,600.f},
        {73.30f,76.05f,72.15f,73.95f,700.f},{75.15f,80.80f,74.65f,80.75f,700.f},
        {80.60f,83.20f,80.40f,83.10f,400.f}};
    g_store[1].clear();g_store[1].perLbl=true;g_store[1].pStep=2.f;
    g_store[1].name="0050 元大台灣50 | 週線";
    g_store[1].cands.assign(DW,DW+7);
    for(auto&s:{"3/02","3/09","3/16","3/23","3/30","4/07","4/14"})g_store[1].addLabel(s);
    g_store[2].clear();g_store[2].perLbl=false;g_store[2].pStep=5.f;
    g_store[2].name="0050 元大台灣50 | 月線 5年";
    g_store[2].cands.assign(D_MON,D_MON+NM);
    {static const struct{int i;const char*l;}MM[]={
        {0,"2021"},{8,"2022"},{20,"2023"},{32,"2024"},{44,"2025"},{56,"2026"}};
     for(auto&m:MM)g_store[2].addMark(m.i,m.l);}
    static const Candle DQ[]={
        {55.0f,64.0f,53.0f,57.9f,5000.f},{57.9f,65.85f,57.9f,65.6f,6500.f},
        {66.0f,81.80f,65.8f,72.35f,7800.f},{75.0f,83.20f,73.8f,83.1f,1100.f}};
    g_store[3].clear();g_store[3].perLbl=true;g_store[3].pStep=5.f;
    g_store[3].name="0050 元大台灣50 | 季線";
    g_store[3].cands.assign(DQ,DQ+4);
    for(auto&s:{"25-3","25-4","26-1","26-2"})g_store[3].addLabel(s);
    static const Candle DY[]={
        {57.9f,65.85f,57.9f,65.6f,20000.f},{66.0f,83.20f,65.8f,83.1f,8900.f}};
    g_store[4].clear();g_store[4].perLbl=true;g_store[4].pStep=10.f;
    g_store[4].name="0050 元大台灣50 | 年線";
    g_store[4].cands.assign(DY,DY+2);
    for(auto&s:{"2025","2026"})g_store[4].addLabel(s);
    buildTFViews();}

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
    float hSub  = usable * wSub  / wTot;

    // 從頂部往下分配
    WORLD_HI = TOP;
    WORLD_LO = TOP - hMain;

    if(g_showVol){
        VOL_HI = WORLD_LO - GAP;
        VOL_LO = VOL_HI - hVol;
    } else {
        VOL_HI = VOL_LO = WORLD_LO;
    }

    if(hasSub){
        MACD_HI = (g_showVol ? VOL_LO : WORLD_LO) - GAP;
        MACD_LO = MACD_HI - hSub;
    } else {
        MACD_HI = MACD_LO = (g_showVol ? VOL_LO : WORLD_LO);
    }

    MACD_MID  = (MACD_LO + MACD_HI) * 0.5f;
    MACD_HALF = (MACD_HI - MACD_LO) * 0.5f;
}

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

// ============================================================
//  太鼓達人 Don-chan（どんちゃん）
//  r=紅(鼓身/手)  b=棕(鼓圈/邊緣)  s=白(鼓面/頭帶/嘴)  d=深(眼/腳/輪廓)
// ============================================================
//  拉拉熊 Rilakkuma（細緻版）
//  jumpT:跳躍程度  squashT:壓扁(<0拉長,>0壓扁)  happy:笑/哭
//  r=棕色(頭/身/耳/手)  b=奶褐(口吻/肚/耳內)  s=淡奶(亮點/淚/臉頰)  d=深棕(眼/鼻/嘴)
// ============================================================
static void buildDonChan(std::vector<float>&r,std::vector<float>&b,
                         std::vector<float>&s,std::vector<float>&d,
                         std::vector<float>&sw,  // 劍（金色）
                         float bx,float by,float sc,
                         float jumpT=0.f,bool happy=true,float squashT=0.f,
                         float slashT=0.f,         // 0=無劍, 0..1=揮劍動畫
                         int faceDir=0){           // -1=左45° 0=正面 +1=右45°
    auto lp=[](float a,float b2,float t){return a+(b2-a)*t;};

    // 圓盤（水平帶逼近圓）
    auto disc=[&](std::vector<float>&m,float cx,float cy,float R,float z0,float z1,int n=16){
        if(R<1e-4f)return;
        float dy=2.f*R/n;
        for(int i=0;i<n;i++){
            float y0=cy-R+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-cy;
            float hw=sqrtf(std::max(0.f,R*R-ym*ym));
            if(hw>1e-4f)pushBox(m,cx-hw,cx+hw,y0,y1,z0,z1);
        }
    };
    // 橢圓 disc（rx≠ry）
    auto ellDisc=[&](std::vector<float>&m,float cx,float cy,float rx,float ry,float z0,float z1,int n=14){
        if(rx<1e-4f||ry<1e-4f)return;
        float dy=2.f*ry/n;
        for(int i=0;i<n;i++){
            float y0=cy-ry+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-cy;
            float hw=rx*sqrtf(std::max(0.f,1.f-(ym*ym)/(ry*ry)));
            if(hw>1e-4f)pushBox(m,cx-hw,cx+hw,y0,y1,z0,z1);
        }
    };
    // 橢圓柱（Z 全寬，squash 縮放）
    auto ellBody=[&](std::vector<float>&m,float cx,float cy,float rx,float ry,float zR,int n=20){
        // squashT<0拉長Y, squashT>0壓扁Y
        float ryS=ry*(1.f-squashT*0.48f);
        float rxS=rx*(1.f+squashT*0.22f);
        float zRS=zR*(1.f+squashT*0.12f);
        float dy=2.f*ryS/n;
        for(int i=0;i<n;i++){
            float y0=cy-ryS+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-cy;
            float hw=rxS*sqrtf(std::max(0.f,1.f-(ym*ym)/(ryS*ryS)));
            if(hw>1e-4f)pushBox(m,cx-hw,cx+hw,y0,y1,-zRS,zRS);
        }
    };

    const float pDp = sc*((faceDir!=0)?0.055f:0.034f); // 側臉時表情更突出
    // 面向偏移（45度 → 臉部 X 偏移，Z 往前突出讓表情可見）
    const float faceXOff = faceDir * sc * 0.14f;  // 臉往左/右偏
    const float faceZOff = (faceDir!=0) ? sc*0.06f : 0.f; // 側臉 Z 更突出

    // ── 尺寸（squash 不影響這些基準值，只影響渲染）──────────────
    const float bRX = sc*0.26f;
    const float bRY = sc*0.28f*(1.f-squashT*0.48f); // 直接在 by 定義時縮放
    const float bRYraw=sc*0.28f;
    const float bRZ = sc*0.27f;
    const float bCY = by+sc*0.28f;   // 固定身體中心（基於 raw bRY）

    const float hRraw=sc*0.42f; // 大頭拉拉熊
    const float hR  = hRraw*(1.f-squashT*0.42f);
    const float hDep= sc*0.30f*(1.f+squashT*0.10f);
    const float hCY = bCY+sc*0.28f*0.65f+hRraw*0.78f; // 固定頭部中心

    const float hFZ = hDep*0.86f;
    const float zFc = hFZ+faceZOff;
    const float fCX = bx+faceXOff; // 臉部中心X（可偏左/右）

    const float earR= sc*0.115f;
    const float earXO=hDep*0.72f;
    const float earCY=hCY+hR*0.76f;

    // ── 腳（誇張跳躍：高舉+後收）────────────────────────────────
    {
        float fR=sc*0.092f, fDZ=sc*0.088f, fXO=bRX*0.55f;
        // 跳躍：腳往後大幅後收（X方向）＋往上縮
        float fShX=lp(0.f, bRX*0.90f, jumpT);   // X方向後縮（比之前大幅加大）
        float fShY=lp(0.f, bRYraw*0.55f, jumpT); // Y方向收起
        float fYC =by-fR + fShY;
        disc(r,bx-fXO,fYC,fR,      -fDZ+fShX,fDZ+fShX,8);
        disc(r,bx+fXO,fYC,fR,      -fDZ+fShX,fDZ+fShX,8);
        disc(r,bx-fXO,fYC-fR*0.12f,fR*1.16f,-fDZ*0.7f+fShX,fDZ*0.7f+fShX,6);
        disc(r,bx+fXO,fYC-fR*0.12f,fR*1.16f,-fDZ*0.7f+fShX,fDZ*0.7f+fShX,6);
    }

    // ── 身體（棕色橢圓柱，squash 縮放）────────────────────────
    ellBody(r,bx,bCY,bRX,bRYraw,bRZ,14);

    // ── 肚子（奶褐橢圓，正面）──────────────────────────────────
    {
        float belRX=bRX*0.56f*(1.f+squashT*0.22f);
        float belRY=bRYraw*0.52f*(1.f-squashT*0.48f);
        float belCY=bCY-bRYraw*0.08f;
        int n=10; float dy=2.f*belRY/n;
        for(int i=0;i<n;i++){
            float y0=belCY-belRY+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-belCY;
            float hw=belRX*sqrtf(std::max(0.f,1.f-(ym*ym)/(belRY*belRY)));
            if(hw>1e-4f)pushBox(b,bx-hw,bx+hw,y0,y1,bRZ*0.86f,bRZ*0.88f+pDp);
        }
    }

    // ── 頭部（棕色橢圓柱，squash 縮放）────────────────────────
    ellBody(r,bx,hCY,hDep,hRraw,hRraw*0.86f,16);


    // ── 耳朵（棕色圓 + 奶褐耳內）──────────────────────────────
    float earActR=earR*(1.f-squashT*0.25f);
    disc(r,bx-earXO,earCY,earActR,-earActR*0.82f,earActR*0.82f,8);
    disc(r,bx+earXO,earCY,earActR,-earActR*0.82f,earActR*0.82f,8);
    disc(b,bx-earXO,earCY,earActR*0.54f,earActR*0.78f,earActR*0.78f+pDp*0.6f,6);
    disc(b,bx+earXO,earCY,earActR*0.54f,earActR*0.78f,earActR*0.78f+pDp*0.6f,6);

    // ── 口吻部（奶褐橢圓突起）──────────────────────────────────
    {
        float mzRX=hDep*0.36f, mzRY=hR*0.25f, mzCY=hCY-hR*0.13f;
        int n=8; float dy=2.f*mzRY/n;
        for(int i=0;i<n;i++){
            float y0=mzCY-mzRY+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-mzCY;
            float hw=mzRX*sqrtf(std::max(0.f,1.f-(ym*ym)/(mzRY*mzRY)));
            if(hw>1e-4f)pushBox(b,fCX-hw,fCX+hw,y0,y1,zFc,zFc+pDp*1.6f);
        }
    }

    // ── 眼睛 ────────────────────────────────────────────────────
    {
        float eXO=hDep*0.36f, eY=hCY+hR*0.18f;
        float eZ=zFc+pDp*0.5f;

        if(happy){
            float eRX=sc*0.068f, eRY=sc*0.040f;
            ellDisc(d,fCX-eXO,eY,eRX,eRY,eZ,eZ+pDp,7);
            ellDisc(d,fCX+eXO,eY,eRX,eRY,eZ,eZ+pDp,7);
            pushBox(b,fCX-eXO-eRX*1.1f,fCX-eXO+eRX*1.1f,eY-eRY*1.2f,eY,zFc+pDp,zFc+pDp*2.f);
            pushBox(b,fCX+eXO-eRX*1.1f,fCX+eXO+eRX*1.1f,eY-eRY*1.2f,eY,zFc+pDp,zFc+pDp*2.f);
            float ckXO=hDep*0.58f, ckY=eY-hR*0.10f;
            ellDisc(s,fCX-ckXO,ckY,sc*0.062f,sc*0.040f,zFc+pDp*1.2f,zFc+pDp*1.8f,5);
            ellDisc(s,fCX+ckXO,ckY,sc*0.062f,sc*0.040f,zFc+pDp*1.2f,zFc+pDp*1.8f,5);
        } else {
            float eR=sc*0.042f;
            disc(d,fCX-eXO,eY,eR,eZ,eZ+pDp,7);
            disc(d,fCX+eXO,eY,eR,eZ,eZ+pDp,7);
            disc(s,fCX-eXO+eR*0.28f,eY+eR*0.28f,eR*0.35f,eZ+pDp,eZ+pDp*1.8f,4);
            disc(s,fCX+eXO+eR*0.28f,eY+eR*0.28f,eR*0.35f,eZ+pDp,eZ+pDp*1.8f,4);
            float brY=eY+eR+sc*0.010f;
            pushBox(d,fCX-eXO-eR*1.6f,fCX-eXO+eR*0.1f,brY,brY+sc*0.030f,eZ,eZ+pDp*0.7f);
            pushBox(d,fCX+eXO-eR*0.1f,fCX+eXO+eR*1.6f,brY,brY+sc*0.030f,eZ,eZ+pDp*0.7f);
            float tW=sc*0.022f;
            pushBox(s,fCX-eXO-tW,fCX-eXO+tW,eY-eR-sc*0.12f,eY-eR,eZ,eZ+pDp*0.6f);
            pushBox(s,fCX+eXO-tW,fCX+eXO+tW,eY-eR-sc*0.12f,eY-eR,eZ,eZ+pDp*0.6f);
            disc(s,fCX-eXO,eY-eR-sc*0.14f,tW*1.4f,eZ,eZ+pDp*0.5f,4);
            disc(s,fCX+eXO,eY-eR-sc*0.14f,tW*1.4f,eZ,eZ+pDp*0.5f,4);
        }
    }

    // ── 鼻子（橢圓黑鼻）───────────────────────────────────────
    {
        float nRX=sc*0.038f, nRY=sc*0.026f, nCY=hCY-hR*0.09f;
        float nZ=zFc+pDp*1.8f;
        ellDisc(d,fCX,nCY,nRX,nRY,nZ,nZ+pDp*0.7f,5);
    }

    // ── 嘴（笑=大U 哭=誇張∩）──────────────────────────────────
    {
        float mW=hDep*(happy?0.34f:0.30f), mH=sc*(happy?0.034f:0.030f);
        float mY=hCY-hR*0.24f;
        float mZ0=zFc+pDp*2.0f, mZ1=mZ0+pDp*0.55f;
        if(happy){
            pushBox(d,fCX-mW*0.16f,fCX+mW*0.16f,mY,      mY+mH*1.4f,mZ0,mZ1);
            pushBox(d,fCX+mW*0.16f,fCX+mW,       mY+mH,   mY+mH*2.2f,mZ0,mZ1);
            pushBox(d,fCX-mW,      fCX-mW*0.16f, mY+mH,   mY+mH*2.2f,mZ0,mZ1);
        } else {
            pushBox(d,fCX-mW*0.16f,fCX+mW*0.16f,mY+mH,   mY+mH*2.2f,mZ0,mZ1);
            pushBox(d,fCX+mW*0.16f,fCX+mW,       mY,      mY+mH*1.4f,mZ0,mZ1);
            pushBox(d,fCX-mW,      fCX-mW*0.16f, mY,      mY+mH*1.4f,mZ0,mZ1);
        }
    }

    // ── 手臂（誇張跳躍：大幅上舉＋張開）────────────────────────
    {
        float aH=sc*0.085f, aDZ=sc*0.068f, aLen=sc*0.175f;
        float aY  =lp(bCY+bRYraw*0.22f, bCY+bRYraw*1.10f, jumpT);
        float aLj =lp(aLen, aLen*1.35f, jumpT);
        float aYtip=lp(aY, aY+aLen*0.80f, jumpT);
        float armRootY=aY;
        // 左臂
        pushBox(r,bx-bRX-aLj,bx-bRX,armRootY,armRootY+aH,-aDZ,aDZ);
        // 右臂
        pushBox(r,bx+bRX,bx+bRX+aLj,armRootY,armRootY+aH,-aDZ,aDZ);
        // 圓形手端
        disc(r,bx-bRX-aLj,aYtip+aH*.5f,aH*0.58f,-aDZ*.72f,aDZ*.72f,6);
        if(slashT<=0.f)
            disc(r,bx+bRX+aLj,aYtip+aH*.5f,aH*0.58f,-aDZ*.72f,aDZ*.72f,6);
    }

    // ── 死神鐮刀（大型，揮砍動畫）──────────────────────────
    if(slashT>0.f){
        // 揮鐮弧度：0=高舉 → 0.5=命中 → 1=收回（更大弧度）
        float ang0=(slashT<0.5f)
            ? (1.30f-slashT*2.f*2.80f)
            : (-1.50f+(slashT-0.5f)*2.f*1.50f);
        float ang=ang0+1.5708f; // 轉 90°（垂直握持）
        float cosA=cosf(ang), sinA=sinf(ang);
        float sRootX=bx+bRX*0.8f;
        float sRootY=lp(bCY+bRYraw*0.22f,bCY+bRYraw*1.10f,jumpT)+sc*0.0425f;

        // ── 長柄（深棕色木杖，粗壯）──────────────────────
        float handleLen=sc*1.20f;  // 加長柄
        float gripHW=sc*0.038f;
        int NH=8;
        for(int i=0;i<NH;i++){
            float t0=(float)i/NH, t1=(float)(i+1)/NH;
            float x0=sRootX+cosA*handleLen*t0, y0=sRootY+sinA*handleLen*t0;
            float x1=sRootX+cosA*handleLen*t1, y1=sRootY+sinA*handleLen*t1;
            // 柄越靠頂端越細
            float hw=gripHW*(1.f-t0*0.3f);
            pushBox(r, std::min(x0,x1)-hw, std::max(x0,x1)+hw,
                       std::min(y0,y1)-hw, std::max(y0,y1)+hw,
                       -hw*0.55f, hw*0.55f);}
        // 柄底端裝飾球
        {float bsx=sRootX-cosA*gripHW*2.f, bsy=sRootY-sinA*gripHW*2.f;
         pushBox(r,bsx-gripHW*1.3f,bsx+gripHW*1.3f,bsy-gripHW*1.3f,bsy+gripHW*1.3f,
                   -gripHW*0.8f,gripHW*0.8f);}

        // ── 刀刃接頭（深色金屬環）──────────────────────
        float tipX=sRootX+cosA*handleLen, tipY=sRootY+sinA*handleLen;
        float collarR=sc*0.055f;
        pushBox(d,tipX-collarR,tipX+collarR,tipY-collarR,tipY+collarR,-collarR*0.7f,collarR*0.7f);

        // ── 死神巨刃（大弧形，從頂端向後彎成半月）──────
        float bladeLen=sc*1.05f;   // 超長刃
        float perpX=-sinA, perpY=cosA;
        int NB=18;  // 更多切片 → 更平滑弧線
        for(int i=0;i<NB;i++){
            float t0=(float)i/NB, t1=(float)(i+1)/NB;
            // 死神鐮刀弧度更大（半圓弧）
            float curve0=sinf(t0*2.5f)*0.72f;
            float curve1=sinf(t1*2.5f)*0.72f;
            float bx0=tipX+perpX*bladeLen*t0-cosA*bladeLen*curve0*0.55f;
            float by0=tipY+perpY*bladeLen*t0-sinA*bladeLen*curve0*0.55f;
            float bx1=tipX+perpX*bladeLen*t1-cosA*bladeLen*curve1*0.55f;
            float by1=tipY+perpY*bladeLen*t1-sinA*bladeLen*curve1*0.55f;
            // 刃根部寬，末端尖
            float widthFrac=sinf(t0*3.14159f);  // 中間最寬，兩端窄
            float w2=sc*0.055f*widthFrac+sc*0.012f;
            float zw=w2*0.35f;
            pushBox(sw, std::min(bx0,bx1)-w2, std::max(bx0,bx1)+w2,
                        std::min(by0,by1)-w2, std::max(by0,by1)+w2,
                        -zw, zw);}
        // 刃脊（刀背加厚，深色線條）
        for(int i=0;i<NB;i+=2){
            float t0=(float)i/NB;
            float curve0=sinf(t0*2.5f)*0.72f;
            float bx0=tipX+perpX*bladeLen*t0-cosA*bladeLen*curve0*0.55f;
            float by0=tipY+perpY*bladeLen*t0-sinA*bladeLen*curve0*0.55f;
            float sp2=sc*0.018f;
            pushBox(d,bx0-sp2,bx0+sp2,by0-sp2,by0+sp2,-sp2*0.5f,sp2*0.5f);}
    }
}

// ============================================================
//  韭菜怪物（buildLeek）
//  g = 綠（莖/葉）  w = 白/奶（根部/臉）  d = 深色（眼/嘴）
//  ax,ay = 腳底座標，sc = 全域縮放
//  t = 當前時間（擺動用）  scl = 1→0 死亡縮小
// ============================================================
static void buildLeek(std::vector<float>&g,std::vector<float>&w,
                      std::vector<float>&d,
                      float ax,float ay,float sc,
                      double t,float scl,bool scared=true){
    if(scl<=0.001f)return;
    float s=sc*0.85f*scl;
    float pDp=s*0.03f;       // 前面突出量
    // 走路搖擺
    float sway=sinf((float)t*4.2f)*0.06f*s;

    // disc 局部幫手
    auto disc=[&](std::vector<float>&m,float cx,float cy,float R,float z0,float z1,int n=8){
        if(R<1e-4f)return;
        float dy=2.f*R/n;
        for(int i=0;i<n;i++){
            float y0=cy-R+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-cy;
            float hw=sqrtf(std::max(0.f,R*R-ym*ym));
            if(hw>1e-4f)pushBox(m,cx-hw,cx+hw,y0,y1,z0,z1);}};

    // ── 根部 / 腳（白色，底部短粗段）────────────────────────
    float rootH=s*0.20f, rootR=s*0.14f;
    // 根部細鬚（底部幾根小觸鬚）
    for(int ri=0;ri<4;ri++){
        float rAng=ri*1.57f+sinf((float)t*2.f)*0.1f;
        float rx=ax+cosf(rAng)*rootR*0.6f+sway;
        float rz=sinf(rAng)*rootR*0.4f;
        float rLen=s*0.06f;
        pushBox(w,rx-s*0.008f,rx+s*0.008f,ay-rLen,ay,rz-s*0.008f,rz+s*0.008f);}
    {int n=14;float dy=rootH/n;
     for(int i=0;i<n;i++){
         float y0=ay+i*dy,y1=y0+dy;
         float frac=(float)i/n;
         float hw=rootR*(0.72f+0.28f*frac);
         float zw=hw*0.65f;
         pushBox(w,ax-hw+sway,ax+hw+sway,y0,y1,-zw,zw);}}

    // ── 莖身（綠色柱，更多段數）────────────────────────────
    float stemBot=ay+rootH;
    float stemH=s*0.75f;
    float stemR=s*0.115f;
    {int n=18;float dy=stemH/n;
     for(int i=0;i<n;i++){
         float y0=stemBot+i*dy,y1=y0+dy;
         float frac=(float)i/n;
         float hw=stemR*(1.f-frac*0.18f);
         float zw=hw*0.62f;
         // 莖身微彎曲（自然感）
         float bend=sinf(frac*3.14159f)*s*0.015f;
         pushBox(g,ax-hw+sway+bend,ax+hw+sway+bend,y0,y1,-zw,zw);}}

    // ── 葉子（5片寬葉，更多段數，各自搖擺）─────────────
    float leafBot=stemBot+stemH*0.60f;
    float leafH=s*0.60f;
    for(int li=0;li<5;li++){
        float xOff=(li==0)?0.f:(li==1)?-s*0.14f:(li==2)?s*0.14f:(li==3)?-s*0.07f:s*0.07f;
        float zOff=(li==0)?stemR*0.35f:(li==1)?-stemR*0.20f:(li==2)?-stemR*0.20f:(li==3)?stemR*0.15f:-stemR*0.10f;
        float leafSway=sinf((float)t*3.5f+li*1.3f)*0.05f*s;
        int n=12;float dy=leafH/n;
        for(int i=0;i<n;i++){
            float y0=leafBot+i*dy,y1=y0+dy;
            float frac=(float)i/n;
            // 葉子越高越寬再收尖
            float w2=s*0.09f*sinf(frac*3.14159f)*1.8f;
            float xC=ax+xOff*(1.f+frac*1.5f)+leafSway*frac+sway;
            if(w2>1e-4f)
                pushBox(g,xC-w2,xC+w2,y0,y1,zOff-s*0.018f,zOff+s*0.018f);}}

    // ── 臉部（白色圓形，正面）──────────────────────────────
    float faceR=s*0.10f;
    float faceCY=stemBot+stemH*0.42f;
    float faceZ=stemR*0.56f;
    disc(w,ax+sway,faceCY,faceR,faceZ,faceZ+pDp*2.f,8);

    // ── 眼睛（深色）────────────────────────────────────────
    {float eXO=faceR*0.48f;
     float eY=faceCY+faceR*0.22f;
     float eZ=faceZ+pDp*2.5f;
     if(scared){
         // 驚恐的大圓眼
         float eR=s*0.028f;
         disc(d,ax-eXO+sway,eY,eR,eZ,eZ+pDp,6);
         disc(d,ax+eXO+sway,eY,eR,eZ,eZ+pDp,6);
         // 白色亮點
         float hlR=eR*0.35f;
         pushBox(w,ax-eXO+eR*0.3f+sway-hlR,ax-eXO+eR*0.3f+sway+hlR,
                   eY+eR*0.25f-hlR,eY+eR*0.25f+hlR,eZ+pDp,eZ+pDp*2.f);
         pushBox(w,ax+eXO+eR*0.3f+sway-hlR,ax+eXO+eR*0.3f+sway+hlR,
                   eY+eR*0.25f-hlR,eY+eR*0.25f+hlR,eZ+pDp,eZ+pDp*2.f);
     } else {
         // 開心瞇眼
         pushBox(d,ax-eXO-s*0.030f+sway,ax-eXO+s*0.030f+sway,
                   eY,eY+s*0.012f,eZ,eZ+pDp);
         pushBox(d,ax+eXO-s*0.030f+sway,ax+eXO+s*0.030f+sway,
                   eY,eY+s*0.012f,eZ,eZ+pDp);}}

    // ── 嘴巴（深色）────────────────────────────────────────
    {float mY=faceCY-faceR*0.28f;
     float mZ=faceZ+pDp*2.5f;
     if(scared){
         // 驚恐 O 嘴
         disc(d,ax+sway,mY,s*0.025f,mZ,mZ+pDp,5);
     } else {
         // 微笑 U 字
         pushBox(d,ax-s*0.025f+sway,ax+s*0.025f+sway,mY,mY+s*0.010f,mZ,mZ+pDp);}}

    // ── 汗滴（白色，驚恐時出現）──────────────────────────
    if(scared){
        float sweatX=ax+faceR*0.65f+sway;
        float sweatY=faceCY+faceR*0.55f;
        float sweatZ=faceZ+pDp*1.5f;
        pushBox(w,sweatX-s*0.012f,sweatX+s*0.012f,sweatY,sweatY+s*0.045f,
                  sweatZ,sweatZ+pDp);}
} // buildLeek

// ============================================================
//  飛行龍蝦（buildLobster）
//  r = 紅（身體/鉗/尾）  d = 深色（眼/觸角）
//  ax,ay = 中心座標  sc = 縮放  t = 時間  scl = 死亡縮放
// ============================================================
static void buildLobster(std::vector<float>&r,std::vector<float>&d,
                         float ax,float ay,float sc,
                         bool facingLeft,double t,float scl){
    if(scl<=0.001f)return;
    float s=sc*0.7f*scl;
    float sx=facingLeft?-1.f:1.f;
    // 飛行搖擺
    float flap=sinf((float)t*6.f)*0.12f*s;
    float bob =sinf((float)t*3.2f)*0.08f*s; // 上下浮動

    auto disc=[&](std::vector<float>&m,float cx,float cy,float R,float z0,float z1,int n=8){
        if(R<1e-4f)return;
        float dy=2.f*R/n;
        for(int i=0;i<n;i++){
            float y0=cy-R+i*dy,y1=y0+dy,ym=(y0+y1)*.5f-cy;
            float hw=sqrtf(std::max(0.f,R*R-ym*ym));
            if(hw>1e-4f)pushBox(m,cx-hw,cx+hw,y0,y1,z0,z1);}};

    float cy=ay+bob;

    // ── 身體（橢圓柱，橫向，更多段數）────────────────────
    float bodyRX=s*0.30f, bodyRY=s*0.14f, bodyZ=s*0.12f;
    {int n=16;float dx2=2.f*bodyRX/n;
     for(int i=0;i<n;i++){
         float x0=ax-sx*bodyRX+sx*i*dx2, x1=x0+sx*dx2;
         float xm=(x0+x1)*0.5f-ax;
         float hw=bodyRY*sqrtf(std::max(0.f,1.f-(xm*xm)/(bodyRX*bodyRX)));
         if(hw>1e-4f)pushBox(r,std::min(x0,x1),std::max(x0,x1),cy-hw,cy+hw,-bodyZ,bodyZ);}}

    // ── 頭部（圓，前方）────────────────────────────────────
    float headR=s*0.12f;
    float headX=ax+sx*(bodyRX+headR*0.6f);
    disc(r,headX,cy,headR,-headR*0.7f,headR*0.7f,8);

    // ── 眼睛（兩個小球突出）────────────────────────────────
    {float eyeR=s*0.032f;
     float eyeX=headX+sx*headR*0.5f;
     float eyeZ=headR*0.55f;
     pushBox(d,eyeX-eyeR,eyeX+eyeR,cy+headR*0.3f-eyeR,cy+headR*0.3f+eyeR,eyeZ,eyeZ+eyeR*2.f);
     pushBox(d,eyeX-eyeR,eyeX+eyeR,cy+headR*0.3f-eyeR,cy+headR*0.3f+eyeR,-(eyeZ+eyeR*2.f),-eyeZ);}

    // ── 觸鬚（2根長線）────────────────────────────────────
    {float antLen=s*0.35f;
     float antX0=headX+sx*headR*0.6f, antY0=cy+headR*0.2f;
     pushBox(d,std::min(antX0,antX0+sx*antLen),std::max(antX0,antX0+sx*antLen),
               antY0,antY0+s*0.015f,-s*0.008f,s*0.008f);
     pushBox(d,std::min(antX0,antX0+sx*antLen*0.85f),std::max(antX0,antX0+sx*antLen*0.85f),
               antY0+s*0.06f,antY0+s*0.075f,-s*0.008f,s*0.008f);}

    // ── 大鉗（兩隻，前方左右展開）─────────────────────────
    for(int ci=0;ci<2;ci++){
        float cZ=(ci==0)?bodyZ*0.8f:-bodyZ*0.8f;
        float clawX0=headX+sx*headR*0.3f;
        float clawLen=s*0.22f;
        // 上臂
        float cEndX=clawX0+sx*clawLen;
        float cEndY=cy+(ci==0?1:-1)*s*0.08f+flap*(ci==0?1.f:-1.f);
        pushBox(r,std::min(clawX0,cEndX)-s*0.025f,std::max(clawX0,cEndX)+s*0.025f,
                  std::min(cy,cEndY)-s*0.025f,std::max(cy,cEndY)+s*0.025f,
                  cZ-s*0.02f,cZ+s*0.02f);
        // 鉗子（V形兩片）
        float pLen=s*0.10f;
        // 上片
        pushBox(r,std::min(cEndX,cEndX+sx*pLen),std::max(cEndX,cEndX+sx*pLen),
                  cEndY+s*0.01f,cEndY+s*0.04f,cZ-s*0.015f,cZ+s*0.015f);
        // 下片
        pushBox(r,std::min(cEndX,cEndX+sx*pLen),std::max(cEndX,cEndX+sx*pLen),
                  cEndY-s*0.04f,cEndY-s*0.01f,cZ-s*0.015f,cZ+s*0.015f);}

    // ── 尾巴（扇形，後方展開）──────────────────────────────
    {float tailX=ax-sx*bodyRX;
     float tailLen=s*0.25f;
     // 中間尾片
     pushBox(r,std::min(tailX,tailX-sx*tailLen),std::max(tailX,tailX-sx*tailLen),
               cy-s*0.035f,cy+s*0.035f,-s*0.015f,s*0.015f);
     // 左右扇葉
     for(int fi=-2;fi<=2;fi++){
         float fAng=fi*0.25f;
         float fEndX=tailX-sx*tailLen*0.85f;
         float fEndY=cy+fi*s*0.055f;
         float fw=s*0.05f, fh=s*0.020f;
         pushBox(r,std::min(tailX,fEndX)-fh,std::max(tailX,fEndX)+fh,
                   fEndY-fw,fEndY+fw,-s*0.012f,s*0.012f);}}

    // ── 小腿（4對，身體下方搖擺）─────────────────────────
    for(int li=0;li<4;li++){
        float lx=ax+sx*(-bodyRX*0.6f+li*bodyRX*0.4f);
        float ly=cy-bodyRY;
        float lSwing=sinf((float)t*5.f+li*1.5f)*0.06f*s;
        float legLen=s*0.15f;
        pushBox(r,lx-s*0.012f,lx+s*0.012f,ly-legLen+lSwing,ly,-s*0.010f,s*0.010f);}
} // buildLobster

// ============================================================
//  紙箱怪物（buildBox）
//  b = 紙箱色（箱體）  d = 深色（表情/膠帶）
//  ax,ay = 腳底  sc = 縮放  t = 時間  scl = 死亡縮放
// ============================================================
static void buildBoxMonster(std::vector<float>&b,std::vector<float>&dk,
                            float ax,float ay,float sc,
                            double t,float scl){
    if(scl<=0.001f)return;
    float s=sc*1.50f*scl; // 2倍大紙箱
    float bob=sinf((float)t*2.8f)*0.03f*s; // 微微上下晃
    float cy=ay+bob;

    // ── 箱體（大正方形盒子）──────────────────────────────
    float bW=s*0.30f, bH=s*0.32f, bD=s*0.25f;
    pushBox(b,ax-bW,ax+bW,cy,cy+bH*2.f,-bD,bD);

    // ── 箱蓋（頂部微開，兩片翻蓋）──────────────────────
    float lidY=cy+bH*2.f;
    float lidH=s*0.05f;
    float lidAng=sinf((float)t*1.5f)*0.08f*s; // 蓋子微動
    // 左蓋
    pushBox(b,ax-bW,ax-bW*0.05f,lidY,lidY+lidH+lidAng,-bD,bD);
    // 右蓋
    pushBox(b,ax+bW*0.05f,ax+bW,lidY,lidY+lidH-lidAng,-bD,bD);

    // ── 封箱膠帶（深色十字）────────────────────────────
    float tape=s*0.028f;
    float fz=bD+s*0.005f;
    // 正面垂直膠帶
    pushBox(dk,ax-tape,ax+tape,cy+bH*0.1f,cy+bH*2.f,fz,fz+s*0.008f);
    // 正面水平膠帶
    pushBox(dk,ax-bW,ax+bW,cy+bH*1.0f-tape,cy+bH*1.0f+tape,fz,fz+s*0.008f);

    // ── 哀傷的臉（正面）───────────────────────────────
    float faceCY=cy+bH*1.2f;
    float faceZ=fz+s*0.01f;

    // 眼睛（兩個圓，往下垂 = 哀傷）
    float eXO=bW*0.42f, eR=s*0.045f;
    float eY=faceCY+bH*0.22f;
    // 左眼
    pushBox(dk,ax-eXO-eR,ax-eXO+eR,eY-eR,eY+eR,faceZ,faceZ+s*0.01f);
    // 右眼
    pushBox(dk,ax+eXO-eR,ax+eXO+eR,eY-eR,eY+eR,faceZ,faceZ+s*0.01f);
    // 白色亮點（小方塊）
    float hlR=eR*0.3f;
    pushBox(b,ax-eXO+eR*0.3f-hlR,ax-eXO+eR*0.3f+hlR,eY+eR*0.3f-hlR,eY+eR*0.3f+hlR,faceZ+s*0.01f,faceZ+s*0.02f);
    pushBox(b,ax+eXO+eR*0.3f-hlR,ax+eXO+eR*0.3f+hlR,eY+eR*0.3f-hlR,eY+eR*0.3f+hlR,faceZ+s*0.01f,faceZ+s*0.02f);

    // 八字眉（哀傷，內高外低）
    float brW=s*0.055f, brH=s*0.015f;
    float brY=eY+eR+s*0.015f;
    // 左眉（左低右高）
    pushBox(dk,ax-eXO-brW,ax-eXO,brY-brH,brY+brH,faceZ,faceZ+s*0.008f);
    pushBox(dk,ax-eXO,ax-eXO+brW*0.6f,brY,brY+brH*2.5f,faceZ,faceZ+s*0.008f);
    // 右眉（左高右低）
    pushBox(dk,ax+eXO-brW*0.6f,ax+eXO,brY,brY+brH*2.5f,faceZ,faceZ+s*0.008f);
    pushBox(dk,ax+eXO,ax+eXO+brW,brY-brH,brY+brH,faceZ,faceZ+s*0.008f);

    // 嘴巴（大 ∩ 哭嘴）
    float mW=bW*0.35f, mH=s*0.025f;
    float mY=faceCY-bH*0.10f;
    pushBox(dk,ax-mW*0.15f,ax+mW*0.15f,mY+mH,mY+mH*2.5f,faceZ,faceZ+s*0.008f); // 中
    pushBox(dk,ax+mW*0.15f,ax+mW,mY,mY+mH*1.5f,faceZ,faceZ+s*0.008f);           // 右
    pushBox(dk,ax-mW,ax-mW*0.15f,mY,mY+mH*1.5f,faceZ,faceZ+s*0.008f);           // 左

    // 眼淚（兩滴）
    float tW=s*0.016f;
    pushBox(dk,ax-eXO-tW,ax-eXO+tW,eY-eR-s*0.08f,eY-eR,faceZ,faceZ+s*0.006f);
    pushBox(dk,ax+eXO-tW,ax+eXO+tW,eY-eR-s*0.08f,eY-eR,faceZ,faceZ+s*0.006f);
    // 淚珠
    pushBox(dk,ax-eXO-tW*1.3f,ax-eXO+tW*1.3f,eY-eR-s*0.11f,eY-eR-s*0.08f,faceZ,faceZ+s*0.006f);
    pushBox(dk,ax+eXO-tW*1.3f,ax+eXO+tW*1.3f,eY-eR-s*0.11f,eY-eR-s*0.08f,faceZ,faceZ+s*0.006f);

    // ── 小腳（底部兩個小方塊）──────────────────────────
    float ftW=s*0.08f, ftH=s*0.06f;
    float ftSway=sinf((float)t*3.5f)*0.02f*s;
    pushBox(dk,ax-bW*0.5f-ftW+ftSway,ax-bW*0.5f+ftW+ftSway,ay-ftH,ay,-bD*0.3f,bD*0.3f);
    pushBox(dk,ax+bW*0.5f-ftW-ftSway,ax+bW*0.5f+ftW-ftSway,ay-ftH,ay,-bD*0.3f,bD*0.3f);
} // buildBoxMonster

// 依價格區間自動算出「好看」的刻度步距，目標約 targetN 個標籤
static float niceStep(float range,int targetN=6){
    if(range<=0.f)return 1.f;
    float raw=range/targetN;
    float mag=powf(10.f,floorf(log10f(raw)));
    float r=raw/mag;
    float step=(r<=1.f)?mag:(r<=2.f)?2.f*mag:(r<=5.f)?5.f*mag:10.f*mag;
    return step;}

static void buildLabels(std::vector<float>&L,const TF&tf,int vis,int si,const glm::mat4&view){
    if(!tf.data||vis<1)return;
    glm::vec3 R{view[0][0],view[1][0],view[2][0]},U{view[0][1],view[1][1],view[2][1]};
    float sc=(WORLD_HI-WORLD_LO)/140.f;
    float midX=(vis-1)*g_sp*.5f;
    appendText(L,g_stockCode.c_str(),{midX,WORLD_HI+.4f,0.f},R,U,sc*1.3f,.5f);
    if(g_showVol) appendText(L,"VOL",{-2.f*g_sp,(VOL_LO+VOL_HI)*.5f,0.f},R,U,sc,.0f);
    {std::string subLbl;
     if(g_showMACD)subLbl+="MACD ";if(g_showKD)subLbl+="KD ";if(g_showRSI)subLbl+="RSI";
     if(!subLbl.empty()){
         // trim trailing space
         while(!subLbl.empty()&&subLbl.back()==' ')subLbl.pop_back();
         appendText(L,subLbl.c_str(),{-2.f*g_sp,MACD_MID+.2f,0.f},R,U,sc*.8f,.0f);}}
    // 依可見價格區間動態計算步距與小數位數
    float rs=niceStep(g_pMax-g_pMin);
    int dec=(rs<1.f)?2:(rs<10.f)?1:0;
    char fmt[8];snprintf(fmt,sizeof(fmt),"%%.%df",dec);
    for(float p=std::ceil(g_pMin/rs)*rs;p<=g_pMax+rs*0.01f;p+=rs){
        char buf[16];snprintf(buf,sizeof(buf),fmt,p);
        appendText(L,buf,{-1.8f*g_sp,toW(p),0.f},R,U,sc,1.f);}
    // 時間軸標籤（marks/labels 要扣掉 startIdx 轉成顯示位置）
    if(tf.marks){
        for(int i=0;i<tf.nmarks;i++){
            int di=tf.marks[i].idx-si;
            if(di<0)continue;if(di>=vis)break;
            appendText(L,tf.marks[i].lbl,{di*g_sp,WORLD_LO-.7f,0.f},R,U,sc*.8f,.5f);}
    }else if(tf.labels){
        for(int i=0;i<vis;i++){int ai=si+i;
            if(ai<tf.cnt)appendText(L,tf.labels[ai],{i*g_sp,WORLD_LO-.7f,0.f},R,U,sc*.8f,.5f);}
    }
    struct{int p;const char*lb;}mas[]={{5,"MA5"},{20,"MA20"},{60,"MA60"},{120,"MA120"},{240,"MA240"}};
    float lx=(vis+.2f)*g_sp;
    for(int mi=0;mi<5;mi++){if(!g_showMA[mi]||vis<mas[mi].p)continue;
        int ns=std::max(0,vis-mas[mi].p);float sum=0;
        for(int j=ns;j<vis;j++)sum+=tf.data[j].c;
        appendText(L,mas[mi].lb,{lx,toW(sum/(vis-ns)),0.f},R,U,sc*.8f,.0f);}}

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

static void rebuildAll(const TF&tf,int vis){
    recalcLayout();
    mGreen.del();mRed.del();mGrid.del();
    mMA5.del();mMA20.del();mMA60.del();mMA120.del();mMA240.del();
    mVolG.del();mVolR.del();mMHG.del();mMHR.del();mDIF.del();mDEA.del();
    mKK.del();mKDLine.del();mRSI.del();mBBUpper.del();mBBLower.del();mBBMid.del();
    if(!tf.data||tf.cnt<1)return;
    // 回放時從頭播；一般時從最新往左偏移 panOffset
    if(g_playing){
        g_startIdx=0;
    } else {
        int maxSI=std::max(0,tf.cnt-vis);
        g_startIdx=std::max(0,maxSI-g_panOffset);
    }
    const Candle* d = tf.data + g_startIdx;
    int n = std::min(vis, tf.cnt - g_startIdx);  // 實際可用根數
    // 間距依「可見根數」縮放，放大時蠟燭變寬
    g_sp=std::min(1.6f,50.f/std::max(1,n));g_bHW=g_sp*.32f;g_wHW=g_sp*.05f;
    // 價格範圍只算可見蠟燭
    g_pMin=d[0].l;g_pMax=d[0].h;
    for(int i=1;i<n;i++){g_pMin=std::min(g_pMin,d[i].l);g_pMax=std::max(g_pMax,d[i].h);}
    float pad=(g_pMax-g_pMin)*.06f;g_pMin-=pad;g_pMax+=pad;
    std::vector<float>gV,rV,gr,m5,m20,m60,m120,m240,vG,vR,hG,hR,dif,dea,kk,kdl;
    std::vector<float>rsiL,bbUp,bbDn,bbMid;
    buildCandles(gV,rV,d,n);buildGrid(gr,n);
    if(g_showMA[0])buildSMA(m5,  d,n,5);
    if(g_showMA[1])buildSMA(m20, d,n,20);
    if(g_showMA[2])buildSMA(m60, d,n,60);
    if(g_showMA[3])buildSMA(m120,d,n,120);
    if(g_showMA[4])buildSMA(m240,d,n,240);
    if(g_showVol)  buildVolume(vG,vR,d,n);
    if(g_showMACD) buildMACD(hG,hR,dif,dea,d,n);
    if(g_showKD)   buildKD(kk,kdl,d,n);
    if(g_showRSI)  buildRSI(rsiL,d,n);
    if(g_showBB)   buildBollinger(bbUp,bbDn,bbMid,d,n);
    mGreen.init(gV,true);mRed.init(rV,true);mGrid.init(gr,false);
    mMA5.init(m5,false);mMA20.init(m20,false);mMA60.init(m60,false);
    mMA120.init(m120,false);mMA240.init(m240,false);
    if(g_showVol){mVolG.init(vG,true);mVolR.init(vR,true);}
    if(g_showMACD){mMHG.init(hG,true);mMHR.init(hR,true);mDIF.init(dif,false);mDEA.init(dea,false);}
    if(g_showKD){mKK.init(kk,false);mKDLine.init(kdl,false);}
    if(g_showRSI)mRSI.init(rsiL,false);
    if(g_showBB){mBBUpper.init(bbUp,false);mBBLower.init(bbDn,false);mBBMid.init(bbMid,false);}}

// ============================================================
//  ImGui panel
// ============================================================
// ============================================================
//  Mouse hover – ray cast onto z=0 plane, find nearest candle
// ============================================================
static void updateHover(double mx,double my,int vw,int fh,const glm::mat4&MVP){
    g_hoveredCandle=-1;
    if(ImGui::GetIO().WantCaptureMouse)return;
    if(mx<0||mx>=vw||my<0||my>=fh)return;
    if(!g_tfs[g_tf].data||g_visible<1)return;
    float nx=(2.f*(float)mx/vw)-1.f;
    float ny=1.f-(2.f*(float)my/fh);
    glm::mat4 inv=glm::inverse(MVP);
    glm::vec4 nP=inv*glm::vec4(nx,ny,-1.f,1.f);
    glm::vec4 fP=inv*glm::vec4(nx,ny, 1.f,1.f);
    nP/=nP.w;fP/=fP.w;
    glm::vec3 ro=glm::vec3(nP),rd=glm::normalize(glm::vec3(fP)-ro);
    if(std::abs(rd.z)<1e-6f)return;
    float t=-ro.z/rd.z;if(t<0)return;
    float wx=ro.x+t*rd.x;
    g_hoverWY=ro.y+t*rd.y;
    int idx=(int)std::round(wx/g_sp);
    if(idx>=0&&idx<g_visible)g_hoveredCandle=idx;}

// Return a human-readable date label for display-index idx (actual = idx + g_startIdx)
static std::string getCandleLabel(int idx){
    int ai=idx+g_startIdx;  // actual index in full data array
    if(!g_store[g_tf].dates.empty()&&ai<(int)g_store[g_tf].dates.size())
        return g_store[g_tf].dates[ai];
    if(g_tfs[g_tf].labels&&ai<g_tfs[g_tf].cnt)
        return g_tfs[g_tf].labels[ai];
    if(g_tfs[g_tf].marks&&g_tfs[g_tf].nmarks>0){
        const char* best=g_tfs[g_tf].marks[0].lbl;
        for(int i=0;i<g_tfs[g_tf].nmarks;i++){
            if(g_tfs[g_tf].marks[i].idx<=ai)best=g_tfs[g_tf].marks[i].lbl;
            else break;}
        return best;}
    char b[16];snprintf(b,sizeof(b),"#%d",ai+1);return b;}

static char g_codeBuf[16]="2330";
static std::string g_failMsg;

// ============================================================
//  自選股清單
// ============================================================
struct WatchItem{std::string code,name;};
static std::vector<WatchItem>g_watchlist;

#ifndef __EMSCRIPTEN__
static std::string watchlistPath(){
    char*ap=nullptr;size_t sz=0;_dupenv_s(&ap,&sz,"APPDATA");
    std::string base=ap?std::string(ap)+"\\KLineGL3D":".";
    free(ap);
    CreateDirectoryA(base.c_str(),nullptr);
    return base+"\\watchlist.txt";}
static void loadWatchlist(){
    g_watchlist.clear();
    std::ifstream f(watchlistPath());
    std::string line;
    while(std::getline(f,line)){
        auto sp=line.find('\t');
        if(sp!=std::string::npos)
            g_watchlist.push_back({line.substr(0,sp),line.substr(sp+1)});}}
static void saveWatchlist(){
    std::ofstream f(watchlistPath());
    for(auto&w:g_watchlist)f<<w.code<<'\t'<<w.name<<'\n';}
#else
static void loadWatchlist(){}
static void saveWatchlist(){}
#endif

// 啟動抓取（平台差異封裝）
static void startFetch(const std::string&code){
#ifndef __EMSCRIPTEN__
    std::thread(fetchWorker,code).detach();
#else
    // Emscripten: 直接用硬編碼資料，跳過網路
    g_loadDone=true;
#endif
}

// ============================================================
//  台股族群分類資料
// ============================================================
struct StockItem{const char*code,*name;};
struct Sector{const char*name;const StockItem*items;int n;};

static const StockItem S_ETF[]={
    {"0050","元大台灣50"},{"0056","元大高股息"},{"006208","富邦台50"},
    {"00878","國泰永續高股息"},{"00881","國泰台灣5G+"},{"00919","群益台灣精選高息"}};
static const StockItem S_SEMI[]={
    {"2330","台積電"},{"2303","聯電"},{"2454","聯發科"},
    {"2379","瑞昱"},{"3711","日月光投控"},{"2408","南亞科"},{"2337","旺宏"}};
static const StockItem S_TECH[]={
    {"2317","鴻海"},{"2382","廣達"},{"2357","華碩"},
    {"3008","大立光"},{"2308","台達電"},{"2395","研華"},{"2301","光寶科"}};
static const StockItem S_FIN[]={
    {"2882","國泰金"},{"2881","富邦金"},{"2886","兆豐金"},
    {"2884","玉山金"},{"2885","元大金"},{"2892","第一金"},{"2880","華南金"}};
static const StockItem S_SHIP[]={
    {"2603","長榮"},{"2609","陽明"},{"2615","萬海"},
    {"2610","華航"},{"2618","長榮航"},{"2606","裕民"}};
static const StockItem S_TEL[]={
    {"2412","中華電"},{"3045","台灣大"},{"4904","遠傳"},{"3672","康舒"}};
static const StockItem S_TRAD[]={
    {"1301","台塑"},{"1303","南亞"},{"1326","台化"},
    {"2002","中鋼"},{"2912","統一超"},{"1216","統一"}};
static const StockItem S_BIO[]={
    {"4711","中裕"},{"6547","安咖科"},{"1788","中鴻"},{"4153","鈺緯"}};

static const Sector SECTORS[]={
    {"ETF",           S_ETF,  (int)(sizeof S_ETF /sizeof*S_ETF)},
    {"半導體",        S_SEMI, (int)(sizeof S_SEMI/sizeof*S_SEMI)},
    {"電子/科技",     S_TECH, (int)(sizeof S_TECH/sizeof*S_TECH)},
    {"金融",          S_FIN,  (int)(sizeof S_FIN /sizeof*S_FIN)},
    {"航運",          S_SHIP, (int)(sizeof S_SHIP/sizeof*S_SHIP)},
    {"電信",          S_TEL,  (int)(sizeof S_TEL /sizeof*S_TEL)},
    {"傳產",          S_TRAD, (int)(sizeof S_TRAD/sizeof*S_TRAD)},
    {"生技/醫療",     S_BIO,  (int)(sizeof S_BIO /sizeof*S_BIO)},
};
static const int N_SECTORS=(int)(sizeof SECTORS/sizeof*SECTORS);

// ── 啟動選股畫面 ──
static void drawStockMenu(int fw,int fh){
    ImVec2 center((float)fw*0.5f,(float)fh*0.5f);
    float menuW=420.f, menuH=500.f;
    ImGui::SetNextWindowPos(ImVec2(center.x-menuW*0.5f,center.y-menuH*0.5f),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menuW,menuH),ImGuiCond_Always);
    ImGui::Begin("##stockmenu",nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

    // 標題
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.82f,.20f,1.f));
    float titleW=ImGui::CalcTextSize("K-Line RPG").x;
    ImGui::SetCursorPosX((menuW-titleW)*0.5f);
    ImGui::Text("K-Line RPG");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // 輸入股票代碼
    ImGui::Text("輸入股票代碼：");
    ImGui::SetNextItemWidth(menuW-20.f);
    bool enter=ImGui::InputText("##menucode",g_codeBuf,sizeof(g_codeBuf),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    // 快速選股按鈕
    ImGui::Text("熱門股票：");
    struct QuickPick{const char*code;const char*name;};
    static const QuickPick picks[]={
        {"2330","台積電"},{"0050","元大台灣50"},{"2317","鴻海"},
        {"2454","聯發科"},{"2881","富邦金"},{"2882","國泰金"},
        {"2303","聯電"},{"3711","日月光"},{"2412","中華電"},
        {"1301","台塑"},{"2308","台達電"},{"2002","中鋼"}};
    for(int i=0;i<12;i++){
        if(i%3!=0)ImGui::SameLine();
        char lbl[32];snprintf(lbl,sizeof(lbl),"%s\n%s##qp%d",picks[i].code,picks[i].name,i);
        if(ImGui::Button(lbl,ImVec2((menuW-30.f)/3.f,45.f))){
            snprintf(g_codeBuf,sizeof(g_codeBuf),"%s",picks[i].code);
            enter=true;}}

    ImGui::Spacing();
    // 自選股
    if(!g_watchlist.empty()){
        ImGui::Separator();
        ImGui::Text("自選股：");
        for(int i=0;i<(int)g_watchlist.size();i++){
            auto&w=g_watchlist[i];
            char wl[48];snprintf(wl,sizeof(wl),"%s %s##wm%d",w.code.c_str(),w.name.c_str(),i);
            if(ImGui::Selectable(wl)){
                snprintf(g_codeBuf,sizeof(g_codeBuf),"%s",w.code.c_str());
                enter=true;}}}

    // 確認載入
    if(enter && g_codeBuf[0] && g_mode!=LOADING){
        g_loadDone=false;g_loadFail=false;g_loadProgress=0;g_failMsg.clear();
        g_mode=LOADING;
        startFetch(std::string(g_codeBuf));}

    // Loading 狀態
    if(g_mode==LOADING){
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(.4f,.8f,1.f,1.f),"載入中... %d/%d",
            g_loadProgress.load(),g_loadTotal.load());}
    if(!g_failMsg.empty())
        ImGui::TextColored(ImVec4(1.f,.3f,.2f,1.f),"%s",g_failMsg.c_str());

    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    ImGui::TextColored(ImVec4(.8f,.9f,1.f,1.f),"=== 遊戲玩法 ===");
    ImGui::BulletText("D = 左移 10 格  F = 右移 10 格");
    ImGui::BulletText("Space = 跳躍（可打飛行龍蝦）");
    ImGui::BulletText("J = 鐮刀攻擊（範圍 10 格）");
    ImGui::BulletText("K = 鈔票魔法（消耗 75 MP）");
    ImGui::BulletText("R = 死亡後重新開始");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.f,.85f,.3f,1.f),"怪物介紹：");
    ImGui::BulletText("韭菜 HP100 - 地面走路");
    ImGui::BulletText("龍蝦 HP300 - 空中飛行 + 丟迴力鏢螯");
    ImGui::BulletText("紙箱 HP200 - 緩慢接近");
    ImGui::Spacing();
    ImGui::TextDisabled("選擇股票後開始遊戲！");

    ImGui::End();}

static void drawPanel(int fw,int fh){
    float pw=(float)PANEL_W;
    ImGui::SetNextWindowPos(ImVec2((float)fw-pw,0.f),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw,(float)fh),ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    ImGui::Begin("##panel",nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ── 股票 ──
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.82f,.25f,1.f));
    ImGui::TextWrapped("%s %s",g_stockCode.c_str(),g_stockName.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    // ── 族群選股 ──
    ImGui::Text("族群選股");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(4,2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(4,2));
    {bool isLoading=(g_mode==LOADING);
    for(int s=0;s<N_SECTORS;s++){
        ImGui::PushID(s);
        bool open=ImGui::CollapsingHeader(SECTORS[s].name);
        if(open){
            ImGui::Indent(6.f);
            for(int i=0;i<SECTORS[s].n;i++){
                const StockItem&st=SECTORS[s].items[i];
                bool cur=(g_stockCode==st.code);
                if(cur)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.82f,.25f,1.f));
                char lbl[40];snprintf(lbl,sizeof(lbl),"%s  %s",st.code,st.name);
                if(ImGui::Selectable(lbl,cur,ImGuiSelectableFlags_DontClosePopups,ImVec2(0,0))
                   &&!isLoading&&!cur){
                    snprintf(g_codeBuf,sizeof(g_codeBuf),"%s",st.code);
                    g_loadDone=false;g_loadFail=false;g_loadProgress=0;g_failMsg.clear();
                    g_mode=LOADING;
                    startFetch(std::string(st.code));}
                if(cur)ImGui::PopStyleColor();}
            ImGui::Unindent(6.f);}
        ImGui::PopID();}}
    ImGui::PopStyleVar(2);
    ImGui::Separator();

    ImGui::Text("股票代號");
    bool loading=(g_mode==LOADING);
    ImGui::SetNextItemWidth(130.f);
    bool enter=ImGui::InputText("##code",g_codeBuf,sizeof(g_codeBuf),
                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if(loading)ImGui::BeginDisabled();
    if((ImGui::Button("載入")||enter)&&!loading&&g_codeBuf[0]){
        g_loadDone=false;g_loadFail=false;g_loadProgress=0;g_failMsg.clear();
        g_mode=LOADING;
        startFetch(std::string(g_codeBuf));}
    if(loading)ImGui::EndDisabled();

    if(loading){
        float p=(float)g_loadProgress/std::max(1,g_loadTotal.load());
        char pb[32];snprintf(pb,sizeof(pb),"%d/%d",g_loadProgress.load(),g_loadTotal.load());
        ImGui::ProgressBar(p,ImVec2(-1.f,12.f),pb);
        ImGui::TextDisabled("從證交所下載中...");}
    else if(!g_failMsg.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.f,.4f,.4f,1.f));
        ImGui::TextWrapped("%s",g_failMsg.c_str());
        ImGui::PopStyleColor();}

    ImGui::Separator();

    // ── 時間週期 ──
    ImGui::Text("時間週期");
    const char*tfl[]={"日","週","月","季","年"};
    for(int i=0;i<5;i++){
        if(i>0)ImGui::SameLine();
        bool act=(g_tf==i);
        if(act)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.22f,.48f,.82f,1.f));
        if(ImGui::Button(tfl[i],ImVec2(37.f,22.f))&&i!=g_tf){
            g_tf=i;g_visible=tfMaxVis(i);g_playing=false;g_panOffset=0;g_hitBarIdx=-1,g_bearNeedsInit=true;g_dirty=true;}
        if(act)ImGui::PopStyleColor();}

    ImGui::Separator();

    // ── 回放 ──
    ImGui::Text("回放");
    int total=g_tfs[g_tf].cnt;

    if(ImGui::Button("|<",ImVec2(30,22))){g_visible=0;g_playing=false;g_dirty=true;}
    ImGui::SameLine();
    if(ImGui::Button(" < ",ImVec2(30,22))&&!g_playing&&g_visible>0){g_visible--;g_dirty=true;}
    ImGui::SameLine();
    if(ImGui::Button(g_playing?" || ":" |> ",ImVec2(40,22))){
        if(g_visible<total)g_playing=!g_playing;
        else{g_visible=0;g_playing=true;g_dirty=true;}
        g_lastStep=glfwGetTime();}
    ImGui::SameLine();
    if(ImGui::Button(" > ",ImVec2(30,22))&&!g_playing&&g_visible<total){g_visible++;g_dirty=true;}
    ImGui::SameLine();
    if(ImGui::Button(">|",ImVec2(30,22))){g_visible=total;g_playing=false;g_dirty=true;}

    float prog=total>0?(float)g_visible/total:0.f;
    char pbuf[32];snprintf(pbuf,sizeof(pbuf),"%d / %d",g_visible,total);
    ImGui::ProgressBar(prog,ImVec2(-1.f,10.f),pbuf);

    ImGui::Text("速度");ImGui::SameLine();
    float spd=(float)(1.0/std::max(g_stepInt,0.001));
    ImGui::SetNextItemWidth(-1.f);
    if(ImGui::SliderFloat("##spd",&spd,1.f,30.f,"%.0fx"))g_stepInt=1.0/spd;

    ImGui::Separator();

    // ── 最新K線 ──
    if(g_tfs[g_tf].data&&g_visible>0){
        const Candle&c=g_tfs[g_tf].data[g_startIdx+g_visible-1];
        float chg=c.c-c.o,pct=c.o>0?chg/c.o*100.f:0.f;
        bool up=(chg>=0);
        ImVec4 col=up?ImVec4(1.f,.38f,.38f,1.f):ImVec4(.25f,.95f,.45f,1.f);
        ImGui::Text("最新K線");
        ImGui::TextColored(col,"收  %.2f  %+.2f (%.1f%%)",c.c,chg,pct);
        ImGui::Text("開  %.2f", c.o); ImGui::SameLine(130); ImGui::Text("高  %.2f",c.h);
        ImGui::Text("低  %.2f", c.l); ImGui::SameLine(130); ImGui::Text("量 %.0f萬",c.v);}

    ImGui::Separator();

    // ── 副圖開關 ──
    ImGui::Text("副圖");
    bool chg=false;
    chg|=ImGui::Checkbox("成交量",&g_showVol);
    ImGui::SameLine(140); chg|=ImGui::Checkbox("布林",&g_showBB);
    chg|=ImGui::Checkbox("MACD",&g_showMACD);
    ImGui::SameLine(140); chg|=ImGui::Checkbox("KD",&g_showKD);
    chg|=ImGui::Checkbox("RSI(14)",&g_showRSI);
    if(chg)g_dirty=true;

    ImGui::Separator();

    // ── 均線 ──
    ImGui::Text("均線");
    struct{const char*n;ImVec4 col;}mleg[]={
        {"MA5",  {.95f,.80f,.20f,1.f}},{"MA20", {.20f,.85f,.90f,1.f}},
        {"MA60", {.90f,.30f,.75f,1.f}},{"MA120",{.95f,.55f,.10f,1.f}},
        {"MA240",{.70f,.50f,.90f,1.f}}};
    for(int i=0;i<5;i++){
        // 畫一個小色塊
        ImVec2 p=ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(p.x,p.y+4),ImVec2(p.x+10,p.y+14),
            ImGui::ColorConvertFloat4ToU32(mleg[i].col));
        ImGui::Dummy(ImVec2(13,0));ImGui::SameLine(0,2);
        // checkbox 用顏色標示文字
        ImGui::PushStyleColor(ImGuiCol_Text,
            g_showMA[i]?mleg[i].col:ImVec4(.45f,.50f,.60f,1.f));
        bool chkMA=ImGui::Checkbox(mleg[i].n,&g_showMA[i]);
        ImGui::PopStyleColor();
        if(chkMA)g_dirty=true;
        // 每兩個一排
        if(i%2==0)ImGui::SameLine(148);}

    ImGui::Separator();

    // ── 自選股清單 ──
    ImGui::Text("自選股");
    {bool alreadyIn=false;
     for(auto&w:g_watchlist)if(w.code==g_stockCode){alreadyIn=true;break;}
     if(!alreadyIn){
         if(ImGui::SmallButton("+ 加入")){
             g_watchlist.push_back({g_stockCode,g_stockName});
             saveWatchlist();}
     } else {
         if(ImGui::SmallButton("- 移除")){
             g_watchlist.erase(
                 std::remove_if(g_watchlist.begin(),g_watchlist.end(),
                     [](const WatchItem&w){return w.code==g_stockCode;}),
                 g_watchlist.end());
             saveWatchlist();}}
     bool loading2=(g_mode==LOADING);
     for(int i=0;i<(int)g_watchlist.size();i++){
         const auto&w=g_watchlist[i];
         bool cur=(g_stockCode==w.code);
         if(cur)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.82f,.25f,1.f));
         char wlbl[48];snprintf(wlbl,sizeof(wlbl),"%s %s##wl%d",w.code.c_str(),w.name.c_str(),i);
         if(ImGui::Selectable(wlbl,cur,ImGuiSelectableFlags_DontClosePopups)&&!cur&&!loading2){
             snprintf(g_codeBuf,sizeof(g_codeBuf),"%s",w.code.c_str());
             g_loadDone=false;g_loadFail=false;g_loadProgress=0;g_failMsg.clear();
             g_panOffset=0;
             g_mode=LOADING;
             startFetch(w.code);}
         if(cur)ImGui::PopStyleColor();}}

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.95f,.25f,.18f,1.f));
    ImGui::Checkbox("拉拉熊跳跳",&g_bearActive);
    ImGui::PopStyleColor();
    if(g_bearActive){
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.25f,.85f,.45f,1.f));
        if(ImGui::Checkbox("遊戲模式 (DF移動 J攻擊 K魔法)",&g_gameMode)){
            g_gameMoveDir=0;g_ants.clear();g_lobs.clear();g_boxes.clear();g_claws.clear();
            g_lastAntSpawn=g_lastLobSpawn=g_lastBoxSpawn=-99.0;g_killCount=0;
            g_bearHP=100.f;g_bearMP=500.f;g_bearDead=false;}
        ImGui::PopStyleColor();
        if(g_gameMode){
            ImGui::TextDisabled("韭菜怪物會自動出現！");
            int money=10000+g_killCount*100;
            ImGui::TextColored(ImVec4(.95f,.85f,.15f,1.f),"$ %d (+%d kills)",money,g_killCount);}}
    ImGui::Separator();
    ImGui::TextDisabled("滾輪：K線縮放");
    ImGui::TextDisabled("Ctrl+滾輪：鏡頭縮放");
    ImGui::TextDisabled("拖曳：旋轉視角");
    ImGui::TextDisabled("Shift+拖曳：時間平移");
    ImGui::TextDisabled("數字1-5：切換週期");

    ImGui::End();}

// Floating tooltip shown over 3D area when hovering a candle
static void drawHoverTooltip(){
    if(g_hoveredCandle<0||!g_tfs[g_tf].data)return;
    const Candle&c=g_tfs[g_tf].data[g_startIdx+g_hoveredCandle];
    float chg=c.c-c.o,pct=c.o>0?chg/c.o*100.f:0.f;
    bool up=(chg>=0);
    ImVec4 col=up?ImVec4(1.f,.40f,.40f,1.f):ImVec4(.25f,.95f,.45f,1.f);
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(getCandleLabel(g_hoveredCandle).c_str());
    ImGui::Separator();
    ImGui::Text("開  %.2f",c.o);
    ImGui::Text("高  %.2f",c.h);
    ImGui::Text("低  %.2f",c.l);
    ImGui::TextColored(col,"收  %.2f   %+.2f (%.1f%%)",c.c,chg,pct);
    ImGui::Text("量  %.1f 萬張",c.v);
    ImGui::EndTooltip();}

// 拉拉熊停留的 K 棒報價泡泡 / 遊戲模式金錢顯示
static void drawBearPriceBubble(int vw,int fh){
    if(!g_bearActive||g_bearScreenX<0.f)return;
    if(!g_tfs[g_tf].data||g_visible<1)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();

    if(g_gameMode) return; // 遊戲模式用頂部 HUD，不在頭上畫泡泡

    int bi=std::min(g_bearIdx,g_visible-1);
    const Candle&c=g_tfs[g_tf].data[g_startIdx+bi];
    float chg=c.c-c.o, pct=c.o>0?chg/c.o*100.f:0.f;
    bool up=(chg>=0);
    std::string date=getCandleLabel(bi);

    char line1[32],line2[32],line3[32];
    snprintf(line1,sizeof(line1),"%.2f  %+.1f%%",c.c,pct);
    snprintf(line2,sizeof(line2),"開%.2f 高%.2f",c.o,c.h);
    snprintf(line3,sizeof(line3),"低%.2f 量%.0f",c.l,c.v);

    float lw=ImGui::CalcTextSize(line1).x;
    lw=std::max(lw,ImGui::CalcTextSize(line2).x);
    lw=std::max(lw,ImGui::CalcTextSize(line3).x);
    lw=std::max(lw,ImGui::CalcTextSize(date.c_str()).x);

    float bW=lw+14.f, bH=74.f;
    float bx=g_bearScreenX-bW/2.f;
    float by_=g_bearScreenY-bH-6.f;
    bx=std::max(4.f,std::min(bx,(float)vw-bW-4.f));
    by_=std::max(4.f,by_);

    dl->AddRectFilled(ImVec2(bx,by_),ImVec2(bx+bW,by_+bH),IM_COL32(20,24,38,220),5.f);
    dl->AddRect(ImVec2(bx,by_),ImVec2(bx+bW,by_+bH),IM_COL32(140,110,60,200),5.f,0,1.2f);
    float tx=g_bearScreenX;
    dl->AddTriangleFilled(ImVec2(tx-5,by_+bH),ImVec2(tx+5,by_+bH),ImVec2(tx,by_+bH+6),IM_COL32(20,24,38,220));
    dl->AddText(ImVec2(bx+7,by_+4),IM_COL32(160,170,200,255),date.c_str());
    ImU32 priceCol=up?IM_COL32(255,100,100,255):IM_COL32(80,230,130,255);
    dl->AddText(ImVec2(bx+7,by_+20),priceCol,line1);
    dl->AddText(ImVec2(bx+7,by_+38),IM_COL32(190,195,210,255),line2);
    dl->AddText(ImVec2(bx+7,by_+55),IM_COL32(190,195,210,255),line3);}

// 十字準線 ImGui 覆蓋標籤（價格 + 日期）
static void drawCrosshairOverlay(int vw,int fh,const glm::mat4&MVP){
    if(g_hoveredCandle<0||!g_tfs[g_tf].data)return;
    // 世界座標 → 螢幕像素
    auto w2s=[&](float wx,float wy)->ImVec2{
        glm::vec4 p=MVP*glm::vec4(wx,wy,0.f,1.f);p/=p.w;
        return ImVec2((p.x+1.f)*.5f*vw,(1.f-p.y)*.5f*fh);};
    ImDrawList*dl=ImGui::GetForegroundDrawList();
    // ── 價格標籤（主圖範圍內才顯示）──
    if(g_hoverWY>=WORLD_LO-0.5f&&g_hoverWY<=WORLD_HI+0.5f){
        float price=g_pMin+(g_hoverWY-WORLD_LO)/(WORLD_HI-WORLD_LO)*(g_pMax-g_pMin);
        ImVec2 sp=w2s(g_hoveredCandle*g_sp,g_hoverWY);
        char pbuf[16];snprintf(pbuf,sizeof(pbuf),"%.2f",price);
        float tw=ImGui::CalcTextSize(pbuf).x;
        float px=vw-tw-10.f;
        if(px<0)px=0;
        dl->AddRectFilled(ImVec2(px-2,sp.y-9),ImVec2((float)vw-1.f,sp.y+9),IM_COL32(30,55,110,215));
        dl->AddText(ImVec2(px,sp.y-8),IM_COL32(200,225,255,255),pbuf);}
    // ── 日期標籤（X軸底部）──
    float cx=g_hoveredCandle*g_sp;
    bool hasSub3=g_showMACD||g_showKD||g_showRSI;
    float yBot=hasSub3?MACD_LO:(g_showVol?VOL_LO:WORLD_LO);
    ImVec2 bpos=w2s(cx,yBot-0.9f);
    std::string dlbl=getCandleLabel(g_hoveredCandle);
    float tw2=ImGui::CalcTextSize(dlbl.c_str()).x;
    float bx=bpos.x-tw2*.5f;
    bx=std::max(0.f,std::min(bx,(float)vw-tw2));
    dl->AddRectFilled(ImVec2(bx-2,bpos.y),ImVec2(bx+tw2+4,bpos.y+16),IM_COL32(30,55,110,215));
    dl->AddText(ImVec2(bx,bpos.y+1),IM_COL32(200,225,255,255),dlbl.c_str());}

// ============================================================
//  Callbacks
// ============================================================
void onKey(GLFWwindow*,int k,int sc,int a,int mod){
    ImGui_ImplGlfw_KeyCallback(g_win,k,sc,a,mod);
    if(ImGui::GetIO().WantCaptureKeyboard)return;
    if(a==GLFW_RELEASE)return;
    if(g_mode==LOADING)return;
    if(k>=GLFW_KEY_1&&k<=GLFW_KEY_5){int i=k-GLFW_KEY_1;
        if(i!=g_tf){g_tf=i;g_visible=tfMaxVis(i);g_playing=false;g_panOffset=0;g_dirty=true;}}
    // 空白鍵在遊戲模式=跳躍，非遊戲模式不做事（不回放）
    if(k==GLFW_KEY_SPACE && !g_gameMode){}
#ifdef __EMSCRIPTEN__
    if(k==GLFW_KEY_R&&!g_gameMode){emscripten_run_script("location.reload()");return;}
#endif
    if(k==GLFW_KEY_R){g_visible=0;g_playing=false;g_dirty=true;}
    if(k==GLFW_KEY_RIGHT&&!g_playing&&g_visible<g_tfs[g_tf].cnt){g_visible++;g_dirty=true;}
    if(k==GLFW_KEY_LEFT&&!g_playing&&g_visible>0){g_visible--;g_dirty=true;}
    // ── 遊戲模式：D=左跳, F=右跳, J=攻擊, K=魔法 ──
    if(g_gameMode&&g_bearActive){
        // 死亡後按 R 重生
        if(g_bearDead && k==GLFW_KEY_R){
            g_bearDead=false;g_bearHP=100.f;g_bearMP=500.f;
            g_ants.clear();g_lobs.clear();g_boxes.clear();g_claws.clear();g_killCount=0;
            g_lastAntSpawn=g_lastLobSpawn=g_lastBoxSpawn=-99.0;}
        if(g_bearDead) return; // 死亡中不接受其他操作
        if(k==GLFW_KEY_D){g_gameMoveDir=-1;g_bearFaceDir=-1;sfxJump();}
        if(k==GLFW_KEY_F){g_gameMoveDir=+1;g_bearFaceDir=+1;sfxJump();}
        if(k==GLFW_KEY_SPACE && (glfwGetTime()-g_bearJumpT)>0.6){g_bearJumpT=glfwGetTime();sfxJump();} // 跳躍
        if(k==GLFW_KEY_J){g_slashStartT=glfwGetTime();g_slashTextT=glfwGetTime();sfxSlash();}
        if(k==GLFW_KEY_K){
            // 魔法：丟一捆鈔票（面向方向，打5隻，消耗 50 MP）
            if(g_bearMP>=75.f && !g_bearDead){
                g_bearMP-=75.f;
                double now2=glfwGetTime();
                g_cashProjT=now2;
                g_cashProjX=g_bearIdx*g_sp;
                g_cashProjY=0.f; // 會在 render 時用 bearY 修正
                g_cashProjDir=g_bearFaceDir; if(g_cashProjDir==0)g_cashProjDir=1;
                g_magicT=now2;
                sfxMagic();}}}}
void onChar(GLFWwindow*,unsigned int cp){ImGui_ImplGlfw_CharCallback(g_win,cp);}
void onMB(GLFWwindow*w,int b,int a,int mod){
    ImGui_ImplGlfw_MouseButtonCallback(g_win,b,a,mod);
    if(ImGui::GetIO().WantCaptureMouse)return;
    bool shift=(mod&GLFW_MOD_SHIFT)!=0;
    bool press=(a==GLFW_PRESS);
    glfwGetCursorPos(w,&g_lx,&g_ly);
    if(b==GLFW_MOUSE_BUTTON_LEFT){
        if(shift){
            // Shift+左鍵 = 時間平移
            g_panDragging=press;
            if(press){g_panDragStartX=g_lx;g_panDragStartOffset=g_panOffset;}
            g_drag=false;
        } else {
            g_drag=press;
            if(press&&g_panDragging)g_panDragging=false;
        }}
    // 右鍵不再觸發魔法（改用 K 鍵）
    if(b==GLFW_MOUSE_BUTTON_MIDDLE){
        // 中鍵 = 時間平移
        g_panDragging=press;
        if(press){g_panDragStartX=g_lx;g_panDragStartOffset=g_panOffset;}}}
void onCursor(GLFWwindow*,double x,double y){
    if(!ImGui::GetIO().WantCaptureMouse){
        if(g_drag)g_cam.drag((float)(x-g_lx),(float)(y-g_ly));
        if(g_panDragging&&g_vpW>0&&g_visible>0){
            double ddx=x-g_panDragStartX;
            float ppc=(float)g_vpW/g_visible;  // pixels per candle
            int dc=(int)(ddx/ppc);
            int cnt=g_tfs[g_tf].cnt;
            int maxPan=std::max(0,cnt-g_visible);
            g_panOffset=std::max(0,std::min(maxPan,g_panDragStartOffset-dc));
            g_dirty=true;}}
    g_lx=x;g_ly=y;}
void onScroll(GLFWwindow*,double dx,double dy){
    ImGui_ImplGlfw_ScrollCallback(g_win,dx,dy);
    if(ImGui::GetIO().WantCaptureMouse)return;
    bool ctrl=(glfwGetKey(g_win,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS||
               glfwGetKey(g_win,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS);
    if(ctrl){g_cam.zoom((float)dy);return;}
    int n=g_tfs[g_tf].cnt;if(n<1)return;
    int minV=TF_MIN_VIS[g_tf];
    int maxV=tfMaxVis(g_tf);
    int step=std::max(1,(int)(g_visible*.1f));
    if(dy>0)g_visible=std::max(minV,g_visible-step);  // 放大 → 減少根數
    else    g_visible=std::min(maxV,g_visible+step);  // 縮小 → 增加根數
    g_dirty=true;}

// ============================================================
//  main
// ============================================================
// GL uniform helpers (global for emscripten mainLoopBody access)
static void uM4(GLuint p,const char*n,const glm::mat4&m){glUniformMatrix4fv(glGetUniformLocation(p,n),1,GL_FALSE,glm::value_ptr(m));}
static void uM3(GLuint p,const char*n,const glm::mat3&m){glUniformMatrix3fv(glGetUniformLocation(p,n),1,GL_FALSE,glm::value_ptr(m));}
static void uV3(GLuint p,const char*n,const glm::vec3&v){glUniform3fv(glGetUniformLocation(p,n),1,glm::value_ptr(v));}
static GLuint g_pLit=0,g_pFlat=0;
static void mainLoopBody();

int main(){
    if(!glfwInit())return 1;
#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);
    glfwWindowHint(GLFW_CLIENT_API,GLFW_OPENGL_ES_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
#endif
    glfwWindowHint(GLFW_SAMPLES,4);
    g_win=glfwCreateWindow(1280,800,"K-Line RPG",nullptr,nullptr);
#ifndef __EMSCRIPTEN__
    if(g_win)glfwMaximizeWindow(g_win);
#endif
    if(!g_win){glfwTerminate();return 1;}
    glfwMakeContextCurrent(g_win);glfwSwapInterval(1);
    glfwSetKeyCallback(g_win,onKey);glfwSetCharCallback(g_win,onChar);
    glfwSetMouseButtonCallback(g_win,onMB);glfwSetCursorPosCallback(g_win,onCursor);
    glfwSetScrollCallback(g_win,onScroll);

#ifndef __EMSCRIPTEN__
    glewExperimental=GL_TRUE;
    if(glewInit()!=GLEW_OK){std::cerr<<"GLEW init failed\n";return 1;}
#endif
    glEnable(GL_DEPTH_TEST);
#ifndef __EMSCRIPTEN__
    glEnable(GL_MULTISAMPLE);
#endif
    glClearColor(.06f,.08f,.13f,1.f);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO&io=ImGui::GetIO();io.IniFilename=nullptr;

    // ── 深色專業風格 ──
    ImGui::StyleColorsDark();
    ImGuiStyle&sty=ImGui::GetStyle();
    sty.WindowRounding    = 0.f;
    sty.FrameRounding     = 3.f;
    sty.ScrollbarRounding = 3.f;
    sty.GrabRounding      = 3.f;
    sty.TabRounding       = 3.f;
    sty.WindowBorderSize  = 0.f;
    sty.FrameBorderSize   = 1.f;
    sty.ItemSpacing       = ImVec2(8,6);
    sty.ItemInnerSpacing  = ImVec2(6,4);
    sty.FramePadding      = ImVec2(6,4);
    sty.WindowPadding     = ImVec2(10,10);
    // 色板
    auto&c=sty.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(.10f,.12f,.18f,1.f);
    c[ImGuiCol_FrameBg]          = ImVec4(.14f,.17f,.24f,1.f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(.20f,.24f,.34f,1.f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(.26f,.30f,.42f,1.f);
    c[ImGuiCol_TitleBg]          = ImVec4(.08f,.10f,.15f,1.f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(.08f,.10f,.15f,1.f);
    c[ImGuiCol_Button]           = ImVec4(.18f,.22f,.32f,1.f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(.25f,.35f,.55f,1.f);
    c[ImGuiCol_ButtonActive]     = ImVec4(.20f,.45f,.75f,1.f);
    c[ImGuiCol_Header]           = ImVec4(.22f,.30f,.48f,1.f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(.28f,.40f,.62f,1.f);
    c[ImGuiCol_SliderGrab]       = ImVec4(.30f,.55f,.90f,1.f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(.40f,.65f,1.0f,1.f);
    c[ImGuiCol_CheckMark]        = ImVec4(.40f,.75f,1.0f,1.f);
    c[ImGuiCol_Separator]        = ImVec4(.25f,.30f,.42f,1.f);
    c[ImGuiCol_Text]             = ImVec4(.90f,.92f,.95f,1.f);
    c[ImGuiCol_TextDisabled]     = ImVec4(.45f,.50f,.60f,1.f);

    // 載入含中文字集的字型（字體大一點：17px）
    {static const ImWchar ranges[]={
        0x0020,0x00FF,  // 基本拉丁 + Latin-1
        0x2010,0x205E,  // 標點
        0x4E00,0x9FAF,  // CJK 統一漢字
        0xFF00,0xFFEF,  // 全形字元
        0};
#ifndef __EMSCRIPTEN__
    bool loaded=false;
    const char* fonts[]={"C:/Windows/Fonts/msjh.ttc",
                         "C:/Windows/Fonts/msjhbd.ttc",
                         "C:/Windows/Fonts/msyh.ttc",
                         "C:/Windows/Fonts/mingliu.ttc"};
    for(auto f:fonts){
        if(GetFileAttributesA(f)!=INVALID_FILE_ATTRIBUTES){
            io.Fonts->AddFontFromFileTTF(f,15.f,nullptr,ranges);loaded=true;break;}}
    if(!loaded&&GetFileAttributesA("C:/Windows/Fonts/segoeui.ttf")!=INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",15.f);}
#else
    // Emscripten: 載入預打包的 Noto Sans TC 字型
    io.Fonts->AddFontFromFileTTF("/assets/NotoSansTC-Regular.otf",16.f,nullptr,ranges);
    }
#endif
    ImGui_ImplGlfw_InitForOpenGL(g_win,false);
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    g_pLit=mkP(VS_LIT,FS_LIT);g_pFlat=mkP(VS_FLAT,FS_FLAT);
    mLabel.init();mHoverLine.init();
    mChr0.init();mChr1.init();mChr2.init();mChr3.init();mChr4.init();
    mLeekG.init();mLeekW.init();mLeekD.init();mBoom.init();mTornado.init();
    mLobR.init();mLobD.init();mBoxB.init();mBoxD.init();mClaw.init();
    initHardcoded0050(); // 備用資料
    loadWatchlist();
    recalcLayout();
    g_tf=0;g_visible=tfMaxVis(0);

    // uM4/uM3/uV3 defined as global functions above main()

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainLoopBody,0,1);
    return 0;
} // end main() for emscripten
static void mainLoopBody(){
    ImGuiIO&io=ImGui::GetIO();
    GLuint pLit=g_pLit,pFlat=g_pFlat; (void)pLit;(void)pFlat;
#endif // __EMSCRIPTEN__
#ifndef __EMSCRIPTEN__
    while(!glfwWindowShouldClose(g_win))
#endif
    {// loop body
        glfwPollEvents();
        if(glfwGetKey(g_win,GLFW_KEY_ESCAPE)==GLFW_PRESS&&!io.WantCaptureKeyboard)
            glfwSetWindowShouldClose(g_win,true);

        // Async load completion
        if(g_loadFail.exchange(false)){
            g_failMsg=std::string(g_codeBuf)+" 查無資料";
            g_mode=MENU;} // 失敗回選單
        if(g_loadDone.exchange(false)){
            buildTFViews();g_tf=0;g_visible=tfMaxVis(0);
            g_playing=false;g_panOffset=0;g_hitBarIdx=-1,g_bearNeedsInit=true;g_mode=NORMAL;g_dirty=true;}

        double now=glfwGetTime();
        if(g_playing&&(now-g_lastStep)>=g_stepInt){
            g_lastStep=now;g_visible++;
            if(g_visible>=g_tfs[g_tf].cnt){g_visible=g_tfs[g_tf].cnt;g_playing=false;}
            g_dirty=true;}
        if(g_dirty){
            const TF&tf=g_tfs[g_tf];rebuildAll(tf,g_visible);
            float midX=(g_visible>0)?(g_visible-1)*g_sp*.5f:0.f;
            // 相機垂直中心 = 版面最上到最下的中點
            bool hasSub2=g_showMACD||g_showKD;
            float yBot=hasSub2?MACD_LO:(g_showVol?VOL_LO:WORLD_LO);
            g_cam.tgt={midX,(WORLD_HI+yBot)*0.5f,0.f};
            // r 只由水平根數決定；垂直方向靠 TOTAL 動態縮放自動填滿
            g_cam.r=std::max((float)g_visible*g_sp*.55f+10.f,18.f);
            g_dirty=false;
            // Update window title
            char t[128];snprintf(t,sizeof(t),"%s %s | %s  [%d/%d]",
                g_stockCode.c_str(),g_stockName.c_str(),
                tf.name?tf.name:"",g_visible,tf.cnt);
            glfwSetWindowTitle(g_win,t);}

        int fw,fh;glfwGetFramebufferSize(g_win,&fw,&fh);
        int vw=fw; // 全寬（右邊選單關閉）
        g_vpW=vw; g_vpH=fh;
        // 視窗尺寸改變 → 重新 layout + 相機
        if(vw!=g_prevVW||fh!=g_prevVH){g_prevVW=vw;g_prevVH=fh;g_dirty=true;}
        glViewport(0,0,vw,fh);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glm::mat4 proj=glm::perspective(glm::radians(45.f),(float)vw/std::max(1,fh),.1f,500.f);
        glm::mat4 view=g_cam.view(),MVP=proj*view,MV=view;
        // 魔法畫面震動（龍捲風期間持續）
        if(g_hitBarIdx==-2){
            double me=(glfwGetTime()-g_magicT);
            float amp=(me<2.0)?0.8f:0.8f*std::max(0.f,1.f-(float)(me-2.0)/0.3f);
            float sx=amp*sinf((float)me*45.f);
            float sy=amp*cosf((float)me*37.f);
            MVP=glm::translate(glm::mat4(1.f),glm::vec3(sx,sy,0.f))*MVP;}
        glm::mat3 NM=glm::mat3(glm::transpose(glm::inverse(MV)));
        glm::vec3 litV=glm::vec3(view*glm::vec4(g_cam.tgt+glm::vec3(8,20,12),1.f));

        glUseProgram(g_pLit);
        uM4(g_pLit,"uMVP",MVP);uM4(g_pLit,"uMV",MV);uM3(g_pLit,"uNM",NM);uV3(g_pLit,"uLit",litV);
        // 游標預設放在最新第5根K線上（只設一次）
        {static bool cursorSet=false;
         if(!cursorSet && g_mode==NORMAL && g_visible>5){
             int ci5=g_visible-5;
             float wx5=ci5*g_sp, wy5=(WORLD_HI+WORLD_LO)*0.5f;
             glm::vec4 sp5=MVP*glm::vec4(wx5,wy5,0.f,1.f);sp5/=sp5.w;
             double sx5=(sp5.x+1.0)*0.5*vw, sy5=(1.0-sp5.y)*0.5*fh;
             glfwSetCursorPos(g_win,sx5,sy5);
             cursorSet=true;}}
        uV3(g_pLit,"uCol",{.90f,.22f,.22f});mGreen.draw();
        uV3(g_pLit,"uCol",{.15f,.85f,.42f});mRed.draw();
        if(g_showVol){uV3(g_pLit,"uCol",{.75f,.20f,.20f});mVolG.draw();
                      uV3(g_pLit,"uCol",{.15f,.65f,.35f});mVolR.draw();}
        if(g_showMACD){uV3(g_pLit,"uCol",{.25f,.75f,.85f});mMHG.draw();
                       uV3(g_pLit,"uCol",{.85f,.45f,.10f});mMHR.draw();}

        // ── 被踩K棒彈跳動畫 ──
        if(g_hitBarIdx>=0&&g_hitBarIdx<g_visible&&g_tfs[g_tf].data&&mHitBar.vao){
            double elapsed=glfwGetTime()-g_hitBarStartT;
            const double HIT_DUR=1.50; // 彈跳持續秒數
            if(elapsed<HIT_DUR){
                float t=(float)(elapsed/HIT_DUR);
                // ── 形變：初始強壓扁（-0.82），接著超彈，衰減 5 次振盪 ──
                float squash=-0.82f*expf(-t*4.0f)*cosf(t*3.14159f*5.0f);
                const Candle&hc=g_tfs[g_tf].data[g_startIdx+g_hitBarIdx];
                float barH=(toW(std::max(hc.o,hc.c))-toW(std::min(hc.o,hc.c)));
                if(barH<0.04f)barH=0.04f;
                // ── 位移：下沉後強力反彈，6 次上下振盪，獨立衰減 ──
                float yOffset=-barH*0.75f*expf(-t*3.2f)*sinf(t*3.14159f*6.0f);
                const Candle&hcRef=hc;
                std::vector<float>hv;
                buildOneCandleSquash(hv,hcRef,g_hitBarIdx,squash,yOffset);
                mHitBar.upload(hv);
                // 落地瞬間強烈閃光，隨時間衰減
                float bright=1.f+0.55f*expf(-t*6.f);
                if(g_hitBarBull) uV3(g_pLit,"uCol",{.90f*bright,.22f*bright,.22f*bright});
                else             uV3(g_pLit,"uCol",{.15f,.85f*bright,.42f*bright});
                // 用 z-offset 避免 z-fighting（在原 K 棒前面一點點）
                glm::mat4 MVPoff=MVP*glm::translate(glm::mat4(1.f),glm::vec3(0,0,0.01f));
                uM4(g_pLit,"uMVP",MVPoff);
                mHitBar.draw();
                uM4(g_pLit,"uMVP",MVP); // 還原
            } else {
                g_hitBarIdx=-1; // 動畫結束
            }
        }

        // ── 拉拉熊動畫 ──
        if(g_bearActive&&g_visible>0&&g_tfs[g_tf].data){
            const Candle*bd=g_tfs[g_tf].data+g_startIdx;
            int bn=std::min(g_visible,g_tfs[g_tf].cnt-g_startIdx);
            if(bn>0){
                if(g_bearNeedsInit){
                    g_bearIdx=bn/2; g_bearPrevIdx=bn/2;
                    g_bearFromY=g_bearToY=toW(bd[bn/2].h);
                    g_bearPhase=1.0; g_bearLastTime=glfwGetTime();
                    g_bearNeedsInit=false;}
                // 推進動畫
                double bnow=glfwGetTime();
                double bdt=bnow-g_bearLastTime;
                float prevPhase=(float)g_bearPhase;
                if(g_gameMode){
                    // 遊戲模式：D/F 平移（不跳）
                    if(g_bearPhase<1.0){
                        g_bearPhase+=bdt/0.30; // 0.3秒滑到下一格
                    }
                    if(g_bearPhase>=1.0 && prevPhase<1.0){
                        g_bearPhase=1.0;
                        g_bearPrevIdx=g_bearIdx;
                        g_bearFromY=g_bearToY=toW(bd[g_bearIdx].h);}
                    // 按鍵觸發平移
                    if(g_bearPhase>=1.0 && g_gameMoveDir!=0){
                        g_bearPhase=0.0;
                        g_bearPrevIdx=g_bearIdx;
                        int nxt=g_bearIdx+g_gameMoveDir*10; // 一次走10格
                        nxt=std::max(0,std::min(bn-1,nxt));
                        g_bearIdx=nxt;
                        g_gameMoveDir=0;
                        g_bearFromY=toW(bd[g_bearPrevIdx].h);
                        g_bearToY  =toW(bd[g_bearIdx].h);}
                } else {
                    // 非遊戲模式：站在最後一根 K 棒上，不移動
                    g_bearIdx=bn-1;
                    g_bearPrevIdx=bn-1;
                }
                g_bearLastTime=bnow;
                g_bearIdx=std::min(g_bearIdx,bn-1);
                g_bearPrevIdx=std::min(g_bearPrevIdx,bn-1);
                // 計算位置 + 姿勢
                float sc=std::max(0.50f,(WORLD_HI-WORLD_LO)*0.22f); // 熊放大
                float bearX,bearY,jumpT=0.f,squashT=0.f;
                const float JUMP_END=0.32f;
                if(!g_gameMode){
                    // 非遊戲模式：安靜站立
                    bearX=(float)g_bearIdx*g_sp;
                    bearY=toW(bd[g_bearIdx].h);
                } else if(g_bearPhase>=1.0){
                    // 遊戲模式待機
                    bearX=(float)g_bearIdx*g_sp;
                    bearY=toW(bd[g_bearIdx].h);
                } else {
                    // 遊戲模式平移（smoothstep，不跳）
                    float t=(float)g_bearPhase;
                    float easeT=t*t*(3.f-2.f*t);
                    bearX=g_bearPrevIdx*g_sp+(g_bearIdx*g_sp-g_bearPrevIdx*g_sp)*easeT;
                    bearY=g_bearFromY+(g_bearToY-g_bearFromY)*easeT;
                }
                bearY+=sc*0.02f;
                // 空白鍵跳躍弧線
                float jumpElapsed=(float)(bnow-g_bearJumpT);
                float jumpDur=0.55f;
                if(jumpElapsed>=0.f && jumpElapsed<jumpDur){
                    float jt=jumpElapsed/jumpDur;
                    float arc=sinf(jt*3.14159f); // 0→1→0
                    bearY+=sc*3.5f*arc; // 跳躍高度
                    jumpT=arc; // 手臂上舉動作
                }

                // ── 遊戲模式：韭菜生成 & 移動 & 揮劍 ──────────────
                if(g_gameMode && g_mode==NORMAL){
                    // 生成韭菜（隨機 5~10 隻散佈畫面）
                    int maxLeeks=10+rand()%11; // 10~20
                    float spawnCD=std::max(0.5f,1.8f-g_killCount*0.04f);
                    int aliveCount=0;
                    for(auto&a:g_ants)if(a.alive)aliveCount++;
                    if(aliveCount<maxLeeks && bnow-g_lastAntSpawn>spawnCD){
                        // 一次補滿到 maxLeeks
                        int toSpawn=maxLeeks-aliveCount;
                        for(int si=0;si<toSpawn;si++){
                            // 隨機出現在畫面任意 K 棒位置
                            int ri=rand()%bn;
                            float ax=ri*g_sp;
                            AntMonster am;
                            am.x=ax; am.alive=true;
                            am.facingLeft=(ax>bearX);
                            am.spawnT=bnow+(si*0.15); am.deathT=-1.0; am.hp=100.f; am.lastHitT=-9.0;
                            g_ants.push_back(am);}
                        g_lastAntSpawn=bnow;}
                    // 移動韭菜（越殺越快）
                    float antSpeed=g_sp*1.0f; // 韭菜：1格/秒
                    for(auto&a:g_ants){
                        if(!a.alive)continue;
                        float dir=(bearX>a.x)?1.f:-1.f;
                        a.x+=dir*antSpeed*(float)bdt;
                        a.facingLeft=(dir<0.f);}
                    // 韭菜攻擊熊（靠近時每 0.8 秒打一下 5~10）
                    {float atkRange=g_sp*0.8f;
                     for(auto&a:g_ants){
                         if(!a.alive)continue;
                         if(std::fabs(a.x-bearX)<atkRange && (bnow-g_bearHitT)>0.8){
                             int dmg=5+rand()%6;
                             g_bearHP-=dmg;
                             g_bearHitT=bnow;sfxBearHit();
                             if(g_bearHP<=0.f){g_bearHP=0.f;if(!g_bearDead){g_bearDead=true;g_bearDeadT=glfwGetTime();}}
                             break;}}}
                    // 揮鐮刀偵測（滑鼠左鍵觸發，見 onMB）
                    // 鐮刀傷害（命中期：slashT 0.28~0.72，每次揮砍只打一下）
                    float slashElapsed=(float)(bnow-g_slashStartT);
                    float slashT2=(slashElapsed<0.f)?0.f:std::min(1.f,slashElapsed/0.45f);
                    if(slashT2>0.28f&&slashT2<0.72f){
                        float hitRange=g_sp*10.0f; // 打 10 根 K 棒遠
                        for(auto&a:g_ants){
                            if(!a.alive)continue;
                            if(std::fabs(a.x-bearX)<hitRange && (bnow-a.lastHitT)>0.35){
                                int dmg=60+rand()%31; // 鐮刀 60~90（均值75）
                                a.hp-=dmg;
                                a.lastHitT=bnow;
                                if(a.hp<=0.f){
                                    a.alive=false;a.deathT=bnow;g_killCount++;g_bearMP=std::min(500.f,g_bearMP+10.f);sfxKill();
                                } else {sfxHit();}
                            }}}
                    // 清除已消散（死亡 >0.8 秒）
                    g_ants.erase(std::remove_if(g_ants.begin(),g_ants.end(),
                        [&](const AntMonster&a){return !a.alive&&(bnow-a.deathT)>0.8;}),
                        g_ants.end());

                    // ── 渲染韭菜 + 爆炸粒子 + 血條投影 ─────────────────
                    struct LeekHPBar{float sx,sy,hpFrac;};
                    std::vector<LeekHPBar>hpBars;
                    std::vector<float>lkG,lkW,lkD,boomV;
                    for(auto&a:g_ants){
                        int bi=glm::clamp((int)std::round(a.x/g_sp),0,bn-1);
                        float leekY=toW(bd[bi].h)+sc*0.02f;
                        if(a.alive){
                            buildLeek(lkG,lkW,lkD,a.x,leekY,sc,a.spawnT,1.f,true);
                            // 投影到螢幕（血條用）
                            float topY=leekY+sc*1.2f;
                            glm::vec4 sp2=MVP*glm::vec4(a.x,topY,0.f,1.f);sp2/=sp2.w;
                            hpBars.push_back({(sp2.x+1.f)*0.5f*(float)vw,
                                              (1.f-sp2.y)*0.5f*(float)fh,
                                              a.hp/100.f});
                        } else {
                            // 爆炸粒子：綠色碎片向四面八方飛散
                            float dt2=(float)(bnow-a.deathT);
                            float alpha=std::max(0.f,1.f-dt2/0.8f);
                            float expand=dt2*sc*6.5f; // 擴散速度
                            float gravity=dt2*dt2*sc*3.2f; // 下墜
                            float pSz=sc*0.06f*alpha;  // 粒子大小隨時間縮小
                            if(pSz>1e-4f){
                                // 12 個碎片向外噴射
                                for(int pi=0;pi<12;pi++){
                                    float ang=pi*0.5236f+dt2*2.f; // 0.5236=30°
                                    float px=a.x+cosf(ang)*expand*(0.6f+0.4f*sinf(pi*1.7f));
                                    float py=leekY+sc*0.4f+sinf(ang)*expand*0.7f-gravity;
                                    float pz=sinf(ang+1.f)*expand*0.3f;
                                    pushBox(boomV,px-pSz,px+pSz,py-pSz,py+pSz,pz-pSz,pz+pSz);}}}}
                    mLeekG.upload(lkG);mLeekW.upload(lkW);mLeekD.upload(lkD);
                    uV3(g_pLit,"uCol",{.22f,.62f,.18f});mLeekG.draw(); // 深綠
                    uV3(g_pLit,"uCol",{.92f,.90f,.84f});mLeekW.draw(); // 白/奶
                    uV3(g_pLit,"uCol",{.12f,.10f,.08f});mLeekD.draw(); // 深色(眼/嘴)
                    // 爆炸粒子（亮綠色閃光）
                    if(!boomV.empty()){
                        mBoom.upload(boomV);
                        uV3(g_pLit,"uCol",{.45f,.95f,.25f});mBoom.draw();}
                    // ── 鈔票投射魔法（丟一捆鈔票，面向方向飛出，打5隻）──
                    // 修正投射 Y（用當前 bearY）
                    if(bnow-g_cashProjT<0.05 && g_cashProjY==0.f)
                        g_cashProjY=bearY;
                    {double projE=bnow-g_cashProjT;
                     float projDur=1.0f; // 飛行 1 秒
                     if(projE>=0.0 && projE<projDur){
                         float pt2=(float)(projE/projDur);
                         // 飛向最遠目標（smoothstep）
                         float easeP=pt2*pt2*(3.f-2.f*pt2);
                         float projX=g_cashProjX+(g_cashProjEndX-g_cashProjX)*easeP;
                         float projY=g_cashProjY+sc*0.5f+sinf(pt2*6.28f)*sc*0.3f; // 微微上下飄
                         float pAlpha=(pt2<0.1f)?(pt2/0.1f):(pt2>0.8f)?(1.f-(pt2-0.8f)/0.2f):1.f;

                         // 碰撞檢測：打面向方向最近 5 隻怪物（只在剛發射時判定一次）
                         if(projE<0.05){
                             int hits=0;
                             g_cashProjEndX=g_cashProjX+(float)g_cashProjDir*g_sp*8.f; // 預設飛 8 格
                             // 打韭菜
                             for(auto&a:g_ants){
                                 if(!a.alive||hits>=5)continue;
                                 float dx5=a.x-g_cashProjX;
                                 if(g_cashProjDir>0?dx5>0:dx5<0){
                                     int dmg=25+rand()%21; // 魔法 25~45（均值35）
                                     a.hp-=dmg;a.lastHitT=bnow;
                                     if(a.hp<=0.f){a.alive=false;a.deathT=bnow;g_killCount++;g_bearMP=std::min(500.f,g_bearMP+10.f);sfxKill();}
                                     else sfxHit();
                                     // 記錄最遠目標
                                     if(g_cashProjDir>0?a.x>g_cashProjEndX:a.x<g_cashProjEndX)
                                         g_cashProjEndX=a.x;
                                     hits++;}}
                             // 打龍蝦
                             for(auto&lb:g_lobs){
                                 if(!lb.alive||hits>=5)continue;
                                 float dx6=lb.x-g_cashProjX;
                                 if(g_cashProjDir>0?dx6>0:dx6<0){
                                     int dmg=25+rand()%21; // 魔法 25~45（均值35）
                                     lb.hp-=dmg;lb.lastHitT=bnow;
                                     if(lb.hp<=0.f){lb.alive=false;lb.deathT=bnow;g_killCount++;g_bearMP=std::min(500.f,g_bearMP+10.f);sfxKill();}
                                     if(g_cashProjDir>0?lb.x>g_cashProjEndX:lb.x<g_cashProjEndX)
                                         g_cashProjEndX=lb.x;
                                     hits++;}}}

                         // 渲染飛行鈔票捆（大捆 + 散落小鈔）
                         std::vector<float>torV,torV2;
                         // 主體鈔票捆（大型長方體，綠色）
                         float bW=sc*0.45f*pAlpha, bH=sc*0.25f*pAlpha, bD=sc*0.15f*pAlpha;
                         float rot2=(float)projE*12.f; // 旋轉
                         float rW=bW*std::fabs(cosf(rot2))+bD*std::fabs(sinf(rot2));
                         pushBox(torV2,projX-rW,projX+rW,projY-bH,projY+bH,-bD,bD);
                         // 金色綁帶（十字）
                         float band=sc*0.035f*pAlpha;
                         pushBox(torV,projX-rW*1.02f,projX+rW*1.02f,projY-band,projY+band,-bD*1.1f,bD*1.1f);
                         pushBox(torV,projX-band,projX+band,projY-bH*1.02f,projY+bH*1.02f,-bD*1.1f,bD*1.1f);
                         // $ 符號（鈔票捆正面）
                         float sz=sc*0.04f*pAlpha;
                         pushBox(torV,projX-sz*2.f,projX+sz*2.f,projY+sz,projY+sz*2.5f,bD,bD+sz*0.5f);
                         pushBox(torV,projX-sz*2.f,projX+sz*2.f,projY-sz*2.5f,projY-sz,bD,bD+sz*0.5f);
                         pushBox(torV,projX-sz*2.f,projX+sz*2.f,projY-sz*0.5f,projY+sz*0.5f,bD,bD+sz*0.5f);
                         pushBox(torV,projX-sz*0.3f,projX+sz*0.3f,projY-sz*3.f,projY+sz*3.f,bD,bD+sz*0.5f);
                         // 散落小鈔票（拖尾）
                         for(int ti=0;ti<8;ti++){
                             float tSeed=(float)(ti*73+11);
                             float tDelay=ti*0.06f;
                             if(projE<tDelay)continue;
                             float tLife=(float)(projE-tDelay);
                             float tAlpha=std::max(0.f,1.f-tLife*2.f)*pAlpha;
                             float tpx=projX-(float)g_cashProjDir*g_sp*tLife*6.f+sinf(tSeed)*sc*0.3f;
                             float tpy=projY+cosf(tSeed*0.7f)*sc*0.4f-tLife*sc*0.5f;
                             float tsz=sc*0.12f*tAlpha;
                             pushBox(torV2,tpx-tsz*1.5f,tpx+tsz*1.5f,tpy-tsz,tpy+tsz,-tsz*0.1f,tsz*0.1f);}

                         // 畫鈔票底色
                         mBoom.upload(torV2);
                         uV3(g_pLit,"uCol",{.35f*pAlpha,.68f*pAlpha,.30f*pAlpha});
                         mBoom.draw();
                         // 畫金色部分
                         mTornado.upload(torV);
                         uV3(g_pLit,"uCol",{.95f*pAlpha,.82f*pAlpha,.18f*pAlpha});
                         mTornado.draw();
                     }}
                    // ── 龍蝦生成 & 飛行 & 傷害 ────────────────────────
                    {int lobAlive=0;
                     for(auto&lb:g_lobs)if(lb.alive)lobAlive++;
                     int maxLob=2+rand()%5; // 2~6
                     if(lobAlive<maxLob && bnow-g_lastLobSpawn>3.5){
                         int toSp=maxLob-lobAlive;
                         for(int si=0;si<toSp;si++){
                             Lobster lb;
                             lb.x=(rand()%bn)*g_sp;
                             // 在 K 棒上方飛行
                             int lbi2=rand()%bn;
                             lb.y=toW(bd[lbi2].h)+sc*(0.8f+0.6f*(rand()%100)/100.f);
                             lb.alive=true; lb.facingLeft=(rand()%2==0);
                             lb.spawnT=bnow; lb.deathT=-1.0; lb.hp=300.f; lb.lastHitT=-9.0;
                             lb.flySpeed=g_sp*3.0f; // 龍蝦：3格/秒
                             lb.flyAng=(rand()%628)/100.f; // 0~2PI
                             g_lobs.push_back(lb);}
                         g_lastLobSpawn=bnow;}
                     // 飛行移動（隨機方向漂移 + 偶爾衝向熊）
                     for(auto&lb:g_lobs){
                         if(!lb.alive)continue;
                         // 偶爾改變方向朝向熊
                         if(rand()%60==0){
                             float dx2=bearX-lb.x, dy2=(bearY+sc*0.5f)-lb.y;
                             lb.flyAng=atan2f(dy2,dx2);}
                         lb.x+=cosf(lb.flyAng)*lb.flySpeed*(float)bdt;
                         lb.y+=sinf(lb.flyAng)*lb.flySpeed*(float)bdt*0.5f;
                         // 限制在畫面範圍內
                         lb.x=glm::clamp(lb.x,0.f,(bn-1)*g_sp);
                         // 保持在 K 棒上方飛行
                         int lbi3=glm::clamp((int)std::round(lb.x/g_sp),0,bn-1);
                         float minFlyY=toW(bd[lbi3].h)+sc*0.5f;
                         lb.y=glm::clamp(lb.y,minFlyY,WORLD_HI+sc*2.f);
                         lb.facingLeft=(cosf(lb.flyAng)<0.f);}
                     // 鐮刀打龍蝦（跳起來可以打到）
                     if(slashT2>0.28f&&slashT2<0.72f){
                         float hitR=g_sp*10.0f; // 打 10 根 K 棒遠
                         for(auto&lb:g_lobs){
                             if(!lb.alive)continue;
                             float dx3=lb.x-bearX, dy3=lb.y-(bearY+sc*0.5f);
                             if(sqrtf(dx3*dx3+dy3*dy3)<hitR && (bnow-lb.lastHitT)>0.35){
                                 int dmg=60+rand()%31; // 鐮刀 60~90
                                 lb.hp-=dmg; lb.lastHitT=bnow;
                                 if(lb.hp<=0.f){lb.alive=false;lb.deathT=bnow;g_killCount++;}}}}
                     // 龍蝦遠距丟螯（每 2 秒丟一次）
                     for(auto&lb:g_lobs){
                         if(!lb.alive)continue;
                         if((bnow-lb.lastHitT)>2.0){ // 借用 lastHitT 做丟螯冷卻
                             lb.lastHitT=bnow;
                             float dx4=bearX-lb.x, dy4=(bearY+sc*0.5f)-lb.y;
                             float dist=sqrtf(dx4*dx4+dy4*dy4);
                             if(dist>0.1f){
                                 float spd=g_sp*40.f; // 螯飛行速度（100格/2.5秒）
                                 ClawProj cp;
                                 cp.x=lb.x;cp.y=lb.y;
                                 cp.ox=lb.x;cp.oy=lb.y;
                                 cp.tx=bearX;cp.ty=bearY+sc*0.5f;
                                 cp.vx=dx4/dist*spd;cp.vy=dy4/dist*spd;
                                 cp.spawnT=bnow;cp.alive=true;cp.returning=false;
                                 g_claws.push_back(cp);}}}
                     // 螯投射物移動（迴力鏢：去程→回程）
                     for(auto&cp:g_claws){
                         if(!cp.alive)continue;
                         float elapsed=(float)(bnow-cp.spawnT);
                         float halfTime=1.2f; // 去程 1.2 秒
                         if(!cp.returning){
                             // 去程：直線飛向目標
                             cp.x+=cp.vx*(float)bdt;
                             cp.y+=cp.vy*(float)bdt;
                             // 到達目標附近或超時 → 轉回程
                             float dx8=cp.x-cp.tx, dy8=cp.y-cp.ty;
                             if(sqrtf(dx8*dx8+dy8*dy8)<g_sp*1.5f || elapsed>halfTime)
                                 cp.returning=true;
                         } else {
                             // 回程：飛回發射源
                             float dx9=cp.ox-cp.x, dy9=cp.oy-cp.y;
                             float d9=sqrtf(dx9*dx9+dy9*dy9);
                             float retSpd=g_sp*35.f;
                             if(d9>g_sp*0.5f){
                                 cp.x+=dx9/d9*retSpd*(float)bdt;
                                 cp.y+=dy9/d9*retSpd*(float)bdt;
                             } else { cp.alive=false; }} // 回到龍蝦身邊消失
                         // 去程碰撞熊
                         if(!cp.returning){
                             float cdx=cp.x-bearX, cdy=cp.y-(bearY+sc*0.5f);
                             if(sqrtf(cdx*cdx+cdy*cdy)<g_sp*0.8f){
                                 if((bnow-g_bearHitT)>0.3){
                                     g_bearHP-=(5+rand()%11);
                                     g_bearHitT=bnow;sfxBearHit();
                                     if(g_bearHP<=0.f){g_bearHP=0.f;if(!g_bearDead){g_bearDead=true;g_bearDeadT=glfwGetTime();}}}}}
                         // 回程也能碰撞熊
                         if(cp.returning){
                             float cdx2=cp.x-bearX, cdy2=cp.y-(bearY+sc*0.5f);
                             if(sqrtf(cdx2*cdx2+cdy2*cdy2)<g_sp*0.8f){
                                 if((bnow-g_bearHitT)>0.3){
                                     g_bearHP-=(3+rand()%6); // 回程傷害較低
                                     g_bearHitT=bnow;sfxBearHit();
                                     if(g_bearHP<=0.f){g_bearHP=0.f;if(!g_bearDead){g_bearDead=true;g_bearDeadT=glfwGetTime();}}}}}
                         if(elapsed>5.f)cp.alive=false;} // 安全超時
                     g_claws.erase(std::remove_if(g_claws.begin(),g_claws.end(),
                         [](const ClawProj&c){return !c.alive;}),g_claws.end());
                     // 渲染螯投射物（紅色旋轉鉗子）
                     {std::vector<float>clawV;
                      for(auto&cp:g_claws){
                          float ct2=(float)(bnow-cp.spawnT);
                          float rot=ct2*12.f; // 快速旋轉
                          float csz=sc*0.08f;
                          float cx2=cp.x, cy2=cp.y;
                          // 鉗子形狀（V字 旋轉）
                          float c2=cosf(rot), s2=sinf(rot);
                          float pLen=csz*1.5f;
                          // 上片
                          float ux=cx2+c2*pLen, uy=cy2+s2*pLen;
                          pushBox(clawV,std::min(cx2,ux)-csz*0.3f,std::max(cx2,ux)+csz*0.3f,
                                       std::min(cy2,uy)-csz*0.3f,std::max(cy2,uy)+csz*0.3f,
                                       -csz*0.2f,csz*0.2f);
                          // 下片
                          float lx=cx2-s2*pLen, ly=cy2+c2*pLen;
                          pushBox(clawV,std::min(cx2,lx)-csz*0.3f,std::max(cx2,lx)+csz*0.3f,
                                       std::min(cy2,ly)-csz*0.3f,std::max(cy2,ly)+csz*0.3f,
                                       -csz*0.2f,csz*0.2f);}
                      if(!clawV.empty()){
                          mClaw.upload(clawV);
                          uV3(g_pLit,"uCol",{.85f,.15f,.10f});mClaw.draw();}}
                     // 右鍵魔法也殺龍蝦
                     if(g_hitBarIdx==-2){
                         double me2=bnow-g_hitBarStartT;
                         if(me2<0.1){for(auto&lb:g_lobs){
                             if(lb.alive){lb.alive=false;lb.deathT=bnow;g_killCount++;}}}}
                     // 清除
                     g_lobs.erase(std::remove_if(g_lobs.begin(),g_lobs.end(),
                         [&](const Lobster&lb){return !lb.alive&&(bnow-lb.deathT)>0.8;}),
                         g_lobs.end());
                     // 渲染龍蝦
                     std::vector<float>lobR,lobD;
                     for(auto&lb:g_lobs){
                         float lscl=lb.alive?1.f:std::max(0.f,1.f-(float)((bnow-lb.deathT)/0.5));
                         buildLobster(lobR,lobD,lb.x,lb.y,sc,lb.facingLeft,lb.spawnT,lscl);
                         // 龍蝦血條
                         if(lb.alive){
                             float topY2=lb.y+sc*0.6f;
                             glm::vec4 sp3=MVP*glm::vec4(lb.x,topY2,0.f,1.f);sp3/=sp3.w;
                             hpBars.push_back({(sp3.x+1.f)*0.5f*(float)vw,
                                               (1.f-sp3.y)*0.5f*(float)fh,
                                               lb.hp/300.f});}}
                     mLobR.upload(lobR);mLobD.upload(lobD);
                     uV3(g_pLit,"uCol",{.85f,.18f,.12f});mLobR.draw(); // 紅色龍蝦
                     uV3(g_pLit,"uCol",{.10f,.08f,.06f});mLobD.draw();} // 深色(眼/觸角)

                    // ── 紙箱怪物生成 & 移動 & 傷害 ──────────────────
                    {int boxAlive=0;
                     for(auto&bm:g_boxes)if(bm.alive)boxAlive++;
                     if(boxAlive<4 && bnow-g_lastBoxSpawn>3.0){ // 最多4隻，每3秒
                         BoxMon bm;
                         bm.x=(rand()%bn)*g_sp;
                         bm.alive=true;bm.spawnT=bnow;bm.deathT=-1.0;bm.hp=200.f;bm.lastHitT=-9.0;
                         g_boxes.push_back(bm);
                         g_lastBoxSpawn=bnow;}
                     // 移動（緩慢向熊靠近）
                     float boxSpd=g_sp*2.0f; // 紙箱：2格/秒
                     for(auto&bm:g_boxes){
                         if(!bm.alive)continue;
                         float dir2=(bearX>bm.x)?1.f:-1.f;
                         bm.x+=dir2*boxSpd*(float)bdt;}
                     // 紙箱攻擊熊（靠近）
                     for(auto&bm:g_boxes){
                         if(!bm.alive)continue;
                         if(std::fabs(bm.x-bearX)<g_sp*0.8f && (bnow-g_bearHitT)>0.8){
                             g_bearHP-=(8+rand()%8); // 8~15
                             g_bearHitT=bnow;sfxBearHit();
                             if(g_bearHP<=0.f){g_bearHP=0.f;if(!g_bearDead){g_bearDead=true;g_bearDeadT=glfwGetTime();}}break;}}
                     // 鐮刀打紙箱
                     if(slashT2>0.28f&&slashT2<0.72f){
                         float hitR2=g_sp*10.0f; // 打 10 根 K 棒遠
                         for(auto&bm:g_boxes){
                             if(!bm.alive)continue;
                             if(std::fabs(bm.x-bearX)<hitR2 && (bnow-bm.lastHitT)>0.35){
                                 int dmg=60+rand()%31; // 鐮刀 60~90
                                 bm.hp-=dmg;bm.lastHitT=bnow;
                                 if(bm.hp<=0.f){bm.alive=false;bm.deathT=bnow;g_killCount++;g_bearMP=std::min(500.f,g_bearMP+10.f);sfxKill();}
                                 else sfxHit();}}}
                     // 鈔票魔法打紙箱
                     double projE2=bnow-g_cashProjT;
                     if(projE2>=0.0&&projE2<0.05){
                         for(auto&bm:g_boxes){
                             if(!bm.alive)continue;
                             float dx7=bm.x-g_cashProjX;
                             if(g_cashProjDir>0?dx7>0:dx7<0){
                                 int dmg=25+rand()%21; // 魔法 25~45（均值35）
                                 bm.hp-=dmg;bm.lastHitT=bnow;
                                 if(bm.hp<=0.f){bm.alive=false;bm.deathT=bnow;g_killCount++;g_bearMP=std::min(500.f,g_bearMP+10.f);sfxKill();}}}}
                     // 清除
                     g_boxes.erase(std::remove_if(g_boxes.begin(),g_boxes.end(),
                         [&](const BoxMon&bm){return !bm.alive&&(bnow-bm.deathT)>0.8;}),
                         g_boxes.end());
                     // 渲染紙箱
                     std::vector<float>boxB,boxDk;
                     for(auto&bm:g_boxes){
                         int bi3=glm::clamp((int)std::round(bm.x/g_sp),0,bn-1);
                         float boxY=toW(bd[bi3].h)+sc*0.02f;
                         float bscl=bm.alive?1.f:std::max(0.f,1.f-(float)((bnow-bm.deathT)/0.5));
                         buildBoxMonster(boxB,boxDk,bm.x,boxY,sc,bm.spawnT,bscl);
                         if(bm.alive){
                             float topY3=boxY+sc*1.0f;
                             glm::vec4 sp6=MVP*glm::vec4(bm.x,topY3,0.f,1.f);sp6/=sp6.w;
                             hpBars.push_back({(sp6.x+1.f)*0.5f*(float)vw,
                                               (1.f-sp6.y)*0.5f*(float)fh,
                                               bm.hp/200.f});}}
                     mBoxB.upload(boxB);mBoxD.upload(boxDk);
                     uV3(g_pLit,"uCol",{.72f,.58f,.38f});mBoxB.draw(); // 紙箱色
                     uV3(g_pLit,"uCol",{.18f,.14f,.10f});mBoxD.draw();} // 深色

                    // 存血條資料供 ImGui 繪製
                    g_hpBars.clear();
                    for(auto&h:hpBars)g_hpBars.push_back({h.sx,h.sy,h.hpFrac});

                    // 計算揮劍 slashT
                    float slashT3=(slashElapsed<0.f)?0.f:std::min(1.f,slashElapsed/0.45f);

                    // ── 渲染拉拉熊 ──────────────────────────────
                    std::vector<float>c0,c1,c2,c3,c4;
                    if(g_bearDead){
                        // 死亡：跪地哭泣（壓扁 + 哭臉 + 微抖）
                        float deadT2=(float)(bnow-g_bearDeadT);
                        float sink=std::min(deadT2*2.f,1.f); // 0→1 下沉
                        float tremble=sinf(deadT2*18.f)*0.015f*sc*(1.f-sink*0.5f); // 顫抖
                        buildDonChan(c0,c1,c2,c3,c4,
                            bearX+tremble, bearY-sink*sc*0.25f, sc*0.85f,
                            0.f, false, 0.55f*sink, 0.f, -1); // 朝左45度跪著
                    } else {
                        // 被打後 0.8 秒哭臉，正常笑臉
                        bool marioHappy2=(bnow-g_bearHitT>0.8);
                        float actionSquash=squashT;
                        float actionX=bearX;
                        float actionY=bearY;

                        // 揮刀動作：身體前傾 + 微蹲
                        if(slashT3>0.f && slashT3<1.f){
                            float swingT=sinf(slashT3*3.14159f); // 0→1→0
                            actionSquash+=swingT*0.18f;  // 微壓扁（蹲下揮刀）
                            actionX+=g_bearFaceDir*sc*0.06f*swingT; // 身體向前傾
                            actionY-=sc*0.03f*swingT;    // 微下沉
                        }
                        // 被打動作：身體後仰 + 彈跳
                        float hitElapsed=(float)(bnow-g_bearHitT);
                        if(hitElapsed>=0.f && hitElapsed<0.5f){
                            float hitT=hitElapsed/0.5f;
                            float recoil=sinf(hitT*3.14159f); // 0→1→0
                            actionSquash-=recoil*0.20f; // 拉長（後仰）
                            actionX-=g_bearFaceDir*sc*0.08f*recoil; // 後退
                            actionY+=sc*0.05f*recoil; // 微跳起
                        }

                        buildDonChan(c0,c1,c2,c3,c4,actionX,actionY,sc,jumpT,marioHappy2,actionSquash,slashT3,g_bearFaceDir);
                    }
                    mChr0.upload(c0);mChr1.upload(c1);mChr2.upload(c2);mChr3.upload(c3);mChr4.upload(c4);
                    uV3(g_pLit,"uCol",{.74f,.54f,.34f});mChr0.draw();
                    uV3(g_pLit,"uCol",{.91f,.82f,.66f});mChr1.draw();
                    uV3(g_pLit,"uCol",{.97f,.94f,.88f});mChr2.draw();
                    uV3(g_pLit,"uCol",{.16f,.10f,.07f});mChr3.draw();
                    if(!g_bearDead) uV3(g_pLit,"uCol",{.82f,.84f,.88f});mChr4.draw();
                } else {
                    // ── 非遊戲模式：正常拉拉熊 ────────────────────
                    bool marioHappy=(bd[g_bearIdx].c >= bd[g_bearIdx].o);
                    std::vector<float>c0,c1,c2,c3,c4;
                    buildDonChan(c0,c1,c2,c3,c4,bearX,bearY,sc,jumpT,marioHappy,squashT,0.f);
                    mChr0.upload(c0);mChr1.upload(c1);mChr2.upload(c2);mChr3.upload(c3);
                    uV3(g_pLit,"uCol",{.74f,.54f,.34f});mChr0.draw();
                    uV3(g_pLit,"uCol",{.91f,.82f,.66f});mChr1.draw();
                    uV3(g_pLit,"uCol",{.97f,.94f,.88f});mChr2.draw();
                    uV3(g_pLit,"uCol",{.16f,.10f,.07f});mChr3.draw();
                }

                // 投影到螢幕（報價泡泡用）
                float bearTopY=bearY+sc*1.55f; // 大頭
                {glm::vec4 sp=MVP*glm::vec4(bearX,bearTopY,0.f,1.f);sp/=sp.w;
                 g_bearScreenX=(sp.x+1.f)*0.5f*(float)vw;
                 g_bearScreenY=(1.f-sp.y)*0.5f*(float)fh;}
            } else { g_bearScreenX=-1.f; }}

        glUseProgram(g_pFlat);uM4(g_pFlat,"uMVP",MVP);
        uV3(g_pFlat,"uCol",{.18f,.22f,.30f});mGrid.draw(GL_LINES);
        if(g_showMA[0]){uV3(g_pFlat,"uCol",{.95f,.80f,.20f});mMA5.draw(GL_LINES);}
        if(g_showMA[1]){uV3(g_pFlat,"uCol",{.20f,.85f,.90f});mMA20.draw(GL_LINES);}
        if(g_showMA[2]){uV3(g_pFlat,"uCol",{.90f,.30f,.75f});mMA60.draw(GL_LINES);}
        if(g_showMA[3]){uV3(g_pFlat,"uCol",{.95f,.55f,.10f});mMA120.draw(GL_LINES);}
        if(g_showMA[4]){uV3(g_pFlat,"uCol",{.70f,.50f,.90f});mMA240.draw(GL_LINES);}
        if(g_showMACD){uV3(g_pFlat,"uCol",{.90f,.92f,.95f});mDIF.draw(GL_LINES);
                       uV3(g_pFlat,"uCol",{.95f,.60f,.20f});mDEA.draw(GL_LINES);}
        if(g_showKD){uV3(g_pFlat,"uCol",{.95f,.85f,.25f});mKK.draw(GL_LINES);   // K 黃線
                     uV3(g_pFlat,"uCol",{.35f,.85f,.95f});mKDLine.draw(GL_LINES);} // D 藍線
        if(g_showRSI){uV3(g_pFlat,"uCol",{.75f,.50f,.90f});mRSI.draw(GL_LINES);}  // RSI 紫線
        if(g_showBB){
            uV3(g_pFlat,"uCol",{.90f,.75f,.20f});mBBUpper.draw(GL_LINES);  // 上軌 金
            uV3(g_pFlat,"uCol",{.90f,.75f,.20f});mBBLower.draw(GL_LINES);  // 下軌 金
            uV3(g_pFlat,"uCol",{.50f,.70f,.90f});mBBMid.draw(GL_LINES);}   // 中軌 藍
        {std::vector<float>buf;buildLabels(buf,g_tfs[g_tf],g_visible,g_startIdx,view);mLabel.upload(buf);}
        uV3(g_pFlat,"uCol",{.85f,.88f,.92f});mLabel.draw();
        // 十字準線（垂直 + 水平）
        if(g_hoveredCandle>=0){
            float cx=g_hoveredCandle*g_sp;
            bool hs2=g_showMACD||g_showKD||g_showRSI;
            float hvBot=hs2?MACD_LO:(g_showVol?VOL_LO:WORLD_LO);
            float x0=-g_sp, x1=(float)g_visible*g_sp;
            std::vector<float>hv={
                cx,hvBot,0.f,cx,WORLD_HI+.4f,0.f,      // 垂直線
                x0,g_hoverWY,0.f,x1,g_hoverWY,0.f};    // 水平線
            mHoverLine.upload(hv);
            uV3(g_pFlat,"uCol",{.75f,.75f,.75f});mHoverLine.draw();}

        // ImGui (full framebuffer viewport)
        glViewport(0,0,fw,fh);
        ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
        if(g_mode==MENU||g_mode==LOADING){
            drawStockMenu(fw,fh);
        } else {
        // Update hover AFTER NewFrame so ImGui mouse state is ready
        double mx,my;glfwGetCursorPos(g_win,&mx,&my);
        updateHover(mx,my,vw,fh,MVP);
        //drawPanel(fw,fh); // 右邊選單暫時關閉
        drawHoverTooltip();
        drawCrosshairOverlay(vw,fh,MVP);
        drawBearPriceBubble(vw,fh);
        // ── 魔法時熊說「本多終勝」（泡泡框）──
        if(g_gameMode&&g_bearScreenX>0.f){
            double magE2=glfwGetTime()-g_magicT;
            if(magE2>=0.0 && magE2<2.0){
                float ma2=(float)magE2;
                float alpha3=(ma2<0.15f)?(ma2/0.15f):(ma2>1.5f)?(1.f-(ma2-1.5f)/0.5f):1.f;
                alpha3=glm::clamp(alpha3,0.f,1.f);
                ImDrawList*dl=ImGui::GetForegroundDrawList();
                ImFont*font=ImGui::GetFont();
                float fs=ImGui::GetFontSize()*2.8f;
                const char*txt="\xe6\x9c\xac\xe5\xa4\x9a\xe7\xb5\x82\xe5\x8b\x9d"; // 本多終勝
                ImVec2 tsz=font->CalcTextSizeA(fs,FLT_MAX,0.f,txt);
                float pad=12.f;
                float tx6=g_bearScreenX-tsz.x*0.5f;
                float ty6=g_bearScreenY-120.f;
                // 泡泡框
                dl->AddRectFilled(ImVec2(tx6-pad,ty6-pad),ImVec2(tx6+tsz.x+pad,ty6+tsz.y+pad),
                    IM_COL32(255,255,255,(int)(230*alpha3)),12.f);
                dl->AddRect(ImVec2(tx6-pad,ty6-pad),ImVec2(tx6+tsz.x+pad,ty6+tsz.y+pad),
                    IM_COL32(220,180,30,(int)(220*alpha3)),12.f,0,2.5f);
                // 小三角指向熊
                float triX2=g_bearScreenX;
                float triY2=ty6+tsz.y+pad;
                dl->AddTriangleFilled(ImVec2(triX2-8,triY2),ImVec2(triX2+8,triY2),
                    ImVec2(triX2,triY2+12),IM_COL32(255,255,255,(int)(230*alpha3)));
                // 金色文字
                dl->AddText(font,fs,ImVec2(tx6,ty6),IM_COL32(220,170,20,(int)(255*alpha3)),txt);}}
        // ── 「割韭菜！」浮動文字 ──
        if(g_gameMode&&g_bearScreenX>0.f){
            double stE=glfwGetTime()-g_slashTextT;
            if(stE>=0.0&&stE<1.0){
                float st2=(float)stE;
                float alpha4=(st2<0.2f)?(st2/0.2f):(1.f-(st2-0.2f)/0.8f);
                alpha4=glm::clamp(alpha4,0.f,1.f);
                float rise=st2*30.f; // 向上飄
                ImDrawList*dl=ImGui::GetForegroundDrawList();
                ImFont*font=ImGui::GetFont();
                float fs=ImGui::GetFontSize()*2.2f;
                const char*txt="\xe5\x89\xb2\xe9\x9f\xad\xe8\x8f\x9c\xef\xbc\x81"; // 割韭菜！
                ImVec2 tsz=font->CalcTextSizeA(fs,FLT_MAX,0.f,txt);
                float tx4=g_bearScreenX-tsz.x*0.5f;
                float ty4=g_bearScreenY-80.f-rise;
                // 泡泡框
                float pad=8.f;
                ImVec2 tsz2=font->CalcTextSizeA(fs,FLT_MAX,0.f,txt);
                float bx4=tx4-pad, by4=ty4-pad;
                float bx4r=tx4+tsz2.x+pad, by4b=ty4+tsz2.y+pad;
                dl->AddRectFilled(ImVec2(bx4,by4),ImVec2(bx4r,by4b),IM_COL32(255,255,255,(int)(220*alpha4)),10.f);
                dl->AddRect(ImVec2(bx4,by4),ImVec2(bx4r,by4b),IM_COL32(60,60,60,(int)(200*alpha4)),10.f,0,2.f);
                // 小三角（指向熊）
                float triX=g_bearScreenX, triY=by4b;
                dl->AddTriangleFilled(ImVec2(triX-6,triY),ImVec2(triX+6,triY),
                    ImVec2(triX,triY+10),IM_COL32(255,255,255,(int)(220*alpha4)));
                // 文字
                dl->AddText(font,fs,ImVec2(tx4,ty4),IM_COL32(255,60,30,(int)(255*alpha4)),txt);}}
        // ── 魔法鈔票「$1000」文字 ──
        if(g_gameMode){
            double pjE=glfwGetTime()-g_cashProjT;
            if(pjE>=0.0&&pjE<1.0){
                float pjt=(float)pjE;
                float projSpeed=g_sp*18.f;
                float pjX=g_cashProjX+(float)g_cashProjDir*projSpeed*pjt;
                float sc5=std::max(0.50f,(WORLD_HI-WORLD_LO)*0.22f);
                float pjY=g_cashProjY+sc5*0.5f+sinf(pjt*6.28f)*sc5*0.3f;
                glm::vec4 sp5=MVP*glm::vec4(pjX,pjY+sc5*0.4f,0.f,1.f);sp5/=sp5.w;
                float sx5=(sp5.x+1.f)*0.5f*(float)vw;
                float sy5=(1.f-sp5.y)*0.5f*(float)fh;
                float pAlpha2=(pjt<0.1f)?(pjt/0.1f):(pjt>0.7f)?(1.f-(pjt-0.7f)/0.3f):1.f;
                ImDrawList*dl=ImGui::GetForegroundDrawList();
                ImFont*font=ImGui::GetFont();
                float fs=ImGui::GetFontSize()*2.5f;
                const char*txt="$1000";
                ImVec2 tsz=font->CalcTextSizeA(fs,FLT_MAX,0.f,txt);
                float tx5=sx5-tsz.x*0.5f, ty5=sy5-tsz.y;
                ImU32 outC=IM_COL32(0,0,0,(int)(180*pAlpha2));
                for(int ox=-2;ox<=2;ox++)for(int oy=-2;oy<=2;oy++)
                    if(ox||oy)dl->AddText(font,fs,ImVec2(tx5+ox,ty5+oy),outC,txt);
                dl->AddText(font,fs,ImVec2(tx5,ty5),IM_COL32(255,220,40,(int)(255*pAlpha2)),txt);}}
        // ── 韭菜血條 ──
        if(g_gameMode&&!g_hpBars.empty()){
            ImDrawList*dl=ImGui::GetForegroundDrawList();
            for(auto&h:g_hpBars){
                float bW=40.f, bH=6.f;
                float bx=h.sx-bW*0.5f, by=h.sy-10.f;
                float fill=glm::clamp(h.frac,0.f,1.f);
                // 背景（暗紅）
                dl->AddRectFilled(ImVec2(bx,by),ImVec2(bx+bW,by+bH),IM_COL32(60,20,20,200));
                // 血量（紅→黃→綠）
                ImU32 col=(fill>0.5f)?IM_COL32(40,(int)(200*fill),40,255)
                         :(fill>0.25f)?IM_COL32(220,180,30,255)
                         :IM_COL32(220,40,30,255);
                dl->AddRectFilled(ImVec2(bx,by),ImVec2(bx+bW*fill,by+bH),col);
                dl->AddRect(ImVec2(bx,by),ImVec2(bx+bW,by+bH),IM_COL32(180,180,180,180));
                // HP 數字
                char hpTxt[8];snprintf(hpTxt,sizeof(hpTxt),"%d",(int)(fill*80.f+0.5f));
                dl->AddText(ImVec2(bx+bW+3,by-2),IM_COL32(255,255,255,200),hpTxt);}}
        // ── 遊戲模式頂部 HUD ──
        if(g_gameMode&&g_bearActive){
            ImDrawList*dl=ImGui::GetForegroundDrawList();
            float hudX=10.f, hudY=8.f;
            float barW=220.f, barH=18.f, gap=6.f;

            // ── HP 血條（紅色）──
            float hpFrac=glm::clamp(g_bearHP/100.f,0.f,1.f);
            dl->AddRectFilled(ImVec2(hudX,hudY),ImVec2(hudX+barW,hudY+barH),IM_COL32(40,12,12,220),4.f);
            ImU32 hpC=(hpFrac>0.5f)?IM_COL32(200,40,40,255):(hpFrac>0.25f)?IM_COL32(220,150,30,255):IM_COL32(220,40,30,255);
            dl->AddRectFilled(ImVec2(hudX,hudY),ImVec2(hudX+barW*hpFrac,hudY+barH),hpC,4.f);
            dl->AddRect(ImVec2(hudX,hudY),ImVec2(hudX+barW,hudY+barH),IM_COL32(200,200,200,180),4.f);
            char hpBuf[24];snprintf(hpBuf,sizeof(hpBuf),"HP %d / 100",(int)g_bearHP);
            dl->AddText(ImVec2(hudX+6,hudY+2),IM_COL32(255,255,255,240),hpBuf);

            // ── MP 魔法條（藍色，滿=500）──
            float mpY=hudY+barH+gap;
            float mpFrac=glm::clamp(g_bearMP/500.f,0.f,1.f);
            bool canMagic=(g_bearMP>=75.f);
            dl->AddRectFilled(ImVec2(hudX,mpY),ImVec2(hudX+barW,mpY+barH),IM_COL32(10,15,40,220),4.f);
            dl->AddRectFilled(ImVec2(hudX,mpY),ImVec2(hudX+barW*mpFrac,mpY+barH),
                canMagic?IM_COL32(60,160,255,255):IM_COL32(40,80,140,200),4.f);
            dl->AddRect(ImVec2(hudX,mpY),ImVec2(hudX+barW,mpY+barH),IM_COL32(200,200,200,180),4.f);
            char mpBuf[24];snprintf(mpBuf,sizeof(mpBuf),"MP %d / 500 [K]",(int)g_bearMP);
            dl->AddText(ImVec2(hudX+6,mpY+2),IM_COL32(255,255,255,canMagic?240:120),mpBuf);

            // ── 擊殺數 ──
            float killY=mpY+barH+gap;
            char killBuf[24];snprintf(killBuf,sizeof(killBuf),"Kills: %d",g_killCount);
            dl->AddText(ImVec2(hudX,killY),IM_COL32(200,210,230,220),killBuf);

            // ── 死亡畫面：墓碑「散戶」──
            if(g_bearDead){
                float deadAlpha=std::min(1.f,(float)(glfwGetTime()-g_bearDeadT)/0.5f);
                ImFont*font=ImGui::GetFont();
                float fs=ImGui::GetFontSize()*5.f;
                // 墓碑壓在熊身上
                float tw2=(float)fw, th2=(float)fh;
                dl->AddRectFilled(ImVec2(0,0),ImVec2(tw2,th2),IM_COL32(0,0,0,(int)(120*deadAlpha)));
                float gsW=120.f,gsH=160.f;
                // 墓碑中心 = 熊的螢幕位置
                float gsX=g_bearScreenX-gsW*0.5f;
                float gsY=g_bearScreenY-gsH*0.6f; // 壓在熊上方
                dl->AddRectFilled(ImVec2(gsX,gsY),ImVec2(gsX+gsW,gsY+gsH),IM_COL32(120,120,120,(int)(230*deadAlpha)),8.f);
                dl->AddRectFilled(ImVec2(gsX-15,gsY+gsH),ImVec2(gsX+gsW+15,gsY+gsH+20),IM_COL32(90,90,90,(int)(230*deadAlpha)),4.f);
                // 頂部弧形
                dl->AddRectFilled(ImVec2(gsX,gsY-20),ImVec2(gsX+gsW,gsY),IM_COL32(120,120,120,(int)(230*deadAlpha)),30.f);
                // 「散戶」文字
                const char*rip="\xe6\x95\xa3\xe6\x88\xb6"; // 散戶
                float fs2=ImGui::GetFontSize()*3.f;
                ImVec2 rsz=font->CalcTextSizeA(fs2,FLT_MAX,0.f,rip);
                float rx=gsX+(gsW-rsz.x)*0.5f, ry=gsY+30.f;
                dl->AddText(font,fs2,ImVec2(rx,ry),IM_COL32(40,40,40,(int)(255*deadAlpha)),rip);
                // R.I.P.
                const char*ripTxt="R.I.P.";
                ImVec2 rsz2=font->CalcTextSizeA(fs2*0.6f,FLT_MAX,0.f,ripTxt);
                dl->AddText(font,fs2*0.6f,ImVec2(gsX+(gsW-rsz2.x)*0.5f,ry+rsz.y+10),
                    IM_COL32(60,60,60,(int)(220*deadAlpha)),ripTxt);
                // 「再來一局」提示
                const char*retry="\xe6\x8c\x89 R \xe9\x87\x8d\xe6\x96\xb0\xe9\x96\x8b\xe5\xa7\x8b"; // 按 R 重新開始
                ImVec2 rsz3=font->CalcTextSizeA(ImGui::GetFontSize()*1.5f,FLT_MAX,0.f,retry);
                dl->AddText(font,ImGui::GetFontSize()*1.5f,
                    ImVec2((tw2-rsz3.x)*0.5f,gsY+gsH+60),
                    IM_COL32(255,255,255,(int)(200*deadAlpha*((sinf((float)glfwGetTime()*3.f)+1.f)*0.5f))),retry);
                // 女孩跑出來說話（延遲 1 秒出現）
                float girlT=(float)(glfwGetTime()-g_bearDeadT)-1.0f;
                if(girlT>0.f){
                    float gAlpha=std::min(1.f,girlT/0.5f)*deadAlpha;
                    // 女孩位置（墓碑右側）
                    float gx=gsX+gsW+50.f, gy=gsY+gsH-30.f;
                    auto col=[&](int r2,int g2,int b2)->ImU32{return IM_COL32(r2,g2,b2,(int)(255*gAlpha));};

                    // ── 身體（粉色洋裝）──
                    float bodyW=24.f, bodyH=55.f;
                    float bodyY=gy+12.f;
                    dl->AddRectFilled(ImVec2(gx-bodyW,bodyY),ImVec2(gx+bodyW,bodyY+bodyH),
                        col(255,160,180),8.f); // 洋裝
                    // 裙擺（梯形，用三角形模擬）
                    dl->AddTriangleFilled(ImVec2(gx-bodyW-10,bodyY+bodyH),
                        ImVec2(gx+bodyW+10,bodyY+bodyH),
                        ImVec2(gx,bodyY+bodyH-15),col(255,140,165));
                    // 腰帶
                    dl->AddRectFilled(ImVec2(gx-bodyW-2,bodyY+18),ImVec2(gx+bodyW+2,bodyY+24),
                        col(220,100,130));
                    // 手臂（左右各一條）
                    dl->AddLine(ImVec2(gx-bodyW,bodyY+8),ImVec2(gx-bodyW-18,bodyY+40),
                        col(255,210,180),3.f);
                    dl->AddLine(ImVec2(gx+bodyW,bodyY+8),ImVec2(gx+bodyW+18,bodyY+40),
                        col(255,210,180),3.f);
                    // 腿（兩條）
                    dl->AddLine(ImVec2(gx-10,bodyY+bodyH),ImVec2(gx-12,bodyY+bodyH+30),
                        col(255,210,180),3.f);
                    dl->AddLine(ImVec2(gx+10,bodyY+bodyH),ImVec2(gx+12,bodyY+bodyH+30),
                        col(255,210,180),3.f);
                    // 鞋子
                    dl->AddCircleFilled(ImVec2(gx-12,bodyY+bodyH+32),5.f,col(180,60,80));
                    dl->AddCircleFilled(ImVec2(gx+12,bodyY+bodyH+32),5.f,col(180,60,80));

                    // ── 頭部 ──
                    float headR=22.f;
                    // 長頭髮（深棕色，先畫在臉後面）
                    // 後髮（大橢圓）
                    dl->AddEllipseFilled(ImVec2(gx,gy),ImVec2(headR+8,headR+20),col(60,30,20));
                    // 左側長髮垂下
                    dl->AddRectFilled(ImVec2(gx-headR-6,gy),ImVec2(gx-headR+4,gy+50),
                        col(60,30,20),5.f);
                    // 右側長髮垂下
                    dl->AddRectFilled(ImVec2(gx+headR-4,gy),ImVec2(gx+headR+6,gy+50),
                        col(60,30,20),5.f);
                    // 臉
                    dl->AddCircleFilled(ImVec2(gx,gy),headR,col(255,220,190));
                    // 瀏海（額頭）
                    dl->AddEllipseFilled(ImVec2(gx,gy-headR*0.5f),ImVec2(headR+2,headR*0.5f),col(60,30,20));
                    // 眼睛（大圓眼）
                    dl->AddCircleFilled(ImVec2(gx-8,gy-3),4.f,col(40,40,40));
                    dl->AddCircleFilled(ImVec2(gx+8,gy-3),4.f,col(40,40,40));
                    // 亮點
                    dl->AddCircleFilled(ImVec2(gx-6,gy-5),1.5f,col(255,255,255));
                    dl->AddCircleFilled(ImVec2(gx+10,gy-5),1.5f,col(255,255,255));
                    // 腮紅
                    dl->AddCircleFilled(ImVec2(gx-14,gy+5),5.f,IM_COL32(255,150,150,(int)(100*gAlpha)));
                    dl->AddCircleFilled(ImVec2(gx+14,gy+5),5.f,IM_COL32(255,150,150,(int)(100*gAlpha)));
                    // 微笑
                    dl->AddBezierQuadratic(ImVec2(gx-6,gy+8),ImVec2(gx,gy+13),ImVec2(gx+6,gy+8),
                        col(200,80,80),2.f);

                    // ── 泡泡框台詞 ──
                    const char*girlTxt="\xe7\x86\x8a\xe7\x86\x8a\xe8\xa8\x98\xe5\xbe\x97\xe8\xa3\x9c\xe9\x8c\xa2"; // 熊熊記得補錢
                    float gfs=ImGui::GetFontSize()*1.8f;
                    ImVec2 gtsz=font->CalcTextSizeA(gfs,FLT_MAX,0.f,girlTxt);
                    float bpad=10.f;
                    float bx5=gx-gtsz.x*0.5f-bpad, by5=gy-headR-gtsz.y-bpad*3.5f;
                    dl->AddRectFilled(ImVec2(bx5,by5),ImVec2(bx5+gtsz.x+bpad*2,by5+gtsz.y+bpad*2),
                        IM_COL32(255,240,245,(int)(230*gAlpha)),10.f);
                    dl->AddRect(ImVec2(bx5,by5),ImVec2(bx5+gtsz.x+bpad*2,by5+gtsz.y+bpad*2),
                        IM_COL32(255,150,180,(int)(200*gAlpha)),10.f,0,2.f);
                    dl->AddTriangleFilled(
                        ImVec2(gx-5,by5+gtsz.y+bpad*2),
                        ImVec2(gx+5,by5+gtsz.y+bpad*2),
                        ImVec2(gx,by5+gtsz.y+bpad*2+8),
                        IM_COL32(255,240,245,(int)(230*gAlpha)));
                    dl->AddText(font,gfs,ImVec2(bx5+bpad,by5+bpad),
                        IM_COL32(220,60,100,(int)(255*gAlpha)),girlTxt);}}}
        } // else (non-MENU)
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(g_win);}
#ifdef __EMSCRIPTEN__
} // end mainLoopBody for emscripten
#else
    // end of while loop — cleanup (desktop only)
    mGreen.del();mRed.del();mGrid.del();mMA5.del();mMA20.del();mMA60.del();
    mMA120.del();mMA240.del();mVolG.del();mVolR.del();
    mMHG.del();mMHR.del();mDIF.del();mDEA.del();
    mKK.del();mKDLine.del();mRSI.del();mBBUpper.del();mBBLower.del();mBBMid.del();
    mLabel.del();mHoverLine.del();
    mChr0.del();mChr1.del();mChr2.del();mChr3.del();mChr4.del();
    mLeekG.del();mLeekW.del();mLeekD.del();mBoom.del();mTornado.del();
    mLobR.del();mLobD.del();mBoxB.del();mBoxD.del();mClaw.del();
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();
    glDeleteProgram(g_pLit);glDeleteProgram(g_pFlat);
    glfwTerminate();return 0;}
#endif
