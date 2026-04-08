#pragma once
// framework.h — 量化交易系统 Pub/Sub 通信框架
//
// 本文件包含 server 和 client 共享的基础设施:
//   - IClock / ManualClock / SystemClock: 时钟抽象
//   - IEvent: 事件源接口 (共享内存收发等实现此接口)
//   - EventCore: 事件驱动循环 + 定时器管理
//   - TopicServer: topic 订阅关系管理 + 广播
//   - IPubsubClient: 客户端接口 (生产模式用 shm_ipc.h 中的 ShmPubsubClient)
//   - IStrategyCB / StrategyApp: 策略接口 + 加载框架
//   - MarketDataPlayer / MarketDataRecorder: 行情回播 / 录制
//   - PubsubPlaybackEntry: pubsub 回播条目

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "platform.h"

// ============================================================================
// 时钟抽象
// ============================================================================

class IClock {
public:
    virtual ~IClock()           = default;
    virtual int64_t Now() const = 0;
};

class ManualClock : public IClock {
public:
    void Set(int64_t t) { now_ = t; }
    int64_t Now() const override { return now_; }

private:
    int64_t now_ = 0;
};

class SystemClock : public IClock {
public:
    int64_t Now() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

// ============================================================================
// IEvent — 事件源接口
// ============================================================================
//
// EventCore 驱动的事件源. 生产环境中, 共享内存收发、网络 IO 等均实现此接口,
// 由 EventCore::Run() 循环调用 ProcessEvent() 驱动.
// 返回 true 表示本次调用处理了事件, false 表示无事件.

class IEvent {
public:
    virtual ~IEvent()           = default;
    virtual bool ProcessEvent() = 0;
};

// ============================================================================
// 事件核心 (事件驱动 + 定时器管理)
// ============================================================================

class EventCore {
public:
    using TimerId  = int;
    using Callback = std::function<void()>;

    explicit EventCore(const IClock& clock)
        : clock_(clock) {}

    // 注册事件源, EventCore::Run() 每轮循环调用其 ProcessEvent()
    void RegisterEvent(IEvent& event) { events_.push_back(&event); }

    // 注册一个延迟触发的定时器, 返回 timer id
    TimerId RegisterTimer(Callback cb, int64_t delay_ms) {
        TimerId id = next_id_++;
        timers_.push_back({id, std::move(cb), clock_.Now() + delay_ms, true});
        return id;
    }

    // 取消一个定时器
    void CancelTimer(TimerId id) {
        for (auto& t : timers_) {
            if (t.id == id) t.active = false;
        }
    }

    // 推进时间到 new_time, 触发所有到期的定时器
    // 注意: 只处理调用前已注册的定时器, 回调中新注册的定时器在下次 AdvanceTo 处理
    void AdvanceTo(int64_t new_time) {
        size_t n = timers_.size();
        for (size_t i = 0; i < n; ++i) {
            if (!timers_[i].active) continue;
            if (new_time >= timers_[i].fire_at) {
                timers_[i].active = false;
                timers_[i].cb();
            }
        }
    }

    // 事件驱动主循环: 持续轮询注册的事件源 + 触发到期定时器
    // 生产环境使用 (搭配 SystemClock), 回测使用 AdvanceTo() 手动推进
    void Run() {
        while (!shutdown_) {
            bool processed = false;
            for (auto* event : events_) {
                if (event->ProcessEvent()) {
                    processed = true;
                }
            }
            AdvanceTo(clock_.Now());
            if (!processed) {
                plat_usleep(1000);
            }
        }
    }

    // 停止 Run() 循环
    void Shutdown() { shutdown_ = true; }

private:
    struct Timer {
        TimerId id;
        Callback cb;
        int64_t fire_at;
        bool active;
    };

