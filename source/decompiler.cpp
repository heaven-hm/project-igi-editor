#include "pch.h"
#include "decompiler.h"
#include "utils.h"
#include "renderer/qvm_parser.h"
#include <filesystem>
#include <fstream>
#include <iostream>

Decompiler::Decompiler() {}

Decompiler::~Decompiler() {}

void Decompiler::SetOutputCallback(std::function<void(const std::string&)> callback) {
    output_callback_ = callback;
}

bool Decompiler::Decompile(const std::string& qvm_path, const std::string& qsc_output_path) {
    if (output_callback_) output_callback_("[Decompiler] Native C++ Decompile started");
    if (output_callback_) output_callback_("[Decompiler] Input QVM:  " + qvm_path);
    if (output_callback_) output_callback_("[Decompiler] Output QSC: " + qsc_output_path);

    try {
        QVMFile qvm = QVM_Parse(qvm_path);
        if (!qvm.valid) {
            if (output_callback_) output_callback_("[Decompiler] ERROR: " + qvm.error);
            return false;
        }

        if (output_callback_) output_callback_("[Decompiler] QVM parsed successfully (" + std::to_string(qvm.instructions.size()) + " instructions)");

        std::string qsc_text = QVM_Decompile(qvm);
        
        std::ofstream out(qsc_output_path);
        if (!out.is_open()) {
            if (output_callback_) output_callback_("[Decompiler] ERROR: Failed to open output file: " + qsc_output_path);
            return false;
        }
        
        out << qsc_text;
        out.close();

        if (output_callback_) output_callback_("[Decompiler] SUCCESS: Native QSC written to: " + qsc_output_path);
        return true;

    } catch (const std::exception& e) {
        if (output_callback_) output_callback_("[Decompiler] ERROR: " + std::string(e.what()));
        return false;
    }
}
