cmake_minimum_required(VERSION 3.20)
project(linebeat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

# ---------------------------------------------------------------- 三方依赖
# 仓库里不 vendor 任何三方源码。规则：
#   * 只用上游打过 tag 的发布版（不用 master / *-WIP），升级时同步改版本号与 URL_HASH；
#   * nlohmann/json 优先用系统包（版本必须正好 3.12.0），没有才从上游拉；
#   * Dear ImGui 没有通用系统包，构建时从上游拉固定 tag 到 build/_deps。
# 离线构建：有系统 nlohmann/json 时不联网；Dear ImGui 可用
#   -DFETCHCONTENT_SOURCE_DIR_IMGUI_UPSTREAM=<本地目录> 覆盖。
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)   # 解压时间戳用解压时刻，避免每次构建都被当成新文件
endif()

FetchContent_Declare(imgui_upstream
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
    URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
)

find_package(nlohmann_json 3.12.0 EXACT QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann/json: 使用系统包 ${nlohmann_json_VERSION}")
else()
    FetchContent_Declare(json_upstream
        URL      https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
endif()

FetchContent_MakeAvailable(imgui_upstream)
if(NOT nlohmann_json_FOUND)
    FetchContent_MakeAvailable(json_upstream)
endif()

add_library(imgui STATIC
    ${imgui_upstream_SOURCE_DIR}/imgui.cpp
    ${imgui_upstream_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_upstream_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_upstream_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_upstream_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_upstream_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_upstream_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_upstream_SOURCE_DIR}
    ${imgui_upstream_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)

# ---------------------------------------------------------------- core
# 不链接 SDL / GL / ImGui，可以单独跑命令行
add_library(lb_core STATIC
    core/expr.cpp
    core/model.cpp
    core/physics.cpp
    core/audio.cpp
    core/project_io.cpp
)
target_include_directories(lb_core PUBLIC core)
target_link_libraries(lb_core PUBLIC PkgConfig::FLUIDSYNTH SDL3::SDL3 nlohmann_json::nlohmann_json)
target_compile_options(lb_core PRIVATE -ffp-contract=off)

# ---------------------------------------------------------------- app
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_executable(linebeat
    src/main.cpp
    src/app.cpp
    src/canvas.cpp
    src/panels.cpp
    src/dialog.cpp
    src/config.cpp
    src/start.cpp
    src/menu.cpp
    src/ui.cpp
    src/scenebrowser.cpp
    src/i18n.cpp
    src/render.cpp
    src/fonts.cpp
)
# 编译标记：每次构建都重新生成，截图时能看出是不是旧程序
set(LB_STAMP_HEADER ${CMAKE_BINARY_DIR}/lb_build_stamp.h)
add_custom_target(lb_build_stamp ALL
    COMMAND ${CMAKE_COMMAND} -DLB_SRC=${CMAKE_CURRENT_SOURCE_DIR} -DLB_OUT=${LB_STAMP_HEADER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stamp.cmake
    BYPRODUCTS ${LB_STAMP_HEADER}
    COMMENT "生成编译标记")
add_dependencies(linebeat lb_build_stamp)
set_source_files_properties(src/start.cpp PROPERTIES OBJECT_DEPENDS ${LB_STAMP_HEADER})
target_include_directories(linebeat PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(linebeat PRIVATE lb_core imgui SDL3::SDL3 OpenGL::GL)
target_compile_options(linebeat PRIVATE -ffp-contract=off)
endif()

# ---------------------------------------------------------------- cli harness
add_executable(lb_cli
    tools/lb_cli.cpp
)
target_link_libraries(lb_cli PRIVATE lb_core)
target_compile_options(lb_cli PRIVATE -ffp-contract=off)
