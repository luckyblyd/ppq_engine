#pragma once
// trading_engine.h — 交易引擎（统一回测+实盘）
// 对应 Python 的 ppq_service.py + 回测.py + 实盘.py
// 通过 config.ini 的 interface 字段切换模式

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "config_parser.h"
#include "database_interface.h"
#include "database_engine.h"
#include "exchange_interface.h"
#include "trade_executor.h"
#include "indicator_engine.h"
#include "python_bridge.h"
#include "account.h"
#include "logger.h"
#include "platform.h"
#include "dll_loader.h"
#include "shm_ipc.h"
#include "market_data_types.h"
#include "order_shm.h"
#include "framework.h"
#include "feishu_notify.h"
#include "exchange_binance_dll.h"

// ============================================================================
// TradingEngine — 量化交易引擎主体
// ============================================================================

class TradingEngine {
public:
    TradingEngine() = default;
    ~TradingEngine() {
        delete exchange_;
        delete db_;
        delete trade_dll_;
        if (report_dll_loaded_) report_dll_.Unload();
    }

    // ---------- 初始化 ----------
    bool Init(const std::string& config_path) {
        // 1. 加载配置
        if (!config_.Load(config_path)) {
            LOG_E("[Engine] Failed to load config: %s", config_path.c_str());
            return false;
        }

        // 2. 读取配置项
        interface_    = config_.Get("system", "interface", "simulation");
        mode_         = config_.Get("system", "mode", "backtest");
        timeframe_    = config_.Get("market", "timeframe", "5m");
        auto pairs_str = config_.Get("market", "currency_pairs", "");
        ParsePairs(pairs_str);

        initial_capital_ = config_.GetDouble("account", "initial_capital", 10000.0);
        backtest_num_    = config_.GetInt("backtest", "limit", 8640);
        start_date_      = config_.Get("backtest", "start_date", "");
        strategy_name_   = config_.Get("strategy", "module", "ElderRayStrategy_clean");
        script_dir_      = config_.Get("strategy", "script_dir", ".");

        // 读取报告配置
        html_report_enabled_ = config_.GetBool("report", "html_report", true);
        report_dir_          = config_.Get("report", "report_dir",
                                           "\xE7\xAD\x96\xE7\x95\xA5\xE7\xA0\x94\xE7\xA9\xB6");

        LOG_I("[Engine] interface=%s timeframe=%s pairs=%d capital=%.0f strategy=%s report=%s",
              interface_.c_str(), timeframe_.c_str(), (int)pairs_.size(),
              initial_capital_, strategy_name_.c_str(),
              html_report_enabled_ ? "ON" : "OFF");

        // 加载 HTML 报告 DLL（如果开启）
        if (html_report_enabled_) {
            std::string report_dll_path = config_.Get("dll", "html_report_dll",
                                                       "html_report_dll.dll");
            if (report_dll_.Load(report_dll_path)) {
                report_dll_.Init();
                report_dll_loaded_ = true;
                LOG_I("[Engine] HTML Report DLL loaded: %s", report_dll_path.c_str());
            } else {
                LOG_W("[Engine] HTML Report DLL load failed, report disabled");
                html_report_enabled_ = false;
            }
        }

        // 加载 CZSC DLL（可选）
        {
            std::string czsc_path = config_.Get("dll", "czsc_dll", "czsc_dll.dll");
            CzscDll czsc_dll;
            if (czsc_dll.Load(czsc_path)) {
                czsc_dll.Init(nullptr, script_dir_.c_str(), timeframe_.c_str());
                LOG_I("[Engine] CZSC DLL loaded: %s", czsc_path.c_str());
            }
        }

        // 3. 创建交易所接口
        if (interface_ == "binance") {
            // 实盘: 尝试加载 trade_dll
            std::string trade_path = config_.Get("dll", "trade_dll", "trade_dll.dll");
            trade_dll_ = new TradeDll();
            if (trade_dll_->Load(trade_path)) {
                trade_dll_->Init(config_path.c_str());
                exchange_ = new BinanceDllExchange(trade_dll_);
                LOG_I("[Engine] Binance DLL exchange loaded: %s", trade_path.c_str());
            } else {
                LOG_W("[Engine] trade_dll load failed, using stub BinanceExchange");
                delete trade_dll_;
                trade_dll_ = nullptr;
                exchange_ = CreateExchange(interface_);
            }
        } else {
            exchange_ = CreateExchange(interface_);
        }
        LOG_I("[Engine] Exchange: %s", exchange_->Name());

        // 4. 连接数据库 (通过 DatabaseEngine 工厂, 支持 sqlite3/postgresql 切换)
        auto db_ptr = DatabaseEngine::Create(config_);
        if (!db_ptr) {
            LOG_E("[Engine] Database connection failed");
            return false;
        }
        db_ = db_ptr.release();

        // 5. 初始化 Python 策略桥接
        if (!python_.Init(script_dir_)) {
            LOG_E("[Engine] Python bridge init failed");
            return false;
        }
        if (!python_.LoadStrategy(strategy_name_)) {
            LOG_E("[Engine] Failed to load strategy: %s", strategy_name_.c_str());
            return false;
        }

        // 6. 初始化飞书通知 (仅实盘模式) 
        if (interface_ != "simulation" ||  mode_ == "live" ) {
            std::string feishu_url = config_.Get("feishu", "webhook_url", "");
            if (!feishu_url.empty()) {
                feishu_.SetUrl(feishu_url);
                LOG_I("[Engine] Feishu notify enabled");
            }
        }

        return true;
    }

