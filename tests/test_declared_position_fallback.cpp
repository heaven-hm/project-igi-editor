// Tests for the declared-position fallback (source/level/level_objects.cpp).
//
// WHY THIS EXISTS
// ---------------
// LoadRecursive() in source/level/level_objects.cpp only has explicit branches for the
// task types the editor models by hand. Every other type fell into a final `else` that
// read nothing but the task id, so those objects loaded at the world origin. In the
// retail scripts that is 2438 of 2944 Task_New lines; ~1757 of them are types that
// genuinely carry a world position at QSC args 3/4/5 (TerrainLightMap 813, EditRigidObj
// 582, EditCamera 111, StatusMessage 73, Building 57, ComputerHilight 45, EditVariable
// 15, AreaActivate 15, TextureModifier 13, Terminal 6, Smoke 6, LevelTimer 3,
// HeightMap 2, LevelFlow 3, AmmoPickup 13).
//
// That is not cosmetic: app_level.cpp runs SnapObjectsToTerrain() right after load, so
// every one of those objects was snapped to the terrain height at (0,0,0) and then
// written back to the QSC on save — silent level corruption.
//
// THE RULE UNDER TEST
// -------------------
// The level file is itself the oracle: it declares every type it uses with a
// Task_DeclareParameters line, and exactly the types that carry a world position declare
// ("Position","ObjectPos") as their FIRST parameter — which lands at arg 3, since args
// 0-2 are id, type and note. Types that merely happen to have three numbers at args 3/4/5
// (LightmapInfo = Texture scale/Passes/Hemicube resolution, Dirlight = bools,
// PatrolPathCommand = Command/Int32, AddAmmo = ID/EnumString32, Binocular = ID, ...)
// declare something else there and must NOT be treated as positions.
//
// Every declaration string below is copied VERBATIM from the retail scripts
// (MISSIONS/location*/LEVEL*/objects.qsc, shipped with the game). Nothing here is
// inferred from documentation.
//
// WHAT IS *NOT* PROVEN HERE
// -------------------------
// The wiring — `LoadRecursive` flagging a candidate and `Load()` placing or reverting it —
// cannot run in this host-native lane: `QSC::Load` and `LevelObjects::Load` both need
// `common.cpp` (File_LoadText / MEM_ALLOC) and `logger.cpp`, which are Windows-only. The
// wiring is therefore compile-verified by `tools/igi-re-parity/tu-check.py`, and the
// decision it depends on is what these tests pin down, over real retail declarations.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "level/level_objects.h"
#include "level/task_schema.h"

namespace {

// Split a Task_DeclareParameters argument list into tokens, honouring double quotes.
// Input is the text between the outermost parentheses, e.g.
//   "TerrainLightMap", "Position", "ObjectPos", "Level", "Int32"
std::vector<std::string> SplitDeclArgs(const std::string& args) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
        if (c == '"') {
            inQuotes = !inQuotes;
            cur.push_back(c);
        } else if (c == ',' && !inQuotes) {
            std::string tok = cur;
            while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.erase(tok.begin());
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) tok.pop_back();
            out.push_back(tok);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::string tok = cur;
    while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.erase(tok.begin());
    while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) tok.pop_back();
    if (!tok.empty()) out.push_back(tok);
    return out;
}

// The declaration lines below are verbatim from the retail scripts.
struct DeclCase {
    const char* typeName;
    const char* args;  // text between the parentheses of Task_DeclareParameters
};

