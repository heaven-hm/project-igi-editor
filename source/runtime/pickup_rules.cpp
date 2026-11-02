// pickup_rules.cpp - Pickup object definitions, touch proximity, and inventory grant rules.
// Provides layout descriptions, collision distance queries, and lifecycle behaviors for pickups.

#include "runtime/pickup_rules.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace igi {
namespace {

// Parameter layouts for pickup types
constexpr PickupParam kGenericPickupParams[] = {
    {"Position", PickupParamType::ObjectPos},
    {"Orientation", PickupParamType::Real32x9},
    {"Model", PickupParamType::String16},
};

constexpr PickupParam kGunPickupParams[] = {
    {"Position", PickupParamType::ObjectPos},
    {"Orientation", PickupParamType::Real32x9},
    {"ID", PickupParamType::EnumString32},
};

constexpr PickupParam kAmmoPickupParams[] = {
    {"Position", PickupParamType::ObjectPos},
    {"Orientation", PickupParamType::Real32x9},
    {"ID", PickupParamType::EnumString32},
    {"Ammo", PickupParamType::Int16},
};

constexpr PickupParam kAddAmmoParams[] = {
    {"ID", PickupParamType::EnumString32},
    {"Ammo", PickupParamType::Int16},
};

// Index mappings for parameters
constexpr int kEnumArgIndex = 9;
constexpr int kModelArgIndex = 9;
constexpr int kAmmoCountArgIndex = 10;
constexpr int kAddAmmoEnumArgIndex = 3;
constexpr int kAddAmmoCountArgIndex = 4;

constexpr PickupLayout kLayouts[] = {
    {PickupKind::GenericPickup, "GenericPickup", kGenericPickupParams, 3,
     -1, kModelArgIndex, -1, PickupEnumDomain::None, false},
    {PickupKind::GunPickup, "GunPickup", kGunPickupParams, 3,
     kEnumArgIndex, -1, -1, PickupEnumDomain::Weapon, false},
    {PickupKind::AmmoPickup, "AmmoPickup", kAmmoPickupParams, 4,
     kEnumArgIndex, -1, kAmmoCountArgIndex, PickupEnumDomain::Ammo, true},
    {PickupKind::AddAmmo, "AddAmmo", kAddAmmoParams, 2,
     kAddAmmoEnumArgIndex, -1, kAddAmmoCountArgIndex, PickupEnumDomain::Ammo, true},
};

int ArgCountFor(PickupParamType type) {
    switch (type) {
        case PickupParamType::ObjectPos: return 3;
        case PickupParamType::Real32x9: return 3;
        case PickupParamType::String16: return 1;
        case PickupParamType::EnumString32: return 1;
        case PickupParamType::Int16: return 1;
    }
    return 0;
}

double ApplyMinTouch(double r) {
    if (!(r >= kPickupMinTouchUnits)) return kPickupMinTouchUnits;
    return r;
}

}  // namespace

int PickupParamArgCount(PickupParamType type) { return ArgCountFor(type); }

int PickupLayout::ArgCount() const {
    int n = 3;  // task id, task type, instance name
    for (int i = 0; i < param_count; ++i) n += ArgCountFor(params[i].type);
    return n;
}

const PickupLayout* PickupLayouts() { return kLayouts; }

int PickupLayoutCount() { return static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0])); }

const PickupLayout* FindPickupLayout(const std::string& task_type) {
    for (const PickupLayout& layout : kLayouts) {
        if (task_type == layout.task_type) return &layout;
    }
    return nullptr;
}

// --- Proximity & Collision Queries -----------------------------------------

float PickupTouchRange(float model_radius, float max_scale) {
    const float scaled_radius = model_radius * max_scale;
    return static_cast<float>(ApplyMinTouch(static_cast<double>(scaled_radius)));
}

bool PickupTouches(double range, const double pickup_pos[3], const double toucher_pos[3]) {
    const double effective_range = ApplyMinTouch(range);

    const double dx = pickup_pos[0] - toucher_pos[0];
    const double dy = pickup_pos[1] - toucher_pos[1];
    const double dz = std::fabs(pickup_pos[2] - toucher_pos[2]);

    const long double horizontal_dist_sq = static_cast<long double>(dx) * dx +
                                           static_cast<long double>(dy) * dy;
    const long double horizontal_dist = std::sqrt(horizontal_dist_sq);

    return !(horizontal_dist >= effective_range) && !(dz >= kPickupTouchHeightUnits);
}

