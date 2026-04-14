cmake_minimum_required(VERSION 3.20)
project(KLineGL3D CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(GLEW   REQUIRED)
find_package(glfw3  REQUIRED)
find_package(glm    CONFIG REQUIRED)
find_package(imgui  CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    GLEW::GLEW
    glfw
    glm::glm
    imgui::imgui
    winhttp
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8)

    set(DEPLOY "$ENV{USERPROFILE}/Desktop/kline")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        # ── 複製 DLL 到輸出目錄 ──
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/vcpkg/installed/x64-windows/bin/glew32.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/vcpkg/installed/x64-windows/bin/glfw3.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        # ── 確保桌面 kline 資料夾存在 ──
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY}"
        # ── 關掉正在執行的舊版本（忽略錯誤）──
        COMMAND cmd /C "taskkill /F /IM KLineGL3D.exe >nul 2>&1 || exit 0"
        # ── 打包到桌面/kline ──
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${DEPLOY}/KLineGL3D.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/vcpkg/installed/x64-windows/bin/glew32.dll"
            "${DEPLOY}/glew32.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/vcpkg/installed/x64-windows/bin/glfw3.dll"
            "${DEPLOY}/glfw3.dll"
        # ── 自動啟動新版本 ──
        COMMAND cmd /C "start \"\" \"${DEPLOY}/KLineGL3D.exe\""
        COMMENT "打包到 ${DEPLOY} 並啟動"
    )
endif()
