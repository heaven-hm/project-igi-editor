#include "pch.h"
#include "compiler.h"
#include "utils.h"
#include "renderer/qvm_parser.h"
#include "level/level_common.h"
#include <filesystem>
#include <iostream>

Compiler::Compiler() {}

Compiler::~Compiler() {}

void Compiler::SetOutputCallback(std::function<void(const std::string&)> callback) {
    output_callback_ = callback;
}

bool Compiler::Compile(const std::string& qsc_path, const std::string& qvm_output_path) {
    if (output_callback_) output_callback_("[Compiler] Native C++ Compile started");
    if (output_callback_) output_callback_("[Compiler] Input QSC:  " + qsc_path);
    if (output_callback_) output_callback_("[Compiler] Output QVM: " + qvm_output_path);

    try {
        QSC qsc;
        qsc.Load(qsc_path.c_str());
        
        if (output_callback_) output_callback_("[Compiler] QSC loaded, root functions: " + std::to_string(qsc.GetRootFuncCount()));

        QVMFile qvm = QVM_CompileFromQSC(qsc);
        if (output_callback_) output_callback_("[Compiler] Bytecode generated, instructions: " + std::to_string(qvm.instructions.size()));

        if (QVM_Write(qvm, qvm_output_path)) {
            if (output_callback_) output_callback_("[Compiler] SUCCESS: Native QVM written to: " + qvm_output_path);
            return true;
        } else {
            if (output_callback_) output_callback_("[Compiler] ERROR: Failed to write binary QVM file");
            return false;
        }
    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Compiler] ERROR: " + std::string(e.what()));
        return false;
    }
}
