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
#include "ExynosPrimaryDisplay.h"
#include "ExynosExternalDisplay.h"
#include "ExynosHWCDebug.h"
#include "displayport_for_hwc.h"
#include <math.h>

using namespace android;
extern struct exynos_hwc_control exynosHWCControl;

const size_t NUM_HW_WINDOWS = MAX_DECON_WIN;
//////////////////////////////////////////////////// ExynosDisplayFbInterface //////////////////////////////////////////////////////////////////
ExynosDisplayFbInterface::ExynosDisplayFbInterface(ExynosDisplay *exynosDisplay)
: mDisplayFd(-1)
{
    mExynosDisplay = exynosDisplay;
    clearFbWinConfigData(mFbConfigData);
    mType = INTERFACE_TYPE_FB;
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

    if (exynosDisplay->mMaxWindowNum != getMaxWindowNum()) {
        ALOGE("%s:: Invalid max window number (mMaxWindowNum: %d, NUM_HW_WINDOWS: %zu",
                __func__, exynosDisplay->mMaxWindowNum, NUM_HW_WINDOWS);
        return;
    }
}

int32_t ExynosDisplayFbInterface::setPowerMode(int32_t mode)
{
    int fb_blank = 0;
    if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    int32_t ret = NO_ERROR;
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

int32_t ExynosDisplayFbInterface::setActiveConfig(hwc2_config_t config)
{
    int32_t ret = NO_ERROR;
    if (config != 0)
        ret = HWC2_ERROR_BAD_CONFIG;
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
    return ioctl(mDisplayFd, S3CFB_WIN_POSITION, &win_pos);
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

decon_idma_type ExynosDisplayFbInterface::getDeconDMAType(ExynosMPP* __unused otfMPP)
{
    return MAX_DECON_DMA_TYPE;
}

int32_t ExynosDisplayFbInterface::configFromDisplayConfig(decon_win_config &config,
        const exynos_win_config_data &display_config)
{
    if (display_config.state == display_config.WIN_STATE_DISABLED)
        return NO_ERROR;

    config.dst = display_config.dst;
    config.plane_alpha = 255;
    if ((display_config.plane_alpha >= 0) && (display_config.plane_alpha < 255)) {
        config.plane_alpha = display_config.plane_alpha;
    }
    if ((config.blending = halBlendingToS3CBlending(display_config.blending))
            >= DECON_BLENDING_MAX) {
        HWC_LOGE(mExynosDisplay, "%s:: config has invalid blending(0x%8x)",
                __func__, display_config.blending);
        return -EINVAL;
    }

    if (display_config.assignedMPP == NULL) {
        HWC_LOGE(mExynosDisplay, "%s:: config has invalid idma_type, assignedMPP is NULL",
                __func__);
        return -EINVAL;
    } else if ((config.idma_type = getDeconDMAType(display_config.assignedMPP))
            == MAX_DECON_DMA_TYPE) {
        HWC_LOGE(mExynosDisplay, "%s:: config has invalid idma_type, assignedMPP(%s)",
                __func__, display_config.assignedMPP->mName.string());
        return -EINVAL;
    }

    if (display_config.state == display_config.WIN_STATE_COLOR) {
        config.state = config.DECON_WIN_STATE_COLOR;
        config.color = display_config.color;
        if (!((display_config.plane_alpha >= 0) && (display_config.plane_alpha <= 255)))
            config.plane_alpha = 0;
    } else if ((display_config.state == display_config.WIN_STATE_BUFFER) ||
            (display_config.state == display_config.WIN_STATE_CURSOR)) {
        if (display_config.state == display_config.WIN_STATE_BUFFER)
            config.state = config.DECON_WIN_STATE_BUFFER;
        else
            config.state = config.DECON_WIN_STATE_CURSOR;

        config.fd_idma[0] = display_config.fd_idma[0];
        config.fd_idma[1] = display_config.fd_idma[1];
        config.fd_idma[2] = display_config.fd_idma[2];
        config.acq_fence = display_config.acq_fence;
        config.rel_fence = display_config.rel_fence;
        if ((config.format = halFormatToS3CFormat(display_config.format))
                == DECON_PIXEL_FORMAT_MAX) {
            HWC_LOGE(mExynosDisplay, "%s:: config has invalid format(0x%8x)",
                    __func__, display_config.format);
            return -EINVAL;
        }
        config.dpp_parm.comp_src = display_config.comp_src;
        config.dpp_parm.rot = (dpp_rotate)halTransformToS3CRot(display_config.transform);
        config.dpp_parm.eq_mode = halDataSpaceToDisplayParam(display_config);
        if (display_config.hdr_enable)
            config.dpp_parm.hdr_std = halTransferToDisplayParam(display_config);
        config.dpp_parm.min_luminance = display_config.min_luminance;
        config.dpp_parm.max_luminance = display_config.max_luminance;
        config.block_area = display_config.block_area;
        config.transparent_area = display_config.transparent_area;
        config.opaque_area = display_config.opaque_area;
        config.src = display_config.src;
        config.protection = display_config.protection;
        config.compression = display_config.compression;
    }
    return NO_ERROR;
}

int32_t ExynosDisplayFbInterface::deliverWinConfigData()
{
    android::String8 result;
    clearFbWinConfigData(mFbConfigData);
    struct decon_win_config *config = mFbConfigData.config;
    for (uint32_t i = 0; i < NUM_HW_WINDOWS; i++) {
        int32_t ret = configFromDisplayConfig(mFbConfigData.config[i],
                 mExynosDisplay->mDpuData.configs[i]);
        if (ret != NO_ERROR) {
            HWC_LOGE(mExynosDisplay, "configFromDisplayConfig config[%d] fail", i);
            return ret;
        }
    }
    if (mExynosDisplay->mDpuData.enable_win_update) {
        size_t winUpdateInfoIdx = DECON_WIN_UPDATE_IDX;
        config[winUpdateInfoIdx].state = config[winUpdateInfoIdx].DECON_WIN_STATE_UPDATE;
        config[winUpdateInfoIdx].dst.x = mExynosDisplay->mDpuData.win_update_region.x;
        config[winUpdateInfoIdx].dst.w = mExynosDisplay->mDpuData.win_update_region.w;
        config[winUpdateInfoIdx].dst.y = mExynosDisplay->mDpuData.win_update_region.y;
        config[winUpdateInfoIdx].dst.h = mExynosDisplay->mDpuData.win_update_region.h;
    }

    dumpFbWinConfigInfo(result, mFbConfigData, true);

    int32_t ret = 0;
    {
        ATRACE_CALL();
        ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &mFbConfigData);
    }

    if (ret) {
        result.clear();
        result.appendFormat("WIN_CONFIG ioctl error\n");
        HWC_LOGE(mExynosDisplay, "%s", dumpFbWinConfigInfo(result, mFbConfigData).string());
        return ret;
    } else {
        mExynosDisplay->mDpuData.retire_fence = mFbConfigData.retire_fence;
        struct decon_win_config *config = mFbConfigData.config;
        for (uint32_t i = 0; i < NUM_HW_WINDOWS; i++) {
            mExynosDisplay->mDpuData.configs[i].rel_fence = config[i].rel_fence;
        }
    }

    return NO_ERROR;
}

int32_t ExynosDisplayFbInterface::clearDisplay()
{
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

    const int ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &win_data);
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

void ExynosDisplayFbInterface::clearFbWinConfigData(decon_win_config_data &winConfigData)
{
    winConfigData = {};
    winConfigData.fd_odma = -1;
    winConfigData.retire_fence = -1;
    struct decon_win_config *config = winConfigData.config;
    /* init */
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        config[i].fd_idma[0] = -1;
        config[i].fd_idma[1] = -1;
        config[i].fd_idma[2] = -1;
        config[i].acq_fence = -1;
        config[i].rel_fence = -1;
    }
}

