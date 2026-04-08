# ============================================================
# 编译器与编译选项
# ============================================================
CXX = g++
# 调试时用 -g 和 -O0，发布实盘时再换回 -O3
# 【新增】 -Wa,-mbig-obj 解决源文件过大、Section 过多的问题
CXXFLAGS = -std=c++17 -Wall -Wextra -fexec-charset=UTF-8 -g -O0 -D_WIN32_WINNT=0x0A00 -Wa,-mbig-obj
DEFINES = -D_WIN32 -DUNICODE -D_UNICODE

# ============================================================
# 目录配置 (保持外层目录精简)
# ============================================================
BIN_DIR = bin
HEAD_DIR = head

# 第三方库路径配置 (⚠️ 请务必根据你的实际路径修改)
PYTHON_ROOT = C:/Users/liuyuandong/AppData/Local/Programs/Python
PG_ROOT = C:/Program Files/PostgreSQL/16

# ============================================================
# 头文件与库查找路径
# ============================================================
# 新增项目自身的通用头文件目录
INCLUDES += -I"$(HEAD_DIR)"
INCLUDES += -I"D:/msys64/mingw64/include"
INCLUDES += -I"C:/Users/liuyuandong/AppData/Local/Programs/Python/Python310/include"

LDFLAGS += -L"D:/msys64/mingw64/lib"
LDFLAGS += -L"C:/Users/liuyuandong/AppData/Local/Programs/Python/Python310/libs"

# 需要链接的具体库文件
LIBS = -lpython310 -lpq
SQLITE_FLAGS = -lsqlite3
CURL_FLAGS = -lcurl -lws2_32

# ============================================================
# 目标与源文件 (所有目标文件输出至 bin/ 目录)
# ============================================================
TARGET_ENGINE = $(BIN_DIR)/ppq_engine.exe
SRC_ENGINE = ppq_main/ppq_main.cpp

TARGET_REPORT_DLL = $(BIN_DIR)/html_report_dll.dll
SRC_REPORT_DLL = html_report/html_report_dll_main.cpp

TARGET_TRADE_DLL = $(BIN_DIR)/trade_dll.dll
SRC_TRADE_DLL = trade/trade_dll_main.cpp

TARGET_SERVER = $(BIN_DIR)/market_server.exe
SRC_SERVER = market_server/market_server_main.cpp

# ============================================================
# 编译规则
# ============================================================
.PHONY: all clean copy_files create_dirs

# 默认输入 make 时，创建目录、编译所有目标并拷贝依赖
all: create_dirs $(TARGET_REPORT_DLL) $(TARGET_TRADE_DLL) $(TARGET_ENGINE) $(TARGET_SERVER) copy_files

# 编译交易 DLL（pure C++，使用 libcurl + openssl）
$(TARGET_TRADE_DLL): $(SRC_TRADE_DLL) $(HEAD_DIR)/trade_dll_interface.h $(HEAD_DIR)/config_parser.h third_party/json.hpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -shared -DTRADE_DLL_EXPORTS $(SRC_TRADE_DLL) -o $@ $(LDFLAGS) -lcurl -lssl -lcrypto -lws2_32

# 创建输出目录
create_dirs:
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)

# 编译 HTML 报告 DLL（依赖项已更新为 head 目录）
$(TARGET_REPORT_DLL): $(SRC_REPORT_DLL) $(HEAD_DIR)/html_report_dll_interface.h
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -shared -DHTML_REPORT_DLL_EXPORTS $(SRC_REPORT_DLL) -o $@

# 编译主程序 ppq_engine
$(TARGET_ENGINE): $(SRC_ENGINE)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $^ -o $@ $(LDFLAGS) $(LIBS) $(SQLITE_FLAGS) $(CURL_FLAGS)

# 编译行情服务器 market_server
$(TARGET_SERVER): $(SRC_SERVER)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $^ -o $@ $(LDFLAGS) $(LIBS) $(SQLITE_FLAGS) $(CURL_FLAGS)

# ============================================================
# 辅助规则：拷贝 DLL 及策略脚本到 bin 目录
# ============================================================
copy_files:
	@echo "Copying required files to bin directory..."
	@if exist "$(PYTHON_ROOT)\python3*.dll" copy /Y "$(PYTHON_ROOT)\python3*.dll" $(BIN_DIR)\ > nul
	@if exist "$(PG_ROOT)\bin\libpq.dll" copy /Y "$(PG_ROOT)\bin\libpq.dll" $(BIN_DIR)\ > nul
	@if exist "$(PG_ROOT)\bin\libssl-*.dll" copy /Y "$(PG_ROOT)\bin\libssl-*.dll" $(BIN_DIR)\ > nul
	@if exist "$(PG_ROOT)\bin\libcrypto-*.dll" copy /Y "$(PG_ROOT)\bin\libcrypto-*.dll" $(BIN_DIR)\ > nul
	@if exist "ppq_main\ElderRayStrategy.py" copy /Y "ppq_main\ElderRayStrategy.py" $(BIN_DIR)\ > nul

# 清理编译产物
clean:
	@echo "Cleaning up..."
	@if exist $(BIN_DIR) rmdir /S /Q $(BIN_DIR)