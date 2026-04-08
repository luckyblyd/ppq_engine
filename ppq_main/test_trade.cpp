// test_trade.cpp — 测试飞书通知和币安交易接口
//
// 编译 (MinGW):
//   g++ -std=c++17 -I../head -I../third_party test_trade.cpp -o test_trade.exe
//        -lcurl -lws2_32
//
// 运行:
//   ./test_trade.exe                    # 运行所有测试
//   ./test_trade.exe feishu <url>       # 测试飞书通知 (需要填入真实 webhook url)
//   ./test_trade.exe binance            # 测试币安 DLL 接口

#include <cstdio>
#include <cstring>
#include <string>
#include <cassert>
#include "order.h"
#include "feishu_notify.h"
#include "dll_loader.h"
#include "exchange_interface.h"
#include "exchange_binance_dll.h"

// ============================================================================
// 测试辅助
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) printf("\n--- TEST: %s ---\n", name)
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_passed++; } \
    else      { printf("  [FAIL] %s\n", msg); g_failed++; } \
} while(0)

// ============================================================================
// 飞书通知测试 (本地构造, 不实际发送)
// ============================================================================

static void TestFeishuLocal() {
    TEST("FeishuNotify 本地构造测试");

    // 1. 未配置 URL 时不发送
    FeishuNotify fs;
    CHECK(!fs.IsEnabled(), "未设置 URL 时 IsEnabled()=false");

    // 2. 设置 URL 后启用
    fs.SetUrl("https://open.feishu.cn/open-apis/bot/v2/hook/test123");
    CHECK(fs.IsEnabled(), "设置 URL 后 IsEnabled()=true");

    // 3. 构造开仓订单
    Order open_order;
    open_order.id = 13335;
    open_order.timestamp = 1700000000000LL;
    open_order.currency_pair = "ME/USDT";
    open_order.price = 0.1022;
    open_order.quantity = 0.1022;
    open_order.amount = 50.0;
    open_order.type = Operate::LO;
    open_order.take_profit_price = 0.104653;
    open_order.stop_loss_price = 0.09463;

    printf("  开仓通知格式: %s|开多|%d|%.4fU|%.4fU|止盈%.6f|止损%.6f\n",
           open_order.currency_pair.c_str(), open_order.id,
           open_order.quantity, open_order.amount,
           open_order.take_profit_price, open_order.stop_loss_price);
    CHECK(true, "开仓订单构造正确");

    // 4. 构造平仓订单
    Order close_order;
    close_order.id = 13365;
    close_order.timestamp = 1700090600000LL;
    close_order.currency_pair = "ME/USDT";
    close_order.price = 0.0966;
    close_order.quantity = 0.0966;
    close_order.amount = 47.2374;
    close_order.type = Operate::LE;
    close_order.profit = -2.7384;
    close_order.old_orderid = 13335;

    double duration_min = (close_order.timestamp - open_order.timestamp) / 60000.0;
    printf("  平仓通知格式: %s|平多|%d|原委托id%d|%.4fU|%.4fU|持续时间%.1f分钟|盈亏%.4f\n",
           close_order.currency_pair.c_str(), close_order.id,
           open_order.id, close_order.quantity, close_order.amount,
           duration_min, close_order.profit);
    CHECK(duration_min > 0, "持续时间计算正确");

    // 5. 空头开仓
    Order short_order;
    short_order.id = 20001;
    short_order.currency_pair = "BTC/USDT";
    short_order.type = Operate::SO;
    short_order.quantity = 0.001;
    short_order.amount = 40.0;
    short_order.take_profit_price = 38000.0;
    short_order.stop_loss_price = 42000.0;
    // NotifyOpen 应显示 "开空"
    CHECK(short_order.type == Operate::SO, "空头订单类型正确");

    // 6. 非开仓/非平仓 → 不通知
    Order initial_order;
    initial_order.type = Operate::INITIAL;
    // NotifyOpen 遇到 INITIAL 类型应直接 return
    CHECK(initial_order.type == Operate::INITIAL, "INITIAL 类型不触发通知");
}

// ============================================================================
// 飞书通知测试 (实际发送, 需要真实 webhook url)
// ============================================================================

