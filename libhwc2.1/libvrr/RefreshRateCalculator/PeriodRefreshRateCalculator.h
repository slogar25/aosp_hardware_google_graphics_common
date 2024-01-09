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

#include <stdint.h>
#include <chrono>
#include <map>

#include "../EventQueue.h"
#include "RefreshRateCalculator.h"

namespace android::hardware::graphics::composer {

enum PeriodRefreshRateCalculatorType {
    kAverage = 0,
    kMajor,
    kTotal,
};

struct PeriodRefreshRateCalculatorParameters {
    PeriodRefreshRateCalculatorType mType = PeriodRefreshRateCalculatorType::kAverage;
    int64_t mMeasurePeriodNs = 500000000; // default is 500 ms.
    // When the presented time percentage exceeds or equals to this value, the Calculator becomes
    // effective; otherwise, return kDefaultInvalidRefreshRate.
    int mConfidencePercentage = 50;
    bool mAlwaysCallback = false;
};

class PeriodRefreshRateCalculator : public RefreshRateCalculator {
public:
    PeriodRefreshRateCalculator(EventQueue* eventQueue)
          : PeriodRefreshRateCalculator(eventQueue, PeriodRefreshRateCalculatorParameters()) {}

    PeriodRefreshRateCalculator(EventQueue* eventQueue,
                                const PeriodRefreshRateCalculatorParameters& params);

    int getRefreshRate() const final;

    void onPresent(int64_t presentTimeNs, int flag) override;

    virtual void reset() override;

private:
    int onMeasure();

    void setNewRefreshRate(int newRefreshRate);

    EventQueue* mEventQueue;
    PeriodRefreshRateCalculatorParameters mParams;
    VrrControllerEvent mMeasureEvent;

    std::map<int, int> mStatistics;

    int64_t mLastPresentTimeNs = kDefaultInvalidPresentTimeNs;
    int mLastRefreshRate = kDefaultInvalidRefreshRate;
    // Regulate the frequency of measurements.
    int mNumVsyncPerMeasure;
    float mMeasurePeriodRatio;
    // Control then next measurement.
    int64_t mLastMeasureTimeNs;
};

} // namespace android::hardware::graphics::composer
