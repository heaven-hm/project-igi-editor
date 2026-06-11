/******************************************************************************
 * @file    level_objects_internal.h
 * @brief   Private shared decls for the LevelObjects implementation modules
 *          (level_objects.cpp + level_objects_serialize.cpp): common includes
 *          and the QSC token formatting/parsing helpers used by both.
 *****************************************************************************/
#pragma once

#include "level_objects.h"
#include "logger.h"
#include "../utils.h"
#include <iostream>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

inline std::string TaskIdFromArg(const QSC::arg_s* a) {
    if (!a) return "";
    if (a->type_ == QSC::arg_s::type_t::STR) return a->str_;
    if (a->type_ == QSC::arg_s::type_t::DBL) return std::to_string((int)a->dbl_);
    if (a->type_ == QSC::arg_s::type_t::BOOL) return a->bool_ ? "1" : "0";
    return "";
}

inline std::string EscapeQscString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

inline std::string FormatQscDouble(double v) {
    char buf[64];

    // Large coordinates (>=1000): keep 2 decimal places, trim trailing zeros.
    // Small values: use Real32 precision (7 significant digits) so a value that
    // was originally a float like 0.95 or 1.54 round-trips cleanly instead of
    // printing double-precision noise (0.949999988079071, 1.5399999618530273).
    if (std::abs(v) >= 1000.0) {
        snprintf(buf, sizeof(buf), "%.2f", v);
    } else {
        // Cast through float first to collapse the double-precision noise, then
        // format with 7 significant digits (max lossless for Real32).
        float f = static_cast<float>(v);
        snprintf(buf, sizeof(buf), "%.7g", static_cast<double>(f));
    }

    std::string s(buf);
    // Strip trailing zeros after decimal point.
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s.empty() || s == "-0") s = "0";
    // Ensure the value is always recognisable as a float token (has . or e).
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

inline std::string FormatQscIntegerToken(const std::string& token) {
    std::string trimmed = Utils::Trim(token);
    if (trimmed.empty()) return trimmed;
    try {
        double value = std::stod(trimmed);
        int ivalue = (int)std::llround(value);
        return std::to_string(ivalue);
    } catch (...) {
        return trimmed;
    }
}

inline std::string ArgTokenFromArg(const QSC::arg_s* a) {
    if (!a) return "";
    switch (a->type_) {
        case QSC::arg_s::type_t::STR:
            return "\"" + std::string(a->str_ ? a->str_ : "") + "\"";
        case QSC::arg_s::type_t::DBL: {
            double v = a->dbl_;
            // Only write as a plain integer when the original token was an integer
            // literal (no '.' or 'e'). Float literals like 24316832.0 must keep
            // their decimal point so a re-serialized line matches the source format.
            double intPart;
            if (!a->is_float_ && std::modf(v, &intPart) == 0.0 &&
                v >= -2147483648.0 && v <= 2147483647.0) {
                return std::to_string((long long)intPart);
            }
            return FormatQscDouble(v);
        }
        case QSC::arg_s::type_t::BOOL:
            return a->bool_ ? "TRUE" : "FALSE";
        case QSC::arg_s::type_t::FUNC:
            return "";
    }
    return "";
}

inline void SplitTopLevelArgs(const std::string& text, std::vector<std::string>& outArgs) {
    outArgs.clear();
    std::string current;
    int parenDepth = 0;
    bool inQuote = false;
    bool escape = false;

    for (char c : text) {
        if (inQuote) {
            current.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inQuote = false;
            }
            continue;
        }

        if (c == '"') {
            inQuote = true;
            current.push_back(c);
            continue;
        }
        if (c == '(') {
            parenDepth++;
            current.push_back(c);
            continue;
        }
        if (c == ')') {
            if (parenDepth > 0) parenDepth--;
            current.push_back(c);
            continue;
        }
        if (c == ',' && parenDepth == 0) {
            outArgs.push_back(Utils::Trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    std::string tail = Utils::Trim(current);
    if (!tail.empty()) outArgs.push_back(tail);
}
