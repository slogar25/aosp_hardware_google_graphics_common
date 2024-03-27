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

#include "PowerStatsPresentProfileTokenGenerator.h"

#include <string>
#include <unordered_map>

namespace android::hardware::graphics::composer {

std::string PowerStatsPresentProfileTokenGenerator::generateModeToken() {
    if (mPowerStatsProfile->isOff()) {
        return "OFF";
    } else {
        if (mPowerStatsProfile->mPowerMode == HWC_POWER_MODE_DOZE) {
            return "LPM";
        }
        return (mPowerStatsProfile->mBrightnessMode == BrightnessMode::kHighBrightnessMode) ? "HBM"
                                                                                            : "NBM";
    }
}

std::string PowerStatsPresentProfileTokenGenerator::generateWidthToken() {
    if (mPowerStatsProfile->isOff()) {
        return "";
    }
    return std::to_string(mPowerStatsProfile->mWidth);
}

std::string PowerStatsPresentProfileTokenGenerator::generateHeightToken() {
    if (mPowerStatsProfile->isOff()) {
        return "";
    }
    return std::to_string(mPowerStatsProfile->mHeight);
}

std::string PowerStatsPresentProfileTokenGenerator::generateFpsToken() {
    if (mPowerStatsProfile->isOff()) {
        return "";
    }
    if (mPowerStatsProfile->mFps == 0) {
        return "oth";
    }
    return std::to_string(mPowerStatsProfile->mFps);
}

std::optional<std::string> PowerStatsPresentProfileTokenGenerator::generateToken(
        const std::string& tokenLabel) {
    static std::unordered_map<std::string, std::function<std::string()>> functors =
            {{"mode", std::bind(&PowerStatsPresentProfileTokenGenerator::generateModeToken, this)},
             {"width",
              std::bind(&PowerStatsPresentProfileTokenGenerator::generateWidthToken, this)},
             {"height",
              std::bind(&PowerStatsPresentProfileTokenGenerator::generateHeightToken, this)},
             {"fps", std::bind(&PowerStatsPresentProfileTokenGenerator::generateFpsToken, this)}};

    if (!mPowerStatsProfile) {
        ALOGE("%s: haven't set target mPowerStatsProfile", __func__);
        return std::nullopt;
    }

    if (functors.find(tokenLabel) != functors.end()) {
        return (functors[tokenLabel])();
    } else {
        ALOGE("%s syntax error: unable to find token label = %s", __func__, tokenLabel.c_str());
        return std::nullopt;
    }
}

} // namespace android::hardware::graphics::composer