    // ---------- 运行回测 ----------
    void RunBacktest() {
        LOG_I("[Engine] === BACKTEST START ===");

        for (auto& pair : pairs_) {
            LOG_I("[Engine] Backtesting: %s", pair.c_str());

            Account account;
            account.InitForBacktest(initial_capital_, pair);

            // 加载交易参数
            TradingParameters params = db_->QueryParameters(pair);
            if (params.currency_pair.empty()) {
                params.currency_pair     = pair;
                params.take_profit_ratio = 1.09;
                params.stop_loss_ratio   = 1.05;
                params.czzy_signal_count = 100.0;
            }

            // 加载历史K线
            auto bars = db_->QueryLatestBars(pair, timeframe_, backtest_num_);
            if (bars.size() < 1300) {
                LOG_W("[Engine] Not enough bars for %s: %d", pair.c_str(), (int)bars.size());
                continue;
            }

            // 创建交易执行器
            TradeExecutor executor(exchange_, timeframe_, initial_capital_);

            LOG_I("[Engine] Loaded %d bars, running strategy...", (int)bars.size());

            // 逐根K线回测
            int start_idx = 1300; // 前1300根用于指标预热
            for (int i = start_idx; i < (int)bars.size(); ++i) {
                // 切片当前可见的K线
                std::vector<Bar> visible(bars.begin(), bars.begin() + i + 1);

                // 计算指标
                IndicatorCache indicators;
                indicators.Compute(visible);

                // 检查挂单成交
                executor.CheckTriggeredOrders(pair, bars[i], account);

                // 调用 Python 策略
                auto signals = python_.GenerateSignal(
                    pair, visible, indicators, account, params);

                // 执行信号
                for (auto& sig : signals) {
                    executor.ExecuteTrade(pair, bars[i], sig, account, params);
                }
            }
            // 回测结束后，平掉所有持仓
            const Bar& last_bar = bars.back();
            auto filled_longs = account.GetFilledLongs();
            for (auto* ol : filled_longs) {
                SignalData close_sig(0, Operate::LE, 1, ol->quantity,
                                    ol->id, "回测结束强制平仓",
                                    last_bar.close, 0, 0);
                executor.ExecuteTrade(pair, last_bar, close_sig, account, params);
            }
            auto filled_shorts = account.GetFilledShorts();
            for (auto* os : filled_shorts) {
                SignalData close_sig(0, Operate::SE, 1, os->quantity,
                                    os->id, "回测结束强制平仓",
                                    last_bar.close, 0, 0);
                executor.ExecuteTrade(pair, last_bar, close_sig, account, params);
            }

            // 输出回测结果
            PrintBacktestResult(pair, account);

            // 生成 HTML 报告
            if (html_report_enabled_ && report_dll_loaded_) {
                GenerateHtmlReport(pair, account, bars);
            }
        }

        LOG_I("[Engine] === BACKTEST END ===");
    }

