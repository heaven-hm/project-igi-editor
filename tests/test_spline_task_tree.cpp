/******************************************************************************
 * @file    test_spline_task_tree.cpp
 * @brief   SplineObj / SplineObjWaypoint in the editor's task tree and inspector.
 *
 * GitHub issue #79 asks for "OpenIGI road and track spline objects": a road, a
 * track, a bridge or a rail is a `SplineObj` container task with a chain of
 * `SplineObjWaypoint` children, and the editor has to be able to author them.
 *
 * The data model and the render path landed earlier (source/runtime/spline_rules.*,
 * source/level/spline_geometry.*, source/renderer/renderer_splines.cpp). What was
 * missing was the AUTHORING SURFACE: `GetBuiltinSchema("SplineObj")` returned
 * nullptr, and the task tree's property panel skips the whole panel when the schema
 * is null (source/renderer/renderer_draw.cpp:2228-2230). A road could be selected
 * and not edited. The waypoint's third parameter was also labelled "Model", a name
 * the engine never uses — it is "Waypoint Model".
 *
 * WHAT IS PINNED HERE
 * -------------------
 *   1. The tree: the loader nests the waypoints under their container, so the tree
 *      view (renderer_draw.cpp:807-950, driven by parentIndex/childrenIndices) has a
 *      SplineObj node with SplineObjWaypoint children.
 *   2. The inspector rows: field name, declared type and absolute QSC argument
 *      offset, compared against the engine's own ParamTable_Add order and the
 *      retail Task_DeclareParameters line — field for field and offset for offset,
 *      in both directions, so an invented field fails as loudly as a missing one.
 *   3. The authoring round trip: the container's "Position" row writes args 9-11
 *      (never 3-5, which are the four bool8 flags and the two Int32s) and the edit
 *      reaches the serialized line with the flags untouched.
 *
 * THE ORACLE
 * ----------
 *   IGI-RE src/editobj/spline.c:177-225  SplineObj_Open HOOK 0x502250
 *     ParamTable_New(10) @:204, ParamTable_Add @:205-214 (gSplineObjSegmentRange
 *     2..20 @:202-203); ParamTable_New(8) @:215, ParamTable_Add @:216-223.
 *   MISSIONS/location0/level1/objects.qsc:2-4 — the shipped level's own
 *     Task_DeclareParameters lines, reproduced verbatim below.
 *
 * NOT PROVEN HERE: the rendered result. The editor's GUI needs OpenGL 4.1 and this
 * host offers 2.1, so nothing in this file is a screenshot or a viewport claim.
 ******************************************************************************/

#include <gtest/gtest.h>

#include "level/level_common.h"
#include "level/level_objects.h"
#include "level/task_schema.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// The two shipping declarations, copied VERBATIM from
// MISSIONS/location0/level1/objects.qsc:2-4 (the "SplineObj" call wraps onto the
// next line in the file; it is joined here the way the QSC lexer sees it).
constexpr const char* kSplineObjDeclaration =
    "\"SplineObj\", \"Linear Segments\", \"bool8\", \"Display waypoints\", \"bool8\", "
    "\"Snap Length\", \"bool8\", \"Automatic Orientation\", \"bool8\", "
    "\"Number of Matrices / Segment\", \"Int32\", \"Collision LOD\", \"Int32\", "
    "\"Position\", \"Real32x3\", \"Gamma Orientation\", \"Angle\", "
    "\"Dirlight Angular Effect\", \"RGB\", \"Dirlight Ambient Effect\", \"RGB\"";

constexpr const char* kSplineWaypointDeclaration =
    "\"SplineObjWaypoint\", \"Orientation\", \"Real32x9\", \"Position\", \"ObjectPos\", "
    "\"Waypoint Model\", \"String16\", \"Segment Model\", \"String16\", "
    "\"NumAreas\", \"Int32\", \"Align\", \"bool8\", \"Flip\", \"bool8\", "
    "\"Automatic Orientation\", \"bool8\"";

// Split a Task_DeclareParameters argument list into tokens, honouring the quotes
// (ParseDeclaration expects the surrounding quotes still present).
std::vector<std::string> SplitDeclArgs(const std::string& args) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (const char c : args) {
        if (c == '"') inQuotes = !inQuotes;
        if (c == ',' && !inQuotes) {
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            out.push_back(cur);
            cur.clear();
            continue;
        }
        if (!cur.empty() || c != ' ') cur.push_back(c);
    }
    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// One authored spline: a container with two waypoints, in the retail shape
