cmake_minimum_required(VERSION 3.16)
# target_precompile_headers
#   Added in version 3.16

project (igi_terrain 
  VERSION 1.0
  LANGUAGES C CXX)

# Designated initializers, introduced in C++20
set(CMAKE_CXX_STANDARD 20)

# enable FOLDER property
#   seperate projects into different groups
SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
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
file(GLOB SHADERS_41 "shaders/41/*.*")
file(GLOB SHADERS_45 "shaders/45/*.*")

source_group("level" FILES ${SOURCES_LEVEL})
source_group("renderer" FILES ${SOURCES_RENDERER})
source_group("shaders/41" FILES ${SHADERS_41})
source_group("shaders/45" FILES ${SHADERS_45})

add_executable(terrain ${SOURCES} ${SOURCES_LEVEL} ${SOURCES_RENDERER} ${SHADERS_41} ${SHADERS_45})

target_precompile_headers(terrain PRIVATE source/pch.h)

target_include_directories(terrain PRIVATE ${PROJECT_SOURCE_DIR}/third_party/glm-1.0.1)
target_include_directories(terrain PRIVATE ${PROJECT_SOURCE_DIR}/third_party/GL/include)
target_include_directories(terrain PRIVATE ${PROJECT_SOURCE_DIR}/source)


if (WIN32)
  target_link_directories(terrain PRIVATE ${PROJECT_SOURCE_DIR}/third_party/GL/lib/x64)
  target_link_libraries(terrain PRIVATE freeglut glew32 OpenGL32)
else()
  target_link_libraries(terrain PRIVATE glut GL GLEW)
endif()
