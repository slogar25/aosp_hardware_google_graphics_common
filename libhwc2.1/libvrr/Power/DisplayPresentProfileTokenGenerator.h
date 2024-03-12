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

class DisplayPresentProfileTokenGenerator {
public:
    DisplayPresentProfileTokenGenerator(CommonDisplayContextProvider* displayContextProvider)
          : mDisplayContextProvider(displayContextProvider) {}

    void setDisplayPresentProfile(const DisplayPresentProfile* displayPresentProfile) {
        mDisplayPresentProfile = displayPresentProfile;
        mDisplayId = mDisplayPresentProfile->mCurrentDisplayConfig.mActiveConfigId;
    }

    std::optional<std::string> generateToken(const std::string& tokenLabel);

private:
    std::string generateModeToken();

    std::string generateWidthToken();

    std::string generateHeightToken();

    std::string generateFpsToken();

    CommonDisplayContextProvider* mDisplayContextProvider;

    const DisplayPresentProfile* mDisplayPresentProfile;
    int mDisplayId = -1;
};

} // namespace android::hardware::graphics::composer
