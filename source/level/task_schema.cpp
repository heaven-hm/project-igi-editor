#include "task_schema.h"
#include <sstream>
#include <cctype>

namespace TaskSchemaNS {

// ---------------------------------------------------------------------------
// TypeArgCount
// ---------------------------------------------------------------------------
int TypeArgCount(const std::string& typeName) {
    if (typeName == "float" || typeName == "int" || typeName == "bool" ||
        typeName == "string" || typeName == "modelid" || typeName == "taskid" ||
        typeName == "graphid" || typeName == "teamid" || typeName == "weaponid" ||
        typeName == "ammoid" || typeName == "texid") {
        return 1;
    }
    if (typeName == "vector" || typeName == "pos" || typeName == "vec3") {
        return 3;
    }
    if (typeName == "orientation" || typeName == "ori" || typeName == "rot") {
        return 3;
    }
    if (typeName == "matrix3" || typeName == "mat3") {
        return 9;
    }
    // Default: single argument
    return 1;
}

// ---------------------------------------------------------------------------
// ParseDeclareParameters
// Parses the token list from a Task_DeclareParameters(...) call.
// Format: name, type, name, type, ...
// ---------------------------------------------------------------------------
TaskSchema ParseDeclareParameters(const std::vector<std::string>& argTokens) {
    TaskSchema schema;
    int argOffset = 0;
    // Tokens come in pairs: name, type
    for (size_t i = 0; i + 1 < argTokens.size(); i += 2) {
        FieldDef fd;
        fd.name = argTokens[i];
        fd.typeName = argTokens[i + 1];
        fd.argOffset = argOffset;
        fd.argCount = TypeArgCount(fd.typeName);
        schema.push_back(fd);
        argOffset += fd.argCount;
    }
    return schema;
}

// ---------------------------------------------------------------------------
// Builtin schemas
// ---------------------------------------------------------------------------
static std::map<std::string, TaskSchema> BuildBuiltinSchemas() {
    std::map<std::string, TaskSchema> m;

    // Helper lambda
    auto makeSchema = [](std::initializer_list<std::pair<std::string,std::string>> fields) {
        TaskSchema s;
        int offset = 0;
        for (auto& [name, type] : fields) {
            FieldDef fd;
            fd.name = name;
            fd.typeName = type;
            fd.argOffset = offset;
            fd.argCount = TypeArgCount(type);
            s.push_back(fd);
            offset += fd.argCount;
        }
        return s;
    };

    // Building
    m["Building"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
    });

    // EditRigidObj / Static
    m["Static"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
    });

    m["EditRigidObj"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
    });

    // HumanSoldier / HumanAI
    m["HumanSoldier"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
        {"team",     "teamid"},
    });

    m["HumanAI"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
        {"team",     "teamid"},
    });

    // Container / Game
    m["Container"] = makeSchema({
        {"taskid",   "taskid"},
        {"name",     "string"},
    });

    m["Game"] = makeSchema({
        {"taskid",   "taskid"},
        {"name",     "string"},
    });

    // Door
    m["Door"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
    });

    // Terminal
    m["Terminal"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
        {"pos",      "vector"},
        {"rot",      "orientation"},
    });

    // Spline
    m["Spline"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
    });

    // Wire
    m["Wire"] = makeSchema({
        {"taskid",   "taskid"},
        {"modelid",  "modelid"},
        {"name",     "string"},
    });

    return m;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------
const std::map<std::string, TaskSchema>& GetBuiltinSchemas() {
    static std::map<std::string, TaskSchema> s_schemas = BuildBuiltinSchemas();
    return s_schemas;
}

const TaskSchema* GetBuiltinSchema(const std::string& taskType) {
    const auto& m = GetBuiltinSchemas();
    auto it = m.find(taskType);
    if (it == m.end()) return nullptr;
    return &it->second;
}

} // namespace TaskSchemaNS
