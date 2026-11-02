#pragma once

#include "../renderer/fnt_parser.h"

struct PauseMenuFontMetrics {
    int lineHeight = 15;
    int rowHeight = 16;
    int boxHeight = 18;
    int charWidth = 7;
    float scale = 1.0f;
    bool bitmap = false;
};

FntFont& GetRetailPauseFont();
int RetailPauseTextWidth(const char* text, float scale = 1.0f);
uint64_t RetailPauseFontSourceStamp();

FntFont& GetEditorPauseFont();
int EditorPauseTextWidth(const char* text, float scale = 1.0f);
int PauseMenuTextWidth(const char* text, bool useEditorFont,
                       int systemFontSize, float layoutScale = 1.0f);
PauseMenuFontMetrics GetPauseMenuFontMetrics(bool useEditorFont,
                                             int systemFontSize);
