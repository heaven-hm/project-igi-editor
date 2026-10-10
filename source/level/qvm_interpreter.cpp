// qvm_interpreter.cpp - Bounded QVM bytecode runtime interpreter implementation
#include "qvm_interpreter.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <cmath>
#include <limits>
#include <sstream>

namespace {

bool ReadLittleEndianUint32(const std::vector<uint8_t>& bytecode, size_t& offset, uint32_t& value) {
    if (bytecode.size() - offset < sizeof(uint32_t)) {
        return false;
    }

    value = static_cast<uint32_t>(bytecode[offset])
        | (static_cast<uint32_t>(bytecode[offset + 1]) << 8U)
        | (static_cast<uint32_t>(bytecode[offset + 2]) << 16U)
        | (static_cast<uint32_t>(bytecode[offset + 3]) << 24U);
    offset += sizeof(uint32_t);
    return true;
}

bool ReadLittleEndianUint64(const std::vector<uint8_t>& bytecode, size_t& offset, uint64_t& value) {
    if (bytecode.size() - offset < sizeof(uint64_t)) {
        return false;
    }

    value = static_cast<uint64_t>(bytecode[offset])
        | (static_cast<uint64_t>(bytecode[offset + 1]) << 8U)
        | (static_cast<uint64_t>(bytecode[offset + 2]) << 16U)
        | (static_cast<uint64_t>(bytecode[offset + 3]) << 24U)
        | (static_cast<uint64_t>(bytecode[offset + 4]) << 32U)
        | (static_cast<uint64_t>(bytecode[offset + 5]) << 40U)
        | (static_cast<uint64_t>(bytecode[offset + 6]) << 48U)
        | (static_cast<uint64_t>(bytecode[offset + 7]) << 56U);
    offset += sizeof(uint64_t);
    return true;
}

} // namespace

