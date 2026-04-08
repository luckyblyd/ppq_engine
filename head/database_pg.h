#pragma once
// database_pg.h — PostgreSQL 数据库实现
// 依赖 libpq (PostgreSQL C client library)
// 编译: 需链接 -lpq
//
// 连接字符串格式: "host=localhost port=5432 dbname=hhjrdata user=hhjr password=hhjr"

#include "database_interface.h"
#include "logger.h"

#ifdef _WIN32
  #pragma comment(lib, "libpq.lib")
#endif

// 前向声明避免暴露 libpq 头文件
// 实际使用时需要 #include <libpq-fe.h>
struct pg_conn;
typedef struct pg_conn PGconn;
struct pg_result;
typedef struct pg_result PGresult;

// libpq 函数声明（避免头文件依赖，实际编译时包含 libpq-fe.h）
extern "C" {
    PGconn* PQconnectdb(const char*);
    void PQfinish(PGconn*);
    int PQstatus(const PGconn*);
    PGresult* PQexec(PGconn*, const char*);
    PGresult* PQexecParams(PGconn*, const char*, int, const void*,
                           const char* const*, const int*, const int*, int);
    int PQresultStatus(const PGresult*);
    int PQntuples(const PGresult*);
    int PQnfields(const PGresult*);
    char* PQgetvalue(const PGresult*, int, int);
    int PQgetisnull(const PGresult*, int, int);
    void PQclear(PGresult*);
    char* PQerrorMessage(const PGconn*);
    char* PQresultErrorMessage(const PGresult*);
}

// 常量
#define PGRES_COMMAND_OK  1
#define PGRES_TUPLES_OK   2
#define CONNECTION_OK     0

class PostgresDatabase : public IDatabase {
public:
    ~PostgresDatabase() override { Close(); }

    bool Open(const std::string& conn_str) override {
        conn_ = PQconnectdb(conn_str.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            LOG_E("PG connect failed: %s", PQerrorMessage(conn_));
            PQfinish(conn_);
            conn_ = nullptr;
            return false;
        }

        LOG_I("PostgreSQL connected");
        EnsureTables();
        return true;
    }

    void Close() override {
        if (conn_) { PQfinish(conn_); conn_ = nullptr; }
    }

    bool IsOpen() const override { return conn_ != nullptr; }

    // ---------- K线数据 ----------
    bool InsertBar(const Bar& bar) override {
        char sql[1024];
        snprintf(sql, sizeof(sql),
            "INSERT INTO stkprice (timestamp,timeframe,currency_pair,openprice,"
            "highprice,lowprice,closeprice,matchqty,matchamt,change_per) "
            "VALUES (%lld,'%s','%s',%.8f,%.8f,%.8f,%.8f,%.4f,%.4f,%.4f) "
            "ON CONFLICT (timestamp,timeframe,currency_pair) DO NOTHING",
            (long long)bar.timestamp, bar.timeframe, bar.currency_pair,
            bar.open, bar.high, bar.low, bar.close,
            bar.volume, bar.quote_volume, bar.change_pct);
        return Exec(sql);
    }

    int InsertBars(const std::vector<Bar>& bars) override {
        BeginTransaction();
        int ok = 0;
        for (auto& b : bars) { if (InsertBar(b)) ok++; }
        CommitTransaction();
        return ok;
    }

