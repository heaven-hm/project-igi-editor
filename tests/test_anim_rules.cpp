// test_anim_rules.cpp - the retail animation player's rules, pure-logic port (#73).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   boneobj/anim.c:9          kTicksPerSecond = 4800.0f, "Times are in ticks,
//                             4800 a second"
//   boneobj/anim.c:38-58      Anim_Play 0x4D61D0: the blend conversion and the
//                             `!(t < 1.0f)` fade branch
//   boneobj/anim.c:72-110     Anim_FadeOut 0x4D62B0
//   boneobj/anim.c:155-185    Anim_GetPhase 0x4D6520, BoneObj_UpdateAnimDirection
//                             0x4D6550, Anim_SetSpeed 0x4D6580, Anim_SetTime 0x4D65B0
//   boneobj/boneobj.c:788-868 BoneObj_AnimPositionCB 0x4D4840, both the Hermite
//                             and the linear form
//   boneobj/boneobj.c:873-941 BoneObj_AnimEventsCB 0x4D4B60, the frame advance
//   boneobj/boneobj.c:988-1021 BoneObj_InitAnimInst 0x4D4F60
//   boneobj/boneobj.c:1583-1630 BoneObj_Load 0x4D67A0, the table build
//   haze/anim.c:110-115       Anim_GetLengthTicks 0x4D7040 (`fistpll`)
//   haze/anim.c:238-242       Anim_GetLengthSeconds 0x4D74D0
//   haze/anim.c:244-255       Anim_Exists 0x4D74F0 - and the `id < 0` hole
//   lod/boneobj.c:71,86-90    gTicksPerFrame = 0 at init, 4800 / fps after
//   game/game.c:449           BoneObj_SetFrameRate(30.0f)
//   glass/tba.c:90-95,105-106,280-284  the GenericTBA params and defaults
//   humanstate/humanstate.c:520  FRAMES(seconds)
//   humanplayer/humanplayer.c:1554-1563  gBodyStates and gAnimSlots
//   dxinit/d3drender.c:226-313  BoneMesh_Skin 0x49B700
//
// The corpus half reads the retail ANIMS IFF files and the authored LEVEL2
// objects.qsc / OBJECTS.QVM. The IFF container walk is the TEST's own decoder:
// IGI-RE has `Iff_Parse` only as an ORIG stub, so the framing is not cited to
// IGI-RE. It is validated by internal consistency instead (every chunk tiles its
// parent exactly, BALH[0] equals the number of BOAN forms, the ids are unique),
// and the values it produces are the ones BoneObj_Load then consumes.

#include "../source/level/qvm_parser.h"
#include "../source/runtime/anim_rules.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// A small clip builder for the unit tests
// ---------------------------------------------------------------------------

igi::AnimRecord Rec(int id, float length, uint32_t flags = 0) {
    igi::AnimRecord record;
    record.id = id;
    record.length_ticks = length;
    record.flags = flags;
    return record;
}

// Skeleton 1 in the shipped data: capacity 212, 95 clips, ids 2..211 with a
// large empty tail. Reproduced small: capacity 8, clips 2, 3, 7.
igi::AnimSetTable SmallTable() {
    const igi::AnimRecord records[] = {
        Rec(2, 13280.0f),
        Rec(3, 3360.0f, igi::kAnimDescLoops),
        Rec(7, 1600.0f),
    };
    return igi::BuildAnimSetTable(1, 33, 3, 8, records, 3, nullptr);
}

// ---------------------------------------------------------------------------
// IFF container walk (see the file header: the test's own decoder)
// ---------------------------------------------------------------------------

uint32_t ReadU32BE(const std::string& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (static_cast<uint32_t>(static_cast<unsigned char>(data[offset])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 3]));
}

// Chunk payloads are LITTLE-endian on the x86 build even though the chunk
// SIZES are big-endian. Getting this backwards is the classic way to read
// `skel = 16777216` out of BOSH.
int32_t ReadI32LE(const std::string& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(data[offset + static_cast<std::size_t>(i)]);
    return static_cast<int32_t>(v);
}

float ReadF32LE(const std::string& data, std::size_t offset) {
    const int32_t bits = ReadI32LE(data, offset);
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

struct Chunk {
    std::string id;
    std::size_t payload = 0;
    std::size_t size = 0;
};

// Direct children of a FORM whose payload runs [start, end).
std::vector<Chunk> IffChildren(const std::string& data, std::size_t start, std::size_t end) {
    std::vector<Chunk> out;
    std::size_t offset = start;
    while (offset + 8 <= end && offset + 8 <= data.size()) {
        Chunk chunk;
        chunk.id = data.substr(offset, 4);
        chunk.size = ReadU32BE(data, offset + 4);
        chunk.payload = offset + 8;
        if (chunk.payload + chunk.size > data.size()) break;  // truncated: stop
        out.push_back(chunk);
        offset = chunk.payload + chunk.size;
        if (chunk.size & 1u) ++offset;  // IFF pads odd chunks
    }
    return out;
}

// The whole file is one FORM: [0..3]="FORM", [4..7]=size, [8..11]=form type,
// payload from 12.
//
// RETAIL DEFECT, reproduced rather than papered over: the outer `FORM ... BOBJ`
// size in every shipped ANIMS file covers only its FIRST child. 000.IFF declares
// 588, which implies the file ends at 596, but the real child chain runs to
// 1512568. The `FORM ... BOAL` size has the same defect (it excludes its last
// child). Every other FORM - BOBH and each BOAN - and every leaf chunk is
// exactly consistent. `BoneObj_Load` never reads a FORM size at all: it locates
// chunks with sequential id scans (boneobj.c:1543-1630, `Iff_FindChunk` /
// `Iff_FindNextChunk`), which is why the files still load. So this decoder walks
// the top level and BOAL to EOF and trusts the declared size only where the
// shipped data is self-consistent.
bool IffTopLevel(const std::string& data, std::vector<Chunk>& out, std::size_t& end,
                 std::size_t* declared_size) {
    if (data.size() < 12 || data.compare(0, 4, "FORM") != 0) return false;
    if (declared_size != nullptr) *declared_size = ReadU32BE(data, 4);
    end = data.size();
    out = IffChildren(data, 12, end);
    return true;
}

const Chunk* FindForm(const std::vector<Chunk>& chunks, const std::string& data, const char* type) {
    for (const Chunk& chunk : chunks) {
        if (chunk.id == "FORM" && chunk.size >= 4 && data.compare(chunk.payload, 4, type) == 0) {
            return &chunk;
        }
    }
    return nullptr;
}

const Chunk* FindChunk(const std::vector<Chunk>& chunks, const char* id) {
    for (const Chunk& chunk : chunks) {
        if (chunk.id == id) return &chunk;
    }
    return nullptr;
}

// One skeleton's worth of BOBJ data, decoded the way BoneObj_Load reads it
// (boneobj.c:1543-1630).
struct DecodedAnimFile {
    bool ok = false;
    int skel = -1;
    int bone_count = 0;
    int anim_count = 0;
    int capacity = 0;
    // The outer FORM's declared extent vs where the child chain really ends.
    std::size_t declared_end = 0;
    std::size_t chain_end = 0;
    // The same pair for the BOAL sub-form, whose size is short by its last child.
    std::size_t boal_declared_end = 0;
    std::size_t boal_chain_end = 0;
    std::vector<igi::AnimRecord> records;
    // The position track of the clip with the lowest id, for the playhead tests.
    std::vector<igi::PosKey> first_pos_keys;
};

DecodedAnimFile DecodeAnimFile(const std::string& data) {
    DecodedAnimFile out;
    std::vector<Chunk> top;
    std::size_t end = 0;
    std::size_t declared = 0;
    if (!IffTopLevel(data, top, end, &declared)) return out;
    out.chain_end = end;
    out.declared_end = declared + 8;

    const Chunk* bobh = FindForm(top, data, "BOBH");
    const Chunk* boal = FindForm(top, data, "BOAL");
    if (bobh == nullptr || boal == nullptr) return out;

    const std::vector<Chunk> bobh_children =
        IffChildren(data, bobh->payload + 4, bobh->payload + bobh->size);
    const Chunk* bosh = FindChunk(bobh_children, "BOSH");
    if (bosh == nullptr || bosh->size < 8) return out;
    out.skel = ReadI32LE(data, bosh->payload);
    out.bone_count = ReadI32LE(data, bosh->payload + 4);

    // BOAL's declared size excludes its last child (see IffTopLevel), so walk it
    // to the end of the file.
    out.boal_declared_end = boal->payload + boal->size;
    const std::vector<Chunk> boal_children = IffChildren(data, boal->payload + 4, data.size());
    const Chunk* balh = FindChunk(boal_children, "BALH");
    if (balh == nullptr || balh->size < 8) return out;
    // boneobj.c:1581-1583: `anims` is the first word, `total` the second.
    out.anim_count = ReadI32LE(data, balh->payload);
    out.capacity = ReadI32LE(data, balh->payload + 4);

    for (const Chunk& chunk : boal_children) {
        if (chunk.id != "FORM" || chunk.size < 4) continue;
        if (data.compare(chunk.payload, 4, "BOAN") != 0) continue;
        out.boal_chain_end = chunk.payload + chunk.size;
        const std::vector<Chunk> clip = IffChildren(data, chunk.payload + 4, chunk.payload + chunk.size);
        const Chunk* boah = FindChunk(clip, "BOAH");
        if (boah == nullptr || boah->size < 12) continue;
        // boneobj.c:1594-1597: length is a float at ah[0], flags ah[1], id ah[2].
        igi::AnimRecord record;
        record.length_ticks = ReadF32LE(data, boah->payload);
        record.flags = static_cast<uint32_t>(ReadI32LE(data, boah->payload + 4));
        record.id = ReadI32LE(data, boah->payload + 8);
        out.records.push_back(record);

        if (out.first_pos_keys.empty()) {
            const Chunk* both = FindChunk(clip, "BOTH");
            const Chunk* botd = FindChunk(clip, "BOTD");
            if (both != nullptr && botd != nullptr && both->size >= 4) {
                const int count = ReadI32LE(data, both->payload);
                // boneobj.c:1606: count * sizeof(PosKey) for count > 1, else
                // count * 16 - a single-key track has no tangents.
                const std::size_t stride = count > 1 ? sizeof(igi::PosKey) : 16u;
                for (int k = 0; k < count; ++k) {
                    const std::size_t at = botd->payload + static_cast<std::size_t>(k) * stride;
                    if (at + 16 > data.size()) break;
                    igi::PosKey key;
                    key.pos[0] = ReadF32LE(data, at);
                    key.pos[1] = ReadF32LE(data, at + 4);
                    key.pos[2] = ReadF32LE(data, at + 8);
                    key.time = ReadF32LE(data, at + 12);
                    if (stride > 16 && at + 40 <= data.size()) {
                        for (int c = 0; c < 3; ++c) {
                            key.out[c] = ReadF32LE(data, at + 16 + static_cast<std::size_t>(c) * 4);
                            key.in[c] = ReadF32LE(data, at + 28 + static_cast<std::size_t>(c) * 4);
                        }
                    }
                    out.first_pos_keys.push_back(key);
                }
            }
        }
    }
    out.ok = true;
    return out;
}

// The six shipped skeleton files. 004.IFF does not exist.
const char* const kAnimFiles[] = {"000.IFF", "001.IFF", "002.IFF", "003.IFF", "005.IFF", "006.IFF"};

DecodedAnimFile LoadAnimFile(const char* name) {
    return DecodeAnimFile(ReadFile(GameRoot() / "common" / "ANIMS" / name));
}

bool AnimsAvailable() {
    return fs::is_regular_file(GameRoot() / "common" / "ANIMS" / "000.IFF");
}

// ---------------------------------------------------------------------------
// Authored GenericTBA calls
// ---------------------------------------------------------------------------

bool IsStringPush(QVMOpType type) {
    return type == QVMOpType::PUSHS || type == QVMOpType::PUSHSI ||
           type == QVMOpType::PUSHSIB || type == QVMOpType::PUSHSIW;
}

bool IsIntPush(QVMOpType type) {
    return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII ||
           type == QVMOpType::PUSHIIB || type == QVMOpType::PUSHIIW ||
           type == QVMOpType::PUSHB || type == QVMOpType::PUSHW;
}

bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string FormatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// GenericTBA's authored argument block: 22 values after id/type/name
// (Position 3 + Orientation 3 + 16 single-token parameters).
constexpr std::size_t kGenericTbaValueCount = 22;

std::vector<std::string> ReadGenericTbaCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");           // the engine assigns the id at create time
    tokens.push_back("GenericTBA");   // type

    const std::size_t expected = 2 + 1 + kGenericTbaValueCount;
    for (std::size_t i = type_index + 1; i < qvm.instructions.size() && tokens.size() < expected; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;
        if (IsStringPush(in.type)) {
            tokens.push_back(in.operand < qvm.strings.size() ? qvm.strings[in.operand] : std::string());
            continue;
        }
        if (IsIntPush(in.type)) {
            if (IsIdentifierPush(in.type) && in.operand < qvm.identifiers.size()) {
                tokens.push_back(qvm.identifiers[in.operand]);
            } else {
                tokens.push_back(std::to_string(static_cast<int>(in.operand)));
            }
            continue;
        }
        if (in.type == QVMOpType::PUSH0) { tokens.push_back("0"); continue; }
        if (in.type == QVMOpType::PUSH1) { tokens.push_back("1"); continue; }
        if (in.type == QVMOpType::PUSHF) { tokens.push_back(FormatFloat(in.operand_float)); continue; }
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        break;
    }
    return tokens;
}

