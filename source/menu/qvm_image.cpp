// The retail menu system as data: turns a parsed "LOOP" container into a
// `MenuDefinition`. Container parsing is NOT done here -- `QVM_Parse`
// (source/level/qvm_parser.h) is the codebase's parser and the source of the
// instruction stream this walks.
//
// ORACLE: IGI-RE, a verified C decompilation of retail IGI.exe.
//   * `src/qvm/qvm.c:641-689` `Qvm_OpCall` (HOOK 0x4BD880) -- the call encoding:
//     `code[pc] == OP_CALL`, `argc = *(int32*)(code+pc+1)`, then `argc` int32 code
//     offsets, one per argument; the interpreter resumes at `pc + argc*4 + 5`.
//     `QVM_Parse` implements exactly that (source/level/qvm_parser.cpp:436-468) and
//     the offsets are the `call_targets` this file follows.
//   * `src/qasm/asm.c:62-126` -- how each push operand is emitted (`Emit` writes
//     `n - 1` operand bytes), which is why `PUSH1`, `PUSHB`, `PUSHW`, `PUSHSIB` and
//     `PUSHIIB` are the only pushes the retail menu scripts contain.
//   * `src/qsc/qvmop.c:277-459` -- how the interpreter reads those operands back.
//   * `src/qvm/qscgen.c:296-345` -- the statement the compiler emits:
//     `PUSHII* <name>`, `CALL(count, argPc...)`, `BRA <end of arguments>`, then one
//     BRK-terminated block per argument, in `argPc` order.
//   * `src/level/level.c:168-246` `Level_NewTask` (HOOK 0x4F0780) -- the semantics
//     of `Task_New`, and the rule that makes the tree (see `BuildWidget`).
//   * `src/physobj/paramops.c:249-250` -- the script names "Task_New" and
//     "Task_DeclareParameters".
//
// NOTHING HERE EXECUTES SCRIPT CODE. The walker recognises the pushes that build
// literal arguments and the calls that consume them, which is what the declared
// menu structure consists of. Anything it cannot account for is reported with the
// code address (and the opcode) instead of being guessed at, and a report makes the
// result `ok == false`.
//
// TWO INPUT PATHS, ONE MODEL. `FromQvm`/`FromQvmFile` walk `QVMFile::instructions`.
// `FromDecompiledSource` reads `QVM_DecompileToString` text (`Task_New(-1, "MenuManager",
// ...);`) with a strict reader that rejects anything outside that shape. Both feed
// the same `Build`, so the two paths cannot disagree about the model.

#include "menu/qvm_image.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace igi {
namespace {

std::string Hex(uint32_t address) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", address);
    return std::string(buf);
}

// ─── the decoded expression model ────────────────────────────────────────────
//
// A statement is an expression; a `Task_New` argument that is itself a call is a
// Call node, which is what makes the widget tree (see `BuildWidget`).

struct Node {
    enum class Kind { Integer, Real, String, Name, Boolean, Call };

    Kind kind = Kind::Integer;
    long long integer = 0;
    double real = 0.0;
    std::string text;        // String/Name/Boolean payload, or the callee for Call
    std::vector<Node> args;  // Call only: one entry per argument, in order

    bool IsCall() const { return kind == Kind::Call; }
};

struct Program {
    std::vector<Node> statements;  // the top-level statements, in file order
    std::vector<std::string> problems;
    bool ok = true;
};

void AddProblem(Program& p, const std::string& what) {
    p.problems.push_back(what);
}

// ─── the container path ──────────────────────────────────────────────────────

// Walks `QVMFile::instructions` the way the interpreter walks the code:
// `QVM_Parse` decodes the whole code section linearly, so instruction order is
// address order and an argument's code is a contiguous run of instructions ending
// at its BRK.
class QvmWalker {
public:
    explicit QvmWalker(const QVMFile& qvm) : qvm_(qvm) {
        for (size_t i = 0; i < qvm_.instructions.size(); ++i) {
            index_at_[qvm_.instructions[i].address] = i;
        }
    }

    Program Run() {
        Program p;
        size_t index = 0;
        while (index < qvm_.instructions.size()) {
            const QVMInstruction& in = qvm_.instructions[index];
            if (in.type != QVMOpType::CALL) {
                ++index;
                continue;
            }
            Node call;
            uint32_t end = 0;
            if (!DecodeCall(index, p, &call, &end)) {
                break;
            }
            p.statements.push_back(std::move(call));
            const size_t next = IndexAt(end);
            if (next == kNoIndex) {
                AddProblem(p, "the call at " + Hex(in.address) + " ends at " + Hex(end) +
                                  ", where the code has no instruction");
                break;
            }
            index = next;
        }
        p.ok = p.problems.empty();
        return p;
    }

private:
    static constexpr size_t kNoIndex = static_cast<size_t>(-1);

