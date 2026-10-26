/******************************************************************************
 * @file    test_magic_object_expand.cpp
 * @brief   Host tests for the magic-object expansion rule (issue #67).
 *
 * Two layers:
 *
 *  1. Synthetic tests that pin every branch of the rule with a fake world, so
 *     the walk is verified without the game corpus.
 *  2. Corpus tests that load the real `magicobj/MAGICOBJ.QVM` and the real
 *     level-1 MEF files, print the expansion inventory, and cross-check the
 *     measured numbers. They self-skip when the corpus is absent.
 *
 * The corpus numbers asserted here were measured on the retail install at
 * `/Users/haseeb-mir/Downloads/IGI` (see docs/igi-re-parity/MAGIC_OBJECTS.md).
 *****************************************************************************/
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "level/qvm_parser.h"
#include "logger.h"
#include "renderer/mef_native.h"
#include "runtime/magic_object_expand.h"
#include "runtime/magic_object_registry.h"

// ─── Host-only logger stub ──────────────────────────────────────────────────
// `source/logger.cpp` includes `source/pch.h`, which pulls in <glew.h>, so the
// host lane's engine pool cannot compile it and `Logger::Log` would be an
// unresolved symbol that only the runner's lazy-link fallback papers over. The
// QVM decompiler reports through Logger, so define the five out-of-line
// members here instead: on the host nothing is lost, and on Windows (where the
// real logger.cpp is linked) this block is compiled out.
#if !defined(_WIN32)
void Logger::Log(LogLevel, const std::string&) {}
void Logger::Init(const std::string&) {}
void Logger::Flush() {}
bool Logger::IsOpen() const { return false; }
std::string Logger::GetLogPath() const { return std::string(); }
#endif

namespace {

using igi::MagicObjectAttachment;
using igi::MagicObjectChildExpansion;
using igi::MagicObjectExpansionResult;
using igi::MagicObjectExpansionSource;
using igi::MagicObjectRegistry;
using igi::MagicObjectSpawn;

// ─── A fake world for the synthetic tests ───────────────────────────────────
struct FakeWorld {
    std::map<std::string, std::vector<MagicObjectAttachment>> attachments;
    std::set<std::string> unavailable;
    std::set<std::string> sectors;

    MagicObjectExpansionSource Source() const {
        MagicObjectExpansionSource source;
        source.attachments_for = [this](const std::string& model) -> const std::vector<MagicObjectAttachment>* {
            const auto it = attachments.find(model);
            return it == attachments.end() ? nullptr : &it->second;
        };
        source.model_available = [this](const std::string& model) {
            return unavailable.count(model) == 0;
        };
        source.model_has_sectors = [this](const std::string& model) {
            return sectors.count(model) != 0;
        };
        return source;
    }
};

MagicObjectAttachment Attachment(const std::string& name,
                                 float x = 0.0f, float y = 0.0f, float z = 0.0f) {
    MagicObjectAttachment attachment;
    attachment.name = name;
    attachment.pos[0] = x;
    attachment.pos[1] = y;
    attachment.pos[2] = z;
    return attachment;
}

// A table big enough for the synthetic graphs.
MagicObjectRegistry SyntheticRegistry() {
    MagicObjectRegistry registry;
    registry.LoadDecompiledSource(
        "DefineMagicObj(\"ATT_A\", \"MODEL_A\", TASKTYPE_MAGICOBJ);\n"
        "DefineMagicObj(\"ATT_B\", \"MODEL_B\", TASKTYPE_MAGICOBJ);\n"
        "DefineMagicObj(\"ATT_C\", \"MODEL_C\", TASKTYPE_MAGICOBJ);\n"
        "DefineMagicObj(\"ATT_SECTOR\", \"MODEL_SECTOR\", TASKTYPE_HITZONE);\n"
        "DefineMagicObj(\"ATT_PLAIN\", \"MODEL_PLAIN\", TASKTYPE_HITZONE);\n"
        "DefineMagicObj(\"ATT_GLASS\", \"MODEL_GLASS\", TASKTYPE_GLASS);\n"
        "DefineMagicObj(\"ATT_ROTOR\", \"MODEL_ROTOR\", TASKTYPE_ROTOR);\n"
        "DefineMagicObj(\"ATT_WHEEL\", \"MODEL_WHEEL\", TASKTYPE_WHEEL);\n"
        "DefineMagicObj(\"ATT_RENAMED\", \"MODEL_REAL\", TASKTYPE_MAGICOBJ);\n");
    return registry;
}

std::vector<std::string> SpawnModels(const MagicObjectExpansionResult& result) {
    std::vector<std::string> models;
    for (const MagicObjectSpawn& spawn : result.spawns) {
        models.push_back(spawn.model_id);
    }
    return models;
}

// ─── Synthetic: definition lookup ───────────────────────────────────────────

TEST(MagicObjectExpandTest, SpawnUsesTheDefinitionsModelNotTheAttachmentName) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_RENAMED")};

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    ASSERT_EQ(result.spawns.size(), 1u);
    // src/shadow/sector.c:667 -- arg.model = def->model.
    EXPECT_EQ(result.spawns[0].model_id, "MODEL_REAL");
    EXPECT_EQ(result.spawns[0].attachment_name, "ATT_RENAMED");
    EXPECT_EQ(result.spawns[0].task_type_name, "TASKTYPE_MAGICOBJ");
    EXPECT_EQ(result.spawns[0].parent_model, "MODEL_ROOT");
    EXPECT_EQ(result.spawns[0].depth, 1);
    EXPECT_FALSE(result.spawns[0].child_is_sector);
    EXPECT_EQ(result.stats.spawns, 1u);
    EXPECT_EQ(result.stats.attachments_seen, 1u);
    EXPECT_EQ(result.stats.models_walked, 1u);
}