    // ---------- 运行实盘 ----------
    void RunLive() {
        LOG_I("[Engine] === LIVE TRADING START ===");
        LOG_I("[Engine] Interface: %s", exchange_->Name());

        // 1. 打开共享内存
        ShmRegion* pub_region = ShmOpen("/binance_pubsub");
        MarketShmRegion* market_shm = MarketShmOpen();
        if (!pub_region || !market_shm) {
            LOG_E("[Engine] Failed to open shared memory, falling back to DB polling");
            //RunLiveFallback();
            return;
        }

        // 等待 server ready
        int wait_count = 0;
        while (!pub_region->server_ready.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (++wait_count > 60) {
                LOG_E("[Engine] Shared memory server not ready after 30s, falling back");
                //RunLiveFallback();
                return;
            }
        }
        LOG_I("[Engine] Shared memory connected");

        // 2. 分配 client 通道
        int client_id = pub_region->next_client_id.fetch_add(1, std::memory_order_acq_rel);
        if (client_id >= ShmRegion::kMaxClients) {
            LOG_E("[Engine] No available client slot (max=%d)", ShmRegion::kMaxClients);
            //RunLiveFallback();
            return;
        }
        pub_region->channels[client_id].client_connected.store(true, std::memory_order_release);

        // 3. 创建 pubsub client 并订阅
        ShmPubsubClient pubsub_client(pub_region->channels[client_id]);

        // 回调: 收到K线消息时触发策略处理
        struct LiveSubscriber : public ISubscriberCB {
            TradingEngine* engine;
            MarketShmRegion* mshm;
            std::mutex mtx;
            std::condition_variable cv;
            bool new_bar = false;
            std::string last_pair;

            void OnMessage(const std::string& topic, const char*, int) override {
                // topic 格式: "kline::ETH/USDT::5m"
                auto pos1 = topic.find("::");
                auto pos2 = topic.rfind("::");
                if (pos1 != std::string::npos && pos2 != pos1) {
                    std::lock_guard<std::mutex> lk(mtx);
                    last_pair = topic.substr(pos1 + 2, pos2 - pos1 - 2);
                    new_bar = true;
                    cv.notify_one();
                }
            }
            void OnSubscribeSuccess(const std::string& topic) override {
                LOG_I("[Engine] Subscribed: %s", topic.c_str());
            }
        };

        LiveSubscriber subscriber;
        subscriber.engine = this;
        subscriber.mshm = market_shm;

        for (auto& pair : pairs_) {
            std::string topic = "kline::" + pair + "::" + timeframe_;
            pubsub_client.Subscribe(topic, subscriber);
        }

        // 4. 为每个交易对创建账户
        std::map<std::string, Account> accounts;
        std::map<std::string, TradingParameters> params_map;

        for (auto& pair : pairs_) {
            accounts[pair].InitForTrade(initial_capital_, pair);
            params_map[pair] = db_->QueryParameters(pair);

            auto active_orders = db_->QueryActiveOrders(pair);
            for (auto& o : active_orders) {
                accounts[pair].AddOrder(o);
            }
            LOG_I("[Engine] %s: restored %d active orders",
                  pair.c_str(), (int)active_orders.size());
        }

        TradeExecutor executor(exchange_, timeframe_, initial_capital_);

        // 5. 创建委托共享内存 (供 web_server 查询)
        OrderShmRegion* order_shm = OrderShmCreate();
        if (order_shm) {
            order_shm->ready.store(true, std::memory_order_release);
            LOG_I("[Engine] Order shared memory created");
        }

        // 6. 创建事件循环
        SystemClock sys_clock;
        EventCore event_core(sys_clock);
        event_core.RegisterEvent(pubsub_client);

        // 7. 主循环: 事件驱动 + 消息处理
        LOG_I("[Engine] Entering live event loop...");
        while (!live_shutdown_) {
            // 轮询共享内存消息
            pubsub_client.Poll();

            // 检查是否有新K线
            std::string pair_to_process;
            {
                std::lock_guard<std::mutex> lk(subscriber.mtx);
                if (subscriber.new_bar) {
                    pair_to_process = subscriber.last_pair;
                    subscriber.new_bar = false;
                }
            }

            if (!pair_to_process.empty()) {
                ProcessLiveBar(pair_to_process, market_shm, accounts, params_map, executor);

                // 同步委托和资金到共享内存
                if (order_shm) {
                    SyncOrdersToShm(accounts, order_shm);
                }
            }

            // 处理 web_server 发来的删除请求
            if (order_shm) {
                int del_ids[32];
                int del_count = order_shm->DrainDeletes(del_ids, 32);
                for (int i = 0; i < del_count; ++i) {
                    for (auto& pair : pairs_) {
                        auto& account = accounts[pair];
                        Order* o = account.FindOrder(del_ids[i]);
                        if (o) {
                            o->matchstatus = MATCH_CANCELED;
                            o->status = "CANCELED";
                            if (!exchange_->IsSimulation())
                                exchange_->CancelOrder(pair, o->outorderid);
                            account.RemoveOrder(del_ids[i]);
                            db_->UpdateOrderStatus(del_ids[i], "CANCELED", MATCH_CANCELED);
                            LOG_I("[Engine] Order %d canceled by web request", del_ids[i]);
                            break;
                        }
                    }
                }
            }

            plat_usleep(1000); // 1ms 轮询间隔
        }

        // 清理
        pub_region->channels[client_id].client_connected.store(false, std::memory_order_release);
        if (order_shm) OrderShmDestroy(order_shm);
        LOG_I("[Engine] === LIVE TRADING END ===");
    }

