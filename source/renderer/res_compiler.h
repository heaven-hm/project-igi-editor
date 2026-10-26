#pragma once
#include <string>

// Generates a QSC resource script by scanning the given input directory for MEF models.
// outResName is the relative path that will be written into the BeginResource("...") call.
bool RES_GenerateQSC(const std::string& inputDir, const std::string& outQscPath, const std::string& outResName, std::string& error);

// Compiles a QSC resource script into a binary .res (ILFF IRES) archive.
bool RES_Compile(const std::string& scriptPath, std::string& error);

// Repacks a set of in-memory resources into a binary .res (ILFF IRES) archive.
// Used to patch an existing archive (parse → modify one entry → write all back)
// without needing every loose file on disk. Preserves entry order and names.
#include "res_writer.h"
#include <functional>
bool RES_WriteEntries(const std::vector<RESEntry>& entries, const std::string& outPath, std::string& error);

// Stream `srcResPath` to `outPath`, appending `newEntries` at the end, WITHOUT loading the
// whole archive into memory (safe for 200MB+ archives in the 32-bit editor). Writes a valid
// ILFF/IRES archive (the previously-final BODY gets a normal skip; the very last appended
// BODY gets skip=0). Returns false (with error) on any IO/stream failure. Does NOT modify
// srcResPath. onProgress(done,total) may be null; total = source entry count + newEntries.
bool RES_StreamAppend(const std::string& srcResPath,
                      const std::vector<RESEntry>& newEntries,
                      const std::string& outPath,
                      std::string& error,
                      const std::function<void(size_t,size_t)>& onProgress = nullptr);

// Stream `srcResPath` to `outPath`, replacing entries the engine would consider
// the same resource as a supplied entry, and appending supplied entries the
// source does not define. The source is never modified.
//
// "Same resource" is the engine's identity, not a file name: Resource_Create
// 0x4B59F0 stores the name upper-cased with '\' folded to '/' (src/res/
// resource.c:65-104) and Resource_IsLoaded 0x4B5B90 compares that full string
// with strcmp (src/res/resource.c:136-138). Two archives, and even two entries
// of one archive, may share a basename while being different resources, so a
// basename comparison is never a match test.
bool RES_StreamMerge(const std::string& srcResPath,
                     const std::vector<RESEntry>& entries,
                     const std::string& outPath,
                     std::string& error,
                     const std::function<void(size_t,size_t)>& onProgress = nullptr);

// ---------------------------------------------------------------------------
// Engine texture resolution: which archive supplies a model file's textures
//
// Transcribed from IGI-RE. The engine has no global texture-name map, so the
// editor must not build one:
//
//   ModelFile_Setup 0x4CF080 (src/model/model.c:251-266) loads exactly one
//   texture archive for the model file it is handed:
//       crt_sprintf(path, "%stextures/%s.res", gModelDir, name);   // :261
//       Resource_Load(path, 0);                                    // :262
//       ModelFile_LoadTextures(f);                                 // :263
//       Resource_Unload(path);                                     // :265
//   ModelFile_LoadTextures 0x4CF790 (src/model/model.c:450-472) reads that
//   archive's MTP_TEXF list (:452-464) and turns each name s into
//       crt_sprintf(gModelPath, "LOCAL:textures/%s.tex", s);       // :467
//   which Texture_LoadFile 0x4B24C0 (src/file/texture.c:231-252) passes to
//   Resource_Load 0x4B5F00 -> Resource_IsLoaded 0x4B5B90
//   (src/res/resource.c:247-269, 124-147). Resource_IsLoaded matches the full
//   upper-cased, '/'-separated name (:136-138); there is no basename form and
//   no fallback -- a miss is a fatal Error_Show (src/file/texture.c:245).
//
//   Because the pack is unloaded again at :265, the model's own archive is the
//   only one loaded when its textures are looked up, so a same-name entry in
//   any other archive can never win. Retail archives really do collide:
//   missions/location0/LEVEL7/textures/LEVEL7.RES and
//   missions/location0/COMMON/textures/location0.res both store members named
//   "LOCAL:textures/<id>.tex" (557 and 274 entries, measured; e.g. 001_02_1.tex
//   is a 128x128 sheet in the first and a 128x32 strip in the second).
//
//   LevelStart (src/game/game.c:411-418) drives two model files per level, each
//   under its own Model_SetDir:
//     COMMON set  dir "LOCAL:missions/location0/common/", file location0.mtp,
//                 textures location0.res   (loaded first,  :411-414)
//     LEVEL  set  dir "LOCAL:missions/location0/level<N>/", file level<N>.mtp,
//                 textures level<N>.res     (loaded second, :416-418)
//   A model of the common set must therefore resolve against location0.res and
//   never against the level archive, even though both may define the same
//   member name.
// ---------------------------------------------------------------------------

// ModelFile_Setup 0x4CF080 (src/model/model.c:261): "%stextures/%s.res".
std::string RES_ModelTextureArchiveName(const std::string& modelDir,
                                        const std::string& modelName);

// Model directories and model file base names of the retail mission table, as
// LevelStart feeds them to Model_SetDir/ModelFile_Load (src/game/game.c:411-418).
std::string RES_LevelModelDir(int levelNo);    // "LOCAL:missions/location0/level7/"
std::string RES_LevelModelName(int levelNo);   // "level7"
std::string RES_CommonModelDir();              // "LOCAL:missions/location0/common/"
std::string RES_CommonModelName();             // "location0"

// "LOCAL:" device name -> host path. LocalDevice_Open 0x50A370 hands everything
// after the device prefix straight to crt_fopen (src/sysmisc/localdev.c:195), so
// "LOCAL:missions/location0/level7/textures/level7.res" is the file
// "<igiRoot>\missions\location0\level7\textures\level7.res".
std::string RES_LocalPathToDisk(const std::string& igiRoot,
                                const std::string& localPath);

// The one (archive, member) pair the engine would read for one MTP_TEXF entry of
// one model file. `archivePath` is a host path, `memberName` the engine resource
// name. There is exactly one candidate by construction -- a caller must not
// search other archives and must not strip a suffix when the member is absent.
struct RESModelTextureTarget {
    std::string archivePath;
    std::string memberName;
};

// ModelFile_Setup 0x4CF080 + ModelFile_LoadTextures 0x4CF790 for one model file.
RESModelTextureTarget RES_ResolveModelTextureTarget(const std::string& igiRoot,
                                                    const std::string& modelDir,
                                                    const std::string& modelName,
                                                    const std::string& textureId);

// The same for a level's own model set (src/game/game.c:416-418).
RESModelTextureTarget RES_ResolveLevelModelTextureTarget(const std::string& igiRoot,
                                                         int levelNo,
                                                         const std::string& textureId);

// The same for the location's common model set (src/game/game.c:411-414).
RESModelTextureTarget RES_ResolveCommonModelTextureTarget(const std::string& igiRoot,
                                                          const std::string& textureId);
