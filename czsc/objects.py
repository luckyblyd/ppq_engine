# -*- coding: utf-8 -*-
"""
缠论核心对象 - 精简高性能版
仅保留项目实际使用的：RawBar, NewBar, FX, BI, ZS, FakeBI
以及策略框架需要的：Signal, Factor, Event, Position
移除所有未使用的属性计算（numpy polyfit等）
"""
import hashlib
from collections import OrderedDict
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Callable, Dict
from czsc.enum import Mark, Direction, Freq, Operate


# ============================================================
# 核心K线对象 - 使用 __slots__ 加速属性访问、减少内存
# ============================================================

@dataclass(slots=True)
class RawBar:
    """原始K线元素"""
    symbol: str
    id: int
    dt: datetime
    freq: Freq
    open: float
    close: float
    high: float
    low: float
    vol: float
    amount: float
    cache: dict = field(default_factory=dict)

    @property
    def upper(self):
        """上影"""
        return self.high - max(self.open, self.close)

    @property
    def lower(self):
        """下影"""
        return min(self.open, self.close) - self.low

    @property
    def solid(self):
        """实体"""
        return abs(self.open - self.close)


@dataclass(slots=True)
class NewBar:
    """去除包含关系后的K线元素"""
    symbol: str
    id: int
    dt: datetime
    freq: Freq
    open: float
    close: float
    high: float
    low: float
    vol: float
    amount: float
    elements: List = field(default_factory=list)
    cache: dict = field(default_factory=dict)

    @property
    def raw_bars(self):
        return self.elements


# ============================================================
# 分型与笔
# ============================================================

@dataclass(slots=True)
class FX:
    symbol: str
    dt: datetime
    mark: Mark
    high: float
    low: float
    fx: float
    elements: List = field(default_factory=list)
    cache: dict = field(default_factory=dict)

    @property
    def new_bars(self):
        return self.elements

    @property
    def raw_bars(self):
        res = []
        for e in self.elements:
            res.extend(e.raw_bars)
        return res

    @property
    def power_str(self):
        assert len(self.elements) == 3
        k1, k2, k3 = self.elements
        if self.mark == Mark.D:
            if k3.close > k1.high:
                return "强"
            elif k3.close > k2.high:
                return "中"
            else:
                return "弱"
        else:
            if k3.close < k1.low:
                return "强"
            elif k3.close < k2.low:
                return "中"
            else:
                return "弱"


@dataclass(slots=True)
class FakeBI:
    """虚拟笔"""
    symbol: str
    sdt: datetime
    edt: datetime
    direction: Direction
    high: float
    low: float
    power: float
    cache: dict = field(default_factory=dict)


def create_fake_bis(fxs: List[FX]) -> List[FakeBI]:
    if len(fxs) % 2 != 0:
        fxs = fxs[:-1]
    fake_bis = []
    for i in range(1, len(fxs)):
        fx1, fx2 = fxs[i - 1], fxs[i]
        if fx1.mark == Mark.D:
            fake_bis.append(FakeBI(
                symbol=fx1.symbol, sdt=fx1.dt, edt=fx2.dt,
                direction=Direction.Up, high=fx2.high, low=fx1.low,
                power=round(fx2.high - fx1.low, 2)))
        elif fx1.mark == Mark.G:
            fake_bis.append(FakeBI(
                symbol=fx1.symbol, sdt=fx1.dt, edt=fx2.dt,
                direction=Direction.Down, high=fx1.high, low=fx2.low,
                power=round(fx1.high - fx2.low, 2)))
    return fake_bis


