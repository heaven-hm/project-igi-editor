#include <gtest/gtest.h>
#include "parsers/qsc_lexer.h"
#include <string>

TEST(QscLexerTest, BasicTokens) {
    std::string qsc = "int x = 10;";
    auto lex_res = qsc::Lex(qsc);
    EXPECT_TRUE(lex_res.ok);
    EXPECT_GT(lex_res.tokens.size(), 0);
}
