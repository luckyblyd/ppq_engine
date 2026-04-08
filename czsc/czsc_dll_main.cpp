// czsc_dll_main.cpp — CZSC DLL 的 C++ 实现
// 编译: cl /LD /utf-8 czsc_dll_main.cpp /I"Python310\include"
//       /link /LIBPATH:"Python310\libs" python310.lib /OUT:czsc_dll.dll
// 定义 CZSC_DLL_EXPORTS 以导出函数

#define CZSC_DLL_EXPORTS

#ifdef _DEBUG
  #undef _DEBUG
  #include <Python.h>
  #define _DEBUG
#else
  #include <Python.h>
#endif

#include "czsc_dll_interface.h"
#include <cstring>
#include <cstdio>

static PyObject* g_module = nullptr;
static bool g_initialized = false;

CZSC_API int czsc_init(const char* python_home, const char* script_dir,
                       const char* base_freq) {
    if (g_initialized) return 0;

    Py_Initialize();
    if (!Py_IsInitialized()) return -1;

    // 添加脚本路径
    PyObject* sys_path = PySys_GetObject("path");
    if (sys_path && script_dir) {
        PyObject* p = PyUnicode_FromString(script_dir);
        PyList_Append(sys_path, p);
        Py_DECREF(p);
    }

    // 加载 czsc_dll 模块
    g_module = PyImport_ImportModule("czsc_dll");
    if (!g_module) {
        PyErr_Print();
        return -1;
    }

    // 设置 base_freq
    if (base_freq) {
        PyObject* func = PyObject_GetAttrString(g_module, "set_base_freq");
        if (func) {
            PyObject* args = PyTuple_Pack(1, PyUnicode_FromString(base_freq));
            PyObject* ret = PyObject_CallObject(func, args);
            Py_XDECREF(ret);
            Py_DECREF(args);
            Py_DECREF(func);
        }
    }

    g_initialized = true;
    return 0;
}

CZSC_API void czsc_destroy() {
    Py_XDECREF(g_module);
    g_module = nullptr;
    if (g_initialized) {
        Py_Finalize();
        g_initialized = false;
    }
}

// 将 CzscBar 数组转为 Python list of dict
static PyObject* bars_to_pylist(const CzscBar* bars, int count) {
    PyObject* lst = PyList_New(count);
    for (int i = 0; i < count; ++i) {
        PyObject* d = PyDict_New();
        PyDict_SetItemString(d, "timestamp", PyLong_FromLongLong(bars[i].timestamp));
        PyDict_SetItemString(d, "open",    PyFloat_FromDouble(bars[i].open));
        PyDict_SetItemString(d, "high",    PyFloat_FromDouble(bars[i].high));
        PyDict_SetItemString(d, "low",     PyFloat_FromDouble(bars[i].low));
        PyDict_SetItemString(d, "close",   PyFloat_FromDouble(bars[i].close));
        PyDict_SetItemString(d, "volume",  PyFloat_FromDouble(bars[i].volume));
        PyDict_SetItemString(d, "amount",  PyFloat_FromDouble(bars[i].amount));
        PyDict_SetItemString(d, "timeframe", PyUnicode_FromString(bars[i].timeframe));
        PyList_SET_ITEM(lst, i, d);
    }
    return lst;
}

CZSC_API int czsc_init_trader(const char* symbol, const CzscBar* bars, int count) {
    if (!g_module || !symbol) return -1;

    PyObject* func = PyObject_GetAttrString(g_module, "init_trader");
    if (!func) return -1;

    PyObject* py_symbol = PyUnicode_FromString(symbol);
    PyObject* py_bars = bars_to_pylist(bars, count);
    PyObject* ret = PyObject_CallFunctionObjArgs(func, py_symbol, py_bars, nullptr);

    int result = -1;
    if (ret) { result = (int)PyLong_AsLong(ret); Py_DECREF(ret); }
    else PyErr_Print();

    Py_DECREF(py_bars);
    Py_DECREF(py_symbol);
    Py_DECREF(func);
    return result;
}

