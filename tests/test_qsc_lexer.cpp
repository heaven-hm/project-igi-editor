#include <gtest/gtest.h>
#include "../source/level/qsc_lexer.h"

using namespace qsc;

// ============================================================
//  QSC Lexer — full suite
// ============================================================

// ---------- empty / whitespace ----------

TEST(QscLexerTest, EmptyInput) {
    auto r = Lex("");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::End);
}

TEST(QscLexerTest, WhitespaceOnly) {
    auto r = Lex("   \t\n\r  ");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::End);
}

// ---------- identifiers & keywords ----------

TEST(QscLexerTest, SimpleIdentifier) {
    auto r = Lex("myIdent");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "myIdent");
}

TEST(QscLexerTest, IdentifierWithUnderscores) {
    auto r = Lex("Task_New_42");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "Task_New_42");
}

TEST(QscLexerTest, KeywordTrue) {
    auto r = Lex("TRUE");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::KwTrue);
    EXPECT_EQ(r.tokens[0].lexeme, "TRUE");
}

TEST(QscLexerTest, KeywordFalse) {
    auto r = Lex("FALSE");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::KwFalse);
}

TEST(QscLexerTest, KeywordIf) {
    auto r = Lex("if");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::KwIf);
}

TEST(QscLexerTest, KeywordElse) {
    auto r = Lex("else");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::KwElse);
}

TEST(QscLexerTest, KeywordWhile) {
    auto r = Lex("while");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::KwWhile);
}

TEST(QscLexerTest, AllKeywordsInSequence) {
    auto r = Lex("TRUE FALSE if else while");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 5u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::KwTrue);
    EXPECT_EQ(r.tokens[1].kind, TokKind::KwFalse);
    EXPECT_EQ(r.tokens[2].kind, TokKind::KwIf);
    EXPECT_EQ(r.tokens[3].kind, TokKind::KwElse);
    EXPECT_EQ(r.tokens[4].kind, TokKind::KwWhile);
}

// "true" / "false" (lowercase) are NOT keywords — they are plain identifiers
TEST(QscLexerTest, LowercaseTrueIsIdent) {
    auto r = Lex("true");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
}

// ---------- integer literals ----------

TEST(QscLexerTest, IntegerLiteralBasic) {
    auto r = Lex("42");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::IntLit);
    EXPECT_EQ(r.tokens[0].int_val, 42);
    EXPECT_EQ(r.tokens[0].lexeme, "42");
}

TEST(QscLexerTest, IntegerLiteralZero) {
    auto r = Lex("0");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::IntLit);
    EXPECT_EQ(r.tokens[0].int_val, 0);
}

TEST(QscLexerTest, IntegerLiteralLarge) {
    auto r = Lex("9900");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::IntLit);
    EXPECT_EQ(r.tokens[0].int_val, 9900);
}

// ---------- hex literals ----------

TEST(QscLexerTest, HexLiteralLowerPrefix) {
    auto r = Lex("0xff");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::HexLit);
    EXPECT_EQ(r.tokens[0].int_val, 255);
}

TEST(QscLexerTest, HexLiteralUpperPrefix) {
    auto r = Lex("0XFF");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::HexLit);
    EXPECT_EQ(r.tokens[0].int_val, 255);
}

TEST(QscLexerTest, HexLiteralMixedCase) {
    auto r = Lex("0xDEAD");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::HexLit);
    EXPECT_EQ(r.tokens[0].int_val, 0xDEAD);
}

TEST(QscLexerTest, HexLiteralZero) {
    auto r = Lex("0x0");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::HexLit);
    EXPECT_EQ(r.tokens[0].int_val, 0);
}

// ---------- float literals ----------

TEST(QscLexerTest, FloatLiteralBasic) {
    auto r = Lex("3.14");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::FloatLit);
    EXPECT_NEAR(r.tokens[0].float_val, 3.14f, 1e-5f);
}

TEST(QscLexerTest, FloatLiteralExponent) {
    auto r = Lex("1.5e3");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::FloatLit);
    EXPECT_NEAR(r.tokens[0].float_val, 1500.0f, 0.1f);
}

TEST(QscLexerTest, FloatLiteralNegativeExponent) {
    auto r = Lex("2.5e-3");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::FloatLit);
    EXPECT_NEAR(r.tokens[0].float_val, 0.0025f, 1e-6f);
}

TEST(QscLexerTest, FloatLiteralPositiveExponent) {
    auto r = Lex("1.0e+2");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::FloatLit);
    EXPECT_NEAR(r.tokens[0].float_val, 100.0f, 0.001f);
}

