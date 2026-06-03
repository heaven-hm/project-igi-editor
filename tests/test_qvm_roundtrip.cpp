#include <gtest/gtest.h>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include "parsers/qsc_lexer.h"
#include "parsers/qsc_parser.h"
#include "parsers/qvm_compiler.h"
#include "parsers/qvm_decompiler.h"
#include "parsers/qvm_parser.h"
#include "utils.h"

std::string ReadFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

std::string NormalizeQsc(const std::string& input) {
    std::string res = std::regex_replace(input, std::regex("\\s+"), " ");
    return Utils::Trim(res);
}

TEST(QvmRoundTripTest, CompileAndDecompileProducesSameOutput) {
    std::string original_qsc = ReadFile("tests/fixtures/level01_simple.qsc");
    ASSERT_FALSE(original_qsc.empty()) << "Failed to read fixture";
    
    auto lex_res = qsc::Lex(original_qsc);
    ASSERT_TRUE(lex_res.ok) << "Lex failed: " << lex_res.error;
    
    auto parse_res = qsc::Parse(lex_res.tokens);
    ASSERT_TRUE(parse_res.ok) << "Parse failed: " << parse_res.error;
    
    auto compile_res = qvm::Compile(*parse_res.program);
    ASSERT_TRUE(compile_res.ok) << "Compile failed: " << compile_res.error;
    ASSERT_FALSE(compile_res.binary.empty());
    
    std::string temp_qvm = "tests/fixtures/temp.qvm";
    {
        std::ofstream ofs(temp_qvm, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(compile_res.binary.data()), compile_res.binary.size());
    }
    
    QVMFile qvm = QVM_Parse(temp_qvm);
    ASSERT_TRUE(qvm.valid) << "QVM parse failed: " << qvm.error;
    
    std::string round_tripped = QVM_DecompileToString(qvm);
    
    std::remove(temp_qvm.c_str());
    
    EXPECT_EQ(NormalizeQsc(original_qsc), NormalizeQsc(round_tripped));
}
