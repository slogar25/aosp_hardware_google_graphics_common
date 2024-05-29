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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "ExitIdleRefreshRateCalculator.h"

#include <algorithm>

namespace android::hardware::graphics::composer {

ExitIdleRefreshRateCalculator::ExitIdleRefreshRateCalculator(EventQueue* eventQueue)
      : ExitIdleRefreshRateCalculator(eventQueue, ExitIdleRefreshRateCalculatorParameters()) {}

ExitIdleRefreshRateCalculator::ExitIdleRefreshRateCalculator(
        EventQueue* eventQueue, const ExitIdleRefreshRateCalculatorParameters& params)
      : mEventQueue(eventQueue), mParams(params) {
    mName = "RefreshRateCalculator-ExitIdle";
    mTimeoutEvent.mEventType = VrrControllerEventType::kExitIdleRefreshRateCalculatorUpdate;
    mTimeoutEvent.mFunctor =
            std::move(std::bind(&ExitIdleRefreshRateCalculator::invalidateRefreshRate, this));
}

int ExitIdleRefreshRateCalculator::getRefreshRate() const {
    return mLastRefreshRate;
}

void ExitIdleRefreshRateCalculator::onPowerStateChange(int from, int to) {
    if (to != HWC_POWER_MODE_NORMAL) {
        setEnabled(false);
    } else {
        if (from == HWC_POWER_MODE_NORMAL) {
            ALOGE("Disregard power state change notification by staying current power state.");
            return;
        }
        setEnabled(true);
    }
}

void ExitIdleRefreshRateCalculator::onPresentInternal(int64_t presentTimeNs, int flag) {
    if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
        return;
    }
    if ((mLastPresentTimeNs == kDefaultInvalidPresentTimeNs) ||
        (presentTimeNs > mLastPresentTimeNs + mParams.mIdleCriteriaTimeNs)) {
        setNewRefreshRate(mMaxFrameRate);

        mTimeoutEvent.mWhenNs = presentTimeNs + mParams.mMaxValidTimeNs;
        mEventQueue->mPriorityQueue.emplace(mTimeoutEvent);
    }
    mLastPresentTimeNs = presentTimeNs;
}

void ExitIdleRefreshRateCalculator::reset() {
    mLastPresentTimeNs = kDefaultInvalidPresentTimeNs;
    setNewRefreshRate(kDefaultInvalidRefreshRate);
}

void ExitIdleRefreshRateCalculator::setEnabled(bool isEnabled) {
    if (!isEnabled) {
        mEventQueue->dropEvent(VrrControllerEventType::kExitIdleRefreshRateCalculatorUpdate);
    } else {
        reset();
    }
}

void ExitIdleRefreshRateCalculator::setNewRefreshRate(int newRefreshRate) {
    if (newRefreshRate != mLastRefreshRate) {
        mLastRefreshRate = newRefreshRate;
        ATRACE_INT(mName.c_str(), newRefreshRate);
        if (mRefreshRateChangeCallback) {
            mRefreshRateChangeCallback(newRefreshRate);
        }
    }
}

int ExitIdleRefreshRateCalculator::invalidateRefreshRate() {
    setNewRefreshRate(kDefaultInvalidRefreshRate);
    return NO_ERROR;
}

} // namespace android::hardware::graphics::composer