TEST(MagicObjectExpandTest, UndefinedAttachmentNameIsSkippedAndReported) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("NOT_IN_THE_TABLE")};

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    EXPECT_TRUE(result.spawns.empty());            // sector.c:659-663 warns and skips
    EXPECT_EQ(result.stats.undefined_names, 1u);
    ASSERT_EQ(result.stats.undefined_name_list.size(), 1u);
    EXPECT_EQ(result.stats.undefined_name_list[0], "NOT_IN_THE_TABLE");
    EXPECT_EQ(result.stats.max_depth, 0);
}

TEST(MagicObjectExpandTest, MissingTopLevelModelSkipsTheSpawn) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A")};
    world.unavailable.insert("MODEL_A");           // sector.c:668-672

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    EXPECT_TRUE(result.spawns.empty());
    EXPECT_EQ(result.stats.missing_models, 1u);
    ASSERT_EQ(result.stats.missing_model_list.size(), 1u);
    EXPECT_EQ(result.stats.missing_model_list[0], "MODEL_A");
}

TEST(MagicObjectExpandTest, MissingNestedModelStillSpawnsButDoesNotExpand) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A")};
    world.attachments["MODEL_A"] = {Attachment("ATT_B")};
    world.attachments["MODEL_B"] = {Attachment("ATT_C")};
    world.unavailable.insert("MODEL_B");

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    // rigidcoll.c:485 creates the task without a VirModel_Find check; the
    // TASKTYPE_MAGICOBJ create handler then warns and creates no children
    // (haze/magic.c:99-100, :108-109).
    ASSERT_EQ(result.spawns.size(), 2u);
    EXPECT_EQ(result.spawns[0].model_id, "MODEL_A");
    EXPECT_TRUE(result.spawns[0].model_available);
    EXPECT_EQ(result.spawns[1].model_id, "MODEL_B");
    EXPECT_FALSE(result.spawns[1].model_available);
    EXPECT_EQ(result.stats.spawns, 2u);
    EXPECT_EQ(result.stats.missing_models, 1u);
    EXPECT_EQ(result.stats.max_depth, 2);
}

// ─── Synthetic: the recursion rule ──────────────────────────────────────────

