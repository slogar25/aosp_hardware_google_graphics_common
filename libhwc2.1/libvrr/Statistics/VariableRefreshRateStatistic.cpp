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

#include "VariableRefreshRateStatistic.h"

namespace android::hardware::graphics::composer {

VariableRefreshRateStatistic::VariableRefreshRateStatistic(
        CommonDisplayContextProvider* displayContextProvider, EventQueue* eventQueue,
        int maxFrameRate, int maxTeFrequency, int64_t updatePeriodNs)
      : mDisplayContextProvider(displayContextProvider),
        mEventQueue(eventQueue),
        mMaxFrameRate(maxFrameRate),
        mMaxTeFrequency(maxTeFrequency),
        mMinFrameIntervalNs(roundDivide(std::nano::den, static_cast<int64_t>(maxFrameRate))),
        mTeFrequency(maxFrameRate),
        mTeIntervalNs(roundDivide(std::nano::den, static_cast<int64_t>(mTeFrequency))),
        mUpdatePeriodNs(updatePeriodNs) {
    mStartStatisticTimeNs = getBootClockTimeNs();

    // For debugging purposes, this will only be triggered when DEBUG_VRR_STATISTICS is defined.
#ifdef DEBUG_VRR_STATISTICS
    auto configs = mDisplayContextProvider->getDisplayConfigs();
    for (const auto& config : *configs) {
        ALOGI("VariableRefreshRateStatistic: config id = %d : %s", config.first,
              config.second.toString().c_str());
    }
    mUpdateEvent.mEventType = VrrControllerEventType::kStaticticUpdate;
    mUpdateEvent.mFunctor =
            std::move(std::bind(&VariableRefreshRateStatistic::updateStatistic, this));
    mUpdateEvent.mWhenNs = getSteadyClockTimeNs() + mUpdatePeriodNs;
    mEventQueue->mPriorityQueue.emplace(mUpdateEvent);
#endif
    mStatistics[mDisplayPresentProfile] = DisplayPresentRecord();
}

uint64_t VariableRefreshRateStatistic::getPowerOffDurationNs() const {
    if (isPowerModeOffNowLocked()) {
        const auto& item = mStatistics.find(mDisplayPresentProfile);
        if (item == mStatistics.end()) {
            ALOGE("%s We should have inserted power-off item in constructor.", __func__);
            return 0;
        }
        return mPowerOffDurationNs +
                (getBootClockTimeNs() - item->second.mLastTimeStampInBootClockNs);
    } else {
        return mPowerOffDurationNs;
    }
}

uint64_t VariableRefreshRateStatistic::getStartStatisticTimeNs() const {
    return mStartStatisticTimeNs;
}

DisplayPresentStatistics VariableRefreshRateStatistic::getStatistics() {
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    return mStatistics;
}

DisplayPresentStatistics VariableRefreshRateStatistic::getUpdatedStatistics() {
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    DisplayPresentStatistics updatedStatistics;
    for (auto& it : mStatistics) {
        if (it.second.mUpdated) {
            if (it.first.mNumVsync < 0) {
                it.second.mAccumulatedTimeNs = getPowerOffDurationNs();
            }
            updatedStatistics[it.first] = it.second;
            it.second.mUpdated = false;
        }
    }
    if (isPowerModeOffNowLocked()) {
        mStatistics[mDisplayPresentProfile].mUpdated = true;
    }
    return std::move(updatedStatistics);
}

void VariableRefreshRateStatistic::onPowerStateChange(int from, int to) {
    if (from == to) {
        return;
    }
    if (mDisplayPresentProfile.mCurrentDisplayConfig.mPowerMode != from) {
        ALOGE("%s Power mode mismatch between storing state(%d) and actual mode(%d)", __func__,
              mDisplayPresentProfile.mCurrentDisplayConfig.mPowerMode, from);
    }
    updateIdleStats();
    std::scoped_lock lock(mMutex);
    if (isPowerModeOff(to)) {
        // Currently the for power stats both |HWC_POWER_MODE_OFF| and |HWC_POWER_MODE_DOZE_SUSPEND|
        // are classified as "off" states in power statistics. Consequently,we assign the value of
        // |HWC_POWER_MODE_OFF| to |mPowerMode| when it is |HWC_POWER_MODE_DOZE_SUSPEND|.
        mDisplayPresentProfile.mCurrentDisplayConfig.mPowerMode = HWC_POWER_MODE_OFF;

        auto& record = mStatistics[mDisplayPresentProfile];
        ++record.mCount;
        record.mLastTimeStampInBootClockNs = getBootClockTimeNs();
        record.mUpdated = true;

        mLastPresentTimeInBootClockNs = kDefaultInvalidPresentTimeNs;
    } else {
        if (isPowerModeOff(from)) {
            mPowerOffDurationNs +=
                    (getBootClockTimeNs() -
                     mStatistics[mDisplayPresentProfile].mLastTimeStampInBootClockNs);
        }
        mDisplayPresentProfile.mCurrentDisplayConfig.mPowerMode = to;
        if (to == HWC_POWER_MODE_DOZE) {
            mDisplayPresentProfile.mNumVsync = mTeFrequency;
            auto& record = mStatistics[mDisplayPresentProfile];
            ++record.mCount;
            record.mLastTimeStampInBootClockNs = getBootClockTimeNs();
            record.mUpdated = true;
        }
    }
}

void VariableRefreshRateStatistic::onPresent(int64_t presentTimeNs, int flag) {
    int64_t presentTimeInBootClockNs = steadyClockTimeToBootClockTimeNs(presentTimeNs);
    if (mLastPresentTimeInBootClockNs == kDefaultInvalidPresentTimeNs) {
        mLastPresentTimeInBootClockNs = presentTimeInBootClockNs;
        updateCurrentDisplayStatus();
        // Ignore first present after resume
        return;
    }
    updateIdleStats(presentTimeInBootClockNs);
    updateCurrentDisplayStatus();
    if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
        // In low power mode, panel boost to 30 Hz while presenting new frame.
        mDisplayPresentProfile.mNumVsync = mTeFrequency / kFrameRateWhenPresentAtLpMode;
        mLastPresentTimeInBootClockNs =
                presentTimeInBootClockNs + (std::nano::den / kFrameRateWhenPresentAtLpMode);
    } else {
        int numVsync = roundDivide((presentTimeInBootClockNs - mLastPresentTimeInBootClockNs),
                                   mTeIntervalNs);
        numVsync = std::max(1, std::min(mTeFrequency, numVsync));
        mDisplayPresentProfile.mNumVsync = numVsync;
        mLastPresentTimeInBootClockNs = presentTimeInBootClockNs;
    }
    {
        std::scoped_lock lock(mMutex);

        auto& record = mStatistics[mDisplayPresentProfile];
        ++record.mCount;
        record.mAccumulatedTimeNs += (mTeIntervalNs * mDisplayPresentProfile.mNumVsync);
        record.mLastTimeStampInBootClockNs = presentTimeInBootClockNs;
        record.mUpdated = true;
        if (hasPresentFrameFlag(flag, PresentFrameFlag::kPresentingWhenDoze)) {
            // After presenting a frame in AOD, we revert back to 1 Hz operation.
            mDisplayPresentProfile.mNumVsync = mTeFrequency;
            auto& record = mStatistics[mDisplayPresentProfile];
            ++record.mCount;
            record.mLastTimeStampInBootClockNs = mLastPresentTimeInBootClockNs;
            record.mUpdated = true;
        }
    }
}

