/******************************************************************************
 * @file    qsc_lexer.cpp
 * @brief   QSC tokenizer -- faithful C++ transcription of the retail engine's
 *          script reader/tokenizer.
 *
 * Engine call chain reproduced here (IGI-RE is the source of truth):
 *
 *   Reader_InitMem    0x4BF1C0  src/qsc/reader.c:113-117
 *   Reader_ReadLine   0x4BEFE0  src/qsc/reader.c:36-111    line + "//" strip
 *   Lexer_Next        0x4BFC70  src/qsc/lexer.c:38-245     operator dispatch
 *   Lex_ReadToken     0x517260  src/terrdraw/lexer.c:23-137 word/string/number
 *   Lexer_IsNumber    0x4C0170  src/qsc/lexer.c:252-265
 *   Lexer_IsInteger   0x4C01D0  src/qsc/lexer.c:267-277
 *   Str_ToInt         0x517510  src/terrdraw/lexer.c:142-152
 *   Str_ToFloat       0x517570  src/terrdraw/lexer.c:154-157
 *   gScriptLineSize   0xA969C4  = 0x800, set by Script_Init 0x4B8B20
 *                               (src/script/script.c:375)
 *
 * The public qsc::Lex()/Token API is unchanged; only the scanning rules are.
 * Deviations from the engine are marked "DEVIATION" and listed in the module
 * report.
 *****************************************************************************/

#include "qsc_lexer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace qsc {

namespace {

// ---------------------------------------------------------------------------
// engine constants
// ---------------------------------------------------------------------------

// gScriptLineSize (BSS 0xA969C4).  Script_Init (HOOK 0x4B8B20,
// src/script/script.c:375) calls Script_SetLineSize(0x800).
constexpr int kScriptLineSize = 0x800;

// src/qsc/reader.c:31-34 (static int IsLineEnd)
bool IsLineEnd(char c) {
    return c == '\r' || c == '\n' || c == '\0';
}

// ---------------------------------------------------------------------------
// Reader -- src/qsc/qsc.h:20-31
// ---------------------------------------------------------------------------

// The engine's Reader.  Only the in-memory form (Reader_InitMem) is needed;
// the editor has no file-backed refill, so `refill` is not modelled.
struct EngineReader {
    char*       cur;
    char*       buf;
    int         line;
    int         textline;
    int         size;
    int         pos;
    const char* src;
};

// Reader_ReadLine, HOOK 0x4BEFE0, src/qsc/reader.c:36-111.
// Returns 1 when a line was produced (an empty line counts), 0 at end of
// source.  The over-long-line case is the engine's Script_Error (0x4B7E10).
//
// NOTE: the quoted-region loop below is transcribed verbatim.  It is entered
// with c == '"' and therefore breaks on its first check, so as written it never
// skips over a quoted region -- exactly what reader.c:88 documents ("starts on
// the opening quote and stops there, so "//" is cut even inside strings").
// The observable rule of the whole strip block is therefore: the first "//"
// anywhere on the line ends the line.  Nothing is rewritten; in particular no
// backslash is touched.
int Reader_ReadLine(EngineReader& r, std::string* error) {
    char* s;
    char c;
    int n, i;

    if (!IsLineEnd(*r.cur))
        return 1;
    if (r.pos >= r.size)
        return 0;

    for (n = 0; n < kScriptLineSize - 2 && r.pos + n < r.size; n++) {
        c = r.src[r.pos + n];
        r.buf[n] = c;
        r.textline = r.line;
        if (IsLineEnd(c)) {
            r.buf[n] = '\0';
            while (IsLineEnd(c)) {
                if (c == '\n')
                    r.line++;
                n++;
                if (r.pos + n >= r.size)
                    break;
                c = r.src[r.pos + n];
            }
            r.pos += n;
            goto strip;
        }
    }
    if (n >= kScriptLineSize - 2) {
        /* line too long: cut it at the last blank */
        while (n != 0) {
            c = r.buf[--n];
            if (c == ' ' || c == '\t') {
                r.buf[n] = '\0';
                break;
            }
        }
        if (n == 0 && error)
            *error = "Token too big in line " + std::to_string(r.textline) +
                     " '" + std::string(r.buf) + "'.";
    }
    r.pos += n;
    r.buf[n] = '\0';

strip:
    r.cur = s = r.buf;
    if (*s == '\0')
        return 1;
    i = 0;
    do {
        c = s[i];
        if (c == '"') {
            /* starts on the opening quote and stops there, so "//" is cut even inside strings */
            for (;;) {
                if (c == '\\') {
                    c = s[++i];
                    if (c == '\0') {
                        i--;
                        break;
                    }
                } else if (c == '"') {
                    break;
                }
                c = s[++i];
                if (c == '\0')
                    break;
            }
        }
        if (s[i] == '/' && s[i + 1] == '/') {
            s[i] = '\0';
            return 1;
        }
    } while (s[++i]);
    return 1;
}

// ---------------------------------------------------------------------------
// Lex_ReadToken -- src/terrdraw/lexer.c:23-137 (HOOK 0x517260)
// ---------------------------------------------------------------------------

// src/terrdraw/lexer.c:11-14 (static int hex_value)
int HexValue(signed char c) {
    return c <= '9' ? (c & 0xF) : (c & 0xF) + 9;
}

// Reads a quoted string (with \b \n \r \t \xNN escapes), a number (decimal,
// 0x hex, $ hex, with fraction and exponent) or a name at s[i] into out, and
// leaves i on the first character it did not consume.  Returns true when
// something was read.
//
// DEVIATION: the engine's output-buffer capacity checks (`if (!--size) return 0`)
// are omitted -- the editor stores the token in a std::string.
bool Lex_ReadToken(const char* s, size_t& i, std::string& out) {
    int found = 0, hex = 0, number = 0, exp = 0;
    char c;

    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')
        i++;
    if (s[i] == '"') {
        i++;
        while ((c = s[i]) != '"') {
            if (!c)
                return false;
            if (c == '\\') {
                c = s[++i];
                switch (c) {
                case 'X':
                case 'x':
                    c = (char)(HexValue(s[++i]) << 4);
                    c = (char)(c | HexValue(s[++i]));
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                }
            }
            out.push_back(c);
            i++;
        }
        i++;
        found = 1;
    } else {
        c = s[i];
        if (c >= '0' && c <= '9') {
            number = 1;
            if (c == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                i++;
                hex = 1;
                out.push_back('0');
                out.push_back(s[i++]);
            }
        } else if (c == '$') {
            i++;
            number = 1;
            hex = 1;
            out.push_back('$');
        }
        for (;;) {
            c = s[i];
            if (hex) {
                if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
                    break;
            } else if (number) {
                if (c == '.') {
                    out.push_back('.');
                    i++;
                    c = s[i];
                }
                if (c == 'e' || c == 'E') {
                    if (exp)
                        break;
                    out.push_back(c);
                    i++;
                    c = s[i];
                    if (c == '+' || c == '-') {
                        out.push_back(c);
                        i++;
                        c = s[i];
                    }
                    exp = 1;
                }
                if (!(c >= '0' && c <= '9'))
                    break;
            } else {
                if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && c != '_' && c != '.')
                    break;
            }
            out.push_back(c);
            i++;
            found = 1;
        }
    }
    return found != 0;
}

