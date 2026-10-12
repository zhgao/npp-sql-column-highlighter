// Highlighter.h — SQL INSERT 列/值解析与高亮绘制。
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <utility>

namespace sqlhl {

// 对光标位置的分析结果。所有下标都是字节下标：
// stmt 内为相对下标，绘制时需加上 base 得到文档绝对下标。
struct HitInfo {
    std::string stmt;
    int base = 0;   // stmt 在文档中的起始字节下标
    std::vector<std::pair<int, int>> columns;                 // 列范围（stmt 内相对下标）
    std::vector<std::vector<std::pair<int, int>>> rows;       // 每个 VALUES 行的值范围
    int colIdx = -1;
    int rowIdx = -1;        // 命中值时有效
    bool isColumn = false;  // true = 光标在列名上，false = 光标在值上
};

// 分析 sci 中 pos 处的 INSERT 语句；找到列/值对应关系返回 true。
bool analyse(HWND sci, int pos, HitInfo& out);

// 按 HitInfo 绘制高亮：列名蓝色，值琥珀色。
void highlight(HWND sci, const HitInfo& hit);

// 清除本插件画的所有高亮。
void clearHighlights(HWND sci);

// 初始化指示器样式（幂等，可重复调用）。
void setupIndicators(HWND sci);

} // namespace sqlhl