namespace igi {

bool QvmProgram::TryGetInstructionIndex(
    uint32_t code_address,
    uint32_t& out_instruction_index) const {
    const auto mapped = code_address_to_instruction_index_.find(code_address);
    if (mapped != code_address_to_instruction_index_.end()) {
        out_instruction_index = mapped->second;
        return true;
    }

    // Normalized test programs use instruction indices as their program
    // counter. Keeping this fallback preserves that deliberately separate seam.
    if (!uses_loop_85_instruction_set && code_address < instructions.size()) {
        out_instruction_index = code_address;
        return true;
    }
    return false;
}

QvmExecutionContext::QvmExecutionContext(QvmProgram const& program, QvmNativeRegistry& registry)
    : program_(program), registry_(registry) {
    Reset();
}

void QvmExecutionContext::Reset() {
    stack_.clear();
    stack_.reserve(MAX_STACK_DEPTH);
    call_stack_.clear();
    call_stack_.reserve(MAX_CALL_FRAMES);
    pc_ = program_.entry_point;
    step_count_ = 0;
    halted_ = false;
    errored_ = false;
    last_error_.clear();
}

void QvmExecutionContext::SetError(const std::string& msg) {
    errored_ = true;
    halted_ = true;
    last_error_ = msg;
}

bool QvmExecutionContext::TryResolveValue(
    const QvmRuntimeValue& value,
    QvmRuntimeValue& out_resolved) const {
    if (value.type != QvmValueType::Identifier && value.type != QvmValueType::Symbol) {
        out_resolved = value;
        return true;
    }

    if (!registry_.TryGetValueByName(value.str_val, out_resolved)) {
        return false;
    }
    return true;
}

bool QvmExecutionContext::TryPopResolved(QvmRuntimeValue& out_value) {
    QvmRuntimeValue value;
    if (!TryPop(value)) {
        return false;
    }
    if (TryResolveValue(value, out_value)) {
        return true;
    }

    SetError("Unknown QVM identifier: " + value.str_val);
    return false;
}

bool QvmExecutionContext::TryPopNumber(QvmRuntimeValue& out_value) {
    if (!TryPopResolved(out_value)) {
        return false;
    }
    if (out_value.type == QvmValueType::Int
        || out_value.type == QvmValueType::Real
        || out_value.type == QvmValueType::Address) {
        return true;
    }

    SetError("QVM operand is not numeric");
    return false;
}

bool QvmExecutionContext::TryPopString(std::string& out_value) {
    QvmRuntimeValue value;
    if (!TryPopResolved(value)) {
        return false;
    }
    if (value.type != QvmValueType::String) {
        SetError("QVM operand is not a string");
        return false;
    }
    out_value = value.str_val;
    return true;
}

bool QvmExecutionContext::TryPopSymbolName(std::string& out_name) {
    QvmRuntimeValue value;
    if (!TryPop(value)) {
        return false;
    }
    if (value.type != QvmValueType::Identifier && value.type != QvmValueType::Symbol) {
        SetError("QVM call target is not a symbol");
        return false;
    }
    out_name = value.str_val;
    return true;
}

bool QvmExecutionContext::JumpToCodeAddress(int64_t code_address) {
    if (code_address < 0 || code_address > std::numeric_limits<uint32_t>::max()) {
        SetError("QVM control-flow target is outside the code section");
        return false;
    }

    uint32_t instruction_index = 0;
    if (!program_.TryGetInstructionIndex(
            static_cast<uint32_t>(code_address), instruction_index)) {
        SetError("QVM control-flow target is not an instruction boundary");
        return false;
    }
    pc_ = instruction_index;
    return true;
}

bool QvmExecutionContext::EvaluateArgumentAtAddress(
    uint32_t code_address,
    QvmValueType requested_type,
    QvmRuntimeValue& out_value) {
    if (!program_.uses_loop_85_instruction_set) {
        SetError("Deferred QVM arguments require a LOOP 8.5 program");
        return false;
    }

    uint32_t argument_instruction = 0;
    if (!program_.TryGetInstructionIndex(code_address, argument_instruction)) {
        SetError("QVM argument address is not an instruction boundary");
        return false;
    }

    const uint32_t saved_pc = pc_;
    const size_t saved_stack_depth = stack_.size();
    const bool saved_halted = halted_;
    const bool saved_errored = errored_;
    const std::string saved_error = last_error_;

    pc_ = argument_instruction;
    halted_ = false;
    errored_ = false;
    last_error_.clear();

    while (!halted_ && !errored_) {
        if (step_count_ >= MAX_INSTRUCTION_STEPS) {
            SetError("Instruction limit exceeded while evaluating QVM argument");
            break;
        }
        StepLoopInstruction();
    }

    bool evaluated = !errored_ && stack_.size() > saved_stack_depth;
    if (evaluated) {
        QvmRuntimeValue candidate = stack_.back();
        if (candidate.type == QvmValueType::Identifier
            || candidate.type == QvmValueType::Symbol) {
            QvmRuntimeValue resolved;
            evaluated = TryResolveValue(candidate, resolved);
            if (evaluated) {
                candidate = std::move(resolved);
            }
        }

        if (evaluated && requested_type == QvmValueType::String) {
            evaluated = candidate.type == QvmValueType::String;
        } else if (evaluated && requested_type == QvmValueType::Real) {
            evaluated = candidate.type == QvmValueType::Real
                || candidate.type == QvmValueType::Int
                || candidate.type == QvmValueType::Address;
        } else if (evaluated && requested_type == QvmValueType::Int) {
            evaluated = candidate.type == QvmValueType::Int
                || candidate.type == QvmValueType::Real
                || candidate.type == QvmValueType::Address;
        }

        if (evaluated) {
            out_value = std::move(candidate);
        }
    }

    stack_.resize(saved_stack_depth);
    pc_ = saved_pc;
    halted_ = saved_halted;
    if (saved_errored) {
        errored_ = true;
        last_error_ = saved_error;
    } else if (!evaluated && !errored_) {
        errored_ = false;
        last_error_ = saved_error;
    }
    return evaluated;
}

void QvmExecutionContext::Push(const QvmRuntimeValue& val) {
    if (stack_.size() >= MAX_STACK_DEPTH) {
        SetError("Stack overflow in QVM execution context");
        return;
    }
    stack_.push_back(val);
}

bool QvmExecutionContext::TryPop(QvmRuntimeValue& out_value) {
    if (stack_.empty()) {
        SetError("Stack underflow in QVM execution context");
        return false;
    }

    out_value = stack_.back();
    stack_.pop_back();
    return true;
}

QvmRuntimeValue QvmExecutionContext::Pop() {
    QvmRuntimeValue value;
    if (!TryPop(value)) {
        return QvmRuntimeValue();
    }
    return value;
}

const QvmRuntimeValue& QvmExecutionContext::Peek(size_t depth) const {
    static const QvmRuntimeValue null_val;
    if (depth >= stack_.size()) {
        return null_val;
    }
    return stack_[stack_.size() - 1 - depth];
}

bool QvmExecutionContext::Run() {
    while (!halted_ && !errored_) {
        if (step_count_ >= MAX_INSTRUCTION_STEPS) {
            SetError("Instruction limit exceeded (possible infinite loop)");
            return false;
        }
        if (!Step()) {
            break;
        }
    }
    return !errored_;
}

bool QvmExecutionContext::PushLoopSymbolByIndex(uint32_t symbol_index) {
    if (symbol_index >= program_.identifier_table.size()) {
        SetError("QVM symbol index is outside the identifier table");
        return false;
    }
    Push(QvmRuntimeValue::FromSymbol(program_.identifier_table[symbol_index]));
    return !errored_;
}

bool QvmExecutionContext::ExecuteLoopBinary(uint8_t opcode) {
    QvmRuntimeValue right;
    QvmRuntimeValue left;
    if (!TryPopResolved(right) || !TryPopResolved(left)) {
        return false;
    }

    const bool left_is_string = left.type == QvmValueType::String;
    const bool right_is_string = right.type == QvmValueType::String;
    if (left_is_string || right_is_string) {
        if (!left_is_string || !right_is_string) {
            SetError("QVM text operation has a non-text operand");
            return false;
        }

        const int comparison = left.str_val.compare(right.str_val);
        switch (opcode) {
            case 0x19: // ADD
                Push(QvmRuntimeValue::FromString(left.str_val + right.str_val));
                break;
            case 0x24: // EQ
                Push(QvmRuntimeValue::FromInt(comparison == 0));
                break;
            case 0x25: // NE
                Push(QvmRuntimeValue::FromInt(comparison != 0));
                break;
            case 0x26: // LT
                Push(QvmRuntimeValue::FromInt(comparison < 0));
                break;
            case 0x27: // LE
                Push(QvmRuntimeValue::FromInt(comparison <= 0));
                break;
            case 0x28: // GT
                Push(QvmRuntimeValue::FromInt(comparison > 0));
                break;
            case 0x29: // GE
                Push(QvmRuntimeValue::FromInt(comparison >= 0));
                break;
            default:
                SetError("Unsupported QVM operation for text operands");
                return false;
        }
        return !errored_;
    }

    const bool has_real_operand = left.type == QvmValueType::Real
        || right.type == QvmValueType::Real;
    if (has_real_operand) {
        const double left_value = left.type == QvmValueType::Real
            ? left.real_val : static_cast<double>(left.int_val);
        const double right_value = right.type == QvmValueType::Real
            ? right.real_val : static_cast<double>(right.int_val);
        switch (opcode) {
            case 0x19: Push(QvmRuntimeValue::FromReal(left_value + right_value)); break;
            case 0x1A: Push(QvmRuntimeValue::FromReal(left_value - right_value)); break;
            case 0x1B: Push(QvmRuntimeValue::FromReal(left_value * right_value)); break;
            case 0x1C:
                if (right_value == 0.0) {
                    SetError("Divide by zero in QVM");
                    return false;
                }
                Push(QvmRuntimeValue::FromReal(left_value / right_value));
                break;
            case 0x22: Push(QvmRuntimeValue::FromInt((left_value != 0.0) && (right_value != 0.0))); break;
            case 0x23: Push(QvmRuntimeValue::FromInt((left_value != 0.0) || (right_value != 0.0))); break;
            case 0x24: Push(QvmRuntimeValue::FromInt(left_value == right_value)); break;
            case 0x25: Push(QvmRuntimeValue::FromInt(left_value != right_value)); break;
            case 0x26: Push(QvmRuntimeValue::FromInt(left_value < right_value)); break;
            case 0x27: Push(QvmRuntimeValue::FromInt(left_value <= right_value)); break;
            case 0x28: Push(QvmRuntimeValue::FromInt(left_value > right_value)); break;
            case 0x29: Push(QvmRuntimeValue::FromInt(left_value >= right_value)); break;
            default:
                SetError("Integer-only QVM operation received a real operand");
                return false;
        }
        return !errored_;
    }

    const int32_t left_value = left.int_val;
    const int32_t right_value = right.int_val;
    switch (opcode) {
        case 0x19: Push(QvmRuntimeValue::FromInt(left_value + right_value)); break;
        case 0x1A: Push(QvmRuntimeValue::FromInt(left_value - right_value)); break;
        case 0x1B: Push(QvmRuntimeValue::FromInt(left_value * right_value)); break;
        case 0x1C:
            if (right_value == 0) {
                SetError("Divide by zero in QVM");
                return false;
            }
            Push(QvmRuntimeValue::FromInt(left_value / right_value));
            break;
        case 0x1D: Push(QvmRuntimeValue::FromInt(left_value << right_value)); break;
        case 0x1E: Push(QvmRuntimeValue::FromInt(left_value >> right_value)); break;
        case 0x1F: Push(QvmRuntimeValue::FromInt(left_value & right_value)); break;
        case 0x20: Push(QvmRuntimeValue::FromInt(left_value | right_value)); break;
        case 0x21: Push(QvmRuntimeValue::FromInt(left_value ^ right_value)); break;
        case 0x22: Push(QvmRuntimeValue::FromInt(left_value != 0 && right_value != 0)); break;
        case 0x23: Push(QvmRuntimeValue::FromInt(left_value != 0 || right_value != 0)); break;
        case 0x24: Push(QvmRuntimeValue::FromInt(left_value == right_value)); break;
        case 0x25: Push(QvmRuntimeValue::FromInt(left_value != right_value)); break;
        case 0x26: Push(QvmRuntimeValue::FromInt(left_value < right_value)); break;
        case 0x27: Push(QvmRuntimeValue::FromInt(left_value <= right_value)); break;
        case 0x28: Push(QvmRuntimeValue::FromInt(left_value > right_value)); break;
        case 0x29: Push(QvmRuntimeValue::FromInt(left_value >= right_value)); break;
        default:
            SetError("Unsupported QVM binary opcode");
            return false;
    }
    return !errored_;
}

bool QvmExecutionContext::ExecuteLoopUnary(uint8_t opcode) {
    QvmRuntimeValue operand;
    if (!TryPopResolved(operand)) {
        return false;
    }

    if (operand.type == QvmValueType::Real) {
        if (opcode == 0x2B) {
            Push(QvmRuntimeValue::FromReal(operand.real_val));
        } else if (opcode == 0x2C) {
            Push(QvmRuntimeValue::FromReal(-operand.real_val));
        } else {
            SetError("Integer-only QVM unary opcode received a real operand");
            return false;
        }
        return !errored_;
    }

    if (operand.type != QvmValueType::Int && operand.type != QvmValueType::Address) {
        SetError("QVM unary operand is not numeric");
        return false;
    }

    switch (opcode) {
        case 0x2B: Push(QvmRuntimeValue::FromInt(operand.int_val)); break;
        case 0x2C: Push(QvmRuntimeValue::FromInt(-operand.int_val)); break;
        case 0x2D: Push(QvmRuntimeValue::FromInt(~operand.int_val)); break;
        case 0x2E: Push(QvmRuntimeValue::FromInt(operand.int_val == 0)); break;
        default:
            SetError("Unsupported QVM unary opcode");
            return false;
    }
    return !errored_;
}

bool QvmExecutionContext::ExecuteLoopAssignment() {
    QvmRuntimeValue value;
    if (!TryPopResolved(value)) {
        return false;
    }

    std::string target_name;
    if (!TryPopSymbolName(target_name)) {
        return false;
    }

    QvmRuntimeValue stored_value;
    if (!registry_.TrySetValueByName(target_name, value, stored_value)) {
        SetError("QVM assignment target is not a variable: " + target_name);
        return false;
    }
    Push(stored_value);
    return !errored_;
}

bool QvmExecutionContext::StepLoopInstruction() {
    if (pc_ >= program_.instructions.size()) {
        SetError("QVM instruction pointer is outside the code section");
        return false;
    }

    ++step_count_;
    const QvmInstruction& instruction = program_.instructions[pc_++];
    const uint8_t opcode = instruction.opcode;

    switch (opcode) {
        case 0x00: // BRK
            halted_ = true;
            return false;
        case 0x01: // NOP
            break;
        case 0x02: // PUSH
            Push(QvmRuntimeValue::FromInt(instruction.operand_i32));
            break;
        case 0x03: // PUSHB
        case 0x04: // PUSHW
            Push(QvmRuntimeValue::FromInt(instruction.operand_i32));
            break;
        case 0x05: // PUSHF
            Push(QvmRuntimeValue::FromReal(instruction.operand_f64));
            break;
        case 0x06: // PUSHA
            Push(QvmRuntimeValue::FromAddress(instruction.operand_i32));
            break;
        case 0x07: // PUSHS
            Push(QvmRuntimeValue::FromString(instruction.operand_str));
            break;
        case 0x08: // PUSHSI
        case 0x09: // PUSHSIB
        case 0x0A: // PUSHSIW
            if (instruction.operand_u32 >= program_.string_table.size()) {
                SetError("QVM string index is outside the string table");
                break;
            }
            Push(QvmRuntimeValue::FromString(
                program_.string_table[instruction.operand_u32]));
            break;
        case 0x0B: // PUSHI
            Push(QvmRuntimeValue::FromIdentifier(instruction.operand_str));
            break;
        case 0x0C: // PUSHII
        case 0x0D: // PUSHIIB
        case 0x0E: // PUSHIIW
            PushLoopSymbolByIndex(instruction.operand_u32);
            break;
        case 0x0F: // PUSH0
            Push(QvmRuntimeValue::FromInt(0));
            break;
        case 0x10: // PUSH1
            Push(QvmRuntimeValue::FromInt(1));
            break;
        case 0x11: // PUSHM
            Push(QvmRuntimeValue::FromInt(-1));
            break;
        case 0x12: { // POP
            QvmRuntimeValue discarded;
            TryPop(discarded);
            break;
        }
        case 0x14: // BRA
            JumpToCodeAddress(
                static_cast<int64_t>(instruction.code_address)
                + instruction.instruction_size
                + instruction.operand_i32);
            break;
        case 0x15: // BF
        case 0x16: { // BT
            QvmRuntimeValue condition;
            if (!TryPopNumber(condition)) {
                break;
            }
            const bool is_true = condition.type == QvmValueType::Real
                ? condition.real_val != 0.0
                : condition.int_val != 0;
            const bool branch_taken = opcode == 0x16 ? is_true : !is_true;
            if (branch_taken) {
                JumpToCodeAddress(
                    static_cast<int64_t>(instruction.code_address)
                    + instruction.instruction_size
                    + instruction.operand_i32);
            }
            break;
        }
        case 0x18: { // CALL
            if (instruction.operand_i32 < 0
                || static_cast<size_t>(instruction.operand_i32)
                    != instruction.argument_addresses.size()) {
                SetError("QVM CALL has an invalid argument count");
                break;
            }

            std::string function_name;
            if (!TryPopSymbolName(function_name)) {
                break;
            }

            QvmRuntimeValue result;
            if (!registry_.TryExecuteDeferredByName(
                    function_name,
                    *this,
                    instruction.argument_addresses,
                    result)) {
                if (!errored_) {
                    SetError("Unknown QVM function: " + function_name);
                }
                break;
            }

            switch (registry_.GetReturnTypeByName(function_name)) {
                case QvmValueType::Real:
                    Push(QvmRuntimeValue::FromReal(
                        result.type == QvmValueType::Real
                            ? result.real_val
                            : static_cast<double>(result.int_val)));
                    break;
                case QvmValueType::String:
                    Push(QvmRuntimeValue::FromString(result.str_val));
                    break;
                default:
                    Push(QvmRuntimeValue::FromInt(result.int_val));
                    break;
            }
            break;
        }
        case 0x19: // ADD
        case 0x1A: // SUB
        case 0x1B: // MUL
        case 0x1C: // DIV
        case 0x1D: // SHL
        case 0x1E: // SHR
        case 0x1F: // AND
        case 0x20: // OR
        case 0x21: // XOR
        case 0x22: // LAND
        case 0x23: // LOR
        case 0x24: // EQ
        case 0x25: // NE
        case 0x26: // LT
        case 0x27: // LE
        case 0x28: // GT
        case 0x29: // GE
            ExecuteLoopBinary(opcode);
            break;
        case 0x2A: // ASSIGN
            ExecuteLoopAssignment();
            break;
        case 0x2B: // PLUS
        case 0x2C: // MINUS
        case 0x2D: // INV
        case 0x2E: // NOT
            ExecuteLoopUnary(opcode);
            break;
        case 0x2F: // BLK
            JumpToCodeAddress(
                static_cast<int64_t>(instruction.code_address)
                + instruction.instruction_size
                + instruction.operand_i32);
            break;
        case 0x13: // RET
        case 0x17: // JSR
        default:
            SetError("Illegal or unsupported LOOP QVM opcode 0x" +
                     [&]() {
                         std::ostringstream stream;
                         stream << std::hex << static_cast<int>(opcode);
                         return stream.str();
                     }());
            break;
    }

    return !errored_ && !halted_;
}

bool QvmExecutionContext::Step() {
    if (halted_ || errored_) {
        return false;
    }

    if (program_.uses_loop_85_instruction_set) {
        return StepLoopInstruction();
    }

    if (pc_ >= program_.instructions.size()) {
        SetError("Instruction pointer is outside the QVM program");
        return false;
    }

    step_count_++;
    const QvmInstruction& inst = program_.instructions[pc_++];

    switch (inst.opcode) {
        case 0x00: // NOP
            break;

        case 0x01: // PUSH_INT
            Push(QvmRuntimeValue::FromInt(inst.operand_i32));
            break;

        case 0x02: // PUSH_REAL
            Push(QvmRuntimeValue::FromReal(inst.operand_f64));
            break;

        case 0x03: // PUSH_STRING
            Push(QvmRuntimeValue::FromString(inst.operand_str));
            break;

        case 0x04: // POP
            {
                QvmRuntimeValue discarded_value;
                TryPop(discarded_value);
            }
            break;

        case 0x05: // DUP
            if (!stack_.empty()) {
                Push(stack_.back());
            } else {
                SetError("DUP on empty stack");
            }
            break;

        case 0x10: // ADD_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    Push(QvmRuntimeValue::FromInt(left_value.int_val + right_value.int_val));
                }
            }
            break;

