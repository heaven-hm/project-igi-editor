/******************************************************************************
 * @file    fog_qsc.h
 * @brief   The single bridge from a level's parsed QSC to its authored fog.
 *
 * Level::LoadFogInfo used to carry its own inline copy of the engine's rule for
 * "which GlobalLightKeyframe is this level's default state, and what are its fog
 * colour and density". That copy and source/runtime/mission_fog.cpp were two
 * implementations of one rule -- which is how an invented `120.0f` constant
 * survived in one of them (issue #66, fixed in 19d226a4). This header is now the
 * only bridge: LoadFogInfo is its only editor caller, and
 * tests/test_mission_fog_wiring.cpp drives it over the retail corpus.
 *
 * Oracle: IGI-RE. The declared parameter order comes from GlobalLight_Open's
 * ParamTable (src/animcam/globallight.c:264-285) -- "Fog color terrain" is
 * parameters 4..6 and "Fog density terrain" is parameter 7, so with the three
 * leading Task_New arguments (id, type, name) they are QSC args 7..9 and 10.
 * The density is converted to the renderer's scale by publish()
 * (src/animcam/globallight.c:115,117-140). The full citation set is in
 * source/runtime/mission_fog.h.
 *
 * ONE intentional difference from the inline copy this replaced, on malformed
 * input only: mission_fog requires the keyframe's whole declared parameter list
 * (54 QSC arguments), while the old loop used any keyframe whose arg 10 was a
 * number -- with a missing colour it would have used its 0.15 grey default, and
 * it took "the last number seen" as Time. Every shipped level authors all 51
 * parameters (the Windows-lane corpus test asserts 54 arguments for all fourteen,
 * through the editor's own decompiler), and a level that does not is malformed,
 * so the stricter rule is the honest one: such a keyframe is ignored and the
 * caller keeps its own default rather than being handed a half-read fog.
 *****************************************************************************/

#pragma once

#include "level_common.h"
#include "runtime/mission_fog.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace igi {

// One QSC argument as a mission_fog token.
//
// The float format is %.9g of the FLOAT value, and that is load-bearing: the QSC
// parser stores every numeric token as a double (level_common.cpp ParseArgs:
// `new_arg->type_ = DBL; new_arg->dbl_ = atof(...)`), while mission_fog re-parses
// its tokens with strtof. %.9g is FLT_DECIMAL_DIG, the shortest form that
// round-trips a float exactly, so a token carries the same float the
// pre-refactor code got from `(float)a->dbl_`. (ArgTokenFromArg in
// level_objects_internal.h deliberately uses %.7g instead, because it exists to
// re-serialise a line in the source's own formatting; 7 significant digits can
// land an ulp away from the original float, and that would move the fog distance
// with it.)
//
// STR arguments keep their quotes, matching ArgTokenFromArg, and BOOL arguments
// become TRUE/FALSE rather than 1/0. Both therefore parse as "not a number" in
// mission_fog, exactly as the pre-refactor loop ignored any argument that was
// not a DBL.
inline std::string FogTokenFromQscArg(const QSC::arg_s* arg) {
    if (arg == nullptr) return std::string();

    char buffer[64];
    switch (arg->type_) {
        case QSC::arg_s::type_t::STR:
            return "\"" + std::string(arg->str_ != nullptr ? arg->str_ : "") + "\"";
        case QSC::arg_s::type_t::DBL: {
            const float value = static_cast<float>(arg->dbl_);
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
            return std::string(buffer);
        }
        case QSC::arg_s::type_t::BOOL:
            return arg->bool_ ? std::string("TRUE") : std::string("FALSE");
        case QSC::arg_s::type_t::FUNC:
            // A nested call occupies a position in the argument chain, so it must
            // keep its slot: dropping it would shift every later declared
            // parameter by one. (LevelObject::argTokens drops FUNC args, which is
            // fine for a property table but not for a positional parse.)
            return std::string();
    }
    return std::string();
}

// One Task_New call's argument chain as a mission_fog task. Split out from
// FogTaskObjectsFromQsc so the positional layout can be tested without loading a
// level: the host-native lane cannot link QSC::Load (source/common.cpp is a
// Windows-only translation unit), but it can build an argument chain by hand.
inline FogTaskObject FogTaskObjectFromFunc(const QSC::func_s& func, const char* type) {
    FogTaskObject object;
    object.type = type != nullptr ? type : "";
    for (const QSC::arg_s* arg = func.args_; arg != nullptr; arg = arg->next_) {
        object.arg_tokens.push_back(FogTokenFromQscArg(arg));
    }
    return object;
}

// Every GlobalLightKeyframe instance the level authors, in the order
// QSC::FindFuncByStr returns them (a depth-first walk, so the nested keyframes
// inside "GlobalLight" are found), as mission_fog tasks.
//
// QSC_FuncMatchStr (level_common.cpp:555-560) matches on the type argument, so
// the `Task_DeclareParameters("GlobalLightKeyframe", ...)` line is not returned:
// its second argument is the first parameter name, not the type.
inline std::vector<FogTaskObject> FogTaskObjectsFromQsc(const QSC& qsc) {
    const QSC::func_s* funcs[1024];
    const int count = qsc.FindFuncByStr("GlobalLightKeyframe", funcs);

    std::vector<FogTaskObject> objects;
    if (count > 0) objects.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        objects.push_back(FogTaskObjectFromFunc(*funcs[i], "GlobalLightKeyframe"));
    }
    return objects;
}

// The level's default authored fog: mission_fog's smallest-Time keyframe over
// every GlobalLightKeyframe in the level. `authored` stays false when the level
// has no usable keyframe, and the caller keeps its own default rather than being
// handed a silent black fog.
inline MissionFog ResolveAuthoredFogFromQsc(const QSC& qsc) {
    return ResolveMissionFog(FogTaskObjectsFromQsc(qsc));
}

}  // namespace igi
