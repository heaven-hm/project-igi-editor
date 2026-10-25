#pragma once

// Reader for the game's compiled script containers ("LOOP" files, `.qvm`), and the
// menu model the retail menu system is built from.
//
// The oracle is IGI-RE: `src/qvm/qvmtypes.h` (the header layout and the loaded
// `Qvm` struct), `src/qvm/qvm.h` (the opcode list), `src/qvm/qvm.c` (the run
// handlers and `Qvm_PushString`/`Qvm_Pop`, which define the stack encoding) and
// `src/menu/menumanager.c` (how a menu script is turned into a live menu:
// `MenuManager_New`, HOOK 0x418B00, loads the script, calls `ScriptInit` and
// `Script_SetSymbolContext("Task_New", parent)`, then runs it -- so the menu tree
// is exactly the `Task_New`/`Task_DeclareParameters` calls the script makes).
//
// Nothing here executes script code. The decoder recognises the push opcodes that
// build literal arguments and the call opcode that consumes them, which is enough
// to recover the declared menu structure; anything it cannot account for is
// reported rather than guessed at.

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// IGI-RE `QvmHeader`, `src/qvm/qvmtypes.h`. Field names are the oracle's.
struct QvmHeaderInfo {
    std::string magic;         // "LOOP"
    int version = 0;           // 8 in every retail file
    int unk08 = 0;             // 5; older files are refused by the loader
    int sym_offsets = 0;
    int sym_text = 0;
    int sym_table_size = 0;    // bytes of int32 offsets
    int sym_text_size = 0;
    int str_offsets = 0;
    int str_text = 0;
    int str_table_size = 0;    // bytes of int32 offsets
    int str_text_size = 0;
    int code = 0;
    int code_size = 0;
    int stack_size = 0;
    int type_stack_size = 0;
};

// One decoded argument of a script call.
enum class QvmValueKind {
    Integer,
    Real,
    String,   // a literal from the string table
    Name,     // a symbol/identifier reference
    Boolean,  // TRUE / FALSE, which are symbols in the retail files
};

struct QvmValue {
    QvmValueKind kind = QvmValueKind::Integer;
    long long integer = 0;
    double real = 0.0;
    std::string text;  // String/Name payload, or the symbol name for Boolean

    bool IsNumber() const {
        return kind == QvmValueKind::Integer || kind == QvmValueKind::Real ||
               kind == QvmValueKind::Boolean;
    }
    double AsNumber() const {
        if (kind == QvmValueKind::Real) {
            return real;
        }
        if (kind == QvmValueKind::Boolean) {
            return text == "TRUE" ? 1.0 : 0.0;
        }
        return static_cast<double>(integer);
    }
    std::string AsText() const;
};

// A decoded call in the script's top-level body.
struct QvmCall {
    std::string symbol;            // e.g. "Task_New", "Task_DeclareParameters"
    std::vector<QvmValue> args;
    int pc = 0;                    // byte offset of the call opcode, for diagnostics
};

// The decoded form of one `.qvm` file.
struct QvmImage {
    bool ok = false;
    std::string error;             // human-readable reason when ok == false
    QvmHeaderInfo header;
    std::vector<std::string> symbols;   // the file's name table
    std::vector<std::string> strings;   // the file's string table
    std::vector<unsigned char> code;
    std::vector<QvmCall> calls;         // decoded top-level calls, in file order

    // Convenience: the first call whose symbol matches, or nullptr.
    const QvmCall* FindCall(const std::string& symbol) const;

    // Parses a "LOOP" container. `data`/`size` is the whole file.
    static QvmImage Parse(const unsigned char* data, size_t size);
    static QvmImage ParseFile(const std::string& path);
};

// A declared widget parameter, in declaration order. `name`/`type` come from the
// script's own `Task_DeclareParameters` strings, so the editor does not invent a
// schema: it reproduces the retail one.
struct MenuField {
    std::string name;
    std::string type;              // "Int32", "String256", "bool8", ...
    std::vector<QvmValue> values;  // one entry per value the call supplied
};

// One widget instance. Retail menus are a flat sequence of `Task_New` calls whose
// parent is the most recent widget of a containing type, so `children` is built by
// nesting: a `MenuFrame`/`MenuScreen` collects the items declared after it until
// the next widget of the same or a shallower level.
struct MenuWidget {
    std::string type;              // "MenuManager", "MenuScreen", "MenuFrame", ...
    std::string name;              // the widget's own name, when it has one
    std::vector<MenuField> fields;
    std::vector<MenuWidget> children;

    // The first field with this name, or nullptr.
    const MenuField* FindField(const std::string& field_name) const;
    // Text of the first field with this name, or an empty string.
    std::string FieldText(const std::string& field_name) const;
};

// The whole menu a `.qvm` describes.
struct MenuDefinition {
    bool ok = false;
    std::string error;
    std::vector<MenuField> manager_fields;  // the MenuManager's own parameters
    MenuWidget root;                        // type == "MenuManager" when ok
    int widget_count = 0;                   // every widget, including nested

    // Builds the definition from a decoded image: reads the widget type
    // declarations, then turns each `Task_New` into a `MenuWidget`.
    static MenuDefinition FromImage(const QvmImage& image);
    static MenuDefinition FromFile(const std::string& path);

    // Every widget, depth-first, as type/name pairs; useful for tests and for the
    // editor's menu listing.
    std::vector<const MenuWidget*> Flatten() const;
};

}  // namespace igi
