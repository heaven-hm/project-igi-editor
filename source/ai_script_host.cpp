// ai_script_host.cpp - Retail AI QVM event host and native bindings
#include "ai_script_host.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace igi {

namespace {

struct NamedIntegerConstant {
    const char* name;
    int32_t value;
};

void ResetPatrolCursorForScriptedRoute(AiGuardEntity& guard) {
    guard.command_index = -1;
    guard.loop_start_index = -1;
    guard.last_move_index = -1;
    guard.prev_move_index = -1;
    guard.end_index = -1;
    guard.deadline_tick = -1;
    guard.patrol_started = false;
    guard.patrol_stopped = false;
    guard.route.clear();
}

constexpr std::array<NamedIntegerConstant, 24> kEventConstants = {{
    {"AIEVENT_CREATE", 0},
    {"AIEVENT_DELETE", 1},
    {"AIEVENT_DEAD", 2},
    {"AIEVENT_ANIMATION", 3},
    {"AIEVENT_IDLE", 4},
    {"AIEVENT_ALERT", 5},
    {"AIEVENT_ALERT_RESPONSE", 6},
    {"AIEVENT_COMBAT", 7},
    {"AIEVENT_ALARMON", 8},
    {"AIEVENT_ALARMOFF", 9},
    {"AIEVENT_WALK", 10},
    {"AIEVENT_GROUNDIMPACT", 11},
    {"AIEVENT_DOOR", 12},
    {"AIEVENT_FENCE", 13},
    {"AIEVENT_LADDER", 14},
    {"AIEVENT_TAKINGDAMAGE", 15},
    {"AIEVENT_GUNSHOT", 16},
    {"AIEVENT_GRENADETHROWN", 17},
    {"AIEVENT_GRENADELAND", 18},
    {"AIEVENT_FLASHBANG", 19},
    {"AIEVENT_GUNSHOTMISS", 20},
    {"AIEVENT_EXPLOSION", 21},
    {"AIEVENT_ENEMYDETECTION", 22},
    {"AIEVENT_FRIENDLYDETECTION", 23},
}};

constexpr std::array<const char*, 18> kActionNames = {{
    "AIAction_Patrol",
    "AIAction_Combat",
    "AIAction_Dead",
    "AIAction_FallFlat",
    "AIAction_Activate",
    "AIAction_WalkToNode",
    "AIAction_RunToNode",
    "AIAction_FireAtNode",
    "AIAction_FireAtTask",
    "AIAction_PlayAnimation",
    "AIAction_PlaySound",
    "AIAction_MoveToEvent",
    "AIAction_LookAtEvent",
    "AIAction_Stunned",
    "AIAction_KickGrenade",
    "AIAction_RunPanicking",
    "AIAction_Idle",
    "AIAction_SetCombat",
}};

constexpr std::array<const char*, 37> kFunctionNames = {{
    "AIFunction_DefaultHandler",
    "AIFunction_RemoveAlarmActions",
    "AIFunction_SetViewLength",
    "AIFunction_SetAlarmViewLength",
    "AIFunction_SetViewAlpha",
    "AIFunction_SetViewGamma",
    "AIFunction_SetSecondaryViewLength",
    "AIFunction_SetSecondaryAlarmViewLength",
    "AIFunction_SetSecondaryViewAlpha",
    "AIFunction_SetSecondaryViewGamma",
    "AIFunction_SetEventPriority",
    "AIFunction_SetInvulnerability",
    "AIFunction_SetInstantDeath",
    "AIFunction_SetDeathAnimation",
    "AIFunction_SetAlarmTriggerID",
    "AIFunction_SetAlarmControlID",
    "AIFunction_SetAlarmAccess",
    "AIFunction_SetGunnerID",
    "AIFunction_SetScriptIntegerValue",
    "AIFunction_SetScriptRealValue",
    "AIFunction_GetAlarmTriggerID",
    "AIFunction_GetAlarmControlID",
    "AIFunction_GetAlarmAccess",
    "AIFunction_GetGunnerID",
    "AIFunction_GetAlarmControlStatus",
    "AIFunction_GetGunnerStatus",
    "AIFunction_GetScriptIntegerValue",
    "AIFunction_GetCurrentEventType",
    "AIFunction_IsEventBehind",
    "AIFunction_GetScriptRealValue",
    "AIFunction_GetRandomValue",
    "AIFunction_GetEventDistance",
    "AIFunction_GetAlarmTriggerDistance",
    "AIFunction_SetAnimationInterval",
    "AIFunction_AddAnimationEntry",
    "AIFunction_GetAnimationToPlay",
    "AIFunction_SendResponse",
}};

} // namespace