// (container flags at 3-6, counts at 7-8, Position at 9-11, Gamma at 12, two RGB
// triples at 13-18; waypoint orientation at 3-5, Position at 6-8, models at 9-10,
// NumAreas/Align/Flip/Automatic Orientation at 11-14).
constexpr const char* kSplineQsc =
    "Task_New(1500, \"SplineObj\", \"Road1\", FALSE, FALSE, FALSE, FALSE, 20, 0, "
    "111.5, 222.5, 333.5, 3.125, 1, 1, 1, 0, 0, 0,\n"
    "    Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 3.125, 2630.0, -5650.0, "
    "1740.0, \"waypoint\", \"368_01_1\", 20, FALSE, FALSE, FALSE),\n"
    "    Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 3.125, 2512.0, -5595.0, "
    "1744.0, \"waypoint\", \"322_01_1\", 10, TRUE, FALSE, TRUE));\n";

// Write `qsc` to a temp file, load it through the real level-loading path, and
// hand the objects back. The temp file is removed before returning.
std::vector<LevelObject> LoadQsc(const std::string& qsc, const char* name) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / name;
    {
        std::ofstream out(path, std::ios::trunc);
        EXPECT_TRUE(out.is_open());
        out << qsc;
    }
    QSC parsed;
    parsed.Load(path.string().c_str());
    LevelObjects objects;
    objects.Load(nullptr, &parsed);
    std::filesystem::remove(path);
    return objects.GetObjects();
}

int FindIndex(const std::vector<LevelObject>& objects, const std::string& type) {
    for (int i = 0; i < static_cast<int>(objects.size()); ++i)
        if (objects[i].type == type) return i;
    return -1;
}

// Collect the (name, type, argOffset) triples of a schema as strings, so a
// mismatch prints the whole row rather than a single component.
std::vector<std::string> FieldRows(const TaskSchemaNS::TaskSchema& schema) {
    std::vector<std::string> rows;
    for (const auto& f : schema)
        rows.push_back(f.name + " " + f.typeName + " @" + std::to_string(f.argOffset));
    return rows;
}

// The row of a schema whose authored name is `name`, or -1.
int RowNamed(const TaskSchemaNS::TaskSchema& schema, const std::string& name) {
    for (int i = 0; i < static_cast<int>(schema.size()); ++i)
        if (schema[i].name == name) return i;
    return -1;
}

}  // namespace

// ── 1. The task tree ────────────────────────────────────────────────────────

// A road's waypoints are children of their container, which is what makes the tree
// view draw them nested (renderer_draw.cpp:945-949 recurses childrenIndices).
TEST(SplineTaskTreeTest, WaypointsAreNestedUnderTheirContainer) {
    const std::vector<LevelObject> objects = LoadQsc(kSplineQsc, "igi_spline_tree.qsc");

    const int container = FindIndex(objects, "SplineObj");
    ASSERT_GE(container, 0) << "the loader produced no SplineObj";
    EXPECT_TRUE(objects[container].isSplineContainer);
    EXPECT_EQ(objects[container].parentIndex, -1) << "the container is a root task";

    std::vector<int> waypoints;
    for (int i = 0; i < static_cast<int>(objects.size()); ++i)
        if (objects[i].type == "SplineObjWaypoint") waypoints.push_back(i);
    ASSERT_EQ(waypoints.size(), 2u);

    for (const int w : waypoints) {
        EXPECT_TRUE(objects[w].isSplineWaypoint);
        EXPECT_EQ(objects[w].parentIndex, container)
            << "waypoint " << w << " is not a child of the container";
    }
    // The container's child list is the tree view's recursion order, and the
    // renderer walks it in that order to pair consecutive waypoints into spans
    // (renderer_splines.cpp:138-161).
    EXPECT_EQ(objects[container].childrenIndices, waypoints);
}

// ── 2. The inspector rows ───────────────────────────────────────────────────

