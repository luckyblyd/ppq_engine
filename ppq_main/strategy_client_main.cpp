// strategy_client_main.cpp — 简单策略客户端
//
// 连接 pub/sub 共享内存, 订阅 K线 topic, 打印收到的行情
// 同时可读取行情缓存共享内存查看最新数据
//
// 用法: ./strategy_client [币种topic, 默认 kline::ETH/USDT::1m]
// 编译: make strategy_client

#include "shm_ipc.h"
#include "market_data_types.h"
#include "third_party/json.hpp"

#include <csignal>
#include <cstdio>
#include <thread>
#include <string>
#include <ctime>

using json = nlohmann::json;

static const char* kPubsubShmName    = "/binance_pubsub";
static volatile sig_atomic_t running = 1;
static EventCore* g_ec               = nullptr;

static void sig_handler(int) {
    running = 0;
    if (g_ec) g_ec->Shutdown();
}

// 北京时间格式化
static std::string MsToBeijing(int64_t ms) {
    time_t s = ms / 1000 + 8 * 3600;
    struct tm t;
    plat_gmtime_r(&s, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

// ============================================================================
// 策略回调: 收到K线消息后处理
// ============================================================================

class SimpleStrategy : public ISubscriberCB {
public:
    void OnMessage(const std::string& topic, const char* data, int len) override {
        msg_count_++;
        try {
            auto j = json::parse(std::string(data, len));
            int64_t ts   = j["ts"];
            double close = j["c"];
            double vol   = j["v"];
            bool gap     = j.value("gap", false);

            // 简单移动平均 (最近10根收盘价)
            prices_.push_back(close);
            if (prices_.size() > 10) prices_.erase(prices_.begin());
            double ma = 0;
            for (double p : prices_) ma += p;
            ma /= prices_.size();

            printf("[Strategy] #%d %s | %s | close=%.4f ma10=%.4f vol=%.2f%s\n",
                   msg_count_, topic.c_str(), MsToBeijing(ts).c_str(),
                   close, ma, vol, gap ? " ⚠️GAP" : "");

            // 示例信号: 收盘价上穿MA10
            if (prices_.size() >= 10) {
                if (close > ma && prev_close_ <= prev_ma_) {
                    printf("  >>> 📈 信号: 上穿MA10, close=%.4f > ma=%.4f\n", close, ma);
                }
                if (close < ma && prev_close_ >= prev_ma_) {
                    printf("  >>> 📉 信号: 下穿MA10, close=%.4f < ma=%.4f\n", close, ma);
                }
            }
            prev_close_ = close;
            prev_ma_    = ma;
        } catch (...) {
            printf("[Strategy] parse error: %.*s\n", len, data);
        }
    }

    void OnSubscribeSuccess(const std::string& topic) override {
        printf("[Strategy] subscribed: %s\n", topic.c_str());
    }

private:
    int msg_count_ = 0;
    std::vector<double> prices_;
    double prev_close_ = 0, prev_ma_ = 0;
};

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    // 默认订阅 topic
    std::string topic = "kline::ETH/USDT::1m";
    if (argc > 1) topic = argv[1];

    // 连接 pub/sub 共享内存
    printf("[Client] connecting to %s\n", kPubsubShmName);
    ShmRegion* region = ShmOpen(kPubsubShmName);
    if (!region) {
        fprintf(stderr, "[Client] ShmOpen failed (server running?)\n");
        return 1;
    }
    while (running && !region->server_ready.load(std::memory_order_acquire))
        plat_usleep(1000);
    if (!running) return 0;

    int cid = region->next_client_id.fetch_add(1, std::memory_order_relaxed);
    if (cid >= ShmRegion::kMaxClients) {
        fprintf(stderr, "[Client] too many clients (max %d)\n", ShmRegion::kMaxClients);
        return 1;
    }

    SystemClock clk;
    EventCore ec(clk);
    g_ec = &ec;

    ShmPubsubClient client(region->channels[cid]);
    ec.RegisterEvent(client);

    // 订阅K线 topic
    SimpleStrategy strategy;
    client.Subscribe(topic, strategy);
    region->channels[cid].client_connected.store(true, std::memory_order_release);

    printf("[Client] client #%d, subscribed to [%s]\n", cid, topic.c_str());
    printf("[Client] waiting for market data... Ctrl+C to quit\n\n");

    // 可选: 读取行情缓存共享内存显示最新数据
    MarketShmRegion* mshm = MarketShmOpen();
    if (mshm) {
        int n = mshm->num_symbols.load(std::memory_order_acquire);
        printf("[Client] Market SHM: %d symbols cached\n", n);
        for (int i = 0; i < n; i++) {
            printf("  [%d] %s/%s, bars=%u\n", i, mshm->symbol_names[i],
                   mshm->symbol_tfs[i], mshm->rings[i].total.load());
        }
        printf("\n");
    }

    // 事件循环
    ec.Run();

    // 清理
    region->channels[cid].client_connected.store(false, std::memory_order_release);
    printf("\n[Client] disconnected\n");
    return 0;
}