        case 0x11: // SUB_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    Push(QvmRuntimeValue::FromInt(left_value.int_val - right_value.int_val));
                }
            }
            break;

        case 0x12: // MUL_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    Push(QvmRuntimeValue::FromInt(left_value.int_val * right_value.int_val));
                }
            }
            break;

        case 0x13: // DIV_INT
            {
                QvmRuntimeValue right_value;
                QvmRuntimeValue left_value;
                if (TryPop(right_value) && TryPop(left_value)) {
                    if (right_value.int_val == 0) {
                        SetError("Divide by zero in QVM");
                    } else {
                        Push(QvmRuntimeValue::FromInt(left_value.int_val / right_value.int_val));
                    }
                }
            }
            break;

        case 0x20: // JUMP
            if (inst.operand_u32 >= program_.instructions.size()) {
                SetError("QVM jump target is outside the program");
            } else {
                pc_ = inst.operand_u32;
            }
            break;

        case 0x21: // JUMP_IF_ZERO
            {
                QvmRuntimeValue condition_value;
                if (!TryPop(condition_value)) {
                    break;
                }
                if (condition_value.int_val == 0) {
                    if (inst.operand_u32 >= program_.instructions.size()) {
                        SetError("QVM conditional jump target is outside the program");
                        break;
                    }
                    pc_ = inst.operand_u32;
                }
            }
            break;

        case 0x30: // CALL_NATIVE
            {
                uint32_t sym_id = inst.operand_u32;
                if (inst.operand_i32 < 0) {
                    SetError("QVM native call has a negative argument count");
                    break;
                }
                const uint32_t arg_count = static_cast<uint32_t>(inst.operand_i32);
                if (arg_count > stack_.size()) {
                    SetError("QVM native call has insufficient stack arguments");
                    break;
                }
                std::vector<QvmRuntimeValue> args;
                args.reserve(arg_count);

                for (uint32_t i = 0; i < arg_count; i++) {
                    QvmRuntimeValue argument;
                    if (!TryPop(argument)) {
                        return false;
                    }
                    args.push_back(argument);
                }
                std::reverse(args.begin(), args.end());

                QvmRuntimeValue result;
                try {
                    if (!registry_.TryExecute(sym_id, *this, args, result)) {
                        std::ostringstream ss;
                        ss << "Unknown native function ID 0x" << std::hex << sym_id;
                        SetError(ss.str());
                    } else if (!errored_) {
                        Push(result);
                    }
                } catch (const std::exception& exception) {
                    SetError(std::string("QVM native function threw: ") + exception.what());
                } catch (...) {
                    SetError("QVM native function threw an unknown exception");
                }
            }
            break;

        case 0xFF: // RETURN / HALT
            halted_ = true;
            return false;

        default:
            {
                std::ostringstream ss;
                ss << "Illegal or unsupported QVM opcode 0x" << std::hex << (int)inst.opcode;
                SetError(ss.str());
            }
            break;
    }

    return !errored_ && !halted_;
}