@dataclass
class BI:
    symbol: str
    fx_a: FX
    fx_b: FX
    fxs: List
    direction: Direction
    bars: List[NewBar] = field(default_factory=list)
    cache: dict = field(default_factory=dict)

    def __post_init__(self):
        self.sdt = self.fx_a.dt
        self.edt = self.fx_b.dt

    def __repr__(self):
        return (f"BI(symbol={self.symbol}, sdt={self.sdt}, edt={self.edt}, "
                f"direction={self.direction}, high={self.high}, low={self.low})")

    def get_cache_with_default(self, key, default: Callable):
        cache = self.cache if self.cache else {}
        value = cache.get(key, None)
        if not value:
            value = default()
            cache[key] = value
            self.cache = cache
        return value

    def get_price_linear(self, price_key="close"):
        cache = self.cache if self.cache else {}
        key = f"{price_key}_linear_info"
        value = cache.get(key, None)
        if not value:
            from czsc.utils.corr import single_linear
            value = single_linear([getattr(x, price_key) for x in self.raw_bars])
            cache[key] = value
            self.cache = cache
        return value

    @property
    def fake_bis(self):
        return self.get_cache_with_default("fake_bis", lambda: create_fake_bis(self.fxs))

    @property
    def high(self):
        return self.get_cache_with_default("high", lambda: max(self.fx_a.high, self.fx_b.high))

    @property
    def low(self):
        return min(self.fx_a.low, self.fx_b.low)

    @property
    def power(self):
        return self.power_price

    @property
    def power_price(self):
        return round(abs(self.fx_b.fx - self.fx_a.fx), 2)

    @property
    def power_volume(self):
        return sum(x.vol for x in self.bars[1:-1])

    @property
    def change(self):
        return round((self.fx_b.fx - self.fx_a.fx) / self.fx_a.fx, 4)

    @property
    def length(self):
        return len(self.bars)

    @property
    def raw_bars(self):
        def __default():
            value = []
            for bar in self.bars[1:-1]:
                value.extend(bar.raw_bars)
            return value
        return self.get_cache_with_default("raw_bars", __default)


# ============================================================
# 中枢
# ============================================================

@dataclass
class ZS:
    bis: List[BI]
    cache: dict = field(default_factory=dict)

    def __post_init__(self):
        self.symbol = self.bis[0].symbol

    @property
    def sdt(self):
        return self.bis[0].sdt

    @property
    def edt(self):
        return self.bis[-1].edt

    @property
    def sdir(self):
        return self.bis[0].direction

    @property
    def edir(self):
        return self.bis[-1].direction

    @property
    def zz(self):
        return self.zd + (self.zg - self.zd) / 2

    @property
    def gg(self):
        return max(x.high for x in self.bis)

    @property
    def zg(self):
        return min(x.high for x in self.bis[:3])

    @property
    def dd(self):
        return min(x.low for x in self.bis)

    @property
    def zd(self):
        return max(x.low for x in self.bis[:3])

    @property
    def is_valid(self):
        if self.zg < self.zd:
            return False
        for bi in self.bis:
            if (self.zg >= bi.high >= self.zd
                    or self.zg >= bi.low >= self.zd
                    or bi.high >= self.zg > self.zd >= bi.low):
                continue
            else:
                return False
        return True

    def __repr__(self):
        return (f"ZS(sdt={self.sdt}, sdir={self.sdir}, edt={self.edt}, edir={self.edir}, "
                f"len_bis={len(self.bis)}, zg={self.zg}, zd={self.zd}, "
                f"gg={self.gg}, dd={self.dd}, zz={self.zz})")


# ============================================================
# 信号框架 - Signal / Factor / Event（策略定义需要）
# ============================================================

@dataclass
class Signal:
    signal: str = ""
    score: int = 0
    k1: str = "任意"
    k2: str = "任意"
    k3: str = "任意"
    v1: str = "任意"
    v2: str = "任意"
    v3: str = "任意"

    def __post_init__(self):
        if not self.signal:
            self.signal = f"{self.k1}_{self.k2}_{self.k3}_{self.v1}_{self.v2}_{self.v3}_{self.score}"
        else:
            self.k1, self.k2, self.k3, self.v1, self.v2, self.v3, score = self.signal.split("_")
            self.score = int(score)

    def __repr__(self):
        return f"Signal('{self.signal}')"

    @property
    def key(self) -> str:
        key = ""
        for k in [self.k1, self.k2, self.k3]:
            if k != "任意":
                key += k + "_"
        return key.strip("_")

    @property
    def value(self) -> str:
        return f"{self.v1}_{self.v2}_{self.v3}_{self.score}"

    def is_match(self, s: dict) -> bool:
        key = self.key
        v = s.get(key, None)
        if not v:
            raise ValueError(f"{key} 不在信号列表中")
        v1, v2, v3, score = v.split("_")
        if int(score) >= self.score:
            if (v1 == self.v1 or self.v1 == "任意"):
                if (v2 == self.v2 or self.v2 == "任意"):
                    if (v3 == self.v3 or self.v3 == "任意"):
                        return True
        return False


