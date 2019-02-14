/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "ExynosDisplayFbInterface.h"
#include "ExynosDisplay.h"
#include "ExynosHWCDebug.h"

using namespace android;
extern struct exynos_hwc_control exynosHWCControl;

ExynosDisplayFbInterface::ExynosDisplayFbInterface(ExynosDisplay *exynosDisplay)
: mDisplayFd(-1)
{
    mExynosDisplay = exynosDisplay;
}

ExynosDisplayFbInterface::~ExynosDisplayFbInterface()
{
    if (mDisplayFd >= 0)
        fence_close(mDisplayFd, mExynosDisplay, FENCE_TYPE_UNDEFINED, FENCE_IP_UNDEFINED);
    mDisplayFd = -1;
}

void ExynosDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mDisplayFd = -1;
    mExynosDisplay = exynosDisplay;
}

int32_t ExynosDisplayFbInterface::setPowerMode(int32_t mode)
{
    int32_t ret = NO_ERROR;
    int fb_blank = 0;
    if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    if ((ret = ioctl(mDisplayFd, FBIOBLANK, fb_blank)) != NO_ERROR) {
        HWC_LOGE(mExynosDisplay, "set powermode ioctl failed errno : %d", errno);
    }

    ALOGD("%s:: mode(%d), blank(%d)", __func__, mode, fb_blank);
    return ret;
}

int32_t ExynosDisplayFbInterface::setVsyncEnabled(uint32_t enabled)
{
    return ioctl(mDisplayFd, S3CFB_SET_VSYNC_INT, &enabled);
}

int32_t ExynosDisplayFbInterface::getDisplayAttribute(
        hwc2_config_t __unused config,
        int32_t attribute, int32_t* outValue)
{
    switch (attribute) {
    case HWC2_ATTRIBUTE_VSYNC_PERIOD:
        *outValue = mExynosDisplay->mVsyncPeriod;
        break;

    case HWC2_ATTRIBUTE_WIDTH:
        *outValue = mExynosDisplay->mXres;
        break;

    case HWC2_ATTRIBUTE_HEIGHT:
        *outValue = mExynosDisplay->mYres;
        break;

    case HWC2_ATTRIBUTE_DPI_X:
        *outValue = mExynosDisplay->mXdpi;
        break;

    case HWC2_ATTRIBUTE_DPI_Y:
        *outValue = mExynosDisplay->mYdpi;
        break;
    default:
        ALOGE("unknown display attribute %u", attribute);
        return HWC2_ERROR_BAD_CONFIG;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs)
{
    if (outConfigs == NULL)
        *outNumConfigs = 1;
    else if (*outNumConfigs >= 1)
        outConfigs[0] = 0;

    *outNumConfigs = 1;
    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::setActiveConfig(hwc2_config_t __unused config)
{
    int32_t ret = NO_ERROR;
    return ret;
}

void ExynosDisplayFbInterface::dumpDisplayConfigs()
{
}

int32_t ExynosDisplayFbInterface::getColorModes(
        uint32_t* outNumModes,
        int32_t* outModes)
{
    uint32_t colorModeNum = 0;
    int32_t ret = 0;
    if ((ret = ioctl(mDisplayFd, EXYNOS_GET_COLOR_MODE_NUM, &colorModeNum )) < 0) {
        *outNumModes = 1;

        ALOGI("%s:: is not supported", __func__);
        if (outModes != NULL) {
            outModes[0] = HAL_COLOR_MODE_NATIVE;
        }
        return HWC2_ERROR_NONE;
    }

    if (outModes == NULL) {
        ALOGI("%s:: Supported color modes (%d)", __func__, colorModeNum);
        *outNumModes = colorModeNum;
        return HWC2_ERROR_NONE;
    }

    if (*outNumModes != colorModeNum) {
        ALOGE("%s:: invalid outNumModes(%d), should be(%d)", __func__, *outNumModes, colorModeNum);
        return -EINVAL;
    }

    for (uint32_t i= 0 ; i < colorModeNum; i++) {
        struct decon_color_mode_info colorMode;
        colorMode.index = i;
        if ((ret = ioctl(mDisplayFd, EXYNOS_GET_COLOR_MODE, &colorMode )) < 0) {
            return HWC2_ERROR_UNSUPPORTED;
        }
        ALOGI("\t[%d] mode %d", i, colorMode.color_mode);
        outModes[i] = colorMode.color_mode;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::setColorMode(int32_t mode)
{
    return ioctl(mDisplayFd, EXYNOS_SET_COLOR_MODE, &mode);
}

int32_t ExynosDisplayFbInterface::setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos) {
    struct decon_user_window win_pos;
    win_pos.x = x_pos;
    win_pos.y = y_pos;
    return ioctl(this->mDisplayFd, S3CFB_WIN_POSITION, &win_pos);
}

int32_t ExynosDisplayFbInterface::getHdrCapabilities(uint32_t* outNumTypes,
        int32_t* outTypes, float* outMaxLuminance,
        float* outMaxAverageLuminance, float* outMinLuminance)
{

    if (outTypes == NULL) {
        struct decon_hdr_capabilities_info outInfo;
        memset(&outInfo, 0, sizeof(outInfo));
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
            return -1;
        }

        *outMaxLuminance = (float)outInfo.max_luminance / (float)10000;
        *outMaxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
        *outMinLuminance = (float)outInfo.min_luminance / (float)10000;
        *outNumTypes = outInfo.out_num;
        // Save to member variables
        mExynosDisplay->mHdrTypeNum = *outNumTypes;
        mExynosDisplay->mMaxLuminance = *outMaxLuminance;
        mExynosDisplay->mMaxAverageLuminance = *outMaxAverageLuminance;
        mExynosDisplay->mMinLuminance = *outMinLuminance;
        ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f)",
                mExynosDisplay->mDisplayName.string(), mExynosDisplay->mHdrTypeNum, mExynosDisplay->mMaxLuminance,
                mExynosDisplay->mMaxAverageLuminance, mExynosDisplay->mMinLuminance);
        return 0;
    }

    struct decon_hdr_capabilities outData;
    memset(&outData, 0, sizeof(outData));

    for (uint32_t i = 0; i < *outNumTypes; i += SET_HDR_CAPABILITIES_NUM) {
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES, &outData) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES ioctl Failed");
            return -1;
        }
        for (uint32_t j = 0; j < *outNumTypes - i; j++)
            outTypes[i+j] = outData.out_types[j];
        // Save to member variables
        mExynosDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        HDEBUGLOGD(eDebugHWC, "%s HWC2: Types : %d",
                mExynosDisplay->mDisplayName.string(), mExynosDisplay->mHdrTypes[i]);
    }
    return 0;
}