QvmInterpreter::QvmInterpreter(QvmNativeRegistry& registry)
    : registry_(registry) {}

bool QvmInterpreter::LoadProgram(const std::vector<uint8_t>& bytecode, QvmProgram& out_program) {
    out_program = QvmProgram();
    last_error_.clear();

    auto fail = [this](const std::string& message) {
        last_error_ = message;
        return false;
    };

    if (bytecode.empty()) {
        return fail("QVM bytecode is empty");
    }

    std::vector<QvmInstruction> decoded_instructions;
    size_t offset = 0;
    while (offset < bytecode.size()) {
        QvmInstruction inst;
        inst.opcode = bytecode[offset++];

        if (inst.opcode == 0x01) { // PUSH_INT
            uint32_t encoded_value = 0;
            if (!ReadLittleEndianUint32(bytecode, offset, encoded_value)) {
                return fail("truncated PUSH_INT operand in QVM bytecode");
            }
            inst.operand_i32 = std::bit_cast<int32_t>(encoded_value);
        } else if (inst.opcode == 0x02) { // PUSH_REAL
            uint64_t encoded_value = 0;
            if (!ReadLittleEndianUint64(bytecode, offset, encoded_value)) {
                return fail("truncated PUSH_REAL operand in QVM bytecode");
            }
            inst.operand_f64 = std::bit_cast<double>(encoded_value);
        } else if (inst.opcode == 0x20 || inst.opcode == 0x21) { // JUMP
            if (!ReadLittleEndianUint32(bytecode, offset, inst.operand_u32)) {
                return fail("truncated jump operand in QVM bytecode");
            }
        } else if (inst.opcode == 0x30) { // CALL_NATIVE
            uint32_t argument_count = 0;
            if (!ReadLittleEndianUint32(bytecode, offset, inst.operand_u32)
                || !ReadLittleEndianUint32(bytecode, offset, argument_count)) {
                return fail("truncated native-call operand in QVM bytecode");
            }
            inst.operand_i32 = std::bit_cast<int32_t>(argument_count);
        }

        decoded_instructions.push_back(std::move(inst));
    }

    if (decoded_instructions.empty()) {
        return fail("QVM bytecode contains no instructions");
    }

    out_program.raw_bytecode = bytecode;
    out_program.instructions = std::move(decoded_instructions);
    out_program.uses_loop_85_instruction_set = false;
    return true;
}

