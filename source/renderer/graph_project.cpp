#include "pch.h"
#include "graph_overlay.h"

bool GraphWorldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                        float viewportW, float viewportH, glm::vec2& outScreen) {
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) return false;  // behind the camera

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x = (ndc.x * 0.5f + 0.5f) * viewportW;
    outScreen.y = (-ndc.y * 0.5f + 0.5f) * viewportH;  // y flipped: screen origin top-left
    return true;
}

int GRAPH_PickNode(const GraphFile& graph, const glm::mat4& viewProj,
                   float mouseX, float mouseY, float viewportW, float viewportH,
                   float thresholdPx) {
    int   bestId   = -1;
    float bestDist = thresholdPx * thresholdPx;  // compare squared distances
    for (const GraphNode& n : graph.nodes) {
        glm::vec2 s;
        if (!GraphWorldToScreen(viewProj,
                                glm::vec3((float)n.x, (float)n.y, (float)n.z),
                                viewportW, viewportH, s))
            continue;
        const float dx = s.x - mouseX;
        const float dy = s.y - mouseY;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDist) {
            bestDist = d2;
            bestId   = n.id;
        }
    }
    return bestId;
}
