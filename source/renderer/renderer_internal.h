/******************************************************************************
 * @file    renderer_internal.h
 * @brief   Shared include set for the Renderer implementation modules
 *          (renderer.cpp core + renderer_draw.cpp HUD pass).
 *****************************************************************************/
#pragma once

/******************************************************************************
 * @file    renderer.cpp
 * @brief   main renderer
 *   GL 4.1: need manually set binding point of uniform blocks
 *                             texture unit of sample object
 *   GL 4.5: binding point, texture unit can specified in shaders
 *****************************************************************************/

#include "config.h"
#include "logger.h"
#include "utils.h"
#include "pch.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdio>

#include <freeglut.h>
#include "../level/task_schema.h"
#include "fnt_parser.h"
using namespace TaskSchemaNS;