TEST(MagicObjectExpandTest, PortalisedChildBecomesASectorAndExpandsOnce) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_SECTOR")};
    world.attachments["MODEL_SECTOR"] = {Attachment("ATT_PLAIN")};
    world.attachments["MODEL_PLAIN"] = {Attachment("ATT_PLAIN")};
    world.sectors.insert("MODEL_SECTOR");
    world.sectors.insert("MODEL_PLAIN");

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    // Depth 1: the portal test (sector.c:673-676) makes this child a Sector,
    // and Sector_Create expands it (sector.c:190).
    ASSERT_EQ(result.spawns.size(), 2u);
    EXPECT_EQ(result.spawns[0].model_id, "MODEL_SECTOR");
    EXPECT_TRUE(result.spawns[0].child_is_sector);
    EXPECT_EQ(result.spawns[0].depth, 1);

    // Depth 2: RigidDynCubeObj_CreateMagicObjs (rigidcoll.c:383-496) has no
    // portal branch, so MODEL_PLAIN is never a sector even though its mesh has
    // portals -- and TASKTYPE_HITZONE does not expand, so the walk stops.
    EXPECT_EQ(result.spawns[1].model_id, "MODEL_PLAIN");
    EXPECT_FALSE(result.spawns[1].child_is_sector);
    EXPECT_EQ(result.spawns[1].depth, 2);
    EXPECT_EQ(result.stats.max_depth, 2);
}

TEST(MagicObjectExpandTest, NonExpandingTypeDoesNotRecurse) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_WHEEL")};
    world.attachments["MODEL_WHEEL"] = {Attachment("ATT_A"), Attachment("ATT_B")};

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    ASSERT_EQ(result.spawns.size(), 1u);          // Wheel_Create never expands
    EXPECT_EQ(result.spawns[0].model_id, "MODEL_WHEEL");
    EXPECT_EQ(result.stats.max_depth, 1);
}

TEST(MagicObjectExpandTest, MagicObjectTypeChainsWhileItsModelsResolve) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A")};
    world.attachments["MODEL_A"] = {Attachment("ATT_B")};
    world.attachments["MODEL_B"] = {Attachment("ATT_C")};
    world.attachments["MODEL_C"] = {};

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    EXPECT_EQ(SpawnModels(result), (std::vector<std::string>{"MODEL_A", "MODEL_B", "MODEL_C"}));
    EXPECT_EQ(result.stats.max_depth, 3);
}

TEST(MagicObjectExpandTest, UnconditionallyExpandingTypesKeepWalkingWithoutAModel) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A")};
    world.attachments["MODEL_A"] = {Attachment("ATT_GLASS"), Attachment("ATT_ROTOR")};
    world.unavailable.insert("MODEL_GLASS");
    world.unavailable.insert("MODEL_ROTOR");

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    // The nested tasks are created either way (rigidcoll.c:485 has no
    // VirModel_Find check). Retail would hard-error and spin here
    // (glass.c:66-70, rotor.c:92-96); the editor records the spawn instead of
    // reproducing the hang, and the missing models end the walk.
    ASSERT_EQ(result.spawns.size(), 3u);
    EXPECT_EQ(result.spawns[0].model_id, "MODEL_A");
    EXPECT_TRUE(result.spawns[0].model_available);
    EXPECT_FALSE(result.spawns[1].model_available);
    EXPECT_FALSE(result.spawns[2].model_available);
    EXPECT_EQ(result.stats.missing_models, 2u);
    EXPECT_EQ(result.stats.max_depth, 2);
}

TEST(MagicObjectExpandTest, CyclicAttachmentGraphTerminates) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A")};
    world.attachments["MODEL_A"] = {Attachment("ATT_A")};   // self cycle

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    // Retail has no cycle guard and would recurse forever; the editor stops
    // after the second visit to MODEL_A.
    EXPECT_EQ(result.stats.spawns, 2u);
    EXPECT_EQ(result.stats.max_depth, 2);
}

