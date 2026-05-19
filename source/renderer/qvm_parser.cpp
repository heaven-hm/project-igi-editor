#include "../pch.h"
#include "qvm_parser.h"
#include "../level/level_common.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <map>
#include <functional>

// Operand sizes for each opcode (in bytes following the opcode byte).
// -1 means special handling (CALL).
static int QVM_OperandSize(QVMOpType op) {
    switch (op) {
    case QVMOpType::PUSH:    return 4;
    case QVMOpType::PUSHB:   return 1;
    case QVMOpType::PUSHW:   return 2;
    case QVMOpType::PUSHF:   return 4;
    case QVMOpType::PUSHSI:  return 4;
    case QVMOpType::PUSHSIB: return 1;
    case QVMOpType::PUSHSIW: return 2;
    case QVMOpType::PUSHII:  return 4;
    case QVMOpType::PUSHIIB: return 1;
    case QVMOpType::PUSHIIW: return 2;
    case QVMOpType::BRA:     return 4;
    case QVMOpType::BF:      return 4;
    case QVMOpType::BT:      return 4;
    case QVMOpType::CALL:    return -1; // special
    default:                 return 0;
    }
}

const char* QVM_OpName(QVMOpType op) {
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

// Helper: read a little-endian uint32 from a byte buffer
static uint32_t ReadU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Helper: read a little-endian uint16 from a byte buffer
static uint16_t ReadU16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Helper: write a little-endian uint32 to a vector
static void WriteU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 24) & 0xFF);
}

// Helper: write a little-endian uint16 to a vector
static void WriteU16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
}

// Helper: split a buffer of null-terminated strings into a vector
static std::vector<std::string> SplitNullTerminated(const uint8_t* data, uint32_t size) {
    std::vector<std::string> result;
    if (!data || size == 0)
        return result;

    uint32_t start = 0;
    for (uint32_t i = 0; i < size; ++i) {
        if (data[i] == '\0') {
            if (i >= start) {
                result.emplace_back(reinterpret_cast<const char*>(data + start));
            }
            start = i + 1;
        }
    }
    return result;
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
    if (file_size < 60) {
        qvm.error = "File too small to contain QVM header (need 60 bytes)";
        return qvm;
    }

    std::vector<uint8_t> data((size_t)file_size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), file_size);
    file.close();

    const uint8_t* buf = data.data();
    size_t buf_size = data.size();

    // Parse header
    std::memcpy(qvm.header.signature, buf, 4);
    qvm.header.ver_major  = ReadU32(buf + 0x04);
    qvm.header.ver_minor  = ReadU32(buf + 0x08);
    qvm.header.of_itable  = ReadU32(buf + 0x0C);
    qvm.header.of_ivalue  = ReadU32(buf + 0x10);
    qvm.header.sz_itable  = ReadU32(buf + 0x14);
    qvm.header.sz_ivalue  = ReadU32(buf + 0x18);
    qvm.header.of_stable  = ReadU32(buf + 0x1C);
    qvm.header.of_svalue  = ReadU32(buf + 0x20);
    qvm.header.sz_stable  = ReadU32(buf + 0x24);
    qvm.header.sz_svalue  = ReadU32(buf + 0x28);
    qvm.header.of_ctable  = ReadU32(buf + 0x2C);
    qvm.header.sz_ctable  = ReadU32(buf + 0x30);
    qvm.header.unknown_1  = ReadU32(buf + 0x34);
    qvm.header.unknown_2  = ReadU32(buf + 0x38);

    // Validate signature
    if (std::memcmp(qvm.header.signature, "LOOP", 4) != 0) {
        qvm.error = "Invalid QVM signature (expected 'LOOP')";
        return qvm;
    }

    // Validate version
    if (qvm.header.ver_major != 8 || qvm.header.ver_minor != 5) {
        qvm.error = "Unsupported QVM version (expected 8.5, got " +
                    std::to_string(qvm.header.ver_major) + "." +
                    std::to_string(qvm.header.ver_minor) + ")";
        return qvm;
    }

    // Parse identifier values (ivalue)
    if (qvm.header.sz_ivalue > 0) {
        qvm.identifiers = SplitNullTerminated(buf + qvm.header.of_ivalue, qvm.header.sz_ivalue);
    }

    // Parse string values (svalue)
    if (qvm.header.sz_svalue > 0) {
        qvm.strings = SplitNullTerminated(buf + qvm.header.of_svalue, qvm.header.sz_svalue);
    }

    // Parse bytecode (code table)
    if (qvm.header.sz_ctable > 0) {
        const uint8_t* code = buf + qvm.header.of_ctable;
        uint32_t code_size = qvm.header.sz_ctable;
        uint32_t pos = 0;

        while (pos < code_size) {
            QVMInstruction instr{};
            instr.address = pos;
            instr.operand = 0;
            instr.operand_float = 0.0f;

            uint8_t opbyte = code[pos];
            if (opbyte > static_cast<uint8_t>(QVMOpType::ILLEGAL)) {
                qvm.error = "Unknown opcode 0x" + std::to_string(opbyte) + " at offset " + std::to_string(pos);
                return qvm;
            }

            instr.type = static_cast<QVMOpType>(opbyte);
            pos += 1;

            int op_size = QVM_OperandSize(instr.type);
            if (op_size == -1) { // CALL
                uint32_t count = ReadU32(code + pos);
                instr.operand = count;
                pos += 4;
                instr.call_targets.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                    instr.call_targets[i] = static_cast<int32_t>(ReadU32(code + pos));
                    pos += 4;
                }
                instr.size = 1 + 4 + count * 4;
            } else if (op_size > 0) {
                if (op_size == 4) {
                    uint32_t val = ReadU32(code + pos);
                    instr.operand = val;
                    if (instr.type == QVMOpType::PUSHF) std::memcpy(&instr.operand_float, &val, 4);
                } else if (op_size == 2) instr.operand = ReadU16(code + pos);
                else if (op_size == 1) instr.operand = code[pos];
                pos += (uint32_t)op_size;
                instr.size = 1 + (uint32_t)op_size;
            } else {
                instr.size = 1;
            }
            qvm.instructions.push_back(std::move(instr));
        }
    }

    qvm.valid = true;
    return qvm;
}

