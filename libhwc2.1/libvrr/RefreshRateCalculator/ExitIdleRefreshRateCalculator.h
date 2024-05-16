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

#include "RefreshRateCalculator.h"

#include "../EventQueue.h"
#include "../Utils.h"

namespace android::hardware::graphics::composer {

struct ExitIdleRefreshRateCalculatorParameters {
    int64_t mIdleCriteriaTimeNs = 1000000000; // 1 second
    int64_t mMaxValidTimeNs = 250000000;      // 250 ms
};

class ExitIdleRefreshRateCalculator : public RefreshRateCalculator {
public:
    ExitIdleRefreshRateCalculator(EventQueue* eventQueue);

    ExitIdleRefreshRateCalculator(EventQueue* eventQueue,
                                  const ExitIdleRefreshRateCalculatorParameters& params);

    int getRefreshRate() const override;

    void onPowerStateChange(int from, int to) final;

    void onPresentInternal(int64_t presentTimeNs, int flag) override;

    void reset() override;

    void setEnabled(bool isEnabled) final;

private:
    ExitIdleRefreshRateCalculator(const ExitIdleRefreshRateCalculator&) = delete;
    ExitIdleRefreshRateCalculator& operator=(const ExitIdleRefreshRateCalculator&) = delete;

    void setNewRefreshRate(int newRefreshRate);

    int invalidateRefreshRate();

    EventQueue* mEventQueue;
    VrrControllerEvent mTimeoutEvent;

    const ExitIdleRefreshRateCalculatorParameters mParams;

    int64_t mLastPresentTimeNs = kDefaultInvalidPresentTimeNs;
    int mLastRefreshRate = kDefaultInvalidRefreshRate;
};

} // namespace android::hardware::graphics::composer
