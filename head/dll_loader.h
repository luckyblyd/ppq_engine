#pragma once
// dll_loader.h — 动态加载 czsc_dll.dll / trade_dll.dll / html_report_dll.dll
// Windows 下使用 LoadLibrary / GetProcAddress

#include <string>
#include <cstdio>
#include "czsc_dll_interface.h"
#include "trade_dll_interface.h"
#include "html_report_dll_interface.h"
#include "logger.h"

#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE DllHandle;
  #define LOAD_DLL(path)       LoadLibraryA(path)
  #define GET_FUNC(h, name)    GetProcAddress(h, name)
  #define FREE_DLL(h)          FreeLibrary(h)
#else
  #include <dlfcn.h>
  typedef void* DllHandle;
  #define LOAD_DLL(path)       dlopen(path, RTLD_LAZY)
  #define GET_FUNC(h, name)    dlsym(h, name)
  #define FREE_DLL(h)          dlclose(h)
#endif

// ============================================================================
// CzscDll — CZSC DLL 运行时加载器
// ============================================================================

class CzscDll {
public:
    bool Load(const std::string& dll_path) {
        handle_ = LOAD_DLL(dll_path.c_str());
        if (!handle_) {
            LOG_E("[CzscDll] Failed to load: %s", dll_path.c_str());
            return false;
        }
        pfn_init_     = (fn_init)GET_FUNC(handle_, "czsc_init");
        pfn_destroy_  = (fn_destroy)GET_FUNC(handle_, "czsc_destroy");
        pfn_init_trader_ = (fn_init_trader)GET_FUNC(handle_, "czsc_init_trader");
        pfn_update_   = (fn_update)GET_FUNC(handle_, "czsc_update_bar");
        pfn_signals_  = (fn_signals)GET_FUNC(handle_, "czsc_get_signals");
        pfn_trend_    = (fn_trend)GET_FUNC(handle_, "czsc_get_trend");
        pfn_sr_       = (fn_sr)GET_FUNC(handle_, "czsc_get_support_resistance");

        LOG_I("[CzscDll] Loaded: %s", dll_path.c_str());
        return pfn_init_ && pfn_destroy_ && pfn_init_trader_ && pfn_update_ && pfn_signals_;
    }

    void Unload() {
        if (pfn_destroy_) pfn_destroy_();
        if (handle_) { FREE_DLL(handle_); handle_ = nullptr; }
    }

    int Init(const char* python_home, const char* script_dir, const char* base_freq) {
        return pfn_init_ ? pfn_init_(python_home, script_dir, base_freq) : -1;
    }
    int InitTrader(const char* symbol, const CzscBar* bars, int count) {
        return pfn_init_trader_ ? pfn_init_trader_(symbol, bars, count) : -1;
    }
    int UpdateBar(const char* symbol, const CzscBar* bar) {
        return pfn_update_ ? pfn_update_(symbol, bar) : -1;
    }
    int GetSignals(const char* symbol, CzscSignals* out) {
        return pfn_signals_ ? pfn_signals_(symbol, out) : -1;
    }
    int GetTrend(const char* symbol, int* t5m, int* t60m) {
        return pfn_trend_ ? pfn_trend_(symbol, t5m, t60m) : -1;
    }
    int GetSupportResistance(const char* symbol, double* hhyl, double* ddzc,
                              double* yl5m, double* zc5m) {
        return pfn_sr_ ? pfn_sr_(symbol, hhyl, ddzc, yl5m, zc5m) : -1;
    }

private:
    DllHandle handle_ = nullptr;

    typedef int  (*fn_init)(const char*, const char*, const char*);
    typedef void (*fn_destroy)();
    typedef int  (*fn_init_trader)(const char*, const CzscBar*, int);
    typedef int  (*fn_update)(const char*, const CzscBar*);
    typedef int  (*fn_signals)(const char*, CzscSignals*);
    typedef int  (*fn_trend)(const char*, int*, int*);
    typedef int  (*fn_sr)(const char*, double*, double*, double*, double*);

    fn_init         pfn_init_ = nullptr;
    fn_destroy      pfn_destroy_ = nullptr;
    fn_init_trader  pfn_init_trader_ = nullptr;
    fn_update       pfn_update_ = nullptr;
    fn_signals      pfn_signals_ = nullptr;
    fn_trend        pfn_trend_ = nullptr;
    fn_sr           pfn_sr_ = nullptr;
};

// ============================================================================
// TradeDll — 交易 DLL 运行时加载器
// ============================================================================