    size_t IndexAt(uint32_t address) const {
        const auto it = index_at_.find(address);
        return it == index_at_.end() ? kNoIndex : it->second;
    }

    // The function name a call relies on: the compiler pushes it immediately before
    // the CALL (qscgen.c:296-345), so it is the preceding instruction.
    static bool IsNamePush(QVMOpType type) {
        return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
               type == QVMOpType::PUSHIIW;
    }

    // The name a push refers to, through the engine's pool lookup: nullptr when
    // Qvm_GetString refuses the index.
    const char* NameOf(const QVMInstruction& in) const {
        if (in.type == QVMOpType::PUSHI) {
            return in.inline_text.c_str();  // inline form; the retail assembler emits the indexed one
        }
        return qvm_.name_pool.Resolve(in.operand);
    }

    // The engine's pool lookup, Qvm_GetString (HOOK 0x4BEC40,
    // src/qsc/qvmop.c:470-476): `index < count && offsets[index] < size`, else
    // nothing.  Qvm_OpPushIndexed's `bad_index` path (src/qsc/qvmop.c:455-457)
    // turns a refused index into QVM_ERR_INDEX = 15, which Qvm_Run prints as
    // "Index out of range" (src/qvm/qvm.c:419-421); the previous reader compared
    // against the vector size and substituted an empty string instead.
    const char* ResolvePool(const QVMTextPool& pool, const QVMInstruction& in, Program& p) const {
        const char* text = pool.Resolve(in.operand);
        if (!text) {
            AddProblem(p, std::string(QVM_OpName(in.type)) + " at " + Hex(in.address) +
                              " indexes " + std::to_string(in.operand) +
                              ", which the pool refuses (QVM error 15, index out of range: " +
                              std::to_string(pool.count) + " entries, table " +
                              std::to_string(pool.table_bytes) + " bytes)");
        }
        return text;
    }

    static Node Integer(long long value) {
        Node out;
        out.kind = Node::Kind::Integer;
        out.integer = value;
        return out;
    }

    static Node Real(double value) {
        Node out;
        out.kind = Node::Kind::Real;
        out.real = value;
        return out;
    }

    // TRUE and FALSE are identifiers in the retail name tables, so a pushed name
    // that is one of them is a Boolean and everything else is a Name.
    static Node Name(std::string text) {
        Node out;
        out.kind = (text == "TRUE" || text == "FALSE") ? Node::Kind::Boolean : Node::Kind::Name;
        out.text = std::move(text);
        return out;
    }

    // A unary operator needs a value under it, and that value must be a literal: the
    // retail menu scripts never apply one to a call result, so that shape is refused
    // rather than silently reducing the call to an integer.
    static bool CheckUnaryOperand(const QVMInstruction& in, Program& p, int values, bool value_is_call) {
        if (values == 0) {
            AddProblem(p, std::string(QVM_OpName(in.type)) + " at " + Hex(in.address) + " has no value to apply to");
            return false;
        }
        if (value_is_call) {
            AddProblem(p, std::string(QVM_OpName(in.type)) + " at " + Hex(in.address) +
                              " is applied to a call result, which this reader does not evaluate");
            return false;
        }
        return true;
    }

