#pragma once
#include <string>
#include <vector>
#include <map>

namespace TaskSchemaNS {

struct FieldDef {
    std::string name;
    std::string typeName;
    int argOffset;
    int argCount;
};

using TaskSchema = std::vector<FieldDef>;

// Returns the number of arguments consumed by a given type name.
int TypeArgCount(const std::string& typeName);

// Parse a Task_DeclareParameters(...) argument list (the comma-separated tokens
// inside the parentheses) into a TaskSchema.
// argTokens should be the raw token list from the QSC line.
TaskSchema ParseDeclareParameters(const std::vector<std::string>& argTokens);

// Returns a pointer to the builtin schema for the given task type, or nullptr
// if no builtin schema exists.
const TaskSchema* GetBuiltinSchema(const std::string& taskType);

// Returns the full builtin schema map (taskType -> TaskSchema).
const std::map<std::string, TaskSchema>& GetBuiltinSchemas();

} // namespace TaskSchemaNS