// ---------------------------------------------------------------------------
// number classification -- src/qsc/lexer.c:247-277
// ---------------------------------------------------------------------------

// src/qsc/lexer.c:247-250 (static int IsHexDigit)
bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Lexer_IsNumber, HOOK 0x4C0170, src/qsc/lexer.c:252-265
bool Lexer_IsNumber(const std::string& s) {
    if (s[0] >= '0' && s[0] <= '9')
        return true;
    if (s[0] == '$' && IsHexDigit(s[1]))
        return true;
    /* any word with 'X' as its second letter counts too */
    if ((s[0] == '0' && s[1] == 'x') || s[1] == 'X')
        return true;
    return false;
}

// Lexer_IsInteger, HOOK 0x4C01D0, src/qsc/lexer.c:267-277
bool Lexer_IsInteger(const std::string& s) {
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '.' || c == 'e' || c == 'E')
            return false;
    }
    return true;
}

// Str_ToInt, HOOK 0x517510, src/terrdraw/lexer.c:142-152
int Str_ToInt(const char* s) {
    int v = 0;

    if (*s != '$') {
        if (*s != '0' || (s[1] != 'x' && s[1] != 'X'))
            return std::atoi(s);
        s++;
    }
    for (s++; *s; s++)
        v = v * 16 + HexValue(*s);
    return v;
}

// Str_ToFloat, HOOK 0x517570, src/terrdraw/lexer.c:154-157 (crt_atof)
//
// crt_atof (0x516AD3) -> crt_fltin (0x516E83) -> _strgtold12 (0x4ADBF9) is a
// DECIMAL scanner: optional sign, digits, '.', exponent, nothing else.
// std::strtod additionally accepts C99 hex floats and inf/nan, so the engine
// reads "0xDEAD" as the leading '0' (0.0) where strtod returns 57005.  Reject
// those two forms, then defer to strtod for the decimal conversion.
double Str_ToFloat(const char* s) {
    if (!s) return 0.0;
    const char* p = s;
    while (std::isspace(static_cast<unsigned char>(*p))) ++p;
    if (*p == '+' || *p == '-') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) return 0.0;               // no hex floats
    if (p[0] == 'i' || p[0] == 'I' || p[0] == 'n' || p[0] == 'N') return 0.0;  // no inf/nan
    return std::strtod(s, nullptr);
}

