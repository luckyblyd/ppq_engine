
// html_report_dll_main.cpp — HTML 回测报告 DLL 主实现
// 纯 C++ 实现，不依赖 Python，生成独立的 HTML 报告
//
// 编译 (MinGW):
//   g++ -std=c++17 -shared -o html_report_dll.dll html_report_dll_main.cpp
//       -DHTML_REPORT_DLL_EXPORTS -fexec-charset=UTF-8

#define HTML_REPORT_DLL_EXPORTS

#ifdef _WIN32
#include <windows.h>
#endif

#include "html_report_dll_interface.h"
#include "html_report_utils.h"
#include "html_report_html_parts.h"

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>

using namespace report_util;
using namespace report_html;

// ============================================================================
// HtmlReportBuilder — HTML报告生成器
// ============================================================================

class HtmlReportBuilder {
public:
    void SetConfig(const ReportConfig& cfg) { config_ = cfg; }
    void SetBars(const ReportBar* bars, int count) { bars_.assign(bars, bars + count); }
    void SetOrders(const ReportOrder* orders, int count) { orders_.assign(orders, orders + count); }
    void SetPerformance(const ReportPerformance& perf) { perf_ = perf; }

    bool Generate(const std::string& filepath) {
        std::ostringstream html;
        WriteDocHead(html);
        WriteHeader(html);
        WriteMetricsCards(html);
        WriteTabNav(html);
        WriteKlineTab(html);
        WriteEquityTab(html);
        WriteOrdersTab(html);
        WriteDailyTab(html);
        WriteMetricsTab(html);
        WriteScripts(html);
        html << "</div>\n</body>\n</html>\n";

        std::string content = html.str();

        // 尝试多种方式打开文件（兼容中文路径）
        bool written = false;
#ifdef _WIN32
        // 方式1: 使用 Windows API 宽字符写文件（最可靠）
        {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
            if (wlen <= 0) {
                // UTF-8 转换失败，尝试 ANSI
                wlen = MultiByteToWideChar(CP_ACP, 0, filepath.c_str(), -1, nullptr, 0);
            }
            if (wlen > 0) {
                std::wstring wpath(wlen, 0);
                // 先尝试 UTF-8
                int r = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);
                if (r <= 0) {
                    // 回退 ANSI
                    MultiByteToWideChar(CP_ACP, 0, filepath.c_str(), -1, &wpath[0], wlen);
                }
                HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0, nullptr,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD bytesWritten = 0;
                    // 写入 UTF-8 BOM
                    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
                    WriteFile(hFile, bom, 3, &bytesWritten, nullptr);
                    WriteFile(hFile, content.c_str(), (DWORD)content.size(), &bytesWritten, nullptr);
                    CloseHandle(hFile);
                    written = true;
                }
            }
        }
#endif
        // 方式2: 标准 C++ ofstream
        if (!written) {
            std::ofstream ofs(filepath, std::ios::out | std::ios::binary);
            if (ofs.is_open()) {
                // 写入 UTF-8 BOM
                const char bom[] = {'\xEF', '\xBB', '\xBF'};
                ofs.write(bom, 3);
                ofs.write(content.c_str(), (std::streamsize)content.size());
                ofs.close();
                written = true;
            }
        }
        return written;
    }

