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

extern struct exynos_hwc_control exynosHWCControl;

ExynosPrimaryDisplay::ExynosPrimaryDisplay(uint32_t __unused type, ExynosDevice *device)
    :   ExynosDisplay(HWC_DISPLAY_PRIMARY, device)
{
    // TODO : Hard coded here
    mNumMaxPriorityAllowed = 5;

    /* Initialization */
    this->mDisplayId = HWC_DISPLAY_PRIMARY;
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
}

ExynosPrimaryDisplay::~ExynosPrimaryDisplay()
{
}

void ExynosPrimaryDisplay::ExynosPrimaryDisplayFbInterface::getDisplayHWInfo() {

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
        goto err_ioctl;
    }

    if (info.reserved[0] == 0 && info.reserved[1] == 0) {
        info.reserved[0] = info.xres;
        info.reserved[1] = info.yres;

        if (ioctl(mDisplayFd, FBIOPUT_VSCREENINFO, &info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO ioctl failed: %s", strerror(errno));
            goto err_ioctl;
        }
    }

    struct decon_disp_info disp_info;
    disp_info.ver = HWC_2_0;

    if (ioctl(mDisplayFd, EXYNOS_DISP_INFO, &disp_info) == -1) {
        ALOGI("EXYNOS_DISP_INFO ioctl failed: %s", strerror(errno));
        goto err_ioctl;
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
        goto err_ioctl;
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
            goto err_ioctl;
        }
        mPrimaryDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        ALOGE("HWC2: Type(%d)",  mPrimaryDisplay->mHdrTypes[i]);
    }

    //TODO : shuld be set by valid number
    //mHdrTypes[0] = HAL_HDR_HDR10;

    return;

err_ioctl:
    return;
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

int32_t ExynosPrimaryDisplay::setPowerMode(
        int32_t /*hwc2_power_mode_t*/ mode) {
    Mutex::Autolock lock(mDisplayMutex);

    /* TODO state check routine should be added */
    int fb_blank = -1;

    if (mode == HWC_POWER_MODE_DOZE ||
        mode == HWC_POWER_MODE_DOZE_SUSPEND) {
        if (this->mPowerModeState != HWC_POWER_MODE_DOZE &&
            this->mPowerModeState != HWC_POWER_MODE_OFF &&
            this->mPowerModeState != HWC_POWER_MODE_DOZE_SUSPEND) {
            fb_blank = FB_BLANK_POWERDOWN;
            clearDisplay();
        } else {
            ALOGE("DOZE or Power off called twice, mPowerModeState : %d", this->mPowerModeState);
        }
    } else if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
        clearDisplay();
        ALOGV("HWC2: Clear display (power off)");
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    ALOGD("%s:: FBIOBLANK mode(%d), blank(%d)", __func__, mode, fb_blank);

    if (fb_blank == FB_BLANK_POWERDOWN)
        mDREnable = false;
    else if (fb_blank == FB_BLANK_UNBLANK)
        mDREnable = mDRDefault;

    // check the dynamic recomposition thread by following display
    mDevice->checkDynamicRecompositionThread();

    mDisplayInterface->setPowerMode(mode);
    this->mPowerModeState = (hwc2_power_mode_t)mode;

    ALOGD("%s:: S3CFB_POWER_MODE mode(%d), blank(%d)", __func__, mode, fb_blank);

    if (mode == HWC_POWER_MODE_OFF) {
        /* It should be called from validate() when the screen is on */
        mSkipFrame = true;
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_OFF);
        if ((mRenderingState >= RENDERING_STATE_VALIDATED) &&
            (mRenderingState < RENDERING_STATE_PRESENTED))
            closeFencesForSkipFrame(RENDERING_STATE_VALIDATED);
        mRenderingState = RENDERING_STATE_NONE;
    } else {
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_ON);
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosPrimaryDisplay::ExynosPrimaryDisplayFbInterface::setPowerMode(int32_t mode)
{
    int32_t ret = NO_ERROR;
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

    return ret;
}

ExynosMPP* ExynosPrimaryDisplay::getExynosMPPForDma(decon_idma_type idma) {
    return ExynosDisplay::getExynosMPPForDma(idma);
}
decon_idma_type ExynosPrimaryDisplay::getDeconDMAType(ExynosMPP *otfMPP) {
    return ExynosDisplay::getDeconDMAType(otfMPP);
}

bool ExynosPrimaryDisplay::getHDRException(ExynosLayer* __unused layer)
{
    return false;
}

void ExynosPrimaryDisplay::initDisplayInterface(uint32_t __unused interfaceType)
{
    mDisplayInterface = new ExynosPrimaryDisplayFbInterface((ExynosDisplay *)this);
    mDisplayInterface->init(this);
}

ExynosPrimaryDisplay::ExynosPrimaryDisplayFbInterface::ExynosPrimaryDisplayFbInterface(ExynosDisplay *exynosDisplay)
    : ExynosDisplayFbInterface(exynosDisplay)
{
}

void ExynosPrimaryDisplay::ExynosPrimaryDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mDisplayFd = open(DECON_PRIMARY_DEV_NAME, O_RDWR);
    if (mDisplayFd < 0)
        ALOGE("%s:: failed to open framebuffer", __func__);

    mExynosDisplay = exynosDisplay;
    mPrimaryDisplay = (ExynosPrimaryDisplay *)exynosDisplay;

    getDisplayHWInfo();
}
