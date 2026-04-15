#!/bin/bash
# Setup script for Android build dependencies
# Run from android/ directory

CPP_DIR="app/src/main/cpp"

# Download ImGui
if [ ! -d "$CPP_DIR/imgui" ]; then
    echo "Downloading ImGui..."
    git clone --depth 1 https://github.com/ocornut/imgui.git "$CPP_DIR/imgui"
fi

# Download GLM
if [ ! -d "$CPP_DIR/glm" ]; then
    echo "Downloading GLM..."
    git clone --depth 1 https://github.com/g-truc/glm.git "$CPP_DIR/glm"
fi

# Copy game assets
echo "Copying assets..."
mkdir -p app/src/main/assets
cp ../assets/*.wav app/src/main/assets/ 2>/dev/null
cp ../assets/*.png app/src/main/assets/ 2>/dev/null

# Copy stb_image
cp ../stb_image.h "$CPP_DIR/" 2>/dev/null

echo "Done! Open this directory in Android Studio to build."
echo ""
echo "Build steps:"
echo "1. Open Android Studio"
echo "2. File -> Open -> select this 'android' directory"
echo "3. Wait for Gradle sync"
echo "4. Build -> Build APK"
