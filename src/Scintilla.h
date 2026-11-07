// Scintilla.h — 本插件用到的 Scintilla 常量与结构的最小定义。
// 消息号必须与官方 include/Scintilla.h 一字不差。2026-11-08 的教训：
// 这里曾把 SCI_GETTEXTRANGE 写成 2166——那其实是 SCI_LINEFROMPOSITION，
// 插件在真机上发的根本不是取文本消息，读回 83 个 \x00，高亮一次都没
// 工作过；而 Linux 假测试用的也是同一份头文件，错号对错号，全绿通过，
// 完全没发现。另有三处同类抄错一并修正（见下）。数字以 Scintilla 5.6.9
// 官方头文件为准，tests 里有回归断言钉住它们；以后加消息号先查官方
// 头文件，禁止凭记忆手写。
// SCNotification 只取用到的字段，布局与官方一致：nmhdr 在前，
// position 紧随其后。
#pragma once

#include <windows.h>
#include <cstdint>

#define SCI_GETTEXTRANGE 2162
#define SCI_GETTEXT 2182
#define SCI_SETTEXT 2181
#define SCI_GETLENGTH 2006
#define SCI_GETCURRENTPOS 2008
#define SCI_GETANCHOR 2009
#define SCI_INDICSETSTYLE 2080
#define SCI_INDICSETFORE 2082
#define SCI_INDICSETALPHA 2523
#define SCI_INDICSETOUTLINEALPHA 2558
#define SCI_SETINDICATORCURRENT 2500
#define SCI_INDICATORFILLRANGE 2504
#define SCI_INDICATORCLEARRANGE 2505

#define SCN_UPDATEUI 2007
#define SCN_DOUBLECLICK 2006

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
