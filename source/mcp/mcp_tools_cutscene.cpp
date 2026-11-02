#include "mcp_tools_cutscene.h"

#include "mcp_tools_objects.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>

namespace mcp {
namespace {

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
}

bool HasOnlyKeys(const JsonValue& value,
                 std::initializer_list<std::string_view> allowed_keys) {
    if (!value.is_object()) return false;
    for (const auto& [key, ignored] : value.as_object()) {
        bool allowed = false;
        for (const std::string_view candidate : allowed_keys) {
            if (key == candidate) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }
    return true;
}

bool ReadString(const JsonValue& value, std::string& result, std::size_t maximum,
                bool allow_empty = false) {
    if (!value.is_string() || value.as_string().size() > maximum ||
        (!allow_empty && value.as_string().empty())) return false;
    for (const unsigned char character : value.as_string()) {
        if (character < 0x20u && character != '\n' && character != '\t') return false;
    }
    result = value.as_string();
    return true;
}

bool CurrentLevel(GameDataService& service, const JsonValue& arguments,
                  int& level, std::string& error) {
    if (!service.HasOpenLevel()) {
        error = "level_not_open";
        return false;
    }
    level = service.CurrentRevision().level;
    if (arguments.contains("level")) {
        if (!arguments.at("level").is_number() ||
            !std::isfinite(arguments.at("level").as_number()) ||
            std::trunc(arguments.at("level").as_number()) != arguments.at("level").as_number() ||
            arguments.at("level").as_number() != static_cast<double>(level)) {
            error = "invalid_arguments";
            return false;
        }
    }
    return true;
}

JsonValue index_or_null(const JsonValue::Array& values, std::size_t index);

JsonValue CutsceneSnapshot(const JsonValue& object) {
    JsonValue::Object result{
        {"id", object.at("id")},
        {"name", object.at("name")},
        {"parent_id", object.at("parent_id")},
        {"type", object.at("type")},
        {"position", object.at("position")},
        {"rotation_radians", object.at("rotation_radians")},
        {"args", object.at("args")},
    };
    const auto& args = object.at("args").as_array();
    const auto string_at = [&](std::size_t index) -> JsonValue {
        return index < args.size() && args[index].is_string() ? args[index] : JsonValue(nullptr);
    };
    const auto number_at = [&](std::size_t index) -> JsonValue {
        return index < args.size() && args[index].is_number() ? args[index] : JsonValue(nullptr);
    };
    result["timeline"] = JsonValue::Object{
        {"run_expression", string_at(9)},
        {"reset_expression", string_at(10)},
        {"time_delta_expression", string_at(11)},
        {"start_time_seconds", number_at(12)},
        {"initial_run", index_or_null(args, 13)},
        {"time_scale", number_at(14)},
        {"viewport_height_factor", number_at(15)},
        {"fade_in_seconds", number_at(16)},
        {"fade_out_seconds", number_at(17)},
        {"time_of_day", number_at(18)},
        {"start_expression", string_at(19)},
        {"stop_expression", string_at(20)},
    };
    return result;
}

JsonValue index_or_null(const JsonValue::Array& values, std::size_t index) {
    return index < values.size() ? values[index] : JsonValue(nullptr);
}

}  // namespace

ToolDefinitionList CutsceneToolDefinitions() {
    JsonValue::Object list{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{{"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}}}}},
        {"additionalProperties", JsonValue(false)},
    };
    JsonValue::Object get = list;
    get.at("properties").as_object()["task_id"] = JsonValue::Object{{"type", JsonValue("string")}};
    get["required"] = JsonValue::Array{JsonValue("task_id")};
    JsonValue::Object camera = get;
    camera.at("properties").as_object()["position"] = JsonValue::Object{{"type", JsonValue("array")}, {"minItems", JsonValue(3)}, {"maxItems", JsonValue(3)}};
    camera.at("properties").as_object()["rotation_radians"] = JsonValue::Object{{"type", JsonValue("array")}, {"minItems", JsonValue(3)}, {"maxItems", JsonValue(3)}};
    for (const std::string_view key : {"dry_run", "backup"})
        camera.at("properties").as_object()[std::string(key)] = JsonValue::Object{{"type", JsonValue("boolean")}};
    camera.at("properties").as_object()["expected_revision"] = JsonValue::Object{{"type", JsonValue("string")}};
    camera["required"] = JsonValue::Array{JsonValue("task_id")};
    JsonValue::Object dialogue = get;
    dialogue.at("properties").as_object()["text"] = JsonValue::Object{{"type", JsonValue("string")}, {"maxLength", JsonValue(4096)}};
    for (const std::string_view key : {"dry_run", "backup"})
        dialogue.at("properties").as_object()[std::string(key)] = JsonValue::Object{{"type", JsonValue("boolean")}};
    dialogue.at("properties").as_object()["expected_revision"] = JsonValue::Object{{"type", JsonValue("string")}};
    dialogue["required"] = JsonValue::Array{JsonValue("task_id"), JsonValue("text")};
    return ToolDefinitionList{
        {"cutscene_list", JsonValue(std::move(list))},
        {"cutscene_get", JsonValue(std::move(get))},
        {"cutscene_edit_camera", JsonValue(std::move(camera))},
        {"cutscene_set_dialogue", JsonValue(std::move(dialogue))},
    };
}

JsonValue CallCutsceneTool(GameDataService& service, std::string_view name,
                           const JsonValue& arguments, std::string& error) {
    error.clear();
    if (name == "cutscene_list") {
        if (!HasOnlyKeys(arguments, {"level"})) return Failure(error, "invalid_arguments");
        int level = 0;
        if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
        const JsonValue objects = service.ListObjects(level, error);
        if (!error.empty()) return JsonValue(nullptr);
        JsonValue::Array cutscenes;
        for (const auto& object : objects.at("objects").as_array()) {
            if (object.at("type").as_string() == "CutScene") cutscenes.emplace_back(CutsceneSnapshot(object));
        }
        return JsonValue::Object{{"level", level}, {"revision", objects.at("revision")},
                                 {"cutscenes", std::move(cutscenes)}};
    }

    if (name == "cutscene_get") {
        if (!HasOnlyKeys(arguments, {"level", "task_id"}) || !arguments.contains("task_id"))
            return Failure(error, "invalid_arguments");
        std::string task_id;
        if (!ReadString(arguments.at("task_id"), task_id, 128)) return Failure(error, "invalid_arguments");
        int level = 0;
        if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
        const JsonValue object = service.GetObject(level, task_id, error);
        if (!error.empty()) return JsonValue(nullptr);
        if (object.at("type").as_string() != "CutScene") return Failure(error, "unsupported_operation");
        return CutsceneSnapshot(object);
    }

    if (name == "cutscene_edit_camera") {
        if (!HasOnlyKeys(arguments, {"level", "task_id", "position", "rotation_radians",
                                      "dry_run", "backup", "expected_revision"}) ||
            !arguments.contains("task_id") ||
            (!arguments.contains("position") && !arguments.contains("rotation_radians")))
            return Failure(error, "invalid_arguments");
        std::string task_id;
        if (!ReadString(arguments.at("task_id"), task_id, 128)) return Failure(error, "invalid_arguments");
        int level = 0;
        if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
        const JsonValue object = service.GetObject(level, task_id, error);
        if (!error.empty()) return JsonValue(nullptr);
        if (object.at("type").as_string() != "CutScene") return Failure(error, "unsupported_operation");
        JsonValue::Object forwarded{{"task_id", task_id}};
        for (const std::string_view key : {"level", "position", "rotation_radians", "dry_run", "backup", "expected_revision"}) {
            if (arguments.contains(key)) forwarded[std::string(key)] = arguments.at(key);
        }
        JsonValue result = CallObjectTool(service, "object_set_transform", forwarded, error);
        if (!error.empty()) return JsonValue(nullptr);
        result["tool"] = "cutscene_edit_camera";
        result["cutscene"] = CutsceneSnapshot(service.GetObject(level, task_id, error));
        return result;
    }

    if (name == "cutscene_set_dialogue") {
        if (!HasOnlyKeys(arguments, {"level", "task_id", "text", "dry_run", "backup", "expected_revision"}) ||
            !arguments.contains("task_id") || !arguments.contains("text")) return Failure(error, "invalid_arguments");
        std::string task_id;
        std::string text;
        if (!ReadString(arguments.at("task_id"), task_id, 128) ||
            !ReadString(arguments.at("text"), text, 4096, true)) return Failure(error, "invalid_arguments");
        int level = 0;
        if (!CurrentLevel(service, arguments, level, error)) return JsonValue(nullptr);
        const JsonValue object = service.GetObject(level, task_id, error);
        if (!error.empty()) return JsonValue(nullptr);
        if (object.at("type").as_string() != "StatusMessage" ||
            !object.at("args").is_array() || object.at("args").as_array().size() <= 10)
            return Failure(error, "unsupported_operation");
        JsonValue::Object forwarded{{"task_id", task_id}, {"parameter_index", 10}, {"value", text}};
        for (const std::string_view key : {"level", "dry_run", "backup", "expected_revision"}) {
            if (arguments.contains(key)) forwarded[std::string(key)] = arguments.at(key);
        }
        JsonValue result = CallObjectTool(service, "object_set_parameter", forwarded, error);
        if (!error.empty()) return JsonValue(nullptr);
        result["tool"] = "cutscene_set_dialogue";
        result["task_id"] = task_id;
        result["text"] = text;
        return result;
    }

    return Failure(error, "unknown_tool");
}

}  // namespace mcp
