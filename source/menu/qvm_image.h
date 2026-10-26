#pragma once

// The retail menu system as data: the menu the game's own compiled menu scripts
// declare, recovered from the "LOOP" containers in `menusystem/`.
//
// CONTAINER PARSING IS NOT DUPLICATED HERE. `source/level/qvm_parser.h` already
// parses the format (`QVM_Parse`) and is the idiom used across this codebase --
// `source/runtime/config_qvm.cpp:112`, `source/runtime/magic_object_registry.cpp:75`,
// `source/renderer/renderer_objects_metadata.cpp:19,208,244`,
// `source/renderer/renderer_objects_atta.cpp:28` and
// `source/runtime/runtime_world.cpp:1042` all do `QVM_Parse(path)` and then work
// with the resulting `QVMFile`. This header only turns that into a menu model.
//
// ORACLE: IGI-RE, a verified C decompilation of retail IGI.exe.
//   * `src/qvm/qvmtypes.h` -- the container header and the loaded `Qvm` struct.
//   * `src/qvm/qvm.c:641-689` `Qvm_OpCall` (HOOK 0x4BD880) -- the call encoding:
//     the callee name is pushed first, then `code[pc] == OP_CALL`, then
//     `argc = *(int32*)(code+pc+1)` and `argc` int32 code offsets, one per
//     argument, each pointing at the code that pushes that argument; the next
//     instruction is at `pc + argc*4 + 5`. `QVM_Parse` implements exactly this
//     (see `source/level/qvm_parser.cpp:436-468`).
//   * `src/level/level.c:182-239` `Level_NewTask` (HOOK 0x4F0780) -- the runtime
//     rule that makes the tree: after the declared parameters are parsed the task
//     becomes the `Task_New` symbol context and the REMAINING arguments are
//     consumed as children, so a widget's children are the `Task_New` calls in its
//     argument positions. During parameter parsing `gLevelTaskNewEnabled` is 0, so
//     a nested `Task_New` cannot appear in a parameter position.
//   * `src/menu/menumanager.c:713-800` `MenuManager_New` (HOOK 0x418B00) -- how the
//     engine turns such a script into a live menu: load, `ScriptInit`,
//     `Script_SetSymbolContext("Task_New", parent)`, run.
//
// Nothing here executes script code.

#include <string>
#include <vector>

#include "level/qvm_parser.h"

namespace igi {

// One argument value of a declared widget parameter.
enum class MenuValueKind {
    Integer,
    Real,
    String,   // a literal from the container's string table
    Name,     // an identifier reference
    Boolean,  // TRUE / FALSE, which are identifiers in the retail files
    Call,     // the argument's code is itself a call; `text` is the callee symbol
};

struct MenuValue {
    MenuValueKind kind = MenuValueKind::Integer;
    long long integer = 0;
    double real = 0.0;
    std::string text;

    bool IsNumber() const {
        return kind == MenuValueKind::Integer || kind == MenuValueKind::Real ||
               kind == MenuValueKind::Boolean;
    }
    // Numeric view: integer as-is, real as-is, TRUE as 1 and FALSE as 0.
    double AsNumber() const;
    // Textual view: `text` for String/Name/Boolean/Call, decimal for Integer.
    std::string AsText() const;
};

// A declared widget parameter, in declaration order. `name` and `type` come from
// the script's own `Task_DeclareParameters` strings, so the editor reproduces the
// retail schema rather than inventing one.
struct MenuField {
    std::string name;
    std::string type;  // "Int32", "String256", "bool8", ...
    std::vector<MenuValue> values;
};

// One widget type's parameter declaration, exactly as the script carries it:
// `Task_DeclareParameters(typeName, field1Name, field1Type, field2Name, ...)`, in
// the file's own statement order. The runtime's `Task_DeclareParameters` is a
// no-op returning 0 (`src/level/level.c:156-166`), so this is data the script
// holds for its own reader -- but its ORDER is the order `Level_NewTask` parses
// arguments in (`src/level/level.c:214-221`) and it matches the C `ParamTable`
// order (docs/igi-re-parity/MENU_SYSTEM.md section 7).
//
// The reader used to parse these statements into a local and throw them away; a
// widget's own `MenuField`s say what its arguments are, but only the declaration
// list says which types exist and in what order the statements appear, which a
// byte-faithful save-back has to reproduce (`menu/menu_qvm_writer.h`).
struct MenuTypeDecl {
    std::string name;
    std::vector<MenuField> fields;  // declaration order, no values
};

// One widget instance.
struct MenuWidget {
    // The task id: `Task_New`'s first argument. Retail menu scripts address
    // screens by this id (a popup screen is activated by number), and
    // `Level_NewTask` (`src/level/level.c:200-205`) treats -1 as "no level id".
    int id = -1;
    std::string type;  // "MenuManager", "MenuScreen", "MenuFrame", ...
    std::string name;
    std::vector<MenuField> fields;
    // Children, in declaration order: the `Task_New` calls in this widget's
    // argument positions (see the `Level_NewTask` note above).
    std::vector<MenuWidget> children;

    // The first field with this name, or nullptr.
    const MenuField* FindField(const std::string& field_name) const;
    // The first value of the first field with this name, or nullptr.
    const MenuValue* FindValue(const std::string& field_name) const;
    // Text of the first value of the first field with this name, or "".
    std::string FieldText(const std::string& field_name) const;
    // Numeric value of the first value of the first field with this name, or
    // `fallback` when the field is absent or not numeric.
    double FieldNumber(const std::string& field_name, double fallback = 0.0) const;

    // Mutable counterparts, for editing a widget and saving it back.
    MenuField* FindField(const std::string& field_name);
    MenuValue* FindValue(const std::string& field_name);
};

// The whole menu a container describes.
struct MenuDefinition {
    bool ok = false;
    std::string error;   // human-readable reason when ok == false
    std::string source;  // the path this was read from, when known
    MenuWidget root;     // type == "MenuManager" when ok
    int widget_count = 0;  // every widget, including nested
    // The script's own `Task_DeclareParameters` statements, in file order. Empty
    // for a definition built by hand; `menu/menu_qvm_writer.h` recovers an
    // equivalent list from the widget tree when it is empty.
    std::vector<MenuTypeDecl> declarations;

    // Parses a "LOOP" container with `QVM_Parse`, then builds the definition.
    static MenuDefinition FromQvmFile(const std::string& path);
    // Builds from an already-parsed container.
    static MenuDefinition FromQvm(const QVMFile& qvm);
    // Builds from decompiled QSC source (`QVM_DecompileToString`), for callers
    // that already hold the text.
    static MenuDefinition FromDecompiledSource(const std::string& source);

    // The declaration of this widget type, or nullptr when the script declares none.
    const MenuTypeDecl* FindType(const std::string& type) const;

    // Every widget, depth-first, including nested ones.
    std::vector<const MenuWidget*> Flatten() const;
    // Depth-first widget ids, in `Flatten()` order.
    std::vector<int> Ids() const;
    // The widget with this id, or nullptr.
    const MenuWidget* FindById(int id) const;
    // The first widget of this type, depth-first, or nullptr.
    const MenuWidget* FindByType(const std::string& type) const;

    // Mutable counterparts, for editing a definition and saving it back.
    std::vector<MenuWidget*> Flatten();
    MenuWidget* FindById(int id);
    MenuWidget* FindByType(const std::string& type);
};

}  // namespace igi