dpp_csc_eq ExynosDisplayFbInterface::halDataSpaceToDisplayParam(const exynos_win_config_data& config)
{
    uint32_t cscEQ = 0;
    android_dataspace dataspace = dataspaceFromConfig(config);
    ExynosMPP* otfMPP = config.assignedMPP;
    uint32_t standard = (dataspace & HAL_DATASPACE_STANDARD_MASK);
    uint32_t range = (dataspace & HAL_DATASPACE_RANGE_MASK);

    if (otfMPP == NULL) {
        HWC_LOGE(mExynosDisplay, "%s:: assignedMPP is NULL", __func__);
        return (dpp_csc_eq)cscEQ;
    }

    if (dataspace_standard_map.find(standard) != dataspace_standard_map.end())
        cscEQ = dataspace_standard_map.at(standard).eq_mode;
    else
        cscEQ = CSC_UNSPECIFIED;

    if ((otfMPP->mAttr & MPP_ATTR_WCG) == 0) {
        switch(cscEQ) {
            case CSC_BT_709:
            case CSC_BT_601:
            case CSC_BT_2020:
            case CSC_DCI_P3:
                break;
            default:
                cscEQ = CSC_UNSPECIFIED;
                break;
        }
        switch(range) {
            case HAL_DATASPACE_RANGE_FULL:
            case HAL_DATASPACE_RANGE_LIMITED:
                break;
            default:
                range = HAL_DATASPACE_RANGE_UNSPECIFIED;
                break;
        }
    }

    if (dataspace_range_map.find(range) != dataspace_range_map.end())
        cscEQ |= dataspace_range_map.at(range);
    else
        cscEQ |= (CSC_RANGE_UNSPECIFIED << CSC_RANGE_SHIFT);

    return (dpp_csc_eq)cscEQ;
}

