// tests/test_highlighter.cpp — 端到端测试：用伪造的 Scintilla 直接驱动
// sqlhl::analyse() / sqlhl::highlight()，验证列<->值映射与高亮范围。
//
// 编译（在 build.ps1 里自动做）：
//   cl /I src /FI"tests/fake_sci.h" tests/test_highlighter.cpp src/Highlighter.cpp
// /FI 把 ::SendMessage 重定向到下面的 FakeSendMessage，于是整个高亮链路
// （语句提取→解析→绘制）都在没有 Notepad++ 的情况下被真实执行。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fake_sci.h"
#include "../src/Scintilla.h"
#include "../src/Highlighter.h"

namespace {

const int INDIC_VALUE = 8;
const int INDIC_COLUMN = 9;

struct Fill {
    int indic;
    int pos;
    int len;
};

// 伪造的 Scintilla：只实现插件用到的消息，记录 indicator 绘制。
struct FakeSci {
    std::string doc;
    int curPos = 0;
    int anchor = 0;
    int curIndic = 0;
    std::vector<Fill> fills;

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case SCI_GETLENGTH:
            return (LRESULT)doc.size();
        case SCI_GETTEXT: {
            // v1.0.3：插件改用 SCI_GETTEXT 取整篇文档。wp 为缓冲区大小
            // （含结尾 \0），返回写入的字符数（不含 \0）。
            char* out = reinterpret_cast<char*>(lp);
            const size_t cap = (size_t)wp;
            size_t n = doc.size();
            if (cap == 0)
                return 0;
            if (n > cap - 1)
                n = cap - 1;
            memcpy(out, doc.data(), n);
            out[n] = '\0';
            return (LRESULT)n;
        }
        case SCI_GETTEXTRANGE: {
            // v1.0.3 起插件不再调它；保留以防旧代码路径被测到。
            // 注意：真机上它曾返回全 \x00（见 v1.0.3 说明），这里的 fake
            // 故意不模拟那个故障——故障本身由真机复现程序验证。
            Sci_TextRange* tr = reinterpret_cast<Sci_TextRange*>(lp);
            int a = (int)tr->chrg.cpMin;
            int b = (int)tr->chrg.cpMax;
            if (a < 0)
                a = 0;
            if (b > (int)doc.size())
                b = (int)doc.size();
            if (b < a)
                b = a;
            memcpy(tr->lpstrText, doc.data() + (size_t)a, (size_t)(b - a));
            tr->lpstrText[b - a] = '\0';
            return (LRESULT)(b - a);
        }
        case SCI_GETCURRENTPOS:
            return (LRESULT)curPos;
        case SCI_GETANCHOR:
            return (LRESULT)anchor;
        case SCI_INDICSETSTYLE:
        case SCI_INDICSETFORE:
        case SCI_INDICSETALPHA:
        case SCI_INDICSETOUTLINEALPHA:
            return 0;
        case SCI_SETINDICATORCURRENT:
            curIndic = (int)wp;
            return 0;
        case SCI_INDICATORFILLRANGE:
            fills.push_back(Fill{curIndic, (int)wp, (int)lp});
            return 0;
        case SCI_INDICATORCLEARRANGE:
            return 0;
        default:
            return 0;
        }
    }
};

FakeSci& theFake() {
    static FakeSci f;
    return f;
}

} // namespace

LRESULT WINAPI FakeSendMessage(HWND, UINT msg, WPARAM wp, LPARAM lp) {
    return theFake().handle(msg, wp, lp);
}

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const char* name, const std::string& detail = std::string()) {
    if (ok) {
        ++g_pass;
        printf("PASS %s\n", name);
    } else {
        ++g_fail;
        printf("FAIL %s %s\n", name, detail.c_str());
    }
}

// marker 的每一次出现都被 indic 的 fill 完整覆盖。
bool covered(int indic, const std::string& text, const std::string& marker) {
    size_t start = 0;
    bool found = false;
    while (true) {
        size_t i = text.find(marker, start);
        if (i == std::string::npos)
            break;
        found = true;
        bool ok = false;
        for (const Fill& f : theFake().fills) {
            if (f.indic == indic && f.pos <= (int)i &&
                f.pos + f.len >= (int)(i + marker.size())) {
                ok = true;
                break;
            }
        }
        if (!ok)
            return false;
        start = i + 1;
    }
    return found;
}

// 模拟单击：光标移到 marker 上（无选区）→ 只有落在列名上才高亮。
void clickAt(const std::string& text, const std::string& marker) {
    FakeSci& f = theFake();
    f.doc = text;
    f.fills.clear();
    size_t p = text.find(marker);
    f.curPos = f.anchor = (int)p + 1;
    sqlhl::HitInfo hit;
    if (sqlhl::analyse((HWND)1, f.curPos, hit) && hit.isColumn)
        sqlhl::highlight((HWND)1, hit);
    else
        sqlhl::clearHighlights((HWND)1);
}

// 模拟双击：双击位置在 marker 处 → 值/列都可触发高亮。
void dclickAt(const std::string& text, const std::string& marker) {
    FakeSci& f = theFake();
    f.doc = text;
    f.fills.clear();
    size_t p = text.find(marker);
    f.curPos = f.anchor = (int)p + 1;
    sqlhl::HitInfo hit;
    if (sqlhl::analyse((HWND)1, (int)p + 1, hit))
        sqlhl::highlight((HWND)1, hit);
    else
        sqlhl::clearHighlights((HWND)1);
}

} // namespace

