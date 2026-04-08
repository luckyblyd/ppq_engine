#pragma once
// indicator_engine.h — 技术指标计算引擎
// 替代 Python 中的 pandas EWM、numpy、talib
// 所有计算结果通过 C++ 传入 Python 策略，策略无需 import

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <map>
#include <cstring>
#include "order.h"

// ============================================================================
// EMA 计算
// ============================================================================

inline std::vector<double> CalcEMA(const std::vector<double>& data, int span) {
    std::vector<double> ema(data.size(), 0.0);
    if (data.empty() || span <= 0) return ema;
    double alpha = 2.0 / (span + 1.0);
    ema[0] = data[0];
    for (size_t i = 1; i < data.size(); ++i) {
        ema[i] = alpha * data[i] + (1.0 - alpha) * ema[i - 1];
    }
    return ema;
}

// ============================================================================
// SMA 计算
// ============================================================================

inline std::vector<double> CalcSMA(const std::vector<double>& data, int period) {
    std::vector<double> sma(data.size(), 0.0);
    if (data.empty() || period <= 0) return sma;
    double sum = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        sum += data[i];
        if ((int)i >= period) sum -= data[i - period];
        if ((int)i >= period - 1)
            sma[i] = sum / period;
        else
            sma[i] = sum / (i + 1);
    }
    return sma;
}

// ============================================================================
// RSI 计算
// ============================================================================

inline std::vector<double> CalcRSI(const std::vector<double>& close, int period) {
    std::vector<double> rsi(close.size(), 50.0);
    if ((int)close.size() < period + 1) return rsi;

    double avg_gain = 0, avg_loss = 0;
    for (int i = 1; i <= period; ++i) {
        double diff = close[i] - close[i - 1];
        if (diff > 0) avg_gain += diff;
        else          avg_loss -= diff;
    }
    avg_gain /= period;
    avg_loss /= period;

    for (size_t i = period; i < close.size(); ++i) {
        if (i > (size_t)period) {
            double diff = close[i] - close[i - 1];
            double gain = diff > 0 ? diff : 0;
            double loss = diff < 0 ? -diff : 0;
            avg_gain = (avg_gain * (period - 1) + gain) / period;
            avg_loss = (avg_loss * (period - 1) + loss) / period;
        }
        if (avg_loss < 1e-12)
            rsi[i] = 100.0;
        else
            rsi[i] = 100.0 - 100.0 / (1.0 + avg_gain / avg_loss);
    }
    return rsi;
}

// ============================================================================
// MACD 计算
// ============================================================================

struct MACDResult {
    std::vector<double> dif;   // DIF线
    std::vector<double> dea;   // DEA线(信号线)
    std::vector<double> hist;  // MACD柱状图
};

inline MACDResult CalcMACD(const std::vector<double>& close,
                           int fast = 12, int slow = 26, int signal = 9) {
    MACDResult r;
    auto ema_fast = CalcEMA(close, fast);
    auto ema_slow = CalcEMA(close, slow);
    r.dif.resize(close.size());
    for (size_t i = 0; i < close.size(); ++i)
        r.dif[i] = ema_fast[i] - ema_slow[i];
    r.dea = CalcEMA(r.dif, signal);
    r.hist.resize(close.size());
    for (size_t i = 0; i < close.size(); ++i)
        r.hist[i] = (r.dif[i] - r.dea[i]) * 2;
    return r;
}

// ============================================================================
// 布林带计算
// ============================================================================

struct BollingerResult {
    std::vector<double> upper;
    std::vector<double> mid;
    std::vector<double> lower;
};

inline BollingerResult CalcBollinger(const std::vector<double>& close,
                                     int period = 20, double nbdev = 2.0) {
    BollingerResult r;
    r.mid = CalcSMA(close, period);
    r.upper.resize(close.size());
    r.lower.resize(close.size());
    for (size_t i = 0; i < close.size(); ++i) {
        if ((int)i < period - 1) {
            r.upper[i] = r.mid[i];
            r.lower[i] = r.mid[i];
            continue;
        }
        double sum_sq = 0;
        for (int j = 0; j < period; ++j) {
            double diff = close[i - j] - r.mid[i];
            sum_sq += diff * diff;
        }
        double stddev = std::sqrt(sum_sq / period);
        r.upper[i] = r.mid[i] + nbdev * stddev;
        r.lower[i] = r.mid[i] - nbdev * stddev;
    }
    return r;
}

// ============================================================================
// SAR (抛物线转向) 简化计算
// ============================================================================

inline std::vector<double> CalcSAR(const std::vector<double>& high,
                                    const std::vector<double>& low,
                                    double af_step = 0.02, double af_max = 0.2) {
    size_t n = high.size();
    std::vector<double> sar(n, 0.0);
    if (n < 2) return sar;

    bool is_long = true;
    double af = af_step;
    double ep = high[0];
    sar[0] = low[0];

    for (size_t i = 1; i < n; ++i) {
        sar[i] = sar[i - 1] + af * (ep - sar[i - 1]);
        if (is_long) {
            if (high[i] > ep) { ep = high[i]; af = std::min(af + af_step, af_max); }
            if (low[i] < sar[i]) {
                is_long = false;
                sar[i] = ep;
                ep = low[i];
                af = af_step;
            }
        } else {
            if (low[i] < ep) { ep = low[i]; af = std::min(af + af_step, af_max); }
            if (high[i] > sar[i]) {
                is_long = true;
                sar[i] = ep;
                ep = high[i];
                af = af_step;
            }
        }
    }
    return sar;
}