// The container's ten rows are the engine's ten ParamTable entries, in order, at
// the argument offsets the token counts put them at. "Position" lands on 9-11.
TEST(SplineTaskTreeTest, ContainerSchemaMatchesTheEnginesDeclaration) {
    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("SplineObj");
    ASSERT_NE(schema, nullptr)
        << "SplineObj has no builtin schema, so the property panel renders nothing";

    // ParseDeclaration turns the shipped declaration into the same structure,
    // offsetting from arg 3 by each type's token count. Comparing the two vectors
    // pins the name, the type AND the offset of every row at once, and fails on an
    // invented row as well as on a missing one.
    const TaskSchemaNS::TaskSchema declared =
        TaskSchemaNS::ParseDeclaration(SplitDeclArgs(kSplineObjDeclaration));
    ASSERT_EQ(declared.size(), 10u);
    EXPECT_EQ(FieldRows(*schema), FieldRows(declared));

    // The offsets the rest of the editor indexes argTokens with.
    EXPECT_EQ((*schema)[0].argOffset, 3);    // Linear Segments   bool8
    EXPECT_EQ((*schema)[1].argOffset, 4);    // Display waypoints bool8
    EXPECT_EQ((*schema)[2].argOffset, 5);    // Snap Length       bool8
    EXPECT_EQ((*schema)[3].argOffset, 6);    // Automatic Orientation bool8
    EXPECT_EQ((*schema)[4].argOffset, 7);    // Number of Matrices / Segment Int32
    EXPECT_EQ((*schema)[5].argOffset, 8);    // Collision LOD     Int32
    EXPECT_EQ((*schema)[6].argOffset, 9);    // Position          Real32x3 (args 9-11)
    EXPECT_EQ((*schema)[7].argOffset, 12);   // Gamma Orientation Angle
    EXPECT_EQ((*schema)[8].argOffset, 13);   // Dirlight Angular Effect RGB
    EXPECT_EQ((*schema)[9].argOffset, 16);   // Dirlight Ambient Effect RGB
    EXPECT_EQ((*schema)[6].typeName, "Real32x3");
    EXPECT_EQ((*schema)[6].argCount, 3);
}

// The waypoint's eight rows likewise, including the authored name of its third
// parameter, which the engine calls "Waypoint Model" and never "Model".
TEST(SplineTaskTreeTest, WaypointSchemaMatchesTheEnginesDeclaration) {
    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("SplineObjWaypoint");
    ASSERT_NE(schema, nullptr);

    const TaskSchemaNS::TaskSchema declared =
        TaskSchemaNS::ParseDeclaration(SplitDeclArgs(kSplineWaypointDeclaration));
    ASSERT_EQ(declared.size(), 8u);
    EXPECT_EQ(FieldRows(*schema), FieldRows(declared));

    EXPECT_EQ((*schema)[0].argOffset, 3);    // Orientation       Real32x9
    EXPECT_EQ((*schema)[1].argOffset, 6);    // Position          ObjectPos
    EXPECT_EQ((*schema)[2].name, "Waypoint Model");
    EXPECT_EQ((*schema)[2].argOffset, 9);
    EXPECT_EQ((*schema)[3].name, "Segment Model");
    EXPECT_EQ((*schema)[3].argOffset, 10);
    EXPECT_EQ((*schema)[4].name, "NumAreas");
    EXPECT_EQ((*schema)[4].argOffset, 11);
    EXPECT_EQ((*schema)[5].name, "Align");
    EXPECT_EQ((*schema)[6].name, "Flip");
    EXPECT_EQ((*schema)[7].name, "Automatic Orientation");
    EXPECT_EQ((*schema)[7].argOffset, 14);
}

// The level's own Task_DeclareParameters wins over the builtin (task_schema.cpp
// GetSchema), which is the path a shipped level takes (app_level.cpp:388-397). Both
// resolutions must expose the same ten rows for a road, or the panel would change
// shape depending on whether the level happened to declare the type.
TEST(SplineTaskTreeTest, RegisteredDeclarationAgreesWithTheBuiltin) {
    TaskSchemaNS::RegisterSchema(
        "SplineObj", TaskSchemaNS::ParseDeclaration(SplitDeclArgs(kSplineObjDeclaration)));
    const TaskSchemaNS::TaskSchema* resolved = TaskSchemaNS::GetSchema("SplineObj");
    ASSERT_NE(resolved, nullptr);
    const TaskSchemaNS::TaskSchema* builtin =
        TaskSchemaNS::GetBuiltinSchema("SplineObj");
    ASSERT_NE(builtin, nullptr);
    EXPECT_EQ(FieldRows(*resolved), FieldRows(*builtin));
    TaskSchemaNS::ClearRegisteredSchemas();
}

// ── 3. Authoring through the inspector ──────────────────────────────────────

