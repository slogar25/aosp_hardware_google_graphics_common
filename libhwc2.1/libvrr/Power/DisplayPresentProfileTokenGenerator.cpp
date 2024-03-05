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

#include "DisplayPresentProfileTokenGenerator.h"

#include <string>
#include <unordered_map>

namespace android::hardware::graphics::composer {

std::string DisplayPresentProfileTokenGenerator::generateModeToken() {
    auto powerMode = mDisplayPresentProfile->mCurrentDisplayConfig.mPowerMode;
    if ((powerMode == HWC_POWER_MODE_OFF) || (powerMode == HWC_POWER_MODE_DOZE_SUSPEND)) {
        return "OFF";
    } else {
        if (powerMode == HWC_POWER_MODE_DOZE) {
            return "LPM";
        }
        return (mDisplayPresentProfile->mCurrentDisplayConfig.mBrightnessMode ==
                BrightnessMode::kHighBrightnessMode)
                ? "HBM"
                : "NBM";
    }
}

std::string DisplayPresentProfileTokenGenerator::generateWidthToken() {
    auto powerMode = mDisplayPresentProfile->mCurrentDisplayConfig.mPowerMode;
    if ((powerMode == HWC_POWER_MODE_OFF) || (powerMode == HWC_POWER_MODE_DOZE_SUSPEND)) {
        return "";
    }
    return std::to_string(mDisplayContextProvider->getWidth(mDisplayId));
}

std::string DisplayPresentProfileTokenGenerator::generateHeightToken() {
    auto powerMode = mDisplayPresentProfile->mCurrentDisplayConfig.mPowerMode;
    if ((powerMode == HWC_POWER_MODE_OFF) || (powerMode == HWC_POWER_MODE_DOZE_SUSPEND)) {
        return "";
    }
    return std::to_string(mDisplayContextProvider->getHeight(mDisplayId));
}

std::string DisplayPresentProfileTokenGenerator::generateFpsToken() {
    auto powerMode = mDisplayPresentProfile->mCurrentDisplayConfig.mPowerMode;
    if ((powerMode == HWC_POWER_MODE_OFF) || (powerMode == HWC_POWER_MODE_DOZE_SUSPEND)) {
        return "";
    }
    if (mDisplayPresentProfile->mNumVsync == 0) {
        return "oth";
    }
    return std::to_string(mDisplayContextProvider->getTeFrequency(mDisplayId) /
                          mDisplayPresentProfile->mNumVsync);
}

std::optional<std::string> DisplayPresentProfileTokenGenerator::generateToken(
        const std::string& tokenLabel) {
    static std::unordered_map<std::string, std::function<std::string()>> functors =
            {{"mode", std::bind(&DisplayPresentProfileTokenGenerator::generateModeToken, this)},
             {"width", std::bind(&DisplayPresentProfileTokenGenerator::generateWidthToken, this)},
             {"height", std::bind(&DisplayPresentProfileTokenGenerator::generateHeightToken, this)},
             {"fps", std::bind(&DisplayPresentProfileTokenGenerator::generateFpsToken, this)}};

    if (!mDisplayPresentProfile) {
        ALOGE("%s: haven't set target DisplayPresentProfile", __func__);
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
