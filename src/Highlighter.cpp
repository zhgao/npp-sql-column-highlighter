// Highlighter.cpp — SQL INSERT 列/值解析与高亮绘制。
//
// 解析策略（与 PythonScript 版 SqlColumnHighlighter.py 一致）：
// 先把字符串字面量和注释 mask 成等长空格，再在 mask 上做括号/逗号/分号
// 的结构扫描，这样字符串里的 ',' '(' ';' 不会干扰。
#include "Highlighter.h"
#include "Scintilla.h"

#include <cctype>
#include <cstdio>

// v1.0.2：不再用 std::regex。v1.0.1 的日志证明通知和 indicator 都正常，
// 但 analyse() 在真机上对每个位置都返回 false，而同一段 SQL 在
// Linux 测试里是通的。MSVC 的 std::regex 在中文 Windows  locale 下对
// 含非 ASCII 字节的字符串行为不可靠，是头号嫌疑；改成纯 ASCII 手写
// 关键字匹配（大小写不敏感、带词边界），彻底去掉这个变量。

namespace sqlhl {
namespace {

int INDIC_VALUE = 8;    // 值的指示器：琥珀色（默认；v1.0.1 可由 setIndicators 改写）
int INDIC_COLUMN = 9;   // 列名的指示器：蓝色（默认；v1.0.1 可由 setIndicators 改写）
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

// 手写关键字匹配（替代 std::regex）：纯 ASCII、大小写不敏感、带词边界。
bool isWordChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

bool matchKeywordAt(const std::string& s, size_t pos, const char* kw) {
    size_t k = 0;
    while (kw[k] != '\0') {
        if (pos + k >= s.size())
            return false;
        const unsigned char a = (unsigned char)s[pos + k];
        const unsigned char b = (unsigned char)kw[k];
        const unsigned char al = (a >= 'A' && a <= 'Z') ? (unsigned char)(a + 32) : a;
        const unsigned char bl = (b >= 'A' && b <= 'Z') ? (unsigned char)(b + 32) : b;
        if (al != bl)
            return false;
        ++k;
    }
    if (pos > 0 && isWordChar((unsigned char)s[pos - 1]))
        return false;
    if (pos + k < s.size() && isWordChar((unsigned char)s[pos + k]))
        return false;
    return true;
}

size_t findKeyword(const std::string& s, const char* kw, size_t from) {
    const size_t n = s.size();
    size_t klen = 0;
    while (kw[klen] != '\0')
        ++klen;
    if (klen == 0 || n < klen)
        return std::string::npos;
    for (size_t i = from; i + klen <= n; ++i) {
        if (matchKeywordAt(s, i, kw))
            return i;
    }
    return std::string::npos;
}

// 在 masked 里找 "INSERT <空白> INTO"，返回 INTO 之后的位置；找不到返回 npos。
size_t findInsertInto(const std::string& masked, size_t from) {
    size_t p = from;
    while (true) {
        const size_t ins = findKeyword(masked, "INSERT", p);
        if (ins == std::string::npos)
            return std::string::npos;
        size_t j = ins + 6;
        while (j < masked.size() &&
               (masked[j] == ' ' || masked[j] == '\t' || masked[j] == '\r' ||
                masked[j] == '\n'))
            ++j;
        if (matchKeywordAt(masked, j, "INTO"))
            return j + 4;
        p = ins + 6;
    }
}

enum ParseStage {
    STAGE_OK = 0,
    STAGE_NO_INSERT = 1,
    STAGE_NO_COLUMN_PAREN = 2,
    STAGE_NO_COLUMN_CLOSE = 3,
    STAGE_NO_COLUMNS = 4,
    STAGE_NO_VALUES = 5,
    STAGE_NO_ROWS = 6,
};

// 解析 INSERT INTO ... (列) VALUES (...), (...)；失败返回 false，
// stageOut（可空）填入失败阶段，见 ParseStage。
bool parseInsert(const std::string& stmt, Parsed& out, int* stageOut = nullptr) {
    const std::string masked = maskText(stmt);

    const size_t afterInsert = findInsertInto(masked, 0);
    if (afterInsert == std::string::npos) {
        if (stageOut)
            *stageOut = STAGE_NO_INSERT;
        return false;
    }

    const size_t ci = masked.find('(', afterInsert);  // 列名列表的左括号
    if (ci == std::string::npos) {
        if (stageOut)
            *stageOut = STAGE_NO_COLUMN_PAREN;
        return false;
    }
    const int cj = matchParen(masked, (int)ci);
    if (cj < 0) {
        if (stageOut)
            *stageOut = STAGE_NO_COLUMN_CLOSE;
        return false;
    }

    std::vector<Range> columns;
    for (const Range& r : splitTopLevel(masked, (int)ci + 1, cj)) {
        if (!isBlank(stmt, r))
            columns.push_back(r);
    }
    if (columns.empty()) {
        if (stageOut)
            *stageOut = STAGE_NO_COLUMNS;
        return false;
    }

    const size_t vpos = findKeyword(masked, "VALUES", ci);
    if (vpos == std::string::npos) {
        if (stageOut)
            *stageOut = STAGE_NO_VALUES;
        return false;
    }
    size_t i = vpos + 6;
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
    if (rows.empty()) {
        if (stageOut)
            *stageOut = STAGE_NO_ROWS;
        return false;
    }

    if (stageOut)
        *stageOut = STAGE_OK;
    out.columns = columns;
    out.rows = rows;
    return true;
}

// 把 stmt 的前 120 字节转义成可读预览：可打印 ASCII 原样，换行/制表转义，
// 其余字节写成 \xNN。这样日志里能看出读到的到底是 UTF-8、GBK、BOM
// 还是别的编码，而不会把原始中文直接写进日志文件。
std::string previewEscaped(const std::string& s) {
    std::string out;
    const size_t n = s.size() < 120 ? s.size() : 120;
    char tmp[8];
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c >= 32 && c < 127)
            out += (char)c;
        else {
            std::snprintf(tmp, sizeof(tmp), "\\x%02X", c);
            out += tmp;
        }
    }
    if (s.size() > 120)
        out += "...";
    return out;
}

}  // namespace

