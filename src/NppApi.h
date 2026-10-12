// NppApi.h — Notepad++ 插件接口的最小定义。
// 数值已对照官方 Notepad_plus_msgs.h 核实：
//   NPPMSG = (WM_USER + 1000)
//   NPPM_GETCURRENTSCINTILLA = (NPPMSG + 4)
//   NPPN_FIRST = 1000
//   NPPN_READY = (NPPN_FIRST + 1)
//   NPPN_BUFFERACTIVATED = (NPPN_FIRST + 10)
#pragma once

#include <windows.h>
#include <tchar.h>

#define NPPMSG (0x0400 + 1000)
#define NPPM_GETCURRENTSCINTILLA (NPPMSG + 4)

#define NPPN_FIRST 1000
#define NPPN_READY (NPPN_FIRST + 1)
#define NPPN_BUFFERACTIVATED (NPPN_FIRST + 10)

#define MAIN_VIEW 0
#define SUB_VIEW 1

struct NppData {
    HWND _nppHandle;
    HWND _scintillaMainHandle;
    HWND _scintillaSecondHandle;
};

typedef void (*PFUNCPLUGINCMD)();

struct ShortcutKey {
    bool _isCtrl;
    bool _isAlt;
    bool _isShift;
    UCHAR _key;
};

struct FuncItem {
    TCHAR _itemName[64];
    PFUNCPLUGINCMD _pFunc;
    int _cmdID;
    bool _init2Check;
    ShortcutKey* _pShKey;
};
