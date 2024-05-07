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

#include <optional>
#include <string>

#include "../Statistics/VariableRefreshRateStatistic.h"
#include "../display/common/CommonDisplayContextProvider.h"

namespace android::hardware::graphics::composer {

typedef struct PowerStatsPresentProfile {
    inline bool isOff() const {
        if ((mPowerMode == HWC_POWER_MODE_OFF) || (mPowerMode == HWC_POWER_MODE_DOZE_SUSPEND)) {
            return true;
        } else {
            return false;
        }
    }

    bool operator==(const PowerStatsPresentProfile& rhs) const {
        if (isOff() || rhs.isOff()) {
            return isOff() == rhs.isOff();
        }
        return (mWidth == rhs.mWidth) && (mHeight == rhs.mHeight) && (mFps == rhs.mFps) &&
                (mPowerMode == rhs.mPowerMode) && (mBrightnessMode == rhs.mBrightnessMode);
    }

    bool operator<(const PowerStatsPresentProfile& rhs) const {
        if (isOff() && rhs.isOff()) {
            return false;
        }

        if (mPowerMode != rhs.mPowerMode) {
            return (isOff() || (mPowerMode < rhs.mPowerMode));
        } else if (mBrightnessMode != rhs.mBrightnessMode) {
            return mBrightnessMode < rhs.mBrightnessMode;
        } else if (mWidth != rhs.mWidth) {
            return mWidth < rhs.mWidth;
        } else if (mHeight != rhs.mHeight) {
            return mHeight < rhs.mHeight;
        } else {
            return mFps < rhs.mFps;
        }
    }

    std::string toString() const {
        std::ostringstream os;
        os << "mWidth = " << mWidth;
        os << " mHeight = " << mHeight;
        os << " mFps = " << mFps;
        os << ", power mode = " << mPowerMode;
        os << ", brightness = " << static_cast<int>(mBrightnessMode);
        return os.str();
    }

    int mWidth = 0;
    int mHeight = 0;
    int mFps = -1;
    int mPowerMode = HWC_POWER_MODE_OFF;
    BrightnessMode mBrightnessMode = BrightnessMode::kInvalidBrightnessMode;

} PowerStatsPresentProfile;

class PowerStatsPresentProfileTokenGenerator {
public:
    PowerStatsPresentProfileTokenGenerator() = default;

    void setPowerStatsPresentProfile(const PowerStatsPresentProfile* powerStatsPresentProfile) {
        mPowerStatsProfile = powerStatsPresentProfile;
    }

    std::optional<std::string> generateToken(const std::string& tokenLabel);

private:
    std::string generateModeToken();

    std::string generateWidthToken();

    std::string generateHeightToken();

    std::string generateFpsToken();

    const PowerStatsPresentProfile* mPowerStatsProfile;
};

} // namespace android::hardware::graphics::composer
