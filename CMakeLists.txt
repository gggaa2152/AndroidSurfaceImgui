cmake_minimum_required(VERSION 3.13.0)

project(AndroidSurfaceImgui)

option(ANDROID_SURFACE_IMGUI_BUILD_STATIC "Build AImGui static library." ON)
option(ANDROID_SURFACE_IMGUI_BUILD_SHARED "Build test library." ON)
option(ANDROID_SURFACE_IMGUI_BUILD_TESTING "Build test programs." ON)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-fno-rtti -fvisibility=hidden)
add_link_options(-s)

find_library(liblog log NO_CACHE)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    modules
    includes
    third_party
    third_party/imgui
    third_party/zstd/lib
)

# --- 源码扫描区 ---
aux_source_directory(third_party/imgui AIMGUI_IMGUI_SOURCES)
set(
    AIMGUI_IMGUI_BACKENDS_SOURCES
    third_party/imgui/backends/imgui_impl_android.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
aux_source_directory(src/common AIMGUI_COMMON_SOURCES)

# --- 构建依赖库 (Zstd) ---
set(ZSTD_LEGACY_SUPPORT OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_SHARED OFF)
add_subdirectory(third_party/zstd/build/cmake)

# --- 1. 构建 AImGui 核心库 (静态) ---
if(ANDROID_SURFACE_IMGUI_BUILD_STATIC)
    add_library(AImGui STATIC 
        ${AIMGUI_COMMON_SOURCES} 
        ${AIMGUI_IMGUI_SOURCES} 
        ${AIMGUI_IMGUI_BACKENDS_SOURCES}
    )
    target_link_libraries(AImGui ${liblog} EGL GLESv3 android libzstd_static)
    set_target_properties(AImGui PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "libs/${ANDROID_ABI}"
    )
endif()

# --- 2. 构建 test-ui 执行程序 ---
# 关键修复：只编 main.cc，链接 AImGui 库以避免符号冲突
if(ANDROID_SURFACE_IMGUI_BUILD_TESTING)
    add_executable(test-ui 
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cc 
    )
    
    target_link_libraries(test-ui 
        AImGui           # 核心：直接使用 AImGui 已经编译好的符号
        ${liblog} 
        EGL 
        GLESv3 
        android 
        libzstd_static
    )
    
    set_target_properties(test-ui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "tests/${ANDROID_ABI}"
    )
    
    # 辅助测试程序也同步修改
    add_executable(canvas-ui src/test-ui/canvas.cc)
    target_link_libraries(canvas-ui AImGui ${liblog} EGL GLESv3 android libzstd_static)
    set_target_properties(canvas-ui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "tests/${ANDROID_ABI}"
    )

    add_executable(render-ui src/test-ui/render.cc)
    target_link_libraries(render-ui AImGui ${liblog} EGL GLESv3 android libzstd_static)
    set_target_properties(render-ui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "tests/${ANDROID_ABI}"
    )
endif()

# --- 3. 构建 test 动态库 (如果需要) ---
if(ANDROID_SURFACE_IMGUI_BUILD_SHARED)
    add_library(test SHARED src/core/main.cc)
    target_link_libraries(test AImGui ${liblog} EGL GLESv3 android libzstd_static)
    set_target_properties(test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "libs/${ANDROID_ABI}"
    )
endif()