    // One literal or operator instruction of an argument's code.  `values` counts the
    // values on the stack (a push adds one); `value_is_call` says whether the value
    // currently on the stack is a call result, which a unary operator cannot apply to.
    bool ApplyInstruction(const QVMInstruction& in, Program& p, Node* out, int* values, bool value_is_call) {
        switch (in.type) {
        case QVMOpType::PUSH0:
            *out = Integer(0);
            ++*values;
            return true;
        case QVMOpType::PUSH1:
            *out = Integer(1);
            ++*values;
            return true;
        case QVMOpType::PUSHM:
            *out = Integer(-1);
            ++*values;
            return true;
        case QVMOpType::PUSHB:
        case QVMOpType::PUSHW:
        case QVMOpType::PUSH:
            *out = Integer(in.signed_operand);
            ++*values;
            return true;
        case QVMOpType::PUSHF:
            *out = Real(static_cast<double>(in.operand_float));
            ++*values;
            return true;
        case QVMOpType::PUSHSI:
        case QVMOpType::PUSHSIB:
        case QVMOpType::PUSHSIW: {
            const char* text = ResolvePool(qvm_.string_pool, in, p);
            if (!text) {
                return false;
            }
            out->kind = Node::Kind::String;
            out->text = text;
            ++*values;
            return true;
        }
        case QVMOpType::PUSHS:
            out->kind = Node::Kind::String;
            out->text = in.inline_text;
            ++*values;
            return true;
        case QVMOpType::PUSHI:
        case QVMOpType::PUSHII:
        case QVMOpType::PUSHIIB:
        case QVMOpType::PUSHIIW: {
            const char* name = in.type == QVMOpType::PUSHI ? NameOf(in)
                                                           : ResolvePool(qvm_.name_pool, in, p);
            if (!name) {
                return false;
            }
            *out = Name(name);
            ++*values;
            return true;
        }
        // Unary operators applied to the value just pushed. The retail menu scripts
        // use exactly one of these shapes: PUSH1 MINUS for the integer -1.
        case QVMOpType::PLUS:
            if (!CheckUnaryOperand(in, p, *values, value_is_call)) {
                return false;
            }
            return true;
        case QVMOpType::MINUS:
            if (!CheckUnaryOperand(in, p, *values, value_is_call)) {
                return false;
            }
            if (out->kind == Node::Kind::Real) {
                out->real = -out->real;
            } else {
                *out = Integer(-out->integer);
            }
            return true;
        case QVMOpType::INV:
            if (!CheckUnaryOperand(in, p, *values, value_is_call)) {
                return false;
            }
            *out = Integer(~out->integer);
            return true;
        case QVMOpType::NOT:
            if (!CheckUnaryOperand(in, p, *values, value_is_call)) {
                return false;
            }
            *out = Integer(out->integer == 0 ? 1 : 0);
            return true;
        default:
            AddProblem(p, std::string("the argument code at ") + Hex(in.address) + " uses " +
                              QVM_OpName(in.type) + ", which this reader does not evaluate");
            return false;
        }
    }

    // One BRK-terminated argument block, from its first instruction.
    //
    // A block produces exactly one value, and the retail menu scripts produce it in
    // exactly two ways (census in docs/igi-re-parity/MENU_SYSTEM.md): a single
    // literal push (optionally with a unary operator), or a nested `Task_New` call,
    // whose callee push the CALL consumes.  The `values` count is therefore the
    // number of values on the stack: a push adds one, and a call replaces its callee
    // push with its result, so it leaves the count alone.
    bool DecodeArgBlock(size_t index, Program& p, Node* out, uint32_t* end_out) {
        const uint32_t start = qvm_.instructions[index].address;
        int values = 0;
        bool value_is_call = false;
        while (true) {
            if (index >= qvm_.instructions.size()) {
                AddProblem(p, "the argument code at " + Hex(start) + " never reaches its BRK");
                return false;
            }
            const QVMInstruction& in = qvm_.instructions[index];
            if (in.type == QVMOpType::BRK) {
                *end_out = in.address + in.size;
                if (values != 1) {
                    AddProblem(p, "the argument code at " + Hex(start) + " leaves " + std::to_string(values) +
                                      " values; exactly one argument value was expected");
                }
                return true;
            }
            if (in.type == QVMOpType::CALL) {
                if (values != 1) {
                    AddProblem(p, "the call at " + Hex(in.address) + " has " + std::to_string(values) +
                                      " values on the stack; only its callee push was expected");
                    return false;
                }
                uint32_t end = 0;
                if (!DecodeCall(index, p, out, &end)) {
                    return false;
                }
                // The CALL pops the callee push and pushes its result.
                value_is_call = true;
                const size_t next = IndexAt(end);
                if (next == kNoIndex) {
                    AddProblem(p, "the argument call at " + Hex(in.address) + " ends at " + Hex(end) +
                                      ", where the code has no instruction");
                    return false;
                }
                index = next;
                continue;
            }
            if (!ApplyInstruction(in, p, out, &values, value_is_call)) {
                return false;
            }
            if (in.type != QVMOpType::PLUS && in.type != QVMOpType::MINUS && in.type != QVMOpType::INV &&
                in.type != QVMOpType::NOT) {
                value_is_call = false;
            }
            ++index;
        }
    }