dpp_hdr_standard ExynosDisplayFbInterface::halTransferToDisplayParam(const exynos_win_config_data& config)
{
    android_dataspace dataspace = dataspaceFromConfig(config);
    ExynosMPP* otfMPP = config.assignedMPP;

    uint32_t transfer = (dataspace & HAL_DATASPACE_TRANSFER_MASK);
    dpp_hdr_standard ret = DPP_HDR_OFF;

    if (otfMPP == NULL) return ret;

    if ((otfMPP->mAttr & MPP_ATTR_WCG) == 0) {
        if (hasHdrInfo(dataspace) == false)
            return DPP_HDR_OFF;
    }

    if (((otfMPP->mAttr & MPP_ATTR_HDR10) == 0) &&
        ((otfMPP->mAttr & MPP_ATTR_WCG) == 0) &&
        ((otfMPP->mAttr & MPP_ATTR_HDR10PLUS) == 0)) return DPP_HDR_OFF;

    if (dataspace_transfer_map.find(transfer) != dataspace_transfer_map.end())
        ret = dataspace_transfer_map.at(transfer).hdr_std;

    return ret;
}

String8& ExynosDisplayFbInterface::dumpFbWinConfigInfo(String8 &result,
        decon_win_config_data &fbConfig, bool debugPrint)
{
    /* print log only if eDebugDisplayInterfaceConfig flag is set when debugPrint is true */
    if (debugPrint &&
        (hwcCheckDebugMessages(eDebugDisplayInterfaceConfig) == false))
        return result;

    result.appendFormat("retire_fence(%d)\n", mFbConfigData.retire_fence);
    struct decon_win_config *config = mFbConfigData.config;
    for (uint32_t i = 0; i <= NUM_HW_WINDOWS; i++) {
        decon_win_config &c = config[i];
        String8 configString;
        configString.appendFormat("win[%d] state = %u\n", i, c.state);
        if (c.state == c.DECON_WIN_STATE_COLOR) {
            configString.appendFormat("\t\tx = %d, y = %d, width = %d, height = %d, color = %u, alpha = %u\n",
                    c.dst.x, c.dst.y, c.dst.w, c.dst.h, c.color, c.plane_alpha);
        } else/* if (c.state != c.DECON_WIN_STATE_DISABLED) */{
            configString.appendFormat("\t\tidma = %d, fd = (%d, %d, %d), acq_fence = %d, rel_fence = %d "
                    "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                    "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                    "format = %u, pa = %d, rot = %d, eq_mode = 0x%8x, hdr_std = %d, blending = %u, "
                    "protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                    "block(x:%d, y:%d, w:%d, h:%d)\n",
                    c.idma_type, c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                    c.acq_fence, c.rel_fence,
                    c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                    c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                    c.format, c.plane_alpha, c.dpp_parm.rot, c.dpp_parm.eq_mode,
                    c.dpp_parm.hdr_std, c.blending, c.protection,
                    c.compression, c.dpp_parm.comp_src,
                    c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                    c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
        }

        if (debugPrint)
            ALOGD("%s", configString.string());
        else
            result.append(configString);
    }
    return result;
}

uint32_t ExynosDisplayFbInterface::getMaxWindowNum()
{
    return NUM_HW_WINDOWS;
}

//////////////////////////////////////////////////// ExynosPrimaryDisplayFbInterface //////////////////////////////////////////////////////////////////

ExynosPrimaryDisplayFbInterface::ExynosPrimaryDisplayFbInterface(ExynosDisplay *exynosDisplay)
    : ExynosDisplayFbInterface(exynosDisplay)
{
}

void ExynosPrimaryDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mDisplayFd = open(DECON_PRIMARY_DEV_NAME, O_RDWR);
    if (mDisplayFd < 0)
        ALOGE("%s:: failed to open framebuffer", __func__);

    mExynosDisplay = exynosDisplay;
    mPrimaryDisplay = (ExynosPrimaryDisplay *)exynosDisplay;

    getDisplayHWInfo();
}

