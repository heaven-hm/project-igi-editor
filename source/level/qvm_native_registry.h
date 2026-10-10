// qvm_native_registry.h - QVM Native function and symbol registry
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace igi {

enum class QvmValueType {
    Null = 0,
    Int = 1,
    Real = 2,
    String = 3,
    Object = 4,
    Address = 5,
    Identifier = 6,
    Symbol = 7
};

struct QvmRuntimeValue {
    QvmValueType type = QvmValueType::Null;
    int32_t int_val = 0;
    double real_val = 0.0;
    std::string str_val;
    void* obj_ptr = nullptr;

    static QvmRuntimeValue FromInt(int32_t val) {
        QvmRuntimeValue v;
        v.type = QvmValueType::Int;
        v.int_val = val;
        v.real_val = static_cast<double>(val);
        return v;
    }

    static QvmRuntimeValue FromReal(double val) {
        QvmRuntimeValue v;
        v.type = QvmValueType::Real;
        v.real_val = val;
        v.int_val = static_cast<int32_t>(val);
        return v;
    }

    static QvmRuntimeValue FromString(const std::string& str) {
        QvmRuntimeValue v;
        v.type = QvmValueType::String;
        v.str_val = str;
        return v;
    }

    static QvmRuntimeValue FromObject(void* ptr) {
        QvmRuntimeValue v;
        v.type = QvmValueType::Object;
        v.obj_ptr = ptr;
        return v;
    }

    static QvmRuntimeValue FromAddress(int32_t address) {
        QvmRuntimeValue value;
        value.type = QvmValueType::Address;
        value.int_val = address;
        return value;
    }

    static QvmRuntimeValue FromIdentifier(const std::string& identifier) {
        QvmRuntimeValue value;
        value.type = QvmValueType::Identifier;
        value.str_val = identifier;
        return value;
    }

    static QvmRuntimeValue FromSymbol(const std::string& symbol_name) {
        QvmRuntimeValue value;
        value.type = QvmValueType::Symbol;
        value.str_val = symbol_name;
        return value;
    }
};

class QvmExecutionContext;

class QvmNativeCallArguments {
public:
    QvmNativeCallArguments(
        QvmExecutionContext& execution_context,
        const std::vector<uint32_t>& argument_addresses);

    size_t Count() const { return argument_addresses_.size(); }
    bool TryGetInt(size_t argument_index, int32_t& out_value) const;
    bool TryGetReal(size_t argument_index, double& out_value) const;
    bool TryGetString(size_t argument_index, std::string& out_value) const;

    int32_t GetInt(size_t argument_index) const;
    double GetReal(size_t argument_index) const;
    std::string GetString(size_t argument_index) const;

private:
    bool TryGetArgument(
        size_t argument_index,
        QvmValueType requested_type,
        QvmRuntimeValue& out_value) const;

    QvmExecutionContext& execution_context_;
    const std::vector<uint32_t>& argument_addresses_;
};

using QvmNativeFn = std::function<QvmRuntimeValue(QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args)>;
using QvmDeferredNativeFn = std::function<QvmRuntimeValue(
    QvmExecutionContext& ctx,
    const QvmNativeCallArguments& args)>;
using QvmDynamicValueResolver = std::function<bool(
    const std::string& name,
    QvmRuntimeValue& out_value)>;
using QvmDynamicValueWriter = std::function<bool(
    const std::string& name,
    const QvmRuntimeValue& value,
    QvmRuntimeValue& out_value)>;

class QvmNativeRegistry {
public:
    QvmNativeRegistry();

    void RegisterFunction(uint32_t symbol_id, const std::string& name, QvmNativeFn fn);
    void RegisterConstant(uint32_t symbol_id, const std::string& name, int32_t val);
    void RegisterRealConstant(uint32_t symbol_id, const std::string& name, double val);

    void RegisterFunctionByName(
        const std::string& name,
        QvmNativeFn fn,
        QvmValueType return_type = QvmValueType::Int);
    void RegisterDeferredFunctionByName(
        const std::string& name,
        QvmDeferredNativeFn fn,
        QvmValueType return_type = QvmValueType::Int);
    void RegisterConstantByName(const std::string& name, int32_t val);
    void RegisterRealConstantByName(const std::string& name, double val);
    void RegisterVariableByName(
        const std::string& name,
        const QvmRuntimeValue& initial_value);
    void SetDynamicValueResolver(
        QvmDynamicValueResolver resolver,
        QvmDynamicValueWriter writer = {});

    bool TryExecute(uint32_t symbol_id, QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args, QvmRuntimeValue& out_result) const;
    bool TryExecuteByName(
        const std::string& name,
        QvmExecutionContext& ctx,
        const std::vector<QvmRuntimeValue>& args,
        QvmRuntimeValue& out_result) const;
    bool TryExecuteDeferredByName(
        const std::string& name,
        QvmExecutionContext& ctx,
        const std::vector<uint32_t>& argument_addresses,
        QvmRuntimeValue& out_result) const;
    bool TryGetConstant(uint32_t symbol_id, QvmRuntimeValue& out_val) const;
    bool TryGetValueByName(const std::string& name, QvmRuntimeValue& out_value) const;
    bool TrySetValueByName(
        const std::string& name,
        const QvmRuntimeValue& value,
        QvmRuntimeValue& out_value);
    QvmValueType GetReturnTypeByName(const std::string& name) const;

    const std::string& GetSymbolName(uint32_t symbol_id) const;

private:
    struct SymbolEntry {
        std::string name;
        QvmNativeFn function;
        QvmDeferredNativeFn deferred_function;
        QvmRuntimeValue constant_value;
        QvmRuntimeValue variable_value;
        QvmValueType return_type = QvmValueType::Int;
        bool is_function = false;
        bool is_constant = false;
        bool is_variable = false;
    };

    std::unordered_map<uint32_t, SymbolEntry> symbols_;
    std::unordered_map<std::string, uint32_t> symbol_ids_by_name_;
    QvmDynamicValueResolver dynamic_value_resolver_;
    QvmDynamicValueWriter dynamic_value_writer_;
    uint32_t next_dynamic_symbol_id_ = 0x80000000U;
    uint32_t GetOrCreateSymbolId(const std::string& name);
    static const std::string empty_name_;
};

} // namespace igi
