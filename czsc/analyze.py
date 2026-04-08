# -*- coding: utf-8 -*-
"""
缠论分型、笔的识别 - 优化版
优化点：
1. remove_include 减少 NewBar 创建开销
2. CZSC.update 减少列表操作
3. 移除 plotly/echarts 可视化的 import（延迟加载）
"""
import os
import webbrowser
from loguru import logger
from typing import List
from collections import OrderedDict
from czsc.enum import Mark, Direction
from czsc.objects import BI, FX, RawBar, NewBar
from czsc import envs

logger.disable('czsc.analyze')


def remove_include(k1: NewBar, k2: NewBar, k3: RawBar):
    """去除包含关系 - 优化版：减少对象创建"""
    if k1.high < k2.high:
        direction = Direction.Up
    elif k1.high > k2.high:
        direction = Direction.Down
    else:
        k4 = NewBar(symbol=k3.symbol, id=k3.id, freq=k3.freq, dt=k3.dt, open=k3.open,
                     close=k3.close, high=k3.high, low=k3.low, vol=k3.vol, amount=k3.amount, elements=[k3])
        return False, k4

    # 判断包含关系
    if (k2.high <= k3.high and k2.low >= k3.low) or (k2.high >= k3.high and k2.low <= k3.low):
        if direction == Direction.Up:
            high = max(k2.high, k3.high)
            low = max(k2.low, k3.low)
            dt = k2.dt if k2.high > k3.high else k3.dt
        elif direction == Direction.Down:
            high = min(k2.high, k3.high)
            low = min(k2.low, k3.low)
            dt = k2.dt if k2.low < k3.low else k3.dt
        else:
            raise ValueError

        open_, close = (high, low) if k3.open > k3.close else (low, high)
        vol = k2.vol + k3.vol
        amount = k2.amount + k3.amount
        elements = [x for x in k2.elements[:100] if x.dt != k3.dt] + [k3]
        k4 = NewBar(symbol=k3.symbol, id=k2.id, freq=k2.freq, dt=dt, open=open_,
                     close=close, high=high, low=low, vol=vol, amount=amount, elements=elements)
        return True, k4
    else:
        k4 = NewBar(symbol=k3.symbol, id=k3.id, freq=k3.freq, dt=k3.dt, open=k3.open,
                     close=k3.close, high=k3.high, low=k3.low, vol=k3.vol, amount=k3.amount, elements=[k3])
        return False, k4


def check_fx(k1: NewBar, k2: NewBar, k3: NewBar):
    """查找分型 - 内联优化"""
    fx = None
    if k1.high < k2.high > k3.high and k1.low < k2.low > k3.low:
        fx = FX(symbol=k1.symbol, dt=k2.dt, mark=Mark.G, high=k2.high,
                low=k2.low, fx=k2.high, elements=[k1, k2, k3])
    elif k1.low > k2.low < k3.low and k1.high > k2.high < k3.high:
        fx = FX(symbol=k1.symbol, dt=k2.dt, mark=Mark.D, high=k2.high,
                low=k2.low, fx=k2.low, elements=[k1, k2, k3])
    return fx


def check_fxs(bars: List[NewBar]) -> List[FX]:
    """查找所有分型 - 优化版：减少 isinstance 调用"""
    fxs = []
    n = len(bars)
    for i in range(1, n - 1):
        fx = check_fx(bars[i - 1], bars[i], bars[i + 1])
        if fx is not None:
            if len(fxs) >= 2 and fx.mark == fxs[-1].mark:
                logger.error(f"check_fxs错误: {bars[i].dt}，{fx.mark}，{fxs[-1].mark}")
            else:
                fxs.append(fx)
    return fxs