bool QVM_Write(const QVMFile& qvm, const std::string& filepath) {
    std::vector<uint8_t> ivalue_buf, svalue_buf, ctable_buf;
    std::vector<uint32_t> itable_offsets, stable_offsets;

    for (const auto& s : qvm.identifiers) {
        itable_offsets.push_back((uint32_t)ivalue_buf.size());
        for (char c : s) ivalue_buf.push_back((uint8_t)c);
        ivalue_buf.push_back(0);
    }
    for (const auto& s : qvm.strings) {
        stable_offsets.push_back((uint32_t)svalue_buf.size());
        for (char c : s) svalue_buf.push_back((uint8_t)c);
        svalue_buf.push_back(0);
    }

    for (const auto& instr : qvm.instructions) {
        ctable_buf.push_back(static_cast<uint8_t>(instr.type));
        int op_size = QVM_OperandSize(instr.type);
        if (op_size == -1) {
            WriteU32(ctable_buf, (uint32_t)instr.call_targets.size());
            for (int32_t target : instr.call_targets) WriteU32(ctable_buf, (uint32_t)target);
        } else if (op_size == 4) {
            if (instr.type == QVMOpType::PUSHF) {
                uint32_t val; std::memcpy(&val, &instr.operand_float, 4);
                WriteU32(ctable_buf, val);
            } else WriteU32(ctable_buf, instr.operand);
        } else if (op_size == 2) WriteU16(ctable_buf, (uint16_t)instr.operand);
        else if (op_size == 1) ctable_buf.push_back((uint8_t)instr.operand);
    }

    std::vector<uint8_t> file_buf;
    file_buf.resize(60, 0);
    std::memcpy(file_buf.data(), "LOOP", 4);
    
    auto write_at = [&](uint32_t offset, uint32_t val) {
        file_buf[offset+0] = val & 0xFF; file_buf[offset+1] = (val >> 8) & 0xFF;
        file_buf[offset+2] = (val >> 16) & 0xFF; file_buf[offset+3] = (val >> 24) & 0xFF;
    };

    write_at(0x04, qvm.header.ver_major);
    write_at(0x08, qvm.header.ver_minor);
    
    uint32_t pos = 60;
    write_at(0x0C, pos); write_at(0x14, (uint32_t)itable_offsets.size() * 4);
    for (uint32_t off : itable_offsets) { WriteU32(file_buf, off); pos += 4; }

    write_at(0x10, pos); write_at(0x18, (uint32_t)ivalue_buf.size());
    file_buf.insert(file_buf.end(), ivalue_buf.begin(), ivalue_buf.end()); pos += (uint32_t)ivalue_buf.size();

    write_at(0x1C, pos); write_at(0x24, (uint32_t)stable_offsets.size() * 4);
    for (uint32_t off : stable_offsets) { WriteU32(file_buf, off); pos += 4; }

    write_at(0x20, pos); write_at(0x28, (uint32_t)svalue_buf.size());
    file_buf.insert(file_buf.end(), svalue_buf.begin(), svalue_buf.end()); pos += (uint32_t)svalue_buf.size();

    write_at(0x2C, pos); write_at(0x30, (uint32_t)ctable_buf.size());
    file_buf.insert(file_buf.end(), ctable_buf.begin(), ctable_buf.end()); pos += (uint32_t)ctable_buf.size();

    write_at(0x34, qvm.header.unknown_1);
    write_at(0x38, qvm.header.unknown_2);

    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(file_buf.data()), file_buf.size());
    return true;
}

