#pragma once
// html_report_dll_interface.h — HTML 回测报告 DLL 的 C 接口定义
// 主程序通过此头文件动态加载 html_report_dll.dll
// 回测完成后调用此 DLL 生成 HTML 报告
// 命名规范: 回测开始时间+标的  例如: 20260101ETHUSDT.html

#include <cstdint>

#ifdef HTML_REPORT_DLL_EXPORTS
  #define HTML_REPORT_API __declspec(dllexport)
#else
  #define HTML_REPORT_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 数据结构（C 兼容，用于跨 DLL 边界传递）
// ============================================================================

// 订单数据（传入DLL）
typedef struct {
    int         id;
    int64_t     timestamp;          // 下单时间戳(ms)
    char        currency_pair[32];  // "ETH/USDT"
    double      price;
    double      quantity;
    double      amount;             // price * quantity
    char        type[8];            // "LO"/"LE"/"SO"/"SE"
    int         order_type;         // 0=限价, 1=市价
    double      take_profit_price;
    double      stop_loss_price;
    int         matchstatus;        // 0=未成交, 1=成交, 2=平仓, 3=已撤销
    int64_t     matchtimestamp;     // 成交时间戳(ms)
    double      profit;
    int         old_orderid;        // 关联的开仓订单ID
    char        remark[128];
} ReportOrder;

// K线数据（传入DLL）
typedef struct {
    int64_t     timestamp;          // 开盘时间(ms)
    double      open;
    double      high;
    double      low;
    double      close;
    double      volume;
    double      quote_volume;
    double      change_pct;
    char        currency_pair[32];
    char        timeframe[8];
} ReportBar;

// 性能指标（传入DLL）
typedef struct {
    int         total_trades;
    int         winning_trades;
    int         losing_trades;
    double      win_rate;
    double      profit_factor;
    double      total_profit;
    double      total_loss;
    double      net_profit;
    double      max_drawdown;
    double      max_drawdown_percent;
    double      initial_capital;
    double      final_capital;
    double      total_return;       // 总收益率
    double      price_change_pct;   // 币种涨跌幅
    double      total_commission;   // 总手续费
} ReportPerformance;

// 报告配置（传入DLL）
typedef struct {
    char        currency_pair[32];  // "ETH/USDT"
    char        timeframe[8];       // "5m"
    char        output_dir[256];    // 输出目录
    char        strategy_name[64];  // 策略名称
    int64_t     backtest_start_ts;  // 回测开始时间戳(ms)
    int64_t     backtest_end_ts;    // 回测结束时间戳(ms)
} ReportConfig;

// ============================================================================
// DLL 函数接口
// ============================================================================

// 初始化报告引擎
// 返回: 0=成功, -1=失败
HTML_REPORT_API int report_init();

// 销毁报告引擎
HTML_REPORT_API void report_destroy();

// 生成 HTML 回测报告
// config:      报告配置
// bars:        K线数据数组
// bar_count:   K线数量
// orders:      订单数组
// order_count: 订单数量
// perf:        性能指标
// out_path:    [输出] 生成的文件路径 (需要至少 512 字节缓冲)
// 返回: 0=成功, -1=失败
HTML_REPORT_API int report_generate(
    const ReportConfig* config,
    const ReportBar* bars,
    int bar_count,
    const ReportOrder* orders,
    int order_count,
    const ReportPerformance* perf,
    char* out_path,
    int out_path_size
);

#ifdef __cplusplus
}
#endif