#pragma once
// order.h — 订单数据结构
// 对应 Python 的 data_module/models.py 中的 Orders 类

#include <string>
#include <cstdint>
#include "enums.h"

// ============================================================================
// 订单结构
// ============================================================================

struct Order {
    int         id              = 0;
    int64_t     timestamp       = 0;      // 下单时间戳(ms)
    std::string currency_pair;
    double      price           = 0.0;
    double      quantity        = 0.0;
    double      amount          = 0.0;    // price * quantity
    Operate     type            = Operate::INITIAL;
    int         order_type      = 0;      // 0=限价, 1=市价
    double      take_profit_price = 0.0;
    double      stop_loss_price   = 0.0;
    std::string orderid;                  // 内部委托号
    std::string outorderid;               // 外部（交易所）委托号
    double      close_position_signal = 0.0;
    std::string status          = "OPEN"; // OPEN / CLOSED / CANCELED
    int         matchstatus     = MATCH_PENDING;
    int64_t     matchtimestamp   = 0;      // 成交时间戳(ms)
    double      profit          = 0.0;
    double      max_close_signal = 0.0;
    std::string remark;
    int         old_orderid     = 0;      // 关联的开仓订单ID（平仓/止盈止损用）
    int64_t     synctimestamp   = 0;      // 最新同步时间

    // 辅助方法
    bool IsFilled() const  { return matchstatus == MATCH_FILLED; }
    bool IsPending() const { return matchstatus == MATCH_PENDING; }
    bool IsOpen() const    { return IsOpenOrder(type); }
    bool IsClose() const   { return IsCloseOrder(type); }
    bool IsLong() const    { return type == Operate::LO; }
    bool IsShort() const   { return type == Operate::SO; }

    const char* TypeStr() const { return OperateToStr(type); }
};

// ============================================================================
// K线数据（扩展已有的 KlineBar，增加辅助方法）
// ============================================================================

struct Bar {
    int64_t     timestamp       = 0;
    char        timeframe[8]    = {};
    char        currency_pair[24] = {};
    double      open            = 0;
    double      high            = 0;
    double      low             = 0;
    double      close           = 0;
    double      volume          = 0;
    double      quote_volume    = 0;
    double      change_pct      = 0;
    uint32_t    flags           = 0;

    bool IsBullish() const { return close > open; }
    bool IsBearish() const { return close < open; }
    double BodySize() const { return std::abs(close - open); }
    double UpperShadow() const { return high - std::max(open, close); }
    double LowerShadow() const { return std::min(open, close) - low; }
    double PriceChangePct() const {
        return open > 0 ? (close - open) / open * 100.0 : 0.0;
    }
};
