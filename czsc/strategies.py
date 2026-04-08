# -*- coding: utf-8 -*-
"""
策略基类 - 精简版
仅保留 CzscStrategyBase 和 CzscJsonStrategy
移除所有示例策略（create_single_ma_long/short, create_macd_*, create_cci_*, create_emv_*, create_third_*）
"""
import os
import hashlib
import pandas as pd
from copy import deepcopy
from datetime import datetime
from abc import ABC, abstractmethod
from loguru import logger
from czsc.objects import RawBar, List, Operate, Signal, Factor, Event, Position
from czsc.traders.base import CzscTrader
from czsc.traders.sig_parse import get_signals_freqs, get_signals_config
from czsc.utils import freqs_sorted, BarGenerator, save_json, read_json
from czsc.utils import check_freq_and_market


class CzscStrategyBase(ABC):
    """择时交易策略基类"""

    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.signals_module_name = kwargs.get("signals_module_name", "")

    @property
    def symbol(self):
        return self.kwargs["symbol"]

    @property
    def unique_signals(self):
        sig_seq = []
        for pos in self.positions:
            sig_seq.extend(pos.unique_signals)
        return list(set(sig_seq))

    @property
    def signals_config(self):
        return get_signals_config(self.unique_signals, self.signals_module_name)

    @property
    def freqs(self):
        return get_signals_freqs(self.unique_signals)

    @property
    def sorted_freqs(self):
        return freqs_sorted(self.freqs)

    @property
    def base_freq(self):
        return self.sorted_freqs[0]

    @abstractmethod
    def positions(self) -> List[Position]:
        raise NotImplementedError

    def init_bar_generator(self, bars: List[RawBar], **kwargs):
        """使用策略定义初始化 BarGenerator"""
        base_freq = str(bars[0].freq.value)
        bg: BarGenerator = kwargs.pop("bg", None)
        freqs = self.sorted_freqs[1:] if base_freq in self.sorted_freqs else self.sorted_freqs

        if bg is None:
            uni_times = sorted(list({x.dt.strftime("%H:%M") for x in bars}))
            _, market = check_freq_and_market(uni_times, freq=base_freq)

            sdt = pd.to_datetime(kwargs.get("sdt", "20200101"))
            n = int(kwargs.get("n", 500))
            bg = BarGenerator(base_freq, freqs=freqs, market=market)

            bars_init = [x for x in bars if x.dt <= sdt]
            if len(bars_init) > n:
                bars1 = bars_init
                bars2 = [x for x in bars if x.dt > sdt]
            else:
                bars1 = bars[:n]
                bars2 = bars[n:]

            for bar in bars1:
                bg.update(bar)

            return bg, bars2
        else:
            assert bg.base_freq == bars[-1].freq.value
            assert isinstance(bg.end_dt, datetime)
            bars2 = [x for x in bars if x.dt > bg.end_dt]
            return bg, bars2

    def init_trader(self, bars: List[RawBar], **kwargs) -> CzscTrader:
        """初始化 CzscTrader"""
        bg, bars2 = self.init_bar_generator(bars, **kwargs)
        trader = CzscTrader(bg=bg, positions=deepcopy(self.positions),
                            signals_config=deepcopy(self.signals_config), **kwargs)
        for bar in bars2:
            trader.on_bar(bar)
        return trader


class CzscJsonStrategy(CzscStrategyBase):
    """从 JSON 配置加载策略"""

    @property
    def positions(self):
        files = self.kwargs["files_position"]
        check = self.kwargs.get("check_position", True)
        return self.load_positions(files, check)

    def load_positions(self, files: List, check=True) -> List[Position]:
        positions = []
        for file in files:
            pos = read_json(file)
            md5 = pos.pop("md5")
            if check:
                assert md5 == hashlib.md5(str(pos).encode()).hexdigest()
            pos["symbol"] = self.symbol
            positions.append(Position.load(pos))
        return positions
