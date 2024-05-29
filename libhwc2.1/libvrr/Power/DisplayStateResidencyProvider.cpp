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

#include <stdlib.h>

#include "DisplayStateResidencyProvider.h"

namespace android::hardware::graphics::composer {

// Currently, the FPS ranges from [1, |kMaxFrameRate| = 120], and the maximum TE
// frequency(|kMaxTefrequency|) = 240. We express fps by dividing the maximum TE by the number of
// vsync. Here, the numerator is set to |kMaxTefrequency|, fraction reduction is not needed here.
const std::set<Fraction<int>> DisplayStateResidencyProvider::kFpsMappingTable =
        {{240, 240}, {240, 120}, {240, 24}, {240, 10}, {240, 8}, {240, 7},
         {240, 6},   {240, 5},   {240, 4},  {240, 3},  {240, 2}};

const std::unordered_set<int> DisplayStateResidencyProvider::kFpsLowPowerModeMappingTable = {1, 30};

const std::unordered_set<int> DisplayStateResidencyProvider::kActivePowerModes =
        {HWC2_POWER_MODE_DOZE, HWC2_POWER_MODE_ON};

namespace {

static constexpr uint64_t MilliToNano = 1000000;

}

DisplayStateResidencyProvider::DisplayStateResidencyProvider(
        std::shared_ptr<CommonDisplayContextProvider> displayContextProvider,
        std::shared_ptr<StatisticsProvider> statisticsProvider)
      : mDisplayContextProvider(displayContextProvider), mStatisticsProvider(statisticsProvider) {
    if (parseDisplayStateResidencyPattern()) {
        generatePowerStatsStates();
    }
    mStartStatisticTimeNs = mStatisticsProvider->getStartStatisticTimeNs();
}

void DisplayStateResidencyProvider::getStateResidency(std::vector<StateResidency>* stats) {
    mapStatistics();

    int64_t powerStatsTotalTimeNs = aggregateStatistics();
#ifdef DEBUG_VRR_POWERSTATS
    uint64_t statisticDurationNs = getBootClockTimeNs() - mStartStatisticTimeNs;
    ALOGD("DisplayStateResidencyProvider: total power stats time = %ld ms, time lapse = %ld ms",
          powerStatsTotalTimeNs / MilliToNano, statisticDurationNs / MilliToNano);
    if (mLastGetStateResidencyTimeNs != -1) {
        int64_t timePassedNs = (getSteadyClockTimeNs() - mLastGetStateResidencyTimeNs);
        int64_t statisticAccumulatedTimeNs = (powerStatsTotalTimeNs - mLastPowerStatsTotalTimeNs);
        ALOGD("DisplayStateResidencyProvider: The time interval between successive calls to "
              "getStateResidency() = %ld ms",
              (timePassedNs / MilliToNano));
        ALOGD("DisplayStateResidencyProvider: The accumulated statistic time interval between "
              "successive calls to "
              "getStateResidency() = %ld ms",
              (statisticAccumulatedTimeNs / MilliToNano));
    }
    mLastGetStateResidencyTimeNs = getSteadyClockTimeNs();
    mLastPowerStatsTotalTimeNs = powerStatsTotalTimeNs;
#endif
    *stats = mStateResidency;
}

const std::vector<State>& DisplayStateResidencyProvider::getStates() {
    return mStates;
}

void DisplayStateResidencyProvider::mapStatistics() {
    auto mUpdatedStatistics = mStatisticsProvider->getUpdatedStatistics();
#ifdef DEBUG_VRR_POWERSTATS
    for (const auto& item : mUpdatedStatistics) {
        ALOGI("DisplayStateResidencyProvider : update key %s value %s",
              item.first.toString().c_str(), item.second.toString().c_str());
    }
#endif
    mRemappedStatistics.clear();
    for (const auto& item : mUpdatedStatistics) {
        mStatistics[item.first] = item.second;
    }

    for (const auto& item : mStatistics) {
        const auto& displayPresentProfile = item.first;
        PowerStatsPresentProfile powerStatsPresentProfile;
        if (displayPresentProfile.mNumVsync <
            0) { // To address the specific scenario of powering off.
            powerStatsPresentProfile.mFps = -1;
            mRemappedStatistics[powerStatsPresentProfile] += item.second;
            mRemappedStatistics[powerStatsPresentProfile].mUpdated = true;
            continue;
        }
        const auto& configId = displayPresentProfile.mCurrentDisplayConfig.mActiveConfigId;
        powerStatsPresentProfile.mWidth = mDisplayContextProvider->getWidth(configId);
        powerStatsPresentProfile.mHeight = mDisplayContextProvider->getHeight(configId);
        powerStatsPresentProfile.mPowerMode =
                displayPresentProfile.mCurrentDisplayConfig.mPowerMode;
        powerStatsPresentProfile.mBrightnessMode =
                displayPresentProfile.mCurrentDisplayConfig.mBrightnessMode;
        auto teFrequency = mDisplayContextProvider->getTeFrequency(configId);
        Fraction fps(teFrequency, displayPresentProfile.mNumVsync);
        if ((kFpsMappingTable.count(fps) > 0)) {
            powerStatsPresentProfile.mFps = fps.round();
            mRemappedStatistics[powerStatsPresentProfile] += item.second;
            mRemappedStatistics[powerStatsPresentProfile].mUpdated = true;
        } else {
            // Others.
            auto key = powerStatsPresentProfile;
            const auto& value = item.second;
            key.mFps = 0;
            mRemappedStatistics[key].mUpdated = true;
            mRemappedStatistics[key].mCount += value.mCount;
            mRemappedStatistics[key].mAccumulatedTimeNs += value.mAccumulatedTimeNs;
            mRemappedStatistics[key].mLastTimeStampInBootClockNs =
                    std::max(mRemappedStatistics[key].mLastTimeStampInBootClockNs,
                             value.mLastTimeStampInBootClockNs);
        }
    }
}

uint64_t DisplayStateResidencyProvider::aggregateStatistics() {
    uint64_t totalTimeNs = 0;
    for (auto& statistic : mRemappedStatistics) {
        if (!statistic.second.mUpdated) {
            continue;
        }
        auto it = mPowerStatsPresentProfileToIdMap.find(statistic.first);
        if (it == mPowerStatsPresentProfileToIdMap.end()) {
            ALOGE("DisplayStateResidencyProvider %s(): unregistered powerstats state [%s]",
                  __func__, statistic.first.toString().c_str());
            continue;
        }
        int id = it->second;
        const auto& displayPresentRecord = statistic.second;

        auto& stateResidency = mStateResidency[id];
        stateResidency.totalStateEntryCount = displayPresentRecord.mCount;
        stateResidency.lastEntryTimestampMs =
                displayPresentRecord.mLastTimeStampInBootClockNs / MilliToNano;
        stateResidency.totalTimeInStateMs = displayPresentRecord.mAccumulatedTimeNs / MilliToNano;
        statistic.second.mUpdated = false;
        totalTimeNs += displayPresentRecord.mAccumulatedTimeNs;
    }
    return totalTimeNs;
}

void DisplayStateResidencyProvider::generatePowerStatsStates() {
    auto configs = mDisplayContextProvider->getDisplayConfigs();
    if (!configs) return;
    std::set<PowerStatsPresentProfile> powerStatsPresentProfileCandidates;
    PowerStatsPresentProfile powerStatsPresentProfile;

    // Generate a list of potential DisplayConfigProfiles.
    // Include the special case 'OFF'.
    powerStatsPresentProfile.mPowerMode = HWC2_POWER_MODE_OFF;
    powerStatsPresentProfileCandidates.insert(powerStatsPresentProfile);
    for (auto powerMode : kActivePowerModes) {
        powerStatsPresentProfile.mPowerMode = powerMode;
        for (int brightnesrMode = static_cast<int>(BrightnessMode::kNormalBrightnessMode);
             brightnesrMode < BrightnessMode::kInvalidBrightnessMode; ++brightnesrMode) {
            powerStatsPresentProfile.mBrightnessMode = static_cast<BrightnessMode>(brightnesrMode);
            for (const auto& config : *configs) {
                powerStatsPresentProfile.mWidth = mDisplayContextProvider->getWidth(config.first);
                powerStatsPresentProfile.mHeight = mDisplayContextProvider->getHeight(config.first);
                // Handle the special case LPM(Low Power Mode).
                if (powerMode == HWC_POWER_MODE_DOZE) {
                    for (auto fps : kFpsLowPowerModeMappingTable) {
                        powerStatsPresentProfile.mFps = fps;
                        powerStatsPresentProfileCandidates.insert(powerStatsPresentProfile);
                    }
                    continue;
                }
                // Include the special case: other fps.
                powerStatsPresentProfile.mFps = 0;
                powerStatsPresentProfileCandidates.insert(powerStatsPresentProfile);
                for (auto fps : kFpsMappingTable) {
                    powerStatsPresentProfile.mFps = fps.round();
                    powerStatsPresentProfileCandidates.insert(powerStatsPresentProfile);
                }
            }
        }
    }

    auto uniqueComp = [](const std::pair<std::string, PowerStatsPresentProfile>& v1,
                         const std::pair<std::string, PowerStatsPresentProfile>& v2) {
        return v1.first < v2.first;
    };

    // Transform candidate DisplayConfigProfiles into a string and eliminate duplicates.
    std::set<std::pair<std::string, PowerStatsPresentProfile>, decltype(uniqueComp)> uniqueStates;
    for (const auto& powerStatsPresentProfile : powerStatsPresentProfileCandidates) {
        std::string stateName;
        mPowerStatsPresentProfileTokenGenerator.setPowerStatsPresentProfile(
                &powerStatsPresentProfile);
        for (const auto& pattern : mDisplayStateResidencyPattern) {
            const auto token = mPowerStatsPresentProfileTokenGenerator.generateToken(pattern.first);
            if (token.has_value()) {
                stateName += token.value();
                // Handle special case when mode is 'OFF'.
                if (pattern.first == "mode" && token.value() == "OFF") {
                    break;
                }
            } else {
                ALOGE("DisplayStateResidencyProvider %s(): cannot find token with label %s",
                      __func__, pattern.first.c_str());
                continue;
            }
            stateName += pattern.second;
        }
        uniqueStates.insert(std::make_pair(stateName, powerStatsPresentProfile));
    }

    auto sortComp = [](const std::pair<std::string, PowerStatsPresentProfile>& v1,
                       const std::pair<std::string, PowerStatsPresentProfile>& v2) {
        return v1.second < v2.second;
    };
    std::set<std::pair<std::string, PowerStatsPresentProfile>, decltype(sortComp)> sortedStates;
    // Sort power stats according to a predefined order.
    std::for_each(uniqueStates.begin(), uniqueStates.end(),
                  [&](const std::pair<std::string, PowerStatsPresentProfile>& item) {
                      sortedStates.insert(item);
                  });

    // Sort and assign a unique identifier to each state string.
    mStateResidency.resize(sortedStates.size());
    int id = 0;
    int index = 0;
    for (const auto& state : sortedStates) {
        mStates.push_back({id, state.first});
        mPowerStatsPresentProfileToIdMap[state.second] = id;
        mStateResidency[index++].id = id;
        ++id;
    }

#ifdef DEBUG_VRR_POWERSTATS
    for (const auto& state : mStates) {
        ALOGI("DisplayStateResidencyProvider state id = %d, content = %s, len = %ld", state.id,
              state.name.c_str(), state.name.length());
    }
#endif
}

bool DisplayStateResidencyProvider::parseDisplayStateResidencyPattern() {
    size_t start, end;
    start = 0;
    end = -1;
    while (true) {
        start = kDisplayStateResidencyPattern.find_first_of(kTokenLabelStart, end + 1);
        if (start == std::string::npos) {
            break;
        }
        ++start;
        end = kDisplayStateResidencyPattern.find_first_of(kTokenLabelEnd, start);
        if (end == std::string::npos) {
            break;
        }
        std::string tokenLabel(kDisplayStateResidencyPattern.substr(start, end - start));

        start = kDisplayStateResidencyPattern.find_first_of(kDelimiterStart, end + 1);
        if (start == std::string::npos) {
            break;
        }
        ++start;
        end = kDisplayStateResidencyPattern.find_first_of(kDelimiterEnd, start);
        if (end == std::string::npos) {
            break;
        }
        std::string delimiter(kDisplayStateResidencyPattern.substr(start, end - start));
        mDisplayStateResidencyPattern.emplace_back(std::make_pair(tokenLabel, delimiter));
    }
    return (end == kDisplayStateResidencyPattern.length() - 1);
}

} // namespace android::hardware::graphics::composer
