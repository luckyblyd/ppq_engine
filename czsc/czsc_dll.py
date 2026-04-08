"""
czsc_dll.py — CZSC DLL 内部的 Python 桥接
被 czsc_dll_main.cpp 内嵌的 Python 解释器加载
提供 init_trader / update_bar / get_signals 等函数
"""
import sys
import os

# 确保路径正确
_script_dir = os.path.dirname(os.path.abspath(__file__))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

from czsc import RawBar, BarGenerator, CzscTrader
from czsc.objects import Freq
from czsc_ppq_strategy import AdvancedPpqCzscStrategy
from datetime import datetime

# 导入策略中的信号函数
from 高级策略_v20251201 import (
    cxt_bi_trend_V20251228,
    coo_sar_V251228,
    bar_triple_V251228,
    tas_rsi_base_V251228,
    vol_ti_suo_V251228,
    tas_macd_bs1_V260101,
    bar_single_V260104,
    bar_window_zcyl_V251230,
)

# 全局状态：每个symbol一个trader
_traders = {}       # symbol -> CzscTrader
_strategies = {}    # symbol -> AdvancedPpqCzscStrategy
_base_freq_str = "5分钟"

_FREQ_MAP = {
    "5m": "5分钟", "15m": "15分钟", "1h": "1小时",
    "4h": "4小时", "1d": "日线",
}


def set_base_freq(freq_str):
    global _base_freq_str
    _base_freq_str = _FREQ_MAP.get(freq_str, freq_str)


def init_trader(symbol, bars_data):
    """
    初始化trader
    bars_data: list of dict, 每个dict含 timestamp,open,high,low,close,volume,amount,timeframe
    """
    global _traders, _strategies, _base_freq_str

    freq_str = _base_freq_str
    freq_lookup = {f.value: f for f in Freq}
    freq = freq_lookup.get(freq_str, Freq.F5)

    raw_bars = []
    for b in bars_data:
        ts = b["timestamp"]
        dt = datetime.fromtimestamp(ts / 1000) if ts > 1e12 else datetime.fromtimestamp(ts)
        rb = RawBar(
            symbol=symbol, id=ts, dt=dt, freq=freq,
            open=b["open"], close=b["close"],
            high=b["high"], low=b["low"],
            vol=b["volume"],
            amount=b.get("amount", b["volume"] * b["close"]),
        )
        raw_bars.append(rb)

    strategy = AdvancedPpqCzscStrategy(symbol=symbol, base_freq=freq_str)
    bg, init_bars = strategy.init_bar_generator(raw_bars)
    trader = CzscTrader(bg=bg, signals_config=strategy.signals_config)

    _traders[symbol] = trader
    _strategies[symbol] = strategy
    return 0


def update_bar(symbol, bar_dict):
    """更新单根K线"""
    trader = _traders.get(symbol)
    if not trader:
        return -1

    freq_lookup = {f.value: f for f in Freq}
    freq = freq_lookup.get(_base_freq_str, Freq.F5)

    ts = bar_dict["timestamp"]
    dt = datetime.fromtimestamp(ts / 1000) if ts > 1e12 else datetime.fromtimestamp(ts)

    rb = RawBar(
        symbol=symbol, id=ts, dt=dt, freq=freq,
        open=bar_dict["open"], close=bar_dict["close"],
        high=bar_dict["high"], low=bar_dict["low"],
        vol=bar_dict["volume"],
        amount=bar_dict.get("amount", bar_dict["volume"] * bar_dict["close"]),
    )
    trader.on_bar(rb)
    return 0


def get_signals(symbol):
    """获取所有CZSC信号，返回dict"""
    trader = _traders.get(symbol)
    if not trader:
        return None

    base_freq = _base_freq_str
    kas = trader.kas
    s = trader.s if hasattr(trader, 's') else {}

    result = {}

    try:
        # 5分钟趋势
        qushi = cxt_bi_trend_V20251228(kas[base_freq], **s).get("v3", "震荡趋势")
        result["trend_5m_str"] = qushi
        result["trend_5m"] = 1 if qushi == "上涨趋势" else (2 if qushi == "下跌趋势" else 0)

        # 60分钟趋势
        s_copy = dict(s)
        s_copy['n'] = 1
        big_freq = "60分钟"
        if big_freq in kas:
            qushibig = cxt_bi_trend_V20251228(kas[big_freq], **s_copy).get("v3", "震荡趋势")
        else:
            qushibig = qushi
        result["trend_60m_str"] = qushibig
        result["trend_60m"] = 1 if qushibig == "上涨趋势" else (2 if qushibig == "下跌趋势" else 0)

        # SAR
        coo_sar = coo_sar_V251228(kas[base_freq], **s).get("v1", "")
        result["sar_direction"] = 1 if coo_sar == "多头" else 0

        # RSI
        rsi = tas_rsi_base_V251228(kas[base_freq], **s).get("v3", "其他")
        result["rsi_signal"] = rsi or "其他"

        # 三K形态
        s3k = bar_triple_V251228(kas[base_freq], **s).get("v3", "")
        result["s3k_signal"] = s3k or ""

        # 梯量缩量
        volti = vol_ti_suo_V251228(kas[base_freq], **s).get("v3", "")
        result["vol_signal"] = volti or ""

        # 支撑压力位（60分钟）
        s_zcyl = dict(s)
        s_zcyl['n'] = 10
        s_zcyl['di'] = 2
        if big_freq in kas:
            sig_zcyl = bar_window_zcyl_V251230(kas[big_freq], **s_zcyl)
            try:
                result["hhyl"] = float(str(sig_zcyl.get("v2", "0")).split(",")[-1])
                result["ddzc"] = float(str(sig_zcyl.get("v3", "0")).split(",")[-1])
            except:
                result["hhyl"] = 0
                result["ddzc"] = 0
        else:
            result["hhyl"] = 0
            result["ddzc"] = 0

        # 5分钟支撑压力位
        s_5m = dict(s)
        s_5m['di'] = 5
        s_5m['n'] = 10
        sig_5m = bar_window_zcyl_V251230(kas[base_freq], **s_5m)
        try:
            result["yl_5m"] = float(str(sig_5m.get("v2", "0")).split(",")[0])
            result["zc_5m"] = float(str(sig_5m.get("v3", "0")).split(",")[0])
        except:
            result["yl_5m"] = 0
            result["zc_5m"] = 0

        # MACD信号
        macd_sig = tas_macd_bs1_V260101(kas[base_freq], **s)
        result["macd_cross"] = macd_sig.get("v2", "") or ""
        result["macd_cross_dist"] = 0
        if macd_sig.get("v3") == "触发金叉或死叉":
            result["macd_cross_dist"] = 1

        # 单K线形态
        bsdg = bar_single_V260104(kas[base_freq], **s).get("v2", "")
        result["bar_pattern"] = bsdg or ""

    except Exception as e:
        result["error"] = str(e)

    return result
