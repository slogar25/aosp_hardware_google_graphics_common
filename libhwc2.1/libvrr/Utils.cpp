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

#include "Utils.h"

#include <hardware/hwcomposer2.h>
#include <chrono>
#include "android-base/chrono_utils.h"

#include "interface/Panel_def.h"

namespace android::hardware::graphics::composer {

int64_t getSteadyClockTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

int64_t getSteadyClockTimeNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

int64_t getBootClockTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   ::android::base::boot_clock::now().time_since_epoch())
            .count();
}

int64_t getBootClockTimeNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   ::android::base::boot_clock::now().time_since_epoch())
            .count();
}

int64_t steadyClockTimeToBootClockTimeNs(int64_t steadyClockTimeNs) {
    return steadyClockTimeNs + (getBootClockTimeNs() - getSteadyClockTimeNs());
}

bool hasPresentFrameFlag(int flag, PresentFrameFlag target) {
    return flag & static_cast<int>(target);
}

bool isPowerModeOff(int powerMode) {
    return ((powerMode == HWC_POWER_MODE_OFF) || (powerMode == HWC_POWER_MODE_DOZE_SUSPEND));
}

void setTimedEventWithAbsoluteTime(TimedEvent& event) {
    if (event.mIsRelativeTime) {
        event.mWhenNs += getSteadyClockTimeNs();
        event.mIsRelativeTime = false;
    }
}

} // namespace android::hardware::graphics::composer
