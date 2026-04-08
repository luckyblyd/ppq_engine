#pragma once
// database.h — SQLite3 数据库实现 (实现 IDatabase 接口)
// 兼容旧的 Database 类(KlineBar操作) + 新的 IDatabase 接口(Bar/Order操作)
//
// 编译: 需链接 -lsqlite3

#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <chrono>
#include "database_interface.h"
#include "market_data_types.h"
#include "logger.h"

// ============================================================================
// SQLiteDatabase — 实现 IDatabase 接口的 SQLite 封装
// ============================================================================

class SQLiteDatabase : public IDatabase {
public:
    ~SQLiteDatabase() override { Close(); }

    bool Open(const std::string& path) override {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            LOG_E("sqlite3_open: %s", sqlite3_errmsg(db_));
            return false;
        }
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        EnsureTables();
        LOG_I("[SQLiteDB] Opened: %s", path.c_str());
        return true;
    }

    void Close() override {
        if (ins_bar_) { sqlite3_finalize(ins_bar_); ins_bar_ = nullptr; }
        if (db_)      { sqlite3_close(db_);         db_ = nullptr;      }
    }

    bool IsOpen() const override { return db_ != nullptr; }

    // ---------- K线数据操作 ----------
    bool InsertBar(const Bar& bar) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (!EnsureInsertBar()) return false;
        sqlite3_reset(ins_bar_);
        sqlite3_bind_int64 (ins_bar_, 1,  bar.timestamp);
        sqlite3_bind_text  (ins_bar_, 2,  bar.timeframe, -1, SQLITE_STATIC);
        sqlite3_bind_text  (ins_bar_, 3,  bar.currency_pair, -1, SQLITE_STATIC);
        sqlite3_bind_text  (ins_bar_, 4,  "", -1, SQLITE_STATIC);
        sqlite3_bind_text  (ins_bar_, 5,  "", -1, SQLITE_STATIC);
        sqlite3_bind_double(ins_bar_, 6,  bar.open);
        sqlite3_bind_double(ins_bar_, 7,  bar.high);
        sqlite3_bind_double(ins_bar_, 8,  bar.low);
        sqlite3_bind_double(ins_bar_, 9,  bar.close);
        sqlite3_bind_double(ins_bar_, 10, bar.volume);
        sqlite3_bind_double(ins_bar_, 11, bar.quote_volume);
        sqlite3_bind_double(ins_bar_, 12, bar.change_pct);
        int rc = sqlite3_step(ins_bar_);
        if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
            LOG_E("insert bar: %s", sqlite3_errmsg(db_)); return false;
        }
        return true;
    }

    int InsertBars(const std::vector<Bar>& bars) override {
        std::lock_guard<std::mutex> lk(mu_);
        sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
        int ok = 0;
        for (auto& b : bars) {
            if (!EnsureInsertBar()) break;
            sqlite3_reset(ins_bar_);
            sqlite3_bind_int64 (ins_bar_, 1,  b.timestamp);
            sqlite3_bind_text  (ins_bar_, 2,  b.timeframe, -1, SQLITE_STATIC);
            sqlite3_bind_text  (ins_bar_, 3,  b.currency_pair, -1, SQLITE_STATIC);
            sqlite3_bind_text  (ins_bar_, 4,  "", -1, SQLITE_STATIC);
            sqlite3_bind_text  (ins_bar_, 5,  "", -1, SQLITE_STATIC);
            sqlite3_bind_double(ins_bar_, 6,  b.open);
            sqlite3_bind_double(ins_bar_, 7,  b.high);
            sqlite3_bind_double(ins_bar_, 8,  b.low);
            sqlite3_bind_double(ins_bar_, 9,  b.close);
            sqlite3_bind_double(ins_bar_, 10, b.volume);
            sqlite3_bind_double(ins_bar_, 11, b.quote_volume);
            sqlite3_bind_double(ins_bar_, 12, b.change_pct);
            if (sqlite3_step(ins_bar_) == SQLITE_DONE) ok++;
        }
        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
        return ok;
    }

    std::vector<Bar> QueryBars(const std::string& pair, const std::string& tf,
                               int64_t start_ms, int64_t end_ms) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<Bar> res;
        const char* sql =
            "SELECT timestamp,timeframe,currency_pair,"
            "openprice,highprice,lowprice,closeprice,"
            "matchqty,matchamt,change_per FROM stkprice "
            "WHERE currency_pair=? AND timeframe=? "
            "AND timestamp>=? AND timestamp<=? ORDER BY timestamp";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return res;
        sqlite3_bind_text (st, 1, pair.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (st, 2, tf.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, start_ms);
        sqlite3_bind_int64(st, 4, end_ms);
        while (sqlite3_step(st) == SQLITE_ROW) {
            res.push_back(RowToBar(st));
        }
        sqlite3_finalize(st);
        return res;
    }

    std::vector<Bar> QueryLatestBars(const std::string& pair,
                                      const std::string& tf, int limit) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<Bar> res;
        const char* sql =
            "SELECT timestamp,timeframe,currency_pair,"
            "openprice,highprice,lowprice,closeprice,"
            "matchqty,matchamt,change_per FROM stkprice "
            "WHERE currency_pair=? AND timeframe=? "
            "ORDER BY timestamp DESC LIMIT ?";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return res;
        sqlite3_bind_text(st, 1, pair.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, tf.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, limit);
        while (sqlite3_step(st) == SQLITE_ROW) {
            res.push_back(RowToBar(st));
        }
        sqlite3_finalize(st);
        std::reverse(res.begin(), res.end());
        return res;
    }

    int CountBars(const std::string& pair, const std::string& tf) override {
        std::lock_guard<std::mutex> lk(mu_);
        const char* sql = "SELECT COUNT(*) FROM stkprice WHERE currency_pair=? AND timeframe=?";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_text(st, 1, pair.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, tf.c_str(), -1, SQLITE_TRANSIENT);
        int count = 0;
        if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return count;
    }

    // ---------- 订单操作 ----------
    bool InsertOrder(const Order& order) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[2048];
        snprintf(sql, sizeof(sql),
            "INSERT OR REPLACE INTO orders (id,timestamp,currency_pair,price,quantity,amount,"
            "type,order_type,take_profit_price,stop_loss_price,"
            "status,matchstatus,matchtimestamp,profit,remark,old_orderid) "
            "VALUES (%d,%lld,'%s',%.8f,%.8f,%.8f,'%s',%d,%.8f,%.8f,"
            "'%s',%d,%lld,%.8f,'%s',%d)",
            order.id, (long long)order.timestamp, order.currency_pair.c_str(),
            order.price, order.quantity, order.amount,
            OperateToStr(order.type), order.order_type,
            order.take_profit_price, order.stop_loss_price,
            order.status.c_str(), order.matchstatus,
            (long long)order.matchtimestamp, order.profit,
            order.remark.c_str(), order.old_orderid);
        return ExecSQL(sql);
    }

    bool UpdateOrderStatus(int order_id, const std::string& status,
                           int matchstatus, double profit, double price) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[512];
        snprintf(sql, sizeof(sql),
            "UPDATE orders SET status='%s',matchstatus=%d,profit=%.8f,price=%.8f "
            "WHERE id=%d",
            status.c_str(), matchstatus, profit, price, order_id);
        return ExecSQL(sql);
    }

    bool UpdateOrderTPSL(int order_id, double tp, double sl) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[256];
        snprintf(sql, sizeof(sql),
            "UPDATE orders SET take_profit_price=%.8f,stop_loss_price=%.8f WHERE id=%d",
            tp, sl, order_id);
        return ExecSQL(sql);
    }

    std::vector<Order> QueryOpenOrders(const std::string& pair) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[512];
        snprintf(sql, sizeof(sql),
            "SELECT id,timestamp,currency_pair,price,quantity,amount,type,"
            "order_type,take_profit_price,stop_loss_price,status,matchstatus,"
            "matchtimestamp,profit,remark,old_orderid "
            "FROM orders WHERE currency_pair='%s' AND status='OPEN' "
            "ORDER BY timestamp DESC", pair.c_str());
        return QueryOrdersSQL(sql);
    }

    std::vector<Order> QueryActiveOrders(const std::string& pair) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[1024];
        snprintf(sql, sizeof(sql),
            "SELECT id,timestamp,currency_pair,price,quantity,amount,type,"
            "order_type,take_profit_price,stop_loss_price,status,matchstatus,"
            "matchtimestamp,profit,remark,old_orderid "
            "FROM orders WHERE currency_pair='%s' AND matchstatus!=3 "
            "AND timestamp >= %lld "
            "AND ((type IN ('开多','开空') AND matchstatus IN (0,1)) "
            "  OR (type IN ('平多','平空') AND matchstatus=0)) "
            "ORDER BY timestamp DESC",
            pair.c_str(), WallNowMs() - 15LL * 86400000LL);
        return QueryOrdersSQL(sql);
    }

    // ---------- 交易参数 ----------
    TradingParameters QueryParameters(const std::string& pair) override {
        std::lock_guard<std::mutex> lk(mu_);
        TradingParameters p;
        p.currency_pair = pair;
        char sql[256];
        snprintf(sql, sizeof(sql),
            "SELECT coefficient,take_profit_ratio,stop_loss_ratio,czzy_signal_count "
            "FROM parameter_table WHERE currency_pair='%s'", pair.c_str());
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                p.coefficient       = sqlite3_column_double(st, 0);
                p.take_profit_ratio = sqlite3_column_double(st, 1);
                p.stop_loss_ratio   = sqlite3_column_double(st, 2);
                p.czzy_signal_count = sqlite3_column_double(st, 3);
            }
            sqlite3_finalize(st);
        }
        return p;
    }

    // ---------- 执行记录 ----------
    int64_t QueryLastExecution(const std::string& pair) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[256];
        snprintf(sql, sizeof(sql),
            "SELECT timestamp_records FROM executions WHERE currency_pair='%s'",
            pair.c_str());
        sqlite3_stmt* st;
        int64_t ts = -1;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW)
                ts = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
        }
        return ts;
    }

    bool InsertExecution(const std::string& pair, int64_t ts) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[256];
        snprintf(sql, sizeof(sql),
            "INSERT INTO executions (currency_pair,timestamp_records) VALUES ('%s',%lld)",
            pair.c_str(), (long long)ts);
        return ExecSQL(sql);
    }

    bool UpdateExecution(const std::string& pair, int64_t ts) override {
        std::lock_guard<std::mutex> lk(mu_);
        char sql[256];
        snprintf(sql, sizeof(sql),
            "UPDATE executions SET timestamp_records=%lld WHERE currency_pair='%s'",
            (long long)ts, pair.c_str());
        return ExecSQL(sql);
    }

    // ---------- 序号发生器 ----------
    int GetNextOrderId() override {
        std::lock_guard<std::mutex> lk(mu_);
        ExecSQL("UPDATE sno SET num=num+1 WHERE name='orderid'");
        const char* sql = "SELECT num FROM sno WHERE name='orderid'";
        sqlite3_stmt* st;
        int id = 0;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW)
                id = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        return id;
    }

    // ---------- 事务控制 ----------
    void BeginTransaction() override {
        sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    }
    void CommitTransaction() override {
        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    }
    void RollbackTransaction() override {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }

    // ===== 兼容旧接口: KlineBar 操作 (供 market_server 使用) =====
    bool InsertKlineBar(const KlineBar& b) {
        Bar bar{};
        bar.timestamp = b.timestamp;
        strncpy(bar.timeframe, b.timeframe, 7);
        strncpy(bar.currency_pair, b.currency_pair, 23);
        bar.open = b.open; bar.high = b.high; bar.low = b.low; bar.close = b.close;
        bar.volume = b.volume; bar.quote_volume = b.quote_volume;
        bar.change_pct = b.change_pct;
        return InsertBar(bar);
    }

    int InsertKlineBars(const std::vector<KlineBar>& bars) {
        std::vector<Bar> converted;
        converted.reserve(bars.size());
        for (auto& kb : bars) {
            Bar b{};
            b.timestamp = kb.timestamp;
            strncpy(b.timeframe, kb.timeframe, 7);
            strncpy(b.currency_pair, kb.currency_pair, 23);
            b.open = kb.open; b.high = kb.high; b.low = kb.low; b.close = kb.close;
            b.volume = kb.volume; b.quote_volume = kb.quote_volume;
            b.change_pct = kb.change_pct;
            converted.push_back(b);
        }
        return InsertBars(converted);
    }

    std::vector<KlineBar> QueryKlineBars(const std::string& pair, const std::string& tf,
                                          int64_t start_ms, int64_t end_ms) {
        auto bars = QueryBars(pair, tf, start_ms, end_ms);
        return BarsToKlineBars(bars);
    }

    std::vector<KlineBar> QueryLatestKlineBars(const std::string& pair,
                                                const std::string& tf, int limit) {
        auto bars = QueryLatestBars(pair, tf, limit);
        return BarsToKlineBars(bars);
    }

