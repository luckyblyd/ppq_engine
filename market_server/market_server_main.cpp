// market_server_main.cpp — 行情服务主程序
//
// 功能:
//   1. 读取 config.ini 配置 (含 initdata_num / onmessage 开关)
//   2. 创建 pub/sub 共享内存 + 行情缓存共享内存 (由 onmessage 控制)
//   3. 初始化K线数据: 先从数据库加载, 不足则从API爬取 (每次最多1000条)
//   4. 为每个币种启动定时拉取线程 (双重采样: T+6s / T+16s / T+65s)
//   5. 拉取成功 → 写共享内存 + 发布pub/sub消息 + 存数据库
//   6. 启动内嵌 Web 管理端
//
// 编译: make market_server
// 运行: ./market_server [config.ini]

#include "shm_ipc.h"
#include "config_parser.h"
#include "logger.h"
#include "market_data_types.h"
#include "database_interface.h"
#include "database_engine.h"
#include "kline_fetcher.h"
#include "web_server.h"
#include "web_page.h"

#include <csignal>
#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <atomic>

using namespace std::chrono;

// ============================================================================
// 全局状态
// ============================================================================

static volatile sig_atomic_t g_running = 1;
static EventCore* g_event_core        = nullptr;

static void signal_handler(int) {
    g_running = 0;
    if (g_event_core) g_event_core->Shutdown();
}

// ============================================================================
// 工具: 获取当前 epoch ms (wall-clock)
// ============================================================================