TEST(QscLexerTest, FloatLiteralGameUnit) {
    // 3.125 is a common world-unit value in IGI QSC files
    auto r = Lex("3.125");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::FloatLit);
    EXPECT_NEAR(r.tokens[0].float_val, 3.125f, 1e-6f);
}

// ---------- string literals ----------

TEST(QscLexerTest, StringLiteralEmpty) {
    auto r = Lex("\"\"");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::StringLit);
    EXPECT_EQ(r.tokens[0].lexeme, "");
}

TEST(QscLexerTest, StringLiteralSimple) {
    auto r = Lex("\"hello world\"");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::StringLit);
    EXPECT_EQ(r.tokens[0].lexeme, "hello world");
}

TEST(QscLexerTest, StringLiteralEscapeNewline) {
    auto r = Lex("\"line1\\nline2\"");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].lexeme, "line1\nline2");
}

TEST(QscLexerTest, StringLiteralEscapeTab) {
    auto r = Lex("\"a\\tb\"");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.tokens[0].lexeme, "a\tb");
}

TEST(QscLexerTest, StringLiteralEscapeQuote) {
    auto r = Lex("\"say \\\"hi\\\"\"");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.tokens[0].lexeme, "say \"hi\"");
}

TEST(QscLexerTest, StringLiteralEscapeBackslash) {
    auto r = Lex("\"a\\\\b\"");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.tokens[0].lexeme, "a\\b");
}

TEST(QscLexerTest, StringLiteralAllEscapes) {
    auto r = Lex("\"\\n\\t\\r\\\"\\\\\"");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::StringLit);
    EXPECT_EQ(r.tokens[0].lexeme, "\n\t\r\"\\");
}

// ---------- comments ----------

TEST(QscLexerTest, LineCommentStripped) {
    auto r = Lex("// this is a comment\nfoo");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "foo");
}

TEST(QscLexerTest, LineCommentAtEndOfLine) {
    auto r = Lex("bar // trailing comment");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "bar");
    // Only 2 tokens: Ident + End
    EXPECT_EQ(r.tokens[1].kind, TokKind::End);
}

TEST(QscLexerTest, BlockCommentInline) {
    auto r = Lex("/* block */ baz");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "baz");
}

TEST(QscLexerTest, BlockCommentMultiline) {
    auto r = Lex("/* line1\n   line2\n*/qux");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "qux");
}

TEST(QscLexerTest, BlockCommentBetweenTokens) {
    auto r = Lex("A /* ignored */ B");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 2u);
    EXPECT_EQ(r.tokens[0].lexeme, "A");
    EXPECT_EQ(r.tokens[1].lexeme, "B");
}

// ---------- operators ----------

TEST(QscLexerTest, TwoCharOperators) {
    struct { const char* src; const char* expected; } cases[] = {
        {"<<", "<<"}, {">>", ">>"}, {"&&", "&&"}, {"||", "||"},
        {"==", "=="}, {"!=", "!="}, {"<=", "<="}, {">=", ">="},
    };
    for (auto& c : cases) {
        auto r = Lex(c.src);
        EXPECT_TRUE(r.ok) << "failed for: " << c.src;
        ASSERT_GE(r.tokens.size(), 1u) << "failed for: " << c.src;
        EXPECT_EQ(r.tokens[0].kind, TokKind::Op) << "failed for: " << c.src;
        EXPECT_EQ(r.tokens[0].lexeme, c.expected) << "failed for: " << c.src;
    }
}

TEST(QscLexerTest, SingleCharOperators) {
    const char* ops[] = {"+", "-", "*", "/", "&", "|", "^", "<", ">", "=", "~", "!"};
    for (const char* op : ops) {
        auto r = Lex(op);
        EXPECT_TRUE(r.ok) << "failed for: " << op;
        ASSERT_GE(r.tokens.size(), 1u) << "failed for: " << op;
        EXPECT_EQ(r.tokens[0].kind, TokKind::Op) << "failed for: " << op;
        EXPECT_EQ(r.tokens[0].lexeme, op) << "failed for: " << op;
    }
}

// ---------- punctuation ----------

TEST(QscLexerTest, AllPunctuation) {
    auto r = Lex("(){},;");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 6u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::LParen);
    EXPECT_EQ(r.tokens[1].kind, TokKind::RParen);
    EXPECT_EQ(r.tokens[2].kind, TokKind::LBrace);
    EXPECT_EQ(r.tokens[3].kind, TokKind::RBrace);
    EXPECT_EQ(r.tokens[4].kind, TokKind::Comma);
    EXPECT_EQ(r.tokens[5].kind, TokKind::Semicolon);
}