static std::string EscapeString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out + "\"";
}

struct VMValue {
    enum Type { NONE, INT, FLOAT, STR, IDENT, BLOCK };
    Type type = NONE;
    int32_t i = 0;
    float f = 0.0f;
    std::string s;
};

static std::string DecompileBlock(const QVMFile& qvm, uint32_t start_addr, int indent, bool is_argument = false) {
    std::string out;
    std::string pad(indent * 4, ' ');
    std::vector<VMValue> stack;

    auto getValueStr = [&](const VMValue& v) {
        if (v.type == VMValue::INT) return std::to_string(v.i);
        if (v.type == VMValue::FLOAT) {
            char buf[64];
            snprintf(buf, 64, "%.6f", v.f);
            std::string s = buf;
            while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
            return s;
        }
        if (v.type == VMValue::STR) return EscapeString(v.s);
        if (v.type == VMValue::IDENT) return v.s;
        return std::string("/*none*/");
    };

    size_t start_idx = 0;
    while (start_idx < qvm.instructions.size() && qvm.instructions[start_idx].address < start_addr) start_idx++;

    for (size_t i = start_idx; i < qvm.instructions.size(); ++i) {
        const auto& instr = qvm.instructions[i];
        switch (instr.type) {
        case QVMOpType::PUSH:
        case QVMOpType::PUSHB:
        case QVMOpType::PUSHW: { VMValue v; v.type = VMValue::INT; v.i = (int32_t)instr.operand; stack.push_back(v); break; }
        case QVMOpType::PUSH0: { VMValue v; v.type = VMValue::INT; v.i = 0; stack.push_back(v); break; }
        case QVMOpType::PUSH1: { VMValue v; v.type = VMValue::INT; v.i = 1; stack.push_back(v); break; }
        case QVMOpType::PUSHF: { VMValue v; v.type = VMValue::FLOAT; v.f = instr.operand_float; stack.push_back(v); break; }
        case QVMOpType::PUSHS: { VMValue v; v.type = VMValue::STR; if (instr.operand < qvm.strings.size()) v.s = qvm.strings[instr.operand]; stack.push_back(v); break; }
        case QVMOpType::PUSHSI:
        case QVMOpType::PUSHSIB:
        case QVMOpType::PUSHSIW:
        case QVMOpType::PUSHII:
        case QVMOpType::PUSHIIB:
        case QVMOpType::PUSHIIW: { VMValue v; v.type = VMValue::IDENT; if (instr.operand < qvm.identifiers.size()) v.s = qvm.identifiers[instr.operand]; stack.push_back(v); break; }
        case QVMOpType::CALL: {
            uint32_t argc = instr.operand;
            std::vector<VMValue> args;
            for (uint32_t j = 0; j < argc; ++j) { if (!stack.empty()) { args.push_back(stack.back()); stack.pop_back(); } }
            std::reverse(args.begin(), args.end());
            VMValue func; if (!stack.empty()) { func = stack.back(); stack.pop_back(); }

            std::string func_str;
            if (!is_argument) {
                func_str += pad;
            }
            func_str += func.s + "(";
            for (size_t j = 0; j < args.size(); ++j) func_str += getValueStr(args[j]) + (j == args.size() - 1 ? "" : ", ");

            if (!instr.call_targets.empty()) {
                for (size_t t = 0; t < instr.call_targets.size(); ++t) {
                    func_str += ", ";
                    if (!is_argument) {
                        func_str += "\n";
                    }
                    func_str += DecompileBlock(qvm, (uint32_t)instr.call_targets[t], indent + 1, true);
                }
            }
            func_str += ")";
            if (!is_argument) {
                func_str += ";\n";
            }
            out += func_str;
            break;
        }
        case QVMOpType::POP: if (!stack.empty()) stack.pop_back(); break;
        case QVMOpType::RET: return out;
        case QVMOpType::BRK: return out;
        default: break;
        }
    }
    return out;
}