TEST(MagicObjectExpandTest, DepthCapStopsTheWalk) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A")};
    world.attachments["MODEL_A"] = {Attachment("ATT_A")};
    world.attachments["MODEL_B"] = {Attachment("ATT_B")};
    world.attachments["MODEL_C"] = {Attachment("ATT_C")};

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source(), 2);

    EXPECT_EQ(result.stats.spawns, 2u);
    EXPECT_EQ(result.stats.max_depth, 2);
}

TEST(MagicObjectExpandTest, EmptySourceProducesNothing) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    const MagicObjectExpansionSource empty;

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", empty);

    EXPECT_TRUE(result.spawns.empty());
    EXPECT_EQ(result.stats.models_walked, 0u);
    EXPECT_EQ(result.stats.definitions, registry.GetDefinitions().size());
}

TEST(MagicObjectExpandTest, SpawnCarriesTheAttachmentLocalFrameVerbatim) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    MagicObjectAttachment attachment = Attachment("ATT_A", 1.5f, -2.5f, 3.5f);
    for (int i = 0; i < 9; ++i) {
        attachment.m[i] = static_cast<float>(i) + 0.25f;
    }
    world.attachments["MODEL_ROOT"] = {attachment};

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());

    ASSERT_EQ(result.spawns.size(), 1u);
    // Carried through so the caller places the spawn with the existing #37
    // helper igi::IgiAttaChildWorld instead of re-deriving the frame.
    EXPECT_FLOAT_EQ(result.spawns[0].pos[0], 1.5f);
    EXPECT_FLOAT_EQ(result.spawns[0].pos[1], -2.5f);
    EXPECT_FLOAT_EQ(result.spawns[0].pos[2], 3.5f);
    for (int i = 0; i < 9; ++i) {
        EXPECT_FLOAT_EQ(result.spawns[0].m[i], static_cast<float>(i) + 0.25f) << "m[" << i << "]";
    }
}

TEST(MagicObjectExpandTest, InventoryReportsEveryCounterAndSpawn) {
    const MagicObjectRegistry registry = SyntheticRegistry();
    FakeWorld world;
    world.attachments["MODEL_ROOT"] = {Attachment("ATT_A"), Attachment("ATT_SECTOR")};
    world.attachments["MODEL_SECTOR"] = {Attachment("NOT_IN_THE_TABLE")};
    world.sectors.insert("MODEL_SECTOR");

    const MagicObjectExpansionResult result =
        igi::ExpandMagicObjects(registry, "MODEL_ROOT", world.Source());
    const std::string inventory = result.Inventory();

    EXPECT_NE(inventory.find("definitions="), std::string::npos);
    EXPECT_NE(inventory.find("attachments_seen=3"), std::string::npos);
    EXPECT_NE(inventory.find("spawns=2"), std::string::npos);
    EXPECT_NE(inventory.find("undefined_names=1"), std::string::npos);
    EXPECT_NE(inventory.find("max_depth=1"), std::string::npos);
    EXPECT_NE(inventory.find("spawn[0] depth=1 attachment=ATT_A parent=MODEL_ROOT model=MODEL_A "
                             "type=TASKTYPE_MAGICOBJ sector=0 model_available=1"),
              std::string::npos);
    EXPECT_NE(inventory.find("spawn[1] depth=1 attachment=ATT_SECTOR parent=MODEL_ROOT "
                             "model=MODEL_SECTOR type=TASKTYPE_HITZONE sector=1 model_available=1"),
              std::string::npos);
    EXPECT_NE(inventory.find("spawns_by_type: TASKTYPE_HITZONE=1 TASKTYPE_MAGICOBJ=1"),
              std::string::npos);
    EXPECT_NE(inventory.find("undefined: NOT_IN_THE_TABLE"), std::string::npos);
}

// ─── Synthetic: the type table itself ───────────────────────────────────────

