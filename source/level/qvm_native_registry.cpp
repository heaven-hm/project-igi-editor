// qvm_native_registry.cpp - QVM Native function and symbol registry implementation
#include "qvm_native_registry.h"

namespace igi {

const std::string QvmNativeRegistry::empty_name_ = "";

QvmNativeRegistry::QvmNativeRegistry() {
    // Register standard IGI 1 core engine natives
    RegisterFunction(0x0001, "Print", [](QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args) {
        return QvmRuntimeValue::FromInt(0);
    });

    RegisterFunction(0x0002, "Random", [](QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args) {
        int max_val = args.empty() ? 100 : args[0].int_val;
        return QvmRuntimeValue::FromInt(max_val > 0 ? (std::rand() % max_val) : 0);
    });
}

void QvmNativeRegistry::RegisterFunction(uint32_t symbol_id, const std::string& name, QvmNativeFn fn) {
    SymbolEntry entry;
    entry.name = name;
    entry.function = fn;
    entry.is_function = true;
    symbols_[symbol_id] = entry;
}

void QvmNativeRegistry::RegisterConstant(uint32_t symbol_id, const std::string& name, int32_t val) {
    SymbolEntry entry;
    entry.name = name;
    entry.constant_value = QvmRuntimeValue::FromInt(val);
    entry.is_constant = true;
    symbols_[symbol_id] = entry;
}

void QvmNativeRegistry::RegisterRealConstant(uint32_t symbol_id, const std::string& name, double val) {
    SymbolEntry entry;
    entry.name = name;
    entry.constant_value = QvmRuntimeValue::FromReal(val);
    entry.is_constant = true;
    symbols_[symbol_id] = entry;
}

bool QvmNativeRegistry::TryExecute(uint32_t symbol_id, QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args, QvmRuntimeValue& out_result) const {
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end() && it->second.is_function && it->second.function) {
        out_result = it->second.function(ctx, args);
        return true;
    }
    return false;
}

bool QvmNativeRegistry::TryGetConstant(uint32_t symbol_id, QvmRuntimeValue& out_val) const {
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end() && it->second.is_constant) {
        out_val = it->second.constant_value;
        return true;
    }
    return false;
}

const std::string& QvmNativeRegistry::GetSymbolName(uint32_t symbol_id) const {
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end()) {
        return it->second.name;
    }
    return empty_name_;
}

} // namespace igi
