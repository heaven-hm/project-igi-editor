// qvm_interpreter.cpp - Bounded QVM bytecode runtime interpreter implementation
#include "qvm_interpreter.h"
#include <sstream>

namespace igi {

QvmExecutionContext::QvmExecutionContext(const QvmProgram& program, const QvmNativeRegistry& registry)
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

void QvmExecutionContext::Push(const QvmRuntimeValue& val) {
    if (stack_.size() >= MAX_STACK_DEPTH) {
        SetError("Stack overflow in QVM execution context");
        return;
    }
    stack_.push_back(val);
}

QvmRuntimeValue QvmExecutionContext::Pop() {
    if (stack_.empty()) {
        SetError("Stack underflow in QVM execution context");
        return QvmRuntimeValue();
    }
    QvmRuntimeValue top = stack_.back();
    stack_.pop_back();
    return top;
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

bool QvmExecutionContext::Step() {
    if (halted_ || errored_ || pc_ >= program_.instructions.size()) {
        halted_ = true;
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
            Pop();
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
                QvmRuntimeValue b = Pop();
                QvmRuntimeValue a = Pop();
                Push(QvmRuntimeValue::FromInt(a.int_val + b.int_val));
            }
            break;

        case 0x11: // SUB_INT
            {
                QvmRuntimeValue b = Pop();
                QvmRuntimeValue a = Pop();
                Push(QvmRuntimeValue::FromInt(a.int_val - b.int_val));
            }
            break;

        case 0x12: // MUL_INT
            {
                QvmRuntimeValue b = Pop();
                QvmRuntimeValue a = Pop();
                Push(QvmRuntimeValue::FromInt(a.int_val * b.int_val));
            }
            break;

        case 0x13: // DIV_INT
            {
                QvmRuntimeValue b = Pop();
                QvmRuntimeValue a = Pop();
                if (b.int_val == 0) {
                    SetError("Divide by zero in QVM");
                } else {
                    Push(QvmRuntimeValue::FromInt(a.int_val / b.int_val));
                }
            }
            break;

        case 0x20: // JUMP
            pc_ = inst.operand_u32;
            break;

        case 0x21: // JUMP_IF_ZERO
            {
                QvmRuntimeValue cond = Pop();
                if (cond.int_val == 0) {
                    pc_ = inst.operand_u32;
                }
            }
            break;

        case 0x30: // CALL_NATIVE
            {
                uint32_t sym_id = inst.operand_u32;
                uint32_t arg_count = inst.operand_i32;
                std::vector<QvmRuntimeValue> args;
                args.reserve(arg_count);

                for (uint32_t i = 0; i < arg_count; i++) {
                    args.push_back(Pop());
                }

                QvmRuntimeValue result;
                if (!registry_.TryExecute(sym_id, *this, args, result)) {
                    std::ostringstream ss;
                    ss << "Unknown native function ID 0x" << std::hex << sym_id;
                    SetError(ss.str());
                } else {
                    Push(result);
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

QvmInterpreter::QvmInterpreter(const QvmNativeRegistry& registry)
    : registry_(registry) {}

bool QvmInterpreter::LoadProgram(const std::vector<uint8_t>& bytecode, QvmProgram& out_program) {
    if (bytecode.empty()) return false;

    out_program.raw_bytecode = bytecode;
    out_program.instructions.clear();
    out_program.entry_point = 0;

    // Simple bytecode decoder
    size_t offset = 0;
    while (offset < bytecode.size()) {
        QvmInstruction inst;
        inst.opcode = bytecode[offset++];

        if (inst.opcode == 0x01) { // PUSH_INT
            if (offset + 4 <= bytecode.size()) {
                inst.operand_i32 = *reinterpret_cast<const int32_t*>(&bytecode[offset]);
                offset += 4;
            }
        } else if (inst.opcode == 0x02) { // PUSH_REAL
            if (offset + 8 <= bytecode.size()) {
                inst.operand_f64 = *reinterpret_cast<const double*>(&bytecode[offset]);
                offset += 8;
            }
        } else if (inst.opcode == 0x20 || inst.opcode == 0x21) { // JUMP
            if (offset + 4 <= bytecode.size()) {
                inst.operand_u32 = *reinterpret_cast<const uint32_t*>(&bytecode[offset]);
                offset += 4;
            }
        } else if (inst.opcode == 0x30) { // CALL_NATIVE
            if (offset + 8 <= bytecode.size()) {
                inst.operand_u32 = *reinterpret_cast<const uint32_t*>(&bytecode[offset]);
                inst.operand_i32 = *reinterpret_cast<const int32_t*>(&bytecode[offset + 4]);
                offset += 8;
            }
        }

        out_program.instructions.push_back(inst);
    }

    return !out_program.instructions.empty();
}

std::unique_ptr<QvmExecutionContext> QvmInterpreter::CreateContext(const QvmProgram& program) {
    return std::make_unique<QvmExecutionContext>(program, registry_);
}

} // namespace igi
