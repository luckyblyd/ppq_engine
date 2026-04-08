// ppq_main.cpp — 量化交易系统主入口
// 用法:
//   ppq_engine.exe backtest          回测模式
//   ppq_engine.exe live              实盘模式
//   ppq_engine.exe backtest -c path  指定配置文件
//
// 编译依赖: libpq, python3, C++17
// Windows: cl /std:c++17 /EHsc ppq_main.cpp /I"C:\Python312\include"
//          /link /LIBPATH:"C:\Python312\libs" python312.lib libpq.lib

#include <cstdio>
#include <cstring>
#include <string>

#include "trading_engine.h"
#include "logger.h"

#ifdef _WIN32
#include <conio.h>   // 只有 Windows 需要这个，提供 _getch()
#endif

static void PrintUsage(const char* prog) {
    printf("PPQ Quantitative Trading Engine v2.0\n");
    printf("====================================\n");
    printf("Usage:\n");
    printf("  %s backtest [-c config.ini]    Run backtest\n", prog);
    printf("  %s live     [-c config.ini]    Run live trading\n", prog);
    printf("  %s help                        Show this help\n", prog);
    printf("\nDefault config: config.ini\n");
}

int main(int argc, char* argv[]) {
    // 初始化日志
    LogInit(LOG_LV_INFO);

    // 解析命令行参数
    std::string mode = "backtest";
    std::string config_path = "config.ini";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "backtest" || arg == "bt") {
            mode = "backtest";
        } else if (arg == "live" || arg == "trade") {
            mode = "live";
        } else if (arg == "-c" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "help" || arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    LOG_I("============================================");
    LOG_I("PPQ Quantitative Trading Engine v2.0");
    LOG_I("Mode: %s  Config: %s", mode.c_str(), config_path.c_str());
    LOG_I("============================================");

    // 创建交易引擎
    TradingEngine engine;

    if (!engine.Init(config_path)) {
        LOG_E("Engine initialization failed!");
        return 1;
    }

    // 运行
    if (engine.mode_ == "backtest") {
        engine.RunBacktest();
    } else if (engine.mode_ == "live") {
        engine.RunLive();
    } else {
        LOG_E("Unknown mode: %s", engine.mode_.c_str());
        PrintUsage(argv[0]);
        return 1;
    }

    LOG_I("程序结束，请按任意键退出！");
#if defined(_WIN32) || defined(_WIN64)
    fflush(stdout);                   // 必须刷新缓冲区
    _getch();                         // 任意键响应
#else
    // Linux / macOS
    printf("按回车键退出...\n");
    fflush(stdout);
    getchar();
#endif

return 0;
}
