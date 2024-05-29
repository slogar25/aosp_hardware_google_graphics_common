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

#include "PeriodRefreshRateCalculator.h"

#include "../Utils.h"

namespace android::hardware::graphics::composer {

PeriodRefreshRateCalculator::PeriodRefreshRateCalculator(
        EventQueue* eventQueue, const PeriodRefreshRateCalculatorParameters& params)
      : mEventQueue(eventQueue), mParams(params) {
    mName = "RefreshRateCalculator-Period";

    mMeasureEvent.mEventType = VrrControllerEventType::kPeriodRefreshRateCalculatorUpdate;
    mLastMeasureTimeNs = getSteadyClockTimeNs() + params.mMeasurePeriodNs;
    mMeasureEvent.mWhenNs = mLastMeasureTimeNs;
    mMeasureEvent.mFunctor = std::move(std::bind(&PeriodRefreshRateCalculator::onMeasure, this));

    mConfidenceThresholdTimeNs = mParams.mMeasurePeriodNs * mParams.mConfidencePercentage / 100;
}

int PeriodRefreshRateCalculator::getRefreshRate() const {
    return mLastRefreshRate;
}

void PeriodRefreshRateCalculator::onPowerStateChange(int from, int to) {
    if (to != HWC_POWER_MODE_NORMAL) {
        // We bypass inspection of the previous power state , as it is irrelevant to discard events
        // when there is no event.
        setEnabled(false);
    } else {
        if (from == HWC_POWER_MODE_NORMAL) {
            ALOGE("Disregard power state change notification by staying current power state.");
            return;
        }
        setEnabled(true);
    }
}

void PeriodRefreshRateCalculator::onPresentInternal(int64_t presentTimeNs, int flag) {
    if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
        return;
    }
    if (mLastPresentTimeNs >= 0) {
        auto periodNs = presentTimeNs - mLastPresentTimeNs;
        if (periodNs <= std::nano::den) {
            int numVsync = std::max(mMinVsyncNum, durationToVsync(periodNs));
            // current frame rate is |mVsyncRate/numVsync|
            ++mStatistics[Fraction<int>(mVsyncRate, numVsync)];
        }
    }
    mLastPresentTimeNs = presentTimeNs;
}

void PeriodRefreshRateCalculator::reset() {
    mStatistics.clear();
    mLastRefreshRate = kDefaultInvalidRefreshRate;
    mLastPresentTimeNs = kDefaultInvalidPresentTimeNs;
}

void PeriodRefreshRateCalculator::setEnabled(bool isEnabled) {
    if (!isEnabled) {
        mEventQueue->dropEvent(VrrControllerEventType::kPeriodRefreshRateCalculatorUpdate);
    } else {
        mLastMeasureTimeNs = getSteadyClockTimeNs() + mParams.mMeasurePeriodNs;
        mMeasureEvent.mWhenNs = mLastMeasureTimeNs;
        mMeasureEvent.mFunctor =
                std::move(std::bind(&PeriodRefreshRateCalculator::onMeasure, this));
        mEventQueue->mPriorityQueue.emplace(mMeasureEvent);
    }
}

int PeriodRefreshRateCalculator::onMeasure() {
    int currentRefreshRate = kDefaultInvalidRefreshRate;
    int totalPresent = 0;
    int64_t totalDurationNs = 0;
    int maxOccurrence = 0;
    Fraction<int> majorRefreshRate;

    for (const auto& [rate, count] : mStatistics) {
        totalPresent += count;
        auto durationNs = freqToDurationNs(rate);
        totalDurationNs += durationNs * count;
        if (count > maxOccurrence) {
            maxOccurrence = count;
            majorRefreshRate = rate;
        }
    }
    if (totalPresent > 0 && (totalDurationNs > mConfidenceThresholdTimeNs)) {
        if (mParams.mType == PeriodRefreshRateCalculatorType::kAverage) {
            if (mParams.mMeasurePeriodNs > totalDurationNs * 2) {
                // avoid sudden high jumping when it's actually idle since last present for more
                // than half of the measure period.
                totalDurationNs = mParams.mMeasurePeriodNs;
                totalPresent++;
            }
            auto avgDurationNs = roundDivide(totalDurationNs, static_cast<int64_t>(totalPresent));
            currentRefreshRate = durationNsToFreq(avgDurationNs);
        } else {
            currentRefreshRate = majorRefreshRate.round();
        }
    }
    mStatistics.clear();
    currentRefreshRate = std::max(currentRefreshRate, 1);
    currentRefreshRate = std::min(currentRefreshRate, mMaxFrameRate);
    setNewRefreshRate(currentRefreshRate);

    // Prepare next measurement event.
    mLastMeasureTimeNs += mParams.mMeasurePeriodNs;
    mMeasureEvent.mWhenNs = mLastMeasureTimeNs;
    mEventQueue->mPriorityQueue.emplace(mMeasureEvent);
    return NO_ERROR;
}

void PeriodRefreshRateCalculator::setNewRefreshRate(int newRefreshRate) {
    if ((newRefreshRate != mLastRefreshRate) || mParams.mAlwaysCallback) {
        mLastRefreshRate = newRefreshRate;
        ATRACE_INT(mName.c_str(), newRefreshRate);
        if (mRefreshRateChangeCallback) {
            mRefreshRateChangeCallback(mLastRefreshRate);
        }
    }
}

} // namespace android::hardware::graphics::composer
