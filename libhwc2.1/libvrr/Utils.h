/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include "interface/Event.h"

inline void clearBit(uint32_t& data, uint32_t bit) {
    data &= ~(1L << (bit));
}

inline void setBit(uint32_t& data, uint32_t bit) {
    data |= (1L << (bit));
}

inline void setBitField(uint32_t& data, uint32_t value, uint32_t offset, uint32_t fieldMask) {
    data = (data & ~fieldMask) | (((value << offset) & fieldMask));
}

namespace android::hardware::graphics::composer {

struct TimedEvent;

constexpr int64_t kMillisecondToNanoSecond = 1000000;

enum PresentFrameFlag {
    kUpdateRefreshRateIndicatorLayerOnly = (1 << 0),
    kIsYuv = (1 << 1),
    kPresentingWhenDoze = (1 << 2),
};

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T roundDivide(T divident, T divisor) {
    if (divident < 0 || divisor <= 0) {
        return 0;
    }
    return (divident + (divisor / 2)) / divisor;
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
struct Fraction {
    T mNum;
    T mDen;

    Fraction(T num = 0, T denom = 1) : mNum(num), mDen(denom) {
        if (mDen < 0) {
            mNum = -mNum;
            mDen = -mDen;
        }
    }

    T round() { return roundDivide(mNum, mDen); }

    bool operator<(const Fraction<T>& other) const { return mNum * other.mDen < other.mNum * mDen; }

    bool operator==(const Fraction<T>& other) const {
        return mNum * other.mDen == other.mNum * mDen;
    }
};

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
int64_t freqToDurationNs(Fraction<T> freq) {
    return roundDivide(std::nano::den * static_cast<int64_t>(freq.mDen),
                       static_cast<int64_t>(freq.mNum));
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T durationNsToFreq(T durationNs) {
    auto res = roundDivide(std::nano::den, static_cast<int64_t>(durationNs));
    return static_cast<T>(res);
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T freqToDurationNs(T freq) {
    auto res = roundDivide(std::nano::den, static_cast<int64_t>(freq));
    return static_cast<T>(res);
}

int64_t getSteadyClockTimeMs();
int64_t getSteadyClockTimeNs();

int64_t getBootClockTimeMs();
int64_t getBootClockTimeNs();

int64_t steadyClockTimeToBootClockTimeNs(int64_t steadyClockTimeNs);

bool hasPresentFrameFlag(int flag, PresentFrameFlag target);

bool isPowerModeOff(int powerMode);

void setTimedEventWithAbsoluteTime(TimedEvent& event);

} // namespace android::hardware::graphics::composer
