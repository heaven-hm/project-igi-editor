/******************************************************************************
 * @file    pch.h
 * @brief   procompiled head
 *****************************************************************************/

#pragma once

#if defined(_WIN32)
# define	NOMINMAX
#endif

#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <glew.h>

#if defined(__linux__)
# define MAX_PATH 256
#endif

#include <assert.h>
#include <exception>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "common.h"

#include "level/level_common.h"
#include "level/flat_sky_layer.h"
#include "level/terrain.h"
#include "level/level.h"

#include "renderer/gl_helper.h"
#include "renderer/renderer_skydome.h"
#include "renderer/renderer_flat_sky_layer.h"
#include "renderer/renderer_flat_sky_layers.h"
#include "renderer/renderer_terrain.h"
#include "renderer/renderer.h"

#include "app.h"
