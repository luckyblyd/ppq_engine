"""
ElderRayStrategy.py — Elder-Ray 策略 (C++ 调用模式)
所有指标由 C++ IndicatorCache 计算并通过 args 字典传入
无需 import pandas/numpy/czsc 等第三方库

返回格式: [(strength, op_str, order_type, qty, id, remark, price, tp, sl), ...]

计算逻辑:
  Elder-Ray 指标: Bull Power = High - EMA(Close, 13)
                   Bear Power = Low  - EMA(Close, 13)
  多头开仓: Bull Power 向上穿越阈值 (crossover START_THRESHOLD)
  多头平仓: Bull Power 跌破开仓时记录的 Power 值
  空头开仓: Bear Power 向下穿越 -阈值 (crossunder -START_THRESHOLD)
  空头平仓: Bear Power 回升超过开仓时记录的 Power 值

  使用5分钟K线数据, 每隔12根(1小时)取样来模拟60分钟级别信号
  整点过滤: 仅在5分钟K线对应整点时触发(即每小时一次)
"""

# ============================================================================
# 策略参数
# ============================================================================
START_THRESHOLD = 0.002
EMA_LENGTH = 13
HOUR_SAMPLE_INTERVAL = 12  # 5分钟 * 12 = 60分钟

# ============================================================================
# 策略状态（模块级，跨调用持久）
# ============================================================================
_state = {
    "long_entry_power": None,   # 多头开仓时的 Bull Power
    "short_entry_power": None,  # 空头开仓时的 Bear Power
}


# ============================================================================
# 辅助函数: 从5分钟历史数据合成1小时级别的 Bull/Bear Power
# ============================================================================

def _resample_hourly(history_5m, interval=HOUR_SAMPLE_INTERVAL):
    """
    从5分钟级别的历史数据中，每隔 interval 根取一个样本
    模拟60分钟周期的数据序列
    返回取样后的列表(从旧到新)
    """
    if not history_5m:
        return []
    n = len(history_5m)
    # 从最新一根往前，以 interval 为步长取样，然后反转为时间正序
    result = []
    i = n - 1
    while i >= 0:
        result.append(history_5m[i])
        i -= interval
    result.reverse()
    return result


def _compute_hourly_elder_ray(close_hist, high_hist, low_hist, ema13_hist):
    """
    从5分钟历史数据合成1小时级别的 Elder-Ray 指标
    
    原版逻辑:
      1. 取60分钟K线的 close/high/low
      2. 计算 EMA(close, 13)
      3. bull_power = high - ema
      4. bear_power = low - ema
    
    C++调用模式适配:
      C++ 已用5分钟级别计算了 bull_power = high - ema13
      这里从5分钟的 bull_power_history/bear_power_history 每12根取样
      来近似60分钟级别的 Elder-Ray
    """
    if not close_hist or not high_hist or not low_hist or not ema13_hist:
        return [], []

    n = len(close_hist)
    if n < HOUR_SAMPLE_INTERVAL + 1:
        return [], []

    # 从5分钟数据中每12根取样得到"小时级"数据点
    hourly_high = _resample_hourly(high_hist)
    hourly_low = _resample_hourly(low_hist)
    hourly_close = _resample_hourly(close_hist)

    if len(hourly_close) < EMA_LENGTH + 2:
        return [], []

    # 计算 EMA(close, 13) - 在小时级别上
    ema = [0.0] * len(hourly_close)
    alpha = 2.0 / (EMA_LENGTH + 1.0)
    ema[0] = hourly_close[0]
    for i in range(1, len(hourly_close)):
        ema[i] = alpha * hourly_close[i] + (1.0 - alpha) * ema[i - 1]

    # 计算 Bull Power 和 Bear Power
    bull_power = [hourly_high[i] - ema[i] for i in range(len(hourly_close))]
    bear_power = [hourly_low[i] - ema[i] for i in range(len(hourly_close))]

    return bull_power, bear_power


# ============================================================================
# 主入口
# ============================================================================