    const IClock& clock_;
    TimerId next_id_ = 1;
    std::vector<Timer> timers_;
    std::vector<IEvent*> events_;
    bool shutdown_ = false;
};

// ============================================================================
// Server 输出回调接口
// ============================================================================

class IServerOutputCB {
public:
    virtual ~IServerOutputCB()                                                          = default;
    virtual void OnServerBroadcast(const std::string& topic, const char* data, int len) = 0;
    virtual void OnSubscribeAck(const std::string& topic)                               = 0;
    virtual void OnRegisterAck(const std::string& topic, bool success, const char* err) = 0;
};

// ============================================================================
// TopicServer (订阅管理 + 广播)
// ============================================================================

class TopicServer {
public:
    void Subscribe(const std::string& topic, IServerOutputCB& cb) {
        auto& info = GetOrCreate(topic);
        if (std::find(info.subscribers.begin(), info.subscribers.end(), &cb) !=
            info.subscribers.end()) {
            cb.OnSubscribeAck(topic);
            return;
        }
        info.subscribers.push_back(&cb);
        cb.OnSubscribeAck(topic);
    }

    bool Register(const std::string& topic, IServerOutputCB& cb) {
        auto& info = GetOrCreate(topic);
        if (info.has_owner) {
            cb.OnRegisterAck(topic, false, "already owned");
            return false;
        }
        info.has_owner = true;
        cb.OnRegisterAck(topic, true, "");
        return true;
    }

    void Publish(const std::string& topic, const char* data, int len) {
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        for (auto* sub : it->second.subscribers) {
            sub->OnServerBroadcast(topic, data, len);
        }
    }

    void RemoveSubscriber(IServerOutputCB* cb) {
        for (auto& [_, info] : topics_) {
            auto& subs = info.subscribers;
            subs.erase(std::remove(subs.begin(), subs.end(), cb), subs.end());
        }
    }

private:
    struct TopicInfo {
        bool has_owner = false;
        std::vector<IServerOutputCB*> subscribers;
    };

    TopicInfo& GetOrCreate(const std::string& name) { return topics_[name]; }

    std::map<std::string, TopicInfo> topics_;
};

// ============================================================================
// 策略回调接口
// ============================================================================

class ISubscriberCB {
public:
    virtual ~ISubscriberCB()                                                    = default;
    virtual void OnMessage(const std::string& topic, const char* data, int len) = 0;
    virtual void OnSubscribeSuccess(const std::string& topic)                   = 0;
};

class IRegisterCB {
public:
    virtual ~IRegisterCB() = default;
    virtual void OnRegisterResult(const std::string& topic, bool success, const char* err) = 0;
};

// ============================================================================
// IPubsubClient — 多态接口 (sim/shm 共用)
// ============================================================================

class IPubsubClient {
public:
    virtual ~IPubsubClient()                                                  = default;
    virtual void Subscribe(const std::string& topic, ISubscriberCB& cb)       = 0;
    virtual void RegisterTopic(const std::string& topic, IRegisterCB& cb)     = 0;
    virtual void Publish(const std::string& topic, const char* data, int len) = 0;
};

// ============================================================================
// PubsubMsgPacker — 消息序列化 (跨进程传输用)
// ============================================================================
//
// 将 topic + data 打包为二进制格式: [topic_len(4)][data_len(4)][topic][data]
// 生产环境中, 消息需序列化后通过共享内存或 TCP 传输到 server 进程

class PubsubMsgPacker {
public:
    static std::vector<char> Pack(const std::string& topic, const char* data, int len) {
        uint32_t topic_len = static_cast<uint32_t>(topic.size());
        uint32_t data_len  = static_cast<uint32_t>(len);
        std::vector<char> buf(sizeof(topic_len) + sizeof(data_len) + topic_len + data_len);
        char* p = buf.data();
        memcpy(p, &topic_len, sizeof(topic_len));
        p += sizeof(topic_len);
        memcpy(p, &data_len, sizeof(data_len));
        p += sizeof(data_len);
        memcpy(p, topic.data(), topic_len);
        p += topic_len;
        if (data_len > 0 && data) {
            memcpy(p, data, data_len);
        }
        return buf;
    }

