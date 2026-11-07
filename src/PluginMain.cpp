// PluginMain.cpp — Notepad++ 插件入口：菜单、通知分发。
//
// 行为：
//   * SCN_UPDATEUI（单击/光标移动）：光标落在 INSERT 列名上 → 高亮对应的值
//   * SCN_DOUBLECLICK：双击值 → 高亮对应的列；双击列名 → 高亮对应的值
//   * NPPN_BUFFERACTIVATED：切换文件时清除高亮
// 菜单里可整体启用/禁用，另有“Highlight at caret”手动触发（绕过通知，
// 用于在真机上区分“通知没到”和“画不出来”）。
//
// v1.0.1 修复（2026-11-07 真机复现：v1.0.0 在 Windows 上点列名完全没高亮，
// 而解析器本身对同一段 SQL 在测试里是通的）：
//   1. 旧 handleCaret 在有选区时（anchor != pos）直接 return。双击会先产生
//      选区，于是单击路径失效；若 SCN_DOUBLECLICK 再晚到/丢了，就什么都不画。
//      现在有选区时改用选区起点 +1 作为探测点，列/值都可触发。
//   2. 旧版用写死的 indicator 8/9。v8.5.6 起官方推荐用 NPPM_ALLOCATEINDICATOR
//      申请，避免与 Notepad++ 自身及其他插件冲突；申请失败回退 8/9。
//   3. 增加调试日志 %TEMP%\SqlColumnHighlighter.log，记录每次通知、探测点和
//      analyse 结果，方便在用户机器上定位是通知、解析还是绘制的问题。
//   4. g_lastPos 的去重只在“上次已成功高亮同一探测点”时跳过，不再因一次
//      双击就把下一次单击也拦掉。
#include <windows.h>
#include <tchar.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#include "NppApi.h"
#include "Scintilla.h"
#include "Highlighter.h"

namespace {

const TCHAR PLUGIN_NAME[] = TEXT("SQL Column Highlighter");

NppData nppData;
bool g_enabled = true;
int g_lastProbe = -1;          // 上次成功高亮的探测点；-1 = 无
bool g_indicAllocated = false;

enum { CMD_TOGGLE = 0, CMD_HIGHLIGHT_NOW = 1, CMD_ABOUT = 2, NB_FUNCS = 3 };
FuncItem funcItems[NB_FUNCS];

HWND curScintilla() {
    int which = -1;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0,
                  reinterpret_cast<LPARAM>(&which));
    return (which == SUB_VIEW) ? nppData._scintillaSecondHandle
                               : nppData._scintillaMainHandle;
}