void VariableRefreshRateStatistic::setActiveVrrConfiguration(int activeConfigId, int teFrequency) {
    updateIdleStats();
    mDisplayPresentProfile.mCurrentDisplayConfig.mActiveConfigId = activeConfigId;
    mTeFrequency = teFrequency;
    if (mTeFrequency % mMaxFrameRate != 0) {
        ALOGW("%s TE frequency does not align with the maximum frame rate as a multiplier.",
              __func__);
    }
    mTeIntervalNs = roundDivide(std::nano::den, static_cast<int64_t>(mTeFrequency));
    // TODO(b/333204544): how can we handle the case if mTeFrequency % mMinimumRefreshRate != 0?
    if ((mMinimumRefreshRate > 0) && (mTeFrequency % mMinimumRefreshRate != 0)) {
        ALOGW("%s TE frequency does not align with the lowest frame rate as a multiplier.",
              __func__);
    }
}

void VariableRefreshRateStatistic::setFixedRefreshRate(uint32_t rate) {
    if (mMinimumRefreshRate != rate) {
        updateIdleStats();
        mMinimumRefreshRate = rate;
        if (mMinimumRefreshRate > 1) {
            mMaximumFrameIntervalNs =
                    roundDivide(std::nano::den, static_cast<int64_t>(mMinimumRefreshRate));
            // TODO(b/333204544): how can we handle the case if mTeFrequency % mMinimumRefreshRate
            // != 0?
            if (mTeFrequency % mMinimumRefreshRate != 0) {
                ALOGW("%s TE frequency does not align with the lowest frame rate as a multiplier.",
                      __func__);
            }
        } else {
            mMaximumFrameIntervalNs = kMaxPresentIntervalNs;
        }
    }
}

