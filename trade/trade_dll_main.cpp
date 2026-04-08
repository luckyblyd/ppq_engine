// Pure C++ trade DLL — 使用 libcurl + OpenSSL 与币安 REST API 交互

#define TRADE_DLL_EXPORTS

#include "../head/trade_dll_interface.h"
#include "../head/config_parser.h"
#include "../head/logger.h"
#include "../third_party/json.hpp"

#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <map>
#include <vector>
#include <chrono>

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

using json = nlohmann::json;

static bool g_initialized = false;
static char g_mode_buf[32] = "future";
static std::string g_api_key;
static std::string g_api_secret;
static std::string g_base_url = "https://fapi.binance.com"; // USDT-M futures
static std::string g_proxy;

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = (std::string*)userp;
    mem->append((char*)contents, realsize);
    return realsize;
}

static std::string url_encode(const std::string& val) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (auto c : val) {
        if (('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c=='-' || c=='_' || c=='.' || c=='~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << (int)((unsigned char)c);
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

static std::string hmac_sha256_hex(const std::string& key, const std::string& data) {
    unsigned char* digest = nullptr;
    unsigned int len = EVP_MAX_MD_SIZE;
    unsigned char result[EVP_MAX_MD_SIZE];
    HMAC_CTX* ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, key.c_str(), (int)key.size(), EVP_sha256(), nullptr);
    HMAC_Update(ctx, (const unsigned char*)data.c_str(), data.size());
    HMAC_Final(ctx, result, &len);
    HMAC_CTX_free(ctx);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i) ss << std::setw(2) << (int)result[i];
    return ss.str();
}

static long long timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string build_query(const std::map<std::string, std::string>& params) {
    std::string q;
    bool first = true;
    for (auto& kv : params) {
        if (!first) q += "&";
        first = false;
        q += kv.first + "=" + url_encode(kv.second);
    }
    return q;
}

static bool http_request(const std::string& method, const std::string& path,
                         const std::map<std::string, std::string>& params,
                         bool need_sign, std::string& out_body) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::map<std::string, std::string> p = params;
    if (need_sign) {
        p["timestamp"] = std::to_string(timestamp_ms());
        if (p.find("recvWindow") == p.end()) p["recvWindow"] = "5000";
    }

    std::string query = build_query(p);
    if (need_sign) {
        std::string sig = hmac_sha256_hex(g_api_secret, query);
        query += "&signature=" + sig;
    }

    std::string url = g_base_url + path;
    std::string postfields;
    struct curl_slist* headers = nullptr;
    if (!g_api_key.empty()) {
        std::string hdr = std::string("X-MBX-APIKEY: ") + g_api_key;
        headers = curl_slist_append(headers, hdr.c_str());
    }

    if (method == "GET") {
        if (!query.empty()) url += std::string("?") + query;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    if (!g_proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, g_proxy.c_str());

    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (method == "POST") {
        postfields = query;
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields.c_str());
    } else if (method == "DELETE") {
        if (!query.empty()) url += std::string("?") + query;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_E("curl error: %s", curl_easy_strerror(res));
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        LOG_W("http status %ld, body=%s", http_code, out_body.c_str());
        // still return true so caller can inspect body
    }
    return true;
}

static bool parse_api_keys_from_config(const char* config_path) {
    if (!config_path) return false;
    ConfigParser cfg;
    if (!cfg.Load(config_path)) return false;

    // 尝试多个位置查找密钥
    std::string api = cfg.Get("keys", "api_key", "");
    std::string sec = cfg.Get("keys", "api_secret", "");
    if (api.empty()) api = cfg.Get("trade", "api_key", "");
    if (sec.empty()) sec = cfg.Get("trade", "api_secret", "");
    if (api.empty()) api = cfg.Get("account", "api_key", "");
    if (sec.empty()) sec = cfg.Get("account", "api_secret", "");

    if (!api.empty() && !sec.empty()) {
        g_api_key = api;
        g_api_secret = sec;
        return true;
    }

    // 若 config.json 存在（与 Python 实现一致），尝试读取
    // 尝试同级目录的 config.json
    std::string cfg_dir = ".";
    std::string cfgs(config_path);
    size_t pos = cfgs.find_last_of("/\\");
    if (pos != std::string::npos) cfg_dir = cfgs.substr(0, pos);
    std::string path1 = cfg_dir + "/config.json";
    std::ifstream ifs(path1);
    if (!ifs.is_open()) {
        // 尝试 trade/config.json
        std::string p2 = cfg_dir + "/trade/config.json";
        ifs.open(p2);
    }
    if (ifs.is_open()) {
        try {
            json j = json::parse(ifs);
            if (j.contains("keys")) {
                auto k = j["keys"];
                if (k.contains("api_key") && k.contains("api_secret")) {
                    g_api_key = k["api_key"].get<std::string>();
                    g_api_secret = k["api_secret"].get<std::string>();
                    return true;
                }
            }
        } catch (...) {}
    }

    // 最后尝试环境变量
    const char* env_key = std::getenv("BINANCE_API_KEY");
    const char* env_secret = std::getenv("BINANCE_API_SECRET");
    if (env_key && env_secret) {
        g_api_key = env_key;
        g_api_secret = env_secret;
        return true;
    }
    return false;
}

