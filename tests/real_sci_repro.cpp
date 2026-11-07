// tests/real_sci_repro.cpp — 真机复现：George 的 Notepad++ 上
// SCI_GETTEXTRANGE 写回的 83 字节全是 \x00（v1.0.2 diagnose 日志），
// 而 SCI_GETLENGTH 返回 83 是对的。
//
// 本程序在真实 Windows + 真实 Scintilla（Notepad++ 自带的 SciLexer.dll）
// 上重现那两个调用，用插件同款 src/Scintilla.h 里的结构体定义，
// 对比 SCI_GETTEXTRANGE 与 SCI_GETTEXT 的返回：
//   - 若 GETTEXTRANGE 在这里也返回全 \x00，说明故障与 Notepad++ 无关，
//     是 Scintilla 调用本身的问题，v1.0.3 改走 SCI_GETTEXT 是对的；
//   - 若 GETTEXTRANGE 在这里正常，说明故障只在 Notepad++ 宿主环境里，
//     同样证明改走 SCI_GETTEXT（更简单的调用）是正确的规避。
// 退出码：0 = SCI_GETTEXT 正确取回 SQL（v1.0.3 的取文本方式在真机上成立），
// 1 = SCI_GETTEXT 也失败。
//
// 编译（GitHub Actions windows-latest，MSVC）：
//   cl /EHsc /I src tests\real_sci_repro.cpp user32.lib
// 运行前把 SciLexer.dll（Notepad++ portable 包里）放在同目录或 PATH 里。
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/Scintilla.h"

// George 真机文件的确切内容（CRLF，末尾带换行，UTF-8 下共 83 字节，
// 与 v1.0.2 日志里的 docLen=83 一致）。
static const char kSql[] =
    "INSERT INTO user (id, name, age)\r\n"
    "VALUES\r\n"
    "(1, '\xE5\xBC\xA0\xE4\xB8\x89', 25),\r\n"
    "(2, '\xE6\x9D\x8E\xE5\x9B\x9B', 30);\r\n";

static void printEscaped(const char* tag, const char* buf, size_t n) {
    printf("%s len=%zu bytes: '", tag, n);
    for (size_t i = 0; i < n && i < 60; ++i) {
        const unsigned char c = (unsigned char)buf[i];
        if (c == '\r')
            printf("\\r");
        else if (c == '\n')
            printf("\\n");
        else if (c >= 32 && c < 127)
            putchar(c);
        else
            printf("\\x%02X", c);
    }
    printf("'\n");
}

static LRESULT sci(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    return ::SendMessage(h, msg, wp, lp);
}

int main() {
    HMODULE lex = ::LoadLibraryA("SciLexer.dll");
    if (!lex) {
        printf("FAIL: cannot load SciLexer.dll (err %lu)\n",
               (unsigned long)::GetLastError());
        return 1;
    }
    printf("SciLexer.dll loaded\n");

    HWND parent = ::CreateWindowExA(0, "STATIC", "repro-parent",
                                    WS_OVERLAPPEDWINDOW, 0, 0, 640, 480,
                                    NULL, NULL, NULL, NULL);
    if (!parent) {
        printf("FAIL: cannot create parent window\n");
        return 1;
    }
    HWND sciWnd = ::CreateWindowExA(0, "Scintilla", "", WS_CHILD | WS_VISIBLE,
                                    0, 0, 640, 480, parent, NULL, NULL, NULL);
    if (!sciWnd) {
        printf("FAIL: cannot create Scintilla window (err %lu)\n",
               (unsigned long)::GetLastError());
        return 1;
    }
    printf("Scintilla window created\n");

    sci(sciWnd, SCI_SETTEXT, 0, (LPARAM)kSql);
    const LRESULT docLen = sci(sciWnd, SCI_GETLENGTH, 0, 0);
    printf("SCI_GETLENGTH = %lld (expected 83)\n", (long long)docLen);

    // 1) 旧方式：SCI_GETTEXTRANGE（v1.0.0–v1.0.2 用的调用）。
    {
        std::string buf((size_t)docLen + 1, '\0');
        Sci_TextRange tr;
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = docLen;
        tr.lpstrText = &buf[0];
        const LRESULT n =
            sci(sciWnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
        printf("SCI_GETTEXTRANGE returned %lld\n", (long long)n);
        printEscaped("SCI_GETTEXTRANGE", buf.data(), (size_t)docLen);
        size_t nonZero = 0;
        for (size_t i = 0; i < (size_t)docLen; ++i)
            if (buf[i] != '\0')
                ++nonZero;
        printf("SCI_GETTEXTRANGE non-zero bytes: %zu / %lld\n", nonZero,
               (long long)docLen);
    }

    // 2) 新方式：SCI_GETTEXT（v1.0.3 用的调用）。
    {
        std::string buf((size_t)docLen + 1, '\0');
        const LRESULT n = sci(sciWnd, SCI_GETTEXT, (WPARAM)(docLen + 1),
                              (LPARAM)&buf[0]);
        printf("SCI_GETTEXT returned %lld\n", (long long)n);
        printEscaped("SCI_GETTEXT", buf.data(),
                     n > 0 ? (size_t)n : 0);
        if (n == docLen && buf.compare(0, (size_t)docLen, kSql,
                                       (size_t)docLen) == 0) {
            printf("RESULT: SCI_GETTEXT returned the exact SQL -> v1.0.3 text retrieval works on real Windows\n");
            return 0;
        }
        printf("RESULT: SCI_GETTEXT did NOT return the exact SQL\n");
        return 1;
    }
}