    void StopLive() { live_shutdown_ = true; }

    std::string mode_;
private:
    ConfigParser config_;
    IExchangeInterface* exchange_ = nullptr;
    IDatabase* db_ = nullptr;
    PythonBridge python_;
    HtmlReportDll report_dll_;
    bool report_dll_loaded_ = false;
    bool html_report_enabled_ = true;
    std::string report_dir_;

    std::string interface_;
    std::string timeframe_;
    std::vector<std::string> pairs_;
    double initial_capital_ = 10000;
    int backtest_num_ = 8640;
    std::string start_date_;
    std::string strategy_name_;
    std::string script_dir_;
    bool live_shutdown_ = false;
    FeishuNotify feishu_;
    TradeDll* trade_dll_ = nullptr;

    // ---------- 同步委托/资金到共享内存 ----------
    void SyncOrdersToShm(std::map<std::string, Account>& accounts,
                         OrderShmRegion* order_shm) {
        // 收集所有活跃委托
        std::vector<OrderShmEntry> entries;
        std::vector<AccountShmEntry> acct_entries;

        for (auto& [pair, account] : accounts) {
            for (auto& o : account.Orders()) {
                OrderShmEntry e{};
                e.id = o.id;
                e.timestamp = o.timestamp;
                strncpy(e.currency_pair, o.currency_pair.c_str(), 23);
                e.price = o.price;
                e.quantity = o.quantity;
                e.amount = o.amount;
                e.type = static_cast<int>(o.type);
                e.order_type = o.order_type;
                e.matchstatus = o.matchstatus;
                e.take_profit_price = o.take_profit_price;
                e.stop_loss_price = o.stop_loss_price;
                e.profit = o.profit;
                e.old_orderid = o.old_orderid;
                strncpy(e.remark, o.remark.c_str(), 63);
                strncpy(e.status, o.status.c_str(), 15);
                entries.push_back(e);
            }

            AccountShmEntry ae{};
            strncpy(ae.currency_pair, pair.c_str(), 23);
            ae.current_capital = account.CurrentCapital();
            ae.occupied_capital = account.OccupiedCapital();
            ae.active_orders = (int)account.Orders().size();
            ae.filled_longs = (int)account.GetFilledLongs().size();
            ae.filled_shorts = (int)account.GetFilledShorts().size();
            acct_entries.push_back(ae);
        }

        order_shm->SyncOrders(entries.data(), (int)entries.size());
        order_shm->SyncAccounts(acct_entries.data(), (int)acct_entries.size());
    }

    // ---------- KlineBar → Bar 转换 ----------
    static Bar KlineBarToBar(const KlineBar& kb) {
        Bar b{};
        b.timestamp = kb.timestamp;
        strncpy(b.timeframe, kb.timeframe, 7);
        strncpy(b.currency_pair, kb.currency_pair, 23);
        b.open = kb.open; b.high = kb.high;
        b.low = kb.low; b.close = kb.close;
        b.volume = kb.volume; b.quote_volume = kb.quote_volume;
        b.change_pct = kb.change_pct;
        b.flags = kb.flags;
        return b;
    }