TEST(MagicObjectExpandTest, TypeTableMatchesTheOracleExtraction) {
    // Every create handler that calls RigidDynCubeObj_CreateMagicObjs.
    const char* always[] = {
        "TASKTYPE_AISTATIONARYGUN", "TASKTYPE_ALARMLIGHTREFLECTOR", "TASKTYPE_CABARREL",
        "TASKTYPE_CACLIP",          "TASKTYPE_EXPLODEMAGICOBJ",    "TASKTYPE_GLASS",
        "TASKTYPE_GUNCLIP",         "TASKTYPE_GUNMOVINGPART",      "TASKTYPE_GUNX2",
        "TASKTYPE_LADDER",          "TASKTYPE_RADARDISH",          "TASKTYPE_ROTOR",
    };
    for (const char* name : always) {
        EXPECT_EQ(igi::MagicObjectTypeChildExpansion(name), MagicObjectChildExpansion::kAlways)
            << name;
    }
    EXPECT_EQ(igi::MagicObjectTypeChildExpansion("TASKTYPE_MAGICOBJ"),
              MagicObjectChildExpansion::kIfModelResolved);

    const char* never[] = {
        "TASKTYPE_AFTERBURNER", "TASKTYPE_ALARMLIGHT",  "TASKTYPE_CARDOOR",   "TASKTYPE_COCKPITSHIELD",
        "TASKTYPE_DEATHZONE",   "TASKTYPE_DRAWER",      "TASKTYPE_GEAR",      "TASKTYPE_GUNFLAME",
        "TASKTYPE_HATCH",       "TASKTYPE_HELIDOOR",    "TASKTYPE_HELIWHEEL", "TASKTYPE_HITZONE",
        "TASKTYPE_ROCKETPOD",   "TASKTYPE_RUDDER",      "TASKTYPE_WHEEL",
        "TASKTYPE_SCAMERALENSVIEWCONE", "TASKTYPE_NOT_A_REAL_TYPE", "",
    };
    for (const char* name : never) {
        EXPECT_EQ(igi::MagicObjectTypeChildExpansion(name), MagicObjectChildExpansion::kNo) << name;
    }
}

// ─── Corpus helpers ─────────────────────────────────────────────────────────

std::string FindCorpusRoot() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("IGI_GAME_PATH")) {
        candidates.emplace_back(env);
    }
    if (const char* env = std::getenv("IGI_VANILLA_ROOT")) {
        candidates.emplace_back(env);
    }
    candidates.emplace_back("/Users/haseeb-mir/Downloads/IGI");
    for (const std::string& root : candidates) {
        if (root.empty()) {
            continue;
        }
        std::ifstream probe(root + "/MAGICOBJ/MAGICOBJ.QVM", std::ios::binary);
        if (probe.good()) {
            return root;
        }
    }
    return {};
}

std::string LevelOneModelsDir(const std::string& root) {
    return root + "/MISSIONS/location0/LEVEL1/MODELS/models";
}

