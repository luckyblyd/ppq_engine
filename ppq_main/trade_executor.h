#pragma once
// trade_executor.h — 交易执行器
// 对应 Python 的 core/trade_executor.py + trade_executor_tool.py
// 处理开仓、平仓、止盈止损、超时撤单等逻辑

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include "order.h"
#include "account.h"
#include "exchange_interface.h"
#include "logger.h"

// ============================================================================
// 工具函数
// ============================================================================

inline double CalculateProfit(double entry, double exit, double qty, Operate type) {
    if (type == Operate::LO) return (exit - entry) * qty;
    if (type == Operate::SO) return (entry - exit) * qty;
    return 0;
}

inline int ParseTimeframeMinutes(const std::string& tf) {
    if (tf.back() == 'm') return std::stoi(tf.substr(0, tf.size() - 1));
    if (tf.back() == 'h') return std::stoi(tf.substr(0, tf.size() - 1)) * 60;
    return 5;
}

// ============================================================================
// TradeExecutor — 交易执行器
// ============================================================================

class TradeExecutor {
public:
    TradeExecutor(IExchangeInterface* exchange, const std::string& timeframe,
                  double initial_capital)
        : exchange_(exchange)
        , timeframe_(timeframe)
        , initial_capital_(initial_capital)
        , timeout_klines_(30) {}

    // ---------- 执行交易信号 ----------
    bool ExecuteTrade(const std::string& pair, const Bar& bar,
                      const SignalData& signal, Account& account,
                      const TradingParameters& params) {
        Operate op = signal.order_op;
        if (op == Operate::INITIAL) return false;

        double price     = signal.price;
        double quantity  = signal.qty;
        int    order_type = signal.order_type;

        // 资金管控
        if (IsOpenOrder(op)) {
            double occupied  = account.OccupiedCapital();
            double projected = occupied + quantity * price;
            if (projected > initial_capital_) {
                LOG_E("[Trade] Capital exceeded: %.2f + %.2f > %.2f",
                      occupied, quantity * price, initial_capital_);
                return false;
            }
        }

        // 创建订单
        Order order;
        order.id             = account.GetNextOrderId();
        order.timestamp      = bar.timestamp;
        order.currency_pair  = pair;
        order.price          = price;
        order.quantity        = quantity;
        order.amount         = price * quantity;
        order.type           = op;
        order.order_type     = order_type;
        order.remark         = signal.remark;

        if (IsOpenOrder(op)) {
            return HandleOpen(pair, bar, order, signal, account, params);
        } else {
            return HandleClose(pair, bar, order, signal, account);
        }
    }

    // ---------- 检查挂单成交（每根K线调用） ----------
    void CheckTriggeredOrders(const std::string& pair, const Bar& bar,
                               Account& account) {
        auto* sim = dynamic_cast<SimulationExchange*>(exchange_);
        auto& orders = account.Orders();

        // 复制ID列表避免迭代时修改
        std::vector<int> ids;
        for (auto& o : orders) ids.push_back(o.id);

        for (int oid : ids) {
            Order* order = account.FindOrder(oid);
            if (!order) continue;
            if (order->matchstatus != MATCH_PENDING) continue;

            bool filled = false;

            if (sim) {
                filled = sim->CheckFillByBar(*order, bar);
            } else {
                // 实盘：查询订单状态
                exchange_->QueryOrder(*order);
                filled = (order->matchstatus == MATCH_FILLED);
            }

            if (!filled) {
                // 超时检查（仅开仓单）
                if (IsOpenOrder(order->type)) {
                    int tf_ms = ParseTimeframeMinutes(timeframe_) * 60000;
                    int klines_passed = (int)((bar.timestamp - order->timestamp) / tf_ms);
                    if (klines_passed >= timeout_klines_) {
                        order->matchstatus = MATCH_CANCELED;
                        order->status      = "CANCELED";
                        if (!exchange_->IsSimulation())
                            exchange_->CancelOrder(pair, order->outorderid);
                        LOG_W("[Trade] Order %d timeout after %d klines", order->id, klines_passed);
                    }
                }
                continue;
            }

            // 成交处理
            // 【修复1】止盈/止损成交时，先检查对应开仓单是否还存在
            if (IsCloseOrder(order->type) && order->old_orderid > 0) {
                Order* parent = account.FindOrder(order->old_orderid);
                if (!parent || parent->matchstatus == MATCH_CLOSED) {
                    // 开仓单已被另一个止盈/止损处理过，标记此单撤销
                    order->matchstatus = MATCH_CANCELED;
                    order->status = "CANCELED";
                    continue;
                }
            }

            ProcessMatch(*order, pair, order->price, account, bar);
        }
    }

private:
    IExchangeInterface* exchange_;
    std::string timeframe_;
    double initial_capital_;
    int timeout_klines_;