std::vector<std::vector<std::string>> ExtractGenericTbaCalls(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type) || in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "GenericTBA") continue;

        // Distinguish the instance from the declaration: after the type comes
        // the instance NAME string, and after that a declaration has another
        // parameter-name string while an instance has a value.
        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;
        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadGenericTbaCall(qvm, i));
    }
    return out;
}

// A QSC tokenizer good enough for the authored lines: strings (with escapes),
// numbers and bare words. The closing `)` of the Task_New call comes back as its
// own token (or glued to a trailing bare word), so it is stripped.
std::vector<std::string> TokenizeQscLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '"') {
            std::size_t j = i + 1;
            while (j < line.size()) {
                if (line[j] == '\\') { j += 2; continue; }
                if (line[j] == '"') break;
                ++j;
            }
            tokens.push_back(line.substr(i, j - i + 1));
            i = j + 1;
        } else if (c == ',' || c == ')') {
            ++i;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else {
            std::size_t j = i;
            while (j < line.size() && line[j] != ',' && line[j] != '\n' && line[j] != ')') ++j;
            std::string token = line.substr(i, j - i);
            while (!token.empty() && (token.back() == ' ' || token.back() == '\t' || token.back() == '\r'))
                token.pop_back();
            while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.erase(0, 1);
            tokens.push_back(token);
            i = j;
        }
    }
    return tokens;
}

// Every Task_New(<id>, "GenericTBA", ...) line of an objects.qsc, as argTokens.
std::vector<std::vector<std::string>> ParseGenericTbaCallsFromText(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    std::size_t offset = 0;
    while (true) {
        const std::size_t at = text.find("Task_New(", offset);
        if (at == std::string::npos) break;
        const std::size_t line_end = text.find('\n', at);
        const std::string line = text.substr(at, line_end == std::string::npos ? std::string::npos
                                                                              : line_end - at);
        offset = at + 9;
        std::vector<std::string> tokens = TokenizeQscLine(line);
        if (tokens.size() < 3) continue;
        if (tokens[1] != "\"GenericTBA\"") continue;
        // tokens[0] is "Task_New(282"; normalise it to the bare id.
        const std::size_t paren = tokens[0].find('(');
        if (paren != std::string::npos) tokens[0] = tokens[0].substr(paren + 1);
        out.push_back(tokens);
    }
    return out;
}

// A HumanSoldier-family Task_New line, for the "Bone Heirachy" / "Stand
// Animation" pair.
std::vector<std::string> FirstSoldierCallFromText(const std::string& text) {
    std::size_t offset = 0;
    while (true) {
        const std::size_t at = text.find("Task_New(", offset);
        if (at == std::string::npos) return {};
        const std::size_t line_end = text.find('\n', at);
        const std::string line = text.substr(at, line_end == std::string::npos ? std::string::npos
                                                                              : line_end - at);
        offset = at + 9;
        std::vector<std::string> tokens = TokenizeQscLine(line);
        if (tokens.size() < 3) continue;
        if (tokens[1] != "\"HumanSoldier\"") continue;
        const std::size_t paren = tokens[0].find('(');
        if (paren != std::string::npos) tokens[0] = tokens[0].substr(paren + 1);
        return tokens;
    }
}

// ---------------------------------------------------------------------------
// The authored GenericTBA declaration as (name, type) pairs, from the
// Task_DeclareParameters line.
std::vector<std::pair<std::string, std::string>> ParseDeclaredGenericTbaFromText(
    const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::size_t at = text.find("Task_DeclareParameters(\"GenericTBA\"");
    if (at == std::string::npos) return out;
    // The declaration wraps over two source lines, so it is delimited by its
    // closing `");` rather than by a newline.
    const std::size_t close = text.find("\");", at);
    if (close == std::string::npos) return out;
    const std::string line = text.substr(at, close - at + 3);
    std::vector<std::string> tokens = TokenizeQscLine(line);
    // tokens[0] is `Task_DeclareParameters("GenericTBA"` glued together; drop it,
    // then read name/type pairs.
    for (std::size_t i = 1; i + 1 < tokens.size(); i += 2) {
        out.emplace_back(tokens[i], tokens[i + 1]);
    }
    return out;
}

// How many argument tokens a declared script type consumes. Only the three
// multi-token aliases matter; paramops.c:178-188 is the alias table and
// paramops.c:423-435 is the Real32x9 reader that takes three, not nine.
int TokensForDeclaredType(const std::string& type) {
    if (type == "\"ObjectPos\"" || type == "\"Real64x3\"") return 3;
    if (type == "\"Real32x9\"") return 3;
    if (type == "\"RGB\"" || type == "\"Real32x3\"") return 3;
    return 1;
}

// A one-clip table with a known position track
// ---------------------------------------------------------------------------

igi::AnimSetTable PlaybackTable() {
    const igi::AnimRecord records[] = {Rec(2, 1600.0f, 0), Rec(4, 1600.0f, 0)};
    return igi::BuildAnimSetTable(0, 33, 2, 8, records, 2, nullptr);
}

}  // namespace

// ===========================================================================
// The animation table
// ===========================================================================

TEST(AnimTableTest, CapacityIsNotTheClipCount) {
    // boneobj.c:1583-1590. `set->count = total` is the CAPACITY read from the
    // BOAL/BALH second word; only `anims` (the first word) slots are filled.
    const igi::AnimSetTable table = SmallTable();
    ASSERT_TRUE(table.valid());
    EXPECT_EQ(table.skel, 1);
    EXPECT_EQ(table.bone_count, 33);
    EXPECT_EQ(table.capacity, 8);
    EXPECT_EQ(table.clip_count, 3);
    EXPECT_EQ(table.by_id.size(), 8u);
    EXPECT_EQ(table.descs.size(), 8u);
}

TEST(AnimTableTest, UnfilledSlotsStayEmpty) {
    const igi::AnimSetTable table = SmallTable();
    // 4 and 5 are inside the capacity and were never loaded - the memset at
    // boneobj.c:1587 leaves them NULL, and Anim_Exists reports a miss.
    EXPECT_EQ(igi::ResolveAnim(table, 0, 4).lookup, igi::kAnimSlotEmpty);
    EXPECT_EQ(igi::ResolveAnim(table, 0, 5).lookup, igi::kAnimSlotEmpty);
    EXPECT_EQ(igi::ResolveAnim(table, 0, 0).lookup, igi::kAnimSlotEmpty);
}

TEST(AnimTableTest, RecordOutsideTheCapacityIsDroppedAndReported) {
    // boneobj.c:1590-1597 writes `set->byId[id]` with no bounds check; a record
    // past the capacity is a heap overflow in retail.
    const igi::AnimRecord records[] = {Rec(2, 100.0f), Rec(9, 200.0f), Rec(-1, 300.0f)};
    bool out_of_range = false;
    const igi::AnimSetTable table = igi::BuildAnimSetTable(0, 33, 3, 8, records, 3, &out_of_range);
    EXPECT_TRUE(out_of_range);
    EXPECT_EQ(table.clip_count, 1);
    EXPECT_EQ(igi::ResolveAnim(table, 0, 2).lookup, igi::kAnimFound);
    EXPECT_EQ(igi::ResolveAnim(table, 0, 9).lookup, igi::kAnimIdAtOrAboveCapacity);
}

TEST(AnimTableTest, ZeroCapacityIsAnEmptyTable) {
    const igi::AnimRecord records[] = {Rec(2, 100.0f)};
    const igi::AnimSetTable table = igi::BuildAnimSetTable(0, 33, 1, 0, records, 1, nullptr);
    EXPECT_FALSE(table.valid());
    EXPECT_EQ(igi::ResolveAnim(table, 0, 2).lookup, igi::kAnimTableEmpty);
}

// ===========================================================================
// Lookup, and the Anim_Exists hole
// ===========================================================================

TEST(AnimLookupTest, ResolvesAKnownId) {
    const igi::AnimSetTable table = SmallTable();
    const igi::AnimResolution r = igi::ResolveAnim(table, 0, 3);
    ASSERT_TRUE(r.found());
    EXPECT_EQ(r.desc_index, 3);
    EXPECT_FLOAT_EQ(r.length_ticks, 3360.0f);
}

TEST(AnimLookupTest, IdAtOrAboveCapacityMisses) {
    // haze/anim.c:249: `if (!set->count || id >= set->count) return 0;`
    const igi::AnimSetTable table = SmallTable();
    EXPECT_EQ(igi::ResolveAnim(table, 0, 8).lookup, igi::kAnimIdAtOrAboveCapacity);
    EXPECT_EQ(igi::ResolveAnim(table, 0, 212).lookup, igi::kAnimIdAtOrAboveCapacity);
}