// Location-wide loose models (the level .RES packs only the level's own 415
// .mef entries; a few definitions resolve from here instead).
std::string CommonModelsDir(const std::string& root) {
    return root + "/MISSIONS/location0/COMMON/MODELS/models";
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return {};
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

// Reads real MEF files through the editor's own parser and exposes them as an
// expansion source. Models are looked up in the level's models directory and in
// the location-wide COMMON set, which is where the editor resolves them from. A
// model "has sectors" when its mesh has at least one portal record: the
// engine's test is the SectorMesh `portals` pointer at +0x98
// (src/sector/sector.h:37, src/shadow/sector.c:673), which the MEF loader fills
// from the TROP chunk.
class CorpusModels {
public:
    CorpusModels(std::string directory, std::string common_directory)
        : directory_(std::move(directory)), common_directory_(std::move(common_directory)) {}

    const std::vector<MagicObjectAttachment>* attachments_for(const std::string& model) const {
        const Entry& entry = Load(model);
        return entry.loaded ? &entry.attachments : nullptr;
    }

    bool available(const std::string& model) const { return Load(model).loaded; }

    bool has_sectors(const std::string& model) const { return Load(model).has_sectors; }

    MagicObjectExpansionSource Source() const {
        MagicObjectExpansionSource source;
        source.attachments_for = [this](const std::string& model) { return attachments_for(model); };
        source.model_available = [this](const std::string& model) { return available(model); };
        source.model_has_sectors = [this](const std::string& model) { return has_sectors(model); };
        return source;
    }

    std::size_t loaded_models() const { return cache_.size(); }

private:
    struct Entry {
        std::vector<MagicObjectAttachment> attachments;
        bool has_sectors = false;
        bool loaded = false;
    };

    const Entry& Load(const std::string& model) const {
        const auto cached = cache_.find(model);
        if (cached != cache_.end()) {
            return cached->second;
        }
        Entry entry;
        std::vector<uint8_t> bytes = ReadFileBytes(directory_ + "/" + model + ".mef");
        if (bytes.empty() && !common_directory_.empty()) {
            bytes = ReadFileBytes(common_directory_ + "/" + model + ".mef");
        }
        if (!bytes.empty()) {
            try {
                const ParsedGeometry geometry = ParseMefFileFromMemory(bytes, model);
                entry.loaded = true;
                entry.has_sectors = !geometry.portals.empty();
                for (const MefAttachment& attachment : geometry.mefAttachments) {
                    MagicObjectAttachment converted;
                    converted.name = std::string(attachment.name,
                                                 strnlen(attachment.name, sizeof(attachment.name)));
                    converted.pos[0] = attachment.px;
                    converted.pos[1] = attachment.py;
                    converted.pos[2] = attachment.pz;
                    const float m[9] = {attachment.r00, attachment.r01, attachment.r02,
                                        attachment.r03, attachment.r04, attachment.r05,
                                        attachment.r06, attachment.r07, attachment.r08};
                    for (int i = 0; i < 9; ++i) {
                        converted.m[i] = m[i];
                    }
                    entry.attachments.push_back(std::move(converted));
                }
            } catch (const std::exception&) {
                entry.loaded = false;
            }
        }
        return cache_.emplace(model, std::move(entry)).first->second;
    }

    mutable std::map<std::string, Entry> cache_;
    std::string directory_;
    std::string common_directory_;
};

// The level's own script names the garage objects; read the model ids from it
// instead of hardcoding them. A `Task_New(id, "Building", "SmallGarage", pos...,
// "MODEL", ...)` line prints its nine position/orientation numbers unquoted, so
// the next quoted token after "SmallGarage" is the model.
std::set<std::string> GarageModelsFromLevelScript(const std::string& root) {
    std::set<std::string> models;
    std::ifstream script(root + "/MISSIONS/location0/LEVEL1/objects.qsc");
    std::string line;
    while (std::getline(script, line)) {
        const std::size_t tag = line.find("\"SmallGarage\"");
        if (tag == std::string::npos) {
            continue;
        }
        const std::size_t open = line.find('"', tag + std::strlen("\"SmallGarage\""));
        if (open == std::string::npos) {
            continue;
        }
        const std::size_t close = line.find('"', open + 1);
        if (close == std::string::npos) {
            continue;
        }
        models.insert(line.substr(open + 1, close - open - 1));
    }
    return models;
}

} // namespace

// ─── Corpus: the retail table ───────────────────────────────────────────────

