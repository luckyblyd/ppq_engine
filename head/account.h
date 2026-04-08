
#pragma once
// account.h — 账户管理
// 对应 Python 的 account.py

#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include "order.h"
#include "logger.h"

// ============================================================================
// 性能指标
// ============================================================================

struct Performance {
    int    total_trades    = 0;
    int    winning_trades  = 0;
    int    losing_trades   = 0;
    double win_rate        = 0.0;
    double profit_factor   = 0.0;
    double total_profit    = 0.0;
    double total_loss      = 0.0;

    void UpdateWinRate() {
        win_rate = total_trades > 0
            ? (double)winning_trades / total_trades : 0.0;
    }
    void UpdateProfitFactor() {
        profit_factor = total_loss > 0
            ? total_profit / total_loss : (total_profit > 0 ? 1e9 : 0.0);
    }
    void Reset() {
        total_trades = winning_trades = losing_trades = 0;
        win_rate = profit_factor = total_profit = total_loss = 0;
    }
};

// ============================================================================
// 交易参数（对应 Python TradingParameters）
// ============================================================================

struct TradingParameters {
    std::string currency_pair;
    double coefficient          = 1.0;
    double take_profit_ratio    = 1.09;
    double stop_loss_ratio      = 1.05;
    double czzy_signal_count    = 100.0;   // 每次开仓金额
    int    up_signal_count      = 0;
    int    down_signal_count    = 0;
    int    oscillation_signal_count = 0;
};

// ============================================================================
// Account — 账户管理器
// ============================================================================

class Account {
public:
    Account() = default;

    // ---------- 初始化 ----------
    void InitForBacktest(double capital, const std::string& pair) {
        initial_capital_ = capital;
        current_capital_ = capital;
        currency_pair_   = pair;
        orders_.clear();
        archive_.clear();
        perf_.Reset();
        next_id_ = 0;
    }

    void InitForTrade(double capital, const std::string& pair) {
        initial_capital_ = capital;
        current_capital_ = capital;
        currency_pair_   = pair;
    }

    // ---------- 属性 ----------
    double InitialCapital() const { return initial_capital_; }
    double CurrentCapital() const { return current_capital_; }
    void   SetCurrentCapital(double v) { current_capital_ = v; }
    void   AddCapital(double v) { current_capital_ += v; }
    const std::string& CurrencyPair() const { return currency_pair_; }

    const Performance& Perf() const { return perf_; }
    Performance& MutPerf() { return perf_; }

    // ---------- 订单管理 ----------
    std::vector<Order>& Orders() { return orders_; }
    const std::vector<Order>& Orders() const { return orders_; }

    std::vector<Order>& Archive() { return archive_; }
    const std::vector<Order>& Archive() const { return archive_; }

    void AddOrder(const Order& o) { orders_.push_back(o); }

    // 移除并归档
    bool RemoveOrder(int order_id) {
        for (auto it = orders_.begin(); it != orders_.end(); ++it) {
            if (it->id == order_id) {
                archive_.push_back(*it);
                orders_.erase(it);
                return true;
            }
        }
        return false;
    }

    void ArchiveAllOrders() {
        for (auto& o : orders_) archive_.push_back(o);
    }

    int GetNextOrderId() { return ++next_id_; }

    // ---------- 查询辅助 ----------
    // 获取已成交的多头开仓订单
    std::vector<Order*> GetFilledLongs() {
        std::vector<Order*> res;
        for (auto& o : orders_) {
            if (o.type == Operate::LO && o.matchstatus == MATCH_FILLED)
                res.push_back(&o);
        }
        return res;
    }

    // 获取已成交的空头开仓订单
    std::vector<Order*> GetFilledShorts() {
        std::vector<Order*> res;
        for (auto& o : orders_) {
            if (o.type == Operate::SO && o.matchstatus == MATCH_FILLED)
                res.push_back(&o);
        }
        return res;
    }

    // 根据ID查找订单
    Order* FindOrder(int id) {
        for (auto& o : orders_) {
            if (o.id == id) return &o;
        }
        return nullptr;
    }

    // 计算已占用资金
    double OccupiedCapital() const {
        double total = 0;
        for (auto& o : orders_) {
            if (IsOpenOrder(o.type) && o.matchstatus != MATCH_CLOSED)
                total += o.quantity * o.price;
        }
        return total;
    }

    // 计算浮动盈亏
    double FloatingPnL(double current_price) const {
        double pnl = 0;
        for (auto& o : orders_) {
            if (o.matchstatus != MATCH_FILLED) continue;
            if (o.type == Operate::LO)
                pnl += (current_price - o.price) * o.quantity;
            else if (o.type == Operate::SO)
                pnl += (o.price - current_price) * o.quantity;
        }
        return pnl;
    }

    // ---------- 性能更新 ----------
    void RecordTrade(double profit) {
        perf_.total_trades++;
        if (profit > 0) {
            perf_.winning_trades++;
            perf_.total_profit += profit;
        } else {
            perf_.losing_trades++;
            perf_.total_loss += std::abs(profit);
        }
        perf_.UpdateWinRate();
        perf_.UpdateProfitFactor();
    }

private:
    double initial_capital_ = 0;
    double current_capital_ = 0;
    std::string currency_pair_;
    std::vector<Order> orders_;
    std::vector<Order> archive_;
    Performance perf_;
    int next_id_ = 0;
};