TEST(AnimLookupTest, NegativeIdIsTheUnGuardedHole) {
    // haze/anim.c:244-255 tests `id >= set->count` and NOT `id < 0`, so a
    // negative id passes the guard and reaches `byId[-1]`. Retail reads four
    // bytes in front of the allocation; the port names it instead.
    const igi::AnimSetTable table = SmallTable();
    EXPECT_EQ(igi::ResolveAnim(table, 0, -1).lookup, igi::kAnimIdNegative);
    EXPECT_EQ(igi::ResolveAnim(table, 0, -88).lookup, igi::kAnimIdNegative);
    // -1 is also the engine's own "no animation authored" sentinel
    // (tba.c:106, soldier.c:746), which is why the hole is reachable at all.
}

TEST(AnimLookupTest, BoneIndexIsCheckedFirst) {
    // haze/anim.c:246-247 checks the bone against the skeleton's bone count
    // before it ever looks at the set. A skeleton with no hierarchy loaded has
    // count 0, so every bone fails (lod/boneobj.c:111-116).
    const igi::AnimSetTable table = SmallTable();
    EXPECT_EQ(igi::ResolveAnim(table, 33, 2).lookup, igi::kAnimBoneOutOfRange);
    EXPECT_EQ(igi::ResolveAnim(table, -1, 2).lookup, igi::kAnimBoneOutOfRange);
    EXPECT_EQ(igi::ResolveAnim(table, 32, 2).lookup, igi::kAnimFound);
}

TEST(AnimLookupTest, MissingIdsReportMinusOneFromTheLengthHelpers) {
    const igi::AnimSetTable table = SmallTable();
    EXPECT_EQ(igi::AnimLengthTicks(table, 4), -1);
    EXPECT_DOUBLE_EQ(igi::AnimLengthSeconds(table, 4), -1.0);
}

TEST(AnimLengthTest, TicksRoundRatherThanTruncate) {
    // haze/anim.c:110-115 is `fistpll`, i.e. round-to-nearest-even.
    const igi::AnimRecord records[] = {
        Rec(2, 3359.6f), Rec(3, 3360.4f), Rec(4, 3360.5f), Rec(5, 3361.5f),
    };
    const igi::AnimSetTable table = igi::BuildAnimSetTable(0, 33, 4, 8, records, 4, nullptr);
    EXPECT_EQ(igi::AnimLengthTicks(table, 2), 3360);
    EXPECT_EQ(igi::AnimLengthTicks(table, 3), 3360);
    // 3360.5 is exactly representable, and ties go to EVEN: 3360, not 3361.
    EXPECT_EQ(igi::AnimLengthTicks(table, 4), 3360);
    // 3361.5 ties to 3362.
    EXPECT_EQ(igi::AnimLengthTicks(table, 5), 3362);
}

TEST(AnimLengthTest, SecondsUseTheNearestFloatToOneOver4800) {
    // haze/anim.c:12 `kSecondsPerTick = 0x1.b4e81cp-13f`, which is
    // 0.00020833333837799728f - NOT exactly 1/4800.
    EXPECT_FLOAT_EQ(igi::kAnimSecondsPerTick, 0.00020833333837799728f);
    EXPECT_NE(static_cast<double>(igi::kAnimSecondsPerTick), 1.0 / 4800.0);
    const igi::AnimSetTable table = SmallTable();
    // 14400 ticks is exactly 3 seconds at 4800 a second.
    EXPECT_NEAR(igi::AnimLengthSeconds(table, 2), 13280.0 * igi::kAnimSecondsPerTick, 1e-12);
}

TEST(AnimPhaseTest, PhaseNarrowsTheTimeToFloatFirst) {
    // anim.c:155-161: `float t = Anim_GetTime(...)` narrows the long double,
    // then `t / Anim_GetLength(...)` widens it back.
    EXPECT_FLOAT_EQ(igi::AnimPhase(0.0f, 1600.0f), 0.0f);
    EXPECT_FLOAT_EQ(igi::AnimPhase(400.0f, 1600.0f), 0.25f);
    EXPECT_FLOAT_EQ(igi::AnimPhase(1600.0f, 1600.0f), 1.0f);
    // Past the end is not clamped: retail uses the raw ratio.
    EXPECT_FLOAT_EQ(igi::AnimPhase(3200.0f, 1600.0f), 2.0f);
}

// ===========================================================================
// Frame / tick timing
// ===========================================================================

TEST(AnimTimingTest, DefaultTicksPerFrameIsExactly160) {
    // game.c:449 installs 30 fps and lod/boneobj.c:88 computes 4800 / 30.
    EXPECT_FLOAT_EQ(igi::kAnimTicksPerFrameUninitialised, 0.0f);
    EXPECT_FLOAT_EQ(igi::kAnimDefaultTicksPerFrame, 160.0f);
}

TEST(AnimTimingTest, InitForwardStartsAtZeroAndRunsForward) {
    // boneobj.c:1017-1020.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    const float first[3] = {1.0f, 2.0f, 3.0f};
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 0, 2, igi::kAnimLoop, 160.0f, first, nullptr));
    EXPECT_FLOAT_EQ(inst.speed, 160.0f);
    EXPECT_FLOAT_EQ(inst.time, 0.0f);
    EXPECT_FLOAT_EQ(inst.pos[0], 1.0f);
    EXPECT_EQ(inst.desc_index, 2);
    EXPECT_EQ(inst.flags, igi::kAnimLoop);
}

TEST(AnimTimingTest, InitReverseStartsAtTheClipLengthAndRunsBackwards) {
    // boneobj.c:1013-1016: pos = the LAST key, speed = -gTicksPerFrame,
    // time = the clip length.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    const float last[3] = {7.0f, 8.0f, 9.0f};
    ASSERT_TRUE(
        igi::InitAnimInst(inst, table, 0, 2, igi::kAnimLoop | igi::kAnimReverse, 160.0f, nullptr, last));
    EXPECT_FLOAT_EQ(inst.speed, -160.0f);
    EXPECT_FLOAT_EQ(inst.time, 1600.0f);
    EXPECT_FLOAT_EQ(inst.pos[2], 9.0f);
}

TEST(AnimTimingTest, InitRefusesAMissingId) {
    // The engine dereferences the NULL desc instead (boneobj.c:1014,1018).
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    EXPECT_FALSE(igi::InitAnimInst(inst, table, 0, 5, 0, 160.0f, nullptr, nullptr));
    EXPECT_FALSE(igi::InitAnimInst(inst, table, 0, -1, 0, 160.0f, nullptr, nullptr));
}

TEST(AnimAdvanceTest, ForwardOneShotClampsAtTheLength) {
    // boneobj.c:893-894 + :936: the end test is `!(time < length)` and the
    // one-shot branch forces `time = length`.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, 0, 160.0f, nullptr, nullptr));

    // 9 ticks of 160 land on 1440; the 10th reaches exactly 1600.
    for (int i = 0; i < 9; ++i) {
        const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
        EXPECT_FALSE(a.past_end) << "tick " << i;
        EXPECT_FALSE(a.events_done) << "tick " << i;
    }
    EXPECT_FLOAT_EQ(inst.time, 1440.0f);

    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    // `!(1600 < 1600)` is TRUE: a tick landing exactly on the length is past
    // the end.
    EXPECT_TRUE(a.past_end);
    EXPECT_TRUE(a.events_done);
    EXPECT_TRUE(a.clamped);
    EXPECT_FALSE(a.wrapped);
    EXPECT_FLOAT_EQ(inst.time, 1600.0f);

    // It does NOT stop - the next tick adds another 160 and clamps again.
    const igi::AnimAdvance b = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_FLOAT_EQ(b.old_time, 1600.0f);
    EXPECT_FLOAT_EQ(inst.time, 1600.0f);
    EXPECT_TRUE(b.clamped);
}

TEST(AnimAdvanceTest, ForwardLoopWrapsWithASingleSubtraction) {
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, igi::kAnimLoop, 160.0f, nullptr, nullptr));

    for (int i = 0; i < 9; ++i) (void)igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_TRUE(a.wrapped);
    EXPECT_FLOAT_EQ(inst.time, 0.0f);  // 1600 - 1600
    EXPECT_TRUE(a.events_done);
    EXPECT_FALSE(a.clamped);
}

TEST(AnimAdvanceTest, LoopWrapIsNotAWhileLoop) {
    // boneobj.c:904 `inst->time = inst->time - inst->desc->length;` runs ONCE.
    // A speed larger than the clip leaves the playhead still past the end.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, igi::kAnimLoop, 5000.0f, nullptr, nullptr));
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_TRUE(a.wrapped);
    EXPECT_FLOAT_EQ(inst.time, 3400.0f);  // 5000 - 1600, still >= length
    EXPECT_GE(inst.time, 1600.0f);
}

TEST(AnimAdvanceTest, ReverseOneShotClampsToZero) {
    // boneobj.c:894 `!(inst->time > 0.0f)` and :931-934.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, igi::kAnimReverse, 160.0f, nullptr, nullptr));
    EXPECT_FLOAT_EQ(inst.time, 1600.0f);

    for (int i = 0; i < 9; ++i) (void)igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_FLOAT_EQ(inst.time, 160.0f);
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_TRUE(a.past_end);
    EXPECT_TRUE(a.clamped);
    EXPECT_FALSE(a.wrapped);
    EXPECT_FLOAT_EQ(inst.time, 0.0f);
}

TEST(AnimAdvanceTest, ReverseLoopWrapsBackToTheFarEnd) {
    // boneobj.c:901 `inst->time = inst->desc->length + inst->time;`
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, igi::kAnimLoop | igi::kAnimReverse, 160.0f, nullptr,
                                  nullptr));
    for (int i = 0; i < 9; ++i) (void)igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_TRUE(a.wrapped);
    EXPECT_FLOAT_EQ(inst.time, 1600.0f);  // 1600 + 0
}

TEST(AnimAdvanceTest, EventsDoneIsClearedAtTheTopOfEveryTick) {
    // boneobj.c:881 `inst->flags &= ~BONE_EVENTS_DONE;`
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, 0, 160.0f, nullptr, nullptr));
    for (int i = 0; i < 10; ++i) (void)igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_NE(inst.flags & igi::kBoneEventsDone, 0u);
    (void)igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_NE(inst.flags & igi::kBoneEventsDone, 0u);  // re-set by the clamp
    EXPECT_TRUE(igi::HumanAnimDone(inst.flags));
    EXPECT_FALSE(igi::HumanAnimDone(0));
}

TEST(AnimAdvanceTest, AnimFirstIsAlwaysCleared) {
    // boneobj.c:939 `inst->flags &= ~ANIM_FIRST;` on every path out.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, igi::kAnimLoop | igi::kAnimFirst, 160.0f, nullptr,
                                  nullptr));
    (void)igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_EQ(inst.flags & igi::kAnimFirst, 0u);
}

