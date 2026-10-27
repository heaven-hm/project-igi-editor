#pragma once

// The SAVE-BACK half of the QVM-driven in-game menu (issue #74): turns the menu
// model back into the exact byte image the engine's own menu scripts have.
//
// ORACLE: IGI-RE, a verified C decompilation of retail IGI.exe. The engine's file
// producer is a three-stage chain, and this writer reproduces all three stages:
//
//   1. QSC source -> QVM assembler TEXT
//      `QscGen_Compile`      HOOK 0x4BBCB0  src/qvm/qscgen.c:86-142 (HOOK at :145)
//        * every top-level statement is `QscGen_Statement` on a BLOCK_EXPR, which
//          is `QscGen_Expression` then `OP_POP` (src/qvm/qscgen.c:259-264);
//        * then ONE trailing `OP_BRK` ends the program (src/qvm/qscgen.c:134);
//        * a call is `OP_PUSHI <callee>`, `OP_CALL(argPc...)`, `OP_BRA <end>`,
//          then one `BRK`-terminated block per argument in `argPc` order
//          (`QscGen_Expression` HOOK 0x4BC2F0, src/qvm/qscgen.c:297-407; the
//          call case is :322-345);
//        * a negative literal is a unary `OP_MINUS` applied to its magnitude,
//          because the QSC parser never hands `QscGen_WriteInt`
//          (HOOK 0x4BC880, src/qvm/qscgen.c:441-448) a negative value -- measured
//          over the whole installed corpus: 41497 `MINUS` opcodes, every one of
//          them immediately preceded by a magnitude push, and `PUSHM` 0 times.
//   2. assembler text -> instruction bytes
//      `QvmAsm_OpPush`       HOOK 0x4C0A10  src/qasm/asm.c:74-124 (HOOK at :125)
//        * integer (:79-98): 0 -> PUSH0, 1 -> PUSH1, -1 -> PUSHM, else
//          -128..127 -> PUSHB, -32768..32767 -> PUSHW, else PUSH (4-byte).  The
//          writer only ever emits non-negative magnitudes (see the MINUS note
//          above), so the -1 arm is never reached;
//        * float (:99-111): 0.0 -> PUSH0, 1.0 -> PUSH1, -1.0 -> PUSHM, else
//          PUSHF (4-byte);
//        * string / name (:113-121): index < 0x100 -> PUSHSIB / PUSHIIB,
//          index < 0x10000 -> PUSHSIW / PUSHIIW, else PUSHSI / PUSHII.
//      The pool INDEX is assigned on first use and de-duplicated:
//      `Asm_AddString` HOOK 0x4BB030 (src/script/asm.c:138-157) and `Asm_AddName`
//      HOOK 0x4BB180 (src/script/asm.c:183-200).
//   3. bytes + tables -> the "LOOP" container
//      `QvmAsm_Assemble`     HOOK 0x4BB270  src/qvm/qvmasm.c:103-276 (HOOK at :277)
//        * header at :227-251: magic "LOOP", version 8, unk08 5, then the three
//          section offset/size pairs (symbols, strings, code) and two zero words;
//        * `QvmAsm_WriteTable` at :88-99 writes the whole offset table first
//          (cumulative, one int32 per entry) and then the NUL-terminated texts;
//        * nothing is aligned and nothing is padded.
//
// NO FORMAT IS INVENTED HERE. Stages 2 and 3 already exist in this codebase as
// `qvm::Compile` (source/level/qvm_compiler.cpp), a translation of exactly those
// two engine functions -- `Gen::pushInt`/`pushIdent`/`pushString`
// (qvm_compiler.cpp:80-133) mirror `QvmAsm_OpPush`, `Gen::emitCall`
// (qvm_compiler.cpp:236-269) mirrors `QscGen_Expression`'s call layout, and
// `assemble` (qvm_compiler.cpp:376-445) mirrors `QvmAsm_Assemble`. This file is
// stage 1: it builds the QSC syntax tree (`qsc::Node`) that stage 2 consumes, in
// the shape the retail scripts have, and hands it to `qvm::Compile`.
//
// WHAT THE MENU MODEL MUST CARRY. The retail menu containers hold a script whose
// top-level statements are N `Task_DeclareParameters` calls followed by ONE
// `Task_New` (`docs/igi-re-parity/MENU_SYSTEM.md` sections 2 and 5). The widget
// tree is the argument tree of that `Task_New`:
//
//   Task_New(id, type, name, <declared parameters...>, <child Task_New calls...>)
//
// so `MenuWidget::fields` gives the declared parameters in order and
// `MenuWidget::children` gives the widgets nested in the argument positions after
// them -- the rule `Level_NewTask` HOOK 0x4F0780 implements at
// src/level/level.c:187-190 (`:187-190` id/type/name), `:214-221` (declared
// parameters, in `ParamTable` order) and `:234-239` (everything after them, where
// a `Task_New` creates a child).
//
// `MenuQvmDocument::declarations` is the list of `Task_DeclareParameters`
// statements to emit, in order. `MenuDefinition::declarations` carries it, so the
// ordinary path is byte-faithful; `MenuQvmDocumentFromDefinition` uses it and
// falls back to recovering the list from the widget tree in depth-first first-use
// order when a definition was built by hand. That fallback is complete for both
// retail files (every declared type is used) but is not the file's own statement
// order, so the emitted bytes then differ in the string-table order (measured on
// `ingamemenu.qvm`: same 11308 bytes, first difference at offset 151, inside the
// string pool).

#include <cstdint>
#include <string>
#include <vector>

#include "menu/qvm_image.h"

namespace igi {

// One `Task_DeclareParameters(typeName, field1Name, field1Type, ...)` statement.
struct MenuQvmTypeDecl {
    std::string name;
    std::vector<MenuField> fields;  // declaration order, no values
};

// Everything the writer needs: the declarations to emit and the widget tree.
struct MenuQvmDocument {
    std::vector<MenuQvmTypeDecl> declarations;
    MenuWidget root;
    bool ok = false;
    std::string error;
};

// The document for a decoded definition: the tree verbatim, and
// `MenuDefinition::declarations` verbatim. When that list is empty (a definition
// built by hand) it is recovered from the widgets' own field lists in depth-first
// first-use order, so a type no widget uses is then not declared; see the header
// note above for what that costs in byte fidelity.
MenuQvmDocument MenuQvmDocumentFromDefinition(const MenuDefinition& definition);

struct MenuQvmWriteResult {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> bytes;  // the complete "LOOP" container
};

// Serialises the document. Fails, with a message naming the widget, when the
// document cannot be represented as the engine's own script: a widget whose field
// count disagrees with its declaration, a declared field with more or fewer than
// one value, a call in a declared parameter position (which `Level_NewTask`
// refuses, src/level/level.c:209), an undeclared type carrying fields, or an
// integer whose magnitude does not fit the assembler's 32-bit literal.
MenuQvmWriteResult MenuQvmWrite(const MenuQvmDocument& document);

// Serialises and writes `path`. On failure returns false and sets `*error`.
bool MenuQvmWriteFile(const MenuQvmDocument& document, const std::string& path,
                      std::string* error = nullptr);

}  // namespace igi
