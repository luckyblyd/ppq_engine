#pragma once
// web_server.h — 内嵌轻量 HTTP 管理端
//
// 依赖 cpp-httplib (httplib.h), 在后台线程运行
// API:
//   GET  /              → 返回管理页面 HTML
//   GET  /api/shm       → 查看共享内存缓存数据
//   GET  /api/db        → 查询数据库数据
//   POST /api/crawl     → 爬取历史K线写入数据库

#include "../third_party/httplib.h"
#include "../third_party/json.hpp"
#include "database_interface.h"
#include "kline_fetcher.h"
#include "market_data_types.h"
#include "order_shm.h"
#include "logger.h"
#include <thread>
#include <atomic>
#include <ctime>

using json = nlohmann::json;

// ============================================================================
// JSON 转换辅助函数 (inline, 在 WebServer 类外定义)
// ============================================================================

inline json OrderToJson(const OrderShmEntry& e) {
    return {
        {"id",          e.id},
        {"timestamp",   e.timestamp},
        {"pair",        e.currency_pair},
        {"price",       e.price},
        {"quantity",    e.quantity},
        {"amount",      e.amount},
        {"type",        e.type},
        {"order_type",  e.order_type},
        {"matchstatus", e.matchstatus},
        {"tp_price",    e.take_profit_price},
        {"sl_price",    e.stop_loss_price},
        {"profit",      e.profit},
        {"old_orderid", e.old_orderid},
        {"remark",      e.remark},
        {"status",      e.status}
    };
}

inline json DbOrderToJson(const Order& o) {
    return {
        {"id",          o.id},
        {"timestamp",   o.timestamp},
        {"pair",        o.currency_pair},
        {"price",       o.price},
        {"quantity",    o.quantity},
        {"amount",      o.amount},
        {"type",        static_cast<int>(o.type)},
        {"order_type",  o.order_type},
        {"matchstatus", o.matchstatus},
        {"tp_price",    o.take_profit_price},
        {"sl_price",    o.stop_loss_price},
        {"profit",      o.profit},
        {"old_orderid", o.old_orderid},
        {"remark",      o.remark},
        {"status",      o.status}
    };
}
class WebServer {
public:
    WebServer(IDatabase* db, KlineFetcher& fetcher, MarketShmRegion* shm,
              OrderShmRegion* order_shm = nullptr)
        : db_(db), fetcher_(fetcher), shm_(shm), order_shm_(order_shm) {}

    ~WebServer() { Stop(); }