    // ---------- 开仓处理 ----------
    bool HandleOpen(const std::string& pair, const Bar& bar,
                    Order& order, const SignalData& signal,
                    Account& account, const TradingParameters& params) {
        // 下单
        exchange_->PlaceOrder(order);

        // 设置止盈止损
        if (signal.tp_price > 0) order.take_profit_price = signal.tp_price;
        if (signal.sl_price > 0) order.stop_loss_price   = signal.sl_price;

        account.AddOrder(order);

        // 如果市价单已成交
        if (order.matchstatus == MATCH_FILLED) {
            LOG_I("[Trade] %s OPEN filled: price=%.4f qty=%.6f id=%d",
                  OperateToStr(order.type), order.price, order.quantity, order.id);
        }

        // 创建止盈止损挂单
        if (order.matchstatus == MATCH_FILLED) {
            CreateTPSLOrders(pair, bar, order, account, signal.tp_price, signal.sl_price);
        }

        LOG_I("[Trade] %s order=%d price=%.4f qty=%.6f %s",
              OperateToStr(order.type), order.id, order.price,
              order.quantity, order.remark.c_str());
        return true;
    }

    // ---------- 平仓处理 ----------
    bool HandleClose(const std::string& pair, const Bar& bar,
                     Order& order, const SignalData& signal,
                     Account& account) {
        // 查找对应的开仓订单
        Order* old_order = account.FindOrder(signal.id);
        if (!old_order) {
            LOG_E("[Trade] Close failed: open order %d not found", signal.id);
            return false;
        }

        order.old_orderid = old_order->id;
        order.quantity    = old_order->quantity;
        order.amount      = order.price * order.quantity;

        // 止盈止损修改（order_type == 0 且有tp/sl价格）
        if (signal.order_type == 0 && (signal.tp_price > 0 || signal.sl_price > 0)) {
            // 修改止盈止损（非平仓）
            if (signal.tp_price > 0) old_order->take_profit_price = signal.tp_price;
            if (signal.sl_price > 0) old_order->stop_loss_price   = signal.sl_price;
            // 撤销旧的止盈止损挂单并重建
            CancelTPSLOrders(old_order->id, account);
            CreateTPSLOrders(pair, bar, *old_order, account,
                             old_order->take_profit_price, old_order->stop_loss_price);
            return true;
        }

        // 正常平仓：撤销止盈止损
        CancelTPSLOrders(old_order->id, account);

        // 下平仓单
        exchange_->PlaceOrder(order);
        account.AddOrder(order);

        if (order.matchstatus == MATCH_FILLED) {
            ProcessMatch(order, pair, order.price, account, bar);
        }

        return true;
    }

