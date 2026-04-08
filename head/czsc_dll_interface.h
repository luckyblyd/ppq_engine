#pragma once
// czsc_dll_interface.h — CZSC DLL 的 C 接口定义
// 主程序通过此头文件动态加载 czsc_dll.dll

#include <cstdint>

#ifdef CZSC_DLL_EXPORTS
  #define CZSC_API __declspec(dllexport)
#else
  #define CZSC_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 数据结构（C 兼容，用于跨 DLL 边界传递）
// ============================================================================

// K线 Bar 数据（传入DLL）
typedef struct {
    int64_t timestamp;
    double  open;
    double  high;
    double  low;
    double  close;
    double  volume;
    double  amount;
    char    timeframe[8];
    char    symbol[24];
} CzscBar;

// CZSC 信号结果（从DLL返回）
typedef struct {
    // 趋势信号
    int     trend_5m;          // 0=震荡, 1=上涨, 2=下跌
    int     trend_60m;         // 0=震荡, 1=上涨, 2=下跌
    char    trend_5m_str[32];  // "上涨趋势"/"下跌趋势"/"震荡趋势"
    char    trend_60m_str[32];

    // SAR 信号
    int     sar_direction;     // 0=空头, 1=多头

    // RSI 信号
    char    rsi_signal[32];    // "超买向上"/"超卖向下"/"其他"

    // 量价信号
    char    s3k_signal[64];    // 三K形态
    char    vol_signal[32];    // 梯量/缩量

    // 支撑压力位
    double  hhyl;              // 最高压力位
    double  ddzc;              // 最低支撑位
    double  yl_5m;             // 5分钟压力位
    double  zc_5m;             // 5分钟支撑位

    // MACD 信号
    char    macd_cross[16];    // "金叉"/"死叉"/""
    int     macd_cross_dist;   // 距今几根K线

    // 单K线形态
    char    bar_pattern[32];   // "长下影"/"长上影"等
} CzscSignals;

// ============================================================================
// DLL 函数接口
// ============================================================================

// 初始化 CZSC 引擎（内部嵌入Python解释器，加载CZSC库）
// base_freq: "5m" 或 "5分钟"
// 返回: 0=成功, -1=失败
CZSC_API int czsc_init(const char* python_home, const char* script_dir,
                       const char* base_freq);

// 销毁 CZSC 引擎
CZSC_API void czsc_destroy();

// 初始化交易对的 CZSC Trader
// 用前61根K线初始化 BarGenerator 和 CzscTrader
CZSC_API int czsc_init_trader(const char* symbol, const CzscBar* bars, int count);

// 更新单根K线（每根新K线调用一次）
CZSC_API int czsc_update_bar(const char* symbol, const CzscBar* bar);

// 获取当前所有CZSC信号
CZSC_API int czsc_get_signals(const char* symbol, CzscSignals* out_signals);

// 获取趋势（快速接口，只返回趋势）
CZSC_API int czsc_get_trend(const char* symbol, int* trend_5m, int* trend_60m);

// 获取支撑压力位
CZSC_API int czsc_get_support_resistance(const char* symbol,
                                          double* hhyl, double* ddzc,
                                          double* yl_5m, double* zc_5m);

#ifdef __cplusplus
}
#endif