static int64_t WallNowMs() {
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

// ============================================================================
// KlineScheduler — 单币种定时拉取线程
// ============================================================================
//
// 调度逻辑 (以1m为例):
//   每到分钟边界后:
//     T+6s  → 第一次拉取, 检查K线是否已切分
//     T+16s → 第二次重试 (如果第一次未拿到)
//     T+65s → 终极兜底 (仍失败则记录日志, 不阻塞下一分钟)

class KlineScheduler {
public:
    KlineScheduler(const std::string& pair, const std::string& tf,
                   KlineFetcher& fetcher, IDatabase* db,
                   MarketShmRegion* mshm, TopicServer& pub_server,
                   ShmRegion* pub_shm, bool onmessage)
        : pair_(pair), tf_(tf), fetcher_(fetcher), db_(db)
        , mshm_(mshm), pub_server_(pub_server), pub_shm_(pub_shm)
        , onmessage_(onmessage)
    {
        interval_ms_ = (tf == "5m") ? 300000LL : 60000LL;
        // 仅在 onmessage 启用时注册共享内存槽位
        if (onmessage_ && mshm_) sym_idx_ = mshm_->FindOrAdd(pair_.c_str(), tf_.c_str());
    }

    // 启动拉取线程
    void Start() {
        thread_ = std::thread([this]() { Run(); });
    }

    void Join() { if (thread_.joinable()) thread_.join(); }

private:
    void Run() {
        printf("[Scheduler] %s/%s started\n", pair_.c_str(), tf_.c_str());
        while (g_running) {
            // 计算下一个区间边界
            int64_t now = WallNowMs();
            int64_t next_boundary = ((now / interval_ms_) + 1) * interval_ms_;
            // 我们要拿的是 "上一根已完成K线", 其 open_time = next_boundary - interval_ms_
            int64_t expected_ts = next_boundary - interval_ms_;

            // --- 第一次: T+6s ---
            SleepUntil(next_boundary + 6000);
            if (!g_running) break;
            if (TryFetch(expected_ts)) continue;

            // --- 第二次: T+16s ---
            SleepUntil(next_boundary + 16000);
            if (!g_running) break;
            if (TryFetch(expected_ts)) continue;

            // --- 终极兜底: T+55s ---
            SleepUntil(next_boundary + 55000);
            if (!g_running) break;
            if (TryFetch(expected_ts)) continue;

            // 全部失败
            LOG_E("[%s] all 3 attempts failed for ts=%lld", pair_.c_str(), (long long)expected_ts);
        }
        printf("[Scheduler] %s/%s stopped\n", pair_.c_str(), tf_.c_str());
    }

    bool TryFetch(int64_t expected_ts) {
        auto bars = fetcher_.FetchLatest(pair_, tf_, 2);
        if (bars.size() < 2) return false;

        // bars[0] = 倒数第2根 (已完成), bars[1] = 最新未完成
        // 找匹配 expected_ts 的那根 (已完成的)
        KlineBar* target = nullptr;
        for (auto& b : bars) {
            if (b.timestamp == expected_ts) { target = &b; break; }
        }
        if (!target) return false;  // 还没切出来

        ProcessBar(*target);
        return true;
    }

    void ProcessBar(KlineBar& bar) {
        // 连续性检测
        if (last_ts_ > 0 && bar.timestamp - last_ts_ > interval_ms_) {
            bar.flags |= 0x01;  // 标记不连续
            LOG_E("[%s] gap: %lld → %lld", pair_.c_str(),
                  (long long)last_ts_, (long long)bar.timestamp);
        }
        last_ts_ = bar.timestamp;

        // 1) 写入行情共享内存 (仅 onmessage 启用时)
        if (onmessage_ && mshm_ && sym_idx_ >= 0) {
            mshm_->rings[sym_idx_].Push(bar);
        }

        // 2) 通过 pub/sub 发布消息给策略 (仅 onmessage 启用时)
        if (onmessage_) {
            PublishToPubsub(bar);
        }

        // 3) 存入数据库 (始终执行)
        db_->InsertKlineBar(bar);

        printf("[%s] bar ts=%lld close=%.4f vol=%.2f%s\n", pair_.c_str(),
               (long long)bar.timestamp, bar.close, bar.volume,
               (bar.flags & 0x01) ? " [GAP]" : "");
    }

    void PublishToPubsub(const KlineBar& bar) {
        // 序列化为 JSON
        char buf[512];
        int len = snprintf(buf, sizeof(buf),
            R"({"ts":%lld,"o":%.8f,"h":%.8f,"l":%.8f,"c":%.8f,"v":%.4f,"qv":%.4f,"cp":%.4f,"gap":%s})",
            (long long)bar.timestamp, bar.open, bar.high, bar.low, bar.close,
            bar.volume, bar.quote_volume, bar.change_pct,
            (bar.flags & 0x01) ? "true" : "false");

        std::string topic = "kline::" + pair_ + "::" + tf_;
        pub_server_.Publish(topic, buf, len);
    }

    void SleepUntil(int64_t target_ms) {
        while (g_running) {
            int64_t diff = target_ms - WallNowMs();
            if (diff <= 0) break;
            int64_t s = std::min(diff, (int64_t)500);  // 每500ms检查 g_running
            std::this_thread::sleep_for(milliseconds(s));
        }
    }

    std::string pair_, tf_;
    int64_t interval_ms_ = 60000;
    int64_t last_ts_     = 0;
    int sym_idx_         = -1;

    KlineFetcher& fetcher_;
    IDatabase* db_;
    MarketShmRegion* mshm_;
    TopicServer& pub_server_;
    ShmRegion* pub_shm_;
    bool onmessage_      = true;
    std::thread thread_;
};

// ============================================================================
// InitializeKlineData — 首次运行时从数据库/API初始化K线数据
// ============================================================================
//
// 逻辑:
//   1. 对每个币种, 先查数据库已有数据量
//   2. 不足 initdata_num 则从API补充 (每次最多1000条, 循环拉取)
//   3. 如果 onmessage=true, 将数据加载到共享内存环形缓冲区

static void InitializeKlineData(const std::vector<std::string>& pairs,
                                const std::string& timeframe,
                                int initdata_num,
                                IDatabase* db,
                                KlineFetcher& fetcher,
                                MarketShmRegion* market_shm,
                                bool onmessage) {
    printf("\n[Init] === Initializing kline data (%d bars per pair) ===\n", initdata_num);
    int64_t interval_ms = (timeframe == "5m") ? 300000LL : 60000LL;

    for (auto& pair : pairs) {
        // Step 1: 查询数据库已有数据量
        int db_count = db->CountBars(pair, timeframe);
        printf("[Init] %s/%s: DB has %d bars", pair.c_str(), timeframe.c_str(), db_count);

        if (db_count < initdata_num) {
            // Step 2: 从API补充不足的数据
            int need = initdata_num - db_count;
            printf(", need %d more, fetching from API...\n", need);

            int64_t now_ms = WallNowMs();
            // 结束时间: 当前K线开始时间 - 1ms (排除当前未完成K线)
            int64_t end_ms = (now_ms / interval_ms) * interval_ms - 1;
            // 起始时间: 往前推 initdata_num 根K线
            int64_t start_ms = end_ms - (int64_t)initdata_num * interval_ms;

            // 如果DB已有数据, 从最新一条之后开始拉取, 避免重复
            if (db_count > 0) {
                auto latest = db->QueryLatestKlineBars(pair, timeframe, 1);
                if (!latest.empty()) {
                    start_ms = latest.back().timestamp + 1;
                }
            }

            // FetchHistorical 内部自动分页 (每次1000条) + 自动去掉未完成K线
            auto bars = fetcher.FetchHistorical(pair, timeframe, start_ms, end_ms);
            if (!bars.empty()) {
                int saved = db->InsertKlineBars(bars);
                printf("[Init] %s: fetched %zu bars from API, saved %d to DB\n",
                       pair.c_str(), bars.size(), saved);
            } else {
                printf("[Init] %s: API returned no data\n", pair.c_str());
            }
        } else {
            printf(", sufficient\n");
        }

        // Step 3: 加载到共享内存 (仅 onmessage 启用时)
        // 环形缓冲区容量为 CAPACITY=6000, 新数据入栈旧数据自动被覆盖
        if (onmessage && market_shm) {
            auto bars = db->QueryLatestKlineBars(pair, timeframe, initdata_num);
            int sym_idx = market_shm->FindOrAdd(pair.c_str(), timeframe.c_str());
            if (sym_idx >= 0 && !bars.empty()) {
                for (auto& b : bars) {
                    market_shm->rings[sym_idx].Push(b);
                }
                printf("[Init] %s: loaded %zu bars into shared memory (ring capacity=%u)\n",
                       pair.c_str(), bars.size(), KlineRing::LOGICAL_CAPACITY);
            }
        }
    }
    printf("[Init] === Initialization complete ===\n\n");
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // --- 1. 读取配置 ---
    const char* cfg_path = (argc > 1) ? argv[1] : "market_server.ini";
    ConfigParser cfg;
    if (!cfg.Load(cfg_path)) {
        fprintf(stderr, "Cannot load %s\n", cfg_path);
        return 1;
    }

    std::string timeframe    = cfg.Get("General", "timeframe", "1m");
    auto pairs               = cfg.GetList("General", "currency_pairs");
    int initdata_num         = cfg.GetInt("General", "initdata_num", 6000);
    bool onmessage           = cfg.GetBool("General", "onmessage", true);
    std::string proxy_url    = cfg.Get("Proxy", "proxy_url");
    std::string base_url     = cfg.Get("Exchange", "base_url", "https://fapi.binance.com");
    int web_port             = cfg.GetInt("WebServer", "port", 8080);

    printf("=== Binance Market Server ===\n");
    printf("Timeframe    : %s\n", timeframe.c_str());
    printf("Pairs        : ");
    for (auto& p : pairs) printf("%s ", p.c_str());
    printf("\nDB type      : %s\n", cfg.Get("database", "db_type", "sqlite3").c_str());
    printf("InitDataNum  : %d\n", initdata_num);
    printf("OnMessage    : %s\n", onmessage ? "true" : "false");
    printf("WebPort      : %d\n", web_port);

    // --- 2. 初始化数据库 (通过 DatabaseEngine 工厂) ---
    auto db = DatabaseEngine::Create(cfg);
    if (!db) { fprintf(stderr, "DB connection failed\n"); return 1; }

    // --- 3. 初始化 K线拉取器 (proxy / base_url 来自配置) ---
    KlineFetcher fetcher(base_url, proxy_url);

    // --- 4. 创建共享内存 (仅 onmessage=true 时) ---
    static const char* kPubsubShmName = "/binance_pubsub";
    ShmRegion* pub_region = nullptr;
    MarketShmRegion* market_shm = nullptr;

    SystemClock sys_clock;
    EventCore event_core(sys_clock);
    g_event_core = &event_core;
    TopicServer topic_server;

    std::vector<std::unique_ptr<ShmServerProxy>> proxies;

    if (onmessage) {
        // pub/sub 共享内存
        pub_region = ShmCreate(kPubsubShmName);
        if (!pub_region) { fprintf(stderr, "ShmCreate failed\n"); return 1; }

        for (int i = 0; i < ShmRegion::kMaxClients; i++) {
            proxies.push_back(std::make_unique<ShmServerProxy>(
                topic_server, pub_region->channels[i]));
            event_core.RegisterEvent(*proxies.back());
        }
        pub_region->server_ready.store(true, std::memory_order_release);

        // 行情缓存共享内存
        market_shm = MarketShmCreate();
        if (market_shm) market_shm->ready.store(true, std::memory_order_release);
    } else {
        printf("[Server] onmessage=false, shared memory and broadcast disabled\n");
    }

    // --- 5. 初始化K线数据 (首次运行时从DB/API填充共享内存) ---
    if (initdata_num > 0) {
        InitializeKlineData(pairs, timeframe, initdata_num, db.get(), fetcher,
                           market_shm, onmessage);
    }

    // --- 6. 启动 Web 管理端 ---
    WebServer web(db.get(), fetcher, market_shm);
    web.Start(web_port);

    // --- 7. 启动各币种定时拉取线程 ---
    std::vector<std::unique_ptr<KlineScheduler>> schedulers;
    for (auto& pair : pairs) {
        auto s = std::make_unique<KlineScheduler>(
            pair, timeframe, fetcher, db.get(), market_shm, topic_server,
            pub_region, onmessage);
        s->Start();
        schedulers.push_back(std::move(s));
    }

    printf("[Server] ready, %zu pairs, Ctrl+C to quit\n", pairs.size());

    // --- 8. 主线程事件循环 ---
    if (onmessage) {
        // pub/sub 模式: EventCore 驱动
        event_core.Run();
    } else {
        // 纯数据库缓存模式: 简单等待
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // --- 9. 清理 ---
    printf("\n[Server] shutting down...\n");
    for (auto& s : schedulers) s->Join();
    web.Stop();
    if (onmessage) {
        MarketShmDestroy(market_shm);
        ShmDestroy(kPubsubShmName, pub_region);
    }
    printf("[Server] done\n");
    return 0;
}
