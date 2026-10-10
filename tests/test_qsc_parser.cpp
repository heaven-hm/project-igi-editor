#include <gtest/gtest.h>
#include "../source/level/qsc_lexer.h"
#include "../source/level/qsc_parser.h"
#include <sstream>

using namespace qsc;

// ============================================================
//  QSC Parser — full suite
// ============================================================

// --- Helper: lex + parse in one step ---
static ParseResult LexAndParse(const std::string& src) {
    auto lr = Lex(src);
    if (!lr.ok) {
        ParseResult r;
        r.ok = false;
        r.error = "lex failed: " + lr.error;
        return r;
    }
    return Parse(lr.tokens);
}

// --- Helper: navigate to the call node of the first top-level ExprStmt ---
static const Node* FirstCallNode(const ParseResult& r) {
    if (!r.ok || !r.program || r.program->children.empty()) return nullptr;
    const Node& stmt = *r.program->children[0];
    if (stmt.kind != NodeKind::ExprStmt || stmt.children.empty()) return nullptr;
    const Node& expr = *stmt.children[0];
    if (expr.kind != NodeKind::Call) return nullptr;
    return &expr;
}

// ============================================================
//  Empty / minimal programs
// ============================================================

TEST(QscParserTest, EmptyProgram) {
    auto r = LexAndParse("");
    EXPECT_TRUE(r.ok);
    ASSERT_NE(r.program, nullptr);
    EXPECT_EQ(r.program->kind, NodeKind::Program);
    EXPECT_EQ(r.program->children.size(), 0u);
}

TEST(QscParserTest, EmptyStatement) {
    // A bare semicolon is parsed as an empty Block
    auto r = LexAndParse(";");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    EXPECT_EQ(r.program->children[0]->kind, NodeKind::Block);
}

TEST(QscParserTest, EmptyBlock) {
    auto r = LexAndParse("{}");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    auto& blk = *r.program->children[0];
    EXPECT_EQ(blk.kind, NodeKind::Block);
    EXPECT_EQ(blk.children.size(), 0u);
}

// ============================================================
//  Function calls
// ============================================================

TEST(QscParserTest, SimpleCallNoArgs) {
    auto r = LexAndParse("Foo();");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    auto& stmt = *r.program->children[0];
    EXPECT_EQ(stmt.kind, NodeKind::ExprStmt);
    auto& call = *stmt.children[0];
    EXPECT_EQ(call.kind, NodeKind::Call);
    EXPECT_EQ(call.s_val, "Foo");
    EXPECT_EQ(call.children.size(), 0u);
}

TEST(QscParserTest, CallWithIntArg) {
    auto r = LexAndParse("Bar(42);");
    EXPECT_TRUE(r.ok);
    const Node* call = FirstCallNode(r);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->children.size(), 1u);
    EXPECT_EQ(call->children[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(call->children[0]->i_val, 42);
}

TEST(QscParserTest, CallWithHexArg) {
    auto r = LexAndParse("SetFlags(0xDEAD);");
    EXPECT_TRUE(r.ok);
    const Node* call = FirstCallNode(r);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->children.size(), 1u);
    EXPECT_EQ(call->children[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(call->children[0]->i_val, 0xDEAD);
}

TEST(QscParserTest, CallWithFloatArg) {
    auto r = LexAndParse("Move(3.125);");
    EXPECT_TRUE(r.ok);
    const Node* call = FirstCallNode(r);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->children.size(), 1u);
    EXPECT_EQ(call->children[0]->kind, NodeKind::FloatLit);
    EXPECT_NEAR(call->children[0]->f_val, 3.125f, 1e-6f);
}

TEST(QscParserTest, CallWithStringArg) {
    auto r = LexAndParse("SetName(\"player\");");
    EXPECT_TRUE(r.ok);
    const Node* call = FirstCallNode(r);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->children.size(), 1u);
    EXPECT_EQ(call->children[0]->kind, NodeKind::StringLit);
    EXPECT_EQ(call->children[0]->s_val, "player");
}