// ---------------------------------------------------------------------------
// Lexer_Next -- src/qsc/lexer.c:38-245 (HOOK 0x4BFC70)
// ---------------------------------------------------------------------------

// DEVIATION: the engine's Token carries an integer op code; the editor's Token
// carries the operator's text, which its Pratt parser keys on.  The engine's
// "copies two characters but only skips one" quirk for a lone '!'
// (src/qsc/lexer.c:76-80) is not reproduced in the text -- a two-character
// lexeme would be unusable to the string-keyed parser -- but the advancement
// (skip = 1) is identical.
TokKind OperatorKind(const char* text) {
    if (text[0] == '(' && text[1] == '\0') return TokKind::LParen;
    if (text[0] == ')' && text[1] == '\0') return TokKind::RParen;
    if (text[0] == '{' && text[1] == '\0') return TokKind::LBrace;
    if (text[0] == '}' && text[1] == '\0') return TokKind::RBrace;
    if (text[0] == ';' && text[1] == '\0') return TokKind::Semicolon;
    if (text[0] == ',' && text[1] == '\0') return TokKind::Comma;
    return TokKind::Op;
}

// True for the lexemes Str_ToInt reads as hex rather than decimal.
// DEVIATION: the engine has no HexLit token (hex becomes TOK_INT); the editor's
// TokKind keeps HexLit, and its parser treats IntLit and HexLit identically.
bool IsHexLexeme(const std::string& s) {
    if (s[0] == '$')
        return true;
    return s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

// Tokenizes one line (the engine's r->cur buffer) into the editor's Token
// shape.  Faithful to Lexer_Next: SkipSpace, the operator switch, then
// Lex_ReadToken for words and quoted strings, then the number classification.
void LexLine(const char* buf, uint32_t line_no, LexResult& out) {
    size_t i = 0;

    for (;;) {
        // SkipSpace, src/qsc/lexer.c:32-36
        while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r')
            i++;
        if (buf[i] == '\0')
            break; // engine: Lexer_Next calls Reader_Refill here

        const size_t start = i;
        const uint32_t col = (uint32_t)start + 1;
        const char ch = buf[i];

        Token tok;
        tok.line = line_no;
        tok.col = col;

        const char* op_text = nullptr;
        int op_skip = 0;

        switch (ch) {
        case '!':
            if (buf[i + 1] == '=') { op_text = "!="; op_skip = 2; }
            else { op_text = "!"; op_skip = 1; }
            break;
        case '&':
            if (buf[i + 1] == '&') { op_text = "&&"; op_skip = 2; }
            else { op_text = "&"; op_skip = 1; }
            break;
        case '(': op_text = "("; op_skip = 1; break;
        case ')': op_text = ")"; op_skip = 1; break;
        case '*': op_text = "*"; op_skip = 1; break;
        case '+': op_text = "+"; op_skip = 1; break;
        case ',': op_text = ","; op_skip = 1; break;
        case '-': op_text = "-"; op_skip = 1; break;
        case '/': op_text = "/"; op_skip = 1; break;
        case ':': op_text = ":"; op_skip = 1; break;
        case ';': op_text = ";"; op_skip = 1; break;
        case '<':
            if (buf[i + 1] == '<') { op_text = "<<"; op_skip = 2; }
            else if (buf[i + 1] == '=') { op_text = "<="; op_skip = 2; }
            else { op_text = "<"; op_skip = 1; }
            break;
        case '=':
            if (buf[i + 1] == '=') { op_text = "=="; op_skip = 2; }
            else { op_text = "="; op_skip = 1; }
            break;
        case '>':
            if (buf[i + 1] == '>') { op_text = ">>"; op_skip = 2; }
            else if (buf[i + 1] == '=') { op_text = ">="; op_skip = 2; }
            else { op_text = ">"; op_skip = 1; }
            break;
        case '?': op_text = "?"; op_skip = 1; break;
        case '^': op_text = "^"; op_skip = 1; break;
        case '{': op_text = "{"; op_skip = 1; break;
        case '|':
            if (buf[i + 1] == '|') { op_text = "||"; op_skip = 2; }
            else { op_text = "|"; op_skip = 1; }
            break;
        case '}': op_text = "}"; op_skip = 1; break;
        case '~': op_text = "~"; op_skip = 1; break;
        default: break;
        }

        if (op_text) {
            tok.kind = OperatorKind(op_text);
            tok.lexeme = op_text;
            // engine "advance": for (; skip; skip--) if (*r->cur) r->cur++;
            for (int k = 0; k < op_skip; k++) {
                if (buf[i])
                    i++;
            }
            out.tokens.push_back(std::move(tok));
            continue;
        }

        // default: and '"' both go through Lex_ReadToken
        std::string value;
        if (!Lex_ReadToken(buf, i, value)) {
            // engine: Lexer_Next returns 0 (end of token stream) when
            // Lex_ReadToken read nothing.
            // DEVIATION: the editor reports it instead of silently truncating.
            out.ok = false;
            out.error = "lex error at " + std::to_string(line_no) + ":" + std::to_string(col) +
                        (ch == '"' ? ": unterminated string literal"
                                   : ": unexpected character '" + std::string(1, ch) + "'");
            return;
        }

        if (ch == '"') {
            tok.kind = TokKind::StringLit;
            tok.lexeme = value;
        } else {
            // DEVIATION: the engine keeps TRUE/FALSE/if/else/while as TOK_WORD
            // (they are registered constants/keywords resolved later); the
            // editor's parser keys on dedicated token kinds.
            if (value == "TRUE")       tok.kind = TokKind::KwTrue;
            else if (value == "FALSE") tok.kind = TokKind::KwFalse;
            else if (value == "if")    tok.kind = TokKind::KwIf;
            else if (value == "else")  tok.kind = TokKind::KwElse;
            else if (value == "while") tok.kind = TokKind::KwWhile;
            else if (Lexer_IsNumber(value)) {
                if (Lexer_IsInteger(value)) {
                    tok.kind = IsHexLexeme(value) ? TokKind::HexLit : TokKind::IntLit;
                    tok.int_val = Str_ToInt(value.c_str());
                } else {
                    tok.kind = TokKind::FloatLit;
                    tok.float_val = (float)Str_ToFloat(value.c_str());
                }
            } else {
                tok.kind = TokKind::Ident;
            }
            tok.lexeme = value;
        }
        out.tokens.push_back(std::move(tok));
    }
}

} // anonymous