TEST(AnimAdvanceTest, RootFixupSnapsTheRootBackToTheFirstKeyOnAWrap) {
    // boneobj.c:909-929. The fixup exists for a LOOPING root: at the wrap the
    // playhead jumps from the far end of the clip back to the start, so the
    // position track is re-sampled at the far end and the root is placed
    // relative to the first key. In the engine's own sequence the instance's pos
    // already holds the far-end sample by the time the wrap happens (the
    // per-frame position callback wrote it), so the probe's delta is zero and
    // the root lands exactly on keys[0].pos. That is what this reproduces.
    const igi::AnimSetTable table = PlaybackTable();
    igi::PosKey keys[2];
    keys[0].pos[0] = 11.0f;
    keys[0].pos[1] = 22.0f;
    keys[0].pos[2] = 33.0f;
    keys[0].time = 0.0f;
    keys[1].pos[0] = 44.0f;
    keys[1].pos[1] = 55.0f;
    keys[1].pos[2] = 66.0f;
    keys[1].time = 1600.0f;
    const igi::PosTrack track{keys, 2};

    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 0, 2, igi::kAnimLoop, 1600.0f, nullptr, nullptr));
    // What the caller's position callback would have left behind at the end of
    // the clip.
    inst.pos[0] = 44.0f;
    inst.pos[1] = 55.0f;
    inst.pos[2] = 66.0f;

    // A full-length step: 1600 -> past the end -> wrap to 0.
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, &track);
    EXPECT_TRUE(a.wrapped);
    EXPECT_TRUE(a.root_fixup);
    EXPECT_FLOAT_EQ(inst.pos[0], 11.0f);
    EXPECT_FLOAT_EQ(inst.pos[1], 22.0f);
    EXPECT_FLOAT_EQ(inst.pos[2], 33.0f);
    EXPECT_FLOAT_EQ(inst.delta[0], 0.0f);
    EXPECT_FLOAT_EQ(a.time, 0.0f);  // inst->time = saved (the wrapped time)

    // Bone 1 never gets the fixup, so its pos is untouched.
    igi::AnimInst other;
    ASSERT_TRUE(igi::InitAnimInst(other, table, 1, 2, igi::kAnimLoop, 1600.0f, nullptr, nullptr));
    other.pos[0] = 44.0f;
    const igi::AnimAdvance b = igi::AdvanceAnimInst(other, 1600.0f, &track);
    EXPECT_TRUE(b.wrapped);
    EXPECT_FALSE(b.root_fixup);
    EXPECT_FLOAT_EQ(other.pos[0], 44.0f);
}

TEST(AnimAdvanceTest, ReverseRootFixupSnapsToTheLastKey) {
    // boneobj.c:914-920 takes the reverse branch: the probe runs at time 0 and
    // the root is placed relative to the LAST key.
    const igi::AnimSetTable table = PlaybackTable();
    igi::PosKey keys[2];
    keys[0].pos[0] = 11.0f;
    keys[0].time = 0.0f;
    keys[1].pos[0] = 44.0f;
    keys[1].time = 1600.0f;
    const igi::PosTrack track{keys, 2};

    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 0, 2, igi::kAnimLoop | igi::kAnimReverse, 1600.0f,
                                  nullptr, nullptr));
    inst.pos[0] = 11.0f;  // the start sample the caller would have left behind
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, &track);
    EXPECT_TRUE(a.wrapped);
    EXPECT_TRUE(a.root_fixup);
    EXPECT_FLOAT_EQ(inst.pos[0], 44.0f);
}

TEST(AnimAdvanceTest, RootFixupIsSkippedWhileAnimFirstIsSet) {
    // boneobj.c:909 `!(inst->flags & ANIM_FIRST)`.
    const igi::AnimSetTable table = PlaybackTable();
    igi::PosKey key;
    key.pos[0] = 11.0f;
    const igi::PosTrack track{&key, 1};
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 0, 2, igi::kAnimLoop | igi::kAnimFirst, 1600.0f, nullptr,
                                  nullptr));
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, &track);
    EXPECT_TRUE(a.wrapped);
    EXPECT_FALSE(a.root_fixup);
}

TEST(AnimAdvanceTest, NaNSpeedCountsAsPastTheEnd) {
    // `!(NaN < length)` is true, so a NaN playhead takes the end branch.
    const igi::AnimSetTable table = PlaybackTable();
    igi::AnimInst inst;
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 1, 2, 0, 160.0f, nullptr, nullptr));
    inst.speed = std::numeric_limits<float>::quiet_NaN();
    const igi::AnimAdvance a = igi::AdvanceAnimInst(inst, 1600.0f, nullptr);
    EXPECT_TRUE(a.past_end);
    EXPECT_TRUE(a.clamped);
}

// ===========================================================================
// The position track
// ===========================================================================

TEST(AnimPosTrackTest, SingleKeyReturnsTheKeyWithNoDelta) {
    // boneobj.c:794-798.
    igi::PosKey key;
    key.pos[0] = 5.0f;
    key.pos[1] = 6.0f;
    key.pos[2] = 7.0f;
    const igi::PosTrack track{&key, 1};
    const float previous[3] = {100.0f, 100.0f, 100.0f};
    float pos[3] = {};
    float delta[3] = {};
    ASSERT_TRUE(igi::SamplePosTrack(track, 800.0f, true, previous, pos, delta));
    EXPECT_FLOAT_EQ(pos[0], 5.0f);
    EXPECT_FLOAT_EQ(pos[1], 6.0f);
    EXPECT_FLOAT_EQ(pos[2], 7.0f);
    // `inst->delta[i] = 0` explicitly in the n == 1 branch, so a caller must
    // pass a previous position equal to the key to see a zero delta. The
    // engine overwrites the delta with 0 there, which the caller-visible
    // behaviour cannot distinguish - documented in ANIMATION.md.
    EXPECT_FLOAT_EQ(delta[0], 5.0f - 100.0f);
}

TEST(AnimPosTrackTest, RefusesAnEmptyTrack) {
    // The engine reads keys[-1] here (boneobj.c:800-815 with tr->keys never
    // assigned, boneobj.c:1604-1609).
    const igi::PosTrack empty{nullptr, 0};
    float pos[3] = {1.0f, 1.0f, 1.0f};
    float delta[3] = {};
    EXPECT_FALSE(igi::SamplePosTrack(empty, 0.0f, true, pos, pos, delta));
    const igi::PosKey key;
    const igi::PosTrack counted{&key, 0};
    EXPECT_FALSE(igi::SamplePosTrack(counted, 0.0f, true, pos, pos, delta));
}

TEST(AnimPosTrackTest, LandsExactlyOnAKey) {
    // boneobj.c:812-813 `if (tm == kt)`.
    igi::PosKey keys[3];
    keys[0].pos[0] = 0.0f;
    keys[0].time = 0.0f;
    keys[1].pos[0] = 10.0f;
    keys[1].time = 100.0f;
    keys[2].pos[0] = 20.0f;
    keys[2].time = 200.0f;
    const igi::PosTrack track{keys, 3};
    const float previous[3] = {0.0f, 0.0f, 0.0f};
    float pos[3] = {};
    float delta[3] = {};
    ASSERT_TRUE(igi::SamplePosTrack(track, 100.0f, false, previous, pos, delta));
    EXPECT_FLOAT_EQ(pos[0], 10.0f);
}

TEST(AnimPosTrackTest, PastTheLastKeyHoldsIt) {
    // boneobj.c:814-815.
    igi::PosKey keys[2];
    keys[0].time = 0.0f;
    keys[1].pos[0] = 20.0f;
    keys[1].time = 100.0f;
    const igi::PosTrack track{keys, 2};
    const float previous[3] = {0.0f, 0.0f, 0.0f};
    float pos[3] = {};
    float delta[3] = {};
    ASSERT_TRUE(igi::SamplePosTrack(track, 500.0f, false, previous, pos, delta));
    EXPECT_FLOAT_EQ(pos[0], 20.0f);
}

TEST(AnimPosTrackTest, LinearInterpolationIsAHalfwaySplit) {
    // boneobj.c:842-855 with f = (kt - tm) / (kt - pk->time) = 0.5 at the
    // midpoint.
    igi::PosKey keys[2];
    keys[0].pos[0] = 0.0f;
    keys[0].pos[1] = 0.0f;
    keys[0].pos[2] = 0.0f;
    keys[0].time = 0.0f;
    keys[1].pos[0] = 100.0f;
    keys[1].pos[1] = 100.0f;
    keys[1].pos[2] = 100.0f;
    keys[1].time = 200.0f;
    const igi::PosTrack track{keys, 2};
    const float previous[3] = {0.0f, 0.0f, 0.0f};
    float pos[3] = {};
    float delta[3] = {};
    ASSERT_TRUE(igi::SamplePosTrack(track, 100.0f, false, previous, pos, delta));
    EXPECT_NEAR(pos[0], 50.0f, 1e-4f);
    EXPECT_NEAR(pos[1], 50.0f, 1e-4f);
    EXPECT_NEAR(pos[2], 50.0f, 1e-4f);
}

TEST(AnimPosTrackTest, SmoothAndLinearDisagreeAwayFromTheEndpoints) {
    // The Hermite form (boneobj.c:819-841, the default via gBoneSmoothAnims)
    // adds the OUT tangent of the previous key and the IN tangent of the next,
    // so with a non-zero tangent it is not a straight line.
    //
    // Sampled at tm = 50 of a 0..200 key pair: s = 0.25, h = -0.15625,
    // h1 = 0.84375, h2 = 0.15625, h3 = 0.140625, h4 = 0.046875, so with
    // out = 0.25 and in = -0.25 the Hermite gives 20.3125 against the lerp's 25.
    // (At the exact midpoint with antisymmetric tangents the two coincide, which
    // is why this test does not sample there.)
    igi::PosKey keys[2];
    keys[0].pos[0] = 0.0f;
    keys[0].time = 0.0f;
    keys[0].out[0] = 0.25f;
    keys[1].pos[0] = 100.0f;
    keys[1].time = 200.0f;
    keys[1].in[0] = -0.25f;
    const igi::PosTrack track{keys, 2};
    const float previous[3] = {0.0f, 0.0f, 0.0f};
    float smooth[3] = {};
    float linear[3] = {};
    float delta[3] = {};
    ASSERT_TRUE(igi::SamplePosTrack(track, 50.0f, true, previous, smooth, delta));
    ASSERT_TRUE(igi::SamplePosTrack(track, 50.0f, false, previous, linear, delta));
    EXPECT_FLOAT_EQ(linear[0], 25.0f);
    EXPECT_FLOAT_EQ(smooth[0], 20.3125f);
    EXPECT_GT(std::fabs(smooth[0] - linear[0]), 1.0f);
}

// ===========================================================================
// Blend, fade, speed and time
// ===========================================================================

TEST(AnimBlendTest, BlendSecondsBecomeFrames) {
    // anim.c:40 `float t = blend * kTicksPerSecond / gTicksPerFrame;` - so
    // `blend` is SECONDS and `t` is FRAMES. At the retail 30 fps that is
    // exactly blend * 30.
    EXPECT_FLOAT_EQ(igi::AnimBlendFrames(0.0f, 160.0f), 0.0f);
    EXPECT_FLOAT_EQ(igi::AnimBlendFrames(1.0f, 160.0f), 30.0f);
    EXPECT_FLOAT_EQ(igi::AnimBlendFrames(0.1f, 160.0f), 3.0f);
}

