// qvm_native_registry.cpp - QVM Native function and symbol registry implementation
#include "qvm_native_registry.h"
#include "qvm_interpreter.h"
#include <algorithm>
#include <exception>
#include <utility>

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

uint32_t QvmNativeRegistry::GetOrCreateSymbolId(const std::string& name) {
    const auto existing = symbol_ids_by_name_.find(name);
    if (existing != symbol_ids_by_name_.end()) {
        return existing->second;
    }

    while (symbols_.find(next_dynamic_symbol_id_) != symbols_.end()) {
        ++next_dynamic_symbol_id_;
    }

    const uint32_t symbol_id = next_dynamic_symbol_id_++;
    symbol_ids_by_name_[name] = symbol_id;
    return symbol_id;
}

void QvmNativeRegistry::RegisterFunction(uint32_t symbol_id, const std::string& name, QvmNativeFn fn) {
    SymbolEntry entry;
    entry.name = name;
    entry.function = fn;
    entry.return_type = QvmValueType::Int;
    entry.is_function = true;
    symbols_[symbol_id] = entry;
    symbol_ids_by_name_[name] = symbol_id;
}

void QvmNativeRegistry::RegisterConstant(uint32_t symbol_id, const std::string& name, int32_t val) {
    SymbolEntry entry;
    entry.name = name;
    entry.constant_value = QvmRuntimeValue::FromInt(val);
    entry.return_type = QvmValueType::Int;
    entry.is_constant = true;
    symbols_[symbol_id] = entry;
    symbol_ids_by_name_[name] = symbol_id;
}

void QvmNativeRegistry::RegisterRealConstant(uint32_t symbol_id, const std::string& name, double val) {
    SymbolEntry entry;
    entry.name = name;
    entry.constant_value = QvmRuntimeValue::FromReal(val);
    entry.return_type = QvmValueType::Real;
    entry.is_constant = true;
    symbols_[symbol_id] = entry;
    symbol_ids_by_name_[name] = symbol_id;
}

void QvmNativeRegistry::RegisterFunctionByName(
    const std::string& name,
    QvmNativeFn fn,
    QvmValueType return_type) {
    const uint32_t symbol_id = GetOrCreateSymbolId(name);
    SymbolEntry entry;
    entry.name = name;
    entry.function = std::move(fn);
    entry.return_type = return_type;
    entry.is_function = true;
    symbols_[symbol_id] = std::move(entry);
}

void QvmNativeRegistry::RegisterDeferredFunctionByName(
    const std::string& name,
    QvmDeferredNativeFn fn,
    QvmValueType return_type) {
    const uint32_t symbol_id = GetOrCreateSymbolId(name);
    SymbolEntry entry;
    entry.name = name;
    entry.deferred_function = std::move(fn);
    entry.return_type = return_type;
    entry.is_function = true;
    symbols_[symbol_id] = std::move(entry);
}

void QvmNativeRegistry::RegisterConstantByName(const std::string& name, int32_t val) {
    const uint32_t symbol_id = GetOrCreateSymbolId(name);
    SymbolEntry entry;
    entry.name = name;
    entry.constant_value = QvmRuntimeValue::FromInt(val);
    entry.return_type = QvmValueType::Int;
    entry.is_constant = true;
    symbols_[symbol_id] = std::move(entry);
}

void QvmNativeRegistry::RegisterRealConstantByName(const std::string& name, double val) {
    const uint32_t symbol_id = GetOrCreateSymbolId(name);
    SymbolEntry entry;
    entry.name = name;
    entry.constant_value = QvmRuntimeValue::FromReal(val);
    entry.return_type = QvmValueType::Real;
    entry.is_constant = true;
    symbols_[symbol_id] = std::move(entry);
}

void QvmNativeRegistry::RegisterVariableByName(
    const std::string& name,
    const QvmRuntimeValue& initial_value) {
    const uint32_t symbol_id = GetOrCreateSymbolId(name);
    SymbolEntry entry;
    entry.name = name;
    entry.variable_value = initial_value;
    entry.return_type = initial_value.type;
    entry.is_variable = true;
    symbols_[symbol_id] = std::move(entry);
}

void QvmNativeRegistry::SetDynamicValueResolver(
    QvmDynamicValueResolver resolver,
    QvmDynamicValueWriter writer) {
    dynamic_value_resolver_ = std::move(resolver);
    dynamic_value_writer_ = std::move(writer);
}

bool QvmNativeRegistry::TryExecute(uint32_t symbol_id, QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args, QvmRuntimeValue& out_result) const {
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end() && it->second.is_function && it->second.function) {
        out_result = it->second.function(ctx, args);
        return true;
    }
    return false;
}

bool QvmNativeRegistry::TryExecuteByName(
    const std::string& name,
    QvmExecutionContext& ctx,
    const std::vector<QvmRuntimeValue>& args,
    QvmRuntimeValue& out_result) const {
    const auto id = symbol_ids_by_name_.find(name);
    if (id == symbol_ids_by_name_.end()) {
        return false;
    }
    return TryExecute(id->second, ctx, args, out_result);
}