TEST(MagicObjectExpandCorpusTest, RetailTableMatchesTheMeasuredCounts) {
    const std::string root = FindCorpusRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not present (set IGI_GAME_PATH)";
    }
    const std::string qvm_path = root + "/MAGICOBJ/MAGICOBJ.QVM";

    const QVMFile parsed = QVM_Parse(qvm_path);
    ASSERT_TRUE(parsed.valid) << parsed.error;
    // The issue's own cross-check number: 387 is the QVM STRING-TABLE entry
    // count, not the definition count. Both are asserted so the distinction
    // cannot drift.
    EXPECT_EQ(parsed.stringCount(), 387u);
    EXPECT_EQ(parsed.identifierCount(), 33u);

    MagicObjectRegistry registry;
    ASSERT_TRUE(registry.LoadQvmFile(qvm_path));
    EXPECT_EQ(registry.GetDefinitions().size(), 374u);

    std::set<std::string> types;
    for (const igi::MagicObjectDefinition& definition : registry.GetDefinitions()) {
        types.insert(definition.task_type_name);
    }
    std::printf("[magic-objects] definitions=%zu stringTable=%zu identifiers=%zu distinctTypes=%zu\n",
                registry.GetDefinitions().size(),
                static_cast<std::size_t>(parsed.stringCount()),
                static_cast<std::size_t>(parsed.identifierCount()), types.size());
    EXPECT_EQ(types.size(), 28u);
    for (const std::string& type : types) {
        EXPECT_EQ(type.rfind("TASKTYPE_", 0), 0u) << type;
    }

    // Which string-table entries are NOT a definition name: the definitions are
    // the only consumer of the table that this task relies on, so the remainder
    // is reported rather than assumed.
    std::set<std::string> definition_names;
    for (const igi::MagicObjectDefinition& definition : registry.GetDefinitions()) {
        definition_names.insert(definition.attachment_name);
    }
    std::set<std::string> other_strings;
    for (const std::string& entry : parsed.strings) {
        if (definition_names.count(entry) == 0) {
            other_strings.insert(entry);
        }
    }
    std::printf("[magic-objects] string-table entries that are not a definition name: %zu\n",
                other_strings.size());
    for (const std::string& entry : other_strings) {
        std::printf("[magic-objects]   %s\n", entry.c_str());
    }
    EXPECT_EQ(other_strings.size(), 13u);
    EXPECT_EQ(definition_names.size(), 374u);
}

TEST(MagicObjectExpandCorpusTest, EveryRetailDefinitionUsesItsOwnNameAsItsModel) {
    const std::string root = FindCorpusRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not present (set IGI_GAME_PATH)";
    }
    MagicObjectRegistry registry;
    ASSERT_TRUE(registry.LoadQvmFile(root + "/MAGICOBJ/MAGICOBJ.QVM"));

    std::size_t renamed = 0;
    for (const igi::MagicObjectDefinition& definition : registry.GetDefinitions()) {
        if (definition.model_id != definition.attachment_name) {
            ++renamed;
            std::printf("[magic-objects] renamed: %s -> %s\n", definition.attachment_name.c_str(),
                        definition.model_id.c_str());
        }
    }
    std::printf("[magic-objects] definitions with model != name: %zu of %zu\n", renamed,
                registry.GetDefinitions().size());
    // Measured on the retail table: the "spawn def->model" rule is
    // observationally identical to a name-based load on shipped data.
    EXPECT_EQ(renamed, 0u);
}

// ─── Corpus: the level-1 garage ─────────────────────────────────────────────

TEST(MagicObjectExpandCorpusTest, LevelOneGarageExpandsToTheMeasuredSpawnCount) {
    const std::string root = FindCorpusRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not present (set IGI_GAME_PATH)";
    }
    const std::string models_dir = LevelOneModelsDir(root);
    if (!std::ifstream(models_dir + "/410_01_1.mef", std::ios::binary).good()) {
        GTEST_SKIP() << "level-1 models not present at " << models_dir;
    }

    MagicObjectRegistry registry;
    ASSERT_TRUE(registry.LoadQvmFile(root + "/MAGICOBJ/MAGICOBJ.QVM"));

    const std::set<std::string> garage_models = GarageModelsFromLevelScript(root);
    std::printf("[magic-objects] garage models named by the level script:");
    for (const std::string& model : garage_models) {
        std::printf(" %s", model.c_str());
    }
    std::printf("\n");
    ASSERT_FALSE(garage_models.empty());

    CorpusModels models(models_dir, CommonModelsDir(root));
    const MagicObjectExpansionSource source = models.Source();

    // Measured on the retail level-1 data: the SmallGarage model 410_01_1 has
    // 9 ATTA records and expands to 26 tasks (9 + the 17 attachments of the one
    // portalised child, 410_02_1); 404_01_1 has 21 and expands to 34.
    const std::map<std::string, std::size_t> expected = {
        {"410_01_1", 26},
        {"404_01_1", 34},
    };

    for (const std::string& model : garage_models) {
        const MagicObjectExpansionResult result = igi::ExpandMagicObjects(registry, model, source);
        std::printf("[magic-objects] === garage model %s ===\n%s", model.c_str(),
                    result.Inventory().c_str());
        const auto want = expected.find(model);
        ASSERT_NE(want, expected.end()) << "unexpected garage model " << model;
        EXPECT_EQ(result.stats.spawns, want->second);
        EXPECT_EQ(result.stats.undefined_names, 0u);
        EXPECT_EQ(result.stats.missing_models, 0u);
        EXPECT_EQ(result.stats.max_depth, 2);
    }
}