@dataclass
class Factor:
    signals_all: List[Signal]
    signals_any: List[Signal] = field(default_factory=list)
    signals_not: List[Signal] = field(default_factory=list)
    name: str = ""

    def __post_init__(self):
        if not self.signals_all:
            raise ValueError("signals_all 不能为空")
        _factor = self.dump()
        _factor.pop("name")
        sha256 = hashlib.sha256(str(_factor).encode("utf-8")).hexdigest().upper()[:4]
        if self.name:
            self.name = self.name.split("#")[0] + f"#{sha256}"
        else:
            self.name = f"#{sha256}"

    @property
    def unique_signals(self) -> List[str]:
        signals = []
        signals.extend(self.signals_all)
        if self.signals_any:
            signals.extend(self.signals_any)
        if self.signals_not:
            signals.extend(self.signals_not)
        return list({x.signal if isinstance(x, Signal) else x for x in signals})

    def is_match(self, s: dict) -> bool:
        if self.signals_not:
            for signal in self.signals_not:
                if signal.is_match(s):
                    return False
        for signal in self.signals_all:
            if not signal.is_match(s):
                return False
        if not self.signals_any:
            return True
        for signal in self.signals_any:
            if signal.is_match(s):
                return True
        return False

    def dump(self) -> dict:
        return {
            "name": self.name,
            "signals_all": [x.signal for x in self.signals_all],
            "signals_any": [x.signal for x in self.signals_any] if self.signals_any else [],
            "signals_not": [x.signal for x in self.signals_not] if self.signals_not else [],
        }

    @classmethod
    def load(cls, raw: dict):
        return Factor(
            name=raw.get("name", ""),
            signals_all=[Signal(x) for x in raw["signals_all"]],
            signals_any=[Signal(x) for x in raw.get("signals_any", [])],
            signals_not=[Signal(x) for x in raw.get("signals_not", [])],
        )


@dataclass
class Event:
    operate: Operate
    factors: List[Factor]
    signals_all: List[Signal] = field(default_factory=list)
    signals_any: List[Signal] = field(default_factory=list)
    signals_not: List[Signal] = field(default_factory=list)
    name: str = ""

    def __post_init__(self):
        if not self.factors:
            raise ValueError("factors 不能为空")
        _event = self.dump()
        _event.pop("name")
        sha256 = hashlib.sha256(str(_event).encode("utf-8")).hexdigest().upper()[:4]
        if self.name:
            self.name = self.name.split("#")[0] + f"#{sha256}"
        else:
            self.name = f"{self.operate.value}#{sha256}"
        self.sha256 = sha256

    @property
    def unique_signals(self) -> List[str]:
        signals = []
        if self.signals_all:
            signals.extend(self.signals_all)
        if self.signals_any:
            signals.extend(self.signals_any)
        if self.signals_not:
            signals.extend(self.signals_not)
        for factor in self.factors:
            signals.extend(factor.unique_signals)
        return list({x.signal if isinstance(x, Signal) else x for x in signals})

    def get_signals_config(self, signals_module: str = "") -> List[Dict]:
        from czsc.traders.sig_parse import get_signals_config
        return get_signals_config(self.unique_signals, signals_module)

    def is_match(self, s: dict):
        if self.signals_not and any(signal.is_match(s) for signal in self.signals_not):
            return False, None
        if self.signals_all and not all(signal.is_match(s) for signal in self.signals_all):
            return False, None
        if self.signals_any and not any(signal.is_match(s) for signal in self.signals_any):
            return False, None
        for factor in self.factors:
            if factor.is_match(s):
                return True, factor.name
        return False, None

    def dump(self) -> dict:
        return {
            "name": self.name,
            "operate": self.operate.value,
            "signals_all": [x.signal for x in self.signals_all] if self.signals_all else [],
            "signals_any": [x.signal for x in self.signals_any] if self.signals_any else [],
            "signals_not": [x.signal for x in self.signals_not] if self.signals_not else [],
            "factors": [x.dump() for x in self.factors],
        }

    @classmethod
    def load(cls, raw: dict):
        assert raw["operate"] in Operate.__dict__["_value2member_map_"]
        assert raw["factors"]
        return Event(
            name=raw.get("name", ""),
            operate=Operate.__dict__["_value2member_map_"][raw["operate"]],
            factors=[Factor.load(x) for x in raw["factors"]],
            signals_all=[Signal(x) for x in raw.get("signals_all", [])],
            signals_any=[Signal(x) for x in raw.get("signals_any", [])],
            signals_not=[Signal(x) for x in raw.get("signals_not", [])],
        )