bool QvmNativeRegistry::TryExecuteDeferredByName(
    const std::string& name,
    QvmExecutionContext& ctx,
    const std::vector<uint32_t>& argument_addresses,
    QvmRuntimeValue& out_result) const {
    const auto id = symbol_ids_by_name_.find(name);
    if (id == symbol_ids_by_name_.end()) {
        return false;
    }

    const auto symbol = symbols_.find(id->second);
    if (symbol == symbols_.end() || !symbol->second.is_function) {
        return false;
    }

    try {
        if (symbol->second.deferred_function) {
            QvmNativeCallArguments arguments(ctx, argument_addresses);
            out_result = symbol->second.deferred_function(ctx, arguments);
            return true;
        }

        if (!symbol->second.function) {
            return false;
        }

        std::vector<QvmRuntimeValue> evaluated_arguments;
        evaluated_arguments.reserve(argument_addresses.size());
        for (const uint32_t address : argument_addresses) {
            QvmRuntimeValue argument;
            if (!ctx.EvaluateArgumentAtAddress(address, QvmValueType::Null, argument)) {
                return false;
            }
            evaluated_arguments.push_back(std::move(argument));
        }
        out_result = symbol->second.function(ctx, evaluated_arguments);
        return true;
    } catch (const std::exception& exception) {
        ctx.SetError(std::string("QVM native function threw: ") + exception.what());
    } catch (...) {
        ctx.SetError("QVM native function threw an unknown exception");
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

bool QvmNativeRegistry::TryGetValueByName(
    const std::string& name,
    QvmRuntimeValue& out_value) const {
    const auto id = symbol_ids_by_name_.find(name);
    if (id == symbol_ids_by_name_.end()) {
        return dynamic_value_resolver_
            && dynamic_value_resolver_(name, out_value);
    }

    const auto symbol = symbols_.find(id->second);
    if (symbol == symbols_.end()) {
        return dynamic_value_resolver_
            && dynamic_value_resolver_(name, out_value);
    }
    if (symbol->second.is_constant) {
        out_value = symbol->second.constant_value;
        return true;
    }
    if (symbol->second.is_variable) {
        out_value = symbol->second.variable_value;
        return true;
    }
    return dynamic_value_resolver_
        && dynamic_value_resolver_(name, out_value);
}

bool QvmNativeRegistry::TrySetValueByName(
    const std::string& name,
    const QvmRuntimeValue& value,
    QvmRuntimeValue& out_value) {
    const auto id = symbol_ids_by_name_.find(name);
    if (id == symbol_ids_by_name_.end()) {
        return dynamic_value_writer_
            && dynamic_value_writer_(name, value, out_value);
    }

    const auto symbol = symbols_.find(id->second);
    if (symbol == symbols_.end() || !symbol->second.is_variable) {
        return dynamic_value_writer_
            && dynamic_value_writer_(name, value, out_value);
    }

    symbol->second.variable_value = value;
    symbol->second.return_type = value.type;
    out_value = value;
    return true;
}

QvmValueType QvmNativeRegistry::GetReturnTypeByName(const std::string& name) const {
    const auto id = symbol_ids_by_name_.find(name);
    if (id == symbol_ids_by_name_.end()) {
        return QvmValueType::Int;
    }
    const auto symbol = symbols_.find(id->second);
    if (symbol == symbols_.end()) {
        return QvmValueType::Int;
    }
    return symbol->second.return_type;
}

const std::string& QvmNativeRegistry::GetSymbolName(uint32_t symbol_id) const {
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end()) {
        return it->second.name;
    }
    return empty_name_;
}

} // namespace igi

namespace igi {

QvmNativeCallArguments::QvmNativeCallArguments(
    QvmExecutionContext& execution_context,
    const std::vector<uint32_t>& argument_addresses)
    : execution_context_(execution_context), argument_addresses_(argument_addresses) {}

bool QvmNativeCallArguments::TryGetArgument(
    size_t argument_index,
    QvmValueType requested_type,
    QvmRuntimeValue& out_value) const {
    if (argument_index >= argument_addresses_.size()) {
        return false;
    }
    return execution_context_.EvaluateArgumentAtAddress(
        argument_addresses_[argument_index], requested_type, out_value);
}

bool QvmNativeCallArguments::TryGetInt(size_t argument_index, int32_t& out_value) const {
    QvmRuntimeValue value;
    if (!TryGetArgument(argument_index, QvmValueType::Int, value)) {
        out_value = 0;
        return false;
    }
    if (value.type == QvmValueType::Real) {
        out_value = static_cast<int32_t>(value.real_val);
    } else {
        out_value = value.int_val;
    }
    return true;
}

bool QvmNativeCallArguments::TryGetReal(size_t argument_index, double& out_value) const {
    QvmRuntimeValue value;
    if (!TryGetArgument(argument_index, QvmValueType::Real, value)) {
        out_value = 0.0;
        return false;
    }
    out_value = value.type == QvmValueType::Real
        ? value.real_val
        : static_cast<double>(value.int_val);
    return true;
}

bool QvmNativeCallArguments::TryGetString(size_t argument_index, std::string& out_value) const {
    QvmRuntimeValue value;
    if (!TryGetArgument(argument_index, QvmValueType::String, value)
        || value.type != QvmValueType::String) {
        out_value.clear();
        return false;
    }
    out_value = value.str_val;
    return true;
}

int32_t QvmNativeCallArguments::GetInt(size_t argument_index) const {
    int32_t value = 0;
    TryGetInt(argument_index, value);
    return value;
}

double QvmNativeCallArguments::GetReal(size_t argument_index) const {
    double value = 0.0;
    TryGetReal(argument_index, value);
    return value;
}

std::string QvmNativeCallArguments::GetString(size_t argument_index) const {
    std::string value;
    TryGetString(argument_index, value);
    return value;
}

} // namespace igi
