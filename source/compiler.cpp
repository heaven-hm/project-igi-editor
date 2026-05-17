#include "pch.h"
#include "compiler.h"
#include "utils.h"
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <fstream>

Compiler::Compiler() {}

Compiler::~Compiler() {}

void Compiler::SetOutputCallback(std::function<void(const std::string&)> callback) {
    output_callback_ = callback;
}

bool Compiler::Compile(const std::string& qsc_path, const std::string& qvm_output_path) {
    namespace fs = std::filesystem;

    if (output_callback_) output_callback_("[Compiler] === Compile started ===");
    if (output_callback_) output_callback_("[Compiler] Input QSC:  " + qsc_path);
    if (output_callback_) output_callback_("[Compiler] Output QVM: " + qvm_output_path);

    // QCompiler layout — driven by AppData\Roaming\QEditor\QCompiler\Compile\
    //   .\input\   <- copy QSC here before running the bat
    //   .\output\  <- bat writes the compiled QVM here
    //   compile_v5.bat runs from .\  (the Compile directory itself)
    std::string qcompiler_path = Config::Get().compilerPath;
    std::string compile_dir    = qcompiler_path + "\\Compile";
    std::string input_dir      = compile_dir + "\\input";
    std::string output_dir     = compile_dir + "\\output";
    std::string compile_bat    = compile_dir + "\\compile_v5.bat";

    if (output_callback_) output_callback_("[Compiler] QCompiler:   " + qcompiler_path);
    if (output_callback_) output_callback_("[Compiler] Compile dir: " + compile_dir);
    if (output_callback_) output_callback_("[Compiler] Input dir:   " + input_dir);
    if (output_callback_) output_callback_("[Compiler] Output dir:  " + output_dir);

    // --- Validate paths ---
    if (!fs::exists(compile_bat)) {
        if (output_callback_) output_callback_("[Compiler] ERROR: compile_v5.bat not found: " + compile_bat);
        return false;
    }
    if (!fs::exists(input_dir)) {
        if (output_callback_) output_callback_("[Compiler] ERROR: input dir not found: " + input_dir);
        return false;
    }
    if (!fs::exists(output_dir)) {
        if (output_callback_) output_callback_("[Compiler] ERROR: output dir not found: " + output_dir);
        return false;
    }

    // --- Clean input and output dirs ---
    try {
        int n_in = 0, n_out = 0;
        for (const auto& e : fs::directory_iterator(input_dir))  { fs::remove_all(e.path()); n_in++;  }
        for (const auto& e : fs::directory_iterator(output_dir)) { fs::remove_all(e.path()); n_out++; }
        if (output_callback_) output_callback_("[Compiler] Cleaned: input(" + std::to_string(n_in) + ") output(" + std::to_string(n_out) + ")");
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Compiler] WARNING: clean failed: " + std::string(e.what()));
    }

    // --- Copy QSC to input\ ---
    std::string qsc_filename = fs::path(qsc_path).filename().string();
    std::string input_qsc    = input_dir + "\\" + qsc_filename;
    try {
        fs::copy_file(qsc_path, input_qsc, fs::copy_options::overwrite_existing);
        if (output_callback_) output_callback_("[Compiler] Copied QSC -> " + input_qsc);
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Compiler] ERROR: copy QSC failed: " + std::string(e.what()));
        return false;
    }

    // --- Run compile_v5.bat from Compile\ dir ---
    std::string log_path = output_dir + "\\compile.log";
    std::string cmd = "cd /d \"" + compile_dir + "\" && compile_v5.bat > \"" + log_path + "\" 2>&1";
    if (output_callback_) output_callback_("[Compiler] Running: " + cmd);

    int result = system(cmd.c_str());
    if (output_callback_) output_callback_("[Compiler] Batch exit code: " + std::to_string(result));

    // --- Read compile log ---
    std::string output;
    std::ifstream log_stream(log_path);
    if (log_stream.is_open()) {
        std::string line;
        while (std::getline(log_stream, line)) output += line + "\n";
        log_stream.close();
    } else {
        if (output_callback_) output_callback_("[Compiler] WARNING: could not read compile.log at: " + log_path);
    }
    if (output_callback_) output_callback_("[Compiler] --- Batch output ---");
    if (output_callback_) output_callback_(output);
    if (output_callback_) output_callback_("[Compiler] --- End output ---");

    // --- Assess success ---
    bool hasSuccess  = output.find("success")  != std::string::npos || output.find("SUCCESS")  != std::string::npos;
    bool hasCompiled = output.find("compiled")  != std::string::npos || output.find("COMPILED")  != std::string::npos;
    bool hasError    = output.find("error")     != std::string::npos || output.find("ERROR")     != std::string::npos;
    bool success     = hasSuccess || hasCompiled || !hasError;

    if (output_callback_) output_callback_("[Compiler] Success keyword:  " + std::string(hasSuccess  ? "yes" : "no"));
    if (output_callback_) output_callback_("[Compiler] Compiled keyword: " + std::string(hasCompiled ? "yes" : "no"));
    if (output_callback_) output_callback_("[Compiler] Error keyword:    " + std::string(hasError    ? "yes" : "no"));
    if (output_callback_) output_callback_("[Compiler] Overall success:  " + std::string(success     ? "yes" : "no"));

    if (!success) {
        if (output_callback_) output_callback_("[Compiler] ERROR: Compilation failed based on batch output");
        return false;
    }

    // --- Locate output QVM in output\ ---
    std::string qvm_filename = qsc_filename;
    size_t dot = qvm_filename.find_last_of('.');
    if (dot != std::string::npos) qvm_filename = qvm_filename.substr(0, dot) + ".qvm";

    std::string output_qvm = output_dir + "\\" + qvm_filename;
    if (output_callback_) output_callback_("[Compiler] Looking for QVM: " + output_qvm);

    if (!fs::exists(output_qvm)) {
        if (output_callback_) output_callback_("[Compiler] ERROR: Output QVM not found: " + output_qvm);
        return false;
    }
    if (output_callback_) output_callback_("[Compiler] Output QVM found: " + output_qvm);

    // --- Copy QVM to final game destination ---
    try {
        fs::create_directories(fs::path(qvm_output_path).parent_path());
        fs::copy_file(output_qvm, qvm_output_path, fs::copy_options::overwrite_existing);
        if (output_callback_) output_callback_("[Compiler] SUCCESS: QVM written to: " + qvm_output_path);
        return true;
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Compiler] ERROR: Failed to write QVM: " + std::string(e.what()));
        return false;
    }
}
