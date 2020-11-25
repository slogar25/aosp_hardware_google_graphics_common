/*
 * Copyright (C) 2012 The Android Open Source Project
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
//#define LOG_NDEBUG 0

#include "ExynosHWCDebug.h"
#include "ExynosPrimaryDisplay.h"
#include "ExynosDevice.h"
#include "ExynosHWCHelper.h"
#include "ExynosExternalDisplay.h"
#include "ExynosDisplayDrmInterface.h"
#include "ExynosDisplayDrmInterfaceModule.h"

extern struct exynos_hwc_control exynosHWCControl;

ExynosPrimaryDisplay::ExynosPrimaryDisplay(uint32_t __unused type, ExynosDevice *device)
    :   ExynosDisplay(HWC_DISPLAY_PRIMARY, device)
{
    // TODO : Hard coded here
    mNumMaxPriorityAllowed = 5;

    /* Initialization */
    mDisplayId = HWC_DISPLAY_PRIMARY;
    mDisplayName = android::String8("PrimaryDisplay");

    // Prepare multi resolution
    // Will be exynosHWCControl.multiResoultion
    mResolutionInfo.nNum = 1;
    mResolutionInfo.nResolution[0].w = 1440;
    mResolutionInfo.nResolution[0].h = 2960;
    mResolutionInfo.nDSCYSliceSize[0] = 40;
    mResolutionInfo.nDSCXSliceSize[0] = 1440 / 2;
    mResolutionInfo.nPanelType[0] = PANEL_DSC;
    mResolutionInfo.nResolution[1].w = 1080;
    mResolutionInfo.nResolution[1].h = 2220;
    mResolutionInfo.nDSCYSliceSize[1] = 30;
    mResolutionInfo.nDSCXSliceSize[1] = 1080 / 2;
    mResolutionInfo.nPanelType[1] = PANEL_DSC;
    mResolutionInfo.nResolution[2].w = 720;
    mResolutionInfo.nResolution[2].h = 1480;
    mResolutionInfo.nDSCYSliceSize[2] = 74;
    mResolutionInfo.nDSCXSliceSize[2] = 720;
    mResolutionInfo.nPanelType[2] = PANEL_LEGACY;

#if defined(MAX_BRIGHTNESS_NODE_BASE) && defined(BRIGHTNESS_NODE_BASE)
    FILE *maxBrightnessFd = fopen(MAX_BRIGHTNESS_NODE_BASE, "r");
    ALOGI("Trying %s open for get max brightness", MAX_BRIGHTNESS_NODE_BASE);

    if (maxBrightnessFd != NULL) {
        char val[MAX_BRIGHTNESS_LEN] = {0};
        size_t size = fread(val, 1, MAX_BRIGHTNESS_LEN, maxBrightnessFd);
        if (size) {
            mMaxBrightness = atoi(val);
            ALOGI("Max brightness : %d", mMaxBrightness);

            mBrightnessFd = fopen(BRIGHTNESS_NODE_BASE, "w+");
            ALOGI("Trying %s open for brightness control", BRIGHTNESS_NODE_BASE);

            if (mBrightnessFd == NULL)
                ALOGE("%s open failed! %s", BRIGHTNESS_NODE_BASE, strerror(errno));
        } else {
            ALOGE("Max brightness read failed (size: %zu)", size);
            if (ferror(maxBrightnessFd)) {
                ALOGE("An error occurred");
                clearerr(maxBrightnessFd);
            }
        }
        fclose(maxBrightnessFd);
    } else {
        ALOGE("Brightness node is not opened");
    }
#endif

}

ExynosPrimaryDisplay::~ExynosPrimaryDisplay()
{
    if (mBrightnessFd != NULL) {
        fclose(mBrightnessFd);
        mBrightnessFd = NULL;
    }
}

void ExynosPrimaryDisplay::setDDIScalerEnable(int width, int height) {

    if (exynosHWCControl.setDDIScaler == false) return;

    ALOGI("DDISCALER Info : setDDIScalerEnable(w=%d,h=%d)", width, height);
    mNewScaledWidth = width;
    mNewScaledHeight = height;
    mXres = width;
    mYres = height;
}

int ExynosPrimaryDisplay::getDDIScalerMode(int width, int height) {

    if (exynosHWCControl.setDDIScaler == false) return 1;

    // Check if panel support support resolution or not.
    for (uint32_t i=0; i < mResolutionInfo.nNum; i++) {
        if (mResolutionInfo.nResolution[i].w * mResolutionInfo.nResolution[i].h ==
                static_cast<uint32_t>(width * height))
            return i + 1;
    }

    return 1; // WQHD
}

int32_t ExynosPrimaryDisplay::getActiveConfig(hwc2_config_t *outConfig) {
    if (outConfig && mPendActiveConfig != UINT_MAX) {
        *outConfig = mPendActiveConfig;
        return HWC2_ERROR_NONE;
    }
    return ExynosDisplay::getActiveConfig(outConfig);
}

