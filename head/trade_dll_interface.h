#pragma once
// trade_dll_interface.h — Binance 交易 DLL 的 C 接口定义
// 主程序通过此头文件动态加载 trade_dll.dll

#include <cstdint>

#ifdef TRADE_DLL_EXPORTS
  #define TRADE_API __declspec(dllexport)
#else
  #define TRADE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 交易订单结构（跨 DLL 边界）
// ============================================================================

typedef struct {
    char    symbol[24];       // "ETHUSDT"
    char    side[8];          // "BUY" / "SELL"
    char    position_side[8]; // "LONG" / "SHORT"
    char    order_type[16];   // "MARKET" / "LIMIT"
    double  price;
    double  quantity;
    int     recv_window;
} TradeOrderRequest;

typedef struct {
    int     success;            // 0=失败, 1=成功
    char    order_id[64];       // 外部委托号
    char    status[16];         // "FILLED" / "NEW" / "CANCELED"
    double  fill_price;         // 实际成交价
    double  fill_qty;           // 实际成交量
    char    error_msg[256];     // 错误信息
} TradeOrderResponse;

typedef struct {
    char    symbol[24];
    char    side[8];
    char    position_side[8];
    char    algo_type[16];      // "TAKE_PROFIT" / "STOP"
    double  trigger_price;
    double  price;
    double  quantity;
} TradeAlgoRequest;

// ============================================================================
// DLL 函数接口
// ============================================================================

// 初始化交易引擎（加载API密钥等）
TRADE_API int trade_init(const char* config_path);

// 销毁
TRADE_API void trade_destroy();

// 下单
TRADE_API int trade_place_order(const TradeOrderRequest* req,
                                 TradeOrderResponse* resp);

// 撤单
TRADE_API int trade_cancel_order(const char* symbol, const char* order_id,
                                  TradeOrderResponse* resp);

// 查询订单状态
TRADE_API int trade_query_order(const char* symbol, const char* order_id,
                                 TradeOrderResponse* resp);

// 下止盈止损算法单
TRADE_API int trade_place_algo_order(const TradeAlgoRequest* req,
                                      TradeOrderResponse* resp);

// 撤销算法单
TRADE_API int trade_cancel_algo_order(const char* algo_order_id,
                                       TradeOrderResponse* resp);

// 获取交易模式 ("future" / "margin" / "spot")
TRADE_API const char* trade_get_mode();

#ifdef __cplusplus
}
#endif