    bool DecodeCall(size_t index, Program& p, Node* out, uint32_t* end_out) {
        const QVMInstruction& call = qvm_.instructions[index];
        out->kind = Node::Kind::Call;
        if (index == 0 || !IsNamePush(qvm_.instructions[index - 1].type)) {
            AddProblem(p, "the call at " + Hex(call.address) + " is not preceded by a function-name push");
        } else {
            const char* name = NameOf(qvm_.instructions[index - 1]);
            if (!name) {
                AddProblem(p, "the call at " + Hex(call.address) +
                                  " is preceded by a name push whose pool index " +
                                  std::to_string(qvm_.instructions[index - 1].operand) +
                                  " the name pool refuses (QVM error 15, index out of range)");
                return false;
            }
            out->text = name;
        }

        // `Qvm_OpCall` resumes at the instruction after the argument table, which is
        // the BRA the compiler emits to skip the argument code (qscgen.c:296-345).
        const size_t bra_index = index + 1;
        if (bra_index >= qvm_.instructions.size() || qvm_.instructions[bra_index].type != QVMOpType::BRA) {
            AddProblem(p, "the call at " + Hex(call.address) +
                              " is not followed by the BRA that skips its arguments");
            return false;
        }
        const QVMInstruction& bra = qvm_.instructions[bra_index];
        const long long bra_target = static_cast<long long>(bra.address) + bra.size + bra.signed_operand;
        if (bra_target < 0) {
            AddProblem(p, "the BRA after the call at " + Hex(call.address) + " points before the code");
            return false;
        }

        uint32_t expected = bra.address + bra.size;
        for (size_t i = 0; i < call.call_targets.size(); ++i) {
            const long long target = call.call_targets[i];
            if (target < 0) {
                AddProblem(p, "the call at " + Hex(call.address) + " has argPc[" + std::to_string(i) + "] = " +
                                  std::to_string(target) + ", which is not an address");
                return false;
            }
            const size_t arg_index = IndexAt(static_cast<uint32_t>(target));
            if (arg_index == kNoIndex) {
                AddProblem(p, "the call at " + Hex(call.address) + " has argPc[" + std::to_string(i) + "] = " +
                                  Hex(static_cast<uint32_t>(target)) + ", where the code has no instruction");
                return false;
            }
            if (static_cast<uint32_t>(target) != expected) {
                AddProblem(p, "the call at " + Hex(call.address) + " has argPc[" + std::to_string(i) + "] = " +
                                  Hex(static_cast<uint32_t>(target)) +
                                  ", but the previous argument's code ends at " + Hex(expected));
                expected = static_cast<uint32_t>(target);
            }
            Node arg;
            uint32_t end = 0;
            if (!DecodeArgBlock(arg_index, p, &arg, &end)) {
                return false;
            }
            out->args.push_back(std::move(arg));
            expected = end;
        }
        if (bra_target != static_cast<long long>(expected)) {
            AddProblem(p, "the BRA after the call at " + Hex(call.address) + " skips to " +
                              Hex(static_cast<uint32_t>(bra_target)) + ", but its arguments end at " + Hex(expected));
        }
        *end_out = expected;
        return true;
    }

    const QVMFile& qvm_;
    std::unordered_map<uint32_t, size_t> index_at_;
};

// ─── the decompiled-source path ──────────────────────────────────────────────
//
// `QVM_DecompileToString` prints one `Callee(arg, arg, ...);` statement per
// top-level call, with a call argument indented onto its own line
// (source/level/qvm_decompiler.cpp:159-183, 556-566) and literals printed as
// `"escaped text"`, a decimal number, or a bare identifier
// (source/level/qvm_decompiler.cpp:295-342). This reader accepts exactly that and
// rejects anything else -- binary operators, `while`, `if`, `{}` blocks -- with an
// error naming the construct, rather than guessing at it.
class SourceReader {
public:
    explicit SourceReader(const std::string& source) : s_(source) {}

    Program Run() {
        Program p;
        while (true) {
            SkipSpace();
            if (AtEnd()) {
                break;
            }
            if (s_[i_] == ';') {
                ++i_;
                continue;
            }
            const size_t start = i_;
            Node statement;
            std::string error;
            if (!ParseExpression(&statement, &error)) {
                AddProblem(p, "offset " + std::to_string(start) + ": " + error);
                break;
            }
            p.statements.push_back(std::move(statement));
            SkipSpace();
            if (AtEnd()) {
                break;
            }
            if (s_[i_] == ';') {
                ++i_;
                continue;
            }
            AddProblem(p, "offset " + std::to_string(i_) + ": expected ';' after the statement that starts at " +
                              std::to_string(start) + ", found '" + s_[i_] + "'");
            break;
        }
        p.ok = p.problems.empty();
        return p;
    }

private:
    bool AtEnd() const { return i_ >= s_.size(); }

