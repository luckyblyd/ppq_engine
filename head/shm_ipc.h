#pragma once
// shm_ipc.h — 共享内存 IPC 基础设施 (header-only)
//
// 使用 POSIX 共享内存 + SPSC 无锁环形队列实现两个独立进程之间的通信:
//   - ShmQueue: SPSC 无锁环形队列
//   - ShmChannelData: 双向通道 (to_server + to_client)
//   - ShmRegion: 共享内存总布局
//   - ShmServerProxy: server 端代理 (TopicServer ↔ 共享内存), 实现 IEvent
//   - ShmPubsubClient: client 端 IPubsubClient 实现, 实现 IEvent
//   - ShmCreate/ShmOpen/ShmDestroy: POSIX shm 辅助函数

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "platform.h"

#include "framework.h"

// ============================================================================
// 消息类型
// ============================================================================

enum class ShmMsgType : uint32_t {
    kSubscribe    = 1,
    kPublish      = 2,
    kBroadcast    = 3,
    kSubscribeAck = 4,
    kRegisterAck  = 5,
};

// ============================================================================
// ShmQueue — SPSC 无锁环形队列
// ============================================================================
//
// 固定大小: 32 个 slot, 每个 slot 1024 字节
// 布局: [type(4)] [topic_len(4)] [data_len(4)] [topic...] [data...]

class ShmQueue {
public:
    static constexpr uint32_t kSlotSize   = 1024;
    static constexpr uint32_t kSlotCount  = 32;
    static constexpr uint32_t kMaxPayload = kSlotSize - sizeof(ShmMsgType) - sizeof(uint32_t) * 2;

