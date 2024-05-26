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

#include "../../interface/DisplayContextProvider.h"
#include "../RefreshRateCalculator/VideoFrameRateCalculator.h"
#include "DisplayConfigurationOwner.h"

namespace android::hardware::graphics::composer {

class CommonDisplayContextProvider : public DisplayContextProvider {
public:
    CommonDisplayContextProvider(DisplayConfigurationsOwner* displayConfigurationOwner,
                                 std::shared_ptr<RefreshRateCalculator> videoFrameRateCalculator)
          : mDisplayConfigurationOwner(displayConfigurationOwner),
            mVideoFrameRateCalculator(std::move(videoFrameRateCalculator)){};

    // Implement DisplayContextProvider
    OperationSpeedMode getOperationSpeedMode() const override;

    virtual BrightnessMode getBrightnessMode() const = 0;

    virtual int getBrightnessNits() const = 0;

    virtual const char* getDisplayFileNodePath() const = 0;

    int getEstimatedVideoFrameRate() const override;

    virtual int getAmbientLightSensorOutput() const = 0;

    virtual bool isProximityThrottlingEnabled() const = 0;
    // End of DisplayContextProvider implementation.

    virtual const std::map<uint32_t, displayConfigs_t>* getDisplayConfigs() const = 0;

    virtual const displayConfigs_t* getDisplayConfig(hwc2_config_t id) const = 0;

    virtual int getMaxFrameRate(hwc2_config_t id) const = 0;

    virtual int getTeFrequency(hwc2_config_t id) const = 0;

    virtual int getWidth(hwc2_config_t id) const = 0;

    virtual int getHeight(hwc2_config_t id) const = 0;

    virtual bool isHsMode(hwc2_config_t id) const = 0;

private:
    DisplayConfigurationsOwner* mDisplayConfigurationOwner;

    std::shared_ptr<RefreshRateCalculator> mVideoFrameRateCalculator;
};

} // namespace android::hardware::graphics::composer
