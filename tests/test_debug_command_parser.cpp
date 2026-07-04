#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sstream>

struct DebugCommandMock {
    std::string type;
    int level = -1;
    std::string modelId;
};

DebugCommandMock ParseCommandStr(const std::string& line) {
    DebugCommandMock cmd;
    std::istringstream iss(line);
    std::string token;
    if (iss >> token) {
        if (token == "goto" || token == "capture-model") {
            cmd.type = token;
            while (iss >> token) {
                if (token.find("level=") == 0) {
                    cmd.level = std::stoi(token.substr(6));
                } else if (token.find("model=") == 0) {
                    cmd.modelId = token.substr(6);
                }
            }
        }
    }
    return cmd;
}

TEST(DebugCommandParserTest, ParseGotoCommand) {
    auto cmd = ParseCommandStr("goto level=5 model=123_45_6");
    EXPECT_EQ(cmd.type, "goto");
    EXPECT_EQ(cmd.level, 5);
    EXPECT_EQ(cmd.modelId, "123_45_6");
}

TEST(DebugCommandParserTest, ParseCaptureCommand) {
    auto cmd = ParseCommandStr("capture-model level=10 model=999_12_1");
    EXPECT_EQ(cmd.type, "capture-model");
    EXPECT_EQ(cmd.level, 10);
    EXPECT_EQ(cmd.modelId, "999_12_1");
}

TEST(DebugCommandParserTest, ParseInvalidCommand) {
    auto cmd = ParseCommandStr("invalid_cmd level=1 model=test");
    EXPECT_EQ(cmd.type, "");
}
