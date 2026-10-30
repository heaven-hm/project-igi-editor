/******************************************************************************
 * @file    weapon_model_lookup.cpp
 * @brief   See weapon_model_lookup.h for the engine evidence.
 *****************************************************************************/

#include "runtime/weapon_model_lookup.h"

#include "runtime/weapon_config.h"

#include <filesystem>
#include <mutex>
#include <system_error>
#include <utility>

namespace igi {
namespace weapons {
namespace {

std::string NormalizeRoot(std::string root) {
    while (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    return root;
}

}  // namespace

std::map<std::string, std::string> LoadWeaponModelRoster(const std::string& game_root) {
    std::map<std::string, std::string> out;
    if (game_root.empty()) return out;

    const std::string weapons_dir = NormalizeRoot(game_root) + "/WEAPONS";
    std::error_code ec;
    if (!std::filesystem::is_directory(weapons_dir, ec)) return out;

    // WeaponType_Open (HOOK 0x413A90, src/weapon/weapontype.c:34-51) runs
    // `weapon.qsc` for every subdirectory of LOCAL:weapons. The shipped build
    // compiles each one to WEAPONS/<NAME>/WEAPON.QVM, so that is the file to
    // scan. `WEAPONS/weaponconfig.qvm` is a file in the same directory and is
    // skipped by the is_directory check, exactly as the engine's subdirectory
    // walk skips it.
    for (std::filesystem::directory_iterator it(weapons_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code is_dir_ec;
        if (!it->is_directory(is_dir_ec) || is_dir_ec) continue;
        const std::filesystem::path file = it->path() / "WEAPON.QVM";
        std::error_code exists_ec;
        if (!std::filesystem::is_regular_file(file, exists_ec) || exists_ec) continue;

        // DefineWeaponType is the file's only call, and every fixed argument is
        // a literal (tests/test_weapon_config.cpp asserts that over the whole
        // roster). A malformed or user-authored file is skipped, never guessed.
        const CallArgs call = ReadCallArgsFromQvmFile(file.string(), "DefineWeaponType");
        if (!call.found) continue;
        const WeaponTypeConfig cfg = ParseWeaponTypeArgs(call.args);

        // Argument 1 is the constant name DefineWeaponType registers against
        // argument 0 (src/weapon/weapontype.c:306); argument 29 is `wt->key`
        // (+0x61C), which Gun_ParamChangedHandler feeds to VirModel_Find
        // (src/guncasing/gun.c:506).
        const std::string name = cfg.Name();
        const std::string model = cfg.Model();
        if (name.rfind("WEAPON_ID_", 0) != 0 || model.empty()) continue;
        out[name] = model;
    }
    return out;
}

std::string FindWeaponModelForEnum(const std::string& game_root,
                                   const std::string& enum_name) {
    if (game_root.empty() || enum_name.empty()) return std::string();

    static std::mutex cache_mutex;
    static std::map<std::string, std::map<std::string, std::string>> cache;

    const std::string root = NormalizeRoot(game_root);
    std::map<std::string, std::string>* roster = nullptr;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(root);
        if (it == cache.end()) {
            it = cache.emplace(root, LoadWeaponModelRoster(root)).first;
        }
        roster = &it->second;
    }
    const auto found = roster->find(enum_name);
    return (found == roster->end()) ? std::string() : found->second;
}

std::string FindWeaponModelForKey(const std::string& game_root,
                                  const std::string& weapon_type_key) {
    if (game_root.empty() || weapon_type_key.empty()) return std::string();

    // The roster is (constant name -> key). WeaponType_FindByKey compares the
    // query against the KEY (weapontype.c:141), so build the reverse relation
    // from the same scan and answer with the matched key: Gun tests
    //     `key == key`, and the model the gun loads is `wt->key`
    // (gun.c:495-497). The first match wins, which is the engine's scan order
    // (the roster is a std::map over directory names; a duplicate key is not
    // shipped -- measured: 24 constants, 24 distinct keys).
    static std::mutex cache_mutex;
    static std::map<std::string, std::map<std::string, std::string>> cache;

    const std::string root = NormalizeRoot(game_root);
    std::map<std::string, std::string>* by_key = nullptr;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(root);
        if (it == cache.end()) {
            std::map<std::string, std::string> built;
            for (const auto& entry : LoadWeaponModelRoster(root)) {
                built.emplace(entry.second, entry.second);
            }
            it = cache.emplace(root, std::move(built)).first;
        }
        by_key = &it->second;
    }
    const auto found = by_key->find(weapon_type_key);
    return (found == by_key->end()) ? std::string() : found->second;
}

}  // namespace weapons
}  // namespace igi