void debugLog(const char* fmt, ...) {
    char path[MAX_PATH];
    if (::GetTempPathA(MAX_PATH, path) == 0)
        return;
    size_t len = strlen(path);
    if (len + 32 >= MAX_PATH)
        return;
    strcpy_s(path + len, MAX_PATH - len, "SqlColumnHighlighter.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f)
        return;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fprintf(f, "%s\n", msg);
    fclose(f);
}

// 在指定探测点尝试高亮；返回 true 表示画上了。
// 失败时多记一行 diagnose（文档长度、语句预览、解析断在哪一步），
// v1.0.1 的日志只说了 analyse=0，没法区分是文本不对还是解析断了。
bool tryHighlightAt(HWND sci, int probe, const char* why) {
    sqlhl::HitInfo hit;
    const bool ok = sqlhl::analyse(sci, probe, hit);
    debugLog("%s probe=%d analyse=%d isColumn=%d colIdx=%d",
             why, probe, ok ? 1 : 0, ok ? (hit.isColumn ? 1 : 0) : -1,
             ok ? hit.colIdx : -1);
    if (ok) {
        sqlhl::highlight(sci, hit);
        g_lastProbe = probe;
        return true;
    }
    debugLog("diagnose %s", sqlhl::diagnose(sci, probe).c_str());
    sqlhl::clearHighlights(sci);
    g_lastProbe = -1;
    return false;
}

void handleCaret(HWND sci) {
    if (!g_enabled)
        return;
    const int pos = (int)::SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    const int anchor = (int)::SendMessage(sci, SCI_GETANCHOR, 0, 0);
    // 有选区（刚双击选中了一个词）时，探测点取选区起点 +1（词内），
    // 而不是直接 return；无选区时探测点就是光标位置。
    const int probe =
        (anchor != pos) ? ((anchor < pos ? anchor : pos) + 1) : pos;
    if (anchor == pos && probe == g_lastProbe)
        return;  // 同一位置已画过，不重复画
    if (anchor == pos) {
        // 单击：只在列名上画（单击值本身不触发，这是原有行为）。
        sqlhl::HitInfo hit;
        const bool ok = sqlhl::analyse(sci, probe, hit);
        if (ok && hit.isColumn) {
            debugLog("caret pos=%d analyse=1 isColumn=1 colIdx=%d", probe,
                     hit.colIdx);
            sqlhl::highlight(sci, hit);
            g_lastProbe = probe;
        } else {
            debugLog("caret pos=%d analyse=%d isColumn=%d", probe,
                     ok ? 1 : 0, ok ? (hit.isColumn ? 1 : 0) : -1);
            debugLog("diagnose %s", sqlhl::diagnose(sci, probe).c_str());
            sqlhl::clearHighlights(sci);
            g_lastProbe = -1;
        }
        return;
    }
    // 有选区：列和值都可触发（双击列名 / 双击值）。
    tryHighlightAt(sci, probe, "caret-sel");
}

void handleDoubleClick(HWND sci, int pos) {
    if (!g_enabled) {
        sqlhl::clearHighlights(sci);
        return;
    }
    // 双击后光标在词尾、选区起点在词首；通知里的 pos 是双击点（词内）。
    // 先试通知点，失败再试选区起点 +1，避免通知点恰好落在词边界上。
    g_lastProbe = -1;  // 别让去重拦住这次双击
    if (tryHighlightAt(sci, pos, "dclick"))
        return;
    const int cur = (int)::SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    const int anchor = (int)::SendMessage(sci, SCI_GETANCHOR, 0, 0);
    if (anchor != cur)
        tryHighlightAt(sci, (anchor < cur ? anchor : cur) + 1, "dclick-sel");
}

void toggleEnabled() {
    g_enabled = !g_enabled;
    g_lastProbe = -1;
    HWND sci = curScintilla();
    if (!g_enabled) {
        sqlhl::clearHighlights(sci);
    } else {
        handleCaret(sci);
    }
}

void highlightNow() {
    HWND sci = curScintilla();
    if (!g_enabled) {
        ::MessageBox(nppData._nppHandle,
                     TEXT("Highlighting is disabled.\r\n")
                     TEXT("Use \"Enable/Disable highlighting\" to turn it on."),
                     PLUGIN_NAME, MB_OK | MB_ICONWARNING);
        return;
    }
    const int pos = (int)::SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    const int anchor = (int)::SendMessage(sci, SCI_GETANCHOR, 0, 0);
    const int probe =
        (anchor != pos) ? ((anchor < pos ? anchor : pos) + 1) : pos;
    g_lastProbe = -1;
    sqlhl::HitInfo hit;
    const bool ok = sqlhl::analyse(sci, probe, hit);
    debugLog("manual probe=%d analyse=%d isColumn=%d colIdx=%d", probe,
             ok ? 1 : 0, ok ? (hit.isColumn ? 1 : 0) : -1,
             ok ? hit.colIdx : -1);
    if (!ok) {
        sqlhl::clearHighlights(sci);
        const std::string diag = sqlhl::diagnose(sci, probe);
        debugLog("diagnose %s", diag.c_str());
        TCHAR msg[1024];
        _stprintf_s(msg,
                    TEXT("No INSERT column/value under the caret ")
                    TEXT("(pos %d).\r\n\r\n")
                    TEXT("It needs: INSERT INTO t (col, ...) ")
                    TEXT("VALUES (...), (...);\r\n")
                    TEXT("Click a column name, or double-click a value."),
                    probe);
        ::MessageBox(nppData._nppHandle, msg, PLUGIN_NAME,
                     MB_OK | MB_ICONINFORMATION);
        return;
    }
    sqlhl::highlight(sci, hit);
    g_lastProbe = probe;
    TCHAR msg[512];
    _stprintf_s(msg,
                TEXT("Highlighted column %d (%s).\r\n")
                TEXT("If you can see blue/amber boxes now, the drawing ")
                TEXT("works and the click notification was the problem;\r\n")
                TEXT("if you still see nothing, the indicator drawing is ")
                TEXT("the problem — please send %%TEMP%%\\")
                TEXT("SqlColumnHighlighter.log."),
                hit.colIdx, hit.isColumn ? TEXT("clicked column")
                                         : TEXT("double-clicked value"));
    ::MessageBox(nppData._nppHandle, msg, PLUGIN_NAME,
                 MB_OK | MB_ICONINFORMATION);
}

void showAbout() {
    TCHAR msg[1024];
    _stprintf_s(msg,
                TEXT("SQL Column Highlighter v1.0.3\r\n")
                TEXT("\r\n")
                TEXT("In an INSERT statement:\r\n")
                TEXT("  - Click a column name  -> its value(s) are highlighted\r\n")
                TEXT("  - Double-click a value -> its column is highlighted\r\n")
                TEXT("  - Plugins -> SQL Column Highlighter -> ")
                TEXT("\"Highlight at caret\" forces a highlight now\r\n")
                TEXT("\r\n")
                TEXT("在 INSERT 语句中：\r\n")
                TEXT("  - 单击列名 → 高亮对应的值\r\n")
                TEXT("  - 双击值   → 高亮对应的列\r\n")
                TEXT("  - 菜单里的 \"Highlight at caret\" 可手动强制高亮一次\r\n")
                TEXT("\r\n")
                TEXT("Status: %s\r\n")
                TEXT("Log: %%TEMP%%\\SqlColumnHighlighter.log\r\n")
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

    _tcscpy_s(funcItems[CMD_HIGHLIGHT_NOW]._itemName, TEXT("Highlight at caret"));
    funcItems[CMD_HIGHLIGHT_NOW]._pFunc = highlightNow;
    funcItems[CMD_HIGHLIGHT_NOW]._init2Check = false;
    funcItems[CMD_HIGHLIGHT_NOW]._pShKey = NULL;

    _tcscpy_s(funcItems[CMD_ABOUT]._itemName, TEXT("About..."));
    funcItems[CMD_ABOUT]._pFunc = showAbout;
    funcItems[CMD_ABOUT]._init2Check = false;
    funcItems[CMD_ABOUT]._pShKey = NULL;
}

void allocateIndicators() {
    if (g_indicAllocated)
        return;
    int base = 0;
    const BOOL ok = ::SendMessage(nppData._nppHandle, NPPM_ALLOCATEINDICATOR, 2,
                                  reinterpret_cast<LPARAM>(&base));
    if (ok && base >= 0) {
        sqlhl::setIndicators(base, base + 1);
        g_indicAllocated = true;
        debugLog("allocateIndicators ok base=%d (value=%d column=%d)", base,
                 base, base + 1);
    } else {
        sqlhl::setIndicators(8, 9);
        debugLog("allocateIndicators FAILED, fallback to 8/9");
    }
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
            allocateIndicators();
            sqlhl::setupIndicators(nppData._scintillaMainHandle);
            sqlhl::setupIndicators(nppData._scintillaSecondHandle);
            debugLog("NPPN_READY indicators allocated=%d", g_indicAllocated ? 1 : 0);
            break;
        case NPPN_BUFFERACTIVATED:
            g_lastProbe = -1;
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