private:
    ReportConfig config_;
    std::vector<ReportBar> bars_;
    std::vector<ReportOrder> orders_;
    ReportPerformance perf_;

    // ---- HTML <head> ----
    void WriteDocHead(std::ostringstream& h) {
        h << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n"
          << "<meta charset=\"UTF-8\">\n"
          << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
          << "<title>" << HtmlEscape(config_.currency_pair) << " - "
          << "\xE5\x9B\x9E\xE6\xB5\x8B\xE6\x8A\xA5\xE5\x91\x8A" << "</title>\n"
          << "<script src=\"https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js\"></script>\n"
          << GetCssStyle() << "\n"
          << "</head>\n<body>\n<div class=\"container\">\n";
    }

    // ---- 页面标题 ----
    void WriteHeader(std::ostringstream& h) {
        std::string start_s = TimestampToStr(config_.backtest_start_ts);
        std::string end_s   = TimestampToStr(config_.backtest_end_ts);
        h << "<div class=\"header\">\n"
          << "  <h1>" << HtmlEscape(config_.currency_pair)
          << " \xE5\x9B\x9E\xE6\xB5\x8B\xE6\x8A\xA5\xE5\x91\x8A</h1>\n"
          << "  <div class=\"subtitle\">"
          << "\xE7\xAD\x96\xE7\x95\xA5: " << HtmlEscape(config_.strategy_name)
          << " | \xE5\x91\xA8\xE6\x9C\x9F: " << config_.timeframe
          << " | " << start_s << " ~ " << end_s << "</div>\n"
          << "</div>\n\n";
    }

    // ---- 指标卡片 ----
    void WriteMetricsCards(std::ostringstream& h) {
        double returns_pct = perf_.total_return * 100;
        h << "<div class=\"metrics-grid\">\n";
        WriteOneMetric(h, "\xE6\x80\xBB\xE6\x94\xB6\xE7\x9B\x8A\xE7\x8E\x87",
                       FmtDouble(returns_pct, 2) + "%", returns_pct >= 0);
        WriteOneMetric(h, "\xE5\x87\x80\xE5\x88\xA9\xE6\xB6\xA6",
                       FmtDouble(perf_.net_profit, 2), perf_.net_profit >= 0);
        WriteOneMetric(h, "\xE8\x83\x9C\xE7\x8E\x87",
                       FmtDouble(perf_.win_rate * 100, 2) + "%", true);
        WriteOneMetric(h, "\xE7\x9B\x88\xE4\xBA\x8F\xE6\xAF\x94",
                       FmtDouble(perf_.profit_factor, 2), true);
        WriteOneMetric(h, "\xE6\x80\xBB\xE4\xBA\xA4\xE6\x98\x93\xE6\xAC\xA1\xE6\x95\xB0",
                       std::to_string(perf_.total_trades), true);
        WriteOneMetric(h, "\xE6\x9C\x80\xE5\xA4\xA7\xE5\x9B\x9E\xE6\x92\xA4",
                       FmtDouble(perf_.max_drawdown_percent, 2) + "%", false);
        WriteOneMetric(h, "\xE5\xB8\x81\xE7\xA7\x8D\xE6\xB6\xA8\xE8\xB7\x8C",
                       FmtDouble(perf_.price_change_pct, 2) + "%", perf_.price_change_pct >= 0);
        WriteOneMetric(h, "\xE6\x80\xBB\xE6\x89\x8B\xE7\xBB\xAD\xE8\xB4\xB9",
                       FmtDouble(perf_.total_commission, 2), false);
        h << "</div>\n\n";
    }

    void WriteOneMetric(std::ostringstream& h, const char* label,
                        const std::string& value, bool positive) {
        const char* cls = positive ? "positive" : "negative";
        h << "  <div class=\"metric-card\">\n"
          << "    <div class=\"label\">" << label << "</div>\n"
          << "    <div class=\"value " << cls << "\">" << value << "</div>\n"
          << "  </div>\n";
    }

    // ---- Tab导航 ----
    void WriteTabNav(std::ostringstream& h) {
        h << "<div class=\"tabs\">\n"
          << "  <button class=\"active\" onclick=\"switchTab(0)\">K\xE7\xBA\xBF\xE8\xB5\xB0\xE5\x8A\xBF</button>\n"
          << "  <button onclick=\"switchTab(1)\">\xE8\xB5\x84\xE9\x87\x91\xE6\x9B\xB2\xE7\xBA\xBF</button>\n"
          << "  <button onclick=\"switchTab(2)\">\xE5\xA7\x94\xE6\x89\x98\xE8\xAE\xB0\xE5\xBD\x95</button>\n"
          << "  <button onclick=\"switchTab(3)\">\xE6\xAF\x8F\xE6\x97\xA5\xE6\x94\xB6\xE7\x9B\x8A</button>\n"
          << "  <button onclick=\"switchTab(4)\">\xE8\xAF\xA6\xE7\xBB\x86\xE6\x8C\x87\xE6\xA0\x87</button>\n"
          << "</div>\n\n";
    }

    // ---- Tab 0: K线走势 ----
    void WriteKlineTab(std::ostringstream& h) {
        h << "<div class=\"tab-panel active\" id=\"tab0\">\n"
          << "  <div id=\"kline_chart\" class=\"chart-container\"></div>\n"
          << "</div>\n\n";
    }

    // ---- Tab 1: 资金曲线 ----
    void WriteEquityTab(std::ostringstream& h) {
        h << "<div class=\"tab-panel\" id=\"tab1\">\n"
          << "  <div id=\"equity_chart\" class=\"chart-container-sm\"></div>\n"
          << "</div>\n\n";
    }

    // ---- Tab 2: 委托记录 ----
    void WriteOrdersTab(std::ostringstream& h) {
        h << "<div class=\"tab-panel\" id=\"tab2\">\n";

        // 过滤器
        h << "  <div class=\"filter-bar\">\n"
          << "    <label><input type=\"checkbox\" id=\"hideCancelled\" checked "
          << "onchange=\"filterOrders()\">"
          << " \xE5\xB1\x8F\xE8\x94\xBD\xE5\xB7\xB2\xE6\x92\xA4\xE9\x94\x80</label>\n"
          << "    <label><input type=\"checkbox\" id=\"hideUnfilled\" "
          << "onchange=\"filterOrders()\">"
          << " \xE5\xB1\x8F\xE8\x94\xBD\xE6\x9C\xAA\xE6\x88\x90\xE4\xBA\xA4</label>\n"
          << "    <span id=\"orderCount\" style=\"margin-left:auto;"
          << "font-size:13px;color:#6c757d;\"></span>\n"
          << "  </div>\n";

        // 表格
        h << "  <div class=\"table-container\">\n"
          << "  <table id=\"ordersTable\">\n"
          << "  <thead><tr>\n"
          << "    <th>\xE6\x97\xB6\xE9\x97\xB4</th>"
          << "<th>\xE7\xB1\xBB\xE5\x9E\x8B</th>"
          << "<th>\xE4\xBB\xB7\xE6\xA0\xBC</th>"
          << "<th>\xE6\x95\xB0\xE9\x87\x8F</th>"
          << "<th>\xE9\x87\x91\xE9\xA2\x9D</th>"
          << "<th>\xE7\x8A\xB6\xE6\x80\x81</th>"
          << "<th>\xE6\x88\x90\xE4\xBA\xA4\xE6\x97\xB6\xE9\x97\xB4</th>"
          << "<th>\xE5\xA7\x94\xE6\x89\x98\xE5\x8F\xB7</th>"
          << "<th>\xE5\x8E\x9F\xE8\xAE\xA2\xE5\x8D\x95ID</th>"
          << "<th>\xE5\xA4\x87\xE6\xB3\xA8</th>"
          << "<th>\xE7\x9B\x88\xE4\xBA\x8F</th>"
          << "<th>\xE6\xAD\xA2\xE7\x9B\x88</th>"
          << "<th>\xE6\xAD\xA2\xE6\x8D\x9F</th>\n"
          << "  </tr></thead>\n<tbody>\n";

        // 排序后输出
        std::vector<ReportOrder> sorted_orders(orders_);
        std::sort(sorted_orders.begin(), sorted_orders.end(),
                  [](const ReportOrder& a, const ReportOrder& b) {
                      return a.timestamp < b.timestamp;
                  });

        for (auto& o : sorted_orders) {
            const char* badge_cls = "badge-danger";
            if (o.matchstatus == 1) badge_cls = "badge-success";
            else if (o.matchstatus == 2) badge_cls = "badge-warning";
            else if (o.matchstatus == 3) badge_cls = "badge-secondary";

            std::string profit_color = "inherit";
            if (o.profit > 0) profit_color = "green";
            else if (o.profit < 0) profit_color = "red";

            h << "    <tr data-status=\"" << o.matchstatus << "\">\n"
              << "      <td>" << TimestampToStr(o.timestamp) << "</td>\n"
              << "      <td>" << OperateDesc(o.type) << "</td>\n"
              << "      <td>" << FmtDouble(o.price) << "</td>\n"
              << "      <td>" << FmtDouble(o.quantity) << "</td>\n"
              << "      <td>" << FmtDouble(o.amount) << "</td>\n"
              << "      <td><span class=\"badge " << badge_cls << "\">"
              << MatchStatusDesc(o.matchstatus) << "</span></td>\n"
              << "      <td>" << TimestampToStr(o.matchtimestamp) << "</td>\n"
              << "      <td>" << o.id << "</td>\n"
              << "      <td>" << (o.old_orderid > 0 ? std::to_string(o.old_orderid) : "-") << "</td>\n"
              << "      <td>" << HtmlEscape(o.remark) << "</td>\n"
              << "      <td style=\"color:" << profit_color << "\">"
              << (o.profit != 0.0 ? FmtDouble(o.profit, 2) : "-") << "</td>\n"
              << "      <td>" << (o.take_profit_price > 0 ? FmtDouble(o.take_profit_price) : "-") << "</td>\n"
              << "      <td>" << (o.stop_loss_price > 0 ? FmtDouble(o.stop_loss_price) : "-") << "</td>\n"
              << "    </tr>\n";
        }

        h << "</tbody>\n</table>\n</div>\n</div>\n\n";
    }

    // ---- Tab 3: 每日收益 ----
    void WriteDailyTab(std::ostringstream& h) {
        auto daily = CalcDailyReturns(orders_.data(), (int)orders_.size());

        h << "<div class=\"tab-panel\" id=\"tab3\">\n"
          << "  <div class=\"card\">\n"
          << "    <div class=\"card-title\">"
          << "\xE6\xAF\x8F\xE6\x97\xA5\xE6\x94\xB6\xE7\x9B\x8A\xE7\x82\xB9\xE9\x98\xB5\xE5\x9B\xBE"
          << "</div>\n    <div class=\"dot-chart\">\n";

        for (auto& d : daily) {
            std::string cls = d.profit >= 0 ? "positive" : "negative";
            std::string sign_char = d.profit >= 0 ? "+" : "-";
            std::string sign_val = d.profit >= 0 ? "+" : "";
            h << "      <div class=\"dot " << cls << "\">" << sign_char
              << "<div class=\"dot-tooltip\">" << d.date << ": "
              << sign_val << FmtDouble(d.profit, 2) << "</div></div>\n";
        }

        h << "    </div>\n  </div>\n\n";

        // 每日收益明细表
        h << "  <div class=\"card\">\n"
          << "    <div class=\"card-title\">"
          << "\xE6\xAF\x8F\xE6\x97\xA5\xE6\x94\xB6\xE7\x9B\x8A\xE6\x98\x8E\xE7\xBB\x86"
          << "</div>\n"
          << "    <table><thead><tr>"
          << "<th>\xE6\x97\xA5\xE6\x9C\x9F</th>"
          << "<th>\xE6\x97\xA5\xE6\x94\xB6\xE7\x9B\x8A</th>"
          << "</tr></thead>\n<tbody>\n";

        for (auto& d : daily) {
            std::string color = d.profit >= 0 ? "green" : "red";
            h << "    <tr><td>" << d.date << "</td>"
              << "<td style=\"color:" << color << "\">"
              << FmtDouble(d.profit, 2) << "</td></tr>\n";
        }
        h << "</tbody></table>\n</div>\n</div>\n\n";
    }

    // ---- Tab 4: 详细指标 ----
    void WriteMetricsTab(std::ostringstream& h) {
        h << "<div class=\"tab-panel\" id=\"tab4\">\n"
          << "  <div class=\"card\">\n"
          << "    <div class=\"card-title\">"
          << HtmlEscape(config_.currency_pair)
          << " - \xE5\x9B\x9E\xE6\xB5\x8B\xE6\x80\xA7\xE8\x83\xBD\xE6\x8C\x87\xE6\xA0\x87"
          << "</div>\n"
          << "    <table>\n"
          << "      <thead><tr>"
          << "<th>\xE6\x8C\x87\xE6\xA0\x87</th>"
          << "<th>\xE6\x95\xB0\xE5\x80\xBC</th>"
          << "</tr></thead>\n      <tbody>\n";

        auto row = [&](const char* name, const std::string& val) {
            h << "        <tr><td>" << name << "</td><td>" << val << "</td></tr>\n";
        };

        row("\xE5\x88\x9D\xE5\xA7\x8B\xE8\xB5\x84\xE9\x87\x91", FmtDouble(perf_.initial_capital, 2));
        row("\xE6\x9C\x80\xE7\xBB\x88\xE8\xB5\x84\xE9\x87\x91", FmtDouble(perf_.final_capital, 2));
        row("\xE6\x80\xBB\xE6\x94\xB6\xE7\x9B\x8A\xE7\x8E\x87", FmtDouble(perf_.total_return * 100, 2) + "%");
        row("\xE5\xB8\x81\xE7\xA7\x8D\xE6\xB6\xA8\xE8\xB7\x8C", FmtDouble(perf_.price_change_pct, 2) + "%");
        row("\xE6\x80\xBB\xE4\xBA\xA4\xE6\x98\x93\xE6\xAC\xA1\xE6\x95\xB0", std::to_string(perf_.total_trades));
        row("\xE7\x9B\x88\xE5\x88\xA9\xE6\xAC\xA1\xE6\x95\xB0", std::to_string(perf_.winning_trades));
        row("\xE4\xBA\x8F\xE6\x8D\x9F\xE6\xAC\xA1\xE6\x95\xB0", std::to_string(perf_.losing_trades));
        row("\xE8\x83\x9C\xE7\x8E\x87", FmtDouble(perf_.win_rate * 100, 2) + "%");
        row("\xE6\x80\xBB\xE7\x9B\x88\xE5\x88\xA9", FmtDouble(perf_.total_profit, 2));
        row("\xE6\x80\xBB\xE4\xBA\x8F\xE6\x8D\x9F", FmtDouble(perf_.total_loss, 2));
        row("\xE5\x87\x80\xE5\x88\xA9\xE6\xB6\xA6", FmtDouble(perf_.net_profit, 2));
        row("\xE7\x9B\x88\xE4\xBA\x8F\xE6\xAF\x94", FmtDouble(perf_.profit_factor, 2));
        row("\xE6\x9C\x80\xE5\xA4\xA7\xE5\x9B\x9E\xE6\x92\xA4", FmtDouble(perf_.max_drawdown, 2));
        row("\xE6\x9C\x80\xE5\xA4\xA7\xE5\x9B\x9E\xE6\x92\xA4\xE7\x99\xBE\xE5\x88\x86\xE6\xAF\x94",
            FmtDouble(perf_.max_drawdown_percent, 2) + "%");
        row("\xE6\x80\xBB\xE6\x89\x8B\xE7\xBB\xAD\xE8\xB4\xB9", FmtDouble(perf_.total_commission, 2));

        h << "      </tbody>\n    </table>\n  </div>\n</div>\n\n";
    }

    // ---- JavaScript: 图表 ----
    void WriteScripts(std::ostringstream& h) {
        h << "<script>\n";
        h << GetJsTabsAndFilter();
        h << "\n";
        WriteKlineChartJS(h);
        WriteEquityChartJS(h);
        h << "</script>\n";
    }

    // ---- ECharts K线图 ----
    void WriteKlineChartJS(std::ostringstream& h) {
        // 降采样：如果K线过多，只显示最近的2000根
        int start_idx = 0;
        int bar_count = (int)bars_.size();
        if (bar_count > 2000) start_idx = bar_count - 2000;

        // 准备数据数组
        h << "var kDates = [";
        for (int i = start_idx; i < bar_count; ++i) {
            if (i > start_idx) h << ",";
            h << "\"" << TimestampToStr(bars_[i].timestamp) << "\"";
        }
        h << "];\n";

        h << "var kData = [";
        for (int i = start_idx; i < bar_count; ++i) {
            if (i > start_idx) h << ",";
            // [open, close, low, high]
            h << "[" << FmtDouble(bars_[i].open)
              << "," << FmtDouble(bars_[i].close)
              << "," << FmtDouble(bars_[i].low)
              << "," << FmtDouble(bars_[i].high) << "]";
        }
        h << "];\n";

        h << "var kVolume = [";
        for (int i = start_idx; i < bar_count; ++i) {
            if (i > start_idx) h << ",";
            h << FmtDouble(bars_[i].volume, 2);
        }
        h << "];\n";

        // 买卖标记（只标记非撤销的委托）
        h << "var buyMarks = [];\nvar sellMarks = [];\n";
        for (auto& o : orders_) {
            if (o.matchstatus == 3) continue; // 跳过已撤销
            int64_t mark_ts = o.matchtimestamp > 0 ? o.matchtimestamp : o.timestamp;
            std::string dt = TimestampToStr(mark_ts);
            bool is_buy = (strcmp(o.type, "LO") == 0 || strcmp(o.type, "SE") == 0);
            const char* arr = is_buy ? "buyMarks" : "sellMarks";
            h << arr << ".push({name:'" << OperateDesc(o.type)
              << "',coord:['" << dt << "'," << FmtDouble(o.price)
              << "],value:'" << OperateDesc(o.type) << " " << FmtDouble(o.price)
              << "'});\n";
        }

        // ECharts 配置
        h << R"JS(
var klineChart = echarts.init(document.getElementById('kline_chart'));
window.klineChart = klineChart;
klineChart.setOption({
    tooltip: { trigger: 'axis', axisPointer: { type: 'cross' } },
    legend: { data: ['K\u7EBF', '\u6210\u4EA4\u91CF'] },
    grid: [
        { left: '5%', right: '2%', top: '5%', height: '55%' },
        { left: '5%', right: '2%', top: '68%', height: '18%' }
    ],
    xAxis: [
        { type: 'category', data: kDates, gridIndex: 0,
          axisLabel: { fontSize: 10 }, boundaryGap: true },
        { type: 'category', data: kDates, gridIndex: 1,
          axisLabel: { show: false }, boundaryGap: true }
    ],
    yAxis: [
        { scale: true, gridIndex: 0, splitArea: { show: true } },
        { scale: true, gridIndex: 1, splitNumber: 2 }
    ],
    dataZoom: [
        { type: 'inside', xAxisIndex: [0, 1], start: 80, end: 100 },
        { show: true, xAxisIndex: [0, 1], type: 'slider', bottom: '2%',
          start: 80, end: 100 }
    ],
    series: [
        {
            name: 'K\u7EBF', type: 'candlestick', data: kData,
            xAxisIndex: 0, yAxisIndex: 0,
            itemStyle: {
                color: '#ef5350', color0: '#26a69a',
                borderColor: '#ef5350', borderColor0: '#26a69a'
            },
            markPoint: {
                data: buyMarks.map(function(m) {
                    return {
                        name: m.name, coord: m.coord, value: m.value,
                        symbol: 'triangle', symbolSize: 12,
                        symbolRotate: 0,
                        itemStyle: { color: '#ef5350' },
                        label: { show: false }
                    };
                }).concat(sellMarks.map(function(m) {
                    return {
                        name: m.name, coord: m.coord, value: m.value,
                        symbol: 'triangle', symbolSize: 12,
                        symbolRotate: 180,
                        itemStyle: { color: '#26a69a' },
                        label: { show: false }
                    };
                })),
                tooltip: { formatter: function(p) { return p.value; } }
            }
        },
        {
            name: '\u6210\u4EA4\u91CF', type: 'bar', data: kVolume,
            xAxisIndex: 1, yAxisIndex: 1,
            itemStyle: {
                color: function(params) {
                    var d = kData[params.dataIndex];
                    return d && d[1] >= d[0] ? '#ef5350' : '#26a69a';
                }
            }
        }
    ]
});
window.addEventListener('resize', function() { klineChart.resize(); });
)JS";
    }

    // ---- ECharts 资金曲线 ----
    void WriteEquityChartJS(std::ostringstream& h) {
        auto equity = CalcEquityCurve(orders_.data(), (int)orders_.size(), perf_.initial_capital);

        h << "var eqDates = [";
        for (size_t i = 0; i < equity.size(); ++i) {
            if (i > 0) h << ",";
            h << "\"" << equity[i].dt << "\"";
        }
        h << "];\n";

        h << "var eqValues = [";
        for (size_t i = 0; i < equity.size(); ++i) {
            if (i > 0) h << ",";
            h << FmtDouble(equity[i].equity, 2);
        }
        h << "];\n";

        h << R"JS(
var equityChart = echarts.init(document.getElementById('equity_chart'));
window.equityChart = equityChart;
equityChart.setOption({
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'category', data: eqDates, axisLabel: { fontSize: 10 } },
    yAxis: { type: 'value', scale: true },
    series: [{
        name: '\u8D44\u91D1\u66F2\u7EBF',
        type: 'line', data: eqValues, smooth: true,
        lineStyle: { width: 2, color: '#0d6efd' },
        areaStyle: { color: 'rgba(13,110,253,0.1)' },
        itemStyle: { color: '#0d6efd' }
    }],
    dataZoom: [
        { type: 'inside', start: 0, end: 100 },
        { show: true, type: 'slider', bottom: '2%', start: 0, end: 100 }
    ]
});
window.addEventListener('resize', function() { equityChart.resize(); });
)JS";
    }
}; // end class HtmlReportBuilder


