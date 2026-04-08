# -*- coding: utf-8 -*-
"""
CZSC 精简版 - 仅保留笔/分型计算核心 + 策略框架最小集
"""
from czsc.analyze import CZSC
from czsc.objects import Freq, Operate, Direction, Signal, Factor, Event, RawBar, NewBar, Position, ZS
from czsc.strategies import CzscStrategyBase, CzscJsonStrategy
from czsc.traders import CzscTrader, CzscSignals
from czsc.traders.sig_parse import get_signals_config, get_signals_freqs, SignalsParser
from czsc.utils import (
    BarGenerator, freq_end_time, resample_bars,
    is_trading_time, get_intraday_times, check_freq_and_market,
    read_json, save_json, get_sub_elements, freqs_sorted,
    x_round, import_by_name, home_path, get_dir_size, empty_cache_path,
)

__version__ = "0.9.67-slim"
__author__ = "zengbin93"
