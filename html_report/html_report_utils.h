#pragma once
// html_report_utils.h — HTML 报告生成辅助工具函数
// 纯 header-only，被 html_report_dll_main.cpp 包含

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>

#include "html_report_dll_interface.h"

namespace report_util {

// 时间戳(ms) -> 格式化字符串
inline std::string TimestampToStr(int64_t ts_ms, const char* fmt = "%Y-%m-%d %H:%M:%S") {
    if (ts_ms <= 0) return "-";
    time_t sec = (time_t)(ts_ms / 1000);
    struct tm tb;
#ifdef _WIN32
    localtime_s(&tb, &sec);
#else
    localtime_r(&sec, &tb);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), fmt, &tb);
    return buf;
}

// 时间戳(ms) -> 日期字符串 YYYYMMDD
inline std::string TimestampToDate(int64_t ts_ms) {
    return TimestampToStr(ts_ms, "%Y%m%d");
}

// 清理币种名中的斜杠: "ETH/USDT" -> "ETHUSDT"
inline std::string CleanSymbol(const std::string& pair) {
    std::string result;
    for (char c : pair) {
        if (c != '/' && c != ' ') result += c;
    }
    return result;
}

// HTML 转义
inline std::string HtmlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<':  r += "&lt;";   break;
            case '>':  r += "&gt;";   break;
            case '&':  r += "&amp;";  break;
            case '"':  r += "&quot;"; break;
            case '\'': r += "&#39;";  break;
            default:   r += c;
        }
    }
    return r;
}

// 操作类型描述
inline const char* OperateDesc(const char* type) {
    if (strcmp(type, "LO") == 0) return "开多";
    if (strcmp(type, "LE") == 0) return "平多";
    if (strcmp(type, "SO") == 0) return "开空";
    if (strcmp(type, "SE") == 0) return "平空";
    return type;
}

// 匹配状态描述
inline const char* MatchStatusDesc(int status) {
    switch (status) {
        case 0: return "未成交";
        case 1: return "成交";
        case 2: return "平仓";
        case 3: return "已撤销";
        default: return "未知";
    }
}

// double 格式化
inline std::string FmtDouble(double v, int prec = 4) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

// 每日收益记录
struct DailyReturn {
    std::string date;
    double profit;
};

// 计算每日收益
inline std::vector<DailyReturn> CalcDailyReturns(const ReportOrder* orders, int count) {
    std::map<std::string, double> daily;
    for (int i = 0; i < count; ++i) {
        if (orders[i].profit == 0.0) continue;
        std::string day = TimestampToStr(orders[i].timestamp, "%Y-%m-%d");
        daily[day] += orders[i].profit;
    }
    std::vector<DailyReturn> result;
    for (auto& kv : daily) {
        result.push_back({kv.first, kv.second});
    }
    std::sort(result.begin(), result.end(),
              [](const DailyReturn& a, const DailyReturn& b) { return a.date < b.date; });
    return result;
}

// 资金曲线点
struct EquityPoint {
    std::string dt;
    double equity;
};

// 计算资金曲线
inline std::vector<EquityPoint> CalcEquityCurve(
    const ReportOrder* orders, int count, double initial_capital)
{
    struct ProfitRec { int64_t ts; double profit; };
    std::vector<ProfitRec> recs;
    for (int i = 0; i < count; ++i) {
        if (orders[i].profit == 0.0) continue;
        int64_t ts = orders[i].matchtimestamp > 0 ? orders[i].matchtimestamp : orders[i].timestamp;
        recs.push_back({ts, orders[i].profit});
    }
    std::sort(recs.begin(), recs.end(),
              [](const ProfitRec& a, const ProfitRec& b) { return a.ts < b.ts; });

    std::vector<EquityPoint> curve;
    if (!recs.empty()) {
        curve.push_back({TimestampToStr(recs[0].ts), initial_capital});
    }
    double equity = initial_capital;
    for (auto& r : recs) {
        equity += r.profit;
        curve.push_back({TimestampToStr(r.ts), equity});
    }
    return curve;
}

} // namespace report_util