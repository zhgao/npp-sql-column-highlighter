// Scintilla.h — 本插件用到的 Scintilla 常量与结构的最小定义。
// 消息号来自 Scintilla.h（长期稳定）；SCNotification 只取用到的字段，
// 布局与官方一致：nmhdr 在前，position 紧随其后。
#pragma once

#include <windows.h>
#include <cstdint>

#define SCI_GETTEXTRANGE 2166
#define SCI_GETLENGTH 2006
#define SCI_GETCURRENTPOS 2008
#define SCI_GETANCHOR 2009
#define SCI_INDICSETSTYLE 2080
#define SCI_INDICSETFORE 2082
#define SCI_INDICSETALPHA 2083
#define SCI_INDICSETOUTLINEALPHA 2208
#define SCI_SETINDICATORCURRENT 2500
#define SCI_INDICATORFILLRANGE 2504
#define SCI_INDICATORCLEARRANGE 2505

#define SCN_UPDATEUI 2007
#define SCN_DOUBLECLICK 2103

#define INDIC_ROUNDBOX 7

typedef intptr_t Sci_Position;

struct Sci_CharacterRange {
    Sci_Position cpMin;
    Sci_Position cpMax;
};

struct Sci_TextRange {
    Sci_CharacterRange chrg;
    char* lpstrText;
};

// 只用到 nmhdr（事件来源）与 position（SCN_DOUBLECLICK 的双击位置）。
struct SCNotification {
    NMHDR nmhdr;
    Sci_Position position;
};
