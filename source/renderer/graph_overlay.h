/******************************************************************************
 * @file    graph_overlay.h
 * @brief   World-space navigation-graph overlay math for the editor.
 *
 * GraphWorldToScreen and GRAPH_PickNode are pure (glm only) and unit-tested.
 * The actual GL drawing of the overlay lives in Renderer::DrawGraphOverlayInternal
 * (renderer_draw.cpp), which reuses the renderer's matrices and screen-space state.
 *****************************************************************************/

#pragma once
#include <glm/glm.hpp>
#include "graph_writer.h"

// Project a world-space point through `viewProj` to screen pixels (origin
// top-left). Returns false if the point is behind the camera (clip.w <= 0);
// in that case outScreen is left unchanged.
bool GraphWorldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                        float viewportW, float viewportH, glm::vec2& outScreen);

// Hit-test: return the id of the node nearest to (mouseX,mouseY) in screen
// space whose screen distance is within thresholdPx, or -1 if none. mouseX/
// mouseY use the same top-left origin as GraphWorldToScreen. Nodes behind the
// camera are ignored. Ties resolve to the first matching node.
int GRAPH_PickNode(const GraphFile& graph, const glm::mat4& viewProj,
                   float mouseX, float mouseY, float viewportW, float viewportH,
                   float thresholdPx);