def generate_signal(args):
    """
    策略主入口，由 C++ PythonBridge 调用
    精确对齐原版 ElderRayStrategy.py SignalGenerator.generate_signal()
    
    参数 args 为 C++ BuildArgsDict 构建的字典，包含:
      - bar_count: K线数量
      - timestamp: 最新K线时间戳(ms)
      - close, high, low: 最新K线价格
      - bull_power, bear_power: 最新5分钟级别的Elder-Ray值
      - close_history, high_history, low_history, ema13_history: 历史数据数组
      - bull_power_history, bear_power_history: 历史Elder-Ray数组
      - long_positions, short_positions: 持仓列表
      - order_amount: 开仓金额
    """
    result = []
    st = _state

    bar_count = args.get("bar_count", 0)
    if bar_count < EMA_LENGTH + 5:
        return result

    # ---------- 提取数据 ----------
    close = args.get("close", 0)
    high = args.get("high", 0)
    low = args.get("low", 0)
    ts = args.get("timestamp", 0)
    order_amt = args.get("order_amount", 100)

    longs = args.get("long_positions", [])
    shorts = args.get("short_positions", [])

    # ---------- 时间过滤: 只在整点触发 ----------
    # 原版: bars[-1].dt.minute != 0 则跳过
    # 5分钟K线时间戳(ms), 判断是否在整点(分钟数为0)
    # timestamp 为毫秒, 转换为分钟数判断
    if ts > 0:
        minutes = (ts // 60000) % 60
        if minutes != 0:
            return result

    # ---------- 获取历史数据, 合成1小时级别 Elder-Ray ----------
    close_hist = args.get("close_history", [])
    high_hist = args.get("high_history", [])
    low_hist = args.get("low_history", [])
    ema13_hist = args.get("ema13_history", [])

    bull_power_hourly, bear_power_hourly = _compute_hourly_elder_ray(
        close_hist, high_hist, low_hist, ema13_hist
    )

    if len(bull_power_hourly) < 2 or len(bear_power_hourly) < 2:
        return result

    # 当前和前一根(小时级)的 Bull/Bear Power
    curr_bull = bull_power_hourly[-1]
    prev_bull = bull_power_hourly[-2]
    curr_bear = bear_power_hourly[-1]
    prev_bear = bear_power_hourly[-2]

    curr_price = close

    # ---------- 计算开仓数量 ----------
    qty = order_amt / curr_price if curr_price > 0 else 0

    # --- 多头逻辑 (Long Logic) ---
    if not longs:
        # 进场逻辑: Crossover START_THRESHOLD
        if curr_bull > START_THRESHOLD and prev_bull <= START_THRESHOLD:
            st["long_entry_power"] = curr_bull
            remark = "有效启动(多)|Power:%.2f" % curr_bull
            result.append((
                1, "LO", 1, qty, 0,
                remark, curr_price, 0, 0
            ))
    else:
        # 出场逻辑: Bull Power 跌破开仓时的力量
        entry_p = st["long_entry_power"] if st["long_entry_power"] is not None else START_THRESHOLD
        if curr_bull < entry_p:
            for o in longs:
                oid = o.get("id", 0)
                o_qty = o.get("quantity", 0)
                remark = "多头离场|Curr:%.2f<Entry:%.2f" % (curr_bull, entry_p)
                result.append((
                    1, "LE", 1, o_qty, oid,
                    remark, curr_price, 0, 0
                ))
            st["long_entry_power"] = None

    # --- 空头逻辑 (Short Logic) ---
    if not shorts:
        if curr_bear < -START_THRESHOLD and prev_bear >= -START_THRESHOLD:
            st["short_entry_power"] = curr_bear
            remark = "有效启动(空)|Power:%.2f" % curr_bear
            result.append((
                1, "SO", 1, qty, 0,
                remark, curr_price, 0, 0
            ))
    else:
        entry_p = st["short_entry_power"] if st["short_entry_power"] is not None else -START_THRESHOLD
        if curr_bear > entry_p:
            for o in shorts:
                oid = o.get("id", 0)
                o_qty = o.get("quantity", 0)
                remark = "空头离场|Curr:%.2f>Entry:%.2f" % (curr_bear, entry_p)
                result.append((
                    1, "SE", 1, o_qty, oid,
                    remark, curr_price, 0, 0
                ))
            st["short_entry_power"] = None

    return result
