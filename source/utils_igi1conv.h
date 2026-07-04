/******************************************************************************
 * @file    utils_igi1conv.h
 * @brief   Shared runner for the bundled igi1conv.exe asset converter.
 *          All file conversion in the editor MUST go through this helper
 *          instead of in-process parsers.
 *****************************************************************************/
#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace igi1conv {

// Path to the bundled converter (editor/tools/igi1conv/igi1conv.exe).
std::string GetExePath();

// Generic runner. Spawns igi1conv.exe with the given argument string and
// waits up to timeoutMs milliseconds. Returns true on exit code 0.
// On failure, `err` is populated with a human-readable reason and (when
// available) the captured stdout/stderr of the tool.
bool Run(const std::string& args, std::string& err, DWORD timeoutMs = 120000);

// ── High-level helpers ──────────────────────────────────────────────────────
// Each one constructs the right `igi1conv <cmd>` invocation and either
// returns the produced path / data, or returns false with `err` set.

struct ExtractResult {
    std::vector<std::string> entry_names;
    std::string out_dir;
    std::string err;
};

// `igi1conv res list <f.res>` — parses the names out of stdout.
std::vector<std::string> ResList(const std::string& resPath, std::string& err);

// `igi1conv res extract <f.res> -o <out_dir>` — extracts every entry.
bool ResExtract(const std::string& resPath, const std::string& outDir, std::string& err);

// `igi1conv res extract <f.res> --file <name> -o <out_dir>`.
bool ResExtractOne(const std::string& resPath, const std::string& entryName,
                   const std::string& outDir, std::string& err);

// `igi1conv res append <src> <files...> -o <dst> [--prefix ...]`.
bool ResAppend(const std::string& srcRes,
               const std::vector<std::string>& newFiles,
               const std::string& dstRes,
               const std::string& prefix,
               std::string& err);

// `igi1conv res pack <dir> <out.res>`.
bool ResPack(const std::string& dir, const std::string& outRes, std::string& err);

// `igi1conv res repack <orig.res> <dir> -o <out.res>` — rebuild origRes into
// outRes, replacing each entry whose basename matches a file in `dir` with that
// file's bytes while preserving the original entry NAMES verbatim. Used to write
// edited lightmaps back into lightmaps.res with game-valid nested entry names.
bool ResRepack(const std::string& origRes, const std::string& dir,
               const std::string& outRes, std::string& err);

// `igi1conv dat export <f.dat> [-o <out.json>] [--text]` — returns the JSON
// path written. If outJson is empty, a temp file is used.
std::string DatExportJson(const std::string& datPath, const std::string& outJson,
                          std::string& err, bool asText = false);

// `igi1conv dat to-mtp <f.dat> -o <out.mtp>`.
bool DatToMtp(const std::string& datPath, const std::string& outMtp, std::string& err);

// `igi1conv mtp dump <f.mtp> -o <out.json>` — returns the JSON path written.
std::string MtpDumpJson(const std::string& mtpPath, const std::string& outJson,
                        std::string& err);

// `igi1conv mtp info <f.mtp>` — returns the captured info text.
std::string MtpInfo(const std::string& mtpPath, std::string& err);

// `igi1conv graph export <f.dat> -o <out.json>`.
std::string GraphExportJson(const std::string& datPath, const std::string& outJson,
                            std::string& err);

// `igi1conv graph info <f.dat>`.
std::string GraphInfo(const std::string& datPath, std::string& err);

// `igi1conv tex decode <f.tex|.spr|.pic> -o <out_dir>` — decodes a texture
// to its raw form (TGA/PNG in the out dir). Useful for cursor/UI use.
bool TexDecode(const std::string& texPath, const std::string& outDir, std::string& err);

// `igi1conv tex to-png <f> [-o <out.png>] [--resize W H]`.
bool TexToPng(const std::string& texPath, const std::string& outPng, std::string& err);

// `igi1conv tex to-tga <f> [-o <out.tga>]`.
bool TexToTga(const std::string& texPath, const std::string& outTga, std::string& err);

// `igi1conv tex info <f>`.
std::string TexInfo(const std::string& texPath, std::string& err);

// `igi1conv qvm decompile <f.qvm> -o <out.qsc>`.
bool QvmDecompile(const std::string& qvmPath, const std::string& outQsc, std::string& err);

// `igi1conv qvm info <f.qvm>`.
std::string QvmInfo(const std::string& qvmPath, std::string& err);

