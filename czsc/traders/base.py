# -*- coding: utf-8 -*-
"""
CzscTrader - 精简高性能版
移除：可视化(take_snapshot/open_in_browser)、集成仓位(ensemble)、权重回测(weight_backtest)
保留：核心信号计算 + on_bar 更新
"""
from collections import OrderedDict
from typing import List, Optional, Union, AnyStr, Callable
from loguru import logger
from czsc.analyze import CZSC
from czsc.objects import Position, RawBar
from czsc.utils.bar_generator import BarGenerator
from czsc.utils import import_by_name
from dataclasses import fields as _dc_fields

# RawBar 使用 __slots__，没有 __dict__，需要手动转换
_RAWBAR_FIELDS = None

def _rawbar_to_dict(bar: RawBar) -> dict:
    """将 RawBar (slots dataclass) 转为 dict，替代 bar.__dict__"""
    global _RAWBAR_FIELDS
    if _RAWBAR_FIELDS is None:
        _RAWBAR_FIELDS = [f.name for f in _dc_fields(RawBar)]
    return {name: getattr(bar, name) for name in _RAWBAR_FIELDS}


class CzscSignals:
    """多级别信号计算 - 精简版"""

    __slots__ = ('name', 'cache', 'kwargs', 'signals_config', 'bg', 'symbol',
                 'base_freq', 'freqs', 'kas', 'end_dt', 'bid', 'latest_price', 's')

    def __init__(self, bg: Optional[BarGenerator] = None, **kwargs):
        self.name = "CzscSignals"
        self.cache = OrderedDict()
        self.kwargs = kwargs
        self.signals_config = kwargs.get("signals_config", [])

        if bg:
            self.bg = bg
            assert bg.symbol, "bg.symbol is None"
            self.symbol = bg.symbol
            self.base_freq = bg.base_freq
            self.freqs = list(bg.bars.keys())
            self.kas = {freq: CZSC(b) for freq, b in bg.bars.items()}

            last_bar = self.kas[self.base_freq].bars_raw[-1]
            self.end_dt, self.bid, self.latest_price = last_bar.dt, last_bar.id, last_bar.close
            self.s = OrderedDict()
            self.s.update(self.get_signals_by_conf())
            self.s.update(_rawbar_to_dict(last_bar))
        else:
            self.bg = None
            self.symbol = None
            self.base_freq = None
            self.freqs = None
            self.kas = None
            self.end_dt, self.bid, self.latest_price = None, None, None
            self.s = OrderedDict()

    def __repr__(self):
        return "<{} for {}>".format(self.name, self.symbol)

    def get_signals_by_conf(self):
        """通过信号参数配置获取信号"""
        s = OrderedDict()
        if not self.signals_config:
            return s

        for param in self.signals_config:
            param = dict(param)
            sig_name = param.pop('name')
            sig_func = import_by_name(sig_name) if isinstance(sig_name, str) else sig_name

            freq = param.pop('freq', None)
            if freq in self.kas:
                s.update(sig_func(self.kas[freq], **param))
            else:
                s.update(sig_func(self, **param))
        return s

    def update_signals(self, bar: RawBar):
        """更新信号（热路径）"""
        self.bg.update(bar)
        for freq, b in self.bg.bars.items():
            self.kas[freq].update(b[-1])

        self.symbol = bar.symbol
        last_bar = self.kas[self.base_freq].bars_raw[-1]
        self.end_dt, self.bid, self.latest_price = last_bar.dt, last_bar.id, last_bar.close
        self.s = OrderedDict()
        self.s.update(self.get_signals_by_conf())
        self.s.update(_rawbar_to_dict(last_bar))

    def take_snapshot(self, file_html=None, width: str = "1400px", height: str = "580px"):
        """获取快照 - 保留用于HTML报告生成"""
        from pyecharts.charts import Tab
        from pyecharts.components import Table
        from pyecharts.options import ComponentTitleOpts

        tab = Tab(page_title="{}@{}".format(self.symbol, self.end_dt.strftime("%Y-%m-%d %H:%M")))
        for freq in self.freqs:
            ka: CZSC = self.kas[freq]
            chart = ka.to_echarts(width, height)
            tab.add(chart, freq)

        signals = {k: v for k, v in self.s.items() if len(k.split("_")) == 3}
        for freq in self.freqs:
            freq_signals = {k: signals[k] for k in signals.keys() if k.startswith("{}_".format(freq))}
            for k in freq_signals.keys():
                signals.pop(k)
            if len(freq_signals) <= 0:
                continue
            t1 = Table()
            t1.add(["名称", "数据"], [[k, v] for k, v in freq_signals.items()])
            t1.set_global_opts(title_opts=ComponentTitleOpts(title="缠中说禅信号表", subtitle=""))
            tab.add(t1, f"{freq}信号")

        if len(signals) > 0:
            t1 = Table()
            t1.add(["名称", "数据"], [[k, v] for k, v in signals.items()])
            t1.set_global_opts(title_opts=ComponentTitleOpts(title="缠中说禅信号表", subtitle=""))
            tab.add(t1, "其他信号")

        if file_html:
            tab.render(file_html)
        else:
            return tab


