#pragma once
// exchange_interface.h — 交易所接口抽象
// 核心思想：回测和实盘统一调用接口，simulation 根据K线判断成交，binance 调用API
// 配置: config.ini 中 interface = simulation / binance

#include <string>
#include <vector>
#include "order.h"
#include "logger.h"

// ============================================================================
// IExchangeInterface — 交易所接口抽象
// ============================================================================

class IExchangeInterface {
public:
    virtual ~IExchangeInterface() = default;

    // 下单，返回是否成功。成功后 order 的 outorderid/matchstatus 等字段会被更新
    virtual bool PlaceOrder(Order& order) = 0;

    // 撤单
    virtual bool CancelOrder(const std::string& pair, const std::string& outorderid) = 0;

    // 查询订单状态，更新 order 的 matchstatus/price
    virtual bool QueryOrder(Order& order) = 0;

    // 是否是模拟模式
    virtual bool IsSimulation() const = 0;

    // 接口名称
    virtual const char* Name() const = 0;
};

// ============================================================================
// SimulationExchange — 回测/模拟交易所
// 根据当前K线的高低价判断是否成交
// ============================================================================

class SimulationExchange : public IExchangeInterface {
public:
    // 模拟下单：市价单立即成交，限价单标记为待成交
    bool PlaceOrder(Order& order) override {
        if (order.order_type == 1) {
            // 市价单：立即成交
            order.matchstatus    = MATCH_FILLED;
            order.matchtimestamp = order.timestamp;
            order.outorderid     = "SIM_" + std::to_string(order.id);
            order.status         = "CLOSED";
        } else {
            // 限价单：等待下根K线判断
            order.matchstatus = MATCH_PENDING;
            order.outorderid  = "SIM_" + std::to_string(order.id);
        }
        return true;
    }

    bool CancelOrder(const std::string&, const std::string&) override {
        return true; // 模拟环境直接成功
    }

    bool QueryOrder(Order&) override {
        return true; // 模拟环境不需要查询
    }

    // 回测核心：根据K线判断挂单是否成交
    // 【关键修复】止盈止损单只通过 stop_loss_price/take_profit_price 触发，
    // 不走普通限价匹配逻辑。对齐Python check_order_filled()
    bool CheckFillByBar(Order& order, const Bar& bar) {
        if (order.matchstatus != MATCH_PENDING) return false;

        bool filled = false;
        bool is_tp_sl_order = (order.remark == "止盈委托" || order.remark == "止损委托");

        // ====== 止盈止损订单触发检查 ======
        // 对应Python: check_order_filled() 中 stop_loss_price / take_profit_price 逻辑
        // 止损单
        if (order.stop_loss_price > 0 && IsCloseOrder(order.type)) {
            if (bar.low <= order.stop_loss_price && order.stop_loss_price <= bar.high) {
                order.price = order.stop_loss_price;
                filled = true;
            }
        }
        // 止盈单
        if (!filled && order.take_profit_price > 0 && IsCloseOrder(order.type)) {
            if (bar.low <= order.take_profit_price && order.take_profit_price <= bar.high) {
                order.price = order.take_profit_price;
                filled = true;
            }
        }

        // ====== 普通限价单匹配 ======
        // 【修复】止盈止损单不用此逻辑，否则会被错误触发
        if (!filled && !is_tp_sl_order) {
            switch (order.type) {
                case Operate::LO: filled = (bar.low <= order.price); break;
                case Operate::SO: filled = (bar.high >= order.price); break;
                case Operate::LE: filled = (bar.high >= order.price); break;
                case Operate::SE: filled = (bar.low <= order.price); break;
                default: break;
            }
        }

        if (filled) {
            order.matchstatus    = MATCH_FILLED;
            order.matchtimestamp  = bar.timestamp;
            order.status         = "CLOSED";
        }
        return filled;
    }

    bool IsSimulation() const override { return true; }
    const char* Name() const override { return "simulation"; }
};

// ============================================================================
// BinanceExchange — 币安交易所接口（框架桩，后续接 DLL）
// ============================================================================

class BinanceExchange : public IExchangeInterface {
public:
    bool PlaceOrder(Order& order) override {
        // TODO: 调用币安API下单
        // 目前作为桩代码，后续做成DLL加载
        LOG_E("BinanceExchange::PlaceOrder not implemented yet");
        return false;
    }

    bool CancelOrder(const std::string& pair, const std::string& outorderid) override {
        LOG_E("BinanceExchange::CancelOrder not implemented yet");
        return false;
    }

    bool QueryOrder(Order& order) override {
        LOG_E("BinanceExchange::QueryOrder not implemented yet");
        return false;
    }

    bool IsSimulation() const override { return false; }
    const char* Name() const override { return "binance"; }
};

// ============================================================================
// 工厂函数
// ============================================================================

inline IExchangeInterface* CreateExchange(const std::string& name) {
    if (name == "simulation" || name == "sim" || name == "backtest")
        return new SimulationExchange();
    if (name == "binance")
        return new BinanceExchange();
    LOG_W("Unknown exchange '%s', using simulation", name.c_str());
    return new SimulationExchange();
}