// `igi1conv qvm disasm <f.qvm> [-o <out.txt>]`.
bool QvmDisasm(const std::string& qvmPath, const std::string& outTxt, std::string& err);

// `igi1conv qsc compile <f.qsc> -o <f.qvm>`.
bool QscCompile(const std::string& qscPath, const std::string& outQvm, std::string& err);

// `igi1conv qsc validate <f.qsc>`.
bool QscValidate(const std::string& qscPath, std::string& err);

// ─── lightmap / olm ──────────────────────────────────────────────────────

// `igi1conv lightmap resolve --model <id> --qsc <path> --task-id <id>`.
// Returns the resolved .olm file paths, or empty with `err` set (no
// binding, no .olm files on disk, exit-code failure, or unexpected output
// format). `taskId` disambiguates exactly — the editor always knows the
// exact placement, so the CLI's ambiguous-match exit code never triggers.
std::vector<std::string> LightmapResolve(const std::string& modelId,
                                         const std::string& qscPath,
                                         const std::string& taskId,
                                         std::string& err);

// Same as LightmapResolve but disambiguates by nearest authored position
// (`--pos X,Y,Z`) instead of task id — used for nested/ATTA objects whose
// taskId is the non-unique literal "-1".
std::vector<std::string> LightmapResolveByPos(const std::string& modelId,
                                              const std::string& qscPath,
                                              double x, double y, double z,
                                              std::string& err);

// Pure parser for `lightmap resolve`'s stdout — exposed for unit testing
// without spawning a process. Extracts the indented .olm file paths
// following the "N .olm file(s):" line. Returns empty with `err` set if the
// format doesn't match (e.g. unexpected output, or no .olm files listed).
std::vector<std::string> ParseLightmapResolveStdout(const std::string& stdoutText, std::string& err);

// `igi1conv olm to-png <input.olm> -o <out.png>`.
bool OlmToPng(const std::string& olmPath, const std::string& outPng, std::string& err);

// `igi1conv lightmap recalc ...` — re-light a moved/rotated object's baked
// .olm files in place by modulating them per-channel by the new sun angle
// (preserving the original shadow detail). rotOrig/rotNew are Euler radians
// (x,y,z); sunDir/sunColor/ambient are world-space vec3s. Returns true on
// success (at least one .olm rewritten).
bool LightmapRecalc(const std::string& modelId, const std::string& qscPath,
                    const std::string& taskId, const std::string& mefPath,
                    const glm::dvec3& rotOrig, const glm::dvec3& rotNew,
                    const glm::vec3& sunDir, const glm::vec3& sunColor,
                    const glm::vec3& ambient, std::string& err);

// `igi1conv fnt export <f.fnt> -o <out.png>`.
bool FntExportPng(const std::string& fntPath, const std::string& outPng, std::string& err);

// `igi1conv fnt info <f.fnt>`.
std::string FntInfo(const std::string& fntPath, std::string& err);

// `igi1conv terrain export-lmp <f.lmp> -o <out.pgm>`.
bool TerrainExportLmp(const std::string& lmpPath, const std::string& outPgm, std::string& err);

// `igi1conv terrain export-ctr <f.ctr> -o <out.json>`.
std::string TerrainExportCtr(const std::string& ctrPath, const std::string& outJson,
                             std::string& err);

// `igi1conv terrain info <f>`.
std::string TerrainInfo(const std::string& path, std::string& err);

// `igi1conv mef export <f.mef> -o <out.obj> [--dat ... --texdir ...]`.
bool MefExportObj(const std::string& mefPath, const std::string& outObj,
                  const std::string& datFile, const std::string& texDir,
                  std::string& err);

// `igi1conv mef info <f.mef>`.
std::string MefInfo(const std::string& mefPath, std::string& err);

// `igi1conv iff convert <src.iff> <out_dir>` — exports .BEF files + Anims.qsc.
bool IffConvert(const std::string& iffPath, const std::string& outDir, std::string& err);

// `igi1conv wav convert <src.wav> -o <out.wav>` — decodes the game's ILSF
// container (raw or IMA-ADPCM) to a standard playable PCM .wav.
bool WavConvert(const std::string& srcWav, const std::string& outWav, std::string& err);

// Generates a unique temp file path under the system temp dir.
std::string MakeTempPath(const std::string& suffix);

} // namespace igi1conv
