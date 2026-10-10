#include "qvm_ai_bindings.h"

namespace igi {
namespace {

const QVMInstruction* FindInstructionAtAddress(
    const QVMFile& qvm,
    uint32_t address) {
    for (const QVMInstruction& instruction : qvm.instructions) {
        if (instruction.address == address) {
            return &instruction;
        }
    }
    return nullptr;
}

bool TryReadIntegerLiteral(
    const QVMInstruction& instruction,
    int& out_value) {
    switch (instruction.type) {
    case QVMOpType::PUSH:
    case QVMOpType::PUSHB:
    case QVMOpType::PUSHW:
        out_value = instruction.signed_operand;
        return true;
    case QVMOpType::PUSH0:
        out_value = 0;
        return true;
    case QVMOpType::PUSH1:
        out_value = 1;
        return true;
    case QVMOpType::PUSHM:
        out_value = -1;
        return true;
    default:
        return false;
    }
}

} // namespace

std::vector<int> FindCallIntegerArguments(
    const QVMFile& qvm,
    const std::string& function_name,
    std::size_t argument_index) {
    std::vector<int> values;
    if (!qvm.valid || function_name.empty()) {
        return values;
    }

    for (size_t instruction_index = 0;
         instruction_index + 1 < qvm.instructions.size();
         ++instruction_index) {
        const QVMInstruction& symbol_push = qvm.instructions[instruction_index];
        if (symbol_push.type != QVMOpType::PUSHII &&
            symbol_push.type != QVMOpType::PUSHIIB &&
            symbol_push.type != QVMOpType::PUSHIIW) {
            continue;
        }
        if (symbol_push.operand >= qvm.identifiers.size() ||
            qvm.identifiers[symbol_push.operand] != function_name) {
            continue;
        }

        const QVMInstruction& call = qvm.instructions[instruction_index + 1];
        if (call.type != QVMOpType::CALL ||
            call.call_targets.size() <= argument_index) {
            continue;
        }

        const int32_t argument_address = call.call_targets[argument_index];
        if (argument_address < 0) {
            continue;
        }

        const QVMInstruction* argument = FindInstructionAtAddress(
            qvm,
            static_cast<uint32_t>(argument_address));
        int value = -1;
        if (argument != nullptr && TryReadIntegerLiteral(*argument, value)) {
            values.push_back(value);
        }
    }

    return values;
}

int FindFirstCallIntegerArgument(
    const QVMFile& qvm,
    const std::string& function_name) {
    const std::vector<int> values = FindCallIntegerArguments(
        qvm,
        function_name,
        0);
    return values.empty() ? -1 : values.front();
}

} // namespace igi
