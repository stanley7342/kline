# 割韭菜無雙 — Android 版

## 需求
- Android Studio Hedgehog (2023.1+)
- Android NDK 26+
- Android SDK API 34
- CMake 3.22+

## 建置步驟

### 1. 下載依賴
```bash
cd android
bash setup.sh
```

### 2. Android Studio 開啟
- File -> Open -> 選擇 `android/` 目錄
- 等待 Gradle sync 完成

### 3. 編譯
- Build -> Build APK
- APK 輸出在 `app/build/outputs/apk/debug/`

### 4. 安裝到手機
```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

## 專案結構
```
android/
├── app/
│   ├── build.gradle          # Android 建置設定
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── java/.../MainActivity.java  # Java 入口
│       ├── cpp/
│       │   ├── CMakeLists.txt          # NDK 建置
│       │   ├── android_main.cpp        # Android 主迴圈 + 觸控
│       │   ├── imgui/                  # ImGui (setup.sh 下載)
│       │   └── glm/                    # GLM (setup.sh 下載)
│       └── assets/                     # 音效/圖片素材
├── setup.sh                  # 依賴下載腳本
└── README.md
```

## 觸控操作
- 左下：D (左移) / F (右移) / Jump (跳躍)
- 右下：J (攻擊) / K (魔法)

## 開發進度
- [x] 專案結構
- [x] EGL + OpenGL ES 3.0 初始化
- [x] ImGui Android backend
- [x] 觸控虛擬按鈕
- [ ] 遊戲邏輯移植（共用 game_logic.h）
- [ ] 網路抓股票（JNI HttpURLConnection）
- [ ] 音效（OpenSL ES）
- [ ] Google Play 上架
