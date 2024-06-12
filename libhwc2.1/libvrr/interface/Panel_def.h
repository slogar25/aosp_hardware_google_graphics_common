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

namespace android::hardware::graphics::composer {

// Definition of panel refresh control.
const std::string kFrameRateNodeName = "frame_rate";
const std::string kRefreshControlNodeName = "refresh_ctrl";
const std::string kRefreshControlNodeEnabled = "Enabled";

static constexpr uint32_t kPanelRefreshCtrlFrameInsertionFrameCountOffset = 0;
static constexpr uint32_t kPanelRefreshCtrlFrameInsertionFrameCountBits = 7;
static constexpr uint32_t kPanelRefreshCtrlFrameInsertionFrameCountMax =
        (1U << kPanelRefreshCtrlFrameInsertionFrameCountBits) - 1;
static constexpr uint32_t kPanelRefreshCtrlFrameInsertionFrameCountMask =
        kPanelRefreshCtrlFrameInsertionFrameCountMax
        << kPanelRefreshCtrlFrameInsertionFrameCountOffset;

static constexpr uint32_t kPanelRefreshCtrlMinimumRefreshRateOffset =
        kPanelRefreshCtrlFrameInsertionFrameCountOffset +
        kPanelRefreshCtrlFrameInsertionFrameCountBits;
static constexpr uint32_t kPanelRefreshCtrlMinimumRefreshRateBits = 8;
static constexpr uint32_t kPanelRefreshCtrlMinimumRefreshRateMax =
        (1U << kPanelRefreshCtrlMinimumRefreshRateBits) - 1;
static constexpr uint32_t kPanelRefreshCtrlMinimumRefreshRateMask =
        (kPanelRefreshCtrlMinimumRefreshRateMax << kPanelRefreshCtrlMinimumRefreshRateOffset);

static constexpr uint32_t kPanelRefreshCtrlMrrV1OverV2Offset = 30;
static constexpr uint32_t kPanelRefreshCtrlMrrV1OverV2 = (1U << kPanelRefreshCtrlMrrV1OverV2Offset);
static constexpr uint32_t kPanelRefreshCtrlFrameInsertionAutoModeOffset = 31;
static constexpr uint32_t kPanelRefreshCtrlFrameInsertionAutoMode =
        (1U << kPanelRefreshCtrlFrameInsertionAutoModeOffset);

static constexpr uint32_t kPanelRefreshCtrlStateBitsMask = kPanelRefreshCtrlMrrV1OverV2 |
        kPanelRefreshCtrlFrameInsertionAutoMode | kPanelRefreshCtrlMinimumRefreshRateMask;

// Definition of protobuf path
static constexpr char kDefaultConfigPathPrefix[] = "/vendor/etc/panel_ctrl_";
static constexpr char kDefaultConfigPathSuffix[] = "_cal0.pb";

} // namespace android::hardware::graphics::composer