    static bool Unpack(
        const char* buf, int buf_len, std::string& topic, const char*& data, int& data_len) {
        if (buf_len < static_cast<int>(sizeof(uint32_t) * 2)) return false;
        uint32_t topic_len, dlen;
        memcpy(&topic_len, buf, sizeof(topic_len));
        memcpy(&dlen, buf + sizeof(topic_len), sizeof(dlen));
        int header_size = sizeof(topic_len) + sizeof(dlen);
        if (buf_len < header_size + static_cast<int>(topic_len + dlen)) return false;
        topic.assign(buf + header_size, topic_len);
        data     = buf + header_size + topic_len;
        data_len = static_cast<int>(dlen);
        return true;
    }
};

// ============================================================================
// IPubsubTransport — 序列化消息的传输接口
// ============================================================================
//
// 定义传输层如何发送序列化后的字节流.
// 生产环境中, 传输层可以是共享内存、TCP 或进程内路由.

class IPubsubTransport {
public:
    virtual ~IPubsubTransport()                               = default;
    virtual void SendSerializedMsg(const char* data, int len) = 0;
};

// ============================================================================
// PubsubMsgClient — pubsub 消息协议处理器
// ============================================================================
//
// 负责消息的序列化, 通过 IPubsubTransport 发送序列化后的字节流.
// 生产环境中还包含消息确认、重传、连接状态管理等协议逻辑.

class PubsubMsgClient {
public:
    explicit PubsubMsgClient(IPubsubTransport& transport)
        : transport_(transport) {}

    void PublishMsg(const std::string& topic, const char* data, int len) {
        auto packed = PubsubMsgPacker::Pack(topic, data, len);
        transport_.SendSerializedMsg(packed.data(), static_cast<int>(packed.size()));
    }

private:
    IPubsubTransport& transport_;
};

// ============================================================================
// PubsubClientBase — pubsub 客户端基类
// ============================================================================
//
// 拥有内部 PubsubMsgClient 实例 (pubsub_client_), 处理消息序列化协议.
// 自身实现 IPubsubTransport 作为 pubsub_client_ 的传输回调.
// 子类通过 override SendSerializedMsg() 定义实际传输方式.
//
// 数据流: 策略 Publish(topic, data)
//   → pubsub_client_.PublishMsg()          [序列化]
//   → IPubsubTransport::SendSerializedMsg  [回调到子类]
//   → 传输层 (共享内存 / TCP / 进程内路由)

class PubsubClientBase : public IPubsubClient, public IPubsubTransport {
public:
    PubsubClientBase()
        : pubsub_client_(*this) {}

    void Publish(const std::string& topic, const char* data, int len) override {
        pubsub_client_.PublishMsg(topic, data, len);
    }

protected:
    // pubsub_client_ 是消息协议处理组件, 负责序列化并通过 transport 回调发送
    PubsubMsgClient pubsub_client_;
};

// ============================================================================
// IStrategyCB — 策略接口 (简化版)
// ============================================================================

class IStrategyCB {
public:
    virtual ~IStrategyCB()                   = default;
    virtual bool Init(IPubsubClient& pubsub) = 0;
    virtual void OnStart()                   = 0;
    virtual void OnStop()                    = 0;
};

using CREATE_INSTANCE  = IStrategyCB* (*)();
using DESTROY_INSTANCE = void (*)(IStrategyCB*);

// ============================================================================
// StrategyApp — 策略加载框架
// ============================================================================

class StrategyApp {
public:
    explicit StrategyApp(IPubsubClient& pubsub)
        : pubsub_(pubsub) {}

    ~StrategyApp() { Stop(); }

    // 直接传入策略指针 (不用 dlopen, 用于测试)
    bool LoadAndInit(IStrategyCB* strategy) {
        strategy_      = strategy;
        owns_strategy_ = false;
        return strategy_->Init(pubsub_);
    }

