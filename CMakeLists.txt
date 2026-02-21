cmake_minimum_required(VERSION 3.13.0)

project(AndroidSurfaceImgui)

# ========== 只编译 test-ui，关闭所有不需要的选项 ==========
option(ANDROID_SURFACE_IMGUI_BUILD_STATIC "Build AImGui static library." ON)
option(ANDROID_SURFACE_IMGUI_BUILD_SHARED "Build test library." OFF)      # 关闭动态库
option(ANDROID_SURFACE_IMGUI_BUILD_TESTING "Build test programs." ON)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-fno-rtti -fvisibility=hidden)
# add_link_options(-s)  # 可选：如果需要strip可以保留

find_library(liblog log NO_CACHE)

# ========== 头文件路径（移除zstd） ==========
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}        # 添加根目录，让 main.cc 能被找到
    modules
    includes
    third_party
    third_party/imgui
    # third_party/zstd/lib  # 注释掉，不需要zstd
)

# ========== 源码扫描 ==========
aux_source_directory(third_party/imgui AIMGUI_IMGUI_SOURCES)
set(
    AIMGUI_IMGUI_BACKENDS_SOURCES
    third_party/imgui/backends/imgui_impl_android.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
aux_source_directory(src/common AIMGUI_COMMON_SOURCES)

# ========== 注释掉 zstd 构建 ==========
# set(ZSTD_LEGACY_SUPPORT OFF)
# set(ZSTD_BUILD_PROGRAMS OFF)
# set(ZSTD_BUILD_SHARED OFF)
# add_subdirectory(third_party/zstd/build/cmake)

# ========== 构建 AImGui 静态库 ==========
if(ANDROID_SURFACE_IMGUI_BUILD_STATIC)
    add_library(AImGui STATIC 
        ${AIMGUI_COMMON_SOURCES} 
        ${AIMGUI_IMGUI_SOURCES} 
        ${AIMGUI_IMGUI_BACKENDS_SOURCES}
    )
    target_link_libraries(AImGui 
        ${liblog} 
        EGL 
        GLESv3 
        android
        # libzstd_static  # 注释掉
    )
    set_target_properties(AImGui PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "libs/${ANDROID_ABI}"
    )
endif()

# ========== 构建 test-ui 可执行文件（只这一个） ==========
if(ANDROID_SURFACE_IMGUI_BUILD_TESTING)
    # main.cc 现在在根目录
    add_executable(test-ui 
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
    )
    
    target_link_libraries(test-ui 
        AImGui  # 链接静态库，避免重复编译
        ${liblog} 
        EGL 
        GLESv3 
        android
        # libzstd_static  # 注释掉
    )
    
    set_target_properties(test-ui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "tests/${ANDROID_ABI}"
    )
    
    # ========== 注释掉不需要的测试程序 ==========
    # add_executable(canvas-ui src/test-ui/canvas.cc ...)
    # add_executable(render-ui src/test-ui/render.cc ...)
endif()

# ========== 注释掉动态库构建 ==========
# if(ANDROID_SURFACE_IMGUI_BUILD_SHARED)
#     add_library(test SHARED src/core/main.cc ...)
# endif()
