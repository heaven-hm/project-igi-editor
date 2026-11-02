#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace igi {

enum class VisualIntegrityStatus {
    kPass,
    kFail,
    kInconclusive,
};

struct VisualIntegrityFinding {
    std::string rule;
    std::string part;
    int viewIndex = -1;
    int observedPixels = 0;
    int requiredPixels = 0;
    std::string reason;
    std::string view;
    std::string evidence;
};

// Geometry/material inventory emitted by the target-only renderer pass. IDs
// are local to one capture and map directly to `VisualIntegrityView::partIds`.
struct VisualIntegrityPart {
    int id = 0;
    int vertexCount = 0;
    int triangleCount = 0;
    int materialSlot = -1;
    int alphaMode = 0;  // 0=opaque, 1=mask, 2=blend
    std::string textureIdentity;
    // A non-empty authored texture identity must be backed by a live GL
    // texture.  This distinguishes normal geometry coverage from the
    // untextured fallback that the visible renderer uses on load failure.
    bool textureResolved = true;
    // Fraction of texels whose RGB channels differ enough to carry authored
    // colour information.  It is measured from the live diffuse texture only
    // for capture evidence, not inferred from the model name.
    float textureChromaticPixelRatio = 0.0f;
    glm::vec3 localBoundsMin{0.0f};
    glm::vec3 localBoundsMax{0.0f};
};

struct VisualIntegrityView {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> targetMask;
    std::vector<int> partIds;
    std::vector<float> sceneDepth;
    std::vector<float> targetDepth;
    // Rendered framebuffer RGB values, one triplet per target-mask pixel.
    // Empty retains compatibility for captures that cannot read colour.
    std::vector<uint8_t> renderedRgb;
    // False when the normal frame used a deformed/skinned mesh but this
    // capture only has a static projection. Such a projection cannot make
    // silhouette or submesh-coverage claims about the visible frame.
    bool geometryProjectionMatchesRenderedFrame = true;
    std::string name;
    std::string sourceFramePath;
    std::string overlayPath;
    // A capture command selects an authored task. The caller records whether
    // the runtime transform still agrees with that authored anchor.
    bool transformMatchesAuthored = true;
    // Two samples with the same non-negative group were rendered from the
    // same camera pose and can therefore be compared for temporal stability.
    int temporalGroup = -1;
};

struct VisualIntegrityInput {
    std::vector<int> expectedPartIds;
    std::vector<VisualIntegrityPart> expectedParts;
    // Parts with an independent attachment instance are not allowed to be
    // satisfied by another root surface.  Root parts may be self-occluded by
    // authored target geometry and are evaluated only when they are exposed.
    std::vector<int> strictPartIds;
    std::vector<VisualIntegrityView> views;
    int minimumPartPixels = 1;
    // Minimum visible fraction of a part's target-only projected area for a
    // view to count as covered. Zero retains count-only compatibility.
    float minimumPartCoverageRatio = 0.0f;
    // Shared calibration floor for the fraction of the expected geometry
    // inventory that produces visible target fragments across the capture.
    float minimumObservedPartRatio = 0.0f;
    int minimumInteriorHolePixels = 16;
    float depthEpsilon = 0.0001f;
    bool requireTemporalEvidence = false;
    // Fractional target-area change allowed between repeated same-camera
    // samples. Camera motion is never compared across temporal groups.
    float maximumTemporalAreaDelta = 0.05f;
};

struct VisualIntegrityResult {
    VisualIntegrityStatus status = VisualIntegrityStatus::kInconclusive;
    int viewsChecked = 0;
    int viewsPassed = 0;
    int viewsFailed = 0;
    int partsExpected = 0;
    int partsObserved = 0;
    std::vector<VisualIntegrityFinding> findings;
};

VisualIntegrityResult EvaluateVisualIntegrity(const VisualIntegrityInput& input);
const char* VisualIntegrityStatusName(VisualIntegrityStatus status);
std::string VisualIntegrityJson(const VisualIntegrityResult& result);

}  // namespace igi
