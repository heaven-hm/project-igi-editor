#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// The "LOOP" container, read the way the retail engine reads it.
//
// ORACLE: IGI-RE, the verified C decompilation of retail IGI.exe.
//   * `src/qvm/qvmtypes.h:4-20`  -- struct QvmHeader (the 60-byte file header)
//   * `src/qvm/qvmtypes.h:23-30` -- struct QvmStrings (one string/name pool)
//   * `src/qvm/qvm.c:433-486`    -- Qvm_Load (HOOK 0x4BD220): the validation,
//                                   the two pools and the code section
//   * `src/qsc/qvmop.c:470-476`  -- Qvm_GetString (HOOK 0x4BEC40): the only
//                                   pool reader
//   * `src/qvm/qvm.c:328-337`    -- Qvm_Execute (HOOK 0x4BCFD0) and
//                                   `src/qvm/qvm.c:521-570` Qvm_Init (HOOK
//                                   0x4BD430), which say what each opcode's
//                                   handler is, and therefore its operand width
//   * the opcode bodies in `src/qsc/qvmop.c`, cited per case in qvm_parser.cpp
//
// Nothing here executes script code; the decode is the same per-instruction
// reading the interpreter performs, applied to the whole code section.
// ============================================================================

// The engine's QvmHeader, field for field (`src/qvm/qvmtypes.h:4-20`).
// `sizeof(QvmHeader) == 60`, which is the size Qvm_Load compares the file
// against (`src/qvm/qvm.c:441`).
//
// `symTableSize` / `strTableSize` are BYTE sizes, not entry counts:
// `symOffsets + symTableSize == symText` and `symTableSize / 4` equals the
// number of NUL-terminated names in `symText`, in all 1043 retail containers.
// Qvm_Load nevertheless assigns the byte size to `QvmStrings::count`
// (`src/qvm/qvm.c:475`, `:481`), so the engine indexes past its own offsets
// array; QVMTextPool below carries the entry count instead.  See the report.
struct QVMHeader {
    char magic[4];          // 0x00  "LOOP"
    int32_t version;        // 0x04  8, else refused (src/qvm/qvm.c:441)
    int32_t unk08;          // 0x08  < 5 refused (src/qvm/qvm.c:443-446)
    int32_t symOffsets;     // 0x0C  byte offset of the name-offsets array
    int32_t symText;        // 0x10  byte offset of the name text
    int32_t symTableSize;   // 0x14  BYTE size of the name-offsets array
    int32_t symTextSize;    // 0x18  byte size of the name text
    int32_t strOffsets;     // 0x1C  byte offset of the string-offsets array
    int32_t strText;        // 0x20  byte offset of the string text
    int32_t strTableSize;   // 0x24  BYTE size of the string-offsets array
    int32_t strTextSize;    // 0x28  byte size of the string text
    int32_t code;           // 0x2C  byte offset of the code section
    int32_t codeSize;       // 0x30
    int32_t stackSize;      // 0x34  0 -> 0x800 (src/qvm/qvm.c:447-454)
    int32_t typeStackSize;  // 0x38  0 -> 0x100
};
static_assert(sizeof(QVMHeader) == 60, "QvmHeader is 60 bytes");

// The engine's error codes (`src/qvm/qvm.h:21-36`); Qvm_Run
// (`src/qvm/qvm.c:367-430`) maps each to the text it prints.
enum QVMError {
    QVM_ERR_UNDERFLOW = 1,
    QVM_ERR_OVERFLOW = 2,
    QVM_ERR_INTERNAL = 3,
    QVM_ERR_ILLEGAL = 5,
    QVM_ERR_OPERAND = 6,
    QVM_ERR_UNKNOWN_NAME = 7,
    QVM_ERR_NOT_VARIABLE = 8,
    QVM_ERR_NOT_FUNCTION = 9,
    QVM_ERR_NO_FUNCTION = 10,
    QVM_ERR_SYNTAX = 11,
    QVM_ERR_CONDITION = 12,
    QVM_ERR_ARGS = 13,
    QVM_ERR_DATABLOCK = 14,
    QVM_ERR_RANGE = 15,
};

