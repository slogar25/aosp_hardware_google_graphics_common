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

#include <functional>
#include <sstream>
#include <string>

#include "../Utils.h"
#include "DisplayContextProvider.h"

namespace android::hardware::graphics::composer {

enum class VrrControllerEventType {
    kGeneralEventMask = 0x100,
    // kSystemRenderingTimeout is responsible for managing present timeout according to the
    // configuration specified in the system HAL API.
    kSystemRenderingTimeout,
    // kVendorRenderingTimeout is responsible for managing present timeout based on the vendor's
    // proprietary definition.
    kVendorRenderingTimeout,
    // kHandleVendorRenderingTimeout is responsible for addressing present timeout by invoking
    // the handling function provided by the vendor.
    kHandleVendorRenderingTimeout,
    kHibernateTimeout,
    kNotifyExpectedPresentConfig,
    kGeneralEventMax = 0x1FF,
    // General callback events.
    kCallbackEventMask = 0x200,
    kRefreshRateCalculatorUpdateMask = 0x200,
    kInstantRefreshRateCalculatorUpdate,
    kPeriodRefreshRateCalculatorUpdate,
    kVideoFrameRateCalculatorUpdate,
    kCombinedRefreshRateCalculatorUpdate,
    kAodRefreshRateCalculatorUpdate,
    kExitIdleRefreshRateCalculatorUpdate,
    kStaticticUpdate,
    kMinLockTimeForPeakRefreshRate,
    kCallbackEventMax = 0x2FF,
    // Sensors, outer events...
};

struct TimedEvent {
    explicit TimedEvent(const std::string& name) : mEventName(std::move(name)) {}

    TimedEvent(const std::string& name, int64_t whenNs) : mEventName(name), mWhenNs(whenNs) {}

    TimedEvent(std::string& name, int64_t whenNs) : mEventName(name), mWhenNs(whenNs) {}

    bool operator<(const TimedEvent& b) const { return mWhenNs > b.mWhenNs; }

    std::string mEventName;
    std::function<int()> mFunctor;
    bool mIsRelativeTime = true;
    int64_t mWhenNs = 0;
};

struct VrrControllerEvent {
    bool operator<(const VrrControllerEvent& b) const { return mWhenNs > b.mWhenNs; }
    std::string getName() const {
        switch (mEventType) {
            case VrrControllerEventType::kSystemRenderingTimeout:
                return "kSystemRenderingTimeout";
            case VrrControllerEventType::kVendorRenderingTimeout:
                return "kVendorRenderingTimeout";
            case VrrControllerEventType::kHandleVendorRenderingTimeout:
                return "kHandleVendorRenderingTimeout";
            case VrrControllerEventType::kHibernateTimeout:
                return "kHibernateTimeout";
            case VrrControllerEventType::kNotifyExpectedPresentConfig:
                return "kNotifyExpectedPresentConfig";
            case VrrControllerEventType::kInstantRefreshRateCalculatorUpdate:
                return "kInstantRefreshRateCalculatorUpdate";
            case VrrControllerEventType::kPeriodRefreshRateCalculatorUpdate:
                return "kPeriodRefreshRateCalculatorUpdate";
            case VrrControllerEventType::kVideoFrameRateCalculatorUpdate:
                return "kVideoFrameRateCalculatorUpdate";
            case VrrControllerEventType::kCombinedRefreshRateCalculatorUpdate:
                return "kCombinedRefreshRateCalculatorUpdate";
            case VrrControllerEventType::kAodRefreshRateCalculatorUpdate:
                return "kAodRefreshRateCalculatorUpdate";
            case VrrControllerEventType::kStaticticUpdate:
                return "kStaticticUpdate";
            case VrrControllerEventType::kMinLockTimeForPeakRefreshRate:
                return "kMinLockTimeForPeakRefreshRate";
            default:
                return "Unknown";
        }
    }

    std::string toString() const {
        std::ostringstream os;
        os << "Vrr event: [";
        os << "type = " << getName() << ", ";
        os << "when = " << mWhenNs << "ns]";
        return os.str();
    }
    int64_t mDisplay;
    VrrControllerEventType mEventType;
    int64_t mWhenNs;
    std::function<int()> mFunctor;
    int64_t mPeriodNs = -1;
};

class ExternalEventHandler {
public:
    virtual ~ExternalEventHandler() = default;

    virtual std::vector<TimedEvent> getHandleEvents() = 0;

    virtual std::function<int()> getHandleFunction() = 0;

    virtual int64_t getPresentTimeoutNs() = 0;
};

} // namespace android::hardware::graphics::composer

typedef android::hardware::graphics::composer::ExternalEventHandler* (
        *createExternalEventHandler_t)(void* interface, void* host, const char* panelName);

typedef void (*destroyExternalEventHandler_t)(
        android::hardware::graphics::composer::ExternalEventHandler* handler);