int32_t ExynosDisplayFbInterface::deliverWinConfigData()
{
    ATRACE_CALL();
    return ioctl(mDisplayFd, S3CFB_WIN_CONFIG, mExynosDisplay->mWinConfigData);
}

int32_t ExynosDisplayFbInterface::clearDisplay()
{
    int ret = 0;

    struct decon_win_config_data win_data;
    memset(&win_data, 0, sizeof(win_data));
    win_data.retire_fence = -1;
    struct decon_win_config *config = win_data.config;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        config[i].acq_fence = -1;
        config[i].rel_fence = -1;
    }

#if defined(HWC_CLEARDISPLAY_WITH_COLORMAP)
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        if (i == mExynosDisplay->mBaseWindowIndex) {
            config[i].state = config[i].DECON_WIN_STATE_COLOR;
            config[i].idma_type = mExynosDisplay->mDefaultDMA;
            config[i].color = 0x0;
            config[i].dst.x = 0;
            config[i].dst.y = 0;
            config[i].dst.w = mExynosDisplay->mXres;
            config[i].dst.h = mExynosDisplay->mYres;
            config[i].dst.f_w = mExynosDisplay->mXres;
            config[i].dst.f_h = mExynosDisplay->mYres;
        }
        else
            config[i].state = config[i].DECON_WIN_STATE_DISABLED;
    }
#endif

    win_data.retire_fence = -1;

    ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &win_data);
    if (ret < 0)
        HWC_LOGE(mExynosDisplay, "ioctl S3CFB_WIN_CONFIG failed to clear screen: %s",
                strerror(errno));

    if (win_data.retire_fence > 0)
        fence_close(win_data.retire_fence, mExynosDisplay, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
    return ret;
}

int32_t ExynosDisplayFbInterface::disableSelfRefresh(uint32_t disable)
{
    return ioctl(mDisplayFd, S3CFB_DECON_SELF_REFRESH, &disable);
}

int32_t ExynosDisplayFbInterface::setForcePanic()
{
    if (exynosHWCControl.forcePanic == 0)
        return NO_ERROR;

    usleep(20000);
    return ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);
}
