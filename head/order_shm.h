#pragma once
// order_shm.h — 委托/资金共享内存布局
//
// 用于 ppq_main 写入实盘委托和资金状态,
// market_server 的 web_server 可查询和删除委托
//
// 设计:
//   - ppq_main 是唯一写入方 (单写者)
//   - web_server 是读取方 + 可发送删除请求
//   - 通过 version 原子变量做乐观锁, 保证读一致性
//   - 删除请求通过 delete_queue 传递, ppq_main 轮询处理

#include <atomic>
#include <cstdint>
#include <cstring>
#include "platform.h"

// ============================================================================
// OrderShmEntry — 单个委托条目 (固定大小, 共享内存对齐)
// ============================================================================

struct OrderShmEntry {
    int      id;
    int64_t  timestamp;
    char     currency_pair[24];
    double   price;
    double   quantity;
    double   amount;
    int      type;              // Operate 枚举值
    int      order_type;        // 0=限价, 1=市价
    int      matchstatus;       // 0=pending, 1=filled, 2=closed, 3=canceled
    double   take_profit_price;
    double   stop_loss_price;
    double   profit;
    int      old_orderid;
    char     remark[64];
    char     status[16];        // "OPEN" / "CLOSED" / "CANCELED"
    char     _pad[4];
};

// ============================================================================
// AccountShmEntry — 单个账户资金快照
// ============================================================================

struct AccountShmEntry {
    char     currency_pair[24];
    double   current_capital;
    double   occupied_capital;
    double   floating_pnl;
    int      active_orders;     // 活跃委托数
    int      filled_longs;      // 已成交多头数
    int      filled_shorts;     // 已成交空头数
    char     _pad[4];
};

// ============================================================================
// DeleteRequest — web_server 发给 ppq_main 的删除请求
// ============================================================================

struct DeleteRequest {
    int      order_id;
    int      processed;         // 0=待处理, 1=已处理
};

// ============================================================================
// OrderShmRegion — 委托共享内存总布局
// ============================================================================

struct OrderShmRegion {
    static constexpr int MAX_ORDERS   = 256;
    static constexpr int MAX_ACCOUNTS = 16;
    static constexpr int MAX_DELETES  = 32;

    std::atomic<bool>     ready{false};
    std::atomic<uint64_t> version{0};       // 写入版本号, 每次更新递增

    // 委托数据
    std::atomic<int> num_orders{0};
    OrderShmEntry    orders[MAX_ORDERS];

    // 账户资金数据
    std::atomic<int> num_accounts{0};
    AccountShmEntry  accounts[MAX_ACCOUNTS];

    // 删除请求队列 (web_server 写, ppq_main 读)
    std::atomic<int> num_deletes{0};
    DeleteRequest    delete_queue[MAX_DELETES];

    // ---- ppq_main 调用: 全量刷新委托列表 ----
    void SyncOrders(const OrderShmEntry* entries, int count) {
        version.fetch_add(1, std::memory_order_acq_rel);
        int n = (count > MAX_ORDERS) ? MAX_ORDERS : count;
        for (int i = 0; i < n; ++i) {
            orders[i] = entries[i];
        }
        num_orders.store(n, std::memory_order_release);
        version.fetch_add(1, std::memory_order_acq_rel);
    }

    // ---- ppq_main 调用: 刷新账户资金 ----
    void SyncAccounts(const AccountShmEntry* entries, int count) {
        int n = (count > MAX_ACCOUNTS) ? MAX_ACCOUNTS : count;
        for (int i = 0; i < n; ++i) {
            accounts[i] = entries[i];
        }
        num_accounts.store(n, std::memory_order_release);
    }

    // ---- web_server 调用: 读取委托 (乐观锁) ----
    // 返回 true 表示读取成功 (版本一致), false 表示读取期间被更新
    bool ReadOrders(OrderShmEntry* out, int& count) {
        uint64_t v1 = version.load(std::memory_order_acquire);
        if (v1 & 1) return false; // 写入中, 稍后重试

        count = num_orders.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i) {
            out[i] = orders[i];
        }

        uint64_t v2 = version.load(std::memory_order_acquire);
        return v1 == v2;
    }

    // ---- web_server 调用: 提交删除请求 ----
    bool RequestDelete(int order_id) {
        int n = num_deletes.load(std::memory_order_acquire);
        if (n >= MAX_DELETES) return false;
        delete_queue[n].order_id  = order_id;
        delete_queue[n].processed = 0;
        num_deletes.store(n + 1, std::memory_order_release);
        return true;
    }

    // ---- ppq_main 调用: 处理删除请求, 返回要删除的 order_id 列表 ----
    int DrainDeletes(int* out_ids, int max_ids) {
        int n = num_deletes.load(std::memory_order_acquire);
        int collected = 0;
        for (int i = 0; i < n && collected < max_ids; ++i) {
            if (delete_queue[i].processed == 0) {
                out_ids[collected++] = delete_queue[i].order_id;
                delete_queue[i].processed = 1;
            }
        }
        // 清理已处理的请求
        if (collected > 0) {
            int new_n = 0;
            for (int i = 0; i < n; ++i) {
                if (delete_queue[i].processed == 0) {
                    if (new_n != i) delete_queue[new_n] = delete_queue[i];
                    new_n++;
                }
            }
            num_deletes.store(new_n, std::memory_order_release);
        }
        return collected;
    }
};

// ============================================================================
// 共享内存辅助函数
// ============================================================================

static const char* kOrderShmName = "/ppq_order_shm";

inline OrderShmRegion* OrderShmCreate() {
    void* p = PlatShmCreate(kOrderShmName, sizeof(OrderShmRegion));
    if (!p) return nullptr;
    return new(p) OrderShmRegion();
}

inline OrderShmRegion* OrderShmOpen() {
    void* p = PlatShmOpen(kOrderShmName, sizeof(OrderShmRegion));
    return p ? static_cast<OrderShmRegion*>(p) : nullptr;
}

inline void OrderShmDestroy(OrderShmRegion* r) {
    PlatShmDestroy(kOrderShmName, r, sizeof(OrderShmRegion));
}
