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

#include <string>
#include <string_view>
#include <utility>

namespace android::hardware::graphics::composer {

constexpr std::string_view kBoardApiLevePropertyId = "ro.board.api_level";

constexpr std::string_view kXrrVersionPropertyIdPrefix = "ro.vendor";
constexpr std::string_view kXrrVersionPropertyIdSuffix = "xrr.version";

// Mrr default version settings.
constexpr int kMrrDefaultVersion = 1;
constexpr int kMaxMrrVersion = 2;

// Vrr default version settings.
constexpr int kVrrDefaultVersion = 1;
constexpr int kMaxVrrVersion = 1;

enum XrrVersion {
    kMrr = 1,
    kVrr = 2,
    kTotalXrrVersion = kVrr,
};

typedef struct XrrVersionInfo {
    int majorVersion = kMrr;
    int minorVersion = kMrrDefaultVersion;

    bool isVrr() const { return (majorVersion == kVrr); }

    bool needVrrParameters() const {
        return (isVrr() || ((majorVersion == kMrr) && (minorVersion > 1)));
    }

    bool hasVrrController() const { return needVrrParameters(); }
} XrrVersionInfo_t;

long getBoardApiLevel();

std::pair<int, int> getDisplayXrrVersion(const std::string& displayTypeIdentifier);

} // namespace android::hardware::graphics::composer
