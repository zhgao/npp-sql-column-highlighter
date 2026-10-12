// PluginMain.cpp — Notepad++ 插件入口：菜单、通知分发。
//
// 行为：
//   * SCN_UPDATEUI（单击/光标移动）：光标落在 INSERT 列名上 → 高亮对应的值
//   * SCN_DOUBLECLICK：双击值 → 高亮对应的列；双击列名 → 高亮对应的值
//   * NPPN_BUFFERACTIVATED：切换文件时清除高亮
// 菜单里可整体启用/禁用。
#include <windows.h>
#include <tchar.h>
#include <cstring>

#include "NppApi.h"
#include "Scintilla.h"
#include "Highlighter.h"

namespace {

const TCHAR PLUGIN_NAME[] = TEXT("SQL Column Highlighter");

NppData nppData;
bool g_enabled = true;
int g_lastPos = -1;

enum { CMD_TOGGLE = 0, CMD_ABOUT = 1, NB_FUNCS = 2 };
FuncItem funcItems[NB_FUNCS];

HWND curScintilla() {
    int which = -1;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0,
                  reinterpret_cast<LPARAM>(&which));
    return (which == SUB_VIEW) ? nppData._scintillaSecondHandle
                               : nppData._scintillaMainHandle;
}

void handleCaret(HWND sci) {
    if (!g_enabled)
        return;
    const int pos = (int)::SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    if (pos == g_lastPos)
        return;
    g_lastPos = pos;
    const int anchor = (int)::SendMessage(sci, SCI_GETANCHOR, 0, 0);
    if (anchor != pos)
        return;  // 有选区（比如刚双击选中了单词）就不动
    sqlhl::HitInfo hit;
    if (sqlhl::analyse(sci, pos, hit) && hit.isColumn) {
        sqlhl::highlight(sci, hit);
    } else {
        sqlhl::clearHighlights(sci);
    }
}

void handleDoubleClick(HWND sci, int pos) {
    if (!g_enabled) {
        sqlhl::clearHighlights(sci);
        return;
    }
    g_lastPos = (int)::SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    sqlhl::HitInfo hit;
    if (sqlhl::analyse(sci, pos, hit)) {
        sqlhl::highlight(sci, hit);
    } else {
        sqlhl::clearHighlights(sci);
    }
}

void toggleEnabled() {
    g_enabled = !g_enabled;
    HWND sci = curScintilla();
    if (!g_enabled) {
        sqlhl::clearHighlights(sci);
    } else {
        g_lastPos = -1;
        handleCaret(sci);
    }
}

void showAbout() {
    TCHAR msg[1024];
    _stprintf_s(msg,
                TEXT("SQL Column Highlighter v1.0.0\r\n")
                TEXT("\r\n")
                TEXT("In an INSERT statement:\r\n")
                TEXT("  - Click a column name  -> its value(s) are highlighted\r\n")
                TEXT("  - Double-click a value -> its column is highlighted\r\n")
                TEXT("\r\n")
                TEXT("在 INSERT 语句中：\r\n")
                TEXT("  - 单击列名 → 高亮对应的值\r\n")
                TEXT("  - 双击值   → 高亮对应的列\r\n")
                TEXT("\r\n")
                TEXT("Status: %s\r\n")
                TEXT("https://github.com/zhgao/npp-sql-column-highlighter"),
                g_enabled ? TEXT("enabled") : TEXT("disabled"));
    ::MessageBox(nppData._nppHandle, msg, PLUGIN_NAME, MB_OK | MB_ICONINFORMATION);
}

void commandMenuInit() {
    memset(funcItems, 0, sizeof(funcItems));

    _tcscpy_s(funcItems[CMD_TOGGLE]._itemName, TEXT("Enable/Disable highlighting"));
    funcItems[CMD_TOGGLE]._pFunc = toggleEnabled;
    funcItems[CMD_TOGGLE]._init2Check = false;
    funcItems[CMD_TOGGLE]._pShKey = NULL;

    _tcscpy_s(funcItems[CMD_ABOUT]._itemName, TEXT("About..."));
    funcItems[CMD_ABOUT]._pFunc = showAbout;
    funcItems[CMD_ABOUT]._init2Check = false;
    funcItems[CMD_ABOUT]._pShKey = NULL;
}

}  // namespace

extern "C" {

__declspec(dllexport) void setInfo(NppData data) {
    nppData = data;
}

__declspec(dllexport) const TCHAR* getName() {
    return PLUGIN_NAME;
}

__declspec(dllexport) FuncItem* getFuncsArray(int* nbF) {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        commandMenuInit();
    }
    *nbF = NB_FUNCS;
    return funcItems;
}

__declspec(dllexport) void beNotified(SCNotification* notifyCode) {
    switch (notifyCode->nmhdr.code) {
        case NPPN_READY:
            sqlhl::setupIndicators(nppData._scintillaMainHandle);
            sqlhl::setupIndicators(nppData._scintillaSecondHandle);
            break;
        case NPPN_BUFFERACTIVATED:
            g_lastPos = -1;
            sqlhl::clearHighlights(curScintilla());
            break;
        case SCN_UPDATEUI:
            handleCaret((HWND)notifyCode->nmhdr.hwndFrom);
            break;
        case SCN_DOUBLECLICK:
            handleDoubleClick((HWND)notifyCode->nmhdr.hwndFrom,
                              (int)notifyCode->position);
            break;
        default:
            break;
    }
}

__declspec(dllexport) LRESULT messageProc(UINT /*Message*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/) {
    return 0;
}

__declspec(dllexport) BOOL isUnicode() {
    return TRUE;
}

}  // extern "C"
