/******************************************************************************
 * @file    res_parser.h
 * @brief   RES (ILFF-based resource archive) parser
 *****************************************************************************/

#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

#include "res_chunks.h"

struct RESEntry {
    std::string name;               // Resource name/path, as the NAME chunk stores it
    std::vector<uint8_t> data;      // Raw binary data
};

struct RESFile {
    std::vector<RESEntry> entries;
    bool valid = false;
    std::string error;
};

// Stream a RES file, invoking onEntry(name, data, size) for each resource without
// holding the whole archive in memory. Returns true if the file header was valid.
// The (data, size) pointer is only valid for the duration of the callback.
bool RES_ForEachEntry(const std::string& filepath,
                      const std::function<void(const std::string&, const uint8_t*, size_t)>& onEntry,
                      std::string& error);

// Parse a RES file, extracting all named resources
RESFile RES_Parse(const std::string& filepath);

// Extract a single resource by name (returns empty vector if not found)
std::vector<uint8_t> RES_Extract(const std::string& filepath, const std::string& resourceName);

// ---------------------------------------------------------------------------
// Indexed random-access reader — reads only chunk headers to build a name→offset
// map, then lets callers seek-and-read individual entries without holding the
// whole archive in memory.  Ideal for the 32-bit process: peak allocation is one
// entry (~5 MB) rather than the entire archive (200+ MB).
// ---------------------------------------------------------------------------
struct ResEntryInfo {
    size_t data_offset; // byte offset of entry data inside the .res file
    size_t data_size;   // byte length of entry data
};

// Build a name→info index from the .res file headers.  Fast: only reads 16-byte
// chunk headers plus NAME chunks.  Call once after a level load.
// Returns true if the file is valid.
//
// The key is the engine's resource identity, not a file name: the NAME chunk
// text upper-cased with '\' folded to '/' (Resource_Create 0x4B59F0,
// src/res/resource.c:65-104). Retail level archives store
// "LOCAL:textures/<id>.tex" and "LOCAL:models/<name>.mef" verbatim, so a lookup
// must use that same string — see igi_res::TextureMemberName /
// igi_res::ModelMemberName (src/model/model.c:467, :364). Two archives may hold
// the same resource name; the archive is selected by the caller, exactly as
// ModelFile_Setup selects "%stextures/%s.res" (src/model/model.c:261,
// HOOK(0x4CF080)). There is no basename key and no cross-archive name search.
bool RES_BuildIndex(const std::string& filepath,
                    std::unordered_map<std::string, ResEntryInfo>& out_index,
                    std::string& error);

// Resource_IsLoaded name test (0x4B5B90, src/res/resource.c:124-147): the name
// is upper-cased with '\' folded to '/', then compared in full. Returns false
// when the archive does not define that resource — the engine has no fallback.
bool RES_FindIndexEntry(const std::unordered_map<std::string, ResEntryInfo>& index,
                        const std::string& name,
                        ResEntryInfo& out);

// Resource_Create name normalisation (0x4B59F0, src/res/resource.c:65-104):
// upper-case, '\' -> '/'. Exposed so callers can compare names the way the
// engine's resource list does.
std::string RES_NormalizeResourceName(const std::string& name);

// Read a single entry by offset/size.  Returns empty on I/O error.
std::vector<uint8_t> RES_ReadEntry(const std::string& filepath, const ResEntryInfo& info);

// IGI_PROFILE-only diagnostic: how many archive entries have been read off disk
// through RES_ReadEntry since process start.  Every call opens the .res and reads
// the whole entry, so this COUNT -- not a wall-clock time -- is what a
// memoization regression changes (the LOD-header cache in
// renderer_objects_mesh.cpp took the per-frame object pass from ~2832 of these to
// zero), and unlike a time it cannot be perturbed by host load.  Counted only
// when IGI_PROFILE is set, so an ordinary run pays nothing.
uint64_t RES_EntryReadCount();