bool PickupTouchesFromModel(float model_radius, float max_scale, const double pickup_pos[3],
                            const double toucher_pos[3]) {
    return PickupTouches(PickupTouchRange(model_radius, max_scale), pickup_pos, toucher_pos);
}

bool PickupTouchesPlayer(float model_radius, const double pickup_pos[3],
                         const double player_pos[3]) {
    return PickupTouchesFromModel(model_radius, 1.0f, pickup_pos, player_pos);
}

bool IsPlacedPickupType(const std::string& task_type) {
    return task_type == "GenericPickup" || task_type == "GunPickup" ||
           task_type == "AmmoPickup";
}

// --- Grants & Inventory Transfers ------------------------------------------

PickupGrant GenericPickupGrant() {
    return PickupGrant{false, 0, 0};
}

PickupGrant GunPickupGrant(int weapon_id, int weapon_clip_size, bool weapon_found) {
    (void)weapon_found;
    return PickupGrant{false, weapon_id, weapon_found ? weapon_clip_size : 0};
}

int GunPickupGrantedAmmo(int weapon_clip_size) {
    return weapon_clip_size;
}

PickupGrant AmmoPickupGrant(int ammo_id, int authored_count) {
    return PickupGrant{false, ammo_id, TruncatePickupCount(authored_count)};
}

bool AddAmmoApplies(bool parent_is_human, bool ammo_found) {
    return parent_is_human && ammo_found;
}

// --- Lifecycle & Persistence -----------------------------------------------

PickupDisposition PickupDispositionFor(PickupKind kind) {
    switch (kind) {
        case PickupKind::GenericPickup:
            return PickupDisposition::RemoveFromWorld;
        case PickupKind::GunPickup:
        case PickupKind::AmmoPickup:
        case PickupKind::AddAmmo:
            return PickupDisposition::DeleteTask;
    }
    return PickupDisposition::DeleteTask;
}

bool PickupIsOneShot(PickupKind kind) {
    (void)kind;
    return true;
}

std::string PickupScriptFlagName(const std::string& task_type, int task_id) {
    return task_type + "_" + std::to_string(task_id) + ".isPickedUp";
}

// --- Argument Validation ---------------------------------------------------

PickupArgCheck CheckPickupArgs(const std::string& task_type,
                               const std::vector<std::string>& arg_tokens) {
    PickupArgCheck out;
    const PickupLayout* layout = FindPickupLayout(task_type);
    if (layout == nullptr) return out;

    out.is_pickup = true;
    out.expected_tokens = layout->ArgCount();
    out.token_count_ok = static_cast<int>(arg_tokens.size()) == out.expected_tokens;

    if (layout->enum_arg_index >= 0 &&
        static_cast<int>(arg_tokens.size()) > layout->enum_arg_index) {
        const std::string& value = arg_tokens[layout->enum_arg_index];
        out.enum_present = !value.empty();
        if (out.enum_present) {
            const char* prefix = (layout->enum_domain == PickupEnumDomain::Weapon) ? "WEAPON_ID_"
                                                                                  : "AMMO_ID_";
            out.enum_known = (value.rfind(prefix, 0) == 0);
        }
    } else if (layout->model_arg_index >= 0 &&
               static_cast<int>(arg_tokens.size()) > layout->model_arg_index) {
        out.enum_present = !arg_tokens[layout->model_arg_index].empty();
        out.enum_known = out.enum_present;
    }

    if (layout->count_arg_index >= 0 &&
        static_cast<int>(arg_tokens.size()) > layout->count_arg_index) {
        const std::string& text = arg_tokens[layout->count_arg_index];
        if (!text.empty()) {
            char* end = nullptr;
            const long value = std::strtol(text.c_str(), &end, 10);
            if (end != nullptr && *end == '\0') {
                out.count_ok = true;
                out.count = TruncatePickupCount(static_cast<int>(value));
            }
        }
    } else if (layout->count_arg_index < 0) {
        out.count_ok = true;
    }

    return out;
}

int TruncatePickupCount(int value) {
    return static_cast<int16_t>(value);
}

}  // namespace igi
