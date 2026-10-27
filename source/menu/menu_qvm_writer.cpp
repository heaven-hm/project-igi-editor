// Save-back for the QVM-driven in-game menu (issue #74). See the header for the
// engine chain this translates: `QscGen_Compile` HOOK 0x4BBCB0
// (src/qvm/qscgen.c:96-141) -> `QvmAsm_OpPush` HOOK 0x4C0A10 (src/qasm/asm.c:66-126)
// -> `QvmAsm_Assemble` HOOK 0x4BB270 (src/qvm/qvmasm.c:103-276). Stages two and
// three are `qvm::Compile` (source/level/qvm_compiler.cpp); this file is stage one.

#include "menu/menu_qvm_writer.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "level/qsc_parser.h"
#include "level/qvm_compiler.h"

namespace igi {
namespace {

using qsc::Node;
using qsc::NodeKind;

std::unique_ptr<Node> NewNode(NodeKind kind) {
    auto n = std::make_unique<Node>();
    n->kind = kind;
    return n;
}

std::unique_ptr<Node> IntLit(int32_t v) {
    auto n = NewNode(NodeKind::IntLit);
    n->i_val = v;
    return n;
}

std::unique_ptr<Node> FloatLit(float v) {
    auto n = NewNode(NodeKind::FloatLit);
    n->f_val = v;
    return n;
}

std::unique_ptr<Node> StringLit(std::string s) {
    auto n = NewNode(NodeKind::StringLit);
    n->s_val = std::move(s);
    return n;
}

std::unique_ptr<Node> IdentLit(std::string s) {
    auto n = NewNode(NodeKind::IdentLit);
    n->s_val = std::move(s);
    return n;
}

std::unique_ptr<Node> BoolLit(bool v) {
    auto n = NewNode(NodeKind::BoolLit);
    n->b_val = v;
    return n;
}

// The unary `MINUS` the engine's compiler emits for a negative literal:
// `QscGen_Expression`'s EXPR_OP case writes the operand first and then the
// operator (src/qvm/qscgen.c:382-400), and `QscGen_OpCodes[4]` is `OP_MINUS`
// (src/qvm/qscgen.c:287-294). `QscGen_WriteInt`/`QscGen_WriteFloat`
// (HOOK 0x4BC880 / 0x4BC8D0) then print the magnitude, so `QvmAsm_OpPush`'s
// `v == -1 -> PUSHM` arm (src/qasm/asm.c:74-75) is never reached -- measured over
// the installed corpus: 41497 `MINUS` opcodes, each preceded by a magnitude push,
// and `PUSHM` 0 times.
std::unique_ptr<Node> Negate(std::unique_ptr<Node> magnitude) {
    auto n = NewNode(NodeKind::Unary);
    n->s_val = "-";
    n->children.push_back(std::move(magnitude));
    return n;
}

std::unique_ptr<Node> CallNode(const std::string& callee, std::vector<std::unique_ptr<Node>> args) {
    auto n = NewNode(NodeKind::Call);
    n->s_val = callee;
    n->children = std::move(args);
    return n;
}

std::unique_ptr<Node> ExprStmt(std::unique_ptr<Node> expr) {
    auto n = NewNode(NodeKind::ExprStmt);
    n->children.push_back(std::move(expr));
    return n;
}

std::string Quote(const std::string& s) {
    return "'" + s + "'";
}

// ─── values ──────────────────────────────────────────────────────────────────

// A literal the engine's assembler can encode. The ranges are `QvmAsm_OpPush`'s
// (src/qasm/asm.c:79-111) as the engine's compiler reaches them: it never passes a
// negative token, so a negative value is `PUSH <magnitude>` + `MINUS` and the
// magnitude must fit the 32-bit literal the assembler's `crt_atoi` reads.
bool ValueToNode(const MenuValue& value, const std::string& where, std::unique_ptr<Node>* out,
                 std::string* error) {
    switch (value.kind) {
    case MenuValueKind::Integer: {
        if (value.integer == 0) {
            *out = IntLit(0);  // -> PUSH0 (src/qasm/asm.c:86-87)
            return true;
        }
        if (value.integer > 0) {
            if (value.integer > 2147483647LL) {
                *error = where + " is the integer " + std::to_string(value.integer) +
                         ", which does not fit the engine's 32-bit literal";
                return false;
            }
            *out = IntLit(static_cast<int32_t>(value.integer));
            return true;
        }
        const long long magnitude = -value.integer;
        if (magnitude > 2147483647LL) {
            *error = where + " is the integer " + std::to_string(value.integer) +
                     ", whose magnitude does not fit the engine's 32-bit literal";
            return false;
        }
        *out = Negate(IntLit(static_cast<int32_t>(magnitude)));
        return true;
    }

    case MenuValueKind::Real: {
        // `QvmAsm_OpPush`'s float arm (src/qasm/asm.c:99-111) shortens 0.0 to
        // PUSH0 and 1.0 to PUSH1; `QscGen_WriteFloat` prints "%f", so the lexer
        // sees a float token and that arm is what runs. -1.0 is never handed to
        // it -- the compiler emits `MINUS` on the magnitude instead (see Negate),
        // which is why the corpus has no PUSHM.
        const float f = static_cast<float>(value.real);
        if (f == 0.0f) {
            *out = IntLit(0);
            return true;
        }
        if (f == 1.0f) {
            *out = IntLit(1);
            return true;
        }
        if (f < 0.0f) {
            // The magnitude goes through the same shortening the engine's
            // assembler applies to the token `QscGen_WriteFloat` printed for it,
            // so -1.0 is PUSH1 + MINUS exactly as the engine's own compiler emits
            // it (a PUSHF 1.0 would be the same value but not the same bytes).
            const float magnitude = -f;
            *out = magnitude == 1.0f ? Negate(IntLit(1)) : Negate(FloatLit(magnitude));
            return true;
        }
        *out = FloatLit(f);
        return true;
    }

    case MenuValueKind::String:
        *out = StringLit(value.text);
        return true;

    case MenuValueKind::Name:
        *out = IdentLit(value.text);
        return true;

    case MenuValueKind::Boolean:
        // TRUE and FALSE are identifiers in the retail name tables, and
        // `Gen::emitExpr` pushes them as identifiers (qvm_compiler.cpp:203-208).
        if (value.text == "TRUE") {
            *out = BoolLit(true);
            return true;
        }
        if (value.text == "FALSE") {
            *out = BoolLit(false);
            return true;
        }
        *error = where + " is the boolean " + Quote(value.text) +
                 "; the engine's scripts use exactly TRUE and FALSE";
        return false;

    case MenuValueKind::Call:
        // `Level_NewTask` disables the `Task_New` symbol while it parses the
        // declared parameters (src/level/level.c:182-185, :209), so a call can
        // never be a declared parameter value.
        *error = where + " is a call to " + Quote(value.text) +
                 ", and a declared parameter cannot be a call";
        return false;
    }

    *error = where + " has an unknown value kind";
    return false;
}

// ─── the widget tree ─────────────────────────────────────────────────────────

const MenuQvmTypeDecl* FindDecl(const std::vector<const MenuQvmTypeDecl*>& decls,
                                const std::string& type) {
    for (const MenuQvmTypeDecl* d : decls) {
        if (d->name == type) {
            return d;
        }
    }
    return nullptr;
}

// `Task_New(id, type, name, <declared parameters...>, <child Task_New calls...>)`.
//
// `Level_NewTask` HOOK 0x4F0780 (src/level/level.c:168-246): `:187-190` reads the
// id, the type name and the instance name; `:214-221` consumes one argument per
// entry of the type's `ParamTable`, in order; `:234-239` then evaluates the
// REMAINING arguments, where a `Task_New` creates a child of this task. That is
// the whole argument layout, and it is what `MenuWidget` models.
bool BuildWidget(const MenuWidget& widget, const std::vector<const MenuQvmTypeDecl*>& decls,
                 std::unique_ptr<Node>* out, std::string* error) {
    if (widget.type.empty()) {
        *error = "a widget has no type, and Task_New's second argument is the type name";
        return false;
    }

    std::vector<std::unique_ptr<Node>> args;
    {
        std::unique_ptr<Node> id;
        MenuValue id_value;
        id_value.kind = MenuValueKind::Integer;
        id_value.integer = widget.id;
        if (!ValueToNode(id_value, "the id of " + Quote(widget.type), &id, error)) {
            return false;
        }
        args.push_back(std::move(id));
    }
    args.push_back(StringLit(widget.type));
    args.push_back(StringLit(widget.name));

    const MenuQvmTypeDecl* decl = FindDecl(decls, widget.type);
    if (decl == nullptr) {
        if (!widget.fields.empty()) {
            // A type with no `Task_DeclareParameters` has an empty C `ParamTable`
            // (the real case is `ControlsMenu`, src/widget/controlsmenu.c:81-101),
            // so its arguments after the name are children, not parameters.
            *error = Quote(widget.type) + " has " + std::to_string(widget.fields.size()) +
                     " field(s) but no Task_DeclareParameters declares it";
            return false;
        }
    } else if (widget.fields.size() != decl->fields.size()) {
        *error = Quote(widget.type) + " has " + std::to_string(widget.fields.size()) +
                 " field(s) but its declaration has " + std::to_string(decl->fields.size());
        return false;
    }

    for (const MenuField& field : widget.fields) {
        const std::string where = Quote(widget.type) + "." + Quote(field.name);
        if (field.values.size() != 1) {
            *error = where + " has " + std::to_string(field.values.size()) +
                     " value(s); a declared parameter is exactly one argument";
            return false;
        }
        std::unique_ptr<Node> value;
        if (!ValueToNode(field.values[0], where, &value, error)) {
            return false;
        }
        args.push_back(std::move(value));
    }

    for (const MenuWidget& child : widget.children) {
        std::unique_ptr<Node> child_call;
        if (!BuildWidget(child, decls, &child_call, error)) {
            return false;
        }
        args.push_back(std::move(child_call));
    }

    *out = CallNode("Task_New", std::move(args));
    return true;
}

// The whole script: every `Task_DeclareParameters` statement in order, then the
// single top-level `Task_New`. `Gen::emitProgram` appends the trailing `BRK` that
// `QscGen_Compile` writes at src/qvm/qscgen.c:134, and `Gen::emitStmt` appends the
// `POP` that `BLOCK_EXPR` writes at src/qvm/qscgen.c:259-264.
std::unique_ptr<Node> BuildProgram(const MenuQvmDocument& document, std::string* error) {
    std::vector<const MenuQvmTypeDecl*> decls;
    for (const MenuQvmTypeDecl& decl : document.declarations) {
        if (decl.name.empty()) {
            *error = "a Task_DeclareParameters has no type name";
            return nullptr;
        }
        for (const MenuQvmTypeDecl* seen : decls) {
            if (seen->name == decl.name) {
                *error = Quote(decl.name) + " is declared twice";
                return nullptr;
            }
        }
        decls.push_back(&decl);
    }

    auto program = NewNode(NodeKind::Program);

    for (const MenuQvmTypeDecl& decl : document.declarations) {
        std::vector<std::unique_ptr<Node>> args;
        args.push_back(StringLit(decl.name));
        for (const MenuField& field : decl.fields) {
            args.push_back(StringLit(field.name));
            args.push_back(StringLit(field.type));
        }
        program->children.push_back(ExprStmt(CallNode("Task_DeclareParameters", std::move(args))));
    }

    std::unique_ptr<Node> root;
    if (!BuildWidget(document.root, decls, &root, error)) {
        return nullptr;
    }
    program->children.push_back(ExprStmt(std::move(root)));
    return program;
}

}  // namespace

MenuQvmDocument MenuQvmDocumentFromDefinition(const MenuDefinition& definition) {
    MenuQvmDocument document;
    if (!definition.ok) {
        document.error = definition.error.empty() ? "the menu definition was not decoded"
                                                  : definition.error;
        return document;
    }

    document.root = definition.root;

    if (!definition.declarations.empty()) {
        // The script's own statement order, which is what a byte-faithful save-back
        // needs: the retail files declare `MenuFrame` before `MenuScreen`, while the
        // widget tree meets `MenuScreen` first.
        for (const MenuTypeDecl& decl : definition.declarations) {
            MenuQvmTypeDecl out;
            out.name = decl.name;
            for (const MenuField& field : decl.fields) {
                MenuField bare;
                bare.name = field.name;
                bare.type = field.type;
                out.fields.push_back(std::move(bare));
            }
            document.declarations.push_back(std::move(out));
        }
        document.ok = true;
        return document;
    }

    // No declaration list (a hand-built definition): recover it from the widgets
    // themselves. Depth-first pre-order; the first widget of a type supplies that
    // type's declaration, from the field names and types the reader already
    // attached to it. A type no widget uses is not declared, and a widget with no
    // fields declares nothing (retail `ControlsMenu` is exactly that case).
    std::vector<const MenuWidget*> stack{&document.root};
    std::vector<std::string> seen;
    while (!stack.empty()) {
        const MenuWidget* widget = stack.back();
        stack.pop_back();
        if (!widget->fields.empty() &&
            std::find(seen.begin(), seen.end(), widget->type) == seen.end()) {
            seen.push_back(widget->type);
            MenuQvmTypeDecl decl;
            decl.name = widget->type;
            for (const MenuField& field : widget->fields) {
                MenuField bare;
                bare.name = field.name;
                bare.type = field.type;
                decl.fields.push_back(std::move(bare));
            }
            document.declarations.push_back(std::move(decl));
        }
        for (auto it = widget->children.rbegin(); it != widget->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }

    document.ok = true;
    return document;
}

MenuQvmWriteResult MenuQvmWrite(const MenuQvmDocument& document) {
    MenuQvmWriteResult result;
    if (!document.ok) {
        result.error = document.error.empty() ? "the menu document is not usable" : document.error;
        return result;
    }

    std::string error;
    std::unique_ptr<Node> program = BuildProgram(document, &error);
    if (program == nullptr) {
        result.error = error;
        return result;
    }

    qvm::CompileResult compiled = qvm::Compile(*program);
    if (!compiled.ok) {
        result.error = "the engine's assembler refused the program: " + compiled.error;
        return result;
    }
    if (compiled.binary.empty()) {
        result.error = "the engine's assembler produced no bytes";
        return result;
    }

    result.bytes = std::move(compiled.binary);
    result.ok = true;
    return result;
}

bool MenuQvmWriteFile(const MenuQvmDocument& document, const std::string& path, std::string* error) {
    MenuQvmWriteResult result = MenuQvmWrite(document);
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) {
            *error = "cannot open " + Quote(path) + " for writing";
        }
        return false;
    }
    out.write(reinterpret_cast<const char*>(result.bytes.data()),
              static_cast<std::streamsize>(result.bytes.size()));
    if (!out) {
        if (error != nullptr) {
            *error = "failed while writing " + Quote(path);
        }
        return false;
    }
    return true;
}

}  // namespace igi