TEST(AnimBlendTest, ZeroTicksPerFrameDividesByZero) {
    // gTicksPerFrame is 0 until game.c:449 runs (lod/boneobj.c:71).
    EXPECT_TRUE(std::isinf(igi::AnimBlendFrames(1.0f, 0.0f)));
    EXPECT_TRUE(std::isnan(igi::AnimBlendFrames(0.0f, 0.0f)));
}

TEST(AnimBlendTest, TheFadeBranchIsNotLessThanOne) {
    // anim.c:45 `if (!(t < 1.0f))`.
    EXPECT_EQ(igi::AnimPlayModeFor(0.0f, true, false), igi::kAnimPlayImmediate);
    EXPECT_EQ(igi::AnimPlayModeFor(0.5f, true, false), igi::kAnimPlayImmediate);
    EXPECT_EQ(igi::AnimPlayModeFor(1.0f, true, false), igi::kAnimPlayFade);
    EXPECT_EQ(igi::AnimPlayModeFor(30.0f, true, false), igi::kAnimPlayFade);
    // With no old blend and no parent the branch is never entered at all.
    EXPECT_EQ(igi::AnimPlayModeFor(30.0f, false, false), igi::kAnimPlayImmediate);
    // A parent alone forces the branch (boneobj.c:44 `old || b->parent`).
    EXPECT_EQ(igi::AnimPlayModeFor(30.0f, false, true), igi::kAnimPlayFade);
}

TEST(AnimBlendTest, NaNBlendTakesTheFadePath) {
    // `!(NaN < 1.0f)` is true, so an uninitialised gTicksPerFrame with a zero
    // blend still fades.
    const float nan = igi::AnimBlendFrames(0.0f, 0.0f);
    ASSERT_TRUE(std::isnan(nan));
    EXPECT_EQ(igi::AnimPlayModeFor(nan, true, false), igi::kAnimPlayFadeNaN);
}

TEST(AnimBlendTest, FadeOutTestsTheLongDoubleNotTheFloat) {
    // anim.c:74-81.
    EXPECT_FLOAT_EQ(igi::AnimFadeOutFrames(0.0f, 160.0f), 0.0f);
    EXPECT_FLOAT_EQ(igi::AnimFadeOutFrames(1.0f, 160.0f), 30.0f);
    // Below one frame the fade is skipped entirely (the blend node is deleted).
    EXPECT_FLOAT_EQ(igi::AnimFadeOutFrames(0.01f, 160.0f), 0.0f);
    // An infinite duration is still a fade.
    EXPECT_TRUE(std::isinf(igi::AnimFadeOutFrames(1.0f, 0.0f)));
}

TEST(AnimSpeedTest, SetSpeedScalesByTicksPerFrameAndDrivesTheDirectionBit) {
    // anim.c:172-179.
    igi::AnimInst inst;
    igi::AnimSetSpeed(inst, 1.0f, 160.0f);
    EXPECT_FLOAT_EQ(inst.speed, 160.0f);
    EXPECT_EQ(inst.flags & igi::kAnimReverse, 0u);

    igi::AnimSetSpeed(inst, -1.0f, 160.0f);
    EXPECT_FLOAT_EQ(inst.speed, -160.0f);
    EXPECT_NE(inst.flags & igi::kAnimReverse, 0u);

    // A zero multiplier CLEARS the reverse bit, because
    // BoneObj_UpdateAnimDirection's test is `speed < 0` with an else.
    igi::AnimSetSpeed(inst, 0.0f, 160.0f);
    EXPECT_FLOAT_EQ(inst.speed, 0.0f);
    EXPECT_EQ(inst.flags & igi::kAnimReverse, 0u);
}

TEST(AnimSpeedTest, SetTimeIsARawStore) {
    // anim.c:181-185: no clamp, no wrap.
    igi::AnimInst inst;
    igi::AnimSetTime(inst, 99999.0f);
    EXPECT_FLOAT_EQ(inst.time, 99999.0f);
    igi::AnimSetTime(inst, -5.0f);
    EXPECT_FLOAT_EQ(inst.time, -5.0f);
}

// ===========================================================================
// The authored form
// ===========================================================================

TEST(AnimAuthoredTest, GenericTbaTokenIndices) {
    // Position 3 tokens (args 3-5), Orientation 3 tokens (args 6-8, because
    // ParamReal32x9_Parse reads three EULER ANGLES - paramops.c:423-435), then
    // fourteen single-token parameters, so "Activate Anim" is token 22 and
    // "Activate Time (s)" token 23.
    EXPECT_EQ(igi::kGenericTbaParamCount, 18);
    EXPECT_EQ(igi::kGenericTbaAnimToken, 22);
    EXPECT_EQ(igi::kGenericTbaUseTimeToken, 23);

    std::vector<std::string> tokens(25);
    for (std::size_t i = 0; i < tokens.size(); ++i) tokens[i] = std::to_string(static_cast<int>(i));
    tokens[22] = "245";
    tokens[23] = "4.0";
    const igi::GenericTbaAnim parsed = igi::ParseGenericTbaAnim(tokens);
    EXPECT_EQ(parsed.anim, 245);
    EXPECT_FLOAT_EQ(parsed.use_time, 4.0f);
    EXPECT_EQ(igi::GenericTbaGetAnim(parsed), 245);
    EXPECT_FLOAT_EQ(igi::GenericTbaGetUseTime(parsed), 4.0f);
}

TEST(AnimAuthoredTest, GenericTbaDefaultsComeFromCreate) {
    // tba.c:105-106: `t->use_time = 4.0f; t->anim = -1;`.
    const igi::GenericTbaAnim parsed = igi::ParseGenericTbaAnim({});
    EXPECT_EQ(parsed.anim, -1);
    EXPECT_FLOAT_EQ(parsed.use_time, 4.0f);
}

TEST(AnimAuthoredTest, GenericTbaShortOrNonNumericTokensKeepTheDefaults) {
    std::vector<std::string> short_tokens(20, "0");
    short_tokens[0] = "1";
    const igi::GenericTbaAnim a = igi::ParseGenericTbaAnim(short_tokens);
    EXPECT_EQ(a.anim, -1);
    EXPECT_FLOAT_EQ(a.use_time, 4.0f);

    std::vector<std::string> bad(25, "\"text\"");
    bad[22] = "abc";
    const igi::GenericTbaAnim b = igi::ParseGenericTbaAnim(bad);
    EXPECT_EQ(b.anim, -1);
    // use_time 23 is the quoted text token, which does not parse as a float.
    EXPECT_FLOAT_EQ(b.use_time, 4.0f);
}

TEST(AnimAuthoredTest, SoldierTokenIndicesAndDefaults) {
    // LEVEL2/objects.qsc:47 declares ... "Bone Heirachy","Int32",
    // "Stand Animation","Int32" as the last two of HumanSoldier.
    EXPECT_EQ(igi::kSoldierBoneHierarchyToken, 9);
    EXPECT_EQ(igi::kSoldierStandAnimationToken, 10);

    std::vector<std::string> tokens(11, "0");
    tokens[9] = "1";
    tokens[10] = "-1";
    const igi::SoldierAnim parsed = igi::ParseSoldierAnim(tokens);
    EXPECT_EQ(parsed.bone_hierarchy, 1);
    EXPECT_EQ(parsed.stand_animation, -1);

    // Unauthored falls back to -1 for both, which the editor uses as
    // "not authored".
    const igi::SoldierAnim empty = igi::ParseSoldierAnim({});
    EXPECT_EQ(empty.bone_hierarchy, -1);
    EXPECT_EQ(empty.stand_animation, -1);

    // The ENGINE's default model is 1, not -1 and not 0 (soldier.c:751).
    EXPECT_EQ(igi::SoldierModelOrDefault(-1), 1);
    EXPECT_EQ(igi::SoldierModelOrDefault(0), 0);
    EXPECT_EQ(igi::SoldierModelOrDefault(5), 5);
}

TEST(AnimAuthoredTest, UseTimeFramesRoundsToNearestEven) {
    // humanstate.c:520 `FRAMES(seconds)` = fistpll((seconds) * 30.0f).
    EXPECT_EQ(igi::AnimUseTimeFrames(4.0f), 120);
    EXPECT_EQ(igi::AnimUseTimeFrames(0.0f), 0);
    EXPECT_EQ(igi::AnimUseTimeFrames(1.0f), 30);
    // 0.05 s * 30 = 1.5 exactly, and the tie goes to even: 2.
    EXPECT_EQ(igi::AnimUseTimeFrames(0.05f), 2);
    // 0.15 s * 30 = 4.5 exactly -> 4.
    EXPECT_EQ(igi::AnimUseTimeFrames(0.15f), 4);
}

TEST(AnimAuthoredTest, TheLoopArgumentIsReallyAFlagsWord) {
    // Human_PlayAnim's `loop` parameter is forwarded straight into Anim_Play's
    // `flags` (loadscreen/human.c:176-179), so the constants are flag words.
    EXPECT_EQ(igi::kAnimLoop, 1u);
    EXPECT_EQ(igi::kAnimReverse, 2u);
    EXPECT_EQ(igi::kAnimLoopGenericTbaUse, igi::kAnimLoop);
    EXPECT_EQ(igi::kAnimLoopSoldierStand, 0u);
    EXPECT_EQ(igi::kAnimLoopSoldierScript, 0u);
    EXPECT_EQ(igi::kAnimLoopSoldierFemaleScript, igi::kAnimLoop);
    EXPECT_EQ(igi::kAnimLoopHumanPlayerCreate, 0u);
    // The one shipped caller that passes 2 is gAnimSlots[38]: REVERSE, not
    // "loop twice".
    EXPECT_EQ(igi::kAnimSlots[38].loop, 2);
}

TEST(AnimAuthoredTest, HumanPlayerIsHardcoded) {
    // humanplayer.c:1212 `BoneDynCubeObj_Init(Game_GetWorld(), h, 0, 0);`
    // and :1214 `Human_PlayAnim(h, 2, 0, 0);`.
    EXPECT_EQ(igi::kHumanPlayerSkeleton, 0);
    EXPECT_EQ(igi::kHumanPlayerStandAnim, 2);
}

TEST(AnimAuthoredTest, NotFoundWarningIsTheShippedString) {
    // pistol/soldier.c:126, rpg/rpg.c:167, soldier/soldier.c:625.
    EXPECT_STREQ(igi::kAnimNotFoundWarning, "Animation #%d not found");
}