// Types that DO carry a world position as their first declared parameter.
const DeclCase kPositional[] = {
    {"TerrainLightMap", "\"TerrainLightMap\", \"Position\", \"ObjectPos\", \"Level\", \"Int32\", \"Size\", \"Int32\", \"TextureIndex\", \"Int32\", \"GeneratedWhenCreated\", \"bool8\""},
    {"EditRigidObj", "\"EditRigidObj\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Model\", \"String16\", \"Dirlight Angular Effect\", \"RGB\", \"Dirlight Ambient Effect\", \"RGB\""},
    {"EditCamera", "\"EditCamera\", \"Position\", \"ObjectPos\", \"Alpha\", \"Angle\", \"Beta\", \"Angle\", \"Gamma\", \"Angle\", \"FOV\", \"Real32\", \"Duration\", \"Real32\", \"Link task ID\", \"Int32\""},
    {"Building", "\"Building\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Model\", \"String16\""},
    {"StatusMessage", "\"StatusMessage\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Send\", \"VarString\", \"Text\", \"VarString\""},
    {"ComputerHilight", "\"ComputerHilight\", \"Position\", \"ObjectPos\", \"Hilight\", \"VarString\", \"TaskID\", \"String256\""},
    {"AreaActivate", "\"AreaActivate\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Dimensions\", \"Real32x3\", \"Criteria\", \"VarString\""},
    {"TextureModifier", "\"TextureModifier\", \"Position\", \"ObjectPos\", \"Level\", \"Int32\", \"Material Index\", \"Int32\", \"Bitmap ID\", \"Int32\", \"Size\", \"Int32\", \"isEdit\", \"bool8\""},
    {"EditVariable", "\"EditVariable\", \"Position\", \"ObjectPos\", \"Initial value\", \"Int32\", \"Add\", \"VarString\", \"Sub\", \"VarString\""},
    {"Terminal", "\"Terminal\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Model\", \"String16\", \"Destroyed model\", \"String16\""},
    {"Smoke", "\"Smoke\", \"Position\", \"ObjectPos\", \"Alpha\", \"Angle\", \"Gamma\", \"Angle\", \"Number of Particles\", \"Int32\", \"Radius\", \"Real32\""},
    {"LevelTimer", "\"LevelTimer\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"On\", \"VarString\", \"Reset\", \"VarString\", \"Initial run\", \"bool8\""},
    {"HeightMap", "\"HeightMap\", \"Position\", \"ObjectPos\", \"Level\", \"Int32\", \"Size\", \"Int32\", \"isEdit\", \"bool8\""},
    {"LevelFlow", "\"LevelFlow\", \"Position\", \"ObjectPos\""},
    {"AmmoPickup", "\"AmmoPickup\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"Model\", \"String16\""},
};

// Types that do NOT: their args 3/4/5 are three numbers (or a string and numbers) that
// must never be mistaken for coordinates.
const DeclCase kNonPositional[] = {
    {"LightmapInfo", "\"LightmapInfo\", \"Texture scale\", \"Real32\", \"Passes\", \"Int32\", \"Hemicube resolution\", \"Int32\", \"Dirlight resolution\", \"Int32\""},
    {"PatrolPathCommand", "\"PatrolPathCommand\", \"Command\", \"Int32\""},
    {"ConditionalSound", "\"ConditionalSound\", \"Condition\", \"VarString\", \"Sound\", \"VarString\""},
    {"ConditionalContainer", "\"ConditionalContainer\", \"Condition\", \"VarString\""},
    {"AddAmmo", "\"AddAmmo\", \"ID\", \"EnumString32\", \"Count\", \"Int32\""},
    {"Binocular", "\"Binocular\", \"ID\", \"EnumString32\", \"Count\", \"Int32\""},
    {"LODSettings", "\"LODSettings\", \"Distance to first switch, relative to object radius (Spline Objects)\", \"Real32\""},
    {"GlobalLight", "\"GlobalLight\", \"Radiosity intensity\", \"Real32\""},
    {"FlatSky", "\"FlatSky\", \"Fog Amount\", \"Real32\""},
    {"GlobalLightKeyframe", "\"GlobalLightKeyframe\", \"Link All Sliders\", \"PushButton\""},
    {"DirlightKeyframe", "\"DirlightKeyframe\", \"Beta\", \"Angle\""},
    {"RainEffect", "\"RainEffect\", \"Is Rain\", \"bool8\""},
    {"FlatSkyLayer", "\"FlatSkyLayer\", \"Texture File Name\", \"String256\""},
    {"DefineComputerObjective", "\"DefineComputerObjective\", \"Objectives Valid\", \"VarString\""},
    {"GuardGenerator", "\"GuardGenerator\", \"Generate guards\", \"VarString\""},
};