def check_bi(bars: List[NewBar], **kwargs):
    """查找一笔"""
    min_bi_len = envs.get_min_bi_len()
    fxs = check_fxs(bars)
    if len(fxs) < 2:
        return None, bars

    fx_a = fxs[0]
    if fx_a.mark == Mark.D:
        direction = Direction.Up
        fxs_b = (x for x in fxs if x.mark == Mark.G and x.dt > fx_a.dt and x.fx > fx_a.fx)
        fx_b = max(fxs_b, key=lambda fx: fx.high, default=None)
    elif fx_a.mark == Mark.G:
        direction = Direction.Down
        fxs_b = (x for x in fxs if x.mark == Mark.D and x.dt > fx_a.dt and x.fx < fx_a.fx)
        fx_b = min(fxs_b, key=lambda fx: fx.low, default=None)
    else:
        raise ValueError

    if fx_b is None:
        return None, bars

    bars_a = [x for x in bars if fx_a.elements[0].dt <= x.dt <= fx_b.elements[2].dt]
    bars_b = [x for x in bars if x.dt >= fx_b.elements[0].dt]

    ab_include = (fx_a.high > fx_b.high and fx_a.low < fx_b.low) or (fx_a.high < fx_b.high and fx_a.low > fx_b.low)

    if (not ab_include) and (len(bars_a) >= min_bi_len):
        fxs_ = [x for x in fxs if fx_a.elements[0].dt <= x.dt <= fx_b.elements[2].dt]
        bi = BI(symbol=fx_a.symbol, fx_a=fx_a, fx_b=fx_b, fxs=fxs_, direction=direction, bars=bars_a)
        return bi, bars_b
    else:
        return None, bars


