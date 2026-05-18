/******************************************************************************
 * @file    res_parser.h
 * @brief   RES (ILFF-based resource archive) parser
 *****************************************************************************/

#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct RESEntry {
    std::string name;               // Resource name/path
    std::vector<uint8_t> data;      // Raw binary data
};

struct RESFile {
    std::vector<RESEntry> entries;
    bool valid = false;
    std::string error;
};

// Parse a RES file, extracting all named resources
RESFile RES_Parse(const std::string& filepath);

// Extract a single resource by name (returns empty vector if not found)
std::vector<uint8_t> RES_Extract(const std::string& filepath, const std::string& resourceName);