// ---------- qualified identifiers ----------

TEST(QscLexerTest, QualifiedIdentifierWithDot) {
    // "AnimTask_1.isRun" — the dot between two ident chars is part of the token
    auto r = Lex("AnimTask_1.isRun");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "AnimTask_1.isRun");
}

TEST(QscLexerTest, QualifiedIdentifierMultipleDots) {
    auto r = Lex("a.b.c");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "a.b.c");
}

// ---------- token positions ----------

TEST(QscLexerTest, TokenPositionFirstToken) {
    auto r = Lex("foo");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].line, 1u);
    EXPECT_EQ(r.tokens[0].col, 1u);
}

TEST(QscLexerTest, TokenPositionSecondLine) {
    auto r = Lex("foo\nbar");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 2u);
    EXPECT_EQ(r.tokens[0].line, 1u);
    EXPECT_EQ(r.tokens[1].line, 2u);
    EXPECT_EQ(r.tokens[1].col, 1u);
}

TEST(QscLexerTest, TokenPositionAfterSpaces) {
    auto r = Lex("   x");
    EXPECT_TRUE(r.ok);
    ASSERT_GE(r.tokens.size(), 1u);
    EXPECT_EQ(r.tokens[0].line, 1u);
    EXPECT_EQ(r.tokens[0].col, 4u);
}

// ---------- realistic QSC snippets ----------

TEST(QscLexerTest, RealWorldTaskCall) {
    auto r = Lex("Task_New(9900, \"SplineObj\", \"TestBridgeTrack\", FALSE, FALSE, FALSE, FALSE, 20, 0, 0, 0, 0, 3.125, 1, 1, 1, 0, 0, 0);");
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.tokens.empty());
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);
    EXPECT_EQ(r.tokens[0].lexeme, "Task_New");
    // The last real token before End is Semicolon
    EXPECT_EQ(r.tokens.back().kind, TokKind::End);
}

TEST(QscLexerTest, NestedCallTokenSequence) {
    auto r = Lex("Outer(Inner(42))");
    EXPECT_TRUE(r.ok);
    // Outer ( Inner ( 42 ) ) End  = 8 tokens
    ASSERT_EQ(r.tokens.size(), 8u);
    EXPECT_EQ(r.tokens[0].kind, TokKind::Ident);   // Outer
    EXPECT_EQ(r.tokens[1].kind, TokKind::LParen);
    EXPECT_EQ(r.tokens[2].kind, TokKind::Ident);   // Inner
    EXPECT_EQ(r.tokens[3].kind, TokKind::LParen);
    EXPECT_EQ(r.tokens[4].kind, TokKind::IntLit);
    EXPECT_EQ(r.tokens[5].kind, TokKind::RParen);
    EXPECT_EQ(r.tokens[6].kind, TokKind::RParen);
    EXPECT_EQ(r.tokens[7].kind, TokKind::End);
}

// ---------- TokKindName ----------

TEST(QscLexerTest, TokKindNamesAllDefined) {
    TokKind kinds[] = {
        TokKind::End, TokKind::Ident, TokKind::KwTrue, TokKind::KwFalse,
        TokKind::KwIf, TokKind::KwElse, TokKind::KwWhile,
        TokKind::IntLit, TokKind::HexLit, TokKind::FloatLit, TokKind::StringLit,
        TokKind::LParen, TokKind::RParen, TokKind::LBrace, TokKind::RBrace,
        TokKind::Comma, TokKind::Semicolon, TokKind::Op,
    };
    for (auto k : kinds) {
        const char* name = TokKindName(k);
        ASSERT_NE(name, nullptr);
        EXPECT_STRNE(name, "?");
    }
}

// ---------- error cases ----------

TEST(QscLexerTest, ErrorUnterminatedString) {
    auto r = Lex("\"unclosed");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(QscLexerTest, ErrorMalformedHexNoDigits) {
    auto r = Lex("0x");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(QscLexerTest, ErrorMalformedFloatExponentNoDigits) {
    auto r = Lex("1e");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(QscLexerTest, ErrorUnexpectedCharAt) {
    auto r = Lex("@");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(QscLexerTest, ErrorUnexpectedCharHash) {
    auto r = Lex("#define X 1");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(QscLexerTest, ErrorMessageContainsPosition) {
    auto r = Lex("@");
    EXPECT_FALSE(r.ok);
    // Error message should mention line/col
    EXPECT_NE(r.error.find("1:1"), std::string::npos);
}