class CZSC:
    """缠论分析引擎 - 优化版"""

    __slots__ = ('verbose', 'max_bi_num', 'bars_raw', 'bars_ubi', 'bi_list',
                 'symbol', 'freq', 'get_signals', 'signals', 'cache')

    def __init__(self, bars: List[RawBar], get_signals=None,
                 max_bi_num=envs.get_max_bi_num()):
        self.verbose = envs.get_verbose()
        self.max_bi_num = max_bi_num
        self.bars_raw: List[RawBar] = []
        self.bars_ubi: List[NewBar] = []
        self.bi_list: List[BI] = []
        self.symbol = bars[0].symbol
        self.freq = bars[0].freq
        self.get_signals = get_signals
        self.signals = None
        self.cache = OrderedDict()

        for bar in bars:
            self.update(bar)

    def __repr__(self):
        return "<CZSC~{}~{}>".format(self.symbol, self.freq.value)

    def __update_bi(self):
        bars_ubi = self.bars_ubi
        if len(bars_ubi) < 3:
            return

        if not self.bi_list:
            fxs = check_fxs(bars_ubi)
            if not fxs:
                return

            fx_a = fxs[0]
            fxs_a = [x for x in fxs if x.mark == fx_a.mark]
            for fx in fxs_a:
                if (fx_a.mark == Mark.D and fx.low <= fx_a.low) \
                        or (fx_a.mark == Mark.G and fx.high >= fx_a.high):
                    fx_a = fx
            bars_ubi = [x for x in bars_ubi if x.dt >= fx_a.elements[0].dt]

            bi, bars_ubi_ = check_bi(bars_ubi)
            if isinstance(bi, BI):
                self.bi_list.append(bi)
            self.bars_ubi = bars_ubi_
            return

        if self.verbose and len(bars_ubi) > 100:
            logger.info(f"{self.symbol} - {self.freq} - {bars_ubi[-1].dt} 未完成笔延伸数量: {len(bars_ubi)}")

        bi, bars_ubi_ = check_bi(bars_ubi)
        self.bars_ubi = bars_ubi_
        if isinstance(bi, BI):
            self.bi_list.append(bi)

        last_bi = self.bi_list[-1]
        bars_ubi = self.bars_ubi
        if (last_bi.direction == Direction.Up and bars_ubi[-1].high > last_bi.high) \
                or (last_bi.direction == Direction.Down and bars_ubi[-1].low < last_bi.low):
            self.bars_ubi = last_bi.bars[:-2] + [x for x in bars_ubi if x.dt >= last_bi.bars[-2].dt]
            self.bi_list.pop(-1)

    def update(self, bar: RawBar):
        """更新分析结果 - 热路径优化"""
        # 更新K线序列
        if not self.bars_raw or bar.dt != self.bars_raw[-1].dt:
            self.bars_raw.append(bar)
            last_bars = [bar]
        else:
            self.bars_raw[-1] = bar
            last_bars = self.bars_ubi.pop(-1).raw_bars
            last_bars[-1] = bar

        # 去除包含关系
        bars_ubi = self.bars_ubi
        for bar in last_bars:
            if len(bars_ubi) < 2:
                bars_ubi.append(NewBar(symbol=bar.symbol, id=bar.id, freq=bar.freq, dt=bar.dt,
                                       open=bar.open, close=bar.close, amount=bar.amount,
                                       high=bar.high, low=bar.low, vol=bar.vol, elements=[bar]))
            else:
                k1, k2 = bars_ubi[-2:]
                has_include, k3 = remove_include(k1, k2, bar)
                if has_include:
                    bars_ubi[-1] = k3
                else:
                    bars_ubi.append(k3)
        self.bars_ubi = bars_ubi

        # 更新笔
        self.__update_bi()

        # 限制数量
        self.bi_list = self.bi_list[-self.max_bi_num:]
        if self.bi_list:
            sdt = self.bi_list[0].fx_a.elements[0].dt
            s_index = 0
            for i, bar in enumerate(self.bars_raw):
                if bar.dt >= sdt:
                    s_index = i
                    break
            self.bars_raw = self.bars_raw[s_index:]

        # 信号计算
        self.signals = self.get_signals(c=self) if self.get_signals else OrderedDict()

    def to_echarts(self, width: str = "1400px", height: str = '580px', bs=[]):
        """绘制K线分析图（延迟导入）"""
        from czsc.utils.echarts_plot import kline_pro
        from dataclasses import asdict
        kline = [asdict(x) for x in self.bars_raw]
        if len(self.bi_list) > 0:
            bi = [{'dt': x.fx_a.dt, "bi": x.fx_a.fx} for x in self.bi_list] + \
                 [{'dt': self.bi_list[-1].fx_b.dt, "bi": self.bi_list[-1].fx_b.fx}]
            fx = [{'dt': x.dt, "fx": x.fx} for x in self.fx_list]
        else:
            bi = []
            fx = []
        chart = kline_pro(kline, bi=bi, fx=fx, width=width, height=height, bs=bs,
                          title="{}-{}".format(self.symbol, self.freq.value))
        return chart

    @property
    def last_bi_extend(self):
        if self.bi_list[-1].direction == Direction.Up \
                and max(x.high for x in self.bars_ubi) > self.bi_list[-1].high:
            return True
        if self.bi_list[-1].direction == Direction.Down \
                and min(x.low for x in self.bars_ubi) < self.bi_list[-1].low:
            return True
        return False

    @property
    def finished_bis(self) -> List[BI]:
        if not self.bi_list:
            return []
        if len(self.bars_ubi) < 5:
            return self.bi_list[:-1]
        return self.bi_list

    @property
    def ubi_fxs(self) -> List[FX]:
        if not self.bars_ubi:
            return []
        return check_fxs(self.bars_ubi)

    @property
    def ubi(self):
        ubi_fxs = self.ubi_fxs
        if not self.bars_ubi or not self.bi_list or not ubi_fxs:
            return None

        bars_raw = [y for x in self.bars_ubi for y in x.raw_bars]
        high_bar = max(bars_raw, key=lambda x: x.high)
        low_bar = min(bars_raw, key=lambda x: x.low)
        direction = Direction.Up if self.bi_list[-1].direction == Direction.Down else Direction.Down

        return {
            "symbol": self.symbol, "direction": direction,
            "high": high_bar.high, "low": low_bar.low,
            "high_bar": high_bar, "low_bar": low_bar,
            "bars": self.bars_ubi, "raw_bars": bars_raw,
            "fxs": ubi_fxs, "fx_a": ubi_fxs[0],
        }

    @property
    def fx_list(self) -> List[FX]:
        fxs = []
        for bi_ in self.bi_list:
            fxs.extend(bi_.fxs[1:])
        ubi = self.ubi_fxs
        for x in ubi:
            if not fxs or x.dt > fxs[-1].dt:
                fxs.append(x)
        return fxs