# ============================================================
# Position - 精简版，仅保留信号记录功能（update 只做信号匹配记录）
# ============================================================

class Position:
    def __init__(self, symbol: str, opens: List[Event], exits: List[Event] = [],
                 interval: int = 0, timeout: int = 1000, stop_loss=1000,
                 T0: bool = False, name=None):
        assert name, "name 是必须的参数"
        self.symbol = symbol
        self.opens = opens
        self.name = name
        self.exits = exits if exits else []
        self.events = self.opens + self.exits
        self.interval = interval
        self.timeout = timeout
        self.stop_loss = stop_loss
        self.T0 = T0
        self.pos = 0
        self.pos_changed = False
        self.signlist = []
        self.end_dt = None

    def __repr__(self):
        return f"Position(name={self.name}, symbol={self.symbol})"

    @property
    def unique_signals(self) -> List[str]:
        signals = []
        for e in self.events:
            signals.extend(e.unique_signals)
        return list(set(signals))

    def get_signals_config(self, signals_module: str = "") -> List[Dict]:
        from czsc.traders.sig_parse import get_signals_config
        return get_signals_config(self.unique_signals, signals_module)

    def dump(self, with_data=False):
        return {
            "symbol": self.symbol, "name": self.name,
            "opens": [x.dump() for x in self.opens],
            "exits": [x.dump() for x in self.exits],
            "interval": self.interval, "timeout": self.timeout,
            "stop_loss": self.stop_loss, "T0": self.T0,
        }

    @classmethod
    def load(cls, raw: dict):
        return Position(
            name=raw["name"], symbol=raw["symbol"],
            opens=[Event.load(x) for x in raw["opens"] if raw.get("opens")],
            exits=[Event.load(x) for x in raw["exits"] if raw.get("exits")],
            interval=raw["interval"], timeout=raw["timeout"],
            stop_loss=raw["stop_loss"], T0=raw["T0"],
        )

    def update(self, s: dict):
        """仅做信号匹配记录，不执行仓位管理（由PPQ自身的交易引擎管理）"""
        matched_event, matched_factor = None, None
        for event in self.events:
            m, f = event.is_match(s)
            if m:
                matched_event, matched_factor = event, f
                break

        if matched_event:
            for k in matched_event.unique_signals:
                parts = k.split("_")
                if len(parts) >= 3:
                    name_parts = [p for p in parts[:3] if p != "任意"]
                    name = "_".join(name_parts)
                else:
                    name = k
                self.signlist.append({
                    "symbol": s.get("symbol", self.symbol),
                    "dt": s.get("dt"),
                    "bid": s.get("id"),
                    "price": s.get("close"),
                    "operate": matched_event.operate,
                    "event": matched_event.name,
                    "factor": matched_factor,
                    "signal_name": name,
                    "signal_values": s.get(name),
                    "remark": ", "
                })