class CzscTrader(CzscSignals):
    """多级别联立交易决策 - 精简版"""

    def __init__(self, bg: Optional[BarGenerator] = None,
                 positions: Optional[List[Position]] = None, **kwargs):
        # 去掉 ensemble_method 参数，不再需要
        self.positions = positions
        if self.positions:
            _pos_names = [x.name for x in self.positions]
            assert len(_pos_names) == len(set(_pos_names)), "仓位策略名称不能重复"
        self.name = "CzscTrader"
        # 过滤掉 CzscSignals 不认识的 kwargs
        _valid_kwargs = {k: v for k, v in kwargs.items()
                         if k in ('signals_config',)}
        _valid_kwargs.update({k: v for k, v in kwargs.items()
                              if k not in ('ensemble_method',)})
        super().__init__(bg, **_valid_kwargs)

    def __repr__(self):
        return "<{} for {}>".format(self.name, self.symbol)

    def update(self, bar: RawBar) -> None:
        """更新信号和仓位"""
        self.update_signals(bar)
        if self.positions:
            for position in self.positions:
                position.update(self.s)

    def on_bar(self, bar: RawBar) -> None:
        self.update(bar)

    def on_sig(self, sig: dict) -> None:
        """通过信号字典直接交易"""
        self.s = sig
        self.symbol, self.end_dt = self.s['symbol'], self.s['dt']
        self.bid, self.latest_price = self.s['id'], self.s['close']
        if self.positions:
            for position in self.positions:
                position.update(self.s)

    @property
    def pos_changed(self) -> bool:
        if not self.positions:
            return False
        return any(position.pos_changed for position in self.positions)

    def get_position(self, name: str) -> Optional[Position]:
        if not self.positions:
            return None
        for position in self.positions:
            if position.name == name:
                return position
        return None

    def take_snapshot(self, file_html=None, width: str = "1400px", height: str = "580px"):
        """获取快照 - 保留用于HTML报告"""
        from pyecharts.charts import Tab
        from pyecharts.components import Table
        from pyecharts.options import ComponentTitleOpts

        tab = Tab(page_title="{}@{}".format(self.symbol, self.end_dt.strftime("%Y-%m-%d %H:%M")))
        for freq in self.freqs:
            ka: CZSC = self.kas[freq]
            bs = None
            if freq == self.base_freq and self.positions:
                bs = []
                for pos in self.positions:
                    for sig in getattr(pos, "signlist", []):
                        if sig.get("dt") and sig["dt"] >= ka.bars_raw[0].dt:
                            bs.append({
                                "dt": sig["dt"],
                                "op": sig.get("operate"),
                                "bid": sig.get("bid"),
                                "price": sig.get("price"),
                                "op_desc": f"{pos.name} | {sig.get('event', '')} | {sig.get('factor', '')}",
                            })
            chart = ka.to_echarts(width, height, bs)
            tab.add(chart, freq)

        signals = {k: v for k, v in self.s.items() if len(k.split("_")) == 3}
        for freq in self.freqs:
            freq_signals = {k: signals[k] for k in signals.keys() if k.startswith("{}_".format(freq))}
            for k in freq_signals.keys():
                signals.pop(k)
            if len(freq_signals) <= 0:
                continue
            t1 = Table()
            t1.add(["名称", "数据"], [[k, v] for k, v in freq_signals.items()])
            t1.set_global_opts(title_opts=ComponentTitleOpts(title="缠中说禅信号表", subtitle=""))
            tab.add(t1, f"{freq}信号")

        if len(signals) > 0:
            t1 = Table()
            t1.add(["名称", "数据"], [[k, v] for k, v in signals.items()])
            t1.set_global_opts(title_opts=ComponentTitleOpts(title="缠中说禅信号表", subtitle=""))
            tab.add(t1, "其他信号")

        if file_html:
            tab.render(file_html)
        else:
            return tab
