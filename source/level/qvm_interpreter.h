// qvm_interpreter.h - Bounded QVM bytecode runtime interpreter
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "qvm_native_registry.h"

namespace igi {

struct QvmInstruction {
    uint8_t opcode;
    uint32_t operand_u32 = 0;
    int32_t operand_i32 = 0;
    double operand_f64 = 0.0;
    std::string operand_str;
};

struct QvmProgram {
    std::vector<uint8_t> raw_bytecode;
    std::vector<QvmInstruction> instructions;
    std::vector<std::string> string_table;
    uint32_t entry_point = 0;
};

class QvmExecutionContext {
public:
    static constexpr size_t MAX_STACK_DEPTH = 256;
    static constexpr size_t MAX_CALL_FRAMES = 64;
    static constexpr uint64_t MAX_INSTRUCTION_STEPS = 100000; // Protection against infinite loops

    QvmExecutionContext(const QvmProgram& program, const QvmNativeRegistry& registry);

    void Reset();

    // Executes program from current PC until return or pause limit
    bool Run();

    // Single step execution
    bool Step();

    // Stack operations
    void Push(const QvmRuntimeValue& val);
    QvmRuntimeValue Pop();
    const QvmRuntimeValue& Peek(size_t depth = 0) const;
    size_t StackSize() const { return stack_.size(); }

    // PC & status
    uint32_t GetPC() const { return pc_; }
    bool HasHalted() const { return halted_; }
    bool HasErrored() const { return errored_; }
    const std::string& GetLastError() const { return last_error_; }
    uint64_t GetStepCount() const { return step_count_; }

    void SetError(const std::string& msg);

private:
    struct CallFrame {
        uint32_t return_pc = 0;
        size_t stack_base = 0;
    };

    const QvmProgram& program_;
    const QvmNativeRegistry& registry_;

    std::vector<QvmRuntimeValue> stack_;
    std::vector<CallFrame> call_stack_;

    uint32_t pc_ = 0;
    uint64_t step_count_ = 0;
    bool halted_ = false;
    bool errored_ = false;
    std::string last_error_;
};

class QvmInterpreter {
public:
    QvmInterpreter(const QvmNativeRegistry& registry);

    bool LoadProgram(const std::vector<uint8_t>& bytecode, QvmProgram& out_program);
    std::unique_ptr<QvmExecutionContext> CreateContext(const QvmProgram& program);

private:
    const QvmNativeRegistry& registry_;
};

} // namespace igi