TEST(QscParserTest, CallWithBoolArgs) {
    auto r = LexAndParse("Init(TRUE, FALSE);");
    EXPECT_TRUE(r.ok);
    const Node* call = FirstCallNode(r);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->children.size(), 2u);
    EXPECT_EQ(call->children[0]->kind, NodeKind::BoolLit);
    EXPECT_TRUE(call->children[0]->b_val);
    EXPECT_EQ(call->children[1]->kind, NodeKind::BoolLit);
    EXPECT_FALSE(call->children[1]->b_val);
}

TEST(QscParserTest, CallWithMixedArgs) {
    auto r = LexAndParse("Task_New(1, 2.5, \"hello\", TRUE, FALSE);");
    EXPECT_TRUE(r.ok);
    const Node* call = FirstCallNode(r);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->children.size(), 5u);
    EXPECT_EQ(call->children[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(call->children[1]->kind, NodeKind::FloatLit);
    EXPECT_EQ(call->children[2]->kind, NodeKind::StringLit);
    EXPECT_EQ(call->children[3]->kind, NodeKind::BoolLit);
    EXPECT_EQ(call->children[4]->kind, NodeKind::BoolLit);
}

TEST(QscParserTest, NestedCalls) {
    auto r = LexAndParse("Outer(Inner(99));");
    EXPECT_TRUE(r.ok);
    const Node* outer = FirstCallNode(r);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->s_val, "Outer");
    ASSERT_EQ(outer->children.size(), 1u);
    EXPECT_EQ(outer->children[0]->kind, NodeKind::Call);
    EXPECT_EQ(outer->children[0]->s_val, "Inner");
    ASSERT_EQ(outer->children[0]->children.size(), 1u);
    EXPECT_EQ(outer->children[0]->children[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(outer->children[0]->children[0]->i_val, 99);
}

TEST(QscParserTest, MultipleTopLevelStatements) {
    auto r = LexAndParse("Foo(); Bar(); Baz();");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.program->children.size(), 3u);
}

// ============================================================
//  Control flow
// ============================================================

TEST(QscParserTest, IfNoElse) {
    auto r = LexAndParse("if (TRUE) { Foo(); }");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    auto& if_node = *r.program->children[0];
    EXPECT_EQ(if_node.kind, NodeKind::If);
    EXPECT_EQ(if_node.children.size(), 2u);  // cond + then-branch
    EXPECT_EQ(if_node.children[0]->kind, NodeKind::BoolLit);
    EXPECT_TRUE(if_node.children[0]->b_val);
    EXPECT_EQ(if_node.children[1]->kind, NodeKind::Block);
}

TEST(QscParserTest, IfWithElse) {
    auto r = LexAndParse("if (FALSE) { Foo(); } else { Bar(); }");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    auto& if_node = *r.program->children[0];
    EXPECT_EQ(if_node.kind, NodeKind::If);
    EXPECT_EQ(if_node.children.size(), 3u);  // cond + then + else
    EXPECT_EQ(if_node.children[0]->kind, NodeKind::BoolLit);
    EXPECT_FALSE(if_node.children[0]->b_val);
}

TEST(QscParserTest, WhileLoop) {
    auto r = LexAndParse("while (TRUE) { Tick(); }");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    auto& while_node = *r.program->children[0];
    EXPECT_EQ(while_node.kind, NodeKind::While);
    EXPECT_EQ(while_node.children.size(), 2u);  // cond + body
    EXPECT_EQ(while_node.children[0]->kind, NodeKind::BoolLit);
    EXPECT_EQ(while_node.children[1]->kind, NodeKind::Block);
}

TEST(QscParserTest, NestedBlocks) {
    auto r = LexAndParse("{ { Foo(); } }");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.program->children.size(), 1u);
    auto& outer = *r.program->children[0];
    EXPECT_EQ(outer.kind, NodeKind::Block);
    ASSERT_EQ(outer.children.size(), 1u);
    EXPECT_EQ(outer.children[0]->kind, NodeKind::Block);
}

