#pragma once
// exchange_binance_dll.h — 通过 trade_dll.dll 实现币安交易所接口
// 替代原来的 BinanceExchange 桩代码

#include "exchange_interface.h"
#include "dll_loader.h"
#include <cstring>

class BinanceDllExchange : public IExchangeInterface {
public:
    BinanceDllExchange(TradeDll* dll) : dll_(dll) {}

    bool PlaceOrder(Order& order) override {
        TradeOrderRequest req{};
        strncpy(req.symbol, FormatSymbol(order.currency_pair).c_str(), 23);

        // 映射操作类型到Binance side/positionSide
        switch (order.type) {
            case Operate::LO: strcpy(req.side, "BUY");  strcpy(req.position_side, "LONG");  break;
            case Operate::LE: strcpy(req.side, "SELL"); strcpy(req.position_side, "LONG");  break;
            case Operate::SO: strcpy(req.side, "SELL"); strcpy(req.position_side, "SHORT"); break;
            case Operate::SE: strcpy(req.side, "BUY");  strcpy(req.position_side, "SHORT"); break;
            default: return false;
        }

        if (order.order_type == 1) strcpy(req.order_type, "MARKET");
        else                        strcpy(req.order_type, "LIMIT");

        req.price    = order.price;
        req.quantity = order.quantity;
        req.recv_window = 9000;

        TradeOrderResponse resp{};
        int rc = dll_->PlaceOrder(&req, &resp);

        if (rc == 0 && resp.success) {
            order.outorderid = resp.order_id;
            if (std::string(resp.status) == "FILLED") {
                order.matchstatus    = MATCH_FILLED;
                order.matchtimestamp  = order.timestamp;
                if (resp.fill_price > 0) order.price = resp.fill_price;
            }
            return true;
        }
        LOG_E("[BinanceDll] PlaceOrder failed: %s", resp.error_msg);
        return false;
    }

    bool CancelOrder(const std::string& pair, const std::string& outorderid) override {
        TradeOrderResponse resp{};
        int rc = dll_->CancelOrder(FormatSymbol(pair).c_str(),
                                    outorderid.c_str(), &resp);
        return rc == 0 && resp.success;
    }

    bool QueryOrder(Order& order) override {
        TradeOrderResponse resp{};
        int rc = dll_->QueryOrder(FormatSymbol(order.currency_pair).c_str(),
                                   order.outorderid.c_str(), &resp);
        if (rc == 0 && resp.success) {
            if (std::string(resp.status) == "FILLED") {
                order.matchstatus = MATCH_FILLED;
                if (resp.fill_price > 0) order.price = resp.fill_price;
            } else if (std::string(resp.status) == "CANCELED") {
                order.matchstatus = MATCH_CANCELED;
            }
            return true;
        }
        return false;
    }

    bool IsSimulation() const override { return false; }
    const char* Name() const override { return "binance_dll"; }

private:
    TradeDll* dll_;

    static std::string FormatSymbol(const std::string& pair) {
        // "ETH/USDT" -> "ETHUSDT"
        std::string s;
        for (char c : pair) {
            if (c != '/') s += c;
        }
        return s;
    }
};