// The container's authored "Position" is args 9-11. The inspector writes the row's
// offset (app_input_mouse.cpp:711-717 for a Real32x3 scrub, app_editor.cpp:866 for
// a typed value), so moving a road in the editor must rewrite exactly those three
// tokens and leave the four flags and the two counts alone — the defect this row
// exists to prevent is a position edit that lands on the flags.
TEST(SplineTaskTreeTest, ContainerPositionRowEditsArgs9To11Only) {
    std::vector<LevelObject> objects = LoadQsc(kSplineQsc, "igi_spline_author.qsc");
    const int container = FindIndex(objects, "SplineObj");
    ASSERT_GE(container, 0);

    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("SplineObj");
    ASSERT_NE(schema, nullptr);
    const int posRow = RowNamed(*schema, "Position");
    ASSERT_GE(posRow, 0);
    const int base = (*schema)[posRow].argOffset;
    ASSERT_EQ(base, 9);

    LevelObject& road = objects[container];
    ASSERT_GE(road.argTokens.size(), 19u);
    // Loaded from the authored call: the flags and counts are where the engine put
    // them, not at 3-5.
    EXPECT_EQ(road.argTokens[3], "FALSE");
    EXPECT_EQ(road.argTokens[7], "20");
    EXPECT_DOUBLE_EQ(road.pos.x, 111.5);
    EXPECT_DOUBLE_EQ(road.pos.y, 222.5);
    EXPECT_DOUBLE_EQ(road.pos.z, 333.5);

    // What the inspector's NumBox commit does for component c of a row.
    road.argTokens[base + 0] = "41000.500000";
    road.argTokens[base + 1] = "-5200.250000";
    road.argTokens[base + 2] = "600.000000";
    road.pos = glm::dvec3(41000.5, -5200.25, 600.0);
    road.modified = true;
    LevelObjects serializer;
    serializer.UpdateCoordinatesInLine(road);

    // FormatQscDouble keeps 2 decimals for |v| >= 1000 and trims trailing zeros
    // only after a decimal point (level_objects_internal.h:44-64), so the
    // persisted line is exactly this.
    EXPECT_EQ(serializer.GenerateTaskLine(road),
              "Task_New(1500, \"SplineObj\", \"Road1\", FALSE, FALSE, FALSE, FALSE, "
              "20, 0, 41000.5, -5200.25, 600.0, 3.125, 1, 1, 1, 0, 0, 0)");
    // The three tokens the "Position" row owns, and only those.
    EXPECT_EQ(road.argTokens[base + 0], "41000.5");
    EXPECT_EQ(road.argTokens[base + 1], "-5200.25");
    EXPECT_EQ(road.argTokens[base + 2], "600.0");
    EXPECT_EQ(road.argTokens[3], "FALSE");
    EXPECT_EQ(road.argTokens[4], "FALSE");
    EXPECT_EQ(road.argTokens[5], "FALSE");
    EXPECT_EQ(road.argTokens[6], "FALSE");
    EXPECT_EQ(road.argTokens[7], "20");
    EXPECT_EQ(road.argTokens[8], "0");
    EXPECT_EQ(road.argTokens[12], "3.125");
}

// The waypoint's authored segment model (arg 10) is what the renderer repeats along
// the span (renderer_splines.cpp:193-194), so the row that edits it must be the row
// the loader reads.
TEST(SplineTaskTreeTest, WaypointSegmentModelRowIsTheRenderedSlot) {
    const std::vector<LevelObject> objects =
        LoadQsc(kSplineQsc, "igi_spline_segment.qsc");

    std::vector<const LevelObject*> waypoints;
    for (const auto& o : objects)
        if (o.type == "SplineObjWaypoint") waypoints.push_back(&o);
    ASSERT_EQ(waypoints.size(), 2u);
    EXPECT_EQ(waypoints[0]->segmentModelId, "368_01_1");
    EXPECT_EQ(waypoints[1]->segmentModelId, "322_01_1");

    const TaskSchemaNS::TaskSchema* schema =
        TaskSchemaNS::GetBuiltinSchema("SplineObjWaypoint");
    ASSERT_NE(schema, nullptr);
    int segRow = -1;
    for (int i = 0; i < static_cast<int>(schema->size()); ++i)
        if ((*schema)[i].name == "Segment Model") segRow = i;
    ASSERT_GE(segRow, 0);
    EXPECT_EQ((*schema)[segRow].argOffset, 10);
    EXPECT_EQ(waypoints[1]->argTokens[(*schema)[segRow].argOffset], "\"322_01_1\"");
}
