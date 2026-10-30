#include "qvm_parser.h"
#include <fstream>
#include <cstring>

// ============================================================================
// The retail "LOOP" reader, transcribed from the engine.
//
// Every case below cites the handler Qvm_Init (HOOK 0x4BD430, src/qvm/qvm.c:
// 521-570) registers for that opcode, and the handler body that fixes the
// operand width and the advance of `vm->pc`.  Qvm_Execute (HOOK 0x4BCFD0,
// src/qvm/qvm.c:328-338) reads `op = vm->code[vm->pc]` and calls
// `gQvmHandlers[op][0]`, so the interpreter is the engine's reader of the
// bytes; the assembler (src/qasm/asm.c) and the disassembler (src/qasm/dis.c)
// are the other two opinions and are cited where they disagree.
//
// WHAT IS DELIBERATELY DIFFERENT FROM THE INTERPRETER:
//   * The whole code section is decoded, not just the reachable part: the
//     interpreter stops at the first BRK (Qvm_OpBrk sets `vm->stop`) or error.
//     Callers need the argument blocks that follow a BRK.  The per-instruction
//     reading is identical, which is why a linear walk of the section lands
//     exactly on `codeSize` in all 1043 retail containers.
//   * Qvm_Load and the handlers trust the header and the byte stream and read
//     the file buffer directly.  This reader validates every offset and length
//     against the file first, so a truncated or hostile container cannot read
//     out of bounds.  Those guards are marked "editor guard" below.
// ============================================================================

const char* QVM_OpName(QVMOpType op) {
    // The engine's table is `opnames[]` (HOOK 0x4BECB0, src/qsc/qvmop.c:
    // 498-512): 48 names, and "ILLEGAL" for an opcode at or above 48.
    static const char* names[] = {
        "BRK", "NOP", "PUSH", "PUSHB", "PUSHW", "PUSHF", "PUSHA", "PUSHS",
        "PUSHSI", "PUSHSIB", "PUSHSIW", "PUSHI", "PUSHII", "PUSHIIB", "PUSHIIW",
        "PUSH0", "PUSH1", "PUSHM", "POP", "RET",
        "BRA", "BF", "BT", "JSR", "CALL",
        "ADD", "SUB", "MUL", "DIV", "SHL", "SHR", "AND", "OR", "XOR",
        "LAND", "LOR", "EQ", "NE", "LT", "LE", "GT", "GE",
        "ASSIGN", "PLUS", "MINUS", "INV", "NOT",
        "BLK", "ILLEGAL"
    };
    int idx = static_cast<int>(op);
    if (idx >= 0 && idx < static_cast<int>(QVMOpType::OP_COUNT))
        return names[idx];
    return "UNKNOWN";
}

// Qvm_GetString (HOOK 0x4BEC40, src/qsc/qvmop.c:470-476):
//     if (i < t->count && t->offsets[i] < t->size) return t->data + t->offsets[i];
//     return NULL;
// `count` is the entry count (the header's byte size / 4); the engine's own
// `count` is the byte size, which lets it index past the offsets array.
// `offsets[index] >= 0` is an editor guard: the engine's test is only
// `t->offsets[i] < t->size`, so a negative offset yields `t->data - n`.
const char* QVMTextPool::Resolve(uint32_t index) const {
    if (index < count && offsets[index] >= 0 && offsets[index] < size)
        return data.data() + offsets[index];
    return nullptr;
}

std::string QVMTextPool::Get(uint32_t index) const {
    const char* s = Resolve(index);
    if (!s)
        return std::string();
    // Bounded by the text blob rather than by whatever NUL the engine's strlen
    // would find past its end.
    size_t remaining = static_cast<size_t>(size - offsets[index]);
    size_t n = 0;
    while (n < remaining && s[n] != '\0')
        ++n;
    return std::string(s, n);
}