TaskSchemaNS::TaskSchema SchemaFrom(const char* args) {
    return TaskSchemaNS::ParseDeclaration(SplitDeclArgs(args));
}

}  // namespace

// The rule is what decides whether ~1757 retail objects get their real coordinates.
TEST(DeclaredPositionFallbackTest, PositionalTypesAreRecognised) {
    for (const auto& c : kPositional) {
        const auto schema = SchemaFrom(c.args);
        ASSERT_FALSE(schema.empty()) << c.typeName;
        // Sanity: the declaration must actually parse to Position/ObjectPos at arg 3.
        EXPECT_EQ(schema.front().name, "Position") << c.typeName;
        EXPECT_EQ(schema.front().typeName, "ObjectPos") << c.typeName;
        EXPECT_EQ(schema.front().argOffset, 3) << c.typeName;
        EXPECT_TRUE(TaskDeclarationHasPositionAt3(schema))
            << c.typeName << " declares a world position at args 3/4/5";
    }
}

// The guard against inventing coordinates: these types have three numbers at args 3/4/5
// in the retail data (LightmapInfo: 1, 1, 450; Dirlight: TRUE, TRUE, 18; FlatSky:
// 0.94166, 4.0, 100.0; DirlightKeyframe: 0.5236, -4.9218, 1) but they are NOT positions.
TEST(DeclaredPositionFallbackTest, NonPositionalTypesAreRejected) {
    for (const auto& c : kNonPositional) {
        const auto schema = SchemaFrom(c.args);
        ASSERT_FALSE(schema.empty()) << c.typeName;
        EXPECT_FALSE(TaskDeclarationHasPositionAt3(schema))
            << c.typeName << " must NOT be treated as a positioned task";
    }
}

// A type the level never declares must not be guessed at either: Load() reverts the
// candidate, keeping the previous behaviour of loading at the origin.
TEST(DeclaredPositionFallbackTest, UndeclaredTypesAreNotGuessedAt) {
    EXPECT_FALSE(TaskDeclarationHasPositionAt3(TaskSchemaNS::TaskSchema{}));

    // PatrolPath has no Task_DeclareParameters line in any retail level, and its args 3/4/5
    // are the first child's tokens rather than coordinates.
    const auto empty = SchemaFrom("\"PatrolPath\"");
    EXPECT_TRUE(empty.empty());
    EXPECT_FALSE(TaskDeclarationHasPositionAt3(empty));
}

// The rule keys off the TYPE name, so a differently-worded position field still counts.
TEST(DeclaredPositionFallbackTest, AnyPositionFieldNameWithObjectPosTypeCounts) {
    const auto renamed = SchemaFrom("\"SomeTask\", \"Where\", \"ObjectPos\", \"Model\", \"String16\"");
    ASSERT_EQ(renamed.size(), 2u);
    EXPECT_EQ(renamed.front().name, "Where");
    EXPECT_TRUE(TaskDeclarationHasPositionAt3(renamed));
}

// A declaration that puts a position somewhere OTHER than the first parameter must not be
// treated as "position at args 3/4/5" — e.g. a hypothetical type with a flag first.
TEST(DeclaredPositionFallbackTest, PositionNotAtArg3DoesNotCount) {
    const auto later = SchemaFrom("\"SomeTask\", \"Flag\", \"bool8\", \"Position\", \"ObjectPos\"");
    ASSERT_EQ(later.size(), 2u);
    EXPECT_EQ(later[1].argOffset, 4);
    EXPECT_FALSE(TaskDeclarationHasPositionAt3(later));
}

