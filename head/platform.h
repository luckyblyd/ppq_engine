#pragma once
// platform.h — 跨平台抽象层 (Windows/Linux 零开销切换)
//
// 封装: 共享内存, 动态库加载, sleep, 时间函数
// Linux 编译路径与原代码完全一致, 无性能损失

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <io.h>
  #include <process.h>
#else
  #include <dlfcn.h>
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <unistd.h>
#endif

#include <cstdint>
#include <cstring>
#include <ctime>
#include <chrono>
#include <thread>
#include <string>
#include <map>
#include <mutex>

// ============================================================================
// 1. Sleep
// ============================================================================

inline void plat_usleep(unsigned int us) {
#ifdef _WIN32
    // Windows Sleep 精度为 ms, 不足 1ms 按 1ms 处理
    DWORD ms = (us + 999) / 1000;
    if (ms == 0) ms = 1;
    ::Sleep(ms);
#else
    ::usleep(us);
#endif
}

// ============================================================================
// 2. 时间函数
// ============================================================================

inline struct tm* plat_localtime_r(const time_t* t, struct tm* buf) {
#ifdef _WIN32
    localtime_s(buf, t);  // Windows: 参数顺序反
#else
    localtime_r(t, buf);
#endif
    return buf;
}

inline struct tm* plat_gmtime_r(const time_t* t, struct tm* buf) {
#ifdef _WIN32
    gmtime_s(buf, t);
#else
    gmtime_r(t, buf);
#endif
    return buf;
}

inline time_t plat_timegm(struct tm* t) {
#ifdef _WIN32
    return _mkgmtime(t);
#else
    return timegm(t);
#endif
}

// ============================================================================
// 3. 动态库加载
// ============================================================================

#ifdef _WIN32
using DlHandle = HMODULE;
#else
using DlHandle = void*;
#endif

inline DlHandle plat_dlopen(const char* path) {
#ifdef _WIN32
    return ::LoadLibraryA(path);
#else
    return ::dlopen(path, RTLD_NOW);
#endif
}

inline void* plat_dlsym(DlHandle h, const char* sym) {
#ifdef _WIN32
    return (void*)::GetProcAddress(h, sym);
#else
    return ::dlsym(h, sym);
#endif
}

inline void plat_dlclose(DlHandle h) {
#ifdef _WIN32
    if (h) ::FreeLibrary(h);
#else
    if (h) ::dlclose(h);
#endif
}

inline std::string plat_dlerror() {
#ifdef _WIN32
    DWORD err = ::GetLastError();
    if (err == 0) return "";
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, buf, sizeof(buf), nullptr);
    return buf;
#else
    const char* e = ::dlerror();
    return e ? e : "";
#endif
}

// ============================================================================
// 4. 共享内存
// ============================================================================
//
// Linux: POSIX shm_open + mmap (原始方式, 零改动)
// Windows: CreateFileMapping + MapViewOfFile (命名对象)
//
// 接口:
//   void* PlatShmCreate(name, size)  — 创建并初始化
//   void* PlatShmOpen(name, size)    — 打开已有
//   void  PlatShmDestroy(name, ptr, size) — 销毁

#ifdef _WIN32

// Windows 需要保留 HANDLE, 用全局表跟踪
namespace detail {
    inline std::mutex& ShmMtx() { static std::mutex m; return m; }
    inline std::map<void*, HANDLE>& ShmHandles() {
        static std::map<void*, HANDLE> h; return h;
    }
    // Windows 共享内存名不能有 '/', 转换: "/xxx" → "Local\\xxx"
    inline std::string ShmName(const char* name) {
        std::string s(name);
        std::string out = "Local\\";
        for (char c : s) {
            if (c == '/') continue;
            out += c;
        }
        return out;
    }
}

inline void* PlatShmCreate(const char* name, size_t size) {
    auto wname = detail::ShmName(name);
    HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                  PAGE_READWRITE, 0, (DWORD)size, wname.c_str());
    if (!h) return nullptr;
    void* ptr = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!ptr) { CloseHandle(h); return nullptr; }
    memset(ptr, 0, size);  // 等价于 Linux ftruncate 初始化
    std::lock_guard<std::mutex> lk(detail::ShmMtx());
    detail::ShmHandles()[ptr] = h;
    return ptr;
}

inline void* PlatShmOpen(const char* name, size_t size) {
    auto wname = detail::ShmName(name);
    HANDLE h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
    if (!h) return nullptr;
    void* ptr = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!ptr) { CloseHandle(h); return nullptr; }
    std::lock_guard<std::mutex> lk(detail::ShmMtx());
    detail::ShmHandles()[ptr] = h;
    return ptr;
}

inline void PlatShmDestroy(const char* /*name*/, void* ptr, size_t /*size*/) {
    if (!ptr) return;
    UnmapViewOfFile(ptr);
    std::lock_guard<std::mutex> lk(detail::ShmMtx());
    auto it = detail::ShmHandles().find(ptr);
    if (it != detail::ShmHandles().end()) {
        CloseHandle(it->second);
        detail::ShmHandles().erase(it);
    }
}

#else  // Linux

inline void* PlatShmCreate(const char* name, size_t size) {
    shm_unlink(name);
    int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, size) != 0) { close(fd); return nullptr; }
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

inline void* PlatShmOpen(const char* name, size_t size) {
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) return nullptr;
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

inline void PlatShmDestroy(const char* name, void* ptr, size_t size) {
    if (ptr) munmap(ptr, size);
    shm_unlink(name);
}

#endif