TEST(AnimSlotTableTest, HasFortyNineEntriesAndOneReverse) {
    // humanplayer.c:1563, 392 bytes / 8.
    EXPECT_EQ(igi::kAnimSlotCount, 49);
    int reverse = 0;
    int one_shot = 0;
    for (int i = 0; i < igi::kAnimSlotCount; ++i) {
        if (igi::kAnimSlots[i].loop == 2) ++reverse;
        if (igi::kAnimSlots[i].loop == 0) ++one_shot;
        EXPECT_GE(igi::kAnimSlots[i].anim, 0);
        EXPECT_LT(igi::kAnimSlots[i].loop, 3);
    }
    EXPECT_EQ(reverse, 1);
    EXPECT_EQ(one_shot, 13);
    // The first slot is the player's stand clip and it LOOPS.
    EXPECT_EQ(igi::kAnimSlots[0].anim, 2);
    EXPECT_EQ(igi::kAnimSlots[0].loop, 1);
    EXPECT_STREQ(igi::kBodyStateNames[0], "HumanState_Enter00");
    EXPECT_STREQ(igi::kBodyStateNames[48], "HumanPlayer_BodyFlyEnter");
}

// ===========================================================================
// The bone / skin side
// ===========================================================================

TEST(AnimSkinTest, EngineRecordLayoutsMatchTheirStaticAsserts) {
    // dxinit/d3drender.c:37-44 (SkinVertex 0x28) and :46-50 (NormalVertex 0x20).
    EXPECT_EQ(sizeof(igi::SkinVertex), 0x28u);
    EXPECT_EQ(sizeof(igi::NormalVertex), 0x20u);
    EXPECT_EQ(offsetof(igi::SkinVertex, p), 0x00u);
    EXPECT_EQ(offsetof(igi::SkinVertex, n), 0x0Cu);
    EXPECT_EQ(offsetof(igi::SkinVertex, uv), 0x18u);
    EXPECT_EQ(offsetof(igi::SkinVertex, w), 0x20u);
    EXPECT_EQ(offsetof(igi::SkinVertex, target), 0x24u);
    EXPECT_EQ(offsetof(igi::SkinVertex, bone), 0x26u);
    // boneobjtypes.h:60-74, sizeof(Bone) == 0x84.
    EXPECT_EQ(igi::kBoneMatrixOffset, 0x24);
    EXPECT_EQ(igi::kBonePosOffset, 0x18);
    EXPECT_EQ(igi::kBoneSize, 0x84);
    // dxinit/d3drender.c:230,253,275.
    EXPECT_EQ(igi::kMeshSkinVertexCountOffset, 0x10);
    EXPECT_EQ(igi::kMeshSkinExtraCountOffset, 0x0C);
    EXPECT_EQ(igi::kMeshSkinVertexArrayOffset, 0x18);
    EXPECT_EQ(igi::kBoneObjBonesPointerOffset, 8);
}

TEST(AnimSkinTest, IdentityBoneIsJustTheWeightedPosition) {
    // d3drender.c:257-274 with an identity matrix and a zero bone position.
    igi::SkinTransformInput in;
    in.p[0] = 40.96f;
    in.p[1] = 0.0f;
    in.p[2] = 0.0f;
    in.n[2] = 1.0f;
    in.uv[0] = 0.25f;
    in.uv[1] = 0.75f;
    in.w = 1.0f;
    igi::BoneTransform bone;
    const igi::SkinTransformOutput out = igi::SkinTransformVertex(in, bone, false, nullptr);
    EXPECT_FLOAT_EQ(out.position[0], 40.96f);
    EXPECT_FLOAT_EQ(out.normal[2], 1.0f);
    EXPECT_FLOAT_EQ(out.uv[0], 0.25f);
    EXPECT_FLOAT_EQ(out.uv[1], 0.75f);
}

TEST(AnimSkinTest, WeightScalesThePosition) {
    igi::SkinTransformInput in;
    in.p[0] = 40.96f;
    in.w = 0.5f;
    igi::BoneTransform bone;
    const igi::SkinTransformOutput out = igi::SkinTransformVertex(in, bone, false, nullptr);
    EXPECT_FLOAT_EQ(out.position[0], 20.48f);
}

TEST(AnimSkinTest, BonePositionIsAddedBeforeTheWeight) {
    // d3drender.c:262-266: `long double X = x + b->pos.x; o->x = X * v->w;`
    igi::SkinTransformInput in;
    in.p[0] = 10.0f;
    in.w = 0.5f;
    igi::BoneTransform bone;
    bone.pos[0] = 30.0f;
    const igi::SkinTransformOutput out = igi::SkinTransformVertex(in, bone, false, nullptr);
    EXPECT_FLOAT_EQ(out.position[0], 20.0f);  // (10 + 30) * 0.5
}

TEST(AnimSkinTest, RotationIsRowMajorTimesThePoint) {
    // d3drender.c:259-261 is `M * p` with a row-major m (types.h:13-16).
    igi::SkinTransformInput in;
    in.p[1] = 1.0f;
    in.w = 1.0f;
    igi::BoneTransform bone;
    // 90 degrees about Z: x' = -y, y' = x.
    bone.matrix[0] = 0.0f;
    bone.matrix[1] = 1.0f;
    bone.matrix[3] = -1.0f;
    bone.matrix[4] = 0.0f;
    const igi::SkinTransformOutput out = igi::SkinTransformVertex(in, bone, false, nullptr);
    EXPECT_FLOAT_EQ(out.position[0], 1.0f);
    EXPECT_FLOAT_EQ(out.position[1], 0.0f);
}

TEST(AnimSkinTest, ExtraInfluenceAccumulatesIntoTheBaseVertex) {
    // d3drender.c:287-289 `t->x = w * X + t->x;` - an ADD, not a replace.
    igi::SkinTransformOutput base;
    base.position[0] = 1.0f;
    base.position[1] = 1.0f;
    base.position[2] = 1.0f;
    base.normal[2] = 1.0f;
    base.uv[0] = 0.5f;

    igi::SkinTransformInput in;
    in.p[0] = 40.96f;
    in.w = 0.5f;
    in.target = 0;
    igi::BoneTransform bone;

    const igi::SkinTransformOutput out = igi::SkinTransformVertex(in, bone, false, &base);
    EXPECT_FLOAT_EQ(out.position[0], 1.0f + 0.5f * 40.96f);
    EXPECT_FLOAT_EQ(out.position[1], 1.0f);
    // The base vertex's normal and uv are untouched by the extra influence.
    EXPECT_FLOAT_EQ(out.normal[2], 1.0f);
    EXPECT_FLOAT_EQ(out.uv[0], 0.5f);

    igi::SkinTransformOutput in_place = base;
    igi::SkinAccumulatePosition(in, bone, in_place);
    EXPECT_FLOAT_EQ(in_place.position[0], out.position[0]);
    EXPECT_FLOAT_EQ(in_place.position[1], out.position[1]);
}

TEST(AnimSkinTest, RigidPathIgnoresTheWeightAndTheExtraVertices) {
    // d3drender.c:291-309: the rotation and the bone position are written
    // straight out, `w` is never read, and the extra-influence loop is skipped.
    igi::SkinTransformInput in;
    in.p[0] = 40.96f;
    in.w = 0.5f;
    igi::BoneTransform bone;
    bone.pos[0] = 10.0f;
    const igi::SkinTransformOutput out = igi::SkinTransformVertex(in, bone, true, nullptr);
    EXPECT_FLOAT_EQ(out.position[0], 50.96f);  // NOT scaled by 0.5
}

// ===========================================================================
// Corpus: the shipped ANIMS files
// ===========================================================================

TEST(AnimRulesCorpus, AnimFilesDecodeWithTheExpectedSkeletonShape) {
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }
    for (const char* name : kAnimFiles) {
        const DecodedAnimFile file = LoadAnimFile(name);
        ASSERT_TRUE(file.ok) << name;
        // The child chain runs to the end of the file ...
        EXPECT_EQ(file.chain_end, fs::file_size(GameRoot() / "common" / "ANIMS" / name)) << name;
        // ... even though the outer FORM's declared size does not. This is the
        // retail defect described above IffTopLevel: the writer closed BOBJ (and
        // BOAL) before its last child, and BoneObj_Load's id scans are why the
        // files still load.
        EXPECT_NE(file.declared_end, file.chain_end) << name;
        EXPECT_GT(file.chain_end, file.declared_end) << name;
        // BOAL's size is short by its final child too.
        EXPECT_LT(file.boal_declared_end, file.boal_chain_end) << name;
        EXPECT_GT(file.bone_count, 0) << name;
        EXPECT_GT(file.capacity, 0) << name;
        // boneobj.c:1581-1583: `anims` is the count of BOAN forms actually
        // present, so the two must agree.
        EXPECT_EQ(static_cast<int>(file.records.size()), file.anim_count) << name;
        EXPECT_LE(file.anim_count, file.capacity) << name;

        std::set<int> ids;
        for (const igi::AnimRecord& record : file.records) {
            EXPECT_GE(record.id, 0) << name;
            EXPECT_LT(record.id, file.capacity) << name;
            EXPECT_TRUE(ids.insert(record.id).second) << name << " duplicate id " << record.id;
            EXPECT_GT(record.length_ticks, 0.0f) << name << " id " << record.id;
        }
    }
}

TEST(AnimRulesCorpus, SkeletonTablesMatchTheShippedCounts) {
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    struct Expected {
        const char* file;
        int skel;
        int bones;
        int clips;
        int capacity;
    };
    // Read out of the shipped files; the point of the test is that the port's
    // BuildAnimSetTable reproduces them from the raw records.
    const Expected expected[] = {
        {"000.IFF", 0, 33, 122, 247}, {"001.IFF", 1, 33, 95, 212}, {"002.IFF", 2, 33, 18, 20},
        {"003.IFF", 3, 33, 47, 50},   {"005.IFF", 5, 56, 78, 81},  {"006.IFF", 6, 33, 16, 18},
    };
    for (const Expected& e : expected) {
        const DecodedAnimFile file = LoadAnimFile(e.file);
        ASSERT_TRUE(file.ok) << e.file;
        EXPECT_EQ(file.skel, e.skel) << e.file;
        EXPECT_EQ(file.bone_count, e.bones) << e.file;
        EXPECT_EQ(file.anim_count, e.clips) << e.file;
        EXPECT_EQ(file.capacity, e.capacity) << e.file;

        bool out_of_range = false;
        const igi::AnimSetTable table =
            igi::BuildAnimSetTable(file.skel, file.bone_count, file.anim_count, file.capacity,
                                   file.records.data(), static_cast<int>(file.records.size()),
                                   &out_of_range);
        EXPECT_FALSE(out_of_range) << e.file;
        EXPECT_TRUE(table.valid()) << e.file;
        EXPECT_EQ(table.clip_count, e.clips) << e.file;
        // Clip 2 exists in every skeleton but has a DIFFERENT length in each, so
        // the length is checked against the decoded record rather than a shared
        // constant. AnimLengthTicks rounds the float (haze/anim.c:110-115).
        ASSERT_FALSE(file.records.empty()) << e.file;
        EXPECT_FLOAT_EQ(table.descs[2].length, file.records.front().length_ticks) << e.file;
        EXPECT_EQ(igi::AnimLengthTicks(table, 2),
                  static_cast<int>(std::nearbyint(file.records.front().length_ticks)))
            << e.file;
    }
}