bool QvmInterpreter::LoadProgram(const QVMFile& parsed_file, QvmProgram& out_program) {
    out_program = QvmProgram();
    last_error_.clear();

    auto fail = [this](const std::string& message) {
        last_error_ = message;
        return false;
    };

    if (!parsed_file.valid) {
        return fail(parsed_file.error.empty()
            ? "Cannot execute an invalid QVM file"
            : parsed_file.error);
    }
    if (parsed_file.instructions.empty()) {
        return fail("QVM file contains no instructions");
    }

    out_program.uses_loop_85_instruction_set = true;
    out_program.string_table = parsed_file.strings;
    out_program.identifier_table = parsed_file.identifiers;
    out_program.instructions.reserve(parsed_file.instructions.size());

    for (size_t instruction_index = 0;
         instruction_index < parsed_file.instructions.size();
         ++instruction_index) {
        const QVMInstruction& source_instruction = parsed_file.instructions[instruction_index];
        if (source_instruction.size == 0) {
            return fail("QVM instruction has zero encoded size");
        }
        if (out_program.code_address_to_instruction_index_.find(source_instruction.address)
            != out_program.code_address_to_instruction_index_.end()) {
            return fail("QVM contains duplicate instruction addresses");
        }

        QvmInstruction target_instruction;
        target_instruction.opcode = static_cast<uint8_t>(source_instruction.type);
        target_instruction.operand_u32 = source_instruction.operand;
        target_instruction.operand_i32 = source_instruction.signed_operand;
        target_instruction.operand_f64 = source_instruction.operand_float;
        target_instruction.operand_str = source_instruction.inline_text;
        target_instruction.code_address = source_instruction.address;
        target_instruction.instruction_size = source_instruction.size;
        target_instruction.argument_addresses.reserve(source_instruction.call_targets.size());
        for (const int32_t argument_address : source_instruction.call_targets) {
            if (argument_address < 0) {
                return fail("QVM CALL contains a negative argument address");
            }
            target_instruction.argument_addresses.push_back(
                static_cast<uint32_t>(argument_address));
        }

        out_program.code_address_to_instruction_index_[source_instruction.address] =
            static_cast<uint32_t>(instruction_index);
        out_program.instructions.push_back(std::move(target_instruction));
    }

    out_program.entry_point = 0;
    return true;
}

std::unique_ptr<QvmExecutionContext> QvmInterpreter::CreateContext(const QvmProgram& program) {
    return std::make_unique<QvmExecutionContext>(program, registry_);
}

} // namespace igi