// ═════════════════════════════════════════════════════════════════════════════
// Declared-model fallback (the second pass in LevelObjects::Load)
//
// LoadRecursive reads a model only for the task types the editor models by hand.
// Every engine type that DRAWS a model registers one Param_String16 slot bound to
// its `FIELD(model)` member and feeds that member to RigidDynCubeObj_SetModel
// (src/missile/train.c:149, src/props/wire.c:180, src/scamera/scamera.c:240,
// src/camera/explodeobject.c:279). The level file mirrors those ParamTables entry
// for entry in Task_DeclareParameters, so `FindDeclaredModelArg` recovers the slot
// from the level itself — no per-type branch, and no guess at the token's shape.
//
// The shipped type this was missing for is PlaceExplosiveTBA: it extends
// GenericTBA -> ExplodeObject, GenericTBA_Create runs the ExplodeObject create
// handler (src/glass/tba.c:99-100), and that handler installs the authored model.
// The old isMissingGeneric scan also gated on an 8-character NNN_NN_N shape, which
// is why AlarmLight needed a hand-written exception for "1729_01_1".
//
// The declarations below are the retail ones (the prefix shown is verbatim from
// the decompiled OBJECTS.QVM; fields after the model slots are elided where the
// case does not depend on them).
// ═════════════════════════════════════════════════════════════════════════════