    void SkipSpace() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) {
            ++i_;
        }
    }

    static bool IsIdentStart(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }
    static bool IsIdentChar(char c) {
        return IsIdentStart(c) || (c >= '0' && c <= '9') || c == '.';
    }
    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

    bool ParseString(std::string* out, std::string* error) {
        ++i_;  // the opening quote
        while (true) {
            if (AtEnd()) {
                *error = "unterminated string literal";
                return false;
            }
            const char c = s_[i_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (AtEnd()) {
                    *error = "unterminated escape in a string literal";
                    return false;
                }
                const char e = s_[i_++];
                // The inverse of the decompiler's EscapeQSCString
                // (source/level/qvm_decompiler.cpp:36-44).
                *out += e == 'n' ? '\n' : e;
                continue;
            }
            *out += c;
        }
    }

    bool ParseIdentifier(std::string* out, std::string* error) {
        if (!IsIdentStart(s_[i_])) {
            *error = "expected an identifier";
            return false;
        }
        while (i_ < s_.size() && IsIdentChar(s_[i_])) {
            *out += s_[i_++];
        }
        return true;
    }

    bool ParseNumber(Node* out, std::string* error) {
        const size_t start = i_;
        while (i_ < s_.size() && IsDigit(s_[i_])) {
            ++i_;
        }
        bool is_real = false;
        if (i_ < s_.size() && s_[i_] == '.') {
            is_real = true;
            ++i_;
            while (i_ < s_.size() && IsDigit(s_[i_])) {
                ++i_;
            }
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            is_real = true;
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
                ++i_;
            }
            while (i_ < s_.size() && IsDigit(s_[i_])) {
                ++i_;
            }
        }
        const std::string text = s_.substr(start, i_ - start);
        if (text.empty()) {
            *error = "expected a number";
            return false;
        }
        if (is_real) {
            out->kind = Node::Kind::Real;
            out->real = std::strtod(text.c_str(), nullptr);
        } else {
            out->kind = Node::Kind::Integer;
            out->integer = std::strtoll(text.c_str(), nullptr, 10);
        }
        return true;
    }

    bool ParsePrimary(Node* out, std::string* error) {
        if (AtEnd()) {
            *error = "expected a value";
            return false;
        }
        if (s_[i_] == '"') {
            out->kind = Node::Kind::String;
            return ParseString(&out->text, error);
        }
        if (IsDigit(s_[i_])) {
            return ParseNumber(out, error);
        }
        if (s_[i_] == '(') {
            ++i_;
            if (!ParseExpression(out, error)) {
                return false;
            }
            SkipSpace();
            if (AtEnd() || s_[i_] != ')') {
                *error = "expected ')'";
                return false;
            }
            ++i_;
            return true;
        }
        if (IsIdentStart(s_[i_])) {
            std::string identifier;
            if (!ParseIdentifier(&identifier, error)) {
                return false;
            }
            SkipSpace();
            if (i_ < s_.size() && s_[i_] == '(') {
                if (identifier == "while" || identifier == "if") {
                    *error = "'" + identifier + "' blocks are not supported; only call statements are read";
                    return false;
                }
                out->kind = Node::Kind::Call;
                out->text = identifier;
                return ParseArgs(out, error);
            }
            out->kind = (identifier == "TRUE" || identifier == "FALSE") ? Node::Kind::Boolean : Node::Kind::Name;
            out->text = identifier;
            return true;
        }
        if (s_[i_] == '{' || s_[i_] == '}') {
            *error = "a '{' block is not supported; only call statements are read";
            return false;
        }
        *error = std::string("unexpected '") + s_[i_] + "'";
        return false;
    }

    bool ParseArgs(Node* out, std::string* error) {
        ++i_;  // the '('
        SkipSpace();
        if (!AtEnd() && s_[i_] == ')') {
            ++i_;
            return true;
        }
        while (true) {
            Node arg;
            if (!ParseExpression(&arg, error)) {
                return false;
            }
            out->args.push_back(std::move(arg));
            SkipSpace();
            if (AtEnd()) {
                *error = "unterminated argument list";
                return false;
            }
            if (s_[i_] == ',') {
                ++i_;
                SkipSpace();
                continue;
            }
            if (s_[i_] == ')') {
                ++i_;
                return true;
            }
            *error = std::string("expected ',' or ')' in the argument list, found '") + s_[i_] + "'";
            return false;
        }
    }

    bool ParseExpression(Node* out, std::string* error) {
        SkipSpace();
        if (AtEnd()) {
            *error = "expected an expression";
            return false;
        }
        // A unary operator, as the decompiler prints PLUS/MINUS/INV/NOT
        // (source/level/qvm_decompiler.cpp:344-368).
        const char c = s_[i_];
        if (c == '-' || c == '+' || c == '!' || c == '~') {
            ++i_;
            Node operand;
            if (!ParseExpression(&operand, error)) {
                return false;
            }
            if (c == '-') {
                if (operand.kind == Node::Kind::Real) {
                    operand.real = -operand.real;
                } else if (operand.kind == Node::Kind::Integer) {
                    operand.integer = -operand.integer;
                } else {
                    *error = "cannot negate a non-numeric value";
                    return false;
                }
            } else if (c == '!') {
                const bool truth = operand.kind == Node::Kind::Integer && operand.integer != 0;
                operand = Node{};
                operand.kind = Node::Kind::Integer;
                operand.integer = truth ? 0 : 1;
            } else if (c == '~') {
                if (operand.kind != Node::Kind::Integer) {
                    *error = "cannot apply '~' to a non-integer value";
                    return false;
                }
                operand.integer = ~operand.integer;
            }
            *out = std::move(operand);
            return true;
        }
        if (!ParsePrimary(out, error)) {
            return false;
        }
        SkipSpace();
        // Anything that continues an expression is outside the shapes the decompiler
        // emits for a menu script, so it is refused rather than misread.
        if (!AtEnd()) {
            const char next = s_[i_];
            if (next == '+' || next == '*' || next == '/' || next == '%' || next == '&' || next == '|' ||
                next == '^' || next == '<' || next == '>' || next == '=' || next == '-') {
                *error = std::string("the operator '") + next +
                         "' is not supported; only literal arguments and nested calls are read";
                return false;
            }
        }
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
};

