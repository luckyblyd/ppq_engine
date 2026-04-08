# coding: utf-8
"""
czsc.utils - 精简版
仅保留项目实际使用的工具函数，移除未使用的重量级导入
"""
from typing import List, Union

from .bar_generator import BarGenerator, freq_end_time, resample_bars, format_standard_kline
from .bar_generator import is_trading_time, get_intraday_times, check_freq_and_market
from .io import read_json, save_json
from .sig import check_gap_info, is_bis_down, is_bis_up, get_sub_elements, is_symmetry_zs
from .sig import same_dir_counts, fast_slow_cross, count_last_same, create_single_signal
from .corr import single_linear
from .cache import home_path, get_dir_size, empty_cache_path


sorted_freqs = [
    "Tick", "1分钟", "2分钟", "3分钟", "4分钟", "5分钟", "6分钟",
    "10分钟", "12分钟", "15分钟", "20分钟", "30分钟", "60分钟", "120分钟",
    "日线", "周线", "月线", "季线", "年线",
]


def x_round(x: Union[float, int], digit: int = 4) -> Union[float, int]:
    if isinstance(x, int):
        return x
    try:
        digit_ = pow(10, digit)
        x = int(x * digit_) / digit_
    except:
        pass
    return x


def import_by_name(name):
    """通过字符串导入模块、类、函数"""
    if "." not in name:
        return __import__(name)
    module_name, function_name = name.rsplit(".", 1)
    module = __import__(module_name, globals(), locals(), [function_name])
    return vars(module)[function_name]


def freqs_sorted(freqs):
    """K线周期列表排序并去重"""
    return [x for x in sorted_freqs if x in freqs]
