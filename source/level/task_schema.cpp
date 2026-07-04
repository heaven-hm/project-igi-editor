#include "level/task_schema.h"

namespace TaskSchemaNS {

int TypeArgCount(const std::string& t) {
    if (t == "ObjectPos" || t == "Real32x9" || t == "RGB" ||
        t == "Real32x3"  || t == "Colour")   return 3;
    return 1;   // String16, VarString, Real32, Int32, Bool, …
}

const std::map<std::string, TaskSchema>& GetBuiltinSchemas() {
    static const std::map<std::string, TaskSchema> schemas = [] {
        std::map<std::string, TaskSchema> s;

        auto add = [](TaskSchema& sc, const char* name, const char* type, int off) {
            FieldDef f; f.name = name; f.typeName = type;
            f.argOffset = off; f.argCount = TypeArgCount(type);
            sc.push_back(f);
        };

        // Building / EditRigidObj / Terminal: pos@3, ori@6, model@9
        for (const char* t : {"Building","EditRigidObj","Terminal"}) {
            add(s[t], "Position",    "ObjectPos", 3);
            add(s[t], "Orientation", "Real32x9",  6);
            add(s[t], "Model",       "String16",  9);
        }
        // HumanSoldier types: pos@3, gamma(1 float)@6, model@7, then AI params 8..10.
        // Confirmed against real objects.qsc data: arg9 is a small index that always
        // matches an existing common/ANIMS/<NNN>.IFF file (Bone Hierarchy); arg10 is an
        // arbitrary animation_id within that file (Stand Animation), or -1 for none.
        for (const char* t : {"HumanSoldier","HumanSoldierFemale","HumanPlayer"}) {
            add(s[t], "Position",        "ObjectPos", 3);
            add(s[t], "Gamma",           "Real32",    6);
            add(s[t], "Model",           "String16",  7);
            add(s[t], "Team",            "Int32",     8);
            add(s[t], "Bone Heirachy",   "Int32",     9);
            add(s[t], "Stand Animation", "Int32",     10);
        }
        // Door: pos@3, (3 unknown physics args @6), ori@9, model@12
        { auto& sc = s["Door"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  9);
          add(sc, "Model",       "String16",  12); }
        // Car / Heli: pos@3, (2 unknowns @6), gamma@8, (4 unknowns @9), model@13
        for (const char* t : {"Car","Heli"}) {
            add(s[t], "Position", "ObjectPos", 3);
            add(s[t], "Heading",  "Real32",    8);
            add(s[t], "Model",    "String16",  13);
        }
        // ExplodeObject: pos@3, ori@6, model@9, destroyed_model@10, ...
        { auto& sc = s["ExplodeObject"];
          add(sc, "Position",        "ObjectPos", 3);
          add(sc, "Orientation",     "Real32x9",  6);
          add(sc, "Model",           "String16",  9);
          add(sc, "Destroyed model", "String16",  10); }
        // Switch: pos@3, ori@6, count@9, reverse@10, models@11..15, loop@16
        { auto& sc = s["Switch"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  11); }
        // Wire: start pos@3, end pos@6, model@9
        { auto& sc = s["Wire"];
          add(sc, "Position", "ObjectPos", 3);
          add(sc, "Model",    "String16",  9); }
        // AlarmControl / SCameraControl: pos@3, ori@6, model@9 (usually "waypoint")
        for (const char* t : {"AlarmControl","SCameraControl"}) {
            add(s[t], "Position",    "ObjectPos", 3);
            add(s[t], "Orientation", "Real32x9",  6);
            add(s[t], "Model",       "String16",  9);
        }
        // SCamera: pos@3, ori@6, unknown@9, model@10
        { auto& sc = s["SCamera"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  10); }
        // SplineObjWaypoint: ori first@3, pos@6, model@9
        { auto& sc = s["SplineObjWaypoint"];
          add(sc, "Orientation", "Real32x9",  3);
          add(sc, "Position",    "ObjectPos", 6);
          add(sc, "Model",       "String16",  9); }
        // AmbientArea
        { auto& sc = s["AmbientArea"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6); }
        // Train: 1D rail position@3 (Real32), RailroadQTaskID@5, Model@6
        // Position is not a world XYZ but a 1D distance along the spline.
        // We use the special type "TrainPos1D" so the parser knows to set posIsRail=true.
        { auto& sc = s["Train"];
          add(sc, "Position", "TrainPos1D", 3);
          add(sc, "Model",    "String256",  6); }
        // Fence: pos@3, heading@6, model@7
        { auto& sc = s["Fence"];
          add(sc, "Position", "ObjectPos", 3);
          add(sc, "Heading",  "Real32",    6);
          add(sc, "Model",    "String16",  7); }
        // Cabinet: same layout as HumanSoldier
        { auto& sc = s["Cabinet"];
          add(sc, "Position", "ObjectPos", 3);
          add(sc, "Heading",  "Real32",    6);
          add(sc, "Model",    "String16",  7); }
        // Generic positioned objects: pos@3, ori@6, model scanned from arg 9+
        for (const char* t : {"AIStationaryGunHolder","AlarmLight","Elevator","Generator",
                               "GenericPickup","GenericTBA","Plane","Radio",
                               "RotatingObject","Siren","StationaryGun", "GunPickup"}) {
            add(s[t], "Position",    "ObjectPos", 3);
            add(s[t], "Orientation", "Real32x9",  6);
            add(s[t], "Model",       "String16",  9);
        }
        // AmmoPickup: pos@3, ori@6, model@9, ammo@10
        { auto& sc = s["AmmoPickup"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  9);
          add(sc, "Ammo",        "Int16",     10); }

        return s;
    }();
    return schemas;
}

const TaskSchema* GetBuiltinSchema(const std::string& taskType) {
    const auto& m = GetBuiltinSchemas();
    auto it = m.find(taskType);
    return it == m.end() ? nullptr : &it->second;
}

// --- Runtime schemas from Task_DeclareParameters ----------------------------

static std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

TaskSchema ParseDeclaration(const std::vector<std::string>& declArgs) {
    TaskSchema schema;
    if (declArgs.size() < 3) return schema; // need TypeName + at least one (name,type)
    int off = 3; // id, type, note occupy 0..2; first declared field starts at 3
    for (size_t i = 1; i + 1 < declArgs.size(); i += 2) {
        FieldDef f;
        f.name      = Unquote(declArgs[i]);
        f.typeName  = Unquote(declArgs[i + 1]);
        f.argCount  = TypeArgCount(f.typeName);
        f.argOffset = off;
        off += f.argCount;
        schema.push_back(f);
    }
    return schema;
}

static std::map<std::string, TaskSchema>& RegisteredSchemas() {
    static std::map<std::string, TaskSchema> s;
    return s;
}

void RegisterSchema(const std::string& taskType, TaskSchema schema) {
    if (taskType.empty() || schema.empty()) return;
    RegisteredSchemas()[taskType] = std::move(schema);
}

void ClearRegisteredSchemas() {
    RegisteredSchemas().clear();
}

const TaskSchema* GetSchema(const std::string& taskType) {
    const auto& reg = RegisteredSchemas();
    auto it = reg.find(taskType);
    if (it != reg.end()) return &it->second;
    return GetBuiltinSchema(taskType);
}

} // namespace TaskSchemaNS
