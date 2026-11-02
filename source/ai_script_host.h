// ai_script_host.h - Retail AI QVM event host and native bindings
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "ai_system.h"
#include "level/qvm_interpreter.h"

namespace igi {

// Binds the shipped AI script vocabulary to one live guard for one event
// dispatch. The host deliberately owns no guard collection; RuntimeWorld
// selects the guard and controls when a fixed-tick event is dispatched.
class AiScriptHost {
public:
    using ScriptSoundHandler = std::function<void(
        uint32_t guard_id,
        const glm::vec3& guard_position,
        const std::string& authored_sound,
        bool relative_to_guard)>;

    explicit AiScriptHost(QvmNativeRegistry& registry);

    void Reset();

    void SetScriptSoundHandler(ScriptSoundHandler script_sound_handler) {
        script_sound_handler_ = std::move(script_sound_handler);
    }

    bool LoadProgram(const QVMFile& parsed_file, QvmProgram& out_program);

    bool Run(
        const QvmProgram& program,
        AiGuardEntity& guard,
        int32_t event_type);

    const std::string& GetLastError() const { return last_error_; }

private:
    void RegisterConstants();
    void RegisterNatives();
    void RegisterVoidNative(const std::string& name);
    bool ResolveDynamicValue(
        const std::string& name,
        QvmRuntimeValue& out_value) const;
    bool WriteDynamicValue(
        const std::string& name,
        const QvmRuntimeValue& value,
        QvmRuntimeValue& out_value);
    AiGuardEntity* GetCurrentGuard() const { return current_guard_; }
    double NextRandomUnit();

    QvmNativeRegistry& registry_;
    QvmInterpreter interpreter_;
    ScriptSoundHandler script_sound_handler_;
    AiGuardEntity* current_guard_ = nullptr;
    int32_t current_event_type_ = 0;
    uint32_t random_state_ = 0x1F123BB5U;
    std::string last_error_;
};

} // namespace igi