TEST(AnimRulesCorpus, SkeletonZeroClipTwoIsThePlayerStandLength) {
    // The player's skeleton 0 is the one the editor cares about; its clip 2 is
    // what HumanPlayer_CreateHandler plays (humanplayer.c:1214).
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile file = LoadAnimFile("000.IFF");
    ASSERT_TRUE(file.ok);
    ASSERT_GT(static_cast<int>(file.records.size()), 2);
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(0, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);
    EXPECT_EQ(file.records.front().id, 2);
    EXPECT_FLOAT_EQ(file.records.front().length_ticks, 13280.0f);
    EXPECT_EQ(igi::AnimLengthTicks(table, 2), 13280);
    EXPECT_EQ(file.records.front().flags & igi::kAnimDescLoops, 0u);
}

TEST(AnimRulesCorpus, EveryAuthoredPlayerSlotResolvesOnSkeletonZero) {
    // gAnimSlots is the table the player's state machine plays from, and the
    // player's skeleton is hardcoded 0 (humanplayer.c:1212). Every NON-ZERO
    // slot id must be a loaded clip; the zero slots are the unassigned ones
    // (id 0 is not a clip in any shipped file, so playing one would fault).
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile file = LoadAnimFile("000.IFF");
    ASSERT_TRUE(file.ok);
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(0, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);

    for (int i = 0; i < igi::kAnimSlotCount; ++i) {
        const int id = igi::kAnimSlots[i].anim;
        if (id == 0) continue;
        const igi::AnimResolution r = igi::ResolveAnim(table, 0, id);
        EXPECT_TRUE(r.found()) << "slot " << i << " (" << igi::kBodyStateNames[i] << ") anim " << id
                               << " lookup " << static_cast<int>(r.lookup);
        if (r.found()) EXPECT_GT(r.length_ticks, 0.0f) << "slot " << i;
    }
}

TEST(AnimRulesCorpus, SkeletonOneRejectsTheLevel2GenericTbaAnimation) {
    // LEVEL2 authors "Activate Anim" = 245 on three GenericTBA objects. Those
    // play on the USING HUMAN's own BoneObj (humanstate.c:996), and for the
    // player that is skeleton 0 - capacity 247, so 245 is a real clip. For a
    // HumanSoldier on skeleton 1 the same id is past the 212-slot capacity and
    // Anim_Exists says no.
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile skel0 = LoadAnimFile("000.IFF");
    const DecodedAnimFile skel1 = LoadAnimFile("001.IFF");
    ASSERT_TRUE(skel0.ok);
    ASSERT_TRUE(skel1.ok);
    const igi::AnimSetTable table0 =
        igi::BuildAnimSetTable(0, skel0.bone_count, skel0.anim_count, skel0.capacity,
                               skel0.records.data(), static_cast<int>(skel0.records.size()), nullptr);
    const igi::AnimSetTable table1 =
        igi::BuildAnimSetTable(1, skel1.bone_count, skel1.anim_count, skel1.capacity,
                               skel1.records.data(), static_cast<int>(skel1.records.size()), nullptr);

    const igi::AnimResolution on_player = igi::ResolveAnim(table0, 0, 245);
    ASSERT_TRUE(on_player.found());
    // The shipped clip: 14400 ticks = exactly 3.0 seconds at 4800 a second, and
    // flagged ANIM_LOOPS.
    EXPECT_FLOAT_EQ(on_player.length_ticks, 14400.0f);
    EXPECT_NE(table0.descs[245].flags & igi::kAnimDescLoops, 0u);
    EXPECT_NEAR(igi::AnimLengthSeconds(table0, 245), 3.0, 1e-6);
    EXPECT_EQ(igi::AnimLengthTicks(table0, 245), 14400);
    // A 14400-tick clip at 160 ticks a frame is exactly 90 frames.
    EXPECT_FLOAT_EQ(14400.0f / igi::kAnimDefaultTicksPerFrame, 90.0f);

    EXPECT_EQ(igi::ResolveAnim(table1, 0, 245).lookup, igi::kAnimIdAtOrAboveCapacity);
    EXPECT_EQ(igi::AnimLengthTicks(table1, 245), -1);
}

TEST(AnimRulesCorpus, TheCapacityGapIsInRangeButEmpty) {
    // boneobj.c:1583-1587 sizes byId by the CAPACITY and fills only `anims`
    // slots, so skeleton 1 has 212 slots with 95 filled: ids 96..211 are in
    // range and NULL. That is the difference between kAnimSlotEmpty and
    // kAnimIdAtOrAboveCapacity, and it is why Anim_Exists is not enough on its
    // own to tell "not authored" from "not loaded".
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile file = LoadAnimFile("001.IFF");
    ASSERT_TRUE(file.ok);
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(1, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);

    int empty_in_range = 0;
    for (int id = 0; id < table.capacity; ++id) {
        const igi::AnimResolution r = igi::ResolveAnim(table, 0, id);
        if (r.lookup == igi::kAnimSlotEmpty) ++empty_in_range;
        EXPECT_NE(r.lookup, igi::kAnimIdAtOrAboveCapacity) << "id " << id;
    }
    EXPECT_EQ(empty_in_range, table.capacity - table.clip_count);
    EXPECT_EQ(empty_in_range, 212 - 95);
    // Ids 0 and 1 are inside the capacity and were never loaded either, so they
    // are part of the same gap - which is why the "no animation authored"
    // sentinel -1 and the "slot 0" value 0 are both misses on this skeleton.
    EXPECT_EQ(igi::ResolveAnim(table, 0, 0).lookup, igi::kAnimSlotEmpty);
    EXPECT_EQ(igi::ResolveAnim(table, 0, 1).lookup, igi::kAnimSlotEmpty);
}

TEST(AnimRulesCorpus, NegativeIdsAreTheHoleAndNeverAppearInTheData) {
    // Anim_Exists (haze/anim.c:244-255) has no `id < 0` test, but no shipped
    // record has a negative id either - so the hole is latent, not exercised.
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    for (const char* name : kAnimFiles) {
        const DecodedAnimFile file = LoadAnimFile(name);
        ASSERT_TRUE(file.ok) << name;
        for (const igi::AnimRecord& record : file.records) {
            EXPECT_GE(record.id, 0) << name;
        }
    }
    const DecodedAnimFile file = LoadAnimFile("000.IFF");
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(0, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);
    EXPECT_EQ(igi::ResolveAnim(table, 0, -1).lookup, igi::kAnimIdNegative);
    EXPECT_EQ(igi::ResolveAnim(table, 0, -88).lookup, igi::kAnimIdNegative);
}

TEST(AnimRulesCorpus, IdsAreSparseNotDense) {
    // The ids are NOT 2..count. Skeleton 0 spreads 122 clips over 245 slots and
    // skeleton 1 has 95 clips of which one sits far out at 211 - so "the number
    // of clips" tells you nothing about which ids exist, and only the table can
    // answer. These are the shipped layouts the port's table must reproduce.
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    struct Expected {
        const char* file;
        int clips;
        int lowest;
        int highest;
        int holes;
    };
    const Expected expected[] = {
        {"000.IFF", 122, 2, 246, 123},
        {"001.IFF", 95, 2, 211, 115},
        {"002.IFF", 18, 2, 19, 0},
        {"003.IFF", 47, 2, 49, 1},
        {"005.IFF", 78, 2, 80, 1},
        {"006.IFF", 16, 2, 17, 0},
    };
    for (const Expected& e : expected) {
        const DecodedAnimFile file = LoadAnimFile(e.file);
        ASSERT_TRUE(file.ok) << e.file;
        ASSERT_EQ(static_cast<int>(file.records.size()), e.clips) << e.file;

        const igi::AnimSetTable table =
            igi::BuildAnimSetTable(file.skel, file.bone_count, file.anim_count, file.capacity,
                                   file.records.data(), static_cast<int>(file.records.size()),
                                   nullptr);
        int lowest = -1;
        int highest = -1;
        int holes = 0;
        for (int id = 0; id < table.capacity; ++id) {
            const igi::AnimResolution r = igi::ResolveAnim(table, 0, id);
            if (!r.found()) continue;
            if (lowest < 0) lowest = id;
            highest = id;
        }
        ASSERT_GE(lowest, 0) << e.file;
        EXPECT_EQ(lowest, e.lowest) << e.file;
        EXPECT_EQ(highest, e.highest) << e.file;
        for (int id = lowest; id <= highest; ++id) {
            if (!igi::ResolveAnim(table, 0, id).found()) ++holes;
        }
        EXPECT_EQ(holes, e.holes) << e.file;
    }

    // The two single holes, named.
    const DecodedAnimFile skel3 = LoadAnimFile("003.IFF");
    const igi::AnimSetTable t3 =
        igi::BuildAnimSetTable(3, skel3.bone_count, skel3.anim_count, skel3.capacity,
                               skel3.records.data(), static_cast<int>(skel3.records.size()), nullptr);
    EXPECT_EQ(igi::ResolveAnim(t3, 0, 8).lookup, igi::kAnimSlotEmpty);
    EXPECT_TRUE(igi::ResolveAnim(t3, 0, 7).found());
    EXPECT_TRUE(igi::ResolveAnim(t3, 0, 9).found());

    const DecodedAnimFile skel5 = LoadAnimFile("005.IFF");
    const igi::AnimSetTable t5 =
        igi::BuildAnimSetTable(5, skel5.bone_count, skel5.anim_count, skel5.capacity,
                               skel5.records.data(), static_cast<int>(skel5.records.size()), nullptr);
    EXPECT_EQ(igi::ResolveAnim(t5, 0, 42).lookup, igi::kAnimSlotEmpty);
    EXPECT_TRUE(igi::ResolveAnim(t5, 0, 41).found());
    EXPECT_TRUE(igi::ResolveAnim(t5, 0, 43).found());

    // Skeleton 1's lone high clip: 211 is real and 96..210 are the gap, which is
    // why the capacity test in Anim_Exists and the table lookup disagree for
    // every id in that band.
    const DecodedAnimFile skel1 = LoadAnimFile("001.IFF");
    const igi::AnimSetTable t1 =
        igi::BuildAnimSetTable(1, skel1.bone_count, skel1.anim_count, skel1.capacity,
                               skel1.records.data(), static_cast<int>(skel1.records.size()), nullptr);
    EXPECT_TRUE(igi::ResolveAnim(t1, 0, 211).found());
    EXPECT_EQ(igi::ResolveAnim(t1, 0, 210).lookup, igi::kAnimSlotEmpty);
    EXPECT_EQ(igi::ResolveAnim(t1, 0, 96).lookup, igi::kAnimSlotEmpty);
}

TEST(AnimRulesCorpus, EveryClipHasAPositionTrackStartingAtTimeZero) {
    // boneobj.c:794-815 needs at least one key, and the reverse wrap fixup at
    // boneobj.c:914-920 needs keys[0].time == 0 to avoid reading keys[-1] when
    // it samples the track at time 0.
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    int clips = 0;
    for (const char* name : kAnimFiles) {
        const DecodedAnimFile file = LoadAnimFile(name);
        ASSERT_TRUE(file.ok) << name;
        EXPECT_FALSE(file.first_pos_keys.empty()) << name;
        EXPECT_FLOAT_EQ(file.first_pos_keys.front().time, 0.0f) << name;
        clips += file.anim_count;
    }
    EXPECT_EQ(clips, 122 + 95 + 18 + 47 + 78 + 16);
}