    void Init() {
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0, std::memory_order_relaxed);
    }

    // 写入一条消息, 返回 false 表示队列满
    bool Push(ShmMsgType type, const std::string& topic, const char* data, uint32_t len) {
        uint32_t wp   = write_pos.load(std::memory_order_relaxed);
        uint32_t rp   = read_pos.load(std::memory_order_acquire);
        uint32_t next = (wp + 1) % kSlotCount;
        if (next == rp) return false;  // full

        uint32_t topic_len = static_cast<uint32_t>(topic.size());
        if (topic_len + len > kMaxPayload) return false;  // too large

        char* slot      = &slots[wp * kSlotSize];
        uint32_t offset = 0;

        memcpy(slot + offset, &type, sizeof(type));
        offset += sizeof(type);
        memcpy(slot + offset, &topic_len, sizeof(topic_len));
        offset += sizeof(topic_len);
        memcpy(slot + offset, &len, sizeof(len));
        offset += sizeof(len);
        memcpy(slot + offset, topic.data(), topic_len);
        offset += topic_len;
        if (len > 0 && data) {
            memcpy(slot + offset, data, len);
        }

        write_pos.store(next, std::memory_order_release);
        return true;
    }

    // 读取一条消息, 返回 false 表示队列空
    bool Pop(ShmMsgType& type, std::string& topic, std::string& data) {
        uint32_t rp = read_pos.load(std::memory_order_relaxed);
        uint32_t wp = write_pos.load(std::memory_order_acquire);
        if (rp == wp) return false;  // empty

        const char* slot = &slots[rp * kSlotSize];
        uint32_t offset  = 0;

        memcpy(&type, slot + offset, sizeof(type));
        offset += sizeof(type);
        uint32_t topic_len, data_len;
        memcpy(&topic_len, slot + offset, sizeof(topic_len));
        offset += sizeof(topic_len);
        memcpy(&data_len, slot + offset, sizeof(data_len));
        offset += sizeof(data_len);

        topic.assign(slot + offset, topic_len);
        offset += topic_len;
        data.assign(slot + offset, data_len);

        read_pos.store((rp + 1) % kSlotCount, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<uint32_t> write_pos;
    alignas(64) std::atomic<uint32_t> read_pos;
    char slots[kSlotCount * kSlotSize];
};

// ============================================================================
// ShmChannelData — 双向通道 (放在共享内存中)
// ============================================================================

struct ShmChannelData {
    ShmQueue to_server;  // client → server
    ShmQueue to_client;  // server → client
    std::atomic<bool> client_connected;

    void Init() {
        to_server.Init();
        to_client.Init();
        client_connected.store(false, std::memory_order_relaxed);
    }
};

// ============================================================================
// ShmRegion — 共享内存总布局
// ============================================================================

struct ShmRegion {
    static constexpr int kMaxClients = 4;

    std::atomic<bool> server_ready;
    std::atomic<int> next_client_id;
    ShmChannelData channels[kMaxClients];

    void Init() {
        server_ready.store(false, std::memory_order_relaxed);
        next_client_id.store(0, std::memory_order_relaxed);
        for (int i = 0; i < kMaxClients; i++) {
            channels[i].Init();
        }
    }
};

// ============================================================================
// POSIX 共享内存辅助函数
// ============================================================================

inline ShmRegion* ShmCreate(const char* name) {
    void* ptr = PlatShmCreate(name, sizeof(ShmRegion));
    if (!ptr) return nullptr;
    auto* region = static_cast<ShmRegion*>(ptr);
    region->Init();
    return region;
}

inline ShmRegion* ShmOpen(const char* name) {
    void* ptr = PlatShmOpen(name, sizeof(ShmRegion));
    return ptr ? static_cast<ShmRegion*>(ptr) : nullptr;
}

inline void ShmDestroy(const char* name, ShmRegion* region) {
    PlatShmDestroy(name, region, sizeof(ShmRegion));
}

// ============================================================================
// ShmServerProxy — server 端代理
// ============================================================================
//
// 持有 TopicServer& + ShmChannelData&
// Poll(): 从 to_server 读取 client 请求, 分发给 TopicServer
// 作为 IServerOutputCB: TopicServer 的输出写入 to_client

class ShmServerProxy : public IServerOutputCB, public IEvent {
public:
    ShmServerProxy(TopicServer& server, ShmChannelData& channel)
        : server_(server)
        , channel_(channel) {}

    // IEvent: EventCore 每轮循环调用, 检查连接状态并处理请求
    bool ProcessEvent() override {
        bool connected = channel_.client_connected.load(std::memory_order_acquire);
        if (!connected) return false;
        return Poll() > 0;
    }

    // 处理 client 发来的请求, 返回处理的消息数
    int Poll() {
        int count = 0;
        ShmMsgType type;
        std::string topic, data;
        while (channel_.to_server.Pop(type, topic, data)) {
            count++;
            switch (type) {
            case ShmMsgType::kSubscribe: server_.Subscribe(topic, *this); break;
            case ShmMsgType::kPublish:
                server_.Publish(topic, data.c_str(), static_cast<int>(data.size()));
                break;
            default: break;
            }
        }
        return count;
    }

    // --- IServerOutputCB ---
    void OnServerBroadcast(const std::string& topic, const char* data, int len) override {
        channel_.to_client.Push(ShmMsgType::kBroadcast, topic, data, static_cast<uint32_t>(len));
    }

    void OnSubscribeAck(const std::string& topic) override {
        channel_.to_client.Push(ShmMsgType::kSubscribeAck, topic, nullptr, 0);
    }

    void OnRegisterAck(const std::string& topic, bool success, const char* err) override {
        std::string payload = success ? "1" : std::string("0") + (err ? err : "");
        channel_.to_client.Push(
            ShmMsgType::kRegisterAck, topic, payload.c_str(),
            static_cast<uint32_t>(payload.size()));
    }

private:
    TopicServer& server_;
    ShmChannelData& channel_;
};

// ============================================================================
// ShmPubsubClient — client 端 IPubsubClient 实现
// ============================================================================

class ShmPubsubClient : public IPubsubClient, public IEvent {
public:
    explicit ShmPubsubClient(ShmChannelData& channel)
        : channel_(channel) {}

    void Subscribe(const std::string& topic, ISubscriberCB& cb) override {
        sub_map_[topic].push_back(&cb);
        channel_.to_server.Push(ShmMsgType::kSubscribe, topic, nullptr, 0);
    }

    void RegisterTopic(const std::string& topic, IRegisterCB& cb) override {
        reg_map_[topic] = &cb;
        // 暂不支持 register via shm
    }

    void Publish(const std::string& topic, const char* data, int len) override {
        channel_.to_server.Push(ShmMsgType::kPublish, topic, data, static_cast<uint32_t>(len));
    }

    // IEvent: EventCore 每轮循环调用, 处理 server 发来的响应
    bool ProcessEvent() override { return Poll() > 0; }

    // 处理 server 发来的响应, 返回处理的消息数
    int Poll() {
        int count = 0;
        ShmMsgType type;
        std::string topic, data;
        while (channel_.to_client.Pop(type, topic, data)) {
            count++;
            switch (type) {
            case ShmMsgType::kBroadcast: {
                auto it = sub_map_.find(topic);
                if (it != sub_map_.end()) {
                    for (auto* cb : it->second) {
                        cb->OnMessage(topic, data.c_str(), static_cast<int>(data.size()));
                    }
                }
                break;
            }
            case ShmMsgType::kSubscribeAck: {
                auto it = sub_map_.find(topic);
                if (it != sub_map_.end()) {
                    for (auto* cb : it->second) {
                        cb->OnSubscribeSuccess(topic);
                    }
                }
                break;
            }
            case ShmMsgType::kRegisterAck: {
                auto it = reg_map_.find(topic);
                if (it != reg_map_.end()) {
                    bool success    = !data.empty() && data[0] == '1';
                    const char* err = success ? "" : data.c_str() + 1;
                    it->second->OnRegisterResult(topic, success, err);
                }
                break;
            }
            default: break;
            }
        }
        return count;
    }

private:
    ShmChannelData& channel_;
    std::map<std::string, std::vector<ISubscriberCB*>> sub_map_;
    std::map<std::string, IRegisterCB*> reg_map_;
};
