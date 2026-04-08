#pragma once
// python_bridge.h — Python 策略桥接器
// 嵌入 CPython 解释器，调用 Python 策略的 generate_signal 函数
// C++ 负责指标计算，Python 只做策略逻辑判断（无需 import pandas/numpy）
//
// 编译: 需链接 python3.lib, 需设置 PYTHONHOME/PYTHONPATH
// Windows: python3x.lib (如 python312.lib)

#include <string>
#include <vector>
#include <map>

// Python.h 必须在所有标准库之前包含

#include <Python.h>


#include "order.h"
#include "account.h"
#include "indicator_engine.h"
#include "logger.h"

class PythonBridge {
public:
    PythonBridge() = default;
    ~PythonBridge() { Shutdown(); }

    // ---------- 初始化/关闭 ----------
    bool Init(const std::string& script_dir) {
        if (initialized_) return true;

        // 设置 Python 路径
        Py_SetProgramName(L"ppq_engine");

        // 初始化 Python 解释器
        Py_Initialize();
        if (!Py_IsInitialized()) {
            LOG_E("[PythonBridge] Failed to initialize Python");
            return false;
        }

        // 添加脚本搜索路径
        PyObject* sys_path = PySys_GetObject("path");
        if (sys_path) {
            std::wstring wdir(script_dir.begin(), script_dir.end());
            PyObject* path_str = PyUnicode_FromWideChar(wdir.c_str(), -1);
            PyList_Append(sys_path, path_str);
            Py_DECREF(path_str);
        }

        initialized_ = true;
        LOG_I("[PythonBridge] Python %s initialized, path: %s",
              Py_GetVersion(), script_dir.c_str());
        return true;
    }

    void Shutdown() {
        // 释放所有已加载模块
        for (auto& [name, mod] : modules_) {
            Py_XDECREF(mod);
        }
        modules_.clear();

        if (initialized_) {
            Py_Finalize();
            initialized_ = false;
            LOG_I("[PythonBridge] Python shutdown");
        }
    }

    // ---------- 加载策略模块 ----------
    bool LoadStrategy(const std::string& module_name) {
        if (!initialized_) return false;

        PyObject* mod = PyImport_ImportModule(module_name.c_str());
        if (!mod) {
            PyErr_Print();
            LOG_E("[PythonBridge] Failed to import '%s'", module_name.c_str());
            return false;
        }

        modules_[module_name] = mod;
        current_module_ = module_name;
        LOG_I("[PythonBridge] Loaded strategy: %s", module_name.c_str());
        return true;
    }

    // ---------- 调用策略生成信号 ----------
    // 核心接口：将 C++ 计算好的指标、K线数据、账户状态传给 Python 策略
    // Python 策略返回 SignalData 列表
    std::vector<SignalData> GenerateSignal(
        const std::string& pair,
        const std::vector<Bar>& bars,
        const IndicatorCache& indicators,
        const Account& account,
        const TradingParameters& params)
    {
        std::vector<SignalData> result;

        if (modules_.find(current_module_) == modules_.end()) {
            LOG_E("[PythonBridge] Strategy '%s' not loaded", current_module_.c_str());
            return result;
        }

        PyObject* mod = modules_[current_module_];

        // 获取 generate_signal 函数
        PyObject* func = PyObject_GetAttrString(mod, "generate_signal");
        if (!func || !PyCallable_Check(func)) {
            LOG_E("[PythonBridge] 'generate_signal' not found in %s", current_module_.c_str());
            Py_XDECREF(func);
            return result;
        }

        // 构建参数字典传给 Python
        PyObject* args_dict = BuildArgsDict(pair, bars, indicators, account, params);

        // 调用 generate_signal(args)
        PyObject* py_result = PyObject_CallFunctionObjArgs(func, args_dict, nullptr);
        Py_DECREF(func);
        Py_DECREF(args_dict);

        if (!py_result) {
            PyErr_Print();
            LOG_E("[PythonBridge] generate_signal() failed");
            return result;
        }

        // 解析返回值：应为 list of tuples
        result = ParseSignalList(py_result);
        Py_DECREF(py_result);

        return result;
    }

private:
    bool initialized_ = false;
    std::string current_module_;
    std::map<std::string, PyObject*> modules_;