AiScriptHost::AiScriptHost(QvmNativeRegistry& registry)
    : registry_(registry), interpreter_(registry) {
    registry_.SetDynamicValueResolver(
        [this](const std::string& name, QvmRuntimeValue& value) {
            return ResolveDynamicValue(name, value);
        },
        [this](const std::string& name, const QvmRuntimeValue& value, QvmRuntimeValue& out_value) {
            return WriteDynamicValue(name, value, out_value);
        });
    RegisterConstants();
    RegisterNatives();
}

void AiScriptHost::Reset() {
    current_guard_ = nullptr;
    current_event_type_ = 0;
    random_state_ = 0x1F123BB5U;
    last_error_.clear();
}

bool AiScriptHost::LoadProgram(const QVMFile& parsed_file, QvmProgram& out_program) {
    if (interpreter_.LoadProgram(parsed_file, out_program)) {
        last_error_.clear();
        return true;
    }

    last_error_ = interpreter_.GetLastError();
    return false;
}

bool AiScriptHost::ResolveDynamicValue(
    const std::string& name,
    QvmRuntimeValue& out_value) const {
    const AiGuardEntity* guard = GetCurrentGuard();
    if (guard == nullptr || name.rfind("EditVariable_", 0) != 0) {
        return false;
    }

    const auto value = guard->script_variables.find(name);
    out_value = QvmRuntimeValue::FromInt(
        value == guard->script_variables.end() ? 0 : value->second);
    return true;
}

bool AiScriptHost::WriteDynamicValue(
    const std::string& name,
    const QvmRuntimeValue& value,
    QvmRuntimeValue& out_value) {
    AiGuardEntity* guard = GetCurrentGuard();
    if (guard == nullptr || name.rfind("EditVariable_", 0) != 0) {
        return false;
    }

    guard->script_variables[name] = value.int_val;
    out_value = QvmRuntimeValue::FromInt(value.int_val);
    return true;
}

bool AiScriptHost::Run(
    const QvmProgram& program,
    AiGuardEntity& guard,
    int32_t event_type) {
    current_guard_ = &guard;
    current_event_type_ = event_type;
    guard.script_last_event_type = event_type;
    last_error_.clear();

    std::unique_ptr<QvmExecutionContext> context = interpreter_.CreateContext(program);
    const bool succeeded = context != nullptr && context->Run();
    if (!succeeded) {
        last_error_ = context == nullptr
            ? "Failed to create AI QVM execution context"
            : context->GetLastError();
    }

    current_guard_ = nullptr;
    return succeeded;
}

void AiScriptHost::RegisterConstants() {
    for (const NamedIntegerConstant& constant : kEventConstants) {
        registry_.RegisterConstantByName(constant.name, constant.value);
    }

    registry_.RegisterConstantByName("TRUE", 1);
    registry_.RegisterConstantByName("FALSE", 0);
    registry_.RegisterConstantByName("AIACTIONFLAG_NONE", 0);
    registry_.RegisterConstantByName("AIACTIONFLAG_PUSHABLE", 1);
    registry_.RegisterConstantByName("AIALARMACCESS_BEFORECOMBAT", 0);
    registry_.RegisterConstantByName("AIALARMACCESS_AFTERCOMBAT", 1);
    registry_.RegisterConstantByName("HUMANAI_DETECTIONEVENT_GUNSHOT_RANGE", 122880);
    registry_.RegisterConstantByName("HUMANAI_DETECTIONEVENT_GUNSHOT_SILENCED_RANGE", 4096);
    registry_.RegisterRealConstantByName("WORLD_METER", 4096.0);
}

void AiScriptHost::RegisterVoidNative(const std::string& name) {
    registry_.RegisterDeferredFunctionByName(
        name,
        [](QvmExecutionContext&, const QvmNativeCallArguments&) {
            return QvmRuntimeValue::FromInt(0);
        });
}