void setIndicators(int valueIndic, int columnIndic) {
    if (valueIndic >= 0)
        INDIC_VALUE = valueIndic;
    if (columnIndic >= 0)
        INDIC_COLUMN = columnIndic;
}

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

std::string diagnose(HWND sci, int pos) {
    const int docLen = sciLength(sci);
    int base = 0;
    std::string stmt;
    char buf[1024];
    if (!statementAt(sci, pos, base, stmt)) {
        std::snprintf(buf, sizeof(buf),
                      "docLen=%d pos=%d stage=no-statement stmtLen=0",
                      docLen, pos);
        return std::string(buf);
    }
    Parsed p;
    int stage = 0;
    if (!parseInsert(stmt, p, &stage)) {
        const char* stageName = "unknown";
        switch (stage) {
            case STAGE_NO_INSERT: stageName = "no-insert"; break;
            case STAGE_NO_COLUMN_PAREN: stageName = "no-column-paren"; break;
            case STAGE_NO_COLUMN_CLOSE: stageName = "no-column-close"; break;
            case STAGE_NO_COLUMNS: stageName = "no-columns"; break;
            case STAGE_NO_VALUES: stageName = "no-values"; break;
            case STAGE_NO_ROWS: stageName = "no-rows"; break;
            default: break;
        }
        std::snprintf(buf, sizeof(buf),
                      "docLen=%d pos=%d base=%d stmtLen=%d stage=%s "
                      "preview='%s'",
                      docLen, pos, base, (int)stmt.size(), stageName,
                      previewEscaped(stmt).c_str());
        return std::string(buf);
    }
    const int rel = pos - base;
    bool hit = false;
    for (const Range& r : p.columns) {
        if (r.first <= rel && rel <= r.second)
            hit = true;
    }
    if (!hit) {
        for (const auto& row : p.rows) {
            for (const Range& r : row) {
                if (r.first <= rel && rel <= r.second)
                    hit = true;
            }
        }
    }
    std::snprintf(buf, sizeof(buf),
                  "docLen=%d pos=%d base=%d rel=%d stmtLen=%d "
                  "stage=%s cols=%d rows=%d preview='%s'",
                  docLen, pos, base, rel, (int)stmt.size(),
                  hit ? "parsed-hit" : "parsed-no-hit",
                  (int)p.columns.size(), (int)p.rows.size(),
                  previewEscaped(stmt).c_str());
    return std::string(buf);
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
