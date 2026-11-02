# =============================================================================
# platform/macos/CMakeLists.txt — the native macOS (arm64) build of the editor.
#
# This is a SEPARATE CMake project on purpose.  The repository's root
# CMakeLists.txt is the MinGW/MSVC Windows build: it hard-fails on non-Windows,
# links the Windows GL import libraries and embeds a Windows .rc.  Making it
# dual-platform would put the shipping Windows gate at risk for no benefit, so
# the Darwin build lives here and reuses the same sources by globbing them.
#
#   cmake -S platform/macos -B build-macos -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-macos --parallel
#   open build/igi1ed.app          # see platform/macos/make-bundle.sh
#
# What differs from Windows, and why:
#   * GL: Apple ships OpenGL/GLUT frameworks; there is no GLEW and no FreeGLUT.
#     include/glew.h and include/freeglut.h are shims over the frameworks and
#     are searched BEFORE anything else.  See src/glew_shim.cpp.
#   * Context: no GLUT_3_2_CORE_PROFILE is requested, so GLUT creates Apple's
#     legacy profile (GL 2.1).  That is deliberate: the renderer's immediate
#     mode (~128 glBegin call sites, e.g. source/app_ui.cpp and
#     source/menu/menu_ui_draw.cpp) does not exist in a core profile, so the
#     proven GL 2.1 tier (shaders/21) is the coherent target.  Probe result on
#     this host: a 4.1 core context IS reachable via GLUT_3_2_CORE_PROFILE, but
#     the 4.1 tier is not core-profile-clean.
#   * Tests are not built here.  igi_tests is the Windows suite (it uses
#     CreateProcess and Wine-staged assets); the host lane covers logic.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../version" IGI_EDITOR_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${IGI_EDITOR_VERSION_STRING}" IGI_EDITOR_VERSION_STRING)
if (NOT IGI_EDITOR_VERSION_STRING)
  message(FATAL_ERROR "The `version` file at the repository root is missing or empty.")
endif()
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" IGI_EDITOR_VERSION_NUMERIC "${IGI_EDITOR_VERSION_STRING}")

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architecture (Apple Silicon)")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(igi_editor_macos
  VERSION ${IGI_EDITOR_VERSION_NUMERIC}
  LANGUAGES C CXX)

if (NOT APPLE)
  message(FATAL_ERROR "platform/macos builds the native macOS editor; configure with an Apple toolchain.")
endif()

set(IGI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(IGI_ROOT "${IGI_ROOT}" ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Same source set as the root project.  CONFIGURE_DEPENDS keeps the glob honest
# (the root CMakeLists.txt documents the stale-object failure that motivated it).
file(GLOB IGI_SOURCES CONFIGURE_DEPENDS "${IGI_ROOT}/source/*.cpp")
file(GLOB IGI_SOURCES_LEVEL CONFIGURE_DEPENDS "${IGI_ROOT}/source/level/*.cpp")
file(GLOB IGI_SOURCES_RUNTIME CONFIGURE_DEPENDS "${IGI_ROOT}/source/runtime/*.cpp")
file(GLOB IGI_SOURCES_RENDERER CONFIGURE_DEPENDS "${IGI_ROOT}/source/renderer/*.cpp")
file(GLOB IGI_SOURCES_CLI CONFIGURE_DEPENDS "${IGI_ROOT}/source/cli/*.cpp")
file(GLOB IGI_SOURCES_MENU CONFIGURE_DEPENDS "${IGI_ROOT}/source/menu/*.cpp")

# wic_loader.cpp is Windows Imaging Component (wincodec.h).  It defines a
# single function, WIC_LoadImage(), which has no call sites in the tree -- the
# IGI texture path uses the .tex/.dat readers instead -- so it is simply not
# part of the macOS target rather than stubbed.
list(REMOVE_ITEM IGI_SOURCES_RENDERER "${IGI_ROOT}/source/renderer/wic_loader.cpp")

add_executable(igi-editor
  ${IGI_SOURCES}
  ${IGI_SOURCES_LEVEL}
  ${IGI_SOURCES_RUNTIME}
  ${IGI_SOURCES_RENDERER}
  ${IGI_SOURCES_CLI}
  ${IGI_SOURCES_MENU}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/glut_compat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/glew_shim.cpp
)

set_target_properties(igi-editor PROPERTIES OUTPUT_NAME "igi1ed")

# The Windows build compiles most translation units through source/pch.h, and a
# few of them genuinely rely on it for declarations (e.g. renderer/model.cpp
# uses `Mesh` from a header the PCH pulls in).  Mirror that here so the macOS
# build is comparing like with like instead of surfacing pre-existing latent
# include-order dependencies as port failures.
target_precompile_headers(igi-editor PRIVATE
  "${IGI_ROOT}/source/pch.h"
)

# Apple's GLUT/OpenGL headers are deprecated (not removed); the framework must
# stay usable until the renderer moves to a core profile + a windowing library.
target_compile_definitions(igi-editor PRIVATE
  GL_SILENCE_DEPRECATION
  MAX_PATH=256
)

target_compile_options(igi-editor PRIVATE
  -Wno-deprecated-declarations
  # The code base is C++20 with MSVC/MinGW habits (e.g. `register`-era GL
  # headers); these are warnings-only on clang but some are hard errors under
  # -Werror, which is not on.  Listed explicitly so the build is reproducible.
  -Wno-unknown-pragmas
)

# Shim headers MUST come first: they replace `glew.h` and `freeglut.h` that the
# Windows build takes from third_party/GL/include.
target_include_directories(igi-editor BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(igi-editor PRIVATE
  "${IGI_ROOT}/third_party/glm-1.0.1"
  "${IGI_ROOT}/third_party/tinygltf"
  "${IGI_ROOT}/source"
  "${IGI_ROOT}/source/runtime"
  "${IGI_ROOT}/source/cli"
)

target_link_libraries(igi-editor PRIVATE
  "-framework GLUT"
  "-framework OpenGL"
  "-framework Cocoa"
)

# Runtime asset tree.  The editor resolves `editor/...` relative to its own
# executable directory (Utils::GetExeDirectory), so mirror the Windows layout
# next to the binary; make-bundle.sh then moves the pair into an .app.
add_custom_command(TARGET igi-editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${IGI_ROOT}/assets/editor"
    "$<TARGET_FILE_DIR:igi-editor>/editor"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${IGI_ROOT}/shaders/21"
    "$<TARGET_FILE_DIR:igi-editor>/editor/shaders/21"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${IGI_ROOT}/version"
    "$<TARGET_FILE_DIR:igi-editor>"
  COMMENT "Staging editor/ asset tree next to the macOS binary"
)

message(STATUS "IGI editor (macOS) version ${IGI_EDITOR_VERSION_STRING}, target ${CMAKE_OSX_ARCHITECTURES}")
