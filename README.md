# KLineGL3D - 3D K 線圖 + 拉拉熊割韭菜遊戲

台股 3D K 線圖視覺化工具，內建拉拉熊 RPG 小遊戲。

## 功能

### K 線圖
- 即時台股資料（Yahoo Finance + TWSE 備援）
- 5 種週期：日線、週線、月線、季線、年線（按 1-5 切換）
- 技術指標：MA5/20/60/120/240、MACD、KD、RSI、布林通道
- 3D 旋轉視角（拖曳旋轉、Ctrl+滾輪縮放）
- 時間軸平移（Shift+拖曳 或 中鍵拖曳）
- 自選股清單（可新增/刪除）

### 拉拉熊遊戲模式
- 拉拉熊站在 K 棒上，手持死神鐮刀
- 韭菜怪物隨機出現在 K 棒上（5~10 隻）
- 飛行龍蝦在 K 棒上方巡邏（1~3 隻）
- 擊殺怪物獲得金幣，累積可施放龍捲風魔法

## 操作

### 圖表操作
| 按鍵 | 功能 |
|------|------|
| 1-5 | 切換週期（日/週/月/季/年） |
| Space | 播放/暫停 K 線動畫 |
| 滾輪 | K 線縮放 |
| Ctrl+滾輪 | 鏡頭縮放 |
| 拖曳 | 旋轉視角 |
| Shift+拖曳 | 時間平移 |
| ESC | 退出 |

### 遊戲操作
| 按鍵 | 功能 |
|------|------|
| D | 向左跳 |
| F | 向右跳 |
| J | 鐮刀攻擊 |
| K | 龍捲風魔法（消耗 $5000） |

### 遊戲機制
- 熊 HP: 1000
- 韭菜 HP: 50（鐮刀每擊 10~30 傷害）
- 龍蝦 HP: 80（鐮刀每擊 10~30 傷害）
- 怪物攻擊熊：韭菜 5~10 / 龍蝦 5~15
- 擊殺 +$100，魔法 -$5000
- 龍捲風：全畫面掃射殺光所有怪物

## 編譯

### 需求
- Windows 10/11
- Visual Studio 2022 (C++17)
- vcpkg 套件：OpenGL, GLEW, GLFW3, GLM, Dear ImGui

### 建置
```bash
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## 技術

- OpenGL 3.3 Core Profile + Phong 光照
- Dear ImGui (GLFW + OpenGL3 backend)
- WinHTTP (HTTPS 資料抓取)
- 3D 角色以 axis-aligned box 堆疊建模
- 即時音效（Windows Beep API）