// ============================================================================
// DLL 导出函数实现
// ============================================================================

static HtmlReportBuilder* g_builder = nullptr;

HTML_REPORT_API int report_init() {
    if (!g_builder) {
        g_builder = new HtmlReportBuilder();
    }
    return 0;
}

HTML_REPORT_API void report_destroy() {
    delete g_builder;
    g_builder = nullptr;
}

HTML_REPORT_API int report_generate(
    const ReportConfig* config,
    const ReportBar* bars,
    int bar_count,
    const ReportOrder* orders,
    int order_count,
    const ReportPerformance* perf,
    char* out_path,
    int out_path_size)
{
    if (!config || !perf) return -1;

    HtmlReportBuilder builder;
    builder.SetConfig(*config);
    if (bars && bar_count > 0) builder.SetBars(bars, bar_count);
    if (orders && order_count > 0) builder.SetOrders(orders, order_count);
    builder.SetPerformance(*perf);

    std::string output_dir(config->output_dir);
    if (output_dir.empty()) output_dir = ".";

    try {
    #ifdef _WIN32
        // ==========================
        // Windows 专用：UTF-8 → 宽字符串创建目录（彻底解决乱码）
        // ==========================
        int wlen = MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wdir(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, wdir.data(), wlen);
            CreateDirectoryW(wdir.c_str(), nullptr); // 只调用 W 版！
        }
    #else
        // Linux / macOS 直接用 UTF-8 即可
        std::filesystem::create_directories(output_dir);
    #endif
    } catch (...) {
        // 目录已存在时会抛异常，直接忽略
    }

    // 文件名拼接
    std::string date_str = TimestampToDate(config->backtest_start_ts);
    std::string symbol = CleanSymbol(config->currency_pair);
    std::string filename = date_str + symbol + ".html";

    std::string filepath = output_dir;
    // 确保路径分隔符
    if (!filepath.empty() && filepath.back() != '/' && filepath.back() != '\\') {
        filepath += '/';
    }
    filepath += filename;

    // 生成报告
    bool ok = false;
    try {
        ok = builder.Generate(filepath);
    } catch (const std::exception& e) {
        // 生成失败，尝试在当前目录生成
        (void)e;
        filepath = filename;  // 回退到当前目录
        try {
            ok = builder.Generate(filepath);
        } catch (...) {
            return -2;
        }
    } catch (...) {
        // 回退到当前目录
        filepath = filename;
        try {
            ok = builder.Generate(filepath);
        } catch (...) {
            return -3;
        }
    }

    // 输出路径
    if (out_path && out_path_size > 0) {
        strncpy(out_path, filepath.c_str(), out_path_size - 1);
        out_path[out_path_size - 1] = '\0';
    }

    return ok ? 0 : -1;
}

// ============================================================================
// DllMain (Windows)
// ============================================================================
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)hModule; (void)reserved;
    if (reason == DLL_PROCESS_DETACH) {
        delete g_builder;
        g_builder = nullptr;
    }
    return TRUE;
}
#endif