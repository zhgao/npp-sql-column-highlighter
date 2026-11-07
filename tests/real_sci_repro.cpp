// tests/real_sci_repro.cpp — 真机复现：George 的 Notepad++ 上
// v1.0.2 的 diagnose 日志读回 83 个 \x00（docLen=83 是对的）。
//
// 真正的根因（2026-11-08 对照 Scintilla 5.6.9 官方头文件查出）：
// src/Scintilla.h 把 SCI_GETTEXTRANGE 的消息号抄错成 2166，
// 2166 其实是 SCI_LINEFROMPOSITION——插件发的根本不是取文本消息，
// Scintilla 当然什么都不写。结构体本身没错，错的是消息号。
// 同一份头文件还抄错三处：SCI_INDICSETALPHA（2083，实为 INDICGETFORE）、
// SCI_INDICSETOUTLINEALPHA（2208）、SCN_DOUBLECLICK（2103，实为 2006）。
// Linux 假测试用同一份头文件分发，错号对错号，全绿也没发现。
//
// 本程序在真实 Windows + 真实 Scintilla 5.6.9（SciTE 自带的 Scintilla.dll；
// Notepad++ 8.9.9 不再单独发 SciLexer.dll，Scintilla 已静态链进
// notepad++.exe）上做三组对照，用插件同款 src/Scintilla.h：
//   1) 旧错号 2166 发 TextRange → 应复现全 \x00（George 日志同款故障）；
//   2) 修正后的 SCI_GETTEXTRANGE（2162）→ 应取回确切 SQL；
//   3) SCI_GETTEXT（2182，v1.0.3 用的调用）→ 应取回确切 SQL。
// 退出码：0 = 三组都符合预期（根因得证，且 v1.0.3 的取文本方式成立）；
// 1 = 任一组不符合预期。
//
// run #4（2026-11-08）：repro.exe 以 0xC0000005 崩溃，且日志里一行输出
// 都没有——stdout 在管道里是全缓冲的，崩溃时没刷出来。之后改成无缓冲
// 输出 + 崩溃过滤器打印异常码和故障地址，再跑就能定位崩到哪一步。
// 崩的是复现程序本身，插件代码不在其中，不影响 v1.0.3 的结论。
//
// run #5 重跑（2026-11-08）：诊断生效，TEST1 通过——旧错号 2166 在
// 真实 Windows 上确实读回 82 个 \x00，根因得证。但 TEST2（修正后的
// SCI_GETTEXTRANGE 2162）在 Scintilla.dll 内部以 0xC0000005 崩溃，
// 故障地址落在 Scintilla.dll 模块内，原因未明；TEST3 没跑到。
// 之后改成先跑 TEST3（SCI_GETTEXT，v1.0.3 实际用的调用），再跑
// TEST2 小范围（0..5），最后跑 TEST2 全范围，逐步定位。
//
// 编译（GitHub Actions windows-latest，MSVC）：
//   cl /EHsc /I src tests\real_sci_repro.cpp user32.lib
// 运行前把 Scintilla.dll 与 Lexilla.dll（SciTE 包 wscite/ 下）
// 放在 repro.exe 同目录。
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/Scintilla.h"

// 旧的错号：v1.0.0–v1.0.2 的头文件里 SCI_GETTEXTRANGE 就是这个值；
// 官方头文件里 2166 = SCI_LINEFROMPOSITION。
#define SCI_GETTEXTRANGE_WRONG 2166

// George 真机文件的内容（CRLF，末尾带换行）。注意：这段字面量是 82 字节，
// George 的原文件是 83 字节（多 1 个字节，内容未知，不影响消息号验证），
// 所以长度预期一律用 sizeof(kSql)-1，不要写死 83——run #3 就是被写死的
// 83 坑了：SCI_GETLENGTH 正确返回 82，程序却当成异常退出了。
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

static size_t countNonZero(const char* buf, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i < n; ++i)
        if (buf[i] != '\0')
            ++c;
    return c;
}

static LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    // stderr is unbuffered, so this survives the crash.
    fprintf(stderr, "CRASH: exception code 0x%08lX at address %p\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    // run #4 教训：stdout 在管道里是全缓冲的，程序崩溃时缓冲区
    // 没刷出来，日志里什么都没有。改成无缓冲，每行立刻进日志，
    // 崩到哪一行都能从最后一条输出看出来。
    setvbuf(stdout, NULL, _IONBF, 0);
    ::SetUnhandledExceptionFilter(onCrash);
    printf("start: sizeof(void*)=%zu (8=x64, 4=x86)\n", sizeof(void*));
    printf("header SCI_GETTEXTRANGE=%d (official 2162)\n", SCI_GETTEXTRANGE);
    printf("header SCI_GETTEXT=%d (official 2182)\n", SCI_GETTEXT);

    HMODULE lex = ::LoadLibraryA("Scintilla.dll");
    if (!lex) {
        printf("FAIL: cannot load Scintilla.dll (err %lu)\n",
               (unsigned long)::GetLastError());
        return 1;
    }
    printf("Scintilla.dll loaded (hmodule=%p)\n", (void*)lex);

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

    printf("calling SCI_SETTEXT...\n");
    sci(sciWnd, SCI_SETTEXT, 0, (LPARAM)kSql);
    printf("SCI_SETTEXT done\n");
    const LRESULT docLen = sci(sciWnd, SCI_GETLENGTH, 0, 0);
    const LRESULT expectLen = (LRESULT)(sizeof(kSql) - 1);
    printf("SCI_GETLENGTH = %lld (expected %lld)\n", (long long)docLen,
           (long long)expectLen);
    if (docLen != expectLen) {
        printf("RESULT: unexpected docLen\n");
        return 1;
    }

    // 1) 旧错号 2166：应复现 George 真机故障——写回全 \x00。
    printf("TEST1: sending msg 2166...\n");
    bool t1ok = false;
    {
        std::string buf((size_t)docLen + 1, '\0');
        Sci_TextRange tr;
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = docLen;
        tr.lpstrText = &buf[0];
        const LRESULT n =
            sci(sciWnd, SCI_GETTEXTRANGE_WRONG, 0, (LPARAM)&tr);
        printf("msg 2166 (old wrong GETTEXTRANGE) returned %lld\n",
               (long long)n);
        printEscaped("msg-2166", buf.data(), (size_t)docLen);
        const size_t nz = countNonZero(buf.data(), (size_t)docLen);
        printf("msg 2166 non-zero bytes: %zu / %lld\n", nz,
               (long long)docLen);
        t1ok = (nz == 0);
        printf("TEST1 old-wrong-number-2166-reproduces-all-NULs: %s\n",
               t1ok ? "PASS" : "FAIL");
    }

    // 3) 先跑 SCI_GETTEXT（v1.0.3 实际用的调用）：run #5 里 TEST2
    // 在 Scintilla.dll 内崩溃，TEST3 没跑到。GETTEXT 不用结构体，
    // 是 v1.0.3 能不能交货的关键，先验证它。
    printf("TEST3: sending SCI_GETTEXT...\n");
    bool t3ok = false;
    {
        std::string buf((size_t)docLen + 1, '\0');
        const LRESULT n = sci(sciWnd, SCI_GETTEXT, (WPARAM)(docLen + 1),
                              (LPARAM)&buf[0]);
        printf("SCI_GETTEXT returned %lld\n", (long long)n);
        printEscaped("SCI_GETTEXT", buf.data(),
                     n > 0 ? (size_t)n : 0);
        t3ok = (n == docLen &&
                buf.compare(0, (size_t)docLen, kSql, (size_t)docLen) == 0);
        printf("TEST3 SCI_GETTEXT-returns-exact-SQL: %s\n",
               t3ok ? "PASS" : "FAIL");
    }

    // 2a) 小范围 GETTEXTRANGE（0..5）：定位 run #5 的崩溃是结构性的
    //（任何 GETTEXTRANGE 都崩）还是跟范围/内容有关。
    printf("TEST2-small: sending SCI_GETTEXTRANGE(2162) range 0..5...\n");
    bool t2sok = false;
    {
        std::string buf(6, '\0');
        Sci_TextRange tr;
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = 5;
        tr.lpstrText = &buf[0];
        const LRESULT n =
            sci(sciWnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
        printf("SCI_GETTEXTRANGE(2162) range 0..5 returned %lld\n",
               (long long)n);
        printEscaped("SCI_GETTEXTRANGE-2162-small", buf.data(), 5);
        t2sok = (n == 5 && buf.compare(0, 5, kSql, 5) == 0);
        printf("TEST2-small corrected-GETTEXTRANGE-2162-range-0..5: %s\n",
               t2sok ? "PASS" : "FAIL");
    }

    // 2b) 全范围 GETTEXTRANGE（2162）：应取回确切 SQL。
    // run #5 在这一步于 Scintilla.dll 内崩溃（0xC0000005），原因未明。
    printf("TEST2: sending SCI_GETTEXTRANGE(2162)...\n");
    bool t2ok = false;
    {
        std::string buf((size_t)docLen + 1, '\0');
        Sci_TextRange tr;
        tr.chrg.cpMin = 0;
        tr.chrg.cpMax = docLen;
        tr.lpstrText = &buf[0];
        const LRESULT n =
            sci(sciWnd, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
        printf("SCI_GETTEXTRANGE(2162) returned %lld\n", (long long)n);
        printEscaped("SCI_GETTEXTRANGE-2162", buf.data(), (size_t)docLen);
        t2ok = (n == docLen &&
                buf.compare(0, (size_t)docLen, kSql, (size_t)docLen) == 0);
        printf("TEST2 corrected-GETTEXTRANGE-2162-returns-exact-SQL: %s\n",
               t2ok ? "PASS" : "FAIL");
    }

    if (t1ok && t2ok && t3ok && t2sok) {
        printf("RESULT: root cause confirmed (2166 was LINEFROMPOSITION, not GETTEXTRANGE) and v1.0.3 text retrieval works on real Windows\n");
        return 0;
    }
    printf("RESULT: NOT all checks passed (t1=%d t2=%d t2small=%d t3=%d)\n",
           (int)t1ok, (int)t2ok, (int)t2sok, (int)t3ok);
    return 1;
}