    std::vector<Bar> QueryBars(const std::string& pair, const std::string& tf,
                               int64_t start_ms, int64_t end_ms) override {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "SELECT timestamp,timeframe,currency_pair,openprice,highprice,"
            "lowprice,closeprice,matchqty,matchamt,change_per FROM stkprice "
            "WHERE currency_pair='%s' AND timeframe='%s' "
            "AND timestamp>=%lld AND timestamp<=%lld ORDER BY timestamp",
            pair.c_str(), tf.c_str(), (long long)start_ms, (long long)end_ms);
        return QueryBarsSQL(sql);
    }

    std::vector<Bar> QueryLatestBars(const std::string& pair,
                                      const std::string& tf, int limit) override {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "SELECT timestamp,timeframe,currency_pair,openprice,highprice,"
            "lowprice,closeprice,matchqty,matchamt,change_per FROM stkprice "
            "WHERE currency_pair='%s' AND timeframe='%s' "
            "ORDER BY timestamp DESC LIMIT %d",
            pair.c_str(), tf.c_str(), limit);
        auto bars = QueryBarsSQL(sql);
        std::reverse(bars.begin(), bars.end());
        return bars;
    }

    int CountBars(const std::string& pair, const std::string& tf) override {
        char sql[256];
        snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM stkprice WHERE currency_pair='%s' AND timeframe='%s'",
            pair.c_str(), tf.c_str());
        PGresult* res = PQexec(conn_, sql);
        int count = 0;
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0)
            count = atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return count;
    }

    // ---------- 订单操作 ----------
    bool InsertOrder(const Order& order) override {
        char sql[2048];
        snprintf(sql, sizeof(sql),
            "INSERT INTO orders (id,timestamp,currency_pair,price,quantity,amount,"
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
        return Exec(sql);
    }

    bool UpdateOrderStatus(int order_id, const std::string& status,
                           int matchstatus, double profit, double price) override {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "UPDATE orders SET status='%s',matchstatus=%d,profit=%.8f,price=%.8f "
            "WHERE id=%d",
            status.c_str(), matchstatus, profit, price, order_id);
        return Exec(sql);
    }

    bool UpdateOrderTPSL(int order_id, double tp, double sl) override {
        char sql[256];
        snprintf(sql, sizeof(sql),
            "UPDATE orders SET take_profit_price=%.8f,stop_loss_price=%.8f WHERE id=%d",
            tp, sl, order_id);
        return Exec(sql);
    }

    std::vector<Order> QueryOpenOrders(const std::string& pair) override {
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
        TradingParameters p;
        p.currency_pair = pair;
        char sql[256];
        snprintf(sql, sizeof(sql),
            "SELECT coefficient,take_profit_ratio,stop_loss_ratio,czzy_signal_count "
            "FROM parameter_table WHERE currency_pair='%s'", pair.c_str());
        PGresult* res = PQexec(conn_, sql);
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            p.coefficient       = atof(PQgetvalue(res, 0, 0));
            p.take_profit_ratio = atof(PQgetvalue(res, 0, 1));
            p.stop_loss_ratio   = atof(PQgetvalue(res, 0, 2));
            p.czzy_signal_count = atof(PQgetvalue(res, 0, 3));
        }
        PQclear(res);
        return p;
    }

    // ---------- 执行记录 ----------
    int64_t QueryLastExecution(const std::string& pair) override {
        char sql[256];
        snprintf(sql, sizeof(sql),
            "SELECT timestamp_records FROM executions WHERE currency_pair='%s'",
            pair.c_str());
        PGresult* res = PQexec(conn_, sql);
        int64_t ts = -1;
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0)
            ts = atoll(PQgetvalue(res, 0, 0));
        PQclear(res);
        return ts;
    }

    bool InsertExecution(const std::string& pair, int64_t ts) override {
        char sql[256];
        snprintf(sql, sizeof(sql),
            "INSERT INTO executions (currency_pair,timestamp_records) VALUES ('%s',%lld)",
            pair.c_str(), (long long)ts);
        return Exec(sql);
    }

    bool UpdateExecution(const std::string& pair, int64_t ts) override {
        char sql[256];
        snprintf(sql, sizeof(sql),
            "UPDATE executions SET timestamp_records=%lld WHERE currency_pair='%s'",
            (long long)ts, pair.c_str());
        return Exec(sql);
    }

    // ---------- 序号发生器 ----------
    int GetNextOrderId() override {
        const char* sql = "UPDATE sno SET num=num+1 WHERE name='orderid' RETURNING num";
        PGresult* res = PQexec(conn_, sql);
        int id = 0;
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0)
            id = atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return id;
    }

    // ---------- 事务 ----------
    void BeginTransaction() override { Exec("BEGIN"); }
    void CommitTransaction() override { Exec("COMMIT"); }
    void RollbackTransaction() override { Exec("ROLLBACK"); }

