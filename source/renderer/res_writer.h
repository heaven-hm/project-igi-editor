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

struct RESEntry {
    std::string name;               // Resource name/path
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
bool RES_BuildIndex(const std::string& filepath,
                    std::unordered_map<std::string, ResEntryInfo>& out_index,
                    std::string& error);

// Read a single entry by offset/size.  Returns empty on I/O error.
std::vector<uint8_t> RES_ReadEntry(const std::string& filepath, const ResEntryInfo& info);