    // ---------- 构建传给 Python 的参数字典 ----------
    PyObject* BuildArgsDict(
        const std::string& pair,
        const std::vector<Bar>& bars,
        const IndicatorCache& ind,
        const Account& account,
        const TradingParameters& params)
    {
        PyObject* d = PyDict_New();
        int n = ind.Size();
        int last = n - 1;

        // 基本信息
        PyDict_SetItemString(d, "pair", PyUnicode_FromString(pair.c_str()));
        PyDict_SetItemString(d, "bar_count", PyLong_FromLong(n));

        // 最新K线
        if (!bars.empty()) {
            const Bar& b = bars.back();
            PyDict_SetItemString(d, "timestamp", PyLong_FromLongLong(b.timestamp));
            PyDict_SetItemString(d, "open",  PyFloat_FromDouble(b.open));
            PyDict_SetItemString(d, "high",  PyFloat_FromDouble(b.high));
            PyDict_SetItemString(d, "low",   PyFloat_FromDouble(b.low));
            PyDict_SetItemString(d, "close", PyFloat_FromDouble(b.close));
            PyDict_SetItemString(d, "volume", PyFloat_FromDouble(b.volume));
        }

        // 指标（最新值）
        PyDict_SetItemString(d, "ema13",      PyFloat_FromDouble(ind.EMA13(last)));
        PyDict_SetItemString(d, "rsi6",       PyFloat_FromDouble(ind.RSI6(last)));
        PyDict_SetItemString(d, "rsi9",       PyFloat_FromDouble(ind.RSI9(last)));
        PyDict_SetItemString(d, "sma10",      PyFloat_FromDouble(ind.SMA10(last)));
        PyDict_SetItemString(d, "sma34",      PyFloat_FromDouble(ind.SMA34(last)));
        PyDict_SetItemString(d, "sar",        PyFloat_FromDouble(ind.SAR(last)));
        PyDict_SetItemString(d, "bull_power", PyFloat_FromDouble(ind.BullPower(last)));
        PyDict_SetItemString(d, "bear_power", PyFloat_FromDouble(ind.BearPower(last)));
        PyDict_SetItemString(d, "avg_volume", PyFloat_FromDouble(ind.AvgVol(last)));
        PyDict_SetItemString(d, "hhv60",      PyFloat_FromDouble(ind.HHV60(last)));
        PyDict_SetItemString(d, "llv60",      PyFloat_FromDouble(ind.LLV60(last)));
        PyDict_SetItemString(d, "macd_dif",   PyFloat_FromDouble(ind.MACD().dif[last]));
        PyDict_SetItemString(d, "macd_dea",   PyFloat_FromDouble(ind.MACD().dea[last]));
        PyDict_SetItemString(d, "macd_hist",  PyFloat_FromDouble(ind.MACD().hist[last]));
        PyDict_SetItemString(d, "boll_upper", PyFloat_FromDouble(ind.Boll().upper[last]));
        PyDict_SetItemString(d, "boll_mid",   PyFloat_FromDouble(ind.Boll().mid[last]));
        PyDict_SetItemString(d, "boll_lower", PyFloat_FromDouble(ind.Boll().lower[last]));

        // 指标历史数组
        PyObject* close_list = PyList_New(n);
        PyObject* ema13_list = PyList_New(n);
        PyObject* rsi6_list  = PyList_New(n);
        PyObject* vol_list   = PyList_New(n);
        PyObject* bull_power_list = PyList_New(n);
        PyObject* bear_power_list = PyList_New(n);
        PyObject* high_list  = PyList_New(n);
        PyObject* low_list   = PyList_New(n);
        for (int i = 0; i < n; ++i) {
            int idx = n - n + i;
            PyList_SET_ITEM(close_list, i, PyFloat_FromDouble(ind.Close(idx)));
            PyList_SET_ITEM(ema13_list, i, PyFloat_FromDouble(ind.EMA13(idx)));
            PyList_SET_ITEM(rsi6_list,  i, PyFloat_FromDouble(ind.RSI6(idx)));
            PyList_SET_ITEM(vol_list,   i, PyFloat_FromDouble(ind.AvgVol(idx)));
            PyList_SET_ITEM(bull_power_list, i, PyFloat_FromDouble(ind.BullPower(idx)));
            PyList_SET_ITEM(bear_power_list, i, PyFloat_FromDouble(ind.BearPower(idx)));
            PyList_SET_ITEM(high_list,  i, PyFloat_FromDouble(ind.High(idx)));
            PyList_SET_ITEM(low_list,   i, PyFloat_FromDouble(ind.Low(idx)));
        }
        PyDict_SetItemString(d, "close_history", close_list);
        PyDict_SetItemString(d, "ema13_history", ema13_list);
        PyDict_SetItemString(d, "rsi6_history",  rsi6_list);
        PyDict_SetItemString(d, "vol_history",   vol_list);
        PyDict_SetItemString(d, "bull_power_history", bull_power_list);
        PyDict_SetItemString(d, "bear_power_history", bear_power_list);
        PyDict_SetItemString(d, "high_history",  high_list);
        PyDict_SetItemString(d, "low_history",   low_list);

        // 账户状态
        PyDict_SetItemString(d, "capital", PyFloat_FromDouble(account.CurrentCapital()));

        // 持仓信息
        const auto& orders = account.Orders();
        PyObject* longs_list = PyList_New(0);
        PyObject* shorts_list = PyList_New(0);
        for (auto& o : orders) {
            if (o.matchstatus != MATCH_FILLED) continue;
            PyObject* od = PyDict_New();
            PyDict_SetItemString(od, "id",       PyLong_FromLong(o.id));
            PyDict_SetItemString(od, "price",    PyFloat_FromDouble(o.price));
            PyDict_SetItemString(od, "quantity", PyFloat_FromDouble(o.quantity));
            PyDict_SetItemString(od, "remark",   PyUnicode_FromString(o.remark.c_str()));
            PyDict_SetItemString(od, "take_profit_price", PyFloat_FromDouble(o.take_profit_price));
            PyDict_SetItemString(od, "stop_loss_price",   PyFloat_FromDouble(o.stop_loss_price));
            if (o.type == Operate::LO) PyList_Append(longs_list, od);
            else if (o.type == Operate::SO) PyList_Append(shorts_list, od);
            Py_DECREF(od);
        }
        PyDict_SetItemString(d, "long_positions",  longs_list);
        PyDict_SetItemString(d, "short_positions", shorts_list);

        // 交易参数
        PyDict_SetItemString(d, "take_profit_ratio", PyFloat_FromDouble(params.take_profit_ratio));
        PyDict_SetItemString(d, "stop_loss_ratio",   PyFloat_FromDouble(params.stop_loss_ratio));
        PyDict_SetItemString(d, "order_amount",      PyFloat_FromDouble(params.czzy_signal_count));

        return d;
    }

