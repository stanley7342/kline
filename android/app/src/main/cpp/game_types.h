#pragma once
#include <string>
#include <vector>
#include <array>
#include <map>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>

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