TEST(MagicObjectExpandCorpusTest, LevelOneScanReportsTheMaximumAndTheIssuesSixtyFour) {
    const std::string root = FindCorpusRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not present (set IGI_GAME_PATH)";
    }
    const std::string models_dir = LevelOneModelsDir(root);
    if (!std::ifstream(models_dir + "/410_01_1.mef", std::ios::binary).good()) {
        GTEST_SKIP() << "level-1 models not present at " << models_dir;
    }

    MagicObjectRegistry registry;
    ASSERT_TRUE(registry.LoadQvmFile(root + "/MAGICOBJ/MAGICOBJ.QVM"));
    CorpusModels models(models_dir, CommonModelsDir(root));
    const MagicObjectExpansionSource source = models.Source();

    std::vector<std::pair<std::size_t, std::string>> rows;
    std::set<std::string> missing_models;
    std::size_t undefined_total = 0;
    std::size_t missing_total = 0;
    std::size_t attachments_total = 0;
    std::size_t spawns_total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(models_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mef") {
            continue;
        }
        const std::string model = entry.path().stem().string();
        const MagicObjectExpansionResult result = igi::ExpandMagicObjects(registry, model, source);
        undefined_total += result.stats.undefined_names;
        missing_total += result.stats.missing_models;
        attachments_total += result.stats.attachments_seen;
        spawns_total += result.stats.spawns;
        for (const std::string& missing : result.stats.missing_model_list) {
            missing_models.insert(missing);
        }
        if (result.stats.spawns != 0) {
            rows.emplace_back(result.stats.spawns, model);
        }
    }
    std::sort(rows.rbegin(), rows.rend());

    std::size_t sixty_four = 0;
    for (const auto& row : rows) {
        if (row.first == 64) {
            ++sixty_four;
        }
    }
    std::printf("[magic-objects] level1 scan: models_with_spawns=%zu attachments=%zu spawns=%zu "
                "undefined=%zu missing=%zu models_at_64=%zu\n",
                rows.size(), attachments_total, spawns_total, undefined_total, missing_total,
                sixty_four);
    for (std::size_t i = 0; i < rows.size() && i < 5; ++i) {
        std::printf("[magic-objects]   %s -> %zu spawns\n", rows[i].second.c_str(), rows[i].first);
    }
    for (const std::string& missing : missing_models) {
        std::printf("[magic-objects]   definition model not in the level models dir: %s\n",
                    missing.c_str());
    }

    // The issue's headline number does not reproduce on this corpus: the
    // largest level-1 expansion is 400_01_1 at 90 and no model lands on 64.
    // Asserted so the negative result is visible if the corpus changes.
    EXPECT_EQ(sixty_four, 0u);
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows[0].second, "400_01_1");
    EXPECT_EQ(rows[0].first, 90u);
    EXPECT_EQ(undefined_total, 0u);
    // Measured: exactly one attachment in the level references a definition
    // whose model is shipped nowhere the editor can see it -- `313_04_1`, named
    // by an ATTA record of `313_02_1`. It is not a loose .mef in the level's
    // models directory or in COMMON, and LEVEL1.RES carries 415
    // `LOCAL:models/*.mef` entries but no `LOCAL:models/313_04_1.mef`. Retail's
    // `VirModel_Find` therefore fails, warns ("Magic object model ... not
    // found", src/shadow/sector.c:668-672) and skips the attachment, which is
    // what this counter records.
    EXPECT_EQ(missing_total, 1u);
}