// ============================================================================
// 滚动最高/最低价
// ============================================================================

inline std::vector<double> RollingMax(const std::vector<double>& data, int window) {
    std::vector<double> result(data.size(), 0.0);
    for (size_t i = 0; i < data.size(); ++i) {
        int start = std::max(0, (int)i - window + 1);
        double mx = data[start];
        for (int j = start + 1; j <= (int)i; ++j)
            mx = std::max(mx, data[j]);
        result[i] = mx;
    }
    return result;
}

inline std::vector<double> RollingMin(const std::vector<double>& data, int window) {
    std::vector<double> result(data.size(), 0.0);
    for (size_t i = 0; i < data.size(); ++i) {
        int start = std::max(0, (int)i - window + 1);
        double mn = data[start];
        for (int j = start + 1; j <= (int)i; ++j)
            mn = std::min(mn, data[j]);
        result[i] = mn;
    }
    return result;
}

// ============================================================================
// 平均成交量
// ============================================================================

inline double CalcAvgVolume(const std::vector<Bar>& bars, int window = 60) {
    if (bars.empty()) return 0;
    int n = std::min(window, (int)bars.size());
    double sum = 0;
    for (int i = (int)bars.size() - n; i < (int)bars.size(); ++i)
        sum += bars[i].volume;
    return sum / n;
}

// ============================================================================
// IndicatorCache — 预计算指标缓存，供策略快速查询
// ============================================================================

class IndicatorCache {
public:
    void Compute(const std::vector<Bar>& bars) {
        size_t n = bars.size();
        close_.resize(n); high_.resize(n); low_.resize(n); vol_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            close_[i] = bars[i].close;
            high_[i]  = bars[i].high;
            low_[i]   = bars[i].low;
            vol_[i]   = bars[i].volume;
        }

        ema13_ = CalcEMA(close_, 13);
        rsi6_  = CalcRSI(close_, 6);
        rsi9_  = CalcRSI(close_, 9);
        sma10_ = CalcSMA(close_, 10);
        sma34_ = CalcSMA(close_, 34);
        ema10_ = CalcEMA(close_, 10);
        ema20_ = CalcEMA(close_, 20);
        sar_   = CalcSAR(high_, low_);
        macd_  = CalcMACD(close_);
        boll_  = CalcBollinger(close_);
        hhv60_ = RollingMax(close_, 60);
        llv60_ = RollingMin(close_, 60);
        avg_vol_60_ = CalcAvgVolume(std::vector<Bar>(), 0); // 需要bars
        
        // 按bars计算滚动平均成交量
        avg_vol_.resize(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            int start = std::max(0, (int)i - 59);
            double sum = 0;
            for (int j = start; j <= (int)i; ++j) sum += vol_[j];
            avg_vol_[i] = sum / (i - start + 1);
        }

        // Elder-Ray: Bull Power = High - EMA13, Bear Power = Low - EMA13
        bull_power_.resize(n); bear_power_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            bull_power_[i] = high_[i] - ema13_[i];
            bear_power_[i] = low_[i]  - ema13_[i];
        }
    }

    // 获取指标值
    double Close(int i)      const { return safe(close_, i); }
    double High(int i)       const { return safe(high_, i); }
    double Low(int i)        const { return safe(low_, i); }
    double EMA13(int i)      const { return safe(ema13_, i); }
    double RSI6(int i)       const { return safe(rsi6_, i); }
    double RSI9(int i)       const { return safe(rsi9_, i); }
    double SMA10(int i)      const { return safe(sma10_, i); }
    double SMA34(int i)      const { return safe(sma34_, i); }
    double SAR(int i)        const { return safe(sar_, i); }
    double BullPower(int i)  const { return safe(bull_power_, i); }
    double BearPower(int i)  const { return safe(bear_power_, i); }
    double AvgVol(int i)     const { return safe(avg_vol_, i); }
    double HHV60(int i)      const { return safe(hhv60_, i); }
    double LLV60(int i)      const { return safe(llv60_, i); }
    const MACDResult& MACD() const { return macd_; }
    const BollingerResult& Boll() const { return boll_; }
    int Size() const { return (int)close_.size(); }

private:
    double safe(const std::vector<double>& v, int i) const {
        return (i >= 0 && i < (int)v.size()) ? v[i] : 0.0;
    }

    std::vector<double> close_, high_, low_, vol_;
    std::vector<double> ema13_, rsi6_, rsi9_, sma10_, sma34_;
    std::vector<double> ema10_, ema20_, sar_;
    std::vector<double> bull_power_, bear_power_;
    std::vector<double> avg_vol_, hhv60_, llv60_;
    MACDResult macd_;
    BollingerResult boll_;
    double avg_vol_60_ = 0;
};
