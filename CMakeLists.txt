cmake_minimum_required(VERSION 3.13.0)

project(AndroidSurfaceImgui)

option(ANDROID_SURFACE_IMGUI_BUILD_STATIC "Build AImGui static library." ON)
option(ANDROID_SURFACE_IMGUI_BUILD_SHARED "Build test library." OFF)
option(ANDROID_SURFACE_IMGUI_BUILD_TESTING "Build test programs." ON)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-fno-rtti -fvisibility=hidden)

find_library(liblog log NO_CACHE)

# 【关键】添加当前目录到头文件路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}  # 让编译器能找到 stb_image.h
    modules
    includes
    third_party
    third_party/imgui
)

# ImGui sources
aux_source_directory(third_party/imgui IMGUI_SOURCES)
set(
    IMGUI_BACKENDS_SOURCES
    third_party/imgui/backends/imgui_impl_android.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
aux_source_directory(src/common COMMON_SOURCES)

# Build AImGui static library
add_library(AImGui STATIC 
    ${COMMON_SOURCES}
    ${IMGUI_SOURCES}
    ${IMGUI_BACKENDS_SOURCES}
)

target_link_libraries(AImGui 
    ${liblog}
    EGL
    GLESv3
    android
)

# Build test-ui
add_executable(test-ui 
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
)

target_link_libraries(test-ui
    AImGui
    ${liblog}
    EGL
    GLESv3
    android
)

set_target_properties(test-ui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "tests/${ANDROID_ABI}"
)
