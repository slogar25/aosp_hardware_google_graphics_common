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
#include <string>

#include "../Utils.h"
#include "DisplayContextProvider.h"

namespace android::hardware::graphics::composer {

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

class ExternalEventHandler {
public:
    virtual ~ExternalEventHandler() = default;

    virtual std::vector<TimedEvent> getHandleEvents() = 0;
};

} // namespace android::hardware::graphics::composer

typedef android::hardware::graphics::composer::ExternalEventHandler* (
        *createExternalEventHandler_t)(void* interface, void* host, const char* panelName);

typedef void (*destroyExternalEventHandler_t)(
        android::hardware::graphics::composer::ExternalEventHandler* handler);