// ─── the shared model ────────────────────────────────────────────────────────

MenuValue ToValue(const Node& node) {
    MenuValue out;
    switch (node.kind) {
    case Node::Kind::Integer:
        out.kind = MenuValueKind::Integer;
        out.integer = node.integer;
        break;
    case Node::Kind::Real:
        out.kind = MenuValueKind::Real;
        out.real = node.real;
        break;
    case Node::Kind::String:
        out.kind = MenuValueKind::String;
        out.text = node.text;
        break;
    case Node::Kind::Name:
        out.kind = MenuValueKind::Name;
        out.text = node.text;
        break;
    case Node::Kind::Boolean:
        out.kind = MenuValueKind::Boolean;
        out.text = node.text;
        break;
    case Node::Kind::Call:
        out.kind = MenuValueKind::Call;
        out.text = node.text;
        break;
    }
    return out;
}

// `MenuTypeDecl` is the public spelling of this (menu/qvm_image.h); the reader's
// declarations are kept on the definition so the writer can re-emit them.
const std::vector<MenuField>* FindDecl(const std::vector<MenuTypeDecl>& types, const std::string& type) {
    for (const MenuTypeDecl& t : types) {
        if (t.name == type) {
            return &t.fields;
        }
    }
    return nullptr;
}

int CountTaskNew(const Node& node) {
    int count = node.IsCall() && node.text == "Task_New" ? 1 : 0;
    for (const Node& arg : node.args) {
        count += CountTaskNew(arg);
    }
    return count;
}

// Builds one widget and, recursively, the widgets nested in its extra arguments.
//
// `Task_New(id, type, name, params...)` (`Level_NewTask`, src/level/level.c:171-246):
// args 0/1/2 are the id, the type name and the instance name (level.c:187-190); the
// arguments after them are consumed in the declared parameter order (level.c:214-221).
// Once the parameters are parsed, the task becomes the `Task_New` symbol context and
// the REMAINING arguments are evaluated (level.c:234-239) -- so a `Task_New` in an
// extra argument position creates a child of this widget, which is the whole nesting
// rule. During parameter parsing `gLevelTaskNewEnabled` is 0, so a nested `Task_New`
// in a parameter position is an error in the runtime (level.c:182-185, 209); the
// reader reports it too.
bool BuildWidget(const Node& call, const std::vector<MenuTypeDecl>& types, MenuWidget* out, std::string* error) {
    if (call.args.size() < 3) {
        *error = "a Task_New has " + std::to_string(call.args.size()) +
                 " arguments; it needs at least an id, a type and a name";
        return false;
    }

    out->type = ToValue(call.args[1]).AsText();
    out->name = ToValue(call.args[2]).AsText();

    // The task id, or -1 when it is not a literal number (the retail scripts always
    // pass one, and -1 is what `Level_NewTask` stores for "no level id").
    const MenuValue id_value = ToValue(call.args[0]);
    out->id = id_value.IsNumber() ? static_cast<int>(id_value.AsNumber()) : -1;

    const std::vector<MenuField>* decl = FindDecl(types, out->type);
    const size_t declared = decl != nullptr ? decl->size() : 0;
    for (size_t i = 0; i < declared; ++i) {
        MenuField field = (*decl)[i];
        field.values.push_back(ToValue(call.args[3 + i]));
        out->fields.push_back(std::move(field));
    }

    for (size_t i = 3; i < call.args.size(); ++i) {
        const Node& arg = call.args[i];
        if (!arg.IsCall()) {
            // A literal in an extra position is consumed by the runtime and does not
            // affect the tree (level.c:235-238).
            continue;
        }
        if (i < 3 + declared) {
            *error = "'" + out->type + "' argument " + std::to_string(i) +
                     " is a nested call inside the declared parameters";
            return false;
        }
        if (arg.text != "Task_New") {
            *error = "a widget position of '" + out->type + "' holds a call to '" + arg.text +
                     "' rather than Task_New";
            return false;
        }
        MenuWidget child;
        if (!BuildWidget(arg, types, &child, error)) {
            return false;
        }
        out->children.push_back(std::move(child));
    }
    return true;
}