TEST(AnimRulesCorpus, TheLevel2GenericTbaClipLoopsInExactlyNinetyFrames) {
    // End to end on shipped data: play skeleton 0's clip 245 (the LEVEL2
    // "Activate Anim") forward with loop at 160 ticks a frame and count the
    // frames until the wrap. 14400 / 160 = 90 exactly, and the tick that lands
    // exactly on the length is the one that wraps.
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile file = LoadAnimFile("000.IFF");
    ASSERT_TRUE(file.ok);
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(0, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);
    const igi::AnimResolution r = igi::ResolveAnim(table, 0, 245);
    ASSERT_TRUE(r.found());

    igi::AnimInst inst;
    // The root track (bone 0), played the way humanstate.c:996 does:
    // `Human_PlayAnim(h, GenericTBA_GetAnim(obj, 1, 0), 1, 0.0f)` - loop = 1.
    ASSERT_TRUE(igi::InitAnimInst(inst, table, 0, 245, igi::kAnimLoopGenericTbaUse,
                                  igi::kAnimDefaultTicksPerFrame, nullptr, nullptr));

    int frames = 0;
    bool wrapped = false;
    for (; frames < 1000; ++frames) {
        const igi::AnimAdvance a =
            igi::AdvanceAnimInst(inst, r.length_ticks, nullptr);
        if (a.wrapped) {
            wrapped = true;
            break;
        }
    }
    EXPECT_TRUE(wrapped);
    EXPECT_EQ(frames, 89);  // 0-based: the 90th tick wraps
    EXPECT_FLOAT_EQ(inst.time, 0.0f);

    // And the one-shot reading of the same clip, which is what the Soldier
    // "Stand Animation" path would do if it were authored here (flags = 0):
    // it clamps at the length and stays there.
    igi::AnimInst once;
    ASSERT_TRUE(igi::InitAnimInst(once, table, 0, 245, igi::kAnimLoopSoldierStand,
                                  igi::kAnimDefaultTicksPerFrame, nullptr, nullptr));
    for (int i = 0; i < 90; ++i) (void)igi::AdvanceAnimInst(once, r.length_ticks, nullptr);
    EXPECT_FLOAT_EQ(once.time, 14400.0f);
    EXPECT_NE(once.flags & igi::kBoneEventsDone, 0u);
}

TEST(AnimRulesCorpus, AuthoredClip245PlaysForFourSecondsOfUseTime) {
    // The authored pair is "Activate Anim" 245 (a 3.0 s clip) with
    // "Activate Time (s)" 4.0, i.e. 120 frames of use window
    // (humanstate.c:789 `h->actionFrames = FRAMES(GenericTBA_GetUseTime(obj))`).
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile file = LoadAnimFile("000.IFF");
    ASSERT_TRUE(file.ok);
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(0, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);

    const int use_frames = igi::AnimUseTimeFrames(4.0f);
    EXPECT_EQ(use_frames, 120);
    const double clip_seconds = igi::AnimLengthSeconds(table, 245);
    EXPECT_NEAR(clip_seconds, 3.0, 1e-6);
    // The window is longer than the clip, so the loop must wrap at least once.
    EXPECT_GT(use_frames * 1.0 / 30.0, clip_seconds);
}

// ===========================================================================
// Corpus: the authored level data
// ===========================================================================

TEST(AnimRulesCorpus, Level2AuthorsActivateAnim245AndUseTime4) {
    // MISSIONS/location0/LEVEL2/objects.qsc:763,766,769 - three GenericTBA
    // objects, each with `..., 245, 4.0, "STATUSSCREEN:a_c4.spr")`.
    const fs::path path = LevelDir(2) / "objects.qsc";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
    }
    const std::vector<std::vector<std::string>> calls = ParseGenericTbaCallsFromText(ReadFile(path));
    ASSERT_EQ(calls.size(), 3u);
    for (const std::vector<std::string>& tokens : calls) {
        ASSERT_EQ(tokens.size(), 3u + kGenericTbaValueCount) << "token count";
        EXPECT_EQ(tokens[1], "\"GenericTBA\"");
        // The icon name is the last value, and it is the one the engine looks up
        // in the 10-entry sprite table (tba.c:169-178).
        EXPECT_EQ(tokens[24], "\"STATUSSCREEN:a_c4.spr\"");
        const igi::GenericTbaAnim parsed = igi::ParseGenericTbaAnim(tokens);
        EXPECT_EQ(parsed.anim, 245);
        EXPECT_FLOAT_EQ(parsed.use_time, 4.0f);
        EXPECT_EQ(igi::GenericTbaGetAnim(parsed), 245);
        EXPECT_EQ(igi::AnimUseTimeFrames(igi::GenericTbaGetUseTime(parsed)), 120);
    }
}
TEST(AnimRulesCorpus, Level2SoldiersAuthorHierarchy1AndNoStandAnimation) {
    // MISSIONS/location0/LEVEL2/objects.qsc:47 declares the pair, and the
    // shipped instances author "Bone Heirachy" = 1 with "Stand Animation" = -1,
    // which is exactly the engine's own "no animation authored" sentinel
    // (soldier.c:746) and therefore takes the Human_PlayAnimSlot fallback.
    const fs::path path = LevelDir(2) / "objects.qsc";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
    }
    const std::vector<std::string> tokens = FirstSoldierCallFromText(ReadFile(path));
    ASSERT_FALSE(tokens.empty());
    ASSERT_GE(tokens.size(), 11u);
    const igi::SoldierAnim parsed = igi::ParseSoldierAnim(tokens);
    EXPECT_EQ(parsed.bone_hierarchy, 1);
    EXPECT_EQ(parsed.stand_animation, -1);
    // And skeleton 1 is where that soldier's animations resolve, which is why
    // the 245 above cannot be his.
    EXPECT_EQ(igi::SoldierModelOrDefault(parsed.bone_hierarchy), 1);
}

TEST(AnimRulesCorpus, DeclaredParametersPutActivateAnimAtToken22) {
    // The token indices are derived from the SHIPPING declaration rather than
    // from a hand-copied list: 18 parameters, of which Position and Orientation
    // each eat three tokens, so "Activate Anim" lands at 22 and
    // "Activate Time (s)" at 23.
    const fs::path path = LevelDir(2) / "objects.qsc";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
    }
    const std::vector<std::pair<std::string, std::string>> declared =
        ParseDeclaredGenericTbaFromText(ReadFile(path));
    ASSERT_EQ(declared.size(), static_cast<std::size_t>(igi::kGenericTbaParamCount));

    int token = 3;  // argTokens: [0]=id, [1]=type, [2]=name, [3]=parameter 0
    int anim_token = -1;
    int use_time_token = -1;
    for (std::size_t i = 0; i < declared.size(); ++i) {
        if (declared[i].first == "\"Activate Anim\"") anim_token = token;
        if (declared[i].first == "\"Activate Time (s)\"") use_time_token = token;
        token += TokensForDeclaredType(declared[i].second);
    }
    EXPECT_EQ(token, 25);  // 3 + 22 values
    EXPECT_EQ(anim_token, igi::kGenericTbaAnimToken);
    EXPECT_EQ(use_time_token, igi::kGenericTbaUseTimeToken);
    EXPECT_EQ(anim_token, 22);
    EXPECT_EQ(use_time_token, 23);

    // And the two names the port exposes are the authored ones, verbatim.
    EXPECT_EQ(declared[15].first, std::string("\"") + igi::kGenericTbaAnimName + "\"");
    EXPECT_EQ(declared[16].first, std::string("\"") + igi::kGenericTbaUseTimeName + "\"");
    EXPECT_EQ(declared[15].second, "\"Int16\"");
    EXPECT_EQ(declared[16].second, "\"Real32\"");

    // The type name that makes this a trap: the authored type says nine reals and
    // the engine reads three.
    EXPECT_EQ(declared[1].first, "\"Orientation\"");
    EXPECT_EQ(declared[1].second, "\"Real32x9\"");
    EXPECT_EQ(TokensForDeclaredType(declared[1].second), 3);
}

TEST(AnimRulesCorpus, QvmGenericTbaCallsAgreeWithTheDecompiledQsc) {
    // LEVEL2 ships both OBJECTS.QVM and a decompiled objects.qsc, so the two
    // readers can be compared - which proves the QVM token reader is aligned
    // with the argument list rather than off by a token.
    const fs::path dir = LevelDir(2);
    if (!fs::is_regular_file(dir / "OBJECTS.QVM") || !fs::is_regular_file(dir / "objects.qsc")) {
        GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse((dir / "OBJECTS.QVM").string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::vector<std::vector<std::string>> from_qvm = ExtractGenericTbaCalls(qvm);
    const std::vector<std::vector<std::string>> from_text =
        ParseGenericTbaCallsFromText(ReadFile(dir / "objects.qsc"));

    ASSERT_EQ(from_qvm.size(), from_text.size());
    for (std::size_t i = 0; i < from_qvm.size(); ++i) {
        const igi::GenericTbaAnim a = igi::ParseGenericTbaAnim(from_qvm[i]);
        const igi::GenericTbaAnim b = igi::ParseGenericTbaAnim(from_text[i]);
        EXPECT_EQ(a.anim, b.anim) << "#" << i;
        EXPECT_FLOAT_EQ(a.use_time, b.use_time) << "#" << i;
    }
}

TEST(AnimRulesCorpus, EveryLevelsGenericTbaAuthorsAResolvableAnimation) {
    // The authored "Activate Anim" is played on the PLAYER's skeleton
    // (skeleton 0, humanplayer.c:1212), so it must resolve there. This is the
    // cross-check that ties the authored data to the animation table.
    if (!AnimsAvailable()) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const DecodedAnimFile file = LoadAnimFile("000.IFF");
    ASSERT_TRUE(file.ok);
    const igi::AnimSetTable table =
        igi::BuildAnimSetTable(0, file.bone_count, file.anim_count, file.capacity,
                               file.records.data(), static_cast<int>(file.records.size()), nullptr);

    int levels_checked = 0;
    int calls_checked = 0;
    for (int level = 0; level <= 20; ++level) {
        const fs::path qvm_path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(qvm_path)) continue;
        const QVMFile qvm = QVM_Parse(qvm_path.string());
        if (!qvm.valid) continue;
        ++levels_checked;
        for (const std::vector<std::string>& tokens : ExtractGenericTbaCalls(qvm)) {
            const igi::GenericTbaAnim parsed = igi::ParseGenericTbaAnim(tokens);
            ++calls_checked;
            if (parsed.anim < 0) continue;  // unauthored: the engine's default
            const igi::AnimResolution r = igi::ResolveAnim(table, 0, parsed.anim);
            EXPECT_TRUE(r.found()) << "LEVEL" << level << " anim " << parsed.anim << " lookup "
                                   << static_cast<int>(r.lookup);
        }
    }
    EXPECT_GT(levels_checked, 0);
    EXPECT_GT(calls_checked, 0);
}