    // ---------- 飞书通知: 检查归档变化并发送通知 ----------
    void NotifyClosedOrders(const std::string& pair, Account& account,
                            size_t archive_before) {
        if (!feishu_.IsEnabled()) return;
        auto& archive = account.Archive();
        for (size_t i = archive_before; i < archive.size(); ++i) {
            auto& o = archive[i];
            if (!IsCloseOrder(o.type)) continue;
            // 找到对应开仓单 (也在归档中)
            const Order* open_order = nullptr;
            for (auto& ao : archive) {
                if (ao.id == o.old_orderid) { open_order = &ao; break; }
            }
            if (!open_order) continue;
            int64_t open_ts  = open_order->matchtimestamp > 0 ? open_order->matchtimestamp : open_order->timestamp;
            int64_t close_ts = o.matchtimestamp > 0 ? o.matchtimestamp : o.timestamp;
            double duration_min = (close_ts - open_ts) / 60000.0;
            feishu_.NotifyClose(o, *open_order, duration_min);
        }
    }

    void NotifyOpenOrders(const std::string& pair, Account& account,
                          size_t orders_before) {
        if (!feishu_.IsEnabled()) return;
        auto& orders = account.Orders();
        // 新增的开仓单 (orders list 可能重排, 用 size 差值)
        for (size_t i = orders_before; i < orders.size(); ++i) {
            auto& o = orders[i];
            if (IsOpenOrder(o.type) && o.matchstatus == MATCH_FILLED) {
                feishu_.NotifyOpen(o);
            }
        }
    }

    // ---------- 实盘: 处理单个交易对的新K线 ----------
    void ProcessLiveBar(const std::string& pair,
                        MarketShmRegion* market_shm,
                        std::map<std::string, Account>& accounts,
                        std::map<std::string, TradingParameters>& params_map,
                        TradeExecutor& executor) {
        auto& account = accounts[pair];
        auto& params  = params_map[pair];

        try {
            // 从共享内存读取K线
            int sym_idx = market_shm->FindOrAdd(pair.c_str(), timeframe_.c_str());
            if (sym_idx < 0) {
                LOG_E("[Engine] Symbol %s not found in shared memory", pair.c_str());
                return;
            }

            std::vector<KlineBar> kbuf(1500);
            uint32_t got = market_shm->rings[sym_idx].ReadLatest(kbuf.data(), 1500);
            if (got < 1300) {
                LOG_W("[Engine] Not enough bars from SHM for %s: %u", pair.c_str(), got);
                return;
            }

            // 转换 KlineBar → Bar
            std::vector<Bar> bars(got);
            for (uint32_t i = 0; i < got; ++i) {
                bars[i] = KlineBarToBar(kbuf[i]);
            }

            // 计算指标
            IndicatorCache indicators;
            indicators.Compute(bars);

            const Bar& latest = bars.back();

            // 检查挂单 (止盈止损触发)
            size_t archive_before = account.Archive().size();
            executor.CheckTriggeredOrders(pair, latest, account);
            NotifyClosedOrders(pair, account, archive_before);

            // 去重：同一根K线只处理一次
            int64_t last_ts = db_->QueryLastExecution(pair);
            if (latest.timestamp <= last_ts) return;

            // 调用策略
            auto signals = python_.GenerateSignal(
                pair, bars, indicators, account, params);

            // 执行信号
            for (auto& sig : signals) {
                size_t ord_before = account.Orders().size();
                size_t arc_before = account.Archive().size();

                executor.ExecuteTrade(pair, latest, sig, account, params);

                NotifyOpenOrders(pair, account, ord_before);
            }

            // 更新执行记录
            if (last_ts < 0)
                db_->InsertExecution(pair, latest.timestamp);
            else
                db_->UpdateExecution(pair, latest.timestamp);

            LOG_I("[Engine] Processed live bar: %s ts=%lld close=%.4f",
                  pair.c_str(), (long long)latest.timestamp, latest.close);
        } catch (const std::exception& ex) {
            LOG_E("[Engine] Exception processing %s: %s", pair.c_str(), ex.what());
            feishu_.NotifyError(pair, ex.what());
        } catch (...) {
            LOG_E("[Engine] Unknown exception processing %s", pair.c_str());
            feishu_.NotifyError(pair, "unknown exception");
        }
    }