    // ---------- 解析 Python 返回的信号列表 ----------
    // 期望格式: [(strength, op_str, order_type, qty, id, remark, price, tp, sl), ...]
    std::vector<SignalData> ParseSignalList(PyObject* py_list) {
        std::vector<SignalData> result;
        if (!PyList_Check(py_list)) return result;

        Py_ssize_t size = PyList_Size(py_list);
        for (Py_ssize_t i = 0; i < size; ++i) {
            PyObject* item = PyList_GetItem(py_list, i);
            if (!PyTuple_Check(item) || PyTuple_Size(item) < 9) continue;

            SignalData sig;
            sig.signal_strength = PyFloat_AsDouble(PyTuple_GetItem(item, 0));

            // 操作类型：接受字符串 "LO"/"LE"/"SO"/"SE" 或中文
            PyObject* op_obj = PyTuple_GetItem(item, 1);
            if (PyUnicode_Check(op_obj)) {
                const char* op_str = PyUnicode_AsUTF8(op_obj);
                sig.order_op = StrToOperate(op_str);
            }

            sig.order_type = (int)PyLong_AsLong(PyTuple_GetItem(item, 2));
            sig.qty        = PyFloat_AsDouble(PyTuple_GetItem(item, 3));
            sig.id         = (int)PyLong_AsLong(PyTuple_GetItem(item, 4));

            PyObject* remark_obj = PyTuple_GetItem(item, 5);
            if (PyUnicode_Check(remark_obj))
                sig.remark = PyUnicode_AsUTF8(remark_obj);

            sig.price    = PyFloat_AsDouble(PyTuple_GetItem(item, 6));
            sig.tp_price = PyFloat_AsDouble(PyTuple_GetItem(item, 7));
            sig.sl_price = PyFloat_AsDouble(PyTuple_GetItem(item, 8));

            result.push_back(sig);
        }
        return result;
    }
};
