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

#include "DisplayStateResidencyWatcher.h"

#include <android-base/properties.h>
#include <android/binder_ibinder.h>

#include "ExynosHWCHelper.h"
#include "Utils.h"

using aidl::android::vendor::powerstats::IPixelStateResidencyCallback;
using aidl::android::vendor::powerstats::IPixelStateResidencyProvider;

namespace android::hardware::graphics::composer {

constexpr std::string_view kPowerStatsPropertyPrefix = "ro.vendor";
constexpr std::string_view kPowerStatsPropertySuffix = "powerstats.entity_name";

DisplayStateResidencyWatcher::DisplayStateResidencyWatcher(
        std::shared_ptr<CommonDisplayContextProvider> displayerInstance,
        std::shared_ptr<StatisticsProvider> statisticsProvider)
      : mDeathRecipient(AIBinder_DeathRecipient_new(binderDied)),
        mStatisticsProvider(statisticsProvider) {
    mDisplayPresentStatisticsProvider =
            std::make_unique<DisplayStateResidencyProvider>(displayerInstance, statisticsProvider);

    registerWithPowerStats();

    std::string displayEntityNameId = std::string(kPowerStatsPropertyPrefix) + "." +
            "primarydisplay" + "." + std::string(kPowerStatsPropertySuffix);
    // Retrieve the entity name from the property.
    mEntityName = android::base::GetProperty(displayEntityNameId, "Display");
}

DisplayStateResidencyWatcher::~DisplayStateResidencyWatcher() {
    mRunning.store(false);
    if (mTaskHandler.joinable()) {
        mTaskHandler.join();
    }
    if (mProvider) {
        AIBinder_unlinkToDeath(mProvider->asBinder().get(), mDeathRecipient.get(),
                               reinterpret_cast<void*>(this));
        mProvider->unregisterCallback(this->ref<IPixelStateResidencyCallback>());
    }
}

::ndk::ScopedAStatus DisplayStateResidencyWatcher::getStateResidency(
        std::vector<StateResidency>* stats) {
    mDisplayPresentStatisticsProvider->getStateResidency(stats);

    return ::ndk::ScopedAStatus::ok();
}

constexpr std::chrono::seconds kOneSecondPeriod{1};

void DisplayStateResidencyWatcher::registerWithPowerStats() {
    if (mProvider) {
        LOG(INFO) << "Need to reconnect PowerStats service";
        mProvider.reset();
    }
    LOG(INFO) << "Registering PowerStats callback";
    if (mTaskHandler.joinable()) {
        mTaskHandler.join();
    }
    mTaskHandler = std::thread([this]() {
        static const std::string kInstance = "power.stats-vendor";
        static const int64_t kMaxWaitServiceTimeMs =
                100 * std::milli::den; // Default timeout is 100 seconds.

        int64_t startMs = getSteadyClockTimeMs();
        while (mRunning.load()) {
            ndk::SpAIBinder binder(AServiceManager_waitForService(kInstance.c_str()));
            mProvider = IPixelStateResidencyProvider::fromBinder(binder);
            if (mProvider) {
                break;
            } else {
                int64_t nowMs = getSteadyClockTimeMs();
                if ((nowMs - startMs) > kMaxWaitServiceTimeMs) {
                    LOG(ERROR) << "Cannot get PowerStats service";
                    break;
                }
                std::this_thread::sleep_for(kOneSecondPeriod);
            }
        }

        if (!mRunning.load() || !mProvider) {
            return;
        }

        if (auto status =
                    mProvider->registerCallbackByStates(mEntityName,
                                                        this->ref<IPixelStateResidencyCallback>(),
                                                        mDisplayPresentStatisticsProvider
                                                                ->getStates());
            status.isOk()) {
            LOG(INFO) << "PowerStats callback is successfully registered.";
        } else {
            LOG(ERROR) << "failed to register callback: " << status.getDescription();
        }
        AIBinder_linkToDeath(mProvider->asBinder().get(), mDeathRecipient.get(),
                             reinterpret_cast<void*>(this));
    });
}

void DisplayStateResidencyWatcher::binderDied(void* cookie) {
    LOG(ERROR) << "power.stats died";

    auto residencyWatcher = reinterpret_cast<DisplayStateResidencyWatcher*>(cookie);

    residencyWatcher->registerWithPowerStats();
}

} // namespace android::hardware::graphics::composer