// ============================================================
//  Expressions
// ============================================================

TEST(QscParserTest, BinaryAdd) {
    auto r = LexAndParse("a + b;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Binary);
    EXPECT_EQ(expr.s_val, "+");
    ASSERT_EQ(expr.children.size(), 2u);
    EXPECT_EQ(expr.children[0]->kind, NodeKind::IdentLit);
    EXPECT_EQ(expr.children[0]->s_val, "a");
    EXPECT_EQ(expr.children[1]->kind, NodeKind::IdentLit);
    EXPECT_EQ(expr.children[1]->s_val, "b");
}

TEST(QscParserTest, BinarySubtract) {
    auto r = LexAndParse("x - 1;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Binary);
    EXPECT_EQ(expr.s_val, "-");
}

TEST(QscParserTest, BinaryMultiply) {
    auto r = LexAndParse("a * b;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Binary);
    EXPECT_EQ(expr.s_val, "*");
}

TEST(QscParserTest, BinaryEqual) {
    auto r = LexAndParse("x == 0;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Binary);
    EXPECT_EQ(expr.s_val, "==");
}

TEST(QscParserTest, BinaryNotEqual) {
    auto r = LexAndParse("x != 0;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.s_val, "!=");
}

TEST(QscParserTest, LogicalAnd) {
    auto r = LexAndParse("a && b;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Binary);
    EXPECT_EQ(expr.s_val, "&&");
}

TEST(QscParserTest, LogicalOr) {
    auto r = LexAndParse("a || b;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.s_val, "||");
}

TEST(QscParserTest, Assignment) {
    auto r = LexAndParse("x = 5;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Binary);
    EXPECT_EQ(expr.s_val, "=");
    EXPECT_EQ(expr.children[0]->kind, NodeKind::IdentLit);
    EXPECT_EQ(expr.children[0]->s_val, "x");
    EXPECT_EQ(expr.children[1]->kind, NodeKind::IntLit);
    EXPECT_EQ(expr.children[1]->i_val, 5);
}

TEST(QscParserTest, UnaryNegation) {
    auto r = LexAndParse("-x;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Unary);
    EXPECT_EQ(expr.s_val, "-");
    ASSERT_EQ(expr.children.size(), 1u);
    EXPECT_EQ(expr.children[0]->kind, NodeKind::IdentLit);
}

TEST(QscParserTest, UnaryLogicalNot) {
    auto r = LexAndParse("!flag;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Unary);
    EXPECT_EQ(expr.s_val, "!");
}

TEST(QscParserTest, UnaryBitwiseNot) {
    auto r = LexAndParse("~mask;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::Unary);
    EXPECT_EQ(expr.s_val, "~");
}

TEST(QscParserTest, UnaryPlusCollapses) {
    // Unary '+' is a no-op in the parser — returns the operand directly
    auto r = LexAndParse("+x;");
    EXPECT_TRUE(r.ok);
    auto& expr = *r.program->children[0]->children[0];
    EXPECT_EQ(expr.kind, NodeKind::IdentLit);
    EXPECT_EQ(expr.s_val, "x");
}

TEST(QscParserTest, OperatorPrecedenceMulBeforeAdd) {
    // "2 + 3 * 4" → Binary(+, IntLit(2), Binary(*, IntLit(3), IntLit(4)))
    auto r = LexAndParse("2 + 3 * 4;");
    EXPECT_TRUE(r.ok);
    auto& add = *r.program->children[0]->children[0];
    EXPECT_EQ(add.kind, NodeKind::Binary);
    EXPECT_EQ(add.s_val, "+");
    EXPECT_EQ(add.children[0]->kind, NodeKind::IntLit);
    EXPECT_EQ(add.children[0]->i_val, 2);
    // Right side should be the multiply
    EXPECT_EQ(add.children[1]->kind, NodeKind::Binary);
    EXPECT_EQ(add.children[1]->s_val, "*");
}

TEST(QscParserTest, AssignmentIsRightAssociative) {
    // "a = b = c" → Binary(=, IdentLit(a), Binary(=, IdentLit(b), IdentLit(c)))
    auto r = LexAndParse("a = b = c;");
    EXPECT_TRUE(r.ok);
    auto& outer = *r.program->children[0]->children[0];
    EXPECT_EQ(outer.kind, NodeKind::Binary);
    EXPECT_EQ(outer.s_val, "=");
    EXPECT_EQ(outer.children[0]->kind, NodeKind::IdentLit);
    // Inner assignment should be the right child
    EXPECT_EQ(outer.children[1]->kind, NodeKind::Binary);
    EXPECT_EQ(outer.children[1]->s_val, "=");
}

TEST(QscParserTest, ParenthesisOverridesPrecedence) {
    // "(2 + 3) * 4" → Binary(*, Binary(+,...), IntLit(4))
    auto r = LexAndParse("(2 + 3) * 4;");
    EXPECT_TRUE(r.ok);
    auto& mul = *r.program->children[0]->children[0];
    EXPECT_EQ(mul.kind, NodeKind::Binary);
    EXPECT_EQ(mul.s_val, "*");
    EXPECT_EQ(mul.children[0]->kind, NodeKind::Binary);  // the (2+3) group
    EXPECT_EQ(mul.children[0]->s_val, "+");
}

// ============================================================
//  Counter tracking
// ============================================================

TEST(QscParserTest, CallCountSingleCall) {
    auto r = LexAndParse("Foo();");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.call_count, 1u);
}

TEST(QscParserTest, CallCountMultipleCalls) {
    auto r = LexAndParse("A(); B(); C();");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.call_count, 3u);
}

TEST(QscParserTest, ArgCountZero) {
    auto r = LexAndParse("Foo();");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.arg_count, 0u);
}

TEST(QscParserTest, ArgCountCounted) {
    auto r = LexAndParse("A(1, 2); B(3);");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.arg_count, 3u);
}

TEST(QscParserTest, ArgCountNestedCalls) {
    // Outer(Inner(1, 2), 3) → arg_count = 4:
    //   Inner contributes 2 args (1, 2) + Outer contributes 2 args (Inner(...), 3)
    auto r = LexAndParse("Outer(Inner(1, 2), 3);");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.arg_count, 4u);
}

// ============================================================
//  DumpAst smoke test
// ============================================================

TEST(QscParserTest, DumpAstDoesNotCrash) {
    auto r = LexAndParse("if (TRUE) { Foo(42); } else { Bar(); }");
    EXPECT_TRUE(r.ok);
    ASSERT_NE(r.program, nullptr);
    std::ostringstream oss;
    EXPECT_NO_FATAL_FAILURE(DumpAst(*r.program, oss));
    EXPECT_FALSE(oss.str().empty());
    EXPECT_NE(oss.str().find("Program"), std::string::npos);
}

// ============================================================
//  Error cases
// ============================================================

TEST(QscParserTest, ErrorMissingSemicolon) {
    auto r = LexAndParse("Foo()");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(QscParserTest, ErrorMissingCloseParenInCall) {
    auto r = LexAndParse("Foo(;");
    EXPECT_FALSE(r.ok);
}

TEST(QscParserTest, ErrorIfMissingConditionParen) {
    auto r = LexAndParse("if TRUE { Foo(); }");
    EXPECT_FALSE(r.ok);
}

TEST(QscParserTest, ErrorWhileMissingBody) {
    auto r = LexAndParse("while (TRUE)");
    EXPECT_FALSE(r.ok);
}

TEST(QscParserTest, ErrorUnexpectedOperatorInPrefix) {
    // A bare '*' at statement level is not a valid expression start
    auto r = LexAndParse("* x;");
    EXPECT_FALSE(r.ok);
}