int32_t ExynosPrimaryDisplay::setActiveConfig(hwc2_config_t config) {
    hwc2_config_t cur_config;

    getActiveConfig(&cur_config);
    if (cur_config == config) {
        ALOGI("%s:: Same display config is set", __func__);
        return HWC2_ERROR_NONE;
    }
    if (mPowerModeState != HWC2_POWER_MODE_ON) {
        mPendActiveConfig = config;
        return HWC2_ERROR_NONE;
    }
    return ExynosDisplay::setActiveConfig(config);
}

int32_t ExynosPrimaryDisplay::applyPendingConfig() {
    hwc2_config_t config;

    if (mPendActiveConfig != UINT_MAX) {
        config = mPendActiveConfig;
        mPendActiveConfig = UINT_MAX;
    } else {
        getActiveConfig(&config);
    }
    return ExynosDisplay::setActiveConfig(config);
}

int32_t ExynosPrimaryDisplay::setPowerOn() {
    ATRACE_CALL();

    int ret = applyPendingConfig();

    if (mPowerModeState == HWC2_POWER_MODE_OFF) {
        // check the dynamic recomposition thread by following display
        mDevice->checkDynamicRecompositionThread();
        if (ret) {
            mDisplayInterface->setPowerMode(HWC2_POWER_MODE_ON);
        }
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_ON);
    }

    mPowerModeState = HWC2_POWER_MODE_ON;

    return HWC2_ERROR_NONE;
}

int32_t ExynosPrimaryDisplay::setPowerOff() {
    ATRACE_CALL();

    clearDisplay();

    // check the dynamic recomposition thread by following display
    mDevice->checkDynamicRecompositionThread();

    mDisplayInterface->setPowerMode(HWC2_POWER_MODE_OFF);
    mPowerModeState = HWC2_POWER_MODE_OFF;

    /* It should be called from validate() when the screen is on */
    mSkipFrame = true;
    setGeometryChanged(GEOMETRY_DISPLAY_POWER_OFF);
    if ((mRenderingState >= RENDERING_STATE_VALIDATED) &&
        (mRenderingState < RENDERING_STATE_PRESENTED))
        closeFencesForSkipFrame(RENDERING_STATE_VALIDATED);
    mRenderingState = RENDERING_STATE_NONE;

    return HWC2_ERROR_NONE;
}

int32_t ExynosPrimaryDisplay::setPowerDoze() {
    ATRACE_CALL();

    if (!mDisplayInterface->isDozeModeAvailable()) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (mDisplayInterface->setLowPowerMode()) {
        ALOGI("Not support LP mode.");
        return HWC2_ERROR_UNSUPPORTED;
    }

    mPowerModeState = HWC2_POWER_MODE_DOZE;

    return HWC2_ERROR_NONE;
}

int32_t ExynosPrimaryDisplay::setPowerMode(int32_t mode) {
    Mutex::Autolock lock(mDisplayMutex);

    if (mode == static_cast<int32_t>(ext_hwc2_power_mode_t::PAUSE)) {
        mode = HWC2_POWER_MODE_OFF;
        mPauseDisplay = true;
    } else if (mode == static_cast<int32_t>(ext_hwc2_power_mode_t::RESUME)) {
        mode = HWC2_POWER_MODE_ON;
        mPauseDisplay = false;
    }

    if (mode == static_cast<int32_t>(mPowerModeState)) {
        ALOGI("Skip power mode transition due to the same power state.");
        return HWC2_ERROR_NONE;
    }

    int fb_blank = (mode != HWC2_POWER_MODE_OFF) ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
    ALOGD("%s:: FBIOBLANK mode(%d), blank(%d)", __func__, mode, fb_blank);

    if (fb_blank == FB_BLANK_POWERDOWN)
        mDREnable = false;
    else
        mDREnable = mDRDefault;

    switch (mode) {
        case HWC2_POWER_MODE_DOZE_SUSPEND:
            return HWC2_ERROR_UNSUPPORTED;
        case HWC2_POWER_MODE_DOZE:
            return setPowerDoze();
        case HWC2_POWER_MODE_OFF:
            setPowerOff();
            break;
        case HWC2_POWER_MODE_ON:
            setPowerOn();
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

bool ExynosPrimaryDisplay::getHDRException(ExynosLayer* __unused layer)
{
    return false;
}

void ExynosPrimaryDisplay::initDisplayInterface(uint32_t interfaceType)
{
    if (interfaceType == INTERFACE_TYPE_DRM)
        mDisplayInterface = std::make_unique<ExynosPrimaryDisplayDrmInterfaceModule>((ExynosDisplay *)this);
    else
        LOG_ALWAYS_FATAL("%s::Unknown interface type(%d)",
                __func__, interfaceType);
    mDisplayInterface->init(this);
}