    /*/ ---------- 暂时不用 实盘降级: 从数据库轮询 (共享内存不可用时) ----------
    void RunLiveFallback() {
        LOG_W("[Engine] Running in DB-polling fallback mode");

        std::map<std::string, Account> accounts;
        std::map<std::string, TradingParameters> params_map;

        for (auto& pair : pairs_) {
            accounts[pair].InitForTrade(initial_capital_, pair);
            params_map[pair] = db_->QueryParameters(pair);
            auto active_orders = db_->QueryActiveOrders(pair);
            for (auto& o : active_orders) {
                accounts[pair].AddOrder(o);
            }
        }

        TradeExecutor executor(exchange_, timeframe_, initial_capital_);
        int tf_seconds = ParseTimeframeMinutes(timeframe_) * 60;

        while (!live_shutdown_) {
            for (auto& pair : pairs_) {
                auto& account = accounts[pair];
                auto& params  = params_map[pair];

                try {
                    auto bars = db_->QueryLatestBars(pair, timeframe_, 1500);
                    if (bars.size() < 1300) continue;

                    IndicatorCache indicators;
                    indicators.Compute(bars);

                    const Bar& latest = bars.back();

                    size_t archive_before = account.Archive().size();
                    executor.CheckTriggeredOrders(pair, latest, account);
                    NotifyClosedOrders(pair, account, archive_before);

                    int64_t last_ts = db_->QueryLastExecution(pair);
                    if (latest.timestamp <= last_ts) continue;

                    auto signals = python_.GenerateSignal(
                        pair, bars, indicators, account, params);

                    for (auto& sig : signals) {
                        size_t ord_before = account.Orders().size();
                        size_t arc_before = account.Archive().size();

                        executor.ExecuteTrade(pair, latest, sig, account, params);

                        NotifyOpenOrders(pair, account, ord_before);
                        NotifyClosedOrders(pair, account, arc_before);
                    }

                    if (last_ts < 0)
                        db_->InsertExecution(pair, latest.timestamp);
                    else
                        db_->UpdateExecution(pair, latest.timestamp);
                } catch (const std::exception& ex) {
                    LOG_E("[Engine] Exception processing %s: %s", pair.c_str(), ex.what());
                    feishu_.NotifyError(pair, ex.what());
                } catch (...) {
                    LOG_E("[Engine] Unknown exception processing %s", pair.c_str());
                    feishu_.NotifyError(pair, "unknown exception");
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(tf_seconds));
        }
    }*/

    // ---------- 辅助方法 ----------
    void ParsePairs(const std::string& s) {
        pairs_.clear();
        std::string buf;
        for (char c : s) {
            if (c == ',' || c == ';') {
                auto t = Trim(buf);
                if (!t.empty()) pairs_.push_back(t);
                buf.clear();
            } else {
                buf += c;
            }
        }
        auto t = Trim(buf);
        if (!t.empty()) pairs_.push_back(t);
    }

