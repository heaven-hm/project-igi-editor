// qvm_interpreter.h - Bounded QVM bytecode runtime interpreter
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "qvm_parser.h"
#include "qvm_native_registry.h"

namespace igi {

struct QvmInstruction {
    uint8_t opcode;
    uint32_t operand_u32 = 0;
    int32_t operand_i32 = 0;
    double operand_f64 = 0.0;
    std::string operand_str;
    uint32_t code_address = 0;
    uint32_t instruction_size = 1;
    std::vector<uint32_t> argument_addresses;
};

struct QvmProgram {
    std::vector<uint8_t> raw_bytecode;
    std::vector<QvmInstruction> instructions;
    std::vector<std::string> string_table;
    std::vector<std::string> identifier_table;
    uint32_t entry_point = 0;
    bool uses_loop_85_instruction_set = false;

    bool TryGetInstructionIndex(
        uint32_t code_address,
        uint32_t& out_instruction_index) const;

private:
    friend class QvmInterpreter;
    std::unordered_map<uint32_t, uint32_t> code_address_to_instruction_index_;
};

class QvmExecutionContext {
public:
    static constexpr size_t MAX_STACK_DEPTH = 256;
    static constexpr size_t MAX_CALL_FRAMES = 64;
    static constexpr uint64_t MAX_INSTRUCTION_STEPS = 100000; // Protection against infinite loops

    QvmExecutionContext(const QvmProgram& program, QvmNativeRegistry& registry);

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

    // Evaluates one LOOP argument block without leaking its temporary stack
    // values into the caller. This is the C++ seam used by deferred natives.
    bool EvaluateArgumentAtAddress(
        uint32_t code_address,
        QvmValueType requested_type,
        QvmRuntimeValue& out_value);

private:
    bool TryPop(QvmRuntimeValue& out_value);
    bool StepLoopInstruction();
    bool TryPopResolved(QvmRuntimeValue& out_value);
    bool TryPopNumber(QvmRuntimeValue& out_value);
    bool TryPopString(std::string& out_value);
    bool TryPopSymbolName(std::string& out_name);
    bool TryResolveValue(
        const QvmRuntimeValue& value,
        QvmRuntimeValue& out_resolved) const;
    bool JumpToCodeAddress(int64_t code_address);
    bool ExecuteLoopBinary(uint8_t opcode);
    bool ExecuteLoopUnary(uint8_t opcode);
    bool ExecuteLoopAssignment();
    bool PushLoopSymbolByIndex(uint32_t symbol_index);

    struct CallFrame {
        uint32_t return_pc = 0;
        size_t stack_base = 0;
    };

    const QvmProgram& program_;
    QvmNativeRegistry& registry_;

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
    QvmInterpreter(QvmNativeRegistry& registry);

    bool LoadProgram(const std::vector<uint8_t>& bytecode, QvmProgram& out_program);
    bool LoadProgram(const QVMFile& parsed_file, QvmProgram& out_program);
    std::unique_ptr<QvmExecutionContext> CreateContext(const QvmProgram& program);

    const std::string& GetLastError() const { return last_error_; }

private:
    QvmNativeRegistry& registry_;
    std::string last_error_;
};

} // namespace igi