namespace {

struct ModelCase {
    const char* typeName;
    const char* args;
    int expectedArg;
};

const ModelCase kDeclaredModelSlots[] = {
    // LEVEL6 "PlaceExplosiveTBA" — the type this fallback was added for.
    {"PlaceExplosiveTBA",
     "\"PlaceExplosiveTBA\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", "
     "\"Model\", \"String16\", \"Destroyed model\", \"String16\", \"Damage scale\", \"Real32\", "
     "\"Explosion radius\", \"Real32\", \"Explosion falloff radius\", \"Real32\", "
     "\"Explosion damage scale\", \"Real32\", \"Explosion delay\", \"Real32\", "
     "\"Explosion fragments\", \"Int32\", \"Explosion fireballs\", \"Int32\", "
     "\"Explosion expression\", \"VarString\", \"Explosion sound\", \"String16\", "
     "\"Explodable\", \"bool8\", \"On Expression\", \"VarString\", "
     "\"Activate Anim\", \"Int16\", \"Activate Time (s)\", \"Real32\", "
     "\"Click to select sprite\", \"DropDownCombo\", \"Activate radius (meters)\", \"Real32\", "
     "\"Task ID to place\", \"Int32\"",
     9},
    // LEVEL14 — "Holder model" is the third entry, arg 9 (gunholder.c:158-165).
    {"AIStationaryGunHolder",
     "\"AIStationaryGunHolder\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", "
     "\"Holder model\", \"String16\", \"Viewcone Alpha\", \"Degrees\", "
     "\"Viewcone Gamma\", \"Degrees\", \"Viewcone Length\", \"Real32\", "
     "\"On expression\", \"VarString\", \"Team expression\", \"VarString\"",
     9},
    // "Model" at arg 9, before "Destroyed model": the first slot is the drawn one.
    {"Siren",
     "\"Siren\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", "
     "\"Model\", \"String16\", \"Destroyed model\", \"String16\", \"Damage scale\", \"Real32\"",
     9},
    // Five model slots; "On model" (arg 11) is the one the engine installs at Create.
    {"Switch",
     "\"Switch\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", "
     "\"On\", \"VarString\", \"Initial on\", \"bool8\", \"On model\", \"String16\", "
     "\"On pressed model\", \"String16\", \"Off model\", \"String16\", "
     "\"Off pressed model\", \"String16\", \"Destroyed model\", \"String16\", "
     "\"Destructable\", \"bool8\"",
     11},
    // SCamera's HOLDER model (arg 7) is the body it draws; the lens (arg 10) belongs
    // to the SCameraLens child the engine creates at runtime (scamera.c:204).
    {"SCamera",
     "\"SCamera\", \"Holder Position\", \"ObjectPos\", \"Holder Gamma\", \"Angle\", "
     "\"Holder Model\", \"String16\", \"Camera Alpha\", \"Angle\", \"Camera Gamma\", \"Angle\", "
     "\"Camera Model\", \"String16\", \"Camera Destroyed Model\", \"String16\"",
     7},
    // AlarmLight's first model-named slot ("On model", arg 10) is NOT what the engine
    // installs: alarm.c:113 wires the authored "Off model" slot to onModel, which is
    // arg 11 and which the explicit LoadRecursive branch reads. This expectation
    // pins that difference — it is why the generic pass skips AlarmLight.
    {"AlarmLight",
     "\"AlarmLight\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", "
     "\"On expression\", \"VarString\", \"On model\", \"String16\", \"Off model\", \"String16\", "
     "\"Destroyde model\", \"String16\", \"On sound\", \"String16\", \"Explode sound\", \"String16\"",
     10},
    // SplineObjWaypoint's slot is the engine waypoint marker ("waypoint"/empty),
    // which LoadRecursive deliberately clears and renderer_splines.cpp replaces with
    // the gizmo — so it is also skipped by the generic pass.
    {"SplineObjWaypoint",
     "\"SplineObjWaypoint\", \"Orientation\", \"Real32x9\", \"Position\", \"ObjectPos\", "
     "\"Waypoint Model\", \"String16\", \"Segment Model\", \"String16\", \"NumAreas\", \"Int32\", "
     "\"Align\", \"bool8\", \"Flip\", \"bool8\", \"Automatic Orientation\", \"bool8\"",
     9},
};

// Declarations with NO model slot must yield -1: the pass must not invent one.
const char* kNoModelSlotDecls[] = {
    // GunPickup's third parameter is a weapon ENUM, not a model (pickup.c:477-479).
    "\"GunPickup\", \"Position\", \"ObjectPos\", \"Orientation\", \"Real32x9\", \"ID\", \"EnumString32\"",
    // No model at all.
    "\"GuardGenerator\", \"Generate guards\", \"VarString\", \"Max spawns\", \"Int32\"",
    // PatrolPath declares no fields (gPatrolPathParams is NULL, patrol/path.c:154).
    "\"PatrolPath\"",
    // A model-shaped NAME whose declared type is an expression, not a fixed string:
    // the engine never feeds a VarString slot to VirModel_Find.
    "\"SomeTask\", \"Model\", \"VarString\"",
    // An unrelated fixed string is not a model either.
    "\"SomeTask\", \"On sound\", \"String16\", \"Explode sound\", \"String16\"",
};

}  // namespace

TEST(DeclaredModelFallbackTest, ModelSlotOfRealDeclarations) {
    for (const auto& c : kDeclaredModelSlots) {
        const auto schema = SchemaFrom(c.args);
        ASSERT_FALSE(schema.empty()) << c.typeName;
        EXPECT_EQ(TaskSchemaNS::FindDeclaredModelArg(schema), c.expectedArg)
            << c.typeName << " declares its drawn model at arg " << c.expectedArg;
    }
}

TEST(DeclaredModelFallbackTest, NoModelSlotIsNeverGuessed) {
    for (const char* decl : kNoModelSlotDecls) {
        const auto schema = SchemaFrom(decl);
        EXPECT_EQ(TaskSchemaNS::FindDeclaredModelArg(schema), -1) << decl;
    }
}

// The empty schema the property panel now renders for parameter-less engine types
// has no model slot either.
TEST(DeclaredModelFallbackTest, EmptySchemaHasNoModelSlot) {
    EXPECT_EQ(TaskSchemaNS::FindDeclaredModelArg(TaskSchemaNS::TaskSchema{}), -1);
}
