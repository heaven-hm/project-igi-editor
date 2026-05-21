cmake_minimum_required(VERSION 3.16)
# target_precompile_headers
#   Added in version 3.16

project (igi_editor 
  VERSION 1.5
  LANGUAGES C CXX)

# Designated initializers, introduced in C++20
set(CMAKE_CXX_STANDARD 20)

# enable FOLDER property
#   seperate projects into different groups
SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release")
endif ()

message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
        add_definitions(-D_DEBUG)
    message("Set cmake debug")
endif ()


# set output binary file directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

if (WIN32)
  # set using UNICODE characterset
  add_definitions(-DUNICODE -D_UNICODE)
endif()

file(GLOB SOURCES "source/*.h" "source/*.cpp")
file(GLOB SOURCES_LEVEL "source/level/*.*")
file(GLOB SOURCES_RENDERER "source/renderer/*.*")
file(GLOB SOURCES_PARSERS "source/parsers/*.*")
file(GLOB SOURCES_CLI "source/cli/*.*")
file(GLOB SHADERS_41 "shaders/41/*.*")
file(GLOB SHADERS_45 "shaders/45/*.*")

# Add Windows icon resource file
if (WIN32)
  list(APPEND SOURCES "assets/igi-editor-icon.rc")
endif()

source_group("level" FILES ${SOURCES_LEVEL})
source_group("renderer" FILES ${SOURCES_RENDERER})
source_group("parsers" FILES ${SOURCES_PARSERS})
source_group("cli" FILES ${SOURCES_CLI})
source_group("shaders/41" FILES ${SHADERS_41})
source_group("shaders/45" FILES ${SHADERS_45})

add_executable(igi-editor ${SOURCES} ${SOURCES_LEVEL} ${SOURCES_RENDERER} ${SOURCES_PARSERS} ${SOURCES_CLI} ${SHADERS_41} ${SHADERS_45})

if (WIN32)
  set_target_properties(igi-editor PROPERTIES
    ICON ${PROJECT_SOURCE_DIR}/assets/igi-editor-icon.ico
    OUTPUT_NAME "igi1ed"
  )
endif()

target_precompile_headers(igi-editor PRIVATE source/pch.h)

# A few translation units pull in very large header graphs (glm, tinygltf/json,
# renderer stacks). MSVC can exhaust virtual memory while trying to map the
# shared PCH for all of them at once, especially on Release builds.
# Let these heavy files compile without the shared PCH to keep memory usage
# stable while preserving PCH speed for the rest of the project.
set_source_files_properties(
  source/app.cpp
  source/renderer/renderer_flat_sky_layers.cpp
  source/renderer/renderer_objects.cpp
  source/renderer/renderer_skydome.cpp
  source/renderer/renderer_splines.cpp
  source/renderer/renderer_terrain.cpp
  source/renderer/wic_loader.cpp
  source/parsers/qvm_parser.cpp
  source/parsers/res_parser.cpp
  source/parsers/mtp_parser.cpp
  source/parsers/tex_parser.cpp
  source/parsers/graph_parser.cpp
  source/cli/asset_extractor.cpp
  source/level/terrain.cpp
  source/parsers/qvm_decompiler.cpp
  source/parsers/qsc_lexer.cpp
  source/parsers/qsc_parser.cpp
  source/parsers/qvm_compiler.cpp
  source/cli/cli_handler.cpp
  source/cli/cli_tests.cpp
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/third_party/glm-1.0.1)
target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/third_party/GL/include)
target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/source)
target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/source/parsers)
target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/source/cli)
target_include_directories(igi-editor PRIVATE ${PROJECT_SOURCE_DIR}/third_party/tinygltf)


if (WIN32)
  # Dynamically select GL library path based on target architecture
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(GL_LIB_DIR "${PROJECT_SOURCE_DIR}/third_party/GL/lib/x64")
  else()
    set(GL_LIB_DIR "${PROJECT_SOURCE_DIR}/third_party/GL/lib/x86")
  endif()
  message("Selected GL library path: ${GL_LIB_DIR}")

  target_link_directories(igi-editor PRIVATE ${GL_LIB_DIR})
  target_link_libraries(igi-editor PRIVATE freeglut glew32 OpenGL32)

  # Increase PCH memory limit to fix compiler heap errors
  if (MSVC)
    target_compile_options(igi-editor PRIVATE "/Zm300" "/bigobj")
  endif()

  # Set runtime library based on configuration
  set_target_properties(igi-editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PROJECT_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_SOURCE_DIR}/bin/Release"
  )

  # Ensure Release builds don't use debug runtime
  set_target_properties(igi-editor PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  )

  # Embed admin manifest
  set(MANIFEST_FILE "${PROJECT_SOURCE_DIR}/igi-editor.exe.manifest")
  if(EXISTS ${MANIFEST_FILE})
    set_target_properties(igi-editor PROPERTIES
      LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${MANIFEST_FILE} /MANIFESTUAC:\"level='asInvoker' uiAccess='false'\""
    )
  endif()

  # Use Console subsystem for both Debug and Release to support CLI mode
  set_target_properties(igi-editor PROPERTIES
    LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
    LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE"
  )

  # Ensure Release mode entry point is correct for console app
  set_target_properties(igi-editor PROPERTIES
    OUTPUT_NAME "igi1ed"
  )

  # Copy required runtime files (DLLs, config, version, shaders) next to the exe
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DLL_SRC_DIR "${PROJECT_SOURCE_DIR}/assets/dlls/x64")
  else()
    set(DLL_SRC_DIR "${PROJECT_SOURCE_DIR}/assets/dlls/x86")
  endif()

  add_custom_command(TARGET igi-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${DLL_SRC_DIR}/freeglut.dll"
      "${DLL_SRC_DIR}/glew32.dll"
      "${PROJECT_SOURCE_DIR}/config.ini"
      "${PROJECT_SOURCE_DIR}/version"
      "$<TARGET_FILE_DIR:igi-editor>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${PROJECT_SOURCE_DIR}/shaders"
      "$<TARGET_FILE_DIR:igi-editor>/shaders"
    COMMENT "Copying runtime files (DLLs, config.ini, version, shaders) to output directory"
  )
else()
  target_link_libraries(igi-editor PRIVATE glut GL GLEW)
endif()