// One of the engine's two pools: `Qvm.names` (the identifier table) or
// `Qvm.strings` (`src/qvm/qvmtypes.h:23-30`), wired by Qvm_Load
// (`src/qvm/qvm.c:472-483`) and read only by Qvm_GetString
// (`src/qsc/qvmop.c:470-476`):
//
//     if (i < t->count && t->offsets[i] < t->size) return t->data + t->offsets[i];
//     return NULL;
//
// `count` here is the ENTRY count (`<table byte size> / 4`); the engine's own
// `count` is the header's byte size, which over-reads the array.
struct QVMTextPool {
    std::vector<int32_t> offsets;  // t->offsets: one byte offset per entry
    std::string data;              // t->data: the text blob
    int32_t size = 0;              // t->size: byte size of the text blob
    uint32_t count = 0;            // t->count, as entries (table byte size / 4)
    int32_t table_bytes = 0;       // the header field as written, for reporting

    bool empty() const { return count == 0; }

    // Qvm_GetString: the C string for `index`, or nullptr when the engine's
    // bounds test fails.  The engine also admits negative indices (it has no
    // `i >= 0` test); a negative offset is refused here.
    const char* Resolve(uint32_t index) const;

    // Resolve(), copied out and bounded by the text blob rather than by the
    // first NUL byte past its end.
    std::string Get(uint32_t index) const;
};

enum class QVMOpType : uint8_t {
    BRK = 0, NOP, PUSH, PUSHB, PUSHW, PUSHF, PUSHA, PUSHS,
    PUSHSI, PUSHSIB, PUSHSIW, PUSHI, PUSHII, PUSHIIB, PUSHIIW,
    PUSH0, PUSH1, PUSHM, POP, RET,
    BRA, BF, BT, JSR, CALL,
    ADD, SUB, MUL, DIV, SHL, SHR, AND, OR, XOR,
    LAND, LOR, EQ, NE, LT, LE, GT, GE,
    ASSIGN, PLUS, MINUS, INV, NOT,
    BLK, ILLEGAL,
    OP_COUNT
};

struct QVMInstruction {
    QVMOpType type;
    uint32_t operand;                  // For simple operands
    int32_t signed_operand = 0;        // Signed immediate/displacement view
    float operand_float;               // For PUSHF
    std::string inline_text;            // Inline PUSHS/PUSHI payload
    std::vector<int32_t> call_targets; // For CALL
    uint32_t address;                  // Offset in code section
    uint32_t size;                     // Total instruction size
};

struct QVMFile {
    QVMHeader header;
    QVMTextPool name_pool;   // engine `Qvm.names`
    QVMTextPool string_pool; // engine `Qvm.strings`
    std::vector<std::string> identifiers;  // name_pool.Get(i), i in [0, count)
    std::vector<std::string> strings;      // string_pool.Get(i), i in [0, count)
    std::vector<QVMInstruction> instructions;
    bool valid = false;
    std::string error;
    // The engine's `vm->error` value when the decode stopped on one
    // (QVM_ERR_ILLEGAL for RET/JSR/ILLEGAL, QVM_ERR_RANGE for a pool index
    // that Qvm_GetString refuses); 0 when there is none.
    int error_code = 0;

    // A NON-FATAL note about the container.  Empty for every retail IGI1 file.
    //
    // `unk08` (the header word at 0x08) is a bytecode MINOR version, not an
    // arbitrary field: `Qvm_Load` HOOK 0x4BD220 refuses `< 5`
    // (src/qvm/qvm.c:443-446) and then decodes with ONE opcode table, the
    // version-5 one.  Every retail IGI1 container carries 5.  A third-party
    // container may carry another minor — 7 is what the mod-author toolchain
    // writes, read as `verMinor` at offset 8 and used to select the opcode
    // order (backend/NeonixTools/src/Qvm.cs:213-224 and :46-50) — and the
    // retail engine, and therefore this reader that follows it, accepts the
    // header and then misinterprets the first instruction.  When this is
    // non-empty the caller should show it instead of the bare decode error.
    std::string warning;

    uint32_t totalInstructions() const { return (uint32_t)instructions.size(); }
    uint32_t identifierCount() const { return (uint32_t)identifiers.size(); }
    uint32_t stringCount() const { return (uint32_t)strings.size(); }
};

// Parse a QVM file from disk
QVMFile QVM_Parse(const std::string& filepath);

// Get opcode name string
const char* QVM_OpName(QVMOpType op);
