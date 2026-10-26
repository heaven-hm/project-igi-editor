#pragma once
//
// mef_writer.h — OBJ -> binary .MEF writer (IGI 1 / ILFF container).
//
// This is the write half of the pipeline described by issue #29
// ("OBJ -> MEF Importer").  It is deliberately self-contained: it does not use
// tinyobjloader or any other third-party parser, so no new dependency is added
// to the build.
//
// Every rule this writer encodes is cited to the verified decompilation of the
// retail IGI.exe at /Users/haseeb-mir/Documents/Code/IGI-RE, as
// `function_name HOOK(0xADDR), src/path/file.c:LINE`.  See
// docs/igi-re-parity/MEF_IMPORT.md for the derived layout, the evidence, and
// the list of things that are NOT proven.
//
// Scope: STATIC meshes only (issue #29: "Start with static buildings only — no
// bone complexity").  A skeletal source model is refused explicitly rather
// than silently written without its skeleton.

#include <cstdint>
#include <string>
#include <vector>

#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>

#include "renderer/mef_native.h"

namespace mef_writer {

// ---------------------------------------------------------------------------
// OBJ input
// ---------------------------------------------------------------------------

// Sentinel for "this OBJ face corner has no texture / normal index".
constexpr uint32_t kNoObjIndex = 0xFFFFFFFFu;

struct ObjFace {
    uint32_t v[3]{kNoObjIndex, kNoObjIndex, kNoObjIndex};   // position index
    uint32_t vt[3]{kNoObjIndex, kNoObjIndex, kNoObjIndex};  // texcoord index
    uint32_t vn[3]{kNoObjIndex, kNoObjIndex, kNoObjIndex};  // normal index
};

struct ObjMesh {
    std::vector<glm::vec3> positions;   // "v"
    std::vector<glm::vec2> texcoords;   // "vt"
    std::vector<glm::vec3> normals;     // "vn"
    std::vector<ObjFace> faces;         // triangulated (n-gons fanned)
    std::vector<std::string> faceMaterial;  // per face, "" when no usemtl seen
    std::vector<std::string> materials;     // usemtl names in order of first use
    size_t degenerateFaces = 0;             // triangles dropped as degenerate
    size_t negativeIndexCorners = 0;        // face corners that used negative (relative) indices
    std::string sourceName;
};

// Parse OBJ text.  Supports v / vt / vn / f / g / o / s / usemtl / mtllib / #
// and negative (relative) indices; f corners may be v, v/vt, v//vn or v/vt/vn.
// Polygons with more than three corners are fanned around the first corner.
bool ParseObjText(const std::string& text, ObjMesh* out, std::string* error);

// Read + parse an OBJ file.
bool ParseObjFile(const std::string& path, ObjMesh* out, std::string* error);

// ---------------------------------------------------------------------------
// Intermediate MEF model (what the serialiser actually writes)
// ---------------------------------------------------------------------------

struct MefVertex {
    glm::vec3 pos{0.f};      // raw MEF units, written to XTRV +0
    glm::vec3 normal{0.f};   // XTRV +12
    glm::vec2 uv{0.f};       // XTRV +24
    glm::vec2 uv2{0.f};      // XTRV +32, format 3 only
};

struct MefGroup {
    std::vector<uint32_t> indices;  // absolute indices into MefModel::vertices
    int texture = -1;               // DNER texture slot, -1 => 0xFFFF (untextured)
    int lightmap = -1;              // DNER lightmap slot (format 3), -1 => 0xFFFF
    int sorted = 0;                 // DNER `sorted` hint (0 = draw immediately)
    int flags = 0;                  // DNER flags byte
    int alpha = 0;                  // DNER alpha byte
};

// XTVC record: 16 bytes.  Collision vertex.
struct MefCollVertex {
    glm::vec3 pos{0.f};
    uint32_t bone = 0;  // +12; 0 in every static retail model measured
};

// ECFC record: 8 bytes.  Collision triangle; `surface` indexes MefModel::collSurfaces.
struct MefCollFace {
    uint16_t a = 0, b = 0, c = 0;
    uint16_t surface = 0;
};

// TAMC record: 12 bytes.  Collision surface (material) descriptor.
struct MefCollSurface {
    uint32_t opacity = 0;
    uint16_t portal = 0xFFFF;
    int16_t index = -1;
    uint16_t material = 0xFFFF;
    uint16_t unknown = 0;
};

struct MefModel {
    uint32_t format = 0;  // HSEM format: 0 rigid, 3 lightmapped (1 = skeletal)
    std::vector<MefVertex> vertices;
    std::vector<MefGroup> groups;

    bool hasCollision = false;
    std::vector<MefCollVertex> collVertices;
    std::vector<MefCollFace> collFaces;
    std::vector<MefCollSurface> collSurfaces;

    // Raw ATTA chunk payload copied verbatim from a reference MEF (may be empty).
    std::vector<uint8_t> attachmentBytes;