    // ---------- 成交处理（开仓+平仓统一） ----------
    void ProcessMatch(Order& order, const std::string& pair,
                      double fill_price, Account& account, const Bar& bar) {
        order.matchstatus    = MATCH_FILLED;
        order.matchtimestamp  = bar.timestamp;

        if (IsOpenOrder(order.type)) {
            LOG_I("[Trade] %s filled: id=%d price=%.4f qty=%.6f",
                  OperateToStr(order.type), order.id, fill_price, order.quantity);
            return;
        }

        // 平仓成交
        Order* old_order = account.FindOrder(order.old_orderid);
        if (!old_order) {
            LOG_E("[Trade] ProcessMatch: open order %d not found", order.old_orderid);
            return;
        }

        double profit = CalculateProfit(old_order->price, fill_price,
                                        old_order->quantity, old_order->type);
        order.profit = profit;
        account.AddCapital(profit);
        account.RecordTrade(profit);

        // 【修复2】先撤销残余止盈止损（在归档之前），并从orders_移除
        CancelAndRemoveTPSLOrders(old_order->id, order.id, account);

        // 归档
        old_order->matchstatus = MATCH_CLOSED;
        account.RemoveOrder(order.id);
        account.RemoveOrder(old_order->id);

        LOG_I("[Trade] CLOSE %s: profit=%.4f entry=%.4f exit=%.4f qty=%.6f",
              profit > 0 ? "WIN" : "LOSS", profit,
              old_order->price, fill_price, old_order->quantity);
    }

    // ---------- 止盈止损管理 ----------
    void CreateTPSLOrders(const std::string& pair, const Bar& bar,
                          const Order& parent, Account& account,
                          double tp_price, double sl_price) {
        Operate close_op = parent.type == Operate::LO ? Operate::LE : Operate::SE;

        if (tp_price > 0) {
            Order tp;
            tp.id              = account.GetNextOrderId();
            tp.timestamp       = bar.timestamp;
            tp.currency_pair   = pair;
            tp.price           = tp_price;
            tp.quantity        = parent.quantity;
            tp.amount          = tp_price * parent.quantity;
            tp.type            = close_op;
            tp.order_type      = 0;
            tp.take_profit_price = tp_price;
            tp.old_orderid     = parent.id;
            tp.remark          = "止盈委托";
            tp.matchstatus     = MATCH_PENDING;
            exchange_->PlaceOrder(tp);
            account.AddOrder(tp);
        }

        if (sl_price > 0) {
            Order sl;
            sl.id              = account.GetNextOrderId();
            sl.timestamp       = bar.timestamp;
            sl.currency_pair   = pair;
            sl.price           = sl_price;
            sl.quantity        = parent.quantity;
            sl.amount          = sl_price * parent.quantity;
            sl.type            = close_op;
            sl.order_type      = 0;
            sl.stop_loss_price = sl_price;
            sl.old_orderid     = parent.id;
            sl.remark          = "止损委托";
            sl.matchstatus     = MATCH_PENDING;
            exchange_->PlaceOrder(sl);
            account.AddOrder(sl);
        }
    }

    // 【修复3】取消并移除止盈止损单（而非仅标记）
    void CancelAndRemoveTPSLOrders(int parent_id, int exclude_id, Account& account) {
        auto& orders = account.Orders();
        std::vector<int> to_remove;
        for (auto& o : orders) {
            if (o.old_orderid == parent_id && o.id != exclude_id
                && o.matchstatus == MATCH_PENDING
                && (o.remark == "止盈委托" || o.remark == "止损委托")) {
                o.matchstatus = MATCH_CANCELED;
                o.status      = "CANCELED";
                if (!exchange_->IsSimulation())
                    exchange_->CancelOrder(o.currency_pair, o.outorderid);
                to_remove.push_back(o.id);
            }
        }
        // 先归档再移除
        for (int id : to_remove) {
            account.RemoveOrder(id);
        }
    }

    // 仅标记取消（用于修改止盈止损场景，需要重建）
    void CancelTPSLOrders(int parent_id, Account& account) {
        CancelAndRemoveTPSLOrders(parent_id, -1, account);
    }
};