    // 从 .so 文件加载策略
    bool LoadFromSo(const std::string& path) {
        handle_ = plat_dlopen(path.c_str());
        if (!handle_) {
            printf("dlopen failed: %s\n", plat_dlerror().c_str());
            return false;
        }
        auto create = (CREATE_INSTANCE)plat_dlsym(handle_, "CreateInstance");
        destroy_fn_ = (DESTROY_INSTANCE)plat_dlsym(handle_, "DestroyInstance");
        if (!create || !destroy_fn_) {
            printf("dlsym failed: %s\n", plat_dlerror().c_str());
            plat_dlclose(handle_);
            handle_ = nullptr;
            return false;
        }
        strategy_      = create();
        owns_strategy_ = true;
        return strategy_->Init(pubsub_);
    }

    void Start() {
        if (strategy_) strategy_->OnStart();
    }

    void Stop() {
        if (strategy_) {
            strategy_->OnStop();
            if (owns_strategy_ && destroy_fn_) {
                destroy_fn_(strategy_);
            }
            strategy_ = nullptr;
        }
        if (handle_) {
            plat_dlclose(handle_);
            handle_ = nullptr;
        }
    }

private:
    IPubsubClient& pubsub_;
    IStrategyCB* strategy_       = nullptr;
    bool owns_strategy_          = false;
    DlHandle handle_             = nullptr;
    DESTROY_INSTANCE destroy_fn_ = nullptr;
};

// ============================================================================
// 行情数据 — 录制和回播
// ============================================================================

struct MarketDataEntry {
    int64_t ts;
    std::string instrument;
    double price;
    int volume;
};

class MarketDataPlayer {
public:
    void AddEntry(int64_t ts, const std::string& instrument, double price, int volume) {
        entries_.push_back({ts, instrument, price, volume});
    }

    std::vector<const MarketDataEntry*> GetReady(int64_t now) {
        std::vector<const MarketDataEntry*> result;
        while (cursor_ < entries_.size() && entries_[cursor_].ts <= now) {
            result.push_back(&entries_[cursor_]);
            cursor_++;
        }
        return result;
    }

private:
    std::vector<MarketDataEntry> entries_;
    size_t cursor_ = 0;
};

class MarketDataRecorder {
public:
    void Record(int64_t ts, const std::string& instrument, double price, int volume) {
        entries_.push_back({ts, instrument, price, volume});
    }

    const std::vector<MarketDataEntry>& GetRecords() const { return entries_; }

private:
    std::vector<MarketDataEntry> entries_;
};

class IMarketDataCB {
public:
    virtual ~IMarketDataCB()                                                           = default;
    virtual void OnMarketData(const std::string& instrument, double price, int volume) = 0;
};

// ============================================================================
// 回测模式辅助组件 — 前向声明
// ============================================================================

class SimPubsubClient;  // 定义在 sim_pubsub_client.h

// ServerBridge: 将 TopicServer 的输出转发回 SimPubsubClient
class ServerBridge : public IServerOutputCB {
public:
    explicit ServerBridge(SimPubsubClient& client);
    void OnServerBroadcast(const std::string& topic, const char* data, int len) override;
    void OnSubscribeAck(const std::string& topic) override;
    void OnRegisterAck(const std::string& topic, bool success, const char* err) override;

private:
    SimPubsubClient& client_;
};

// DummyServerOutput: 丢弃 server 输出 (回放时 server 的响应不需要发给任何人)
class DummyServerOutput : public IServerOutputCB {
public:
    void OnServerBroadcast(const std::string&, const char*, int) override {}
    void OnSubscribeAck(const std::string&) override {}
    void OnRegisterAck(const std::string&, bool, const char*) override {}
};

// Pubsub 消息条目 (录制用)
struct PubsubMsgEntry {
    int64_t ts;
    std::string topic;
    std::string data;
};

// Pubsub 回播条目
struct PubsubPlaybackEntry {
    int64_t ts;
    std::string topic;
    std::string data;
};
