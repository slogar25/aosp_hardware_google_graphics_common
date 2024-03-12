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

#include <displaycolor/displaycolor.h>

#include "ExynosDisplayContextProvider.h"
#include "libdevice/BrightnessController.h"

namespace android::hardware::graphics::composer {

// Implement DisplayContextProvider
BrightnessMode ExynosDisplayContextProvider::getBrightnessMode() const {
    if (!mDisplay || !(mDisplay->mBrightnessController)) {
        return BrightnessMode::kInvalidBrightnessMode;
    }
    auto res = mDisplay->mBrightnessController->getBrightnessNitsAndMode();
    if (res == std::nullopt) {
        return BrightnessMode::kInvalidBrightnessMode;
    }
    auto brightnessMode = std::get<displaycolor::BrightnessMode>(res.value());
    switch (brightnessMode) {
        case displaycolor::BrightnessMode::BM_NOMINAL: {
            return BrightnessMode::kNormalBrightnessMode;
        }
        case displaycolor::BrightnessMode::BM_HBM: {
            return BrightnessMode::kHighBrightnessMode;
        }
        default: {
            return BrightnessMode::kInvalidBrightnessMode;
        }
    }
}

int ExynosDisplayContextProvider::getBrightnessNits() const {
    if (!mDisplay || !(mDisplay->mBrightnessController)) {
        return -1;
    }
    auto res = mDisplay->mBrightnessController->getBrightnessNitsAndMode();
    if (res == std::nullopt) {
        return -1;
    }
    return std::round(std::get<float>(res.value()));
}

const char* ExynosDisplayContextProvider::getDisplayFileNodePath() const {
    return mDisplayFileNodePath.c_str();
}

int ExynosDisplayContextProvider::getAmbientLightSensorOutput() const {
    return -1;
}

bool ExynosDisplayContextProvider::isProximityThrottlingEnabled() const {
    return false;
}
// End of DisplayContextProvider implementation.

const std::map<uint32_t, displayConfigs_t>* ExynosDisplayContextProvider::getDisplayConfigs()
        const {
    if (!mDisplay) return nullptr;
    return &(mDisplay->mDisplayConfigs);
}

const displayConfigs_t* ExynosDisplayContextProvider::getDisplayConfig(hwc2_config_t id) const {
    const auto configs = getDisplayConfigs();
    if ((!configs) || configs->count(id) == 0) {
        return nullptr;
    }
    const auto& config = configs->find(id);
    return &(config->second);
}

bool ExynosDisplayContextProvider::isHsMode(hwc2_config_t id) const {
    auto config = getDisplayConfig(id);
    return config ? (!config->isNsMode) : false;
}

int ExynosDisplayContextProvider::getTeFrequency(hwc2_config_t id) const {
    auto config = getDisplayConfig(id);
    return config ? durationNsToFreq(config->vsyncPeriod) : -1;
}

int ExynosDisplayContextProvider::getMaxFrameRate(hwc2_config_t id) const {
    auto config = getDisplayConfig(id);
    return config ? durationNsToFreq(config->refreshRate) : -1;
}

int ExynosDisplayContextProvider::getWidth(hwc2_config_t id) const {
    auto config = getDisplayConfig(id);
    return config ? config->width : -1;
}

int ExynosDisplayContextProvider::getHeight(hwc2_config_t id) const {
    auto config = getDisplayConfig(id);
    return config ? config->height : -1;
}

} // namespace android::hardware::graphics::composer