CZSC_API int czsc_update_bar(const char* symbol, const CzscBar* bar) {
    if (!g_module || !symbol || !bar) return -1;

    PyObject* func = PyObject_GetAttrString(g_module, "update_bar");
    if (!func) return -1;

    PyObject* py_symbol = PyUnicode_FromString(symbol);
    PyObject* d = PyDict_New();
    PyDict_SetItemString(d, "timestamp", PyLong_FromLongLong(bar->timestamp));
    PyDict_SetItemString(d, "open",    PyFloat_FromDouble(bar->open));
    PyDict_SetItemString(d, "high",    PyFloat_FromDouble(bar->high));
    PyDict_SetItemString(d, "low",     PyFloat_FromDouble(bar->low));
    PyDict_SetItemString(d, "close",   PyFloat_FromDouble(bar->close));
    PyDict_SetItemString(d, "volume",  PyFloat_FromDouble(bar->volume));
    PyDict_SetItemString(d, "amount",  PyFloat_FromDouble(bar->amount));

    PyObject* ret = PyObject_CallFunctionObjArgs(func, py_symbol, d, nullptr);
    int result = ret ? (int)PyLong_AsLong(ret) : -1;
    if (!ret) PyErr_Print();

    Py_XDECREF(ret);
    Py_DECREF(d);
    Py_DECREF(py_symbol);
    Py_DECREF(func);
    return result;
}

static void safe_copy(char* dst, int dst_size, PyObject* dict, const char* key) {
    PyObject* val = PyDict_GetItemString(dict, key);
    if (val && PyUnicode_Check(val)) {
        const char* s = PyUnicode_AsUTF8(val);
        strncpy(dst, s ? s : "", dst_size - 1);
        dst[dst_size - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

static double safe_double(PyObject* dict, const char* key, double def_val) {
    PyObject* val = PyDict_GetItemString(dict, key);
    if (val && PyFloat_Check(val)) return PyFloat_AsDouble(val);
    if (val && PyLong_Check(val)) return (double)PyLong_AsLong(val);
    return def_val;
}

static int safe_int(PyObject* dict, const char* key, int def_val) {
    PyObject* val = PyDict_GetItemString(dict, key);
    if (val && PyLong_Check(val)) return (int)PyLong_AsLong(val);
    return def_val;
}

CZSC_API int czsc_get_signals(const char* symbol, CzscSignals* out) {
    if (!g_module || !symbol || !out) return -1;
    memset(out, 0, sizeof(CzscSignals));

    PyObject* func = PyObject_GetAttrString(g_module, "get_signals");
    if (!func) return -1;

    PyObject* py_symbol = PyUnicode_FromString(symbol);
    PyObject* ret = PyObject_CallFunctionObjArgs(func, py_symbol, nullptr);
    Py_DECREF(py_symbol);
    Py_DECREF(func);

    if (!ret || ret == Py_None) {
        Py_XDECREF(ret);
        return -1;
    }

    out->trend_5m  = safe_int(ret, "trend_5m", 0);
    out->trend_60m = safe_int(ret, "trend_60m", 0);
    safe_copy(out->trend_5m_str,  sizeof(out->trend_5m_str),  ret, "trend_5m_str");
    safe_copy(out->trend_60m_str, sizeof(out->trend_60m_str), ret, "trend_60m_str");
    out->sar_direction = safe_int(ret, "sar_direction", 0);
    safe_copy(out->rsi_signal,  sizeof(out->rsi_signal),  ret, "rsi_signal");
    safe_copy(out->s3k_signal,  sizeof(out->s3k_signal),  ret, "s3k_signal");
    safe_copy(out->vol_signal,  sizeof(out->vol_signal),  ret, "vol_signal");
    out->hhyl  = safe_double(ret, "hhyl", 0);
    out->ddzc  = safe_double(ret, "ddzc", 0);
    out->yl_5m = safe_double(ret, "yl_5m", 0);
    out->zc_5m = safe_double(ret, "zc_5m", 0);
    safe_copy(out->macd_cross,  sizeof(out->macd_cross),  ret, "macd_cross");
    out->macd_cross_dist = safe_int(ret, "macd_cross_dist", 0);
    safe_copy(out->bar_pattern, sizeof(out->bar_pattern), ret, "bar_pattern");

    Py_DECREF(ret);
    return 0;
}

CZSC_API int czsc_get_trend(const char* symbol, int* trend_5m, int* trend_60m) {
    CzscSignals sig;
    if (czsc_get_signals(symbol, &sig) != 0) return -1;
    if (trend_5m)  *trend_5m  = sig.trend_5m;
    if (trend_60m) *trend_60m = sig.trend_60m;
    return 0;
}

CZSC_API int czsc_get_support_resistance(const char* symbol,
                                          double* hhyl, double* ddzc,
                                          double* yl_5m, double* zc_5m) {
    CzscSignals sig;
    if (czsc_get_signals(symbol, &sig) != 0) return -1;
    if (hhyl)  *hhyl  = sig.hhyl;
    if (ddzc)  *ddzc  = sig.ddzc;
    if (yl_5m) *yl_5m = sig.yl_5m;
    if (zc_5m) *zc_5m = sig.zc_5m;
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    return TRUE;
}
