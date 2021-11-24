/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef _BRIGHTNESS_CONTROLLER_H_
#define _BRIGHTNESS_CONTROLLER_H_

#include <drm/samsung_drm.h>
#include <fstream>
#include <utils/Mutex.h>

#include "ExynosDisplayDrmInterface.h"

/**
 * Brightness change requests come from binder calls or HWC itself.
 * The request could be applied via next drm commit or immeditely via sysfs.
 *
 * To make it simple, setDisplayBrightness from SF, if not triggering a HBM on/off,
 * will be applied immediately via sysfs path. All other requests will be applied via next
 * drm commit.
 *
 * Sysfs path is faster than drm path. So if there is a pending drm commit that may
 * change brightness level, sfsfs path task should wait until it has completed.
 */
class BrightnessController {
public:
    BrightnessController(int32_t panelIndex);
    int initDrm(const DrmDevice& drmDevice,
                const DrmConnector& connector);

    int processEnhancedHbm(bool on);
    int processDisplayBrightness(float bl, std::function<void(void)> refresh,
                                 const nsecs_t vsyncNs, bool waitPresent = false);
    int processLocalHbm(bool on);
    int applyPendingChangeViaSysfs(const nsecs_t vsyncNs);
    bool validateLayerWhitePointNits(float nits);

    /**
     * processInstantHbm for GHBM UDFPS
     *  - on true: turn on HBM at next frame with peak brightness
     *       false: turn off HBM at next frame and use system display brightness
     *              from processDisplayBrightness
     */
    int processInstantHbm(bool on);

    void updateFrameStates(bool hdrFullScreen) { mHdrFullScreen.store(hdrFullScreen); }

    /**
     * Dim ratio to keep the sdr brightness unchange after an instant hbm on with peak brightness.
     */
    float getSdrDimRatioForInstantHbm();

    void onClearDisplay();

    /**
     * apply brightness change on drm path.
     * Note: only this path can hold the lock for a long time
     */
    int prepareFrameCommit(ExynosDisplay& display,
                           const DrmConnector& connector,
                           ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq,
                           bool& ghbmSync, bool& lhbmSync, bool& blSync);

    bool isGhbmSupported() { return mGhbmSupported; }
    bool isLhbmSupported() { return mLhbmSupported; }

    bool isGhbmOn() {
        std::lock_guard<std::mutex> lock(mBrightnessMutex);
        return mGhbm.get() != HbmMode::OFF;
    }

    bool isLhbmOn() {
        std::lock_guard<std::mutex> lock(mBrightnessMutex);
        return mLhbm.get();
    }

    uint32_t getBrightnessLevel() {
        std::lock_guard<std::mutex> lock(mBrightnessMutex);
        return mBrightnessLevel.get();
    }

    bool isDimSdr() {
        std::lock_guard<std::mutex> lock(mBrightnessMutex);
        return mInstantHbmReq.get();
    }

    bool isHdrFullScreen() {
        return mHdrFullScreen.get();
    }

    bool isSupported() {
        // valid mMaxBrightness means both brightness and max_brightness sysfs exist
        return mMaxBrightness > 0;
    }

    int getDisplayWhitePointNits(float* nits) {
        if (!nits) {
            return HWC2_ERROR_BAD_PARAMETER;
        }

        if (!mBrightnessIntfSupported) {
            return HWC2_ERROR_UNSUPPORTED;
        }

        *nits = mDisplayWhitePointNits;
        return NO_ERROR;
    }

    void dump(String8 &result);

    struct BrightnessTable {
        float mBriStart;
        float mBriEnd;
        uint32_t mBklStart;
        uint32_t mBklEnd;
        uint32_t mNitsStart;
        uint32_t mNitsEnd;
        BrightnessTable() {}
        BrightnessTable(const brightness_attribute &attr)
              : mBriStart(static_cast<float>(attr.percentage.min) / 100.0f),
                mBriEnd(static_cast<float>(attr.percentage.max) / 100.0f),
                mBklStart(attr.level.min),
                mBklEnd(attr.level.max),
                mNitsStart(attr.nits.min),
                mNitsEnd(attr.nits.max) {}
    };

    const BrightnessTable *getBrightnessTable() { return mBrightnessTable; }