// ---------------------------------------------------------------------------
// public adapter
// ---------------------------------------------------------------------------

LexResult Lex(const std::string& source) {
    LexResult r;

    // Reader_InitMem, HOOK 0x4BF1C0 (src/qsc/reader.c:113-117) + Reader_Init
    // 0x4BEF60 (src/qsc/reader.c:9-21): a zeroed gScriptLineSize line buffer.
    std::vector<char> buf((size_t)kScriptLineSize, '\0');
    EngineReader rd;
    rd.cur = buf.data();
    rd.buf = buf.data();
    rd.line = 0;
    rd.textline = 0;
    rd.size = (int)source.size();
    rd.pos = 0;
    rd.src = source.c_str();

    for (;;) {
        // Lexer_Next refills only once the current line buffer is spent.
        rd.cur = rd.buf + std::strlen(rd.buf);
        if (!Reader_ReadLine(rd, &r.error))
            break;
        if (!r.error.empty()) {
            r.ok = false;
            return r;
        }

        // DEVIATION: the engine's textline is 0-based; the editor's Token::line
        // is 1-based (it is only used for diagnostics).
        LexLine(rd.buf, (uint32_t)rd.textline + 1, r);
        if (!r.ok)
            return r;
    }

    Token end;
    end.kind = TokKind::End;
    end.line = (uint32_t)rd.line + 1;
    end.col = 1;
    r.tokens.push_back(std::move(end));
    return r;
}

const char* TokKindName(TokKind k) {
    switch (k) {
        case TokKind::End:       return "End";
        case TokKind::Ident:     return "Ident";
        case TokKind::KwTrue:    return "KwTrue";
        case TokKind::KwFalse:   return "KwFalse";
        case TokKind::KwIf:      return "KwIf";
        case TokKind::KwElse:    return "KwElse";
        case TokKind::KwWhile:   return "KwWhile";
        case TokKind::IntLit:    return "IntLit";
        case TokKind::HexLit:    return "HexLit";
        case TokKind::FloatLit:  return "FloatLit";
        case TokKind::StringLit: return "StringLit";
        case TokKind::LParen:    return "LParen";
        case TokKind::RParen:    return "RParen";
        case TokKind::LBrace:    return "LBrace";
        case TokKind::RBrace:    return "RBrace";
        case TokKind::Comma:     return "Comma";
        case TokKind::Semicolon: return "Semicolon";
        case TokKind::Op:        return "Op";
    }
    return "?";
}

} // namespace qsc
