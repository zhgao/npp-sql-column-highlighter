// Highlighter.cpp — SQL INSERT 列/值解析与高亮绘制。
//
// 解析策略（与 PythonScript 版 SqlColumnHighlighter.py 一致）：
// 先把字符串字面量和注释 mask 成等长空格，再在 mask 上做括号/逗号/分号
// 的结构扫描，这样字符串里的 ',' '(' ';' 不会干扰。
#include "Highlighter.h"
#include "Scintilla.h"

#include <regex>
#include <cctype>

namespace sqlhl {
namespace {

const int INDIC_VALUE = 8;    // 值的指示器：琥珀色
const int INDIC_COLUMN = 9;   // 列名的指示器：蓝色
const int WINDOW = 20000;     // 在光标前后各取多少字节来定位语句

typedef std::pair<int, int> Range;

int sciLength(HWND sci) {
    return (int)::SendMessage(sci, SCI_GETLENGTH, 0, 0);
}

std::string sciTextRange(HWND sci, int start, int end) {
    if (end <= start)
        return std::string();
    Sci_TextRange tr;
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = end;
    std::string buf((size_t)(end - start) + 1, '\0');
    tr.lpstrText = &buf[0];
    ::SendMessage(sci, SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
    buf.resize((size_t)(end - start));
    return buf;
}

bool isBlank(const std::string& s, const Range& r) {
    for (int i = r.first; i < r.second; ++i) {
        if (!isspace((unsigned char)s[i]))
            return false;
    }
    return true;
}

std::string maskText(const std::string& text) {
    std::string out = text;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const char ch = text[i];
        if ((ch == '-' && i + 1 < n && text[i + 1] == '-') || ch == '#') {
            // -- 注释 / # 注释：到行尾
            size_t j = text.find('\n', i);
            if (j == std::string::npos)
                j = n;
            for (size_t k = i; k < j; ++k)
                out[k] = ' ';
            i = j;
        } else if (ch == '/' && i + 1 < n && text[i + 1] == '*') {
            // /* */ 注释
            size_t j = text.find("*/", i + 2);
            j = (j == std::string::npos) ? n : j + 2;
            for (size_t k = i; k < j; ++k)
                out[k] = ' ';
            i = j;
        } else if (ch == '\'' || ch == '"' || ch == '`') {
            // 字符串字面量（含 '' / "" 转义与反斜杠转义）
            size_t j = i + 1;
            while (j < n) {
                if (text[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (text[j] == ch) {
                    if ((ch == '\'' || ch == '"') && j + 1 < n && text[j + 1] == ch) {
                        j += 2;
                        continue;
                    }
                    break;
                }
                ++j;
            }
            if (j < n)
                ++j;
            for (size_t k = i; k < j && k < n; ++k)
                out[k] = ' ';
            i = j;
        } else if (ch == '[') {
            // [col] 风格的标识符
            size_t j = text.find(']', i + 1);
            j = (j == std::string::npos) ? n : j + 1;
            for (size_t k = i; k < j && k < n; ++k)
                out[k] = ' ';
            i = j;
        } else {
            ++i;
        }
    }
    return out;
}

// 返回与 masked[openIdx] == '(' 配对的 ')' 下标；找不到返回 -1。
int matchParen(const std::string& masked, int openIdx) {
    int depth = 0;
    for (int i = openIdx; i < (int)masked.size(); ++i) {
        const char ch = masked[i];
        if (ch == '(')
            ++depth;
        else if (ch == ')') {
            if (--depth == 0)
                return i;
        }
    }
    return -1;
}

// 在 masked[lo:hi] 内按顶层逗号切分，返回原文下标。
std::vector<Range> splitTopLevel(const std::string& masked, int lo, int hi) {
    std::vector<Range> items;
    int depth = 0;
    int cur = lo;
    for (int i = lo; i < hi; ++i) {
        const char ch = masked[i];
        if (ch == '(')
            ++depth;
        else if (ch == ')')
            --depth;
        else if (ch == ',' && depth == 0) {
            items.push_back(Range(cur, i));
            cur = i + 1;
        }
    }
    items.push_back(Range(cur, hi));
    return items;
}

// 取光标所在的整条语句：向前/向后找到顶层分号为界。
bool statementAt(HWND sci, int pos, int& baseOut, std::string& stmtOut) {
    const int docLen = sciLength(sci);
    int start = pos - WINDOW;
    if (start < 0)
        start = 0;
    int end = pos + WINDOW;
    if (end > docLen)
        end = docLen;
    const std::string text = sciTextRange(sci, start, end);
    if (text.empty())
        return false;
    const std::string masked = maskText(text);
    const int rel = pos - start;
    int prev = -1, nxt = -1;
    for (int i = 0; i < (int)masked.size(); ++i) {
        if (masked[i] == ';') {
            if (i < rel)
                prev = i;
            else if (nxt == -1)
                nxt = i;
        }
    }
    const int s0 = (prev == -1) ? 0 : prev + 1;
    const int s1 = (nxt == -1) ? (int)text.size() : nxt + 1;
    baseOut = start + s0;
    stmtOut = text.substr((size_t)s0, (size_t)(s1 - s0));
    return true;
}

struct Parsed {
    std::vector<Range> columns;
    std::vector<std::vector<Range>> rows;
};

// 解析 INSERT INTO ... (列) VALUES (...), (...)；失败返回 false。
bool parseInsert(const std::string& stmt, Parsed& out) {
    const std::string masked = maskText(stmt);

    static const std::regex insertRe("\\bINSERT\\s+INTO\\b",
                                     std::regex_constants::ECMAScript |
                                         std::regex_constants::icase);
    std::smatch m;
    if (!std::regex_search(masked, m, insertRe))
        return false;
    const size_t afterInsert = (size_t)m.position() + (size_t)m.length();

    const size_t ci = masked.find('(', afterInsert);  // 列名列表的左括号
    if (ci == std::string::npos)
        return false;
    const int cj = matchParen(masked, (int)ci);
    if (cj < 0)
        return false;

    std::vector<Range> columns;
    for (const Range& r : splitTopLevel(masked, (int)ci + 1, cj)) {
        if (!isBlank(stmt, r))
            columns.push_back(r);
    }
    if (columns.empty())
        return false;

    static const std::regex valuesRe("\\bVALUES\\b",
                                     std::regex_constants::ECMAScript |
                                         std::regex_constants::icase);
    std::smatch vm;
    const std::string tail = masked.substr(ci);
    if (!std::regex_search(tail, vm, valuesRe))
        return false;
    size_t i = ci + (size_t)vm.position() + (size_t)vm.length();
    const size_t n = masked.size();

    std::vector<std::vector<Range>> rows;
    while (i < n) {
        const char ch = masked[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',') {
            ++i;
            continue;
        }
        if (ch == '(') {
            const int close = matchParen(masked, (int)i);
            if (close < 0)
                break;
            std::vector<Range> items;
            for (const Range& r : splitTopLevel(masked, (int)i + 1, close)) {
                if (!isBlank(stmt, r))
                    items.push_back(r);
            }
            if (!items.empty())
                rows.push_back(items);
            i = (size_t)close + 1;
            continue;
        }
        break;  // 碰到 ON DUPLICATE KEY 之类的尾巴就停
    }
    if (rows.empty())
        return false;

    out.columns = columns;
    out.rows = rows;
    return true;
}

}  // namespace

bool analyse(HWND sci, int pos, HitInfo& out) {
    int base = 0;
    std::string stmt;
    if (!statementAt(sci, pos, base, stmt))
        return false;
    Parsed p;
    if (!parseInsert(stmt, p))
        return false;
    const int rel = pos - base;

    for (int ci = 0; ci < (int)p.columns.size(); ++ci) {
        const Range& r = p.columns[ci];
        if (r.first <= rel && rel <= r.second) {
            out.stmt = stmt;
            out.base = base;
            out.columns = p.columns;
            out.rows = p.rows;
            out.colIdx = ci;
            out.rowIdx = -1;
            out.isColumn = true;
            return true;
        }
    }
    for (int ri = 0; ri < (int)p.rows.size(); ++ri) {
        for (int vi = 0; vi < (int)p.rows[ri].size(); ++vi) {
            const Range& r = p.rows[ri][vi];
            if (r.first <= rel && rel <= r.second) {
                out.stmt = stmt;
                out.base = base;
                out.columns = p.columns;
                out.rows = p.rows;
                out.colIdx = vi;
                out.rowIdx = ri;
                out.isColumn = false;
                return true;
            }
        }
    }
    return false;
}

void clearHighlights(HWND sci) {
    const int docLen = sciLength(sci);
    const int indics[2] = {INDIC_VALUE, INDIC_COLUMN};
    for (int k = 0; k < 2; ++k) {
        ::SendMessage(sci, SCI_SETINDICATORCURRENT, indics[k], 0);
        ::SendMessage(sci, SCI_INDICATORCLEARRANGE, 0, docLen);
    }
}

void setupIndicators(HWND sci) {
    ::SendMessage(sci, SCI_INDICSETSTYLE, INDIC_VALUE, INDIC_ROUNDBOX);
    ::SendMessage(sci, SCI_INDICSETFORE, INDIC_VALUE, RGB(255, 193, 60));
    ::SendMessage(sci, SCI_INDICSETALPHA, INDIC_VALUE, 100);
    ::SendMessage(sci, SCI_INDICSETOUTLINEALPHA, INDIC_VALUE, 70);

    ::SendMessage(sci, SCI_INDICSETSTYLE, INDIC_COLUMN, INDIC_ROUNDBOX);
    ::SendMessage(sci, SCI_INDICSETFORE, INDIC_COLUMN, RGB(120, 175, 255));
    ::SendMessage(sci, SCI_INDICSETALPHA, INDIC_COLUMN, 100);
    ::SendMessage(sci, SCI_INDICSETOUTLINEALPHA, INDIC_COLUMN, 70);
}

void highlight(HWND sci, const HitInfo& hit) {
    setupIndicators(sci);
    clearHighlights(sci);

    const std::string& s = hit.stmt;
    const int base = hit.base;
    const int ci = hit.colIdx;

    // 列名：蓝色
    {
        const Range& r = hit.columns[ci];
        int a = r.first, b = r.second;
        while (a < b && isspace((unsigned char)s[a]))
            ++a;
        while (b > a && isspace((unsigned char)s[b - 1]))
            --b;
        if (b > a) {
            ::SendMessage(sci, SCI_SETINDICATORCURRENT, INDIC_COLUMN, 0);
            ::SendMessage(sci, SCI_INDICATORFILLRANGE, base + a, b - a);
        }
    }

    // 值：琥珀色。单击列名时标出所有 VALUES 行的同列值；双击值时只标这一个。
    std::vector<Range> targets;
    if (hit.isColumn) {
        for (size_t ri = 0; ri < hit.rows.size(); ++ri) {
            if (ci < (int)hit.rows[ri].size())
                targets.push_back(hit.rows[ri][ci]);
        }
    } else if (hit.rowIdx >= 0 && hit.rowIdx < (int)hit.rows.size() &&
               ci < (int)hit.rows[hit.rowIdx].size()) {
        targets.push_back(hit.rows[hit.rowIdx][ci]);
    }

    ::SendMessage(sci, SCI_SETINDICATORCURRENT, INDIC_VALUE, 0);
    for (size_t k = 0; k < targets.size(); ++k) {
        int a = targets[k].first, b = targets[k].second;
        while (a < b && isspace((unsigned char)s[a]))
            ++a;
        while (b > a && isspace((unsigned char)s[b - 1]))
            --b;
        if (b > a)
            ::SendMessage(sci, SCI_INDICATORFILLRANGE, base + a, b - a);
    }
}

}  // namespace sqlhl
