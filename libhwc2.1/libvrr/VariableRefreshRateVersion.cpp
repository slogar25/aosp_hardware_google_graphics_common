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

#include "VariableRefreshRateVersion.h"

#include <android-base/properties.h>
#include <log/log.h>
#include <vector>

namespace android::hardware::graphics::composer {

long getBoardApiLevel() {
    long apiLevel = 0;
    auto apiLevelString = android::base::GetProperty("ro.board.api_level", "");
    if (!apiLevelString.empty()) {
        char* endPos;
        apiLevel = std::strtol(apiLevelString.c_str(), &endPos, 10);
        if (*endPos != 0) apiLevel = 0;
    }
    return apiLevel;
}

std::pair<int, int> getDisplayXrrVersion(const std::string& displayTypeIdentifier) {
    char* endPos;
    long apiLevel = getBoardApiLevel();
    size_t pos = 0;
    std::string versionString;
    float version;

    std::string xrrVersionPropertyId = std::string(kXrrVersionPropertyIdPrefix) + "." +
            displayTypeIdentifier + "." + std::string(kXrrVersionPropertyIdSuffix);
    auto xrrVersionString = android::base::GetProperty(xrrVersionPropertyId, "");

    std::vector<std::string> patterns;
    while ((pos = xrrVersionString.find(':')) != std::string::npos) {
        patterns.emplace_back(xrrVersionString.substr(0, pos));
        xrrVersionString.erase(0, pos + 1);
    }
    patterns.emplace_back(xrrVersionString);
    for (auto& pattern : patterns) {
        pos = pattern.find('@');
        if (pos == std::string::npos) {
            // There are no limitations for this setting, so we can apply it directly.
            versionString = pattern;
            break;
        } else {
            std::string candidateVersionString = pattern.substr(0, pos);
            pattern.erase(0, pos + 1);
            if (!pattern.empty()) {
                long minApiLevel = std::strtol(pattern.c_str(), &endPos, 10);
                if (minApiLevel == 0) continue;
                if (minApiLevel <= apiLevel) {
                    versionString = candidateVersionString;
                    break;
                }
            } else {
                ALOGW("%s() disregard this setting by an empty minimum API level.", __func__);
            }
        }
    }

    // If the versionString does not represent a floating-point number, std::atof will return 0.0f.
    version = std::atof(versionString.c_str());
    // The integer part represents the major version.
    int majorVersion = static_cast<int>(version);
    // The decimal part represents the minor version.
    int minorVersion = static_cast<int>(version * 10) % 10;

    if ((majorVersion > kTotalXrrVersion) || (majorVersion < kMrr)) {
        ALOGE("%s(): Illegal XRR major version (%d) detected; use MRR V1 instead.", __func__,
              majorVersion);
        majorVersion = kMrr;
        minorVersion = kMrrDefaultVersion;
    } else {
        if ((minorVersion <= 0) ||
            (minorVersion > ((majorVersion == kMrr) ? kMaxMrrVersion : kMaxVrrVersion))) {
            ALOGE("%s(): Illegal XRR minor version (%d) detected; use default instead.", __func__,
                  minorVersion);
            minorVersion = (majorVersion == kMrr ? kMrrDefaultVersion : kVrrDefaultVersion);
        }
    }
    return std::make_pair(majorVersion, minorVersion);
}

} // namespace android::hardware::graphics::composer
