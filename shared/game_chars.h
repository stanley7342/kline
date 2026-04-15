#pragma once
// Character builders (bear, leek, lobster, box)

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

    // ── 手臂（右手揮劍，左手不動）──────────────────────────
    {
        float aH=sc*0.085f, aDZ=sc*0.068f, aLen=sc*0.175f;
        float aY=lp(bCY+bRYraw*0.22f, bCY+bRYraw*1.10f, jumpT);
        float aLj=lp(aLen, aLen*1.35f, jumpT);
        // 左臂（固定不動）
        pushBox(r,bx-bRX-aLj,bx-bRX,aY,aY+aH,-aDZ,aDZ);
        disc(r,bx-bRX-aLj,aY+aH*.5f,aH*0.58f,-aDZ*.72f,aDZ*.72f,6);
        // 右臂（揮劍時跟隨劍角度）
        if(slashT<=0.f){
            pushBox(r,bx+bRX,bx+bRX+aLj,aY,aY+aH,-aDZ,aDZ);
            disc(r,bx+bRX+aLj,aY+aH*.5f,aH*0.58f,-aDZ*.72f,aDZ*.72f,6);
        } else {
            float sAng;
            if(slashT<0.35f)      sAng=slashT/0.35f*1.2f;
            else if(slashT<0.55f) sAng=1.2f-(slashT-0.35f)/0.20f*2.8f;
            else                  sAng=-1.6f+(slashT-0.55f)/0.45f*1.6f;
            float rC=cosf(sAng+1.5708f), rS=sinf(sAng+1.5708f);
            float rRX=bx+bRX, rRY=aY+aH*0.5f;
            float rTX=rRX+rC*aLj*1.2f, rTY=rRY+rS*aLj*1.2f;
            pushBox(r,std::min(rRX,rTX)-aH*0.45f,std::max(rRX,rTX)+aH*0.45f,
                       std::min(rRY,rTY)-aH*0.45f,std::max(rRY,rTY)+aH*0.45f,-aDZ,aDZ);
            disc(r,rTX,rTY,aH*0.55f,-aDZ*.72f,aDZ*.72f,6);
        }
    }

    // ── 死神鐮刀（50段弧刃）──────────────────────────────
    if(slashT>0.f){
        float ang0;
        if(slashT<0.35f)      ang0=1.40f-slashT/0.35f*0.50f;
        else if(slashT<0.55f) ang0=0.90f-(slashT-0.35f)/0.20f*2.50f;
        else                  ang0=-1.60f+(slashT-0.55f)/0.45f*1.60f;
        float ang=ang0+1.5708f;
        float cosA=cosf(ang), sinA=sinf(ang);
        float sRootX=bx+bRX*0.75f+cosA*sc*0.05f;
        float sRootY=lp(bCY+bRYraw*0.22f,bCY+bRYraw*1.10f,jumpT)+sc*0.04f+sinA*sc*0.05f;
        float handleLen=sc*1.25f, gripHW=sc*0.040f;
        for(int i=0;i<12;i++){
            float t0=(float)i/12,t1=(float)(i+1)/12;
            float x0=sRootX+cosA*handleLen*t0,y0_=sRootY+sinA*handleLen*t0;
            float x1=sRootX+cosA*handleLen*t1,y1_=sRootY+sinA*handleLen*t1;
            float hw=gripHW*(1.f-t0*0.35f);
            pushBox(r,std::min(x0,x1)-hw,std::max(x0,x1)+hw,std::min(y0_,y1_)-hw,std::max(y0_,y1_)+hw,-hw*0.55f,hw*0.55f);
            if(i%3==0){float mx=(x0+x1)*0.5f,my=(y0_+y1_)*0.5f;
                pushBox(d,mx-hw*1.1f,mx+hw*1.1f,my-hw*0.3f,my+hw*0.3f,-hw*0.6f,hw*0.6f);}}
        {float bsx2=sRootX-cosA*gripHW*2.5f,bsy2=sRootY-sinA*gripHW*2.5f;
         pushBox(r,bsx2-gripHW*1.4f,bsx2+gripHW*1.4f,bsy2-gripHW*1.4f,bsy2+gripHW*1.4f,-gripHW*0.9f,gripHW*0.9f);}
        float tipX=sRootX+cosA*handleLen,tipY=sRootY+sinA*handleLen;
        pushBox(d,tipX-sc*0.058f,tipX+sc*0.058f,tipY-sc*0.058f,tipY+sc*0.058f,-sc*0.044f,sc*0.044f);
        float bladeLen=sc*1.15f,perpX=-sinA,perpY=cosA;
        for(int i=0;i<50;i++){
            float t0=(float)i/50,t1=(float)(i+1)/50;
            float c0=sinf(t0*2.6f)*0.75f,c1=sinf(t1*2.6f)*0.75f;
            float bx0=tipX+perpX*bladeLen*t0-cosA*bladeLen*c0*0.58f;
            float by0=tipY+perpY*bladeLen*t0-sinA*bladeLen*c0*0.58f;
            float bx1=tipX+perpX*bladeLen*t1-cosA*bladeLen*c1*0.58f;
            float by1=tipY+perpY*bladeLen*t1-sinA*bladeLen*c1*0.58f;
            float w2=sc*0.060f*sinf(t0*3.14159f)+sc*0.010f;
            pushBox(sw,std::min(bx0,bx1)-w2,std::max(bx0,bx1)+w2,std::min(by0,by1)-w2,std::max(by0,by1)+w2,-w2*0.32f,w2*0.32f);}
        for(int i=0;i<50;i+=3){float t0=(float)i/50;float c0=sinf(t0*2.6f)*0.75f;
            float bx0=tipX+perpX*bladeLen*t0-cosA*bladeLen*c0*0.58f;
            float by0=tipY+perpY*bladeLen*t0-sinA*bladeLen*c0*0.58f;
            float sp2=sc*0.016f;pushBox(d,bx0-sp2,bx0+sp2,by0-sp2,by0+sp2,-sp2*0.5f,sp2*0.5f);}
        if(slashT>0.30f&&slashT<0.65f){float glow=sinf((slashT-0.30f)/0.35f*3.14159f);
            float gx2=tipX+perpX*bladeLen*0.9f-cosA*bladeLen*sinf(0.9f*2.6f)*0.75f*0.58f;
            float gy2=tipY+perpY*bladeLen*0.9f-sinA*bladeLen*sinf(0.9f*2.6f)*0.75f*0.58f;
            float gs=sc*0.08f*glow;pushBox(sw,gx2-gs,gx2+gs,gy2-gs,gy2+gs,-gs,gs);}
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