int32_t ExynosPrimaryDisplayFbInterface::setPowerMode(int32_t mode)
{
    int fb_blank = -1;
    if (mode == HWC_POWER_MODE_DOZE ||
        mode == HWC_POWER_MODE_DOZE_SUSPEND) {
        if (mPrimaryDisplay->mPowerModeState != HWC_POWER_MODE_DOZE &&
            mPrimaryDisplay->mPowerModeState != HWC_POWER_MODE_OFF &&
            mPrimaryDisplay->mPowerModeState != HWC_POWER_MODE_DOZE_SUSPEND) {
            fb_blank = FB_BLANK_POWERDOWN;
        }
    } else if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    int32_t ret = NO_ERROR;
    if (fb_blank >= 0) {
        if ((ret = ioctl(mDisplayFd, FBIOBLANK, fb_blank)) < 0) {
            ALOGE("FB BLANK ioctl failed errno : %d", errno);
            return ret;
        }
    }

    if ((ret = ioctl(mDisplayFd, S3CFB_POWER_MODE, &mode)) < 0) {
        ALOGE("Need to check S3CFB power mode ioctl : %d", errno);
        return ret;
    }

    return 0;
}

void ExynosPrimaryDisplayFbInterface::getDisplayHWInfo() {

    int refreshRate = 0;

    /* get PSR info */
    FILE *psrInfoFd;
    int psrMode;
    int panelType;
    uint64_t refreshCalcFactor = 0;

    /* Get screen info from Display DD */
    struct fb_var_screeninfo info;

    if (ioctl(mDisplayFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("FBIOGET_VSCREENINFO ioctl failed: %s", strerror(errno));
        return;
    }

    if (info.reserved[0] == 0 && info.reserved[1] == 0) {
        info.reserved[0] = info.xres;
        info.reserved[1] = info.yres;

        if (ioctl(mDisplayFd, FBIOPUT_VSCREENINFO, &info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO ioctl failed: %s", strerror(errno));
            return;
        }
    }

    struct decon_disp_info disp_info;
    disp_info.ver = HWC_2_0;

    if (ioctl(mDisplayFd, EXYNOS_DISP_INFO, &disp_info) == -1) {
        ALOGI("EXYNOS_DISP_INFO ioctl failed: %s", strerror(errno));
        return;
    } else {
        ALOGI("HWC2: %d, psr_mode : %d", disp_info.ver, disp_info.psr_mode);
    }

    mPrimaryDisplay->mXres = info.reserved[0];
    mPrimaryDisplay->mYres = info.reserved[1];

    /* Support Multi-resolution scheme */
    {
        mPrimaryDisplay->mDeviceXres = mPrimaryDisplay->mXres;
        mPrimaryDisplay->mDeviceYres = mPrimaryDisplay->mYres;
        mPrimaryDisplay->mNewScaledWidth = mPrimaryDisplay->mXres;
        mPrimaryDisplay->mNewScaledHeight = mPrimaryDisplay->mYres;

        mPrimaryDisplay->mResolutionInfo.nNum = 1;
        mPrimaryDisplay->mResolutionInfo.nResolution[0].w = 1440;
        mPrimaryDisplay->mResolutionInfo.nResolution[0].h = 2960;
    }
    refreshCalcFactor = uint64_t( info.upper_margin + info.lower_margin + mPrimaryDisplay->mYres + info.vsync_len )
                        * ( info.left_margin  + info.right_margin + mPrimaryDisplay->mXres + info.hsync_len )
                        * info.pixclock;

    if (refreshCalcFactor)
        refreshRate = 1000000000000LLU / refreshCalcFactor;

    if (refreshRate == 0) {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60;
    }

    mPrimaryDisplay->mXdpi = 1000 * (mPrimaryDisplay->mXres * 25.4f) / info.width;
    mPrimaryDisplay->mYdpi = 1000 * (mPrimaryDisplay->mYres * 25.4f) / info.height;
    mPrimaryDisplay->mVsyncPeriod = 1000000000 / refreshRate;

    ALOGD("using\n"
            "xres         = %d px\n"
            "yres         = %d px\n"
            "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %d Hz\n",
            mPrimaryDisplay->mXres, mPrimaryDisplay->mYres, info.width,
            mPrimaryDisplay->mXdpi / 1000.0,
            info.height, mPrimaryDisplay->mYdpi / 1000.0, refreshRate);

    /* get PSR info */
    psrInfoFd = NULL;
    mPrimaryDisplay->mPsrMode = psrMode = PSR_MAX;
    panelType = PANEL_LEGACY;

    char devname[MAX_DEV_NAME + 1];
    devname[MAX_DEV_NAME] = '\0';

    strncpy(devname, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
    strlcat(devname, PSR_DEV_NAME, MAX_DEV_NAME);

    char psrDevname[MAX_DEV_NAME + 1];
    memset(psrDevname, 0, MAX_DEV_NAME + 1);

    strncpy(psrDevname, devname, strlen(devname) - 5);
    strlcat(psrDevname, "psr_info", MAX_DEV_NAME);
    ALOGI("PSR info devname = %s\n", psrDevname);

    psrInfoFd = fopen(psrDevname, "r");

    if (psrInfoFd == NULL) {
        ALOGW("HWC needs to know whether LCD driver is using PSR mode or not\n");
        devname[strlen(VSYNC_DEV_PREFIX)] = '\0';
        strlcat(devname, VSYNC_DEV_MIDDLE, MAX_DEV_NAME);
        strlcat(devname, PSR_DEV_NAME, MAX_DEV_NAME);
        ALOGI("Retrying with %s", devname);
        psrInfoFd = fopen(devname, "r");
    }

    if (psrInfoFd != NULL) {
        char val[4];
        if (fread(&val, 1, 1, psrInfoFd) == 1) {
            mPrimaryDisplay->mPsrMode = psrMode = (0x03 & atoi(val));
        }
    } else {
        ALOGW("HWC needs to know whether LCD driver is using PSR mode or not (2nd try)\n");
    }

    ALOGI("PSR mode   = %d (0: video mode, 1: DP PSR mode, 2: MIPI-DSI command mode)\n",
            psrMode);

    if (psrInfoFd != NULL) {
        /* get DSC info */
        if (exynosHWCControl.multiResolution == true) {
            uint32_t panelModeCnt = 1;
            uint32_t sliceXSize = mPrimaryDisplay->mXres;
            uint32_t sliceYSize = mPrimaryDisplay->mYres;
            uint32_t xSize = mPrimaryDisplay->mXres;
            uint32_t ySize = mPrimaryDisplay->mYres;
            uint32_t panelType = PANEL_LEGACY;
            const int mode_limit = 3;
            ResolutionInfo &resolutionInfo = mPrimaryDisplay->mResolutionInfo;

            if (fscanf(psrInfoFd, "%u\n", &panelModeCnt) != 1) {
                ALOGE("Fail to read panel mode count");
            } else {
                ALOGI("res count : %u", panelModeCnt);
                if (panelModeCnt <= mode_limit) {
                    resolutionInfo.nNum = panelModeCnt;
                    for(uint32_t i = 0; i < panelModeCnt; i++) {
                        if (fscanf(psrInfoFd, "%d\n%d\n%d\n%d\n%d\n", &xSize, &ySize, &sliceXSize, &sliceYSize, &panelType) < 0) {
                            ALOGE("Fail to read slice information");
                        } else {
                            resolutionInfo.nResolution[i].w = xSize;
                            resolutionInfo.nResolution[i].h = ySize;
                            resolutionInfo.nDSCXSliceSize[i] = sliceXSize;
                            resolutionInfo.nDSCYSliceSize[i] = sliceYSize;
                            resolutionInfo.nPanelType[i] = panelType;
                            ALOGI("mode no. : %d, Width : %d, Height : %d, X_Slice_Size : %d, Y_Slice_Size : %d, Panel type : %d\n", i,
                                    resolutionInfo.nResolution[i].w,
                                    resolutionInfo.nResolution[i].h,
                                    resolutionInfo.nDSCXSliceSize[i],
                                    resolutionInfo.nDSCYSliceSize[i],
                                    resolutionInfo.nPanelType[i]);
                        }
                    }
                }
                mPrimaryDisplay->mDSCHSliceNum = mPrimaryDisplay->mXres / resolutionInfo.nDSCXSliceSize[0];
                mPrimaryDisplay->mDSCYSliceSize = resolutionInfo.nDSCYSliceSize[0];
            }
        } else {
            uint32_t sliceNum = 1;
            uint32_t sliceSize = mPrimaryDisplay->mYres;
            if (fscanf(psrInfoFd, "\n%d\n%d\n", &sliceNum, &sliceSize) < 0) {
                ALOGE("Fail to read slice information");
            } else {
                mPrimaryDisplay->mDSCHSliceNum = sliceNum;
                mPrimaryDisplay->mDSCYSliceSize = sliceSize;
            }
        }
        fclose(psrInfoFd);
    }

    mPrimaryDisplay->mDRDefault = (mPrimaryDisplay->mPsrMode == PSR_NONE);
    mPrimaryDisplay->mDREnable = mPrimaryDisplay->mDRDefault;

    ALOGI("DSC H_Slice_Num: %d, Y_Slice_Size: %d (for window partial update)",
            mPrimaryDisplay->mDSCHSliceNum, mPrimaryDisplay->mDSCYSliceSize);

    struct decon_hdr_capabilities_info outInfo;
    memset(&outInfo, 0, sizeof(outInfo));

    if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
        ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
        return;
    }


    // Save to member variables
    mPrimaryDisplay->mHdrTypeNum = outInfo.out_num;
    mPrimaryDisplay->mMaxLuminance = (float)outInfo.max_luminance / (float)10000;
    mPrimaryDisplay->mMaxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
    mPrimaryDisplay->mMinLuminance = (float)outInfo.min_luminance / (float)10000;

    ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f)",
            mPrimaryDisplay->mDisplayName.string(), mPrimaryDisplay->mHdrTypeNum,
            mPrimaryDisplay->mMaxLuminance, mPrimaryDisplay->mMaxAverageLuminance,
            mPrimaryDisplay->mMinLuminance);

    struct decon_hdr_capabilities outData;

    for (int i = 0; i < mPrimaryDisplay->mHdrTypeNum; i += SET_HDR_CAPABILITIES_NUM) {
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES, &outData) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES ioctl Failed");
            return;
        }
        mPrimaryDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        ALOGE("HWC2: Type(%d)",  mPrimaryDisplay->mHdrTypes[i]);
    }

    //TODO : shuld be set by valid number
    //mHdrTypes[0] = HAL_HDR_HDR10;

    return;
}

