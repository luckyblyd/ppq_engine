#pragma once
// market_data_types.h — K线数据结构 + 行情共享内存布局
#include <atomic>
#include <cstdint>
#include <cstring>
#include "platform.h"

// ============================================================================
// KlineBar — 单根K线 (固定128字节, 适合共享内存对齐)
// ============================================================================
#pragma pack(push, 1)
struct KlineBar {
    int64_t  timestamp;           // 开盘时间 (epoch ms)
    double   open;
    double   high;
    double   low;
    double   close;
    double   volume;              // matchqty
    double   quote_volume;        // matchamt
    double   change_pct;          // change_per
    char     currency_pair[32];   // "ETH/USDT"
    char     timeframe[8];        // "1m"
    uint8_t  flags;               // bit0=数据不连续
    //uint8_t  _pad[7];
    char     _reserved[23];       // 凑满128字节
};
#pragma pack(pop)
static_assert(sizeof(KlineBar) == 128, "KlineBar size mismatch");

// ============================================================================
// KlineRing — 单币种环形缓冲区 (无锁 SPSC 写, 多读)
// ============================================================================
struct KlineRing {
    // 物理容量必须是 2 的幂，用于底层位运算
    static constexpr uint32_t PHYSICAL_CAPACITY = 8192; 
    static constexpr uint32_t MASK = PHYSICAL_CAPACITY - 1;
    
    // 业务逻辑容量
    static constexpr uint32_t LOGICAL_CAPACITY = 6000; 

    alignas(64) std::atomic<uint32_t> head{0}; 
    alignas(64) std::atomic<uint32_t> total{0};
    KlineBar bars[PHYSICAL_CAPACITY];

    // 入栈：生产者（行情端）毫无顾忌地写入，永远保持 O(1) 且无溢出Bug
    void Push(const KlineBar& bar) {
        uint32_t pos = head.load(std::memory_order_relaxed);
        bars[pos & MASK] = bar; // 位运算取模
        head.store(pos + 1, std::memory_order_release);
        total.fetch_add(1, std::memory_order_relaxed);
    }

    // "出栈"：消费者（策略端）获取最近的数据，严格被 6000 限制
    uint32_t ReadLatest(KlineBar* out, uint32_t count) const {
        // 业务限制：策略端最多只能要 6000 条
        if (count > LOGICAL_CAPACITY) count = LOGICAL_CAPACITY;

        uint32_t h = head.load(std::memory_order_acquire);
        uint32_t t = total.load(std::memory_order_relaxed);
        
        // 实际可用数量
        uint32_t avail = (t < count) ? t : count;
        
        // 逆向倒推，获取最新的 avail 条数据
        for (uint32_t i = 0; i < avail; i++) {
            // 注意：这里仍然要用物理容量 MASK 来计算真实物理地址
            uint32_t physical_idx = (h - avail + i) & MASK;
            out[i] = bars[physical_idx];
        }
        return avail;
    }
};

// ============================================================================
// MarketShmRegion — 行情共享内存总布局
// ============================================================================
struct MarketShmRegion {
    static constexpr int MAX_SYMBOLS = 16;
    std::atomic<bool> ready{false};
    std::atomic<int>  num_symbols{0};
    char symbol_names[MAX_SYMBOLS][24];
    char symbol_tfs[MAX_SYMBOLS][8];
    KlineRing rings[MAX_SYMBOLS];

    int FindOrAdd(const char* pair, const char* tf) {
        int n = num_symbols.load(std::memory_order_relaxed);
        for (int i = 0; i < n; i++) {
            if (strcmp(symbol_names[i], pair) == 0 && strcmp(symbol_tfs[i], tf) == 0)
                return i;
        }
        if (n >= MAX_SYMBOLS) return -1;
        strncpy(symbol_names[n], pair, 23);
        strncpy(symbol_tfs[n], tf, 7);
        num_symbols.store(n + 1, std::memory_order_release);
        return n;
    }
};

// ============================================================================
// 行情共享内存辅助函数
// ============================================================================
static const char* kMarketShmName = "/binance_market_shm";

inline MarketShmRegion* MarketShmCreate() {
    void* p = PlatShmCreate(kMarketShmName, sizeof(MarketShmRegion));
    if (!p) return nullptr;
    return new(p) MarketShmRegion();
}

inline MarketShmRegion* MarketShmOpen() {
    void* p = PlatShmOpen(kMarketShmName, sizeof(MarketShmRegion));
    return p ? static_cast<MarketShmRegion*>(p) : nullptr;
}

inline void MarketShmDestroy(MarketShmRegion* r) {
    PlatShmDestroy(kMarketShmName, r, sizeof(MarketShmRegion));
}