    void Start(int port) {
        // ---------- 主页 ----------
        svr_.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(GetPageHtml(), "text/html; charset=utf-8");
        });

        // ---------- 查看共享内存 ----------
        svr_.Get("/api/shm", [this](const httplib::Request& req, httplib::Response& res) {
            json j = json::array();
            if (!shm_) { res.set_content(j.dump(), "application/json"); return; }
            std::string filter = req.has_param("pair") ? req.get_param_value("pair") : "";
            int count = req.has_param("count") ? std::stoi(req.get_param_value("count")) : 100;
            int n = shm_->num_symbols.load(std::memory_order_acquire);
            for (int i = 0; i < n; i++) {
                if (!filter.empty() && filter != shm_->symbol_names[i]) continue;
                KlineBar buf[1440];
                uint32_t got = shm_->rings[i].ReadLatest(buf, count);
                for (uint32_t k = 0; k < got; k++) {
                    j.push_back(BarToJson(buf[k]));
                }
            }
            res.set_content(j.dump(), "application/json");
        });

        // ---------- 查询数据库 ----------
        svr_.Get("/api/db", [this](const httplib::Request& req, httplib::Response& res) {
            std::string pair = req.get_param_value("pair");
            std::string tf   = req.get_param_value("timeframe");
            std::string sd   = req.get_param_value("start");  // "2024-01-01"
            std::string ed   = req.get_param_value("end");
            if (pair.empty() || tf.empty() || sd.empty() || ed.empty()) {
                res.set_content(R"({"error":"missing params"})", "application/json");
                return;
            }
            int64_t s_ms = DateToMs(sd), e_ms = DateToMs(ed) + 86400000LL - 1;
            auto bars = db_->QueryKlineBars(pair, tf, s_ms, e_ms);
            json j = json::array();
            for (auto& b : bars) j.push_back(BarToJson(b));
            res.set_content(j.dump(), "application/json");
        });

        // ---------- 爬取历史数据 ----------
        svr_.Post("/api/crawl", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string pair = body["pair"];
                std::string tf   = body["timeframe"];
                int64_t s_ms = DateToMs(body["start"]);
                int64_t e_ms = DateToMs(body["end"]) + 86400000LL - 1;
                // 异步执行, 不阻塞响应
                std::thread([this, pair, tf, s_ms, e_ms]() {
                    auto bars = fetcher_.FetchHistorical(pair, tf, s_ms, e_ms);
                    int ok = db_->InsertKlineBars(bars);
                    printf("[Crawl] %s %s: fetched %zu, saved %d\n",
                           pair.c_str(), tf.c_str(), bars.size(), ok);
                }).detach();
                res.set_content(R"({"status":"started"})", "application/json");
            } catch (...) {
                res.set_content(R"({"error":"bad request"})", "application/json");
            }
        });

        // ---------- 查询委托 (从共享内存) ----------
        svr_.Get("/api/orders", [this](const httplib::Request& req, httplib::Response& res) {
            json j = json::array();

            // 优先从共享内存读
            OrderShmRegion* oshm = order_shm_;
            if (!oshm) oshm = OrderShmOpen();  // 尝试连接 ppq_main 的共享内存

            if (oshm && oshm->ready.load(std::memory_order_acquire)) {
                std::string filter = req.has_param("pair") ? req.get_param_value("pair") : "";
                OrderShmEntry buf[OrderShmRegion::MAX_ORDERS];
                int count = 0;
                // 乐观锁重试
                for (int retry = 0; retry < 3; ++retry) {
                    if (oshm->ReadOrders(buf, count)) break;
                    plat_usleep(1000);
                }
                for (int i = 0; i < count; ++i) {
                    if (!filter.empty() && filter != buf[i].currency_pair) continue;
                    j.push_back(OrderToJson(buf[i]));
                }
            } else {
                // 降级: 从数据库读
                std::string pair = req.has_param("pair") ? req.get_param_value("pair") : "";
                if (!pair.empty()) {
                    auto orders = db_->QueryActiveOrders(pair);
                    for (auto& o : orders) j.push_back(DbOrderToJson(o));
                }
            }
            res.set_content(j.dump(), "application/json");
        });

        // ---------- 查询账户资金 (从共享内存) ----------
        svr_.Get("/api/accounts", [this](const httplib::Request&, httplib::Response& res) {
            json j = json::array();
            OrderShmRegion* oshm = order_shm_;
            if (!oshm) oshm = OrderShmOpen();

            if (oshm && oshm->ready.load(std::memory_order_acquire)) {
                int n = oshm->num_accounts.load(std::memory_order_acquire);
                for (int i = 0; i < n; ++i) {
                    auto& a = oshm->accounts[i];
                    j.push_back({
                        {"pair",     a.currency_pair},
                        {"capital",  a.current_capital},
                        {"occupied", a.occupied_capital},
                        {"pnl",      a.floating_pnl},
                        {"orders",   a.active_orders},
                        {"longs",    a.filled_longs},
                        {"shorts",   a.filled_shorts}
                    });
                }
            }
            res.set_content(j.dump(), "application/json");
        });

        // ---------- 删除委托 ----------
        svr_.Delete("/api/orders", [this](const httplib::Request& req, httplib::Response& res) {
            if (!req.has_param("id")) {
                res.set_content(R"({"error":"missing id"})", "application/json");
                return;
            }
            int order_id = std::stoi(req.get_param_value("id"));

            // 1. 通过共享内存通知 ppq_main 取消
            OrderShmRegion* oshm = order_shm_;
            if (!oshm) oshm = OrderShmOpen();
            bool shm_ok = false;
            if (oshm && oshm->ready.load(std::memory_order_acquire)) {
                shm_ok = oshm->RequestDelete(order_id);
            }

            // 2. 同时从数据库标记取消
            db_->UpdateOrderStatus(order_id, "CANCELED", 3);

            json resp = {
                {"order_id", order_id},
                {"shm_notified", shm_ok},
                {"db_updated", true}
            };
            res.set_content(resp.dump(), "application/json");
        });

        thread_ = std::thread([this, port]() { svr_.listen("0.0.0.0", port); });
        printf("[WebServer] http://localhost:%d\n", port);
    }

    void Stop() {
        svr_.stop();
        if (thread_.joinable()) thread_.join();
    }

private:
    // K线 → JSON
    static json BarToJson(const KlineBar& b) {
        // 时间戳转北京时间字符串
        time_t sec = b.timestamp / 1000;
        sec += 8 * 3600;  // UTC+8
        struct tm t;
        plat_gmtime_r(&sec, &t);
        char ts[32];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        return {
            {"time",     ts},
            {"timestamp", b.timestamp},
            {"pair",     b.currency_pair},
            {"tf",       b.timeframe},
            {"open",     b.open},
            {"high",     b.high},
            {"low",      b.low},
            {"close",    b.close},
            {"volume",   b.volume},
            {"amount",   b.quote_volume},
            {"change",   b.change_pct},
            {"gap",      (b.flags & 0x01) != 0}
        };
    }

    // "2024-01-15" → epoch ms (UTC)
    static int64_t DateToMs(const std::string& s) {
        struct tm t{};
        sscanf(s.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
        t.tm_year -= 1900; t.tm_mon -= 1;
        return (int64_t)plat_timegm(&t) * 1000;
    }

    // 内嵌 HTML 页面 (见下方 index.html 独立文件, 这里内联返回)
    static std::string GetPageHtml();

    httplib::Server svr_;
    IDatabase* db_;
    KlineFetcher& fetcher_;
    MarketShmRegion* shm_;
    OrderShmRegion* order_shm_;
    std::thread thread_;
};

