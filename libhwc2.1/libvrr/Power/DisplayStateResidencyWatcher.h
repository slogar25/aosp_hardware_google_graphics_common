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

#include <unordered_map>

#include <android-base/logging.h>
#include <android-base/thread_annotations.h>
#include <android/binder_manager.h>

#include <aidl/android/hardware/power/stats/StateResidency.h>
#include <aidl/android/vendor/powerstats/BnPixelStateResidencyCallback.h>
#include <aidl/android/vendor/powerstats/IPixelStateResidencyCallback.h>
#include <aidl/android/vendor/powerstats/IPixelStateResidencyProvider.h>

#include <thread>

#include "../Statistics/VariableRefreshRateStatistic.h"
#include "DisplayStateResidencyProvider.h"

using aidl::android::hardware::power::stats::StateResidency;
using aidl::android::vendor::powerstats::BnPixelStateResidencyCallback;
using aidl::android::vendor::powerstats::IPixelStateResidencyCallback;
using aidl::android::vendor::powerstats::IPixelStateResidencyProvider;

namespace android::hardware::graphics::composer {

class DisplayStateResidencyWatcher : public BnPixelStateResidencyCallback {
public:
    DisplayStateResidencyWatcher(std::shared_ptr<CommonDisplayContextProvider> displayerInstance,
                                 std::shared_ptr<StatisticsProvider> statisticsProvider);
    virtual ~DisplayStateResidencyWatcher();

    virtual ndk::ScopedAStatus getStateResidency(std::vector<StateResidency>* stats) override;

    void registerWithPowerStats();
    static void binderDied(void* cookie);

private:
    std::atomic_bool mRunning = true;
    std::shared_ptr<IPixelStateResidencyProvider> mProvider;
    ::ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
    std::thread mTaskHandler;

    std::shared_ptr<StatisticsProvider> mStatisticsProvider;
    std::unique_ptr<DisplayStateResidencyProvider> mDisplayPresentStatisticsProvider;

    std::string mEntityName;
};

} // namespace android::hardware::graphics::composer
