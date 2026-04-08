#pragma once
// enums.h — 交易枚举和信号数据结构
// 对应 Python 的 core/enums.py

#include <string>
#include <cstdint>

// ============================================================================
// 操作类型枚举
// ============================================================================

enum class Operate {
    LO,       // 开多 Long Open
    LE,       // 平多 Long Exit
    SO,       // 开空 Short Open
    SE,       // 平空 Short Exit
    INITIAL   // 初始化/不操作
};

inline const char* OperateToStr(Operate op) {
    switch (op) {
        case Operate::LO: return "开多";
        case Operate::LE: return "平多";
        case Operate::SO: return "开空";
        case Operate::SE: return "平空";
        case Operate::INITIAL: return "INITIAL";
    }
    return "UNKNOWN";
}

inline Operate StrToOperate(const std::string& s) {
    if (s == "开多" || s == "LO") return Operate::LO;
    if (s == "平多" || s == "LE") return Operate::LE;
    if (s == "开空" || s == "SO") return Operate::SO;
    if (s == "平空" || s == "SE") return Operate::SE;
    return Operate::INITIAL;
}

inline bool IsOpenOrder(Operate op) {
    return op == Operate::LO || op == Operate::SO;
}

inline bool IsCloseOrder(Operate op) {
    return op == Operate::LE || op == Operate::SE;
}

// ============================================================================
// 订单匹配状态
// ============================================================================

enum MatchStatus {
    MATCH_PENDING  = 0,  // 未成交
    MATCH_FILLED   = 1,  // 已成交
    MATCH_CLOSED   = 2,  // 已平仓
    MATCH_CANCELED = 3   // 已撤销
};

// ============================================================================
// 信号数据 — 策略产出的标准化信号
// ============================================================================
// 对应 Python: SignalData = namedtuple(...)

struct SignalData {
    double   signal_strength;  // 信号强度（用于排序）
    Operate  order_op;         // 操作类型
    int      order_type;       // 0=限价挂单, 1=市价单
    double   qty;              // 数量
    int      id;               // 关联委托号（平仓时指向开仓订单ID）
    std::string remark;        // 备注
    double   price;            // 价格
    double   tp_price;         // 止盈价格（0=不设）
    double   sl_price;         // 止损价格（0=不设）

    SignalData()
        : signal_strength(0), order_op(Operate::INITIAL), order_type(0)
        , qty(0), id(0), price(0), tp_price(0), sl_price(0) {}

    SignalData(double strength, Operate op, int otype, double q,
              int oid, const std::string& rem, double p,
              double tp, double sl)
        : signal_strength(strength), order_op(op), order_type(otype)
        , qty(q), id(oid), remark(rem), price(p)
        , tp_price(tp), sl_price(sl) {}
};