//////////////////////////////////////////////////// ExynosExternalDisplayFbInterface //////////////////////////////////////////////////////////////////

bool is_same_dv_timings(const struct v4l2_dv_timings *t1,
        const struct v4l2_dv_timings *t2)
{
    if (t1->type == t2->type &&
            t1->bt.width == t2->bt.width &&
            t1->bt.height == t2->bt.height &&
            t1->bt.interlaced == t2->bt.interlaced &&
            t1->bt.polarities == t2->bt.polarities &&
            t1->bt.pixelclock == t2->bt.pixelclock &&
            t1->bt.hfrontporch == t2->bt.hfrontporch &&
            t1->bt.vfrontporch == t2->bt.vfrontporch &&
            t1->bt.vsync == t2->bt.vsync &&
            t1->bt.vbackporch == t2->bt.vbackporch &&
            (!t1->bt.interlaced ||
             (t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
              t1->bt.il_vsync == t2->bt.il_vsync &&
              t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
        return true;
    return false;
}

int ExynosExternalDisplay::getDVTimingsIndex(int preset)
{
    for (int i = 0; i < SUPPORTED_DV_TIMINGS_NUM; i++) {
        if (preset == preset_index_mappings[i].preset)
            return preset_index_mappings[i].dv_timings_index;
    }
    return -1;
}

ExynosExternalDisplayFbInterface::ExynosExternalDisplayFbInterface(ExynosDisplay *exynosDisplay)
    : ExynosDisplayFbInterface(exynosDisplay)
{
}

void ExynosExternalDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mDisplayFd = open(DECON_EXTERNAL_DEV_NAME, O_RDWR);
    if (mDisplayFd < 0)
        ALOGE("%s:: failed to open framebuffer", __func__);

    mExynosDisplay = exynosDisplay;
    mExternalDisplay = (ExynosExternalDisplay *)exynosDisplay;

    memset(&mDVTimings, 0, sizeof(mDVTimings));
}

int32_t ExynosExternalDisplayFbInterface::getDisplayAttribute(
        hwc2_config_t config,
        int32_t attribute, int32_t* outValue)
{
    if (config >= SUPPORTED_DV_TIMINGS_NUM) {
        HWC_LOGE(mExternalDisplay, "%s:: Invalid config(%d), mConfigurations(%zu)", __func__, config, mConfigurations.size());
        return -EINVAL;
    }

    v4l2_dv_timings dv_timing = mDVTimings[config];
    switch(attribute) {
    case HWC2_ATTRIBUTE_VSYNC_PERIOD:
        {
            *outValue = calVsyncPeriod(dv_timing);
            break;
        }
    case HWC2_ATTRIBUTE_WIDTH:
        *outValue = dv_timing.bt.width;
        break;

    case HWC2_ATTRIBUTE_HEIGHT:
        *outValue = dv_timing.bt.height;
        break;

    case HWC2_ATTRIBUTE_DPI_X:
        *outValue = mExternalDisplay->mXdpi;
        break;

    case HWC2_ATTRIBUTE_DPI_Y:
        *outValue = mExternalDisplay->mYdpi;
        break;

    default:
        HWC_LOGE(mExternalDisplay, "%s unknown display attribute %u",
                mExternalDisplay->mDisplayName.string(), attribute);
        return HWC2_ERROR_BAD_CONFIG;
    }

    return HWC2_ERROR_NONE;
}


int32_t ExynosExternalDisplayFbInterface::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs)
{
    int ret = 0;
    exynos_displayport_data dp_data;

    if (outConfigs != NULL) {
        if (mConfigurations.size() != *outNumConfigs){
            HWC_LOGE(mExternalDisplay, "%s outNumConfigs(%d) is different with the number of configurations(%zu)",
                    mExternalDisplay->mDisplayName.string(), *outNumConfigs, mConfigurations.size());
            return -1;
        }

        for (size_t index = 0; index < *outNumConfigs; index++) {
            outConfigs[index] = mConfigurations[index];
        }

        dp_data.timings = mDVTimings[outConfigs[0]];
        dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_PRESET;
        if(ioctl(mDisplayFd, EXYNOS_SET_DISPLAYPORT_CONFIG, &dp_data) <0) {
            HWC_LOGE(mExternalDisplay, "%s fail to send selected config data, %d",
                    mExternalDisplay->mDisplayName.string(), errno);
            return -1;
        }

        mExternalDisplay->mXres = mDVTimings[outConfigs[0]].bt.width;
        mExternalDisplay->mYres = mDVTimings[outConfigs[0]].bt.height;
        mExternalDisplay->mVsyncPeriod = calVsyncPeriod(mDVTimings[outConfigs[0]]);
        HDEBUGLOGD(eDebugExternalDisplay, "ExternalDisplay is connected to (%d x %d, %d fps) sink",
                mExternalDisplay->mXres, mExternalDisplay->mYres, mExternalDisplay->mVsyncPeriod);

        dumpDisplayConfigs();

        return HWC2_ERROR_NONE;
    }

    memset(&mDVTimings, 0, sizeof(mDVTimings));
    cleanConfigurations();

    /* configs store the index of mConfigurations */
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_ENUM_PRESET;
    for (size_t index = 0; index < SUPPORTED_DV_TIMINGS_NUM; index++) {
        dp_data.etimings.index = index;
        ret = ioctl(mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data);
        if (ret < 0) {
            if (errno == EINVAL) {
                HDEBUGLOGD(eDebugExternalDisplay, "%s:: Unmatched config index %zu", __func__, index);
                continue;
            }
            else if (errno == E2BIG) {
                HDEBUGLOGD(eDebugExternalDisplay, "%s:: Total configurations %zu", __func__, index);
                break;
            }
            HWC_LOGE(mExternalDisplay, "%s: enum_dv_timings error, %d", __func__, errno);
            return -1;
        }

        mDVTimings[index] = dp_data.etimings.timings;
        mConfigurations.push_back(index);
    }

    if (mConfigurations.size() == 0){
        HWC_LOGE(mExternalDisplay, "%s do not receivce any configuration info",
                mExternalDisplay->mDisplayName.string());
        mExternalDisplay->closeExternalDisplay();
        return -1;
    }

    int config = 0;
    v4l2_dv_timings temp_dv_timings = mDVTimings[mConfigurations[mConfigurations.size()-1]];
    for (config = 0; config < (int)mConfigurations[mConfigurations.size()-1]; config++) {
        if (mDVTimings[config].bt.width != 0) {
            mDVTimings[mConfigurations[mConfigurations.size()-1]] = mDVTimings[config];
            break;
        }
    }

    mDVTimings[config] = temp_dv_timings;
    mExternalDisplay->mActiveConfigIndex = config;

    *outNumConfigs = mConfigurations.size();

    return 0;
}

void ExynosExternalDisplayFbInterface::cleanConfigurations()
{
    mConfigurations.clear();
}

void ExynosExternalDisplayFbInterface::dumpDisplayConfigs()
{
    HDEBUGLOGD(eDebugExternalDisplay, "External display configurations:: total(%zu), active configuration(%d)",
            mConfigurations.size(), mExternalDisplay->mActiveConfigIndex);

    for (size_t i = 0; i <  mConfigurations.size(); i++ ) {
        unsigned int dv_timings_index = mConfigurations[i];
        v4l2_dv_timings configuration = mDVTimings[dv_timings_index];
        float refresh_rate = (float)((float)configuration.bt.pixelclock /
                ((configuration.bt.width + configuration.bt.hfrontporch + configuration.bt.hsync + configuration.bt.hbackporch) *
                 (configuration.bt.height + configuration.bt.vfrontporch + configuration.bt.vsync + configuration.bt.vbackporch)));
        uint32_t vsyncPeriod = 1000000000 / refresh_rate;
        HDEBUGLOGD(eDebugExternalDisplay, "%zu : index(%d) type(%d), %d x %d, fps(%f), vsyncPeriod(%d)", i, dv_timings_index, configuration.type, configuration.bt.width,
                configuration.bt.height,
                refresh_rate, vsyncPeriod);
    }
}

int32_t ExynosExternalDisplayFbInterface::calVsyncPeriod(v4l2_dv_timings dv_timing)
{
    int32_t result;
    float refreshRate = (float)((float)dv_timing.bt.pixelclock /
            ((dv_timing.bt.width + dv_timing.bt.hfrontporch + dv_timing.bt.hsync + dv_timing.bt.hbackporch) *
             (dv_timing.bt.height + dv_timing.bt.vfrontporch + dv_timing.bt.vsync + dv_timing.bt.vbackporch)));

    result = (1000000000/refreshRate);
    return result;
}

int32_t ExynosExternalDisplayFbInterface::getHdrCapabilities(
        uint32_t* outNumTypes, int32_t* outTypes, float* outMaxLuminance,
        float* outMaxAverageLuminance, float* outMinLuminance)
{
    HDEBUGLOGD(eDebugExternalDisplay, "HWC2: %s, %d", __func__, __LINE__);
    if (outTypes == NULL) {
        struct decon_hdr_capabilities_info outInfo;
        memset(&outInfo, 0, sizeof(outInfo));

        exynos_displayport_data dp_data;
        memset(&dp_data, 0, sizeof(dp_data));
        dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_HDR_INFO;
        int ret = ioctl(mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data);
        if (ret < 0) {
            ALOGE("%s: EXYNOS_DISPLAYPORT_STATE_HDR_INFO ioctl error, %d", __func__, errno);
        }

        mExternalDisplay->mExternalHdrSupported = dp_data.hdr_support;
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
            return -1;
        }

        if (mExternalDisplay->mExternalHdrSupported) {
            *outMaxLuminance = 50 * pow(2.0 ,(double)outInfo.max_luminance / 32);
            *outMaxAverageLuminance = 50 * pow(2.0 ,(double)outInfo.max_average_luminance / 32);
            *outMinLuminance = *outMaxLuminance * (float)pow(outInfo.min_luminance, 2.0) / pow(255.0, 2.0) / (float)100;
        }
        else {
            *outMaxLuminance = (float)outInfo.max_luminance / (float)10000;
            *outMaxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
            *outMinLuminance = (float)outInfo.min_luminance / (float)10000;
        }

#ifndef USES_HDR_GLES_CONVERSION
        mExternalDisplay->mExternalHdrSupported = 0;
#endif

        *outNumTypes = outInfo.out_num;
        // Save to member variables
        mExternalDisplay->mHdrTypeNum = *outNumTypes;
        mExternalDisplay->mMaxLuminance = *outMaxLuminance;
        mExternalDisplay->mMaxAverageLuminance = *outMaxAverageLuminance;
        mExternalDisplay->mMinLuminance = *outMinLuminance;
        ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f), externalHdrSupported(%d)",
                mExternalDisplay->mDisplayName.string(), mExternalDisplay->mHdrTypeNum,
                mExternalDisplay->mMaxLuminance, mExternalDisplay->mMaxAverageLuminance,
                mExternalDisplay->mMinLuminance, mExternalDisplay->mExternalHdrSupported);
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
        mExternalDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        HDEBUGLOGD(eDebugExternalDisplay, "HWC2: Types : %d", mExternalDisplay->mHdrTypes[i]);
    }
    return 0;
}

android_dataspace ExynosDisplayFbInterface::dataspaceFromConfig(const exynos_win_config_data &config)
{
    if (config.dataspace == HAL_DATASPACE_UNKNOWN) {
        if (isFormatRgb(config.format))
            return HAL_DATASPACE_V0_SRGB;
    }

    return config.dataspace;
}