// Helper: read a little-endian int32/uint16 from a byte buffer
static int32_t ReadI32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// Qvm_Load's pool wiring (src/qvm/qvm.c:472-483).  The engine enters this only
// when the table's BYTE size is non-zero (`if (h->symTableSize)`, :472), then
// sets offsets = file + table offset, data = file + text offset, size = text
// size and count = the table byte size.
static bool LoadPool(const uint8_t* buf, size_t buf_size,
                     int32_t table_off, int32_t text_off,
                     int32_t table_bytes, int32_t text_size,
                     const char* what, QVMTextPool& pool, std::string& error) {
    if (table_bytes == 0)
        return true;

    // Editor guard: Qvm_Load performs no bounds test.
    if (table_off < 0 || text_off < 0 || table_bytes < 0 || text_size < 0 ||
        static_cast<size_t>(table_off) + static_cast<size_t>(table_bytes) > buf_size ||
        static_cast<size_t>(text_off) + static_cast<size_t>(text_size) > buf_size) {
        error = std::string(what) + " pool extends beyond file";
        return false;
    }

    pool.table_bytes = table_bytes;
    pool.count = static_cast<uint32_t>(table_bytes) / 4u;  // entries, not bytes
    pool.size = text_size;
    pool.offsets.resize(pool.count);
    for (uint32_t i = 0; i < pool.count; ++i)
        pool.offsets[i] = ReadI32(buf + table_off + 4 * static_cast<int32_t>(i));
    pool.data.assign(reinterpret_cast<const char*>(buf + text_off),
                     static_cast<size_t>(text_size));
    return true;
}