MenuDefinition Build(const Program& program, const std::string& source) {
    MenuDefinition def;
    def.source = source;

    if (!program.ok) {
        def.error = program.problems.empty() ? "the script did not decode" : program.problems.front();
        return def;
    }

    // The widget type declarations, in file order: each is
    // Task_DeclareParameters(typeName, field1Name, field1Type, field2Name, field2Type, ...).
    // Note: the runtime's Task_DeclareParameters is a no-op that returns 0
    // (src/level/level.c:156-166), so this list is data the script carries rather
    // than something the engine consumes; the *order* it implies is the order the
    // runtime parses arguments in (level.c:214-221).
    std::vector<MenuTypeDecl> types;
    for (const Node& statement : program.statements) {
        if (!statement.IsCall() || statement.text != "Task_DeclareParameters") {
            continue;
        }
        if (statement.args.empty() || statement.args.size() % 2 == 0) {
            def.error = "a Task_DeclareParameters has " + std::to_string(statement.args.size()) +
                        " arguments; it needs a type name followed by name/type pairs";
            return def;
        }
        MenuTypeDecl decl;
        decl.name = ToValue(statement.args[0]).AsText();
        for (size_t i = 1; i + 1 < statement.args.size(); i += 2) {
            MenuField field;
            field.name = ToValue(statement.args[i]).AsText();
            field.type = ToValue(statement.args[i + 1]).AsText();
            decl.fields.push_back(std::move(field));
        }
        types.push_back(std::move(decl));
    }

    // The MenuManager is the top-level Task_New; every other Task_New is a widget
    // inside it.
    const Node* root = nullptr;
    int widget_count = 0;
    for (const Node& statement : program.statements) {
        if (statement.IsCall() && statement.text == "Task_New" && root == nullptr) {
            root = &statement;
        }
    }
    for (const Node& statement : program.statements) {
        widget_count += CountTaskNew(statement);
    }
    if (root == nullptr) {
        def.error = "the script has no top-level Task_New";
        return def;
    }

    std::string error;
    if (!BuildWidget(*root, types, &def.root, &error)) {
        def.error = error;
        return def;
    }
    def.widget_count = widget_count;
    // The statements the script carries, in file order, for the save-back half.
    def.declarations = types;
    if (def.root.type != "MenuManager") {
        def.error = "the script's top-level Task_New creates '" + def.root.type + "', not a MenuManager";
        return def;
    }
    def.ok = true;
    return def;
}

}  // namespace

// ─── MenuValue ───────────────────────────────────────────────────────────────

double MenuValue::AsNumber() const {
    switch (kind) {
    case MenuValueKind::Integer:
        return static_cast<double>(integer);
    case MenuValueKind::Real:
        return real;
    case MenuValueKind::Boolean:
        return text == "TRUE" ? 1.0 : 0.0;
    default:
        return 0.0;
    }
}

std::string MenuValue::AsText() const {
    switch (kind) {
    case MenuValueKind::String:
    case MenuValueKind::Name:
    case MenuValueKind::Boolean:
    case MenuValueKind::Call:
        return text;
    case MenuValueKind::Real: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", real);
        return std::string(buf);
    }
    case MenuValueKind::Integer:
    default:
        return std::to_string(integer);
    }
}

