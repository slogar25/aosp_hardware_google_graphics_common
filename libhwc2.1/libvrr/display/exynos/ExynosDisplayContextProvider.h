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

#include <map>

#include "../common/CommonDisplayContextProvider.h"
#include "libdevice/ExynosDisplay.h"

namespace android::hardware::graphics::composer {

class ExynosDisplayContextProvider : public CommonDisplayContextProvider {
public:
    ExynosDisplayContextProvider(void* display,
                                 DisplayConfigurationsOwner* displayConfigurationOwner,
                                 std::shared_ptr<RefreshRateCalculator> videoFrameRateCalculator)
          : CommonDisplayContextProvider(displayConfigurationOwner,
                                         std::move(videoFrameRateCalculator)),
            mDisplay(static_cast<ExynosDisplay*>(display)),
            mDisplayFileNodePath(mDisplay->getPanelSysfsPath()){};

    // Implement DisplayContextProvider
    BrightnessMode getBrightnessMode() const final;

    int getBrightnessNits() const final;

    const char* getDisplayFileNodePath() const final;

    int getAmbientLightSensorOutput() const final;

    bool isProximityThrottlingEnabled() const final;
    // End of DisplayContextProvider implementation.

    const std::map<uint32_t, displayConfigs_t>* getDisplayConfigs() const final;

    const displayConfigs_t* getDisplayConfig(hwc2_config_t id) const final;

    bool isHsMode(hwc2_config_t id) const final;

    int getTeFrequency(hwc2_config_t id) const final;

    int getMaxFrameRate(hwc2_config_t id) const final;

    int getWidth(hwc2_config_t id) const final;

    int getHeight(hwc2_config_t id) const final;

private:
    ExynosDisplay* mDisplay;

    std::string mDisplayFileNodePath;
};

} // namespace android::hardware::graphics::composer