TRADE_API int trade_init(const char* config_path) {
    if (g_initialized) return 0;

    // 解析 config.ini 中的 trademode
    if (config_path) {
        ConfigParser cfg;
        if (cfg.Load(config_path)) {
            std::string tm = cfg.Get("trade", "trademode", "future");
            strncpy(g_mode_buf, tm.c_str(), sizeof(g_mode_buf)-1);
            g_mode_buf[sizeof(g_mode_buf)-1] = '\0';
            // 代理配置
            g_proxy = cfg.Get("trade", "proxy", "");
            // try parse api keys from config
            parse_api_keys_from_config(config_path);
        }
    }

    // 如果没有在 config 中找到, 尝试环境变量
    if (g_api_key.empty() || g_api_secret.empty()) {
        const char* k = std::getenv("BINANCE_API_KEY");
        const char* s = std::getenv("BINANCE_API_SECRET");
        if (k && s) {
            g_api_key = k; g_api_secret = s;
        }
    }

    if (g_api_key.empty() || g_api_secret.empty()) {
        LOG_E("API key/secret not found. Set BINANCE_API_KEY/BINANCE_API_SECRET or add to config.");
        return -1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_initialized = true;
    LOG_I("trade_init success, mode=%s", g_mode_buf);
    return 0;
}

TRADE_API void trade_destroy() {
    if (g_initialized) {
        curl_global_cleanup();
        g_initialized = false;
    }
}

static std::string dbl2str(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8) << v;
    std::string s = ss.str();
    // trim trailing zeros
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

static int fill_resp_from_json(TradeOrderResponse* out, const std::string& body) {
    if (!out) return -1;
    memset(out, 0, sizeof(TradeOrderResponse));
    try {
        auto j = json::parse(body);
        if (j.is_object()) {
            if (j.contains("orderId")) {
                out->success = 1;
                // orderId may be number
                if (j["orderId"].is_number()) {
                    long long id = j["orderId"].get<long long>();
                    snprintf(out->order_id, sizeof(out->order_id), "%lld", id);
                } else if (j["orderId"].is_string()) {
                    std::string s = j["orderId"].get<std::string>();
                    strncpy(out->order_id, s.c_str(), sizeof(out->order_id)-1);
                }
            }
            if (j.contains("clientOrderId")) {
                std::string s = j["clientOrderId"].get<std::string>();
                if (out->order_id[0] == '\0') strncpy(out->order_id, s.c_str(), sizeof(out->order_id)-1);
            }
            if (j.contains("status")) out->status[0] = '\0', strncpy(out->status, j["status"].get<std::string>().c_str(), sizeof(out->status)-1);
            if (j.contains("avgPrice")) out->fill_price = std::stod(j["avgPrice"].get<std::string>());
            if (j.contains("executedQty")) out->fill_qty = std::stod(j["executedQty"].get<std::string>());
            if (j.contains("price")) {
                try { double p = std::stod(j["price"].get<std::string>()); if (out->fill_price==0) out->fill_price = p; } catch(...){}
            }
            if (j.contains("msg")) strncpy(out->error_msg, j["msg"].get<std::string>().c_str(), sizeof(out->error_msg)-1);
            if (j.contains("code")) {
                try { int code = j["code"].get<int>(); if (code != 0) out->success = 0; } catch(...){}
            }
            return 0;
        }
    } catch (const std::exception& e) {
        strncpy(out->error_msg, e.what(), sizeof(out->error_msg)-1);
        return -1;
    }
    strncpy(out->error_msg, body.c_str(), sizeof(out->error_msg)-1);
    return -1;
}

TRADE_API int trade_place_order(const TradeOrderRequest* req, TradeOrderResponse* resp) {
    if (!g_initialized || !req || !resp) return -1;
    std::map<std::string, std::string> params;
    params["symbol"] = req->symbol;
    params["side"] = req->side;
    params["type"] = req->order_type;
    if (strlen(req->position_side) > 0) params["positionSide"] = req->position_side;
    if (req->quantity > 0) params["quantity"] = dbl2str(req->quantity);
    if (strcmp(req->order_type, "LIMIT") == 0) {
        params["price"] = dbl2str(req->price);
        params["timeInForce"] = "GTC";
    }
    if (req->recv_window > 0) params["recvWindow"] = std::to_string(req->recv_window);

    std::string body;
    bool ok = http_request("POST", "/fapi/v1/order", params, true, body);
    if (!ok) {
        memset(resp, 0, sizeof(TradeOrderResponse));
        strncpy(resp->error_msg, "http error", sizeof(resp->error_msg)-1);
        return -1;
    }
    fill_resp_from_json(resp, body);
    return (resp->success ? 0 : -1);
}

TRADE_API int trade_cancel_order(const char* symbol, const char* order_id, TradeOrderResponse* resp) {
    if (!g_initialized || !symbol || !order_id || !resp) return -1;
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    // 尝试把 order_id 转为数字
    bool isnum = true;
    for (const char* p = order_id; *p; ++p) if (!(*p>='0' && *p<='9')) { isnum = false; break; }
    if (isnum) params["orderId"] = order_id; else params["origClientOrderId"] = order_id;

    std::string body;
    bool ok = http_request("DELETE", "/fapi/v1/order", params, true, body);
    if (!ok) {
        memset(resp, 0, sizeof(TradeOrderResponse));
        strncpy(resp->error_msg, "http error", sizeof(resp->error_msg)-1);
        return -1;
    }
    // cancel may return status string or JSON
    try {
        auto j = json::parse(body);
        fill_resp_from_json(resp, body);
    } catch(...) {
        // maybe plain string
        memset(resp, 0, sizeof(TradeOrderResponse));
        strncpy(resp->status, body.c_str(), sizeof(resp->status)-1);
        resp->success = 1;
    }
    return (resp->success ? 0 : -1);
}

TRADE_API int trade_query_order(const char* symbol, const char* order_id, TradeOrderResponse* resp) {
    if (!g_initialized || !symbol || !order_id || !resp) return -1;
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    bool isnum = true;
    for (const char* p = order_id; *p; ++p) if (!(*p>='0' && *p<='9')) { isnum = false; break; }
    if (isnum) params["orderId"] = order_id; else params["origClientOrderId"] = order_id;

    std::string body;
    bool ok = http_request("GET", "/fapi/v1/order", params, true, body);
    if (!ok) {
        memset(resp, 0, sizeof(TradeOrderResponse));
        strncpy(resp->error_msg, "http error", sizeof(resp->error_msg)-1);
        return -1;
    }
    fill_resp_from_json(resp, body);
    return (resp->success ? 0 : -1);
}

TRADE_API int trade_place_algo_order(const TradeAlgoRequest* req, TradeOrderResponse* resp) {
    if (!g_initialized || !req || !resp) return -1;
    std::map<std::string, std::string> params;
    params["symbol"] = req->symbol;
    params["side"] = req->side;
    params["type"] = req->algo_type; // e.g. TAKE_PROFIT / STOP
    if (req->quantity > 0) params["quantity"] = dbl2str(req->quantity);
    if (req->price > 0) params["price"] = dbl2str(req->price);
    if (req->trigger_price > 0) params["stopPrice"] = dbl2str(req->trigger_price);
    params["algotype"] = "CONDITIONAL";

    std::string body;
    bool ok = http_request("POST", "/fapi/v1/order", params, true, body);
    if (!ok) {
        memset(resp, 0, sizeof(TradeOrderResponse));
        strncpy(resp->error_msg, "http error", sizeof(resp->error_msg)-1);
        return -1;
    }
    fill_resp_from_json(resp, body);
    return (resp->success ? 0 : -1);
}

TRADE_API int trade_cancel_algo_order(const char* algo_order_id, TradeOrderResponse* resp) {
    if (!g_initialized || !algo_order_id || !resp) return -1;
    std::map<std::string, std::string> params;
    // try both names algoId / algoOrderId
    params["algoId"] = algo_order_id;

    std::string body;
    bool ok = http_request("DELETE", "/fapi/v1/order", params, true, body);
    if (!ok) {
        memset(resp, 0, sizeof(TradeOrderResponse));
        strncpy(resp->error_msg, "http error", sizeof(resp->error_msg)-1);
        return -1;
    }
    fill_resp_from_json(resp, body);
    return (resp->success ? 0 : -1);
}

TRADE_API const char* trade_get_mode() {
    return g_mode_buf;
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    return TRUE;
}
#endif
