#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace mcp {

inline constexpr std::size_t kMaxJsonMessageBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kMaxJsonNestingDepth = 64u;

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

struct JsonError {
    std::string message;
    std::size_t offset = 0;
};

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;

    JsonValue();
    JsonValue(std::nullptr_t);
    JsonValue(bool value);
    JsonValue(double value);
    JsonValue(float value);

    template <typename Integer>
        requires(std::is_integral_v<Integer> &&
                 !std::is_same_v<std::remove_cv_t<Integer>, bool>)
    JsonValue(Integer value) : value_(static_cast<double>(value)) {}

    JsonValue(const char* value);
    JsonValue(std::string value);
    JsonValue(std::string_view value);
    JsonValue(Array value);
    JsonValue(Object value);

    JsonType type() const noexcept;
    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_number() const noexcept;
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    bool as_bool() const;
    double as_number() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    Array& as_array();
    const Object& as_object() const;
    Object& as_object();

    bool contains(std::string_view key) const;
    const JsonValue& at(std::string_view key) const;
    JsonValue& at(std::string_view key);
    JsonValue& operator[](std::string_view key);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;
};

bool JsonParse(std::string_view input, JsonValue& output, JsonError& error);
std::string JsonStringify(const JsonValue& value);

}  // namespace mcp
