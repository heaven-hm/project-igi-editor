#include "pch.h"
#include "decompiler.h"
#include "utils.h"
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <iostream>

Decompiler::Decompiler() {}

Decompiler::~Decompiler() {}

void Decompiler::SetOutputCallback(std::function<void(const std::string&)> callback) {
    output_callback_ = callback;
}

bool Decompiler::Decompile(const std::string& qvm_path, const std::string& qsc_output_path) {
    namespace fs = std::filesystem;

    // Read QCompiler path from config
    std::string qcompiler_path = Config::Get().compilerPath;

    // Use local temp directory for writing instead of AppData
    std::string local_temp = Utils::GetExeDirectory() + "\\temp_decompile";
    std::string decompile_input_dir = local_temp + "\\input";
    std::string decompile_output_dir = local_temp + "\\output";
    std::string decompile_bat = qcompiler_path + "\\decompile.bat";

    // Ensure temp directories exist
    fs::create_directories(decompile_input_dir);
    fs::create_directories(decompile_output_dir);

    // Ensure directories exist
    if (!fs::exists(decompile_input_dir)) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Decompile input directory not found: " + decompile_input_dir);
        return false;
    }
    if (!fs::exists(decompile_output_dir)) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Decompile output directory not found: " + decompile_output_dir);
        return false;
    }
    if (!fs::exists(decompile_bat)) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: decompile.bat not found: " + decompile_bat);
        return false;
    }

    // Clean input and output directories before decompiling
    try {
        int cleaned_input = 0;
        int cleaned_output = 0;
        for (const auto& entry : fs::directory_iterator(decompile_input_dir)) {
            fs::remove_all(entry.path());
            cleaned_input++;
        }
        for (const auto& entry : fs::directory_iterator(decompile_output_dir)) {
            fs::remove_all(entry.path());
            cleaned_output++;
        }
        if (output_callback_) output_callback_("[Decompiler] Cleaned input dir (removed " + std::to_string(cleaned_input) + " items), output dir (removed " + std::to_string(cleaned_output) + " items)");
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Decompiler] WARNING: Error cleaning directories: " + std::string(e.what()));
    }

    // Copy qvm file to decompile input directory
    std::string qvm_filename = fs::path(qvm_path).filename().string();
    std::string input_qvm = decompile_input_dir + "\\" + qvm_filename;

    try {
        if (fs::exists(input_qvm)) fs::remove(input_qvm);
        fs::copy_file(qvm_path, input_qvm, fs::copy_options::overwrite_existing);
        if (output_callback_) output_callback_("[Decompiler] Copied " + qvm_path + " to " + input_qvm);
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Failed to copy qvm file: " + std::string(e.what()));
        return false;
    }

    // Run decompile.bat
    std::string cmd = "cd /d \"" + qcompiler_path + "\" && decompile.bat";
    if (output_callback_) output_callback_("[Decompiler] Running: " + cmd);

    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Failed to execute decompile.bat");
        return false;
    }

    char buffer[128];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    _pclose(pipe);

    if (output_callback_) output_callback_("[Decompiler] Output: " + output);

    // Check for success indicators
    bool success = (output.find("success") != std::string::npos ||
                    output.find("SUCCESS") != std::string::npos ||
                    output.find("decompiled") != std::string::npos ||
                    output.find("error") == std::string::npos);

    if (!success) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Decompilation appears to have failed");
        return false;
    }

    // Find the output QSC file
    std::string qsc_filename = qvm_filename;
    size_t dot = qsc_filename.find_last_of('.');
    if (dot != std::string::npos) qsc_filename = qsc_filename.substr(0, dot) + ".qsc";

    std::string output_qsc = decompile_output_dir + "\\" + qsc_filename;
    if (!fs::exists(output_qsc)) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Output QSC not found: " + output_qsc);
        return false;
    }

    // Copy QSC to destination
    try {
        fs::create_directories(fs::path(qsc_output_path).parent_path());
        if (fs::exists(qsc_output_path)) fs::remove(qsc_output_path);
        fs::copy_file(output_qsc, qsc_output_path, fs::copy_options::overwrite_existing);
        Utils::TrimFileInPlace(qsc_output_path);
        if (output_callback_) output_callback_("[Decompiler] SUCCESS: Decompiled QSC copied to " + qsc_output_path);
        return true;
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: Failed to copy QSC: " + std::string(e.what()));
        return false;
    }
}