int main() {
    const std::string t1 =
        "INSERT INTO users (id, name, email) VALUES (1, '张三', 'zhang@example.com');";

    clickAt(t1, "name");
    check(covered(INDIC_COLUMN, t1, "name"), "click-column-basic/col");
    check(covered(INDIC_VALUE, t1, "'张三'"), "click-column-basic/val");

    dclickAt(t1, "zhang@example.com");
    check(covered(INDIC_COLUMN, t1, "email"), "dclick-value-basic/col");
    check(covered(INDIC_VALUE, t1, "'zhang@example.com'"), "dclick-value-basic/val");

    dclickAt(t1, "email");
    check(covered(INDIC_VALUE, t1, "'zhang@example.com'"), "dclick-column-basic/val");

    const std::string t4 =
        "INSERT INTO t (\n"
        "  a,\n"
        "  b\n"
        ") VALUES (\n"
        "  func(1, 2),\n"
        "  'x, y'\n"
        "),\n"
        "(\n"
        "  3,\n"
        "  'it''s'\n"
        ");";
    clickAt(t4, "b");
    check(covered(INDIC_COLUMN, t4, "b"), "multiline-tricky/col");
    check(covered(INDIC_VALUE, t4, "'x, y'"), "multiline-tricky/row1");
    check(covered(INDIC_VALUE, t4, "'it''s'"), "multiline-tricky/row2");

    clickAt("hello world, just text", "world");
    check(theFake().fills.empty(), "plain-text/none");

    dclickAt("INSERT INTO t VALUES (1, 2);", "1");
    check(theFake().fills.empty(), "no-column-list/none");

    const std::string t7 = "SELECT 1;\nINSERT INTO t (a) VALUES (10);";
    clickAt(t7, "(a)");
    check(covered(INDIC_COLUMN, t7, "a"), "statement-boundary/col");
    check(covered(INDIC_VALUE, t7, "10"), "statement-boundary/val");

    dclickAt(t4, "'it''s'");
    check(covered(INDIC_COLUMN, t4, "b"), "dclick-second-row/col");
    check(covered(INDIC_VALUE, t4, "'it''s'"), "dclick-second-row/val");

    const std::string t9 =
        "/* comment; ( */\n"
        "INSERT INTO t (a, b) -- tail comment\n"
        "VALUES (1, 'v');";
    clickAt(t9, "b");
    check(covered(INDIC_COLUMN, t9, "b"), "comments/col");
    check(covered(INDIC_VALUE, t9, "'v'"), "comments/val");

    clickAt("INSERT INTO t (a, b) SELECT a, b FROM s;", "a");
    check(theFake().fills.empty(), "insert-select/none");

    // v1.0.1 回归：George 2026-11-07 真机复现用例（多行、中文值）。
    // v1.0.0 在 Windows 上点 id 完全没高亮；解析器对这段 SQL 必须通。
    const std::string tu =
        "INSERT INTO user (id, name, age)\n"
        "VALUES\n"
        "(1, '张三', 25),\n"
        "(2, '李四', 30);";
    clickAt(tu, "id");
    check(covered(INDIC_COLUMN, tu, "id"), "user-sql-click-id/col");
    check(covered(INDIC_VALUE, tu, "1"), "user-sql-click-id/val1");
    clickAt(tu, "name");
    check(covered(INDIC_VALUE, tu, "'张三'"), "user-sql-click-name/row1");
    check(covered(INDIC_VALUE, tu, "'李四'"), "user-sql-click-name/row2");
    clickAt(tu, "age");
    check(covered(INDIC_VALUE, tu, "25"), "user-sql-click-age/row1");
    check(covered(INDIC_VALUE, tu, "30"), "user-sql-click-age/row2");
    dclickAt(tu, "'张三'");
    check(covered(INDIC_COLUMN, tu, "name"), "user-sql-dclick-val/col");

    // v1.0.2：diagnose()——analyse 失败时说清断在哪一步。
    {
        FakeSci& f = theFake();
        f.doc = tu;
        const std::string d = sqlhl::diagnose((HWND)1, (int)tu.find("id") + 1);
        check(d.find("parsed-hit") != std::string::npos,
              "diagnose/user-sql/parsed-hit");
        check(d.find("docLen=") != std::string::npos, "diagnose/has-doclen");
        check(d.find("INSERT INTO user") != std::string::npos,
              "diagnose/preview-has-sql");
        f.doc = "hello world, just text";
        const std::string d2 = sqlhl::diagnose((HWND)1, 3);
        check(d2.find("no-insert") != std::string::npos,
              "diagnose/plain-text/no-insert");
    }

    // v1.0.3 回归：George 2026-11-08 真机日志——docLen=83 但
    // SCI_GETTEXTRANGE 写回的 83 字节全是 \x00，stage=no-insert。
    // 改走 SCI_GETTEXT 后，同一段 SQL 的 diagnose 预览必须出现真文本，
    // 且 analyse 必须命中列。
    {
        FakeSci& f = theFake();
        f.doc = tu;
        const std::string d = sqlhl::diagnose((HWND)1, (int)tu.find("id") + 1);
        check(d.find("INSERT INTO user") != std::string::npos,
              "v103/gettext-preview-real-sql");
        check(d.find("\\x00") == std::string::npos,
              "v103/gettext-preview-no-nuls");
        sqlhl::HitInfo hit;
        check(sqlhl::analyse((HWND)1, (int)tu.find("id") + 1, hit) &&
                  hit.isColumn && hit.colIdx == 0,
              "v103/analyse-id-via-gettext");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
