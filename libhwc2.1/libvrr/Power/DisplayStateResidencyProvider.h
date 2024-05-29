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

#include <unordered_set>

#include <aidl/android/hardware/power/stats/State.h>
#include <aidl/android/hardware/power/stats/StateResidency.h>

#include "../Statistics/VariableRefreshRateStatistic.h"
#include "PowerStatsPresentProfileTokenGenerator.h"

// #define DEBUG_VRR_POWERSTATS 1

namespace android::hardware::graphics::composer {

using aidl::android::hardware::power::stats::State;
using aidl::android::hardware::power::stats::StateResidency;

typedef std::vector<StateResidency> StateResidencies;

class DisplayStateResidencyProvider {
public:
    DisplayStateResidencyProvider(
            std::shared_ptr<CommonDisplayContextProvider> displayContextProvider,
            std::shared_ptr<StatisticsProvider> statisticsProvider);

    void getStateResidency(std::vector<StateResidency>* stats);

    const std::vector<State>& getStates();

    DisplayStateResidencyProvider(const DisplayStateResidencyProvider& other) = delete;
    DisplayStateResidencyProvider& operator=(const DisplayStateResidencyProvider& other) = delete;

private:
    static const std::set<Fraction<int>> kFpsMappingTable;
    static const std::unordered_set<int> kFpsLowPowerModeMappingTable;
    static const std::unordered_set<int> kActivePowerModes;

    // The format of pattern is: ([token label]'delimiter'?)*
    static constexpr std::string_view kDisplayStateResidencyPattern =
            "[mode](:)[width](x)[height](@)[fps]()";

    static constexpr char kTokenLabelStart = '[';
    static constexpr char kTokenLabelEnd = ']';
    static constexpr char kDelimiterStart = '(';
    static constexpr char kDelimiterEnd = ')';

    void mapStatistics();
    uint64_t aggregateStatistics();

    void generatePowerStatsStates();

    bool parseDisplayStateResidencyPattern();

    std::shared_ptr<CommonDisplayContextProvider> mDisplayContextProvider;

    std::shared_ptr<StatisticsProvider> mStatisticsProvider;

    DisplayPresentStatistics mStatistics;

    typedef std::map<PowerStatsPresentProfile, DisplayPresentRecord> PowerStatsPresentStatistics;

    PowerStatsPresentStatistics mRemappedStatistics;

    PowerStatsPresentProfileTokenGenerator mPowerStatsPresentProfileTokenGenerator;
    std::vector<std::pair<std::string, std::string>> mDisplayStateResidencyPattern;

    std::vector<State> mStates;
    std::map<PowerStatsPresentProfile, int> mPowerStatsPresentProfileToIdMap;

#ifdef DEBUG_VRR_POWERSTATS
    int64_t mLastGetStateResidencyTimeNs = -1;
    int64_t mLastPowerStatsTotalTimeNs = -1;
#endif

    uint64_t mStartStatisticTimeNs;

    std::vector<StateResidency> mStateResidency;
};

} // namespace android::hardware::graphics::composer