private:
    sqlite3* db_           = nullptr;
    sqlite3_stmt* ins_bar_ = nullptr;
    std::mutex mu_;

    static int64_t WallNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void EnsureTables() {
        // stkprice K线表
        ExecSQL(
            "CREATE TABLE IF NOT EXISTS stkprice ("
            "timestamp BIGINT, timeframe VARCHAR(10), currency_pair VARCHAR(20),"
            "currency VARCHAR(20) DEFAULT '', market CHAR(1) DEFAULT '',"
            "openprice DECIMAL, highprice DECIMAL, lowprice DECIMAL, closeprice DECIMAL,"
            "matchqty DECIMAL, matchamt DECIMAL, change_per DECIMAL,"
            "PRIMARY KEY (timestamp, timeframe, currency_pair))");

        // orders 订单表
        ExecSQL(
            "CREATE TABLE IF NOT EXISTS orders ("
            "id INTEGER PRIMARY KEY, timestamp BIGINT, currency_pair VARCHAR(20),"
            "price DECIMAL, quantity DECIMAL, amount DECIMAL,"
            "type VARCHAR(8), order_type INTEGER,"
            "take_profit_price DECIMAL, stop_loss_price DECIMAL,"
            "status VARCHAR(16), matchstatus INTEGER,"
            "matchtimestamp BIGINT, profit DECIMAL,"
            "remark TEXT, old_orderid INTEGER)");

        // sno 序号表
        ExecSQL("CREATE TABLE IF NOT EXISTS sno ("
                "name VARCHAR(64) PRIMARY KEY, num BIGINT NOT NULL)");
        ExecSQL("INSERT OR IGNORE INTO sno (name,num) VALUES ('orderid',0)");

        // executions 执行记录表
        ExecSQL("CREATE TABLE IF NOT EXISTS executions ("
                "currency_pair VARCHAR(20) PRIMARY KEY, timestamp_records BIGINT)");

        // parameter_table 交易参数表
        ExecSQL(
            "CREATE TABLE IF NOT EXISTS parameter_table ("
            "currency_pair VARCHAR(20) PRIMARY KEY,"
            "coefficient DECIMAL DEFAULT 1.0,"
            "take_profit_ratio DECIMAL DEFAULT 0.05,"
            "stop_loss_ratio DECIMAL DEFAULT 0.03,"
            "czzy_signal_count DECIMAL DEFAULT 100)");
    }

    bool EnsureInsertBar() {
        if (ins_bar_) return true;
        const char* sql =
            "INSERT OR REPLACE INTO stkprice "
            "(timestamp,timeframe,currency_pair,currency,market,"
            "openprice,highprice,lowprice,closeprice,matchqty,matchamt,change_per)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?)";
        return sqlite3_prepare_v2(db_, sql, -1, &ins_bar_, nullptr) == SQLITE_OK;
    }

    bool ExecSQL(const char* sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            LOG_E("sqlite exec: %s | %s", err, sql);
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    Bar RowToBar(sqlite3_stmt* st) {
        Bar b{};
        b.timestamp = sqlite3_column_int64(st, 0);
        strncpy(b.timeframe,     (const char*)sqlite3_column_text(st, 1), 7);
        strncpy(b.currency_pair, (const char*)sqlite3_column_text(st, 2), 23);
        b.open         = sqlite3_column_double(st, 3);
        b.high         = sqlite3_column_double(st, 4);
        b.low          = sqlite3_column_double(st, 5);
        b.close        = sqlite3_column_double(st, 6);
        b.volume       = sqlite3_column_double(st, 7);
        b.quote_volume = sqlite3_column_double(st, 8);
        b.change_pct   = sqlite3_column_double(st, 9);
        return b;
    }

    std::vector<Order> QueryOrdersSQL(const char* sql) {
        std::vector<Order> orders;
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return orders;
        while (sqlite3_step(st) == SQLITE_ROW) {
            Order o;
            o.id              = sqlite3_column_int(st, 0);
            o.timestamp       = sqlite3_column_int64(st, 1);
            o.currency_pair   = (const char*)sqlite3_column_text(st, 2);
            o.price           = sqlite3_column_double(st, 3);
            o.quantity        = sqlite3_column_double(st, 4);
            o.amount          = sqlite3_column_double(st, 5);
            o.type            = StrToOperate((const char*)sqlite3_column_text(st, 6));
            o.order_type      = sqlite3_column_int(st, 7);
            o.take_profit_price = sqlite3_column_double(st, 8);
            o.stop_loss_price   = sqlite3_column_double(st, 9);
            o.status          = (const char*)sqlite3_column_text(st, 10);
            o.matchstatus     = sqlite3_column_int(st, 11);
            o.matchtimestamp  = sqlite3_column_int64(st, 12);
            o.profit          = sqlite3_column_double(st, 13);
            const char* remark_text = (const char*)sqlite3_column_text(st, 14);
            o.remark          = remark_text ? remark_text : "";
            o.old_orderid     = sqlite3_column_int(st, 15);
            orders.push_back(o);
        }
        sqlite3_finalize(st);
        return orders;
    }

    static std::vector<KlineBar> BarsToKlineBars(const std::vector<Bar>& bars) {
        std::vector<KlineBar> result;
        result.reserve(bars.size());
        for (auto& b : bars) {
            KlineBar kb{};
            kb.timestamp = b.timestamp;
            strncpy(kb.timeframe, b.timeframe, 7);
            strncpy(kb.currency_pair, b.currency_pair, 23);
            kb.open = b.open; kb.high = b.high; kb.low = b.low; kb.close = b.close;
            kb.volume = b.volume; kb.quote_volume = b.quote_volume;
            kb.change_pct = b.change_pct;
            result.push_back(kb);
        }
        return result;
    }
};

// ============================================================================
// 向后兼容: Database 别名 (旧代码使用 Database 类名)
// ============================================================================
using Database = SQLiteDatabase;
