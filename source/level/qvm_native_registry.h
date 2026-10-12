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
    Object = 4
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
};

class QvmExecutionContext;

using QvmNativeFn = std::function<QvmRuntimeValue(QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args)>;

class QvmNativeRegistry {
public:
    QvmNativeRegistry();

    void RegisterFunction(uint32_t symbol_id, const std::string& name, QvmNativeFn fn);
    void RegisterConstant(uint32_t symbol_id, const std::string& name, int32_t val);
    void RegisterRealConstant(uint32_t symbol_id, const std::string& name, double val);

    bool TryExecute(uint32_t symbol_id, QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args, QvmRuntimeValue& out_result) const;
    bool TryGetConstant(uint32_t symbol_id, QvmRuntimeValue& out_val) const;

    const std::string& GetSymbolName(uint32_t symbol_id) const;

private:
    struct SymbolEntry {
        std::string name;
        QvmNativeFn function;
        QvmRuntimeValue constant_value;
        bool is_function = false;
        bool is_constant = false;
    };

    std::unordered_map<uint32_t, SymbolEntry> symbols_;
    static const std::string empty_name_;
};

} // namespace igi
