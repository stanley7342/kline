cmake_minimum_required(VERSION 3.22)
project(klinerpg CXX)

set(CMAKE_CXX_STANDARD 17)

# ImGui sources (download or copy to this directory)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

# GLM (header-only, download or copy)
set(GLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glm)

add_library(${PROJECT_NAME} SHARED
    android_main.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLM_DIR}
)

target_link_libraries(${PROJECT_NAME}
    android
    log
    EGL
    GLESv3
    OpenSLES
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    __ANDROID_GAME__
    IMGUI_IMPL_OPENGL_ES3
)
