#pragma once
// feishu_notify.h — 飞书 Webhook 通知模块
//
// 通过 libcurl POST JSON 到飞书 Webhook 发送消息
// 仅在实盘 (live) 模式下使用, 回测 (backtest/simulation) 不发送
//
// 消息格式:
//   开单: ME/USDT|开多|13335|0.1022U|50U|止盈0.104653|止损0.09463
//   平仓: ME/USDT|平多|13365|原委托id13335|0.0966U|47.2374U|持续时间1510.0分钟|盈亏-2.7384
//   报错: 处理货币对 STX/USDT 异常:XXX

#include <string>
#include <cstdio>
#include <curl/curl.h>
#include "order.h"
#include "logger.h"

class FeishuNotify {
public:
    FeishuNotify() = default;

    explicit FeishuNotify(const std::string& webhook_url)
        : url_(webhook_url) {}

    void SetUrl(const std::string& url) { url_ = url; }
    bool IsEnabled() const { return !url_.empty(); }

    // 开单通知
    // 格式: ME/USDT|开多|13335|0.1022U|50U|止盈0.104653|止损0.09463
    void NotifyOpen(const Order& order) {
        if (!IsEnabled()) return;

        const char* op_str = "";
        switch (order.type) {
            case Operate::LO: op_str = "开多"; break;
            case Operate::SO: op_str = "开空"; break;
            default: return; // 非开仓不通知
        }

        char msg[512];
        snprintf(msg, sizeof(msg),
            "%s|%s|%d|%.4fU|%.4fU|止盈%.6f|止损%.6f",
            order.currency_pair.c_str(),
            op_str,
            order.id,
            order.quantity,
            order.amount,
            order.take_profit_price,
            order.stop_loss_price);

        SendText(msg);
    }

    // 平仓通知
    // 格式: ME/USDT|平多|13365|原委托id13335|0.0966U|47.2374U|持续时间1510.0分钟|盈亏-2.7384
    void NotifyClose(const Order& close_order, const Order& open_order,
                     double duration_minutes) {
        if (!IsEnabled()) return;

        const char* op_str = "";
        switch (close_order.type) {
            case Operate::LE: op_str = "平多"; break;
            case Operate::SE: op_str = "平空"; break;
            default: return;
        }

        char msg[512];
        snprintf(msg, sizeof(msg),
            "%s|%s|%d|原委托id%d|%.4fU|%.4fU|持续时间%.1f分钟|盈亏%.4f",
            close_order.currency_pair.c_str(),
            op_str,
            close_order.id,
            open_order.id,
            close_order.quantity,
            close_order.amount,
            duration_minutes,
            close_order.profit);

        SendText(msg);
    }

    // 异常通知
    // 格式: 处理货币对 STX/USDT 异常:XXX
    void NotifyError(const std::string& pair, const std::string& error) {
        if (!IsEnabled()) return;

        char msg[512];
        snprintf(msg, sizeof(msg),
            "处理货币对 %s 异常:%s",
            pair.c_str(), error.c_str());

        SendText(msg);
    }

private:
    std::string url_;

    // 发送文本消息到飞书 Webhook
    void SendText(const char* text) {
        // 构造飞书消息 JSON
        // 飞书 Webhook 格式: {"msg_type":"text","content":{"text":"xxx"}}
        char json_body[1024];
        // 转义文本中的特殊字符
        std::string escaped = EscapeJson(text);
        snprintf(json_body, sizeof(json_body),
            R"({"msg_type":"text","content":{"text":"%s"}})",
            escaped.c_str());

        CURL* curl = curl_easy_init();
        if (!curl) {
            LOG_E("[Feishu] curl_easy_init failed");
            return;
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            LOG_E("[Feishu] send failed: %s", curl_easy_strerror(rc));
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    static std::string EscapeJson(const char* s) {
        std::string out;
        for (; *s; ++s) {
            switch (*s) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += *s;     break;
            }
        }
        return out;
    }
};