void AiScriptHost::RegisterNatives() {
    for (const char* name : kActionNames) {
        RegisterVoidNative(name);
    }
    for (const char* name : kFunctionNames) {
        RegisterVoidNative(name);
    }

    registry_.RegisterDeferredFunctionByName(
        "AIAction_PlaySound",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            AiGuardEntity* guard = GetCurrentGuard();
            if (guard != nullptr && script_sound_handler_ != nullptr &&
                arguments.Count() >= 2U) {
                script_sound_handler_(
                    guard->id,
                    guard->position,
                    arguments.GetString(0),
                    arguments.GetInt(1) != 0);
            }
            return QvmRuntimeValue::FromInt(0);
        });

    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetCurrentEventType",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            return QvmRuntimeValue::FromInt(current_event_type_);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetRandomValue",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            return QvmRuntimeValue::FromReal(
                NextRandomUnit() * std::max(0.0, arguments.GetReal(0)));
        },
        QvmValueType::Real);
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetEventDistance",
        [](QvmExecutionContext&, const QvmNativeCallArguments&) {
            return QvmRuntimeValue::FromReal(0.0);
        },
        QvmValueType::Real);
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetAlarmTriggerDistance",
        [](QvmExecutionContext&, const QvmNativeCallArguments&) {
            return QvmRuntimeValue::FromReal(0.0);
        },
        QvmValueType::Real);
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetScriptRealValue",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            const AiGuardEntity* guard = GetCurrentGuard();
            double value = 0.0;
            if (guard != nullptr) {
                const auto stored = guard->script_real_values.find(arguments.GetInt(0));
                if (stored != guard->script_real_values.end()) {
                    value = stored->second;
                }
            }
            return QvmRuntimeValue::FromReal(value);
        },
        QvmValueType::Real);
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetScriptRealValue",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_real_values[arguments.GetInt(0)] =
                    static_cast<float>(arguments.GetReal(1));
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetScriptIntegerValue",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            const AiGuardEntity* guard = GetCurrentGuard();
            int32_t value = 0;
            if (guard != nullptr) {
                const auto stored = guard->script_integer_values.find(arguments.GetInt(0));
                if (stored != guard->script_integer_values.end()) {
                    value = stored->second;
                }
            }
            return QvmRuntimeValue::FromInt(value);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetScriptIntegerValue",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_integer_values[arguments.GetInt(0)] = arguments.GetInt(1);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetInvulnerability",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_invulnerable = arguments.GetInt(0) != 0;
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetInstantDeath",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_instant_death_disabled = arguments.GetInt(0) == 0;
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetAlarmAccess",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_alarm_access = arguments.GetInt(0);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetAlarmTriggerID",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_alarm_trigger_id = arguments.GetInt(0);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetAlarmControlID",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_alarm_control_id = arguments.GetInt(0);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_SetGunnerID",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->script_gunner_id = arguments.GetInt(0);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetAlarmTriggerID",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            const AiGuardEntity* guard = GetCurrentGuard();
            return QvmRuntimeValue::FromInt(
                guard == nullptr ? -1 : guard->script_alarm_trigger_id);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetAlarmControlID",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            const AiGuardEntity* guard = GetCurrentGuard();
            return QvmRuntimeValue::FromInt(
                guard == nullptr ? -1 : guard->script_alarm_control_id);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetGunnerID",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            const AiGuardEntity* guard = GetCurrentGuard();
            return QvmRuntimeValue::FromInt(
                guard == nullptr ? -1 : guard->script_gunner_id);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetAlarmAccess",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            const AiGuardEntity* guard = GetCurrentGuard();
            return QvmRuntimeValue::FromInt(guard == nullptr ? 0 : guard->script_alarm_access);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIFunction_GetAnimationToPlay",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            const AiGuardEntity* guard = GetCurrentGuard();
            return QvmRuntimeValue::FromInt(guard == nullptr ? -1 : guard->requested_animation);
        });

    registry_.RegisterDeferredFunctionByName(
        "AIAction_Patrol",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                const int requested_path_id = arguments.GetInt(0);
                if (guard->script_patrol_path_id != requested_path_id
                    || guard->patrol_stopped) {
                    const auto route_iterator = guard->patrol_routes.find(
                        requested_path_id);
                    if (route_iterator != guard->patrol_routes.end()) {
                        guard->patrol_commands = route_iterator->second;
                        guard->active_patrol_path_id = requested_path_id;
                        ResetPatrolCursorForScriptedRoute(*guard);
                    }
                    guard->script_patrol_path_id = requested_path_id;
                }
                guard->script_action_flags = arguments.GetInt(2);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIAction_PlayAnimation",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->requested_animation = arguments.GetInt(0);
                ++guard->animation_request_serial;
                guard->script_action_flags = arguments.GetInt(1);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIAction_Combat",
        [this](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->state = AiGuardState::Combat;
                guard->script_action_flags = arguments.GetInt(0);
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIAction_SetCombat",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->state = AiGuardState::Combat;
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIAction_Dead",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->state = AiGuardState::Dead;
            }
            return QvmRuntimeValue::FromInt(0);
        });
    registry_.RegisterDeferredFunctionByName(
        "AIAction_RunPanicking",
        [this](QvmExecutionContext&, const QvmNativeCallArguments&) {
            if (AiGuardEntity* guard = GetCurrentGuard()) {
                guard->state = AiGuardState::Suspicious;
            }
            return QvmRuntimeValue::FromInt(0);
        });
}

double AiScriptHost::NextRandomUnit() {
    random_state_ = random_state_ * 1664525U + 1013904223U;
    return static_cast<double>(random_state_) /
        static_cast<double>(std::numeric_limits<uint32_t>::max());
}

} // namespace igi