    // Where HSEM center[3] comes from (copied out of WriteOptions by the
    // Build* functions so SerialiseMef stays options-free).
    bool useBoundsCentrePivot = true;
    glm::vec3 pivot{0.f};

    // Informational only (printed by the CLI).
    size_t sourceTriangles = 0;
    bool windingReversed = false;
};

struct WriteOptions {
    // Output HSEM format.  0 = rigid static (the default for OBJ import: an OBJ
    // carries no lightmap atlas UVs, so format 3 would need invented uv2 data).
    uint32_t format = 0;

    // OBJ/Blender front faces are counter-clockwise seen from outside; retail
    // packed DNER triangles are wound the other way (measured on the shipped
    // corpus — see MEF_IMPORT.md).  Reversing {a,b,c} -> {a,c,b} makes an OBJ
    // mesh match retail.  Set false to write the OBJ winding unchanged.
    bool reverseWinding = true;

    // Emit the HSMC/XTVC/ECFC/TAMC/HPSC collision chunks generated from the mesh.
    bool emitCollision = true;

    // Copy the ATTA chunk of `sourceMefPath` verbatim into the output.  Has no
    // effect unless `sourceMefPath` names a readable static MEF.
    bool preserveAttachments = true;
    std::string sourceMefPath;

    // HSEM center[3]: the model-space pivot the engine measures LOD distance
    // from.  Default is the mesh bounding-box centre; set `pivot` explicitly
    // (e.g. vec3(0)) to override.
    bool useBoundsCentrePivot = true;
    glm::vec3 pivot{0.f};

    // Reject an OBJ whose vertex count cannot be represented by the 16-bit
    // DNER group fields.  Raise only for deliberate stress testing.
    uint32_t maxVertices = 0xFFFF;
};

// Build a MefModel from a parsed OBJ.  No file I/O.
bool BuildMefFromObj(const ObjMesh& obj, const WriteOptions& options, MefModel* out,
                     std::string* error);

// Build a MefModel from an already-parsed MEF (used by the corpus round-trip
// test and by any future "replace the mesh, keep the rest" flow).
bool BuildMefFromGeometry(const ParsedGeometry& geometry, const WriteOptions& options,
                          MefModel* out, std::string* error);

// Serialise a MefModel to ILFF bytes.  No file I/O.
bool SerialiseMef(const MefModel& model, std::vector<uint8_t>* out, std::string* error);

// One-line summary for console output, e.g.
//   "MEF: 8 vertices, 12 triangles, 1 group, format 0, collision 8/12, 148 bytes"
std::string DescribeMefModel(const MefModel& model);

// Full pipeline: OBJ file -> MEF file.  Returns false and sets *error on any
// failure; on failure no output file is created or left behind.
bool WriteMefFromObj(const std::string& objPath, const std::string& outMefPath,
                     std::string* error);
bool WriteMefFromObj(const std::string& objPath, const std::string& outMefPath,
                     const WriteOptions& options, std::string* error);

// Same, but returns the bytes instead of writing a file.
bool WriteMefBytesFromObj(const std::string& objPath, const WriteOptions& options,
                          std::vector<uint8_t>* out, std::string* error);

// Full pipeline from an already-parsed MEF: build, lift the reference's ATTA
// chunk when `options.sourceMefPath` is set, then serialise.
bool WriteMefBytesFromGeometry(const ParsedGeometry& geometry, const WriteOptions& options,
                               std::vector<uint8_t>* out, std::string* error);

// ---------------------------------------------------------------------------
// CLI glue for `igi1ed.exe --mef <input.obj> --import-mef <output.mef>`
//
// The parsing lives here (and not in source/cli/cli_handler.cpp) so that it is
// covered by the host test lane, which does not compile cli_handler.cpp.
// ---------------------------------------------------------------------------

struct ImportMefRequest {
    std::string inputObjPath;
    std::string outputMefPath;
    WriteOptions options;
};

// True when the argument list contains --import-mef (this is what puts the
// editor into CLI mode for this command).
bool IsImportMefInvocation(int argc, char** argv);

// Parse argv.  Accepted forms:
//   --mef <input.obj> --import-mef <output.mef>
//   --import-mef <input.obj> <output.mef>
// Optional: --source-mef <ref.mef>  preserve ATTA from a reference static MEF
//           --mef-format <0|3>      output HSEM format (default 0)
//           --keep-winding          do not reverse OBJ winding
//           --no-collision          skip HSMC/XTVC/ECFC/TAMC/HPSC
//           --pivot-origin          HSEM center = (0,0,0)
bool ParseImportMefRequest(int argc, char** argv, ImportMefRequest* out, std::string* error);

// Parse + run, printing progress to stdout/stderr.  Returns a process exit code.
int RunImportMefCli(int argc, char** argv);

}  // namespace mef_writer