std::string QVM_Decompile(const QVMFile& qvm) {
    if (!qvm.valid) return "// Invalid QVM: " + qvm.error;
    return DecompileBlock(qvm, 0, 0);
}

QVMFile QVM_CompileFromQSC(const QSC& qsc) {
    QVMFile qvm;
    qvm.header.ver_major = 8;
    qvm.header.ver_minor = 5;
    std::memcpy(qvm.header.signature, "LOOP", 4);

    auto addString = [&](const std::string& s) {
        auto it = std::find(qvm.strings.begin(), qvm.strings.end(), s);
        if (it != qvm.strings.end()) return (uint32_t)std::distance(qvm.strings.begin(), it);
        qvm.strings.push_back(s);
        return (uint32_t)qvm.strings.size() - 1;
    };
    auto addIdent = [&](const std::string& s) {
        auto it = std::find(qvm.identifiers.begin(), qvm.identifiers.end(), s);
        if (it != qvm.identifiers.end()) return (uint32_t)std::distance(qvm.identifiers.begin(), it);
        qvm.identifiers.push_back(s);
        return (uint32_t)qvm.identifiers.size() - 1;
    };

    std::function<void(const QSC::func_s*)> emitFunc = [&](const QSC::func_s* f) {
        if (!f) return;
        std::vector<const QSC::arg_s*> args;
        std::vector<const QSC::func_s*> local_bodies;
        for (auto* a = f->args_; a; a = a->next_) {
            if (a->type_ == QSC::arg_s::type_t::FUNC) local_bodies.push_back(a->func_);
            else args.push_back(a);
        }
        for (int i = (int)args.size() - 1; i >= 0; --i) {
            const auto* a = args[i];
            QVMInstruction ins{};
            if (a->type_ == QSC::arg_s::type_t::STR) { ins.type = QVMOpType::PUSHS; ins.operand = addString(a->str_); ins.size = 3; }
            else if (a->type_ == QSC::arg_s::type_t::DBL) { ins.type = QVMOpType::PUSHF; ins.operand_float = (float)a->dbl_; ins.size = 5; }
            else if (a->type_ == QSC::arg_s::type_t::BOOL) { ins.type = a->bool_ ? QVMOpType::PUSH1 : QVMOpType::PUSH0; ins.size = 1; }
            qvm.instructions.push_back(ins);
        }
        QVMInstruction pushf{}; pushf.type = QVMOpType::PUSHSIB; pushf.operand = addIdent(f->func_name_); pushf.size = 2;
        qvm.instructions.push_back(pushf);
        
        QVMInstruction call{}; call.type = QVMOpType::CALL; call.operand = (uint32_t)args.size();
        call.size = 1 + 4 + (uint32_t)local_bodies.size() * 4;
        qvm.instructions.push_back(call);
        size_t call_idx = qvm.instructions.size() - 1;
        
        QVMInstruction brk{}; brk.type = QVMOpType::BRK; brk.size = 1;
        qvm.instructions.push_back(brk);

        // Recursively emit local bodies
        for (const auto* child : local_bodies) {
            uint32_t addr = 0;
            for (const auto& ins : qvm.instructions) addr += ins.size;
            qvm.instructions[call_idx].call_targets.push_back((int32_t)addr);
            emitFunc(child);
        }
    };

    QVMInstruction top_push{}; top_push.type = QVMOpType::PUSHIIB; top_push.operand = 0; top_push.size = 2;
    qvm.instructions.push_back(top_push);
    QVMInstruction top_call{}; top_call.type = QVMOpType::CALL; top_call.operand = 0;
    top_call.size = 1 + 4 + qsc.GetRootFuncCount() * 4;
    for (int i = 0; i < qsc.GetRootFuncCount(); ++i) top_call.call_targets.push_back(0);
    qvm.instructions.push_back(top_call);
    QVMInstruction top_ret{}; top_ret.type = QVMOpType::RET; top_ret.size = 1;
    qvm.instructions.push_back(top_ret);

    for (int i = 0; i < qsc.GetRootFuncCount(); ++i) {
        uint32_t addr = 0;
        for (const auto& ins : qvm.instructions) addr += ins.size;
        qvm.instructions[1].call_targets[i] = (int32_t)addr;
        emitFunc(qsc.GetRootFunc(i));
        if (qvm.instructions.back().type != QVMOpType::RET) {
            QVMInstruction r{}; r.type = QVMOpType::RET; r.size = 1;
            qvm.instructions.push_back(r);
        }
    }
    qvm.valid = true;
    return qvm;
}