bool VariableRefreshRateStatistic::isPowerModeOffNowLocked() const {
    return isPowerModeOff(mDisplayPresentProfile.mCurrentDisplayConfig.mPowerMode);
}

void VariableRefreshRateStatistic::updateCurrentDisplayStatus() {
    mDisplayPresentProfile.mCurrentDisplayConfig.mBrightnessMode =
            mDisplayContextProvider->getBrightnessMode();
    if (mDisplayPresentProfile.mCurrentDisplayConfig.mBrightnessMode ==
        BrightnessMode::kInvalidBrightnessMode) {
        mDisplayPresentProfile.mCurrentDisplayConfig.mBrightnessMode =
                BrightnessMode::kNormalBrightnessMode;
    }
}

void VariableRefreshRateStatistic::updateIdleStats(int64_t endTimeStampInBootClockNs) {
    if (mDisplayPresentProfile.isOff()) return;
    if (mLastPresentTimeInBootClockNs == kDefaultInvalidPresentTimeNs) return;

    endTimeStampInBootClockNs =
            endTimeStampInBootClockNs < 0 ? getBootClockTimeNs() : endTimeStampInBootClockNs;
    auto durationFromLastPresentNs = endTimeStampInBootClockNs - mLastPresentTimeInBootClockNs;
    durationFromLastPresentNs = durationFromLastPresentNs < 0 ? 0 : durationFromLastPresentNs;
    if (mDisplayPresentProfile.mCurrentDisplayConfig.mPowerMode == HWC_POWER_MODE_DOZE) {
        mDisplayPresentProfile.mNumVsync = mTeFrequency;

        std::scoped_lock lock(mMutex);

        auto& record = mStatistics[mDisplayPresentProfile];
        record.mAccumulatedTimeNs += durationFromLastPresentNs;
        record.mLastTimeStampInBootClockNs = mLastPresentTimeInBootClockNs;
        mLastPresentTimeInBootClockNs = endTimeStampInBootClockNs;
        record.mUpdated = true;
    } else {
        int numVsync = roundDivide(durationFromLastPresentNs, mTeIntervalNs);
        mDisplayPresentProfile.mNumVsync =
                (mMinimumRefreshRate > 1 ? (mTeFrequency / mMinimumRefreshRate) : mTeFrequency);
        if (numVsync <= mDisplayPresentProfile.mNumVsync) return;

        // Ensure that the last vsync should not be included now, since it would be processed for
        // next update or |onPresent|
        auto count = (numVsync - 1) / mDisplayPresentProfile.mNumVsync;
        auto alignedDurationNs = mMaximumFrameIntervalNs * count;
        {
            std::scoped_lock lock(mMutex);

            auto& record = mStatistics[mDisplayPresentProfile];
            record.mCount += count;
            record.mAccumulatedTimeNs += alignedDurationNs;
            mLastPresentTimeInBootClockNs += alignedDurationNs;
            record.mLastTimeStampInBootClockNs = mLastPresentTimeInBootClockNs;
            record.mUpdated = true;
        }
    }
}

#ifdef DEBUG_VRR_STATISTICS
int VariableRefreshRateStatistic::updateStatistic() {
    updateIdleStats();
    for (const auto& it : mStatistics) {
        const auto& key = it.first;
        const auto& value = it.second;
        ALOGD("%s: power mode = %d, id = %d, birghtness mode = %d, vsync "
              "= %d : count = %ld, last entry time =  %ld",
              __func__, key.mCurrentDisplayConfig.mPowerMode,
              key.mCurrentDisplayConfig.mActiveConfigId, key.mCurrentDisplayConfig.mBrightnessMode,
              key.mNumVsync, value.mCount, value.mLastTimeStampInBootClockNs);
    }
    // Post next update statistics event.
    mUpdateEvent.mWhenNs = getSteadyClockTimeNs() + mUpdatePeriodNs;
    mEventQueue->mPriorityQueue.emplace(mUpdateEvent);

    return NO_ERROR;
}
#endif

} // namespace android::hardware::graphics::composer