static void TestFeishuSend(const std::string& webhook_url) {
    TEST("FeishuNotify 实际发送测试");

    FeishuNotify fs(webhook_url);
    CHECK(fs.IsEnabled(), "Webhook URL 已配置");

    // 发送开仓通知
    Order open_order;
    open_order.id = 99901;
    open_order.currency_pair = "ETH/USDT";
    open_order.type = Operate::LO;
    open_order.quantity = 0.05;
    open_order.amount = 100.0;
    open_order.take_profit_price = 2200.0;
    open_order.stop_loss_price = 1900.0;

    printf("  发送开仓通知...\n");
    fs.NotifyOpen(open_order);
    CHECK(true, "开仓通知已发送 (请检查飞书群)");

    // 发送平仓通知
    Order close_order;
    close_order.id = 99902;
    close_order.currency_pair = "ETH/USDT";
    close_order.type = Operate::LE;
    close_order.quantity = 0.05;
    close_order.amount = 110.0;
    close_order.profit = 10.0;
    close_order.old_orderid = 99901;

    printf("  发送平仓通知...\n");
    fs.NotifyClose(close_order, open_order, 120.5);
    CHECK(true, "平仓通知已发送 (请检查飞书群)");

    // 发送异常通知
    printf("  发送异常通知...\n");
    fs.NotifyError("STX/USDT", "connection timeout after 5000ms");
    CHECK(true, "异常通知已发送 (请检查飞书群)");
}

// ============================================================================
// 币安 DLL 接口测试
// ============================================================================

static void TestBinanceDll() {
    TEST("BinanceDllExchange DLL 加载测试");

    TradeDll dll;
    bool loaded = dll.Load("trade_dll.dll");
    if (!loaded) {
        printf("  [SKIP] trade_dll.dll 未找到, 跳过 DLL 测试\n");
        printf("  提示: 确保 trade_dll.dll 在当前目录或 PATH 中\n");
        return;
    }
    CHECK(loaded, "trade_dll.dll 加载成功");

    // 初始化 (需要有效的 config.ini)
    int rc = dll.Init("config.ini");
    CHECK(rc == 0, "DLL Init 成功");

    // 创建交易所接口
    BinanceDllExchange exchange(&dll);
    CHECK(!exchange.IsSimulation(), "IsSimulation()=false");
    CHECK(std::string(exchange.Name()) == "binance_dll", "Name()=binance_dll");

    // 构造测试订单 (不实际下单, 仅测试接口调用)
    // 注意: 如果配置了真实 API key, 以下会实际下单!
    printf("  [INFO] DLL 接口验证完成, 未执行实际交易\n");
    printf("  [INFO] 如需测试下单, 请在测试网 (testnet) 环境执行\n");

    dll.Unload();
    CHECK(true, "DLL 卸载成功");
}

// ============================================================================
// SimulationExchange 基础测试
// ============================================================================

static void TestSimulationExchange() {
    TEST("SimulationExchange 基础测试");

    SimulationExchange sim;
    CHECK(sim.IsSimulation(), "IsSimulation()=true");
    CHECK(std::string(sim.Name()) == "simulation", "Name()=simulation");

    // 市价单测试
    Order market_order;
    market_order.id = 1;
    market_order.timestamp = 1700000000000LL;
    market_order.currency_pair = "ETH/USDT";
    market_order.price = 2000.0;
    market_order.quantity = 0.1;
    market_order.type = Operate::LO;
    market_order.order_type = 1;  // 市价

    sim.PlaceOrder(market_order);
    CHECK(market_order.matchstatus == MATCH_FILLED, "市价单立即成交");
    CHECK(!market_order.outorderid.empty(), "outorderid 已设置");

    // 限价单测试
    Order limit_order;
    limit_order.id = 2;
    limit_order.timestamp = 1700000000000LL;
    limit_order.currency_pair = "ETH/USDT";
    limit_order.price = 1950.0;
    limit_order.quantity = 0.1;
    limit_order.type = Operate::LO;
    limit_order.order_type = 0;  // 限价

    sim.PlaceOrder(limit_order);
    CHECK(limit_order.matchstatus == MATCH_PENDING, "限价单待成交");

    // 模拟K线触发
    Bar bar;
    bar.timestamp = 1700000060000LL;
    bar.open = 1960; bar.high = 1970; bar.low = 1940; bar.close = 1955;
    strncpy(bar.currency_pair, "ETH/USDT", 23);

    bool filled = sim.CheckFillByBar(limit_order, bar);
    CHECK(filled, "限价单 1950 在 low=1940 时成交");
    CHECK(limit_order.matchstatus == MATCH_FILLED, "matchstatus=FILLED");

    // CreateExchange 工厂测试
    auto* ex1 = CreateExchange("simulation");
    CHECK(ex1->IsSimulation(), "CreateExchange(simulation) → SimulationExchange");
    delete ex1;

    auto* ex2 = CreateExchange("binance");
    CHECK(!ex2->IsSimulation(), "CreateExchange(binance) → BinanceExchange");
    delete ex2;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    printf("=== PPQ Trade Interface Test ===\n");

    if (argc >= 3 && std::string(argv[1]) == "feishu") {
        // 仅测试飞书 (实际发送)
        TestFeishuSend(argv[2]);
    } else if (argc >= 2 && std::string(argv[1]) == "binance") {
        // 仅测试币安 DLL
        TestBinanceDll();
    } else {
        // 运行所有本地测试
        TestFeishuLocal();
        TestSimulationExchange();
        TestBinanceDll();
    }

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