class TradeDll {
public:
    bool Load(const std::string& dll_path) {
        handle_ = LOAD_DLL(dll_path.c_str());
        if (!handle_) {
            LOG_E("[TradeDll] Failed to load: %s", dll_path.c_str());
            return false;
        }
        pfn_init_     = (fn_init)GET_FUNC(handle_, "trade_init");
        pfn_destroy_  = (fn_destroy)GET_FUNC(handle_, "trade_destroy");
        pfn_place_    = (fn_place)GET_FUNC(handle_, "trade_place_order");
        pfn_cancel_   = (fn_cancel)GET_FUNC(handle_, "trade_cancel_order");
        pfn_query_    = (fn_query)GET_FUNC(handle_, "trade_query_order");
        pfn_algo_     = (fn_algo)GET_FUNC(handle_, "trade_place_algo_order");
        pfn_cancel_algo_ = (fn_cancel_algo)GET_FUNC(handle_, "trade_cancel_algo_order");

        LOG_I("[TradeDll] Loaded: %s", dll_path.c_str());
        return pfn_init_ && pfn_place_;
    }

    void Unload() {
        if (pfn_destroy_) pfn_destroy_();
        if (handle_) { FREE_DLL(handle_); handle_ = nullptr; }
    }

    int Init(const char* config_path) {
        return pfn_init_ ? pfn_init_(config_path) : -1;
    }
    int PlaceOrder(const TradeOrderRequest* req, TradeOrderResponse* resp) {
        return pfn_place_ ? pfn_place_(req, resp) : -1;
    }
    int CancelOrder(const char* sym, const char* oid, TradeOrderResponse* resp) {
        return pfn_cancel_ ? pfn_cancel_(sym, oid, resp) : -1;
    }
    int QueryOrder(const char* sym, const char* oid, TradeOrderResponse* resp) {
        return pfn_query_ ? pfn_query_(sym, oid, resp) : -1;
    }
    int PlaceAlgoOrder(const TradeAlgoRequest* req, TradeOrderResponse* resp) {
        return pfn_algo_ ? pfn_algo_(req, resp) : -1;
    }
    int CancelAlgoOrder(const char* aid, TradeOrderResponse* resp) {
        return pfn_cancel_algo_ ? pfn_cancel_algo_(aid, resp) : -1;
    }

private:
    DllHandle handle_ = nullptr;

    typedef int   (*fn_init)(const char*);
    typedef void  (*fn_destroy)();
    typedef int   (*fn_place)(const TradeOrderRequest*, TradeOrderResponse*);
    typedef int   (*fn_cancel)(const char*, const char*, TradeOrderResponse*);
    typedef int   (*fn_query)(const char*, const char*, TradeOrderResponse*);
    typedef int   (*fn_algo)(const TradeAlgoRequest*, TradeOrderResponse*);
    typedef int   (*fn_cancel_algo)(const char*, TradeOrderResponse*);

    fn_init         pfn_init_ = nullptr;
    fn_destroy      pfn_destroy_ = nullptr;
    fn_place        pfn_place_ = nullptr;
    fn_cancel       pfn_cancel_ = nullptr;
    fn_query        pfn_query_ = nullptr;
    fn_algo         pfn_algo_ = nullptr;
    fn_cancel_algo  pfn_cancel_algo_ = nullptr;
};

// ============================================================================
// HtmlReportDll — HTML 报告 DLL 运行时加载器
// ============================================================================

class HtmlReportDll {
public:
    bool Load(const std::string& dll_path) {
        handle_ = LOAD_DLL(dll_path.c_str());
        if (!handle_) {
            LOG_E("[HtmlReportDll] Failed to load: %s", dll_path.c_str());
            return false;
        }
        pfn_init_     = (fn_init)GET_FUNC(handle_, "report_init");
        pfn_destroy_  = (fn_destroy)GET_FUNC(handle_, "report_destroy");
        pfn_generate_ = (fn_generate)GET_FUNC(handle_, "report_generate");

        LOG_I("[HtmlReportDll] Loaded: %s", dll_path.c_str());
        return pfn_init_ && pfn_generate_;
    }

    void Unload() {
        if (pfn_destroy_) pfn_destroy_();
        if (handle_) { FREE_DLL(handle_); handle_ = nullptr; }
    }

    int Init() {
        return pfn_init_ ? pfn_init_() : -1;
    }

    int Generate(const ReportConfig* config,
                 const ReportBar* bars, int bar_count,
                 const ReportOrder* orders, int order_count,
                 const ReportPerformance* perf,
                 char* out_path, int out_path_size) {
        return pfn_generate_
            ? pfn_generate_(config, bars, bar_count, orders, order_count,
                            perf, out_path, out_path_size)
            : -1;
    }

    bool IsLoaded() const { return handle_ != nullptr; }

private:
    DllHandle handle_ = nullptr;

    typedef int  (*fn_init)();
    typedef void (*fn_destroy)();
    typedef int  (*fn_generate)(const ReportConfig*, const ReportBar*, int,
                                const ReportOrder*, int,
                                const ReportPerformance*,
                                char*, int);

    fn_init     pfn_init_ = nullptr;
    fn_destroy  pfn_destroy_ = nullptr;
    fn_generate pfn_generate_ = nullptr;
};
