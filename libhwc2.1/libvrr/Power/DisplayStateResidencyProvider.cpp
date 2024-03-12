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

#include "DisplayStateResidencyProvider.h"

namespace android::hardware::graphics::composer {

const std::unordered_set<int> DisplayStateResidencyProvider::kFpsMappingTable = {1,  2,  10, 24, 30,
                                                                                 48, 60, 80, 120};

const std::unordered_set<int> DisplayStateResidencyProvider::kFpsLowPowerModeMappingTable = {1, 30};

const std::unordered_set<int> DisplayStateResidencyProvider::kActivePowerModes =
        {HWC2_POWER_MODE_DOZE, HWC2_POWER_MODE_ON};

namespace {

static constexpr uint64_t MilliToNano = 1000000;

}

DisplayStateResidencyProvider::DisplayStateResidencyProvider(
        std::shared_ptr<CommonDisplayContextProvider> displayContextProvider,
        std::shared_ptr<StatisticsProvider> statisticsProvider)
      : mDisplayContextProvider(displayContextProvider),
        mStatisticsProvider(statisticsProvider),
        mDisplayPresentProfileTokenGenerator(mDisplayContextProvider.get()) {
    if (parseDisplayStateResidencyPattern()) {
        generatePowerStatsStates();
    }
}

void DisplayStateResidencyProvider::getStateResidency(std::vector<StateResidency>* stats) {
    mapStatistics();

    aggregateStatistics();

    *stats = mStateResidency;
}

const std::vector<State>& DisplayStateResidencyProvider::getStates() const {
    return mStates;
}

void DisplayStateResidencyProvider::mapStatistics() {
    auto mUpdatedStatistics = mStatisticsProvider->getUpdatedStatistics();

    for (const auto& item : mUpdatedStatistics) {
        const auto& displayPresentProfile = item.first;
        int configId = displayPresentProfile.mCurrentDisplayConfig.mActiveConfigId;
        auto teFrequency = mDisplayContextProvider->getTeFrequency(configId);
        if (teFrequency % displayPresentProfile.mNumVsync) {
            ALOGE("There should NOT be a frame rate including decimals, TE = %d, number of vsync = "
                  "%d",
                  teFrequency, displayPresentProfile.mNumVsync);
            continue;
        }
        auto fps = teFrequency / displayPresentProfile.mNumVsync;
        if (kFpsMappingTable.count(fps) > 0) {
            mRemappedStatistics[displayPresentProfile] = item.second;
            mRemappedStatistics[displayPresentProfile].mUpdated = true;
        } else {
            // Others.
            auto key = displayPresentProfile;
            key.mNumVsync = 0;
            mRemappedStatistics[key] += item.second;
            mRemappedStatistics[key].mUpdated = true;
        }
    }
}

void DisplayStateResidencyProvider::aggregateStatistics() {
    for (auto& statistic : mRemappedStatistics) {
        if (!statistic.second.mUpdated) {
            continue;
        }
        int id = mDisplayPresentProfileToIdMap[statistic.first];
        const auto& displayPresentProfile = statistic.first;
        const auto& displayPresentRecord = statistic.second;
        int configId = displayPresentProfile.mCurrentDisplayConfig.mActiveConfigId;
        auto teFrequency = mDisplayContextProvider->getTeFrequency(configId);
        // TODO(b/328962277): given the updated entries, calculate "others" statistics and metrics.
        // Here handle "non-others" only.
        if (displayPresentProfile.mNumVsync > 0) {
            auto fps = teFrequency / displayPresentProfile.mNumVsync;
            auto durationNs = freqToDurationNs(fps);
            auto& stateResidency = mStateResidency[id];
            stateResidency.totalStateEntryCount = displayPresentRecord.mCount;
            stateResidency.lastEntryTimestampMs =
                    displayPresentRecord.mLastTimeStampNs / MilliToNano;
            stateResidency.totalTimeInStateMs =
                    (stateResidency.totalStateEntryCount * durationNs) / MilliToNano;
        }
        statistic.second.mUpdated = false;
    }
    return;
}

void DisplayStateResidencyProvider::generatePowerStatsStates() {
    auto configs = mDisplayContextProvider->getDisplayConfigs();
    if (!configs) return;
    std::vector<DisplayPresentProfile> displayConfigProfileCandidates;
    DisplayPresentProfile displayConfigProfile;

    // Generate a list of potential DisplayConfigProfiles.
    // Include the special case 'OFF'.
    displayConfigProfile.mCurrentDisplayConfig.mPowerMode = HWC2_POWER_MODE_OFF;
    displayConfigProfileCandidates.emplace_back(displayConfigProfile);
    for (auto powerMode : kActivePowerModes) {
        displayConfigProfile.mCurrentDisplayConfig.mPowerMode = powerMode;
        for (int brightnesrMode = static_cast<int>(BrightnessMode::kNormalBrightnessMode);
             brightnesrMode < BrightnessMode::kInvalidBrightnessMode; ++brightnesrMode) {
            displayConfigProfile.mCurrentDisplayConfig.mBrightnessMode =
                    static_cast<BrightnessMode>(brightnesrMode);
            for (const auto& config : *configs) {
                displayConfigProfile.mCurrentDisplayConfig.mActiveConfigId = config.first;
                auto teFrequency = mDisplayContextProvider->getTeFrequency(config.first);
                // Handle the special case LPM(Low Power Mode).
                if (powerMode == HWC_POWER_MODE_DOZE) {
                    for (auto fps : kFpsLowPowerModeMappingTable) {
                        displayConfigProfile.mNumVsync = (teFrequency / fps);
                        displayConfigProfileCandidates.emplace_back(displayConfigProfile);
                    }
                    continue;
                }
                // Include the special case: other fps.
                displayConfigProfile.mNumVsync = 0;
                displayConfigProfileCandidates.emplace_back(displayConfigProfile);
                for (auto fps : kFpsMappingTable) {
                    if (teFrequency % fps) continue;
                    displayConfigProfile.mNumVsync = (teFrequency / fps);
                    displayConfigProfileCandidates.emplace_back(displayConfigProfile);
                }
            }
        }
    }

    auto comp = [](const std::pair<std::string, DisplayPresentProfile>& v1,
                   const std::pair<std::string, DisplayPresentProfile>& v2) {
        return v1.first < v2.first;
    };

    // Convert candidate DisplayConfigProfiles into a string.
    std::set<std::pair<std::string, DisplayPresentProfile>, decltype(comp)> States;
    for (const auto& displayConfigProfile : displayConfigProfileCandidates) {
        std::string stateName;
        mDisplayPresentProfileTokenGenerator.setDisplayPresentProfile(&displayConfigProfile);
        for (const auto& pattern : mDisplayStateResidencyPattern) {
            const auto& token = mDisplayPresentProfileTokenGenerator.generateToken(pattern.first);
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
        States.insert(std::make_pair(stateName, displayConfigProfile));
    }

    // Sort and assign a unique identifier to each state string..
    mStateResidency.resize(States.size());
    int id = 0;
    int index = 0;
    for (const auto& state : States) {
        mStates.push_back({id, state.first});
        mDisplayPresentProfileToIdMap[state.second] = id;
        mStateResidency[index++].id = id;
        ++id;
    }
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
