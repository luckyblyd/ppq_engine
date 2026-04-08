#pragma once
// kline_fetcher.h — 币安 REST API K线拉取器 (libcurl)
//
// 功能:
//   - FetchLatest(): 拉取最新1根完成的K线
//   - FetchHistorical(): 拉取指定时间段历史K线 (网页补漏用)
//   - 内置代理支持、超时控制 (500ms)

#include <curl/curl.h>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include "../third_party/json.hpp"
#include "market_data_types.h"
#include "logger.h"

using json = nlohmann::json;

class KlineFetcher {
public:
    // proxy / base_url 必须由调用方从配置文件传入
    KlineFetcher(const std::string& base_url = "https://fapi.binance.com",
                 const std::string& proxy = "")
        : base_url_(base_url), proxy_(proxy)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~KlineFetcher() { curl_global_cleanup(); }

    // "ETH/USDT" → "ETHUSDT"
    static std::string ToSymbol(const std::string& pair) {
        std::string s;
        for (char c : pair) if (c != '/') s += c;
        return s;
    }

    // 拉取最新 limit 根K线 (包含当前未完成的)
    // 返回按时间升序排列, 倒数第2根是最新已完成K线
    std::vector<KlineBar> FetchLatest(const std::string& pair,
                                      const std::string& interval, int limit = 2) {
        std::string url = base_url_ + "/fapi/v1/klines?symbol=" + ToSymbol(pair)
                        + "&interval=" + interval + "&limit=" + std::to_string(limit);
        std::string body;
        if (!HttpGet(url, body)) return {};
        return ParseKlines(body, pair, interval);
    }

    // 拉取历史K线 (自动分页, 每次1000根)
    std::vector<KlineBar> FetchHistorical(const std::string& pair,
                                          const std::string& interval,
                                          int64_t start_ms, int64_t end_ms) {
        std::vector<KlineBar> all;
        int64_t cursor = start_ms;
        while (cursor < end_ms) {
            std::string url = base_url_ + "/fapi/v1/klines?symbol=" + ToSymbol(pair)
                + "&interval=" + interval
                + "&startTime=" + std::to_string(cursor)
                + "&endTime=" + std::to_string(end_ms)
                + "&limit=1000";
            std::string body;
            if (!HttpGet(url, body)) break;
            auto bars = ParseKlines(body, pair, interval);
            if (bars.empty()) break;
            for (auto& b : bars) {
                all.push_back(b);
                cursor = b.timestamp + 1;  // 下一根起点
            }
            if (bars.size() < 1000) break;  // 已到末尾
            plat_usleep(200000);  // 限流: 200ms 间隔
        }
        // 去掉最新未完成的K线 (如果最后一根属于当前正在形成的K线)
        if (!all.empty()) {
            int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t intv = (interval == "5m") ? 300000LL : 60000LL;
            int64_t current_candle_start = (now_ms / intv) * intv;
            if (all.back().timestamp >= current_candle_start) {
                LOG_E("[FetchHistorical] stripping incomplete bar ts=%lld",
                      (long long)all.back().timestamp);
                all.pop_back();
            }
        }
        return all;
    }

private:
    // 解析币安 klines JSON 响应
    // 格式: [[open_time, open, high, low, close, volume, close_time,
    //          quote_vol, trades, taker_buy_base, taker_buy_quote, ignore], ...]
    std::vector<KlineBar> ParseKlines(const std::string& body,
                                      const std::string& pair,
                                      const std::string& interval) {
        std::vector<KlineBar> res;
        try {
            auto arr = json::parse(body);
            for (auto& k : arr) {
                KlineBar b{};
                b.timestamp    = k[0].get<int64_t>();
                b.open         = std::stod(k[1].get<std::string>());
                b.high         = std::stod(k[2].get<std::string>());
                b.low          = std::stod(k[3].get<std::string>());
                b.close        = std::stod(k[4].get<std::string>());
                b.volume       = std::stod(k[5].get<std::string>());
                b.quote_volume = std::stod(k[7].get<std::string>());
                if (b.open > 1e-9)
                    b.change_pct = (b.close - b.open) / b.open * 100.0;
                strncpy(b.currency_pair, pair.c_str(), sizeof(b.currency_pair) - 1);
                strncpy(b.timeframe, interval.c_str(), sizeof(b.timeframe) - 1);
                b.flags = 0;
                res.push_back(b);
            }
        } catch (const std::exception& e) {
            LOG_E("JSON parse: %s", e.what());
        }
        return res;
    }

    static size_t WriteCB(void* p, size_t sz, size_t n, std::string* s) {
        s->append((char*)p, sz * n);
        return sz * n;
    }

    bool HttpGet(const std::string& url, std::string& out) {
        CURL* c = curl_easy_init();
        if (!c) { LOG_E("curl_easy_init failed"); return false; }
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCB);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 10000L);       // 10s 总超时
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10s 连接超时
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        if (!proxy_.empty())
            curl_easy_setopt(c, CURLOPT_PROXY, proxy_.c_str());
        CURLcode rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK) {
            LOG_E("curl: %s url=%s", curl_easy_strerror(rc), url.c_str());
            return false;
        }
        return true;
    }

    std::string base_url_;
    std::string proxy_;
};