    enum class BrightnessRange : uint32_t {
        NORMAL = 0,
        HBM,
        MAX,
    };

    enum class HbmMode {
        OFF = 0,
        ON_IRC_ON,
        ON_IRC_OFF,
    };

    /*
     * BrightnessDimmingUsage:
     * NORMAL- enable dimming
     * HBM-    enable dimming only for hbm transition
     * NONE-   disable dimming
     */
    enum class BrightnessDimmingUsage {
        NORMAL = 0,
        HBM,
        NONE,
    };

private:
    // Worst case for panel with brightness range 2 nits to 1000 nits.
    static constexpr float kGhbmMinDimRatio = 0.002;
    static constexpr int32_t kHbmDimmingTimeUs = 5000000;
    static constexpr const char *kLocalHbmModeFileNode =
                "/sys/class/backlight/panel%d-backlight/local_hbm_mode";
    static constexpr const char *kGlobalHbmModeFileNode =
                "/sys/class/backlight/panel%d-backlight/hbm_mode";

    int queryBrightness(float brightness, bool* ghbm = nullptr, uint32_t* level = nullptr,
                        float *nits = nullptr);
    int checkSysfsStatus(const char *file, const std::string &expectedValue,
                         const nsecs_t timeoutNs);
    void initBrightnessTable(const DrmDevice& device, const DrmConnector& connector);
    void initBrightnessSysfs();
    int applyBrightnessViaSysfs(uint32_t level);
    int updateStates() REQUIRES(mBrightnessMutex);

    void parseHbmModeEnums(const DrmProperty& property);

    bool mLhbmSupported = false;
    bool mGhbmSupported = false;
    bool mBrightnessIntfSupported = false;
    BrightnessTable mBrightnessTable[toUnderlying(BrightnessRange::MAX)];

    int32_t mPanelIndex;
    DrmEnumParser::MapHal2DrmEnum mHbmModeEnums;

    // brightness state
    std::mutex mBrightnessMutex;
    // requests
    CtrlValue<bool> mEnhanceHbmReq GUARDED_BY(mBrightnessMutex);
    CtrlValue<bool> mLhbmReq GUARDED_BY(mBrightnessMutex);
    CtrlValue<float> mBrightnessFloatReq GUARDED_BY(mBrightnessMutex);
    CtrlValue<bool> mInstantHbmReq GUARDED_BY(mBrightnessMutex);
    // states to drm after updateStates call
    CtrlValue<uint32_t> mBrightnessLevel GUARDED_BY(mBrightnessMutex);
    CtrlValue<HbmMode> mGhbm GUARDED_BY(mBrightnessMutex);
    CtrlValue<bool> mDimming GUARDED_BY(mBrightnessMutex);
    CtrlValue<bool> mLhbm GUARDED_BY(mBrightnessMutex);

    // Indicating if the last LHBM on has changed the brightness level
    bool mLhbmBrightnessAdj = false;

    CtrlValue<bool> mHdrFullScreen;

    // these are used by sysfs path to wait drm path bl change task
    // indicationg an unchecked LHBM change in drm path
    std::atomic<bool> mUncheckedLhbmRequest = false;
    std::atomic<bool> mPendingLhbmStatus = false;
    // indicationg an unchecked GHBM change in drm path
    std::atomic<bool> mUncheckedGbhmRequest = false;
    std::atomic<HbmMode> mPendingGhbmStatus = HbmMode::OFF;
    // indicating an unchecked brightness change in drm path
    std::atomic<bool> mUncheckedBlRequest = false;
    std::atomic<uint32_t> mPendingBl = 0;

    // these are dimming related
    BrightnessDimmingUsage mBrightnessDimmingUsage = BrightnessDimmingUsage::NORMAL;
    bool mHbmSvDimming = false;
    int32_t mHbmDimmingTimeUs = 0;
    struct timeval mHbmDimmingStart = { 0, 0 };

    // sysfs path
    std::ofstream mBrightnessOfs;
    uint32_t mMaxBrightness = 0; // read from sysfs

    // Note IRC or dimming is not in consideration for now.
    float mDisplayWhitePointNits = 0;
};

#endif // _BRIGHTNESS_CONTROLLER_H_
