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
#include "res_parser.h"
bool RES_WriteEntries(const std::vector<RESEntry>& entries, const std::string& outPath, std::string& error);