QVMFile QVM_Parse(const std::string& filepath) {
    QVMFile qvm{};
    qvm.valid = false;

    // Read entire file into memory
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        qvm.error = "Failed to open file: " + filepath;
        return qvm;
    }

    auto file_size = file.tellg();
    // Qvm_Load: `size < sizeof(QvmHeader)` refuses the file (src/qvm/qvm.c:441).
    if (file_size < static_cast<std::streamoff>(sizeof(QVMHeader))) {
        qvm.error = "File too small to contain QVM header (need 60 bytes)";
        return qvm;
    }

    std::vector<uint8_t> data((size_t)file_size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), file_size);
    file.close();

    const uint8_t* buf = data.data();
    size_t buf_size = data.size();

    // Header, field for field (src/qvm/qvmtypes.h:4-20)
    std::memcpy(qvm.header.magic, buf, 4);
    qvm.header.version       = ReadI32(buf + 0x04);
    qvm.header.unk08         = ReadI32(buf + 0x08);
    qvm.header.symOffsets    = ReadI32(buf + 0x0C);
    qvm.header.symText       = ReadI32(buf + 0x10);
    qvm.header.symTableSize  = ReadI32(buf + 0x14);
    qvm.header.symTextSize   = ReadI32(buf + 0x18);
    qvm.header.strOffsets    = ReadI32(buf + 0x1C);
    qvm.header.strText       = ReadI32(buf + 0x20);
    qvm.header.strTableSize  = ReadI32(buf + 0x24);
    qvm.header.strTextSize   = ReadI32(buf + 0x28);
    qvm.header.code          = ReadI32(buf + 0x2C);
    qvm.header.codeSize      = ReadI32(buf + 0x30);
    qvm.header.stackSize     = ReadI32(buf + 0x34);
    qvm.header.typeStackSize = ReadI32(buf + 0x38);

    // Qvm_Load (src/qvm/qvm.c:441): magic, then version 8, else NULL.
    if (std::memcmp(qvm.header.magic, "LOOP", 4) != 0) {
        qvm.error = "Invalid QVM signature (expected 'LOOP')";
        return qvm;
    }
    if (qvm.header.version != 8) {
        qvm.error = "Unsupported QVM version (expected 8, got " +
                    std::to_string(qvm.header.version) + ")";
        return qvm;
    }
    // Qvm_Load (src/qvm/qvm.c:443-446): `unk08 < 5` is refused; 5 or more is
    // accepted, so an exact-5 test would be stricter than the engine.
    if (qvm.header.unk08 < 5) {
        qvm.error = "Unsupported QVM header word 0x08 (expected >= 5, got " +
                    std::to_string(qvm.header.unk08) + ")";
        return qvm;
    }
    // `unk08` is the bytecode MINOR version.  The engine accepts anything >= 5
    // and then decodes with the version-5 opcode table only, so a container
    // written by a toolchain that used another minor is accepted and then
    // misread.  Every retail IGI1 container carries 5; record the others as a
    // non-fatal note so the decode error names the real cause.
    //
    // NOTE the accept/reject decision is UNCHANGED — it is the engine's.  This
    // only explains a failure the engine would hit too.
    if (qvm.header.unk08 != 5) {
        qvm.warning =
            "QVM header word 0x08 is " + std::to_string(qvm.header.unk08) +
            ", not the retail 5.  The engine refuses only values below 5 "
            "(src/qvm/qvm.c:443-446) and always decodes with its version-5 "
            "opcode table, so a container that uses another bytecode minor is "
            "accepted here and its instructions are then misread.  The "
            "mod-author toolchain reads this word as ver_minor and switches "
            "opcode tables on it (backend/NeonixTools/src/Qvm.cs:213-224, "
            "46-50); this reader does not, because IGI-RE has no such table for "
            "IGI1.";
    }

    // The two pools (src/qvm/qvm.c:472-483).  The engine keys both on the
    // table's byte size, not on the text size.
    if (!LoadPool(buf, buf_size, qvm.header.symOffsets, qvm.header.symText,
                  qvm.header.symTableSize, qvm.header.symTextSize,
                  "identifier", qvm.name_pool, qvm.error))
        return qvm;
    if (!LoadPool(buf, buf_size, qvm.header.strOffsets, qvm.header.strText,
                  qvm.header.strTableSize, qvm.header.strTextSize,
                  "string", qvm.string_pool, qvm.error))
        return qvm;

    qvm.identifiers.reserve(qvm.name_pool.count);
    for (uint32_t i = 0; i < qvm.name_pool.count; ++i)
        qvm.identifiers.push_back(qvm.name_pool.Get(i));
    qvm.strings.reserve(qvm.string_pool.count);
    for (uint32_t i = 0; i < qvm.string_pool.count; ++i)
        qvm.strings.push_back(qvm.string_pool.Get(i));

    // Qvm_Load sets `vm->code = file + h->code` and `vm->codeSize = h->codeSize`
    // with no validation (src/qvm/qvm.c:459-460); the bounds test is the
    // editor's guard.
    if (qvm.header.codeSize > 0) {
        if (qvm.header.code < 0 ||
            static_cast<size_t>(qvm.header.code) + static_cast<size_t>(qvm.header.codeSize) > buf_size) {
            qvm.error = "Code section extends beyond file";
            return qvm;
        }

        const uint8_t* code = buf + qvm.header.code;
        const uint32_t code_size = static_cast<uint32_t>(qvm.header.codeSize);
        uint32_t pos = 0;

        while (pos < code_size) {
            QVMInstruction instr{};
            instr.address = pos;
            instr.operand = 0;
            instr.signed_operand = 0;
            instr.operand_float = 0.0f;

            const uint8_t opbyte = code[pos];
            // Qvm_Execute indexes gQvmHandlers[op][0] with no bounds test
            // (src/qvm/qvm.c:332-334); the table is QVM_OPCODES = 0x31 entries
            // (src/qvm/qvm.h:48), so 0x30 (ILLEGAL) is the highest defined
            // opcode.  The test is the editor's guard.
            if (opbyte > static_cast<uint8_t>(QVMOpType::ILLEGAL)) {
                qvm.error = "Unknown opcode 0x" + std::to_string(opbyte) +
                            " at offset " + std::to_string(pos);
                qvm.error_code = QVM_ERR_INTERNAL;
                return qvm;
            }

            instr.type = static_cast<QVMOpType>(opbyte);
            const uint8_t* p = code + pos;  // p[0] = opcode, p[1..] = operand
            uint32_t total = 0;             // bytes consumed, opcode included

            switch (instr.type) {
            // ── one byte, then `vm->pc++` ────────────────────────────────────
            case QVMOpType::NOP:    // Qvm_OpNop   0x4BEC70 (src/qsc/qvmop.c:478-482)
            case QVMOpType::PUSH0:  // Qvm_OpPush0 0x4BE830 (src/qsc/qvmop.c:263-268)
            case QVMOpType::PUSH1:  // Qvm_OpPush1 0x4BE810 (src/qsc/qvmop.c:256-261)
            case QVMOpType::PUSHM:  // Qvm_OpPushM 0x4BE850 (src/qsc/qvmop.c:270-275)
            case QVMOpType::POP:    // Qvm_OpPop   0x4BD860 (src/qvm/qvm.c:633-638)
            // Qvm_OpBinary 0x4BDC40, `vm->pc++` at src/qsc/qvmop.c:196
            case QVMOpType::ADD:
            case QVMOpType::SUB:
            case QVMOpType::MUL:
            case QVMOpType::DIV:
            case QVMOpType::SHL:
            case QVMOpType::SHR:
            case QVMOpType::AND:
            case QVMOpType::OR:
            case QVMOpType::XOR:
            case QVMOpType::LAND:
            case QVMOpType::LOR:
            case QVMOpType::EQ:
            case QVMOpType::NE:
            case QVMOpType::LT:
            case QVMOpType::LE:
            case QVMOpType::GT:
            case QVMOpType::GE:
            // Qvm_OpAssign 0x4BDA60, `vm->pc++` on each success path
            // (src/qvm/qvm.c:766, :773, :779, :786)
            case QVMOpType::ASSIGN:
            // Qvm_OpUnary 0x4BE6A0, `vm->pc++` at src/qsc/qvmop.c:252
            case QVMOpType::PLUS:
            case QVMOpType::MINUS:
            case QVMOpType::INV:
            case QVMOpType::NOT:
                total = 1;
                break;

            // Qvm_OpBrk 0x4BEC80 (src/qsc/qvmop.c:484-488) sets `vm->stop` and
            // does not advance; the linear walk continues to the next block.
            case QVMOpType::BRK:
                total = 1;
                break;

            // ── PUSH and PUSHA: a 4-byte operand, `vm->pc += 5` ──────────────
            // Qvm_OpPush 0x4BE8F0 (src/qsc/qvmop.c:310-326) reads
            // `Read32(p + 1)`: for OP_PUSH it pushes the word, for OP_PUSHA it
            // calls Qvm_PushAddress, and both advance by 5.  The assembler
            // writes ONE byte for PUSHA (QvmAsm_OpPlain 0x4C0F50,
            // src/qasm/asm.c:157-164, and PUSHA is absent from the
            // QvmAsm_OpPush registrations at src/qasm/asm.c:46-57); the
            // interpreter is followed here.
            case QVMOpType::PUSH:
            case QVMOpType::PUSHA:
                if (pos + 5 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of " +
                                std::string(QVM_OpName(instr.type)) + " at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 5;
                instr.operand = ReadU32(p + 1);
                instr.signed_operand = ReadI32(p + 1);
                break;

            // Qvm_OpPushB 0x4BE870 (src/qsc/qvmop.c:277-288): `(signed char)p[1]`,
            // `vm->pc += 2`.
            case QVMOpType::PUSHB:
                if (pos + 2 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of PUSHB at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 2;
                instr.operand = p[1];
                instr.signed_operand = static_cast<int8_t>(p[1]);
                break;

            // Qvm_OpPushW 0x4BE8B0 (src/qsc/qvmop.c:291-304): a 2-byte short,
            // `vm->pc += 3`.
            case QVMOpType::PUSHW:
                if (pos + 3 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of PUSHW at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 3;
                instr.operand = ReadU16(p + 1);
                instr.signed_operand = static_cast<int16_t>(ReadU16(p + 1));
                break;

            // Qvm_OpPushF 0x4BE9A0 (src/qsc/qvmop.c:344-352): a 4-byte float,
            // `vm->pc += 5`.
            case QVMOpType::PUSHF: {
                if (pos + 5 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of PUSHF at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 5;
                instr.operand = ReadU32(p + 1);
                instr.signed_operand = ReadI32(p + 1);
                float f;
                std::memcpy(&f, p + 1, 4);
                instr.operand_float = f;
                break;
            }

            // Qvm_OpPushS 0x4BE9D0 (src/qsc/qvmop.c:354-372): the text follows
            // the opcode, `vm->pc += strlen(p + 1) + 2`.  The engine's strlen
            // runs past the code section if the text is not terminated; the
            // terminator test is the editor's guard.
            case QVMOpType::PUSHS:
            case QVMOpType::PUSHI: {
                uint32_t end = pos + 1;
                while (end < code_size && code[end] != 0)
                    ++end;
                if (end >= code_size) {
                    qvm.error = "Unterminated inline string at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                instr.inline_text.assign(
                    reinterpret_cast<const char*>(code + pos + 1), end - (pos + 1));
                total = (end - pos) + 1;  // opcode + text + NUL
                break;
            }

            // Qvm_OpPushIndexed 0x4BEA30 (src/qsc/qvmop.c:378-459) reads the
            // pool index and advances `vm->pc += n + 1`: 4 bytes for PUSHSI and
            // PUSHII, 1 for the B forms, 2 for the W forms.
            case QVMOpType::PUSHSI:
            case QVMOpType::PUSHII:
                if (pos + 5 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of " +
                                std::string(QVM_OpName(instr.type)) + " at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 5;
                instr.operand = ReadU32(p + 1);
                instr.signed_operand = ReadI32(p + 1);
                break;

            case QVMOpType::PUSHSIB:
            case QVMOpType::PUSHIIB:
                if (pos + 2 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of " +
                                std::string(QVM_OpName(instr.type)) + " at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 2;
                instr.operand = p[1];
                instr.signed_operand = static_cast<int8_t>(p[1]);
                break;

            case QVMOpType::PUSHSIW:
            case QVMOpType::PUSHIIW:
                if (pos + 3 > code_size) {
                    qvm.error = "Unexpected end of code reading the operand of " +
                                std::string(QVM_OpName(instr.type)) + " at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 3;
                instr.operand = static_cast<uint32_t>(p[1] | (p[2] << 8));
                instr.signed_operand = static_cast<int16_t>(p[1] | (p[2] << 8));
                break;

            // ── BRA, BF, BT: a 4-byte displacement, `vm->pc += disp + 5` ─────
            // Qvm_OpBranch 0x4BD730 (src/qvm/qvm.c:573-631): `disp =
            // *(int *)(p + 1)`, and the target is the opcode's pc + 5 + disp.
            case QVMOpType::BRA:
            case QVMOpType::BF:
            case QVMOpType::BT:
                if (pos + 5 > code_size) {
                    qvm.error = "Unexpected end of code reading the displacement of " +
                                std::string(QVM_OpName(instr.type)) + " at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                total = 5;
                instr.operand = ReadU32(p + 1);
                instr.signed_operand = ReadI32(p + 1);
                break;

            // ── CALL: a 4-byte count, then one address per argument ──────────
            // Qvm_OpCall 0x4BD880 (src/qvm/qvm.c:641-689): `argc = *(int *)(p + 1)`
            // at :652 and `vm->pc = pc + argc * 4 + 5` at :667.  `argc < 0` makes
            // the engine's pc move backwards; the test is the editor's guard.
            case QVMOpType::CALL: {
                if (pos + 5 > code_size) {
                    qvm.error = "Unexpected end of code in CALL operand count";
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                const int32_t argc = ReadI32(p + 1);
                if (argc < 0) {
                    qvm.error = "CALL at offset " + std::to_string(pos) +
                                " has a negative argument count (" + std::to_string(argc) + ")";
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                if (static_cast<int64_t>(argc) * 4 + 5 > static_cast<int64_t>(code_size) - pos) {
                    qvm.error = "Unexpected end of code in CALL arguments";
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                instr.operand = static_cast<uint32_t>(argc);
                instr.signed_operand = argc;
                instr.call_targets.resize(static_cast<size_t>(argc));
                for (int32_t i = 0; i < argc; ++i)
                    instr.call_targets[static_cast<size_t>(i)] = ReadI32(p + 5 + 4 * i);
                total = 5u + static_cast<uint32_t>(argc) * 4u;
                break;
            }

            // ── BLK: a 4-byte skip count, then the skipped block ─────────────
            // Qvm_OpBlk 0x4BE980 (src/qsc/qvmop.c:338-342):
            //     vm->pc += 5 + Read32(vm->code + vm->pc + 1);
            // The skipped bytes are data, not instructions, and are part of the
            // instruction here too.  The assembler writes ONE byte
            // (QvmAsm_OpPlain 0x4C0F50, src/qasm/asm.c:157-164) and the
            // disassembler steps one byte (src/qasm/dis.c:65-70); the
            // interpreter is followed here.  A skip that leaves the code section
            // is refused by the editor's guard -- the engine's pc would run past
            // the buffer.
            case QVMOpType::BLK: {
                if (pos + 5 > code_size) {
                    qvm.error = "Unexpected end of code reading the skip count of BLK at offset " +
                                std::to_string(pos);
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                const int32_t skip = ReadI32(p + 1);
                if (skip < 0 ||
                    5 + static_cast<int64_t>(skip) > static_cast<int64_t>(code_size) - pos) {
                    qvm.error = "BLK at offset " + std::to_string(pos) + " skips " +
                                std::to_string(skip) + " bytes, past the end of the code";
                    qvm.error_code = QVM_ERR_INTERNAL;
                    return qvm;
                }
                instr.operand = static_cast<uint32_t>(skip);
                instr.signed_operand = skip;
                total = 5u + static_cast<uint32_t>(skip);
                break;
            }

            // ── RET, JSR and ILLEGAL: the illegal instruction ────────────────
            // Qvm_Init registers Qvm_OpError for RET (src/qvm/qvm.c:544) and JSR
            // (:545) and leaves it for ILLEGAL; that is Qvm_OpIllegal (HOOK
            // 0x4BEC90, src/qsc/qvmop.c:490-496): it sets
            // `vm->error = QVM_ERR_ILLEGAL` and returns WITHOUT advancing pc, and
            // Qvm_Execute (`while (!vm->error)`, src/qvm/qvm.c:331) stops on the
            // first error.  There is therefore no next instruction to read: the
            // container cannot be walked, and Qvm_Run prints "Illegal
            // instruction" (src/qvm/qvm.c:390-392).  The assembler writes one
            // byte for both (QvmAsm_OpPlain 0x4C0F50, src/qasm/asm.c:157-164);
            // the interpreter is followed here.
            case QVMOpType::RET:
            case QVMOpType::JSR:
            case QVMOpType::ILLEGAL:
                qvm.error = "Illegal instruction";
                if (!qvm.warning.empty())
                    qvm.error += " (bytecode minor " +
                                 std::to_string(qvm.header.unk08) +
                                 "; the engine decodes version-5 bytecode only)";
                else if (static_cast<int>(opbyte) == static_cast<int>(QVMOpType::RET))
                    // The retail writer never emits a bare RET as the first
                    // instruction of a code section: every retail container
                    // opens with a callee push.  Report the byte so a foreign
                    // bytecode order is diagnosable rather than mysterious.
                    qvm.error += " (opcode 0x13 = RET at offset " +
                                 std::to_string(pos) + ")";
                qvm.error_code = QVM_ERR_ILLEGAL;
                return qvm;

            default:
                // Unreachable: every value of QVMOpType below OP_COUNT is handled.
                qvm.error = "Unhandled opcode 0x" + std::to_string(opbyte) +
                            " at offset " + std::to_string(pos);
                qvm.error_code = QVM_ERR_INTERNAL;
                return qvm;
            }

            instr.size = total;
            pos += total;
            qvm.instructions.push_back(std::move(instr));
        }
    }

    qvm.valid = true;
    return qvm;
}
