// tests/test_load.cpp — DLL 加载冒烟测试：验证导出的插件接口存在、
// 基本调用正常（getName / getFuncsArray / isUnicode）。
// 在 dist 目录下运行（与 SqlColumnHighlighter.dll 同目录）。
#include <windows.h>
#include <cstdio>
#include <cwchar>

int main() {
    HMODULE h = LoadLibraryW(L"SqlColumnHighlighter.dll");
    if (!h) {
        printf("FAIL: LoadLibrary error %lu\n", GetLastError());
        return 1;
    }
    int fails = 0;
    auto check = [&](bool ok, const char* name) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok)
            ++fails;
    };

    check(GetProcAddress(h, "setInfo") != nullptr, "export setInfo");
    check(GetProcAddress(h, "getName") != nullptr, "export getName");
    check(GetProcAddress(h, "getFuncsArray") != nullptr, "export getFuncsArray");
    check(GetProcAddress(h, "beNotified") != nullptr, "export beNotified");
    check(GetProcAddress(h, "messageProc") != nullptr, "export messageProc");
    check(GetProcAddress(h, "isUnicode") != nullptr, "export isUnicode");

    typedef const wchar_t* (*GetNameFn)();
    GetNameFn getName = (GetNameFn)GetProcAddress(h, "getName");
    check(getName && wcscmp(getName(), L"SQL Column Highlighter") == 0,
          "getName() value");

    typedef BOOL (*IsUnicodeFn)();
    IsUnicodeFn isUnicode = (IsUnicodeFn)GetProcAddress(h, "isUnicode");
    check(isUnicode && isUnicode(), "isUnicode() true");

    typedef void* (*GetFuncsFn)(int*);
    GetFuncsFn getFuncsArray = (GetFuncsFn)GetProcAddress(h, "getFuncsArray");
    int n = 0;
    void* arr = getFuncsArray ? getFuncsArray(&n) : nullptr;
    check(arr != nullptr && n == 2, "getFuncsArray() count==2");

    FreeLibrary(h);
    printf(fails ? "LOAD TEST FAILED\n" : "LOAD TEST PASSED\n");
    return fails ? 1 : 0;
}