// ─── MenuWidget ──────────────────────────────────────────────────────────────

const MenuField* MenuWidget::FindField(const std::string& field_name) const {
    for (const MenuField& field : fields) {
        if (field.name == field_name) {
            return &field;
        }
    }
    return nullptr;
}

const MenuValue* MenuWidget::FindValue(const std::string& field_name) const {
    const MenuField* field = FindField(field_name);
    if (field == nullptr || field->values.empty()) {
        return nullptr;
    }
    return &field->values.front();
}

MenuField* MenuWidget::FindField(const std::string& field_name) {
    for (MenuField& field : fields) {
        if (field.name == field_name) {
            return &field;
        }
    }
    return nullptr;
}

MenuValue* MenuWidget::FindValue(const std::string& field_name) {
    MenuField* field = FindField(field_name);
    if (field == nullptr || field->values.empty()) {
        return nullptr;
    }
    return &field->values.front();
}

std::string MenuWidget::FieldText(const std::string& field_name) const {
    const MenuValue* value = FindValue(field_name);
    return value == nullptr ? std::string() : value->AsText();
}

double MenuWidget::FieldNumber(const std::string& field_name, double fallback) const {
    const MenuValue* value = FindValue(field_name);
    if (value == nullptr || !value->IsNumber()) {
        return fallback;
    }
    return value->AsNumber();
}

// ─── MenuDefinition ──────────────────────────────────────────────────────────

MenuDefinition MenuDefinition::FromQvmFile(const std::string& path) {
    const QVMFile qvm = QVM_Parse(path);
    if (!qvm.valid) {
        MenuDefinition def;
        def.source = path;
        def.error = qvm.error.empty() ? "the container did not parse" : qvm.error;
        return def;
    }
    return Build(QvmWalker(qvm).Run(), path);
}

MenuDefinition MenuDefinition::FromQvm(const QVMFile& qvm) {
    if (!qvm.valid) {
        MenuDefinition def;
        def.error = qvm.error.empty() ? "the container did not parse" : qvm.error;
        return def;
    }
    return Build(QvmWalker(qvm).Run(), std::string());
}

MenuDefinition MenuDefinition::FromDecompiledSource(const std::string& source) {
    return Build(SourceReader(source).Run(), std::string());
}

std::vector<const MenuWidget*> MenuDefinition::Flatten() const {
    std::vector<const MenuWidget*> out;
    if (root.type.empty()) {
        return out;
    }
    // Depth-first, the widget before its children.
    std::vector<const MenuWidget*> stack{&root};
    while (!stack.empty()) {
        const MenuWidget* widget = stack.back();
        stack.pop_back();
        out.push_back(widget);
        for (auto it = widget->children.rbegin(); it != widget->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }
    return out;
}

std::vector<int> MenuDefinition::Ids() const {
    std::vector<int> out;
    for (const MenuWidget* widget : Flatten()) {
        out.push_back(widget->id);
    }
    return out;
}

const MenuWidget* MenuDefinition::FindById(int id) const {
    for (const MenuWidget* widget : Flatten()) {
        if (widget->id == id) {
            return widget;
        }
    }
    return nullptr;
}

const MenuWidget* MenuDefinition::FindByType(const std::string& type) const {
    for (const MenuWidget* widget : Flatten()) {
        if (widget->type == type) {
            return widget;
        }
    }
    return nullptr;
}

const MenuTypeDecl* MenuDefinition::FindType(const std::string& type) const {
    for (const MenuTypeDecl& decl : declarations) {
        if (decl.name == type) {
            return &decl;
        }
    }
    return nullptr;
}

// Mutable counterparts: same traversal, so an edit made through one of these is
// the edit `menu/menu_qvm_writer.h` serialises.
std::vector<MenuWidget*> MenuDefinition::Flatten() {
    std::vector<MenuWidget*> out;
    if (root.type.empty()) {
        return out;
    }
    std::vector<MenuWidget*> stack{&root};
    while (!stack.empty()) {
        MenuWidget* widget = stack.back();
        stack.pop_back();
        out.push_back(widget);
        for (auto it = widget->children.rbegin(); it != widget->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }
    return out;
}

MenuWidget* MenuDefinition::FindById(int id) {
    for (MenuWidget* widget : Flatten()) {
        if (widget->id == id) {
            return widget;
        }
    }
    return nullptr;
}

MenuWidget* MenuDefinition::FindByType(const std::string& type) {
    for (MenuWidget* widget : Flatten()) {
        if (widget->type == type) {
            return widget;
        }
    }
    return nullptr;
}

}  // namespace igi
