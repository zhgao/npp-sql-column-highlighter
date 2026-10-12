#pragma once
// 测试专用：把 ::SendMessage 重定向到 FakeSendMessage（伪造的 Scintilla）。
// 用法：cl /FI"tests/fake_sci.h" 编译 Highlighter.cpp（ shipped 代码本身不用改）。
#include <windows.h>
#ifdef SendMessage
#undef SendMessage
#endif
#define SendMessage FakeSendMessage
LRESULT WINAPI FakeSendMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
