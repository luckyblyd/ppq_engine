#pragma once
// database_interface.h — 数据库抽象接口
// 统一 SQLite 和 PostgreSQL 的访问方式

#include <string>
#include <vector>
#include <functional>
#include "account.h"
#include "market_data_types.h"

// ============================================================================
// IDatabase — 数据库抽象接口
// ============================================================================

class IDatabase {
public:
    virtual ~IDatabase() = default;

    // 连接/断开
    virtual bool Open(const std::string& conn_str) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // ---------- K线数据操作 ----------
    virtual bool InsertBar(const Bar& bar) = 0;
    virtual int  InsertBars(const std::vector<Bar>& bars) = 0;

    virtual std::vector<Bar> QueryBars(
        const std::string& pair, const std::string& tf,
        int64_t start_ms, int64_t end_ms) = 0;

    virtual std::vector<Bar> QueryLatestBars(
        const std::string& pair, const std::string& tf, int limit) = 0;

    virtual int CountBars(
        const std::string& pair, const std::string& tf) = 0;

    // ---------- 订单操作 ----------
    virtual bool InsertOrder(const Order& order) = 0;

    virtual bool UpdateOrderStatus(int order_id, const std::string& status,
                                   int matchstatus, double profit = 0,
                                   double price = 0) = 0;

    virtual bool UpdateOrderTPSL(int order_id, double tp_price, double sl_price) = 0;

    virtual std::vector<Order> QueryOpenOrders(const std::string& pair) = 0;

    virtual std::vector<Order> QueryActiveOrders(const std::string& pair) = 0;

    // ---------- 交易参数 ----------
    virtual TradingParameters QueryParameters(const std::string& pair) = 0;

    // ---------- 执行记录 ----------
    virtual int64_t QueryLastExecution(const std::string& pair) = 0;
    virtual bool InsertExecution(const std::string& pair, int64_t ts) = 0;
    virtual bool UpdateExecution(const std::string& pair, int64_t ts) = 0;

    // ---------- 序号发生器 ----------
    virtual int GetNextOrderId() = 0;

    // ---------- 事务控制 ----------
    virtual void BeginTransaction() = 0;
    virtual void CommitTransaction() = 0;
    virtual void RollbackTransaction() = 0;

    // ---------- KlineBar 操作 (供 market_server 使用) ----------
    // 默认实现通过 Bar 接口转换，子类可覆盖以优化

    virtual bool InsertKlineBar(const KlineBar& kb) {
        return InsertBar(KlineBarToBar(kb));
    }

    virtual int InsertKlineBars(const std::vector<KlineBar>& kbars) {
        std::vector<Bar> bars;
        bars.reserve(kbars.size());
        for (auto& kb : kbars) bars.push_back(KlineBarToBar(kb));
        return InsertBars(bars);
    }

    virtual std::vector<KlineBar> QueryKlineBars(
        const std::string& pair, const std::string& tf,
        int64_t start_ms, int64_t end_ms) {
        return BarsToKlineBars(QueryBars(pair, tf, start_ms, end_ms));
    }

    virtual std::vector<KlineBar> QueryLatestKlineBars(
        const std::string& pair, const std::string& tf, int limit) {
        return BarsToKlineBars(QueryLatestBars(pair, tf, limit));
    }

protected:
    static Bar KlineBarToBar(const KlineBar& kb) {
        Bar b{};
        b.timestamp = kb.timestamp;
        strncpy(b.timeframe, kb.timeframe, 7);
        strncpy(b.currency_pair, kb.currency_pair, 23);
        b.open = kb.open; b.high = kb.high;
        b.low = kb.low; b.close = kb.close;
        b.volume = kb.volume; b.quote_volume = kb.quote_volume;
        b.change_pct = kb.change_pct;
        return b;
    }

    static std::vector<KlineBar> BarsToKlineBars(const std::vector<Bar>& bars) {
        std::vector<KlineBar> result;
        result.reserve(bars.size());
        for (auto& b : bars) {
            KlineBar kb{};
            kb.timestamp = b.timestamp;
            strncpy(kb.timeframe, b.timeframe, 7);
            strncpy(kb.currency_pair, b.currency_pair, 23);
            kb.open = b.open; kb.high = b.high;
            kb.low = b.low; kb.close = b.close;
            kb.volume = b.volume; kb.quote_volume = b.quote_volume;
            kb.change_pct = b.change_pct;
            result.push_back(kb);
        }
        return result;
    }
};