    static std::string Trim(const std::string& s) {
        auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    void PrintBacktestResult(const std::string& pair, const Account& account) {
        auto& perf = account.Perf();
        double final_capital = account.CurrentCapital();
        double returns = (final_capital - initial_capital_) / initial_capital_ * 100;

        LOG_I("========== %s 回测结果 ==========", pair.c_str());
        LOG_I("  初始资金:    %.2f", initial_capital_);
        LOG_I("  最终资金:    %.2f", final_capital);
        LOG_I("  收益率:      %.2f%%", returns);
        LOG_I("  总交易次数:  %d", perf.total_trades);
        LOG_I("  盈利次数:    %d", perf.winning_trades);
        LOG_I("  亏损次数:    %d", perf.losing_trades);
        LOG_I("  胜率:        %.2f%%", perf.win_rate * 100);
        LOG_I("  盈亏比:      %.2f", perf.profit_factor);
        LOG_I("  总盈利:      %.2f", perf.total_profit);
        LOG_I("  总亏损:      %.2f", perf.total_loss);
        LOG_I("==========================================");
    }

    // ---------- HTML 报告生成 ----------
    void GenerateHtmlReport(const std::string& pair,
                            const Account& account,
                            const std::vector<Bar>& bars) {
        if (!report_dll_loaded_ || bars.empty()) return;

        auto& perf = account.Perf();
        double final_capital = account.CurrentCapital();

        // 1. 构建 ReportConfig
        ReportConfig cfg = {};
        strncpy(cfg.currency_pair, pair.c_str(), sizeof(cfg.currency_pair) - 1);
        strncpy(cfg.timeframe, timeframe_.c_str(), sizeof(cfg.timeframe) - 1);
        strncpy(cfg.output_dir, report_dir_.c_str(), sizeof(cfg.output_dir) - 1);
        strncpy(cfg.strategy_name, strategy_name_.c_str(), sizeof(cfg.strategy_name) - 1);
        cfg.backtest_start_ts = bars.front().timestamp;
        cfg.backtest_end_ts   = bars.back().timestamp;

        // 2. 转换 K线数据
        std::vector<ReportBar> report_bars(bars.size());
        for (size_t i = 0; i < bars.size(); ++i) {
            auto& b = bars[i];
            auto& rb = report_bars[i];
            rb.timestamp    = b.timestamp;
            rb.open         = b.open;
            rb.high         = b.high;
            rb.low          = b.low;
            rb.close        = b.close;
            rb.volume       = b.volume;
            rb.quote_volume = b.quote_volume;
            rb.change_pct   = b.change_pct;
            strncpy(rb.currency_pair, pair.c_str(), sizeof(rb.currency_pair) - 1);
            strncpy(rb.timeframe, timeframe_.c_str(), sizeof(rb.timeframe) - 1);
        }

        // 3. 转换订单数据（活跃 + 归档）
        std::vector<ReportOrder> report_orders;
        auto convert_order = [](const Order& o) -> ReportOrder {
            ReportOrder ro = {};
            ro.id               = o.id;
            ro.timestamp        = o.timestamp;
            strncpy(ro.currency_pair, o.currency_pair.c_str(), sizeof(ro.currency_pair) - 1);
            ro.price            = o.price;
            ro.quantity         = o.quantity;
            ro.amount           = o.amount;
            strncpy(ro.type, OperateToStr(o.type), sizeof(ro.type) - 1);
            // 将中文操作类型映射为英文缩写
            if (o.type == Operate::LO) strncpy(ro.type, "LO", sizeof(ro.type));
            else if (o.type == Operate::LE) strncpy(ro.type, "LE", sizeof(ro.type));
            else if (o.type == Operate::SO) strncpy(ro.type, "SO", sizeof(ro.type));
            else if (o.type == Operate::SE) strncpy(ro.type, "SE", sizeof(ro.type));
            ro.order_type       = o.order_type;
            ro.take_profit_price = o.take_profit_price;
            ro.stop_loss_price  = o.stop_loss_price;
            ro.matchstatus      = o.matchstatus;
            ro.matchtimestamp   = o.matchtimestamp;
            ro.profit           = o.profit;
            ro.old_orderid      = o.old_orderid;
            strncpy(ro.remark, o.remark.c_str(), sizeof(ro.remark) - 1);
            return ro;
        };

        for (auto& o : account.Orders()) {
            report_orders.push_back(convert_order(o));
        }
        for (auto& o : account.Archive()) {
            report_orders.push_back(convert_order(o));
        }

        // 4. 构建性能指标
        ReportPerformance rp = {};
        rp.total_trades       = perf.total_trades;
        rp.winning_trades     = perf.winning_trades;
        rp.losing_trades      = perf.losing_trades;
        rp.win_rate           = perf.win_rate;
        rp.profit_factor      = perf.profit_factor;
        rp.total_profit       = perf.total_profit;
        rp.total_loss         = perf.total_loss;
        rp.net_profit         = perf.total_profit - perf.total_loss;
        rp.initial_capital    = initial_capital_;
        rp.final_capital      = final_capital;
        rp.total_return       = (final_capital - initial_capital_) / initial_capital_;

        // 计算币种涨跌
        if (!bars.empty() && bars.front().open > 0) {
            rp.price_change_pct = (bars.back().close - bars.front().open)
                                  / bars.front().open * 100.0;
        }

        // 估算手续费（总交易额 / 1000）
        double total_volume = 0;
        for (auto& ro : report_orders) {
            total_volume += ro.amount;
        }
        rp.total_commission = total_volume / 1000.0;

        // 5. 调用 DLL 生成报告
        char out_path[512] = {};
        int ret = report_dll_.Generate(
            &cfg,
            report_bars.data(), (int)report_bars.size(),
            report_orders.data(), (int)report_orders.size(),
            &rp, out_path, sizeof(out_path));

        if (ret == 0) {
            LOG_I("[Engine] HTML report generated: %s", out_path);
        } else {
            LOG_E("[Engine] HTML report generation failed for %s (ret=%d, dir=%s, bars=%d, orders=%d)",
                  pair.c_str(), ret, report_dir_.c_str(),
                  (int)report_bars.size(), (int)report_orders.size());
        }
    }
};
