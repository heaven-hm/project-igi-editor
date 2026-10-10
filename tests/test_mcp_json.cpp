#include "mcp/mcp_json.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

using mcp::JsonError;
using mcp::JsonValue;

TEST(McpJsonTest, RoundTripsScalarsArraysAndObjects) {
    const std::string input =
        R"({"active":true,"items":[null,-12,3.5,"text"],"name":"IGI"})";

    JsonValue value;
    JsonError error;
    ASSERT_TRUE(mcp::JsonParse(input, value, error)) << error.message;
    ASSERT_TRUE(value.is_object());
    EXPECT_TRUE(value.at("active").as_bool());
    EXPECT_EQ(value.at("items").as_array().size(), 4u);
    EXPECT_DOUBLE_EQ(value.at("items").as_array()[1].as_number(), -12.0);
    EXPECT_EQ(value.at("name").as_string(), "IGI");

    EXPECT_EQ(mcp::JsonStringify(value), input);
}

TEST(McpJsonTest, DecodesAndEscapesStrings) {
    JsonValue value;
    JsonError error;
    ASSERT_TRUE(mcp::JsonParse("\"quote \\\" slash \\\\ snowman \\u2603\"", value, error))
        << error.message;
    EXPECT_EQ(value.as_string(), "quote \" slash \\ snowman \xE2\x98\x83");
    EXPECT_EQ(mcp::JsonStringify(value), "\"quote \\\" slash \\\\ snowman \xE2\x98\x83\"");
}

TEST(McpJsonTest, RejectsMalformedUtf8AndEscapes) {
    JsonValue value;
    JsonError error;

    const std::string malformed_utf8("\xC3\x28", 2);
    EXPECT_FALSE(mcp::JsonParse("\"" + malformed_utf8 + "\"", value, error));
    EXPECT_FALSE(mcp::JsonParse("\"\\uD800\"", value, error));
    EXPECT_FALSE(mcp::JsonParse("\"\\x20\"", value, error));
    EXPECT_FALSE(mcp::JsonParse("\"line\nfeed\"", value, error));
}

TEST(McpJsonTest, RejectsDuplicateKeysAndTrailingValues) {
    JsonValue value;
    JsonError error;
    EXPECT_FALSE(mcp::JsonParse(R"({"a":1,"a":2})", value, error));
    EXPECT_FALSE(mcp::JsonParse("true false", value, error));
    EXPECT_FALSE(mcp::JsonParse("null{}", value, error));
}

TEST(McpJsonTest, EnforcesMessageAndNestingLimits) {
    JsonValue value;
    JsonError error;

    const std::string oversized(mcp::kMaxJsonMessageBytes + 1, ' ');
    EXPECT_FALSE(mcp::JsonParse(oversized, value, error));

    std::string nested;
    for (std::size_t i = 0; i < mcp::kMaxJsonNestingDepth + 1; ++i) {
        nested.push_back('[');
    }
    nested += "0";
    for (std::size_t i = 0; i < mcp::kMaxJsonNestingDepth + 1; ++i) {
        nested.push_back(']');
    }
    EXPECT_FALSE(mcp::JsonParse(nested, value, error));
}

TEST(McpJsonTest, RejectsNonFiniteNumbers) {
    JsonValue value;
    JsonError error;
    EXPECT_FALSE(mcp::JsonParse("1e400", value, error));
    EXPECT_FALSE(mcp::JsonParse("NaN", value, error));
    EXPECT_FALSE(mcp::JsonParse("Infinity", value, error));
    EXPECT_THROW((JsonValue(std::numeric_limits<double>::infinity())), std::invalid_argument);
    EXPECT_THROW((JsonValue(std::numeric_limits<double>::quiet_NaN())), std::invalid_argument);
}

TEST(McpJsonTest, SerializesObjectsInDeterministicKeyOrder) {
    JsonValue::Object object;
    object.emplace("z", JsonValue(1));
    object.emplace("a", JsonValue(2));
    object.emplace("escaped\nkey", JsonValue("line\nfeed"));

    const JsonValue value(std::move(object));
    EXPECT_EQ(mcp::JsonStringify(value),
              R"({"a":2,"escaped\nkey":"line\nfeed","z":1})");
}

TEST(McpJsonTest, BoundsProgrammaticOutputAndRejectsInvalidKeys) {
    JsonValue::Object invalid;
    invalid.emplace(std::string("bad\xFF", 4), JsonValue(1));
    EXPECT_THROW(mcp::JsonStringify(JsonValue(std::move(invalid))), std::invalid_argument);

    JsonValue::Array nested{JsonValue(0)};
    for (std::size_t i = 0; i < mcp::kMaxJsonNestingDepth + 1; ++i)
        nested = JsonValue::Array{JsonValue(std::move(nested))};
    EXPECT_THROW(mcp::JsonStringify(JsonValue(std::move(nested))), std::invalid_argument);

    JsonValue::Object oversized;
    oversized.emplace("payload", JsonValue(std::string(mcp::kMaxJsonMessageBytes, 'x')));
    EXPECT_THROW(mcp::JsonStringify(JsonValue(std::move(oversized))), std::invalid_argument);

    JsonValue::Array many_numbers;
    many_numbers.reserve(1100000);
    for (int i = 0; i < 1100000; ++i) many_numbers.emplace_back(1234567);
    EXPECT_THROW(mcp::JsonStringify(JsonValue(std::move(many_numbers))), std::invalid_argument);
}

}  // namespace
