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

#include "VideoFrameRateCalculator.h"

#include <numeric>

#include "../Utils.h"

namespace android::hardware::graphics::composer {

VideoFrameRateCalculator::VideoFrameRateCalculator(EventQueue* eventQueue,
                                                   const VideoFrameRateCalculatorParameters& params)
      : mEventQueue(eventQueue), mParams(params) {
    mName = "RefreshRateCalculator-Video";

    mParams.mMaxInterestedFrameRate = std::min(mMaxFrameRate, mParams.mMaxInterestedFrameRate);
    mParams.mMinInterestedFrameRate = std::max(1, mParams.mMinInterestedFrameRate);

    mRefreshRateCalculator =
            std::make_unique<PeriodRefreshRateCalculator>(mEventQueue, params.mPeriodParams);
    mRefreshRateCalculator->setName("RefreshRateCalculator-Period-Worker");
    mRefreshRateCalculator->registerRefreshRateChangeCallback(
            std::bind(&VideoFrameRateCalculator::onReportRefreshRate, this, std::placeholders::_1));
}

int VideoFrameRateCalculator::getRefreshRate() const {
    if ((mLastVideoFrameRate >= mParams.mMinInterestedFrameRate) &&
        (mLastVideoFrameRate <= mParams.mMaxInterestedFrameRate)) {
        return mLastVideoFrameRate;
    }
    return kDefaultInvalidRefreshRate;
}

void VideoFrameRateCalculator::onPowerStateChange(int from, int to) {
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

void VideoFrameRateCalculator::onPresentInternal(int64_t presentTimeNs, int flag) {
    if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
        return;
    }
    if (hasPresentFrameFlag(flag, PresentFrameFlag::kIsYuv)) {
        mRefreshRateCalculator->onPresentInternal(presentTimeNs, flag);
    } else {
        reset();
    }
}

void VideoFrameRateCalculator::reset() {
    setNewRefreshRate(kDefaultInvalidRefreshRate);
    mLastPeriodFrameRate = kDefaultInvalidRefreshRate;
    mLastPeriodFrameRateRuns = 0;
    mHistory.clear();
}

void VideoFrameRateCalculator::setEnabled(bool isEnabled) {
    mRefreshRateCalculator->setEnabled(isEnabled);
}

void VideoFrameRateCalculator::setVrrConfigAttributes(int64_t vsyncPeriodNs,
                                                      int64_t minFrameIntervalNs) {
    RefreshRateCalculator::setVrrConfigAttributes(vsyncPeriodNs, minFrameIntervalNs);

    mRefreshRateCalculator->setVrrConfigAttributes(vsyncPeriodNs, minFrameIntervalNs);
}

int VideoFrameRateCalculator::onReportRefreshRate(int refreshRate) {
    if ((mLastPeriodFrameRate != kDefaultInvalidRefreshRate) &&
        (std::abs(mLastPeriodFrameRate - refreshRate) <= mParams.mDelta) &&
        (mLastPeriodFrameRate >= mParams.mMinInterestedFrameRate) &&
        (mLastPeriodFrameRate <= mParams.mMaxInterestedFrameRate)) {
        ++mLastPeriodFrameRateRuns;
        mHistory.push_back(refreshRate);
        while (mHistory.size() > mParams.mWindowSize) {
            mHistory.pop_front();
        }
        if (mLastPeriodFrameRateRuns >= mParams.mMinStableRuns) {
            int sum = std::accumulate(std::begin(mHistory), std::end(mHistory), 0);
            mLastPeriodFrameRate = std::round(sum / static_cast<float>(mHistory.size()));
            setNewRefreshRate(mLastPeriodFrameRate);
        }
    } else {
        mLastPeriodFrameRate = refreshRate;
        mLastPeriodFrameRateRuns = 1;
        setNewRefreshRate(kDefaultInvalidRefreshRate);
        mHistory.clear();
        mHistory.push_back(refreshRate);
    }
    return NO_ERROR;
}

void VideoFrameRateCalculator::setNewRefreshRate(int newRefreshRate) {
    if (newRefreshRate != mLastVideoFrameRate) {
        mLastVideoFrameRate = newRefreshRate;
        ATRACE_INT(mName.c_str(), newRefreshRate);
        if (mRefreshRateChangeCallback) {
            if ((mLastVideoFrameRate >= mParams.mMinInterestedFrameRate) &&
                (mLastVideoFrameRate <= mParams.mMaxInterestedFrameRate)) {
                mRefreshRateChangeCallback(mLastVideoFrameRate);
            }
        }
    }
}

} // namespace android::hardware::graphics::composer