private:
    PGconn* conn_ = nullptr;

    static int64_t WallNowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool Exec(const char* sql) {
        PGresult* res = PQexec(conn_, sql);
        int st = PQresultStatus(res);
        bool ok = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK);
        if (!ok) LOG_E("PG exec: %s | %s", PQresultErrorMessage(res), sql);
        PQclear(res);
        return ok;
    }

    void EnsureTables() {
        // sno 序号表
        Exec("CREATE TABLE IF NOT EXISTS sno ("
             "name VARCHAR(64) PRIMARY KEY, num BIGINT NOT NULL)");
        Exec("INSERT INTO sno (name,num) VALUES ('orderid',0) ON CONFLICT DO NOTHING");

        // executions 表
        Exec("CREATE TABLE IF NOT EXISTS executions ("
             "currency_pair VARCHAR(20) PRIMARY KEY, timestamp_records BIGINT)");
    }

    std::vector<Bar> QueryBarsSQL(const char* sql) {
        std::vector<Bar> bars;
        PGresult* res = PQexec(conn_, sql);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) { PQclear(res); return bars; }
        int rows = PQntuples(res);
        bars.reserve(rows);
        for (int r = 0; r < rows; ++r) {
            Bar b{};
            b.timestamp = atoll(PQgetvalue(res, r, 0));
            strncpy(b.timeframe, PQgetvalue(res, r, 1), 7);
            strncpy(b.currency_pair, PQgetvalue(res, r, 2), 23);
            b.open         = atof(PQgetvalue(res, r, 3));
            b.high         = atof(PQgetvalue(res, r, 4));
            b.low          = atof(PQgetvalue(res, r, 5));
            b.close        = atof(PQgetvalue(res, r, 6));
            b.volume       = atof(PQgetvalue(res, r, 7));
            b.quote_volume = atof(PQgetvalue(res, r, 8));
            b.change_pct   = atof(PQgetvalue(res, r, 9));
            bars.push_back(b);
        }
        PQclear(res);
        return bars;
    }

    std::vector<Order> QueryOrdersSQL(const char* sql) {
        std::vector<Order> orders;
        PGresult* res = PQexec(conn_, sql);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) { PQclear(res); return orders; }
        int rows = PQntuples(res);
        for (int r = 0; r < rows; ++r) {
            Order o;
            o.id              = atoi(PQgetvalue(res, r, 0));
            o.timestamp       = atoll(PQgetvalue(res, r, 1));
            o.currency_pair   = PQgetvalue(res, r, 2);
            o.price           = atof(PQgetvalue(res, r, 3));
            o.quantity        = atof(PQgetvalue(res, r, 4));
            o.amount          = atof(PQgetvalue(res, r, 5));
            o.type            = StrToOperate(PQgetvalue(res, r, 6));
            o.order_type      = atoi(PQgetvalue(res, r, 7));
            o.take_profit_price = atof(PQgetvalue(res, r, 8));
            o.stop_loss_price   = atof(PQgetvalue(res, r, 9));
            o.status          = PQgetvalue(res, r, 10);
            o.matchstatus     = atoi(PQgetvalue(res, r, 11));
            o.matchtimestamp  = atoll(PQgetvalue(res, r, 12));
            o.profit          = atof(PQgetvalue(res, r, 13));
            o.remark          = PQgetvalue(res, r, 14);
            o.old_orderid     = atoi(PQgetvalue(res, r, 15));
            orders.push_back(o);
        }
        PQclear(res);
        return orders;
    }
};
