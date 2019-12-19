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

#include <sys/types.h>
#include "ExynosDisplayDrmInterface.h"
#include "ExynosHWCDebug.h"
#include <drm/drm_fourcc.h>

constexpr uint32_t MAX_PLANE_NUM = 3;
constexpr uint32_t CBCR_INDEX = 1;
constexpr float DISPLAY_LUMINANCE_UNIT = 10000;

typedef struct _drmModeAtomicReqItem drmModeAtomicReqItem, *drmModeAtomicReqItemPtr;

struct _drmModeAtomicReqItem {
    uint32_t object_id;
    uint32_t property_id;
    uint64_t value;
};

struct _drmModeAtomicReq {
    uint32_t cursor;
    uint32_t size_items;
    drmModeAtomicReqItemPtr items;
};

extern struct exynos_hwc_control exynosHWCControl;
static const int32_t kUmPerInch = 25400;
ExynosDisplayDrmInterface::ExynosDisplayDrmInterface(ExynosDisplay *exynosDisplay)
{
    mType = INTERFACE_TYPE_DRM;
    init(exynosDisplay);
}

ExynosDisplayDrmInterface::~ExynosDisplayDrmInterface()
{
    if (mModeState.blob_id)
        mDrmDevice->DestroyPropertyBlob(mModeState.blob_id);
    if (mModeState.old_blob_id)
        mDrmDevice->DestroyPropertyBlob(mModeState.old_blob_id);
}

void ExynosDisplayDrmInterface::init(ExynosDisplay *exynosDisplay)
{
    mExynosDisplay = exynosDisplay;
    mDrmDevice = NULL;
    mDrmCrtc = NULL;
    mDrmConnector = NULL;
    mActiveConfig = -1;
}

void ExynosDisplayDrmInterface::initDrmDevice(DrmDevice *drmDevice)
{
    if (mExynosDisplay == NULL) {
        ALOGE("mExynosDisplay is not set");
        return;
    }
    if ((mDrmDevice = drmDevice) == NULL) {
        ALOGE("drmDevice is NULL");
        return;
    }
    if ((mDrmCrtc = mDrmDevice->GetCrtcForDisplay(mExynosDisplay->mDisplayId)) == NULL) {
        ALOGE("%s:: GetCrtcForDisplay is NULL", mExynosDisplay->mDisplayName.string());
        return;
    }
    if ((mDrmConnector = mDrmDevice->GetConnectorForDisplay(mExynosDisplay->mDisplayId)) == NULL) {
        ALOGE("%s:: GetConnectorForDisplay is NULL", mExynosDisplay->mDisplayName.string());
        return;
    }

    /* TODO: We should map plane to ExynosMPP */
#if 0
    for (auto &plane : mDrmDevice->planes()) {
        uint32_t plane_id = plane->id();
        ExynosMPP *exynosMPP =
            mExynosDisplay->mResourceManager->getOtfMPPWithChannel(plane_id);
        if (exynosMPP == NULL)
            HWC_LOGE(mExynosDisplay, "getOtfMPPWithChannel fail, ch(%d)", plane_id);
        mExynosMPPsForPlane[plane_id] = exynosMPP;
    }
#endif

    if (mExynosDisplay->mMaxWindowNum != getMaxWindowNum()) {
        ALOGE("%s:: Invalid max window number (mMaxWindowNum: %d, getMaxWindowNum(): %d",
                __func__, mExynosDisplay->mMaxWindowNum, getMaxWindowNum());
        return;
    }

    mOldFbIds.assign(getMaxWindowNum(), 0);

    mVsyncCallbak.init(mExynosDisplay->mDevice, mExynosDisplay);
    mDrmVSyncWorker.Init(mDrmDevice, mExynosDisplay->mDisplayId);
    mDrmVSyncWorker.RegisterCallback(std::shared_ptr<VsyncCallback>(&mVsyncCallbak));

    chosePreferredConfig();

    return;
}

void ExynosDisplayDrmInterface::ExynosVsyncCallback::init(
        ExynosDevice *exynosDevice, ExynosDisplay *exynosDisplay)
{
    mExynosDevice = exynosDevice;
    mExynosDisplay = exynosDisplay;
}

void ExynosDisplayDrmInterface::ExynosVsyncCallback::Callback(
        int display, int64_t timestamp)
{
    mExynosDevice->compareVsyncPeriod();
    if (mExynosDevice->mVsyncDisplay == (int)mExynosDisplay->mDisplayId) {
        hwc2_callback_data_t callbackData =
            mExynosDevice->mCallbackInfos[HWC2_CALLBACK_VSYNC].callbackData;
        HWC2_PFN_VSYNC callbackFunc =
            (HWC2_PFN_VSYNC)mExynosDevice->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer;
        if (callbackFunc != NULL)
            callbackFunc(callbackData, mExynosDisplay->mDisplayId, timestamp);
    }
}

int32_t ExynosDisplayDrmInterface::setPowerMode(int32_t mode)
{
    int ret = 0;
    uint64_t dpms_value = 0;
    if (mode == HWC_POWER_MODE_OFF) {
        dpms_value = DRM_MODE_DPMS_OFF;
    } else {
        dpms_value = DRM_MODE_DPMS_ON;
    }

    const DrmProperty &prop = mDrmConnector->dpms_property();
    if ((ret = drmModeConnectorSetProperty(mDrmDevice->fd(), mDrmConnector->id(), prop.id(),
            dpms_value)) != NO_ERROR) {
        HWC_LOGE(mExynosDisplay, "setPower mode ret (%d)", ret);
    }
    return ret;
}

int32_t ExynosDisplayDrmInterface::setVsyncEnabled(uint32_t enabled)
{
    mDrmVSyncWorker.VSyncControl(HWC2_VSYNC_ENABLE == enabled);
    return NO_ERROR;
}

int32_t ExynosDisplayDrmInterface::getDisplayAttribute(
        hwc2_config_t config,
        int32_t attribute, int32_t* outValue)
{
    auto mode = std::find_if(mDrmConnector->modes().begin(),
            mDrmConnector->modes().end(),
            [config](DrmMode const &m) {
            return m.id() == config;
            });
    if (mode == mDrmConnector->modes().end()) {
        ALOGE("Could not find active mode for %d", config);
        return HWC2_ERROR_BAD_CONFIG;
    }

    uint32_t mm_width = mDrmConnector->mm_width();
    uint32_t mm_height = mDrmConnector->mm_height();

    switch (attribute) {
        case HWC2_ATTRIBUTE_WIDTH:
            *outValue = mode->h_display();
            break;
        case HWC2_ATTRIBUTE_HEIGHT:
            *outValue = mode->v_display();
            break;
        case HWC2_ATTRIBUTE_VSYNC_PERIOD:
            // in nanoseconds
            *outValue = std::chrono::duration_cast<std::chrono::nanoseconds>((std::chrono::seconds)1).count() / mode->v_refresh();
            break;
        case HWC2_ATTRIBUTE_DPI_X:
            // Dots per 1000 inches
            *outValue = mm_width ? (mode->h_display() * kUmPerInch) / mm_width : -1;
            break;
        case HWC2_ATTRIBUTE_DPI_Y:
            // Dots per 1000 inches
            *outValue = mm_height ? (mode->v_display() * kUmPerInch) / mm_height : -1;
            break;
        default:
            *outValue = -1;
            return HWC2_ERROR_BAD_CONFIG;
    }
    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayDrmInterface::chosePreferredConfig()
{
    uint32_t num_configs = 0;
    int32_t err = getDisplayConfigs(&num_configs, NULL);
    if (err != HWC2_ERROR_NONE || !num_configs)
        return err;

    ALOGI("Preferred mode id: %d", mDrmConnector->get_preferred_mode_id());
    return setActiveConfig(mDrmConnector->get_preferred_mode_id());
}

int32_t ExynosDisplayDrmInterface::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs)
{
    if (!outConfigs) {
        int ret = mDrmConnector->UpdateModes();
        if (ret) {
            ALOGE("Failed to update display modes %d", ret);
            return HWC2_ERROR_BAD_DISPLAY;
        }
        dumpDisplayConfigs();
    }

    uint32_t num_modes = static_cast<uint32_t>(mDrmConnector->modes().size());
    if (!outConfigs) {
        *outNumConfigs = num_modes;
        return HWC2_ERROR_NONE;
    }

    uint32_t idx = 0;
    for (const DrmMode &mode : mDrmConnector->modes()) {
        if (idx >= *outNumConfigs)
            break;
        outConfigs[idx++] = mode.id();
    }
    *outNumConfigs = idx;

    return 0;
}

void ExynosDisplayDrmInterface::dumpDisplayConfigs()
{
    uint32_t num_modes = static_cast<uint32_t>(mDrmConnector->modes().size());
    for (uint32_t i = 0; i < num_modes; i++) {
        auto mode = mDrmConnector->modes().at(i);
        ALOGD("%s display config[%d] %s:: id(%d), clock(%d), flags(%d), type(%d)",
                mExynosDisplay->mDisplayName.string(), i, mode.name().c_str(), mode.id(), mode.clock(), mode.flags(), mode.type());
        ALOGD("\th_display(%d), h_sync_start(%d), h_sync_end(%d), h_total(%d), h_skew(%d)",
                mode.h_display(), mode.h_sync_start(), mode.h_sync_end(), mode.h_total(), mode.h_skew());
        ALOGD("\tv_display(%d), v_sync_start(%d), v_sync_end(%d), v_total(%d), v_scan(%d), v_refresh(%f)",
                mode.v_display(), mode.v_sync_start(), mode.v_sync_end(), mode.v_total(), mode.v_scan(), mode.v_refresh());

    }
}

int32_t ExynosDisplayDrmInterface::getColorModes(
        uint32_t* outNumModes,
        int32_t* outModes)
{
    *outNumModes = 1;

    if (outModes != NULL) {
        outModes[0] = HAL_COLOR_MODE_NATIVE;
    }
    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayDrmInterface::setColorMode(int32_t mode)
{
    return 0;
}

int32_t ExynosDisplayDrmInterface::setActiveConfig(hwc2_config_t config)
{
    ALOGI("%s:: %s config(%d)", __func__, mExynosDisplay->mDisplayName.string(), config);

    if (mActiveConfig == config) {
        ALOGI("%s:: Same display config is set", __func__);
        return NO_ERROR;
    }

    auto mode = std::find_if(mDrmConnector->modes().begin(),
            mDrmConnector->modes().end(),
            [config](DrmMode const &m) {
            return m.id() == config;
            });
    if (mode == mDrmConnector->modes().end()) {
        HWC_LOGE(mExynosDisplay, "Could not find active mode for %d", config);
        return HWC2_ERROR_BAD_CONFIG;
    }

    uint32_t mm_width = mDrmConnector->mm_width();
    uint32_t mm_height = mDrmConnector->mm_height();

    mActiveConfig = mode->id();
    mExynosDisplay->mXres = mode->h_display();
    mExynosDisplay->mYres = mode->v_display();
    // in nanoseconds
    mExynosDisplay->mVsyncPeriod = 1000 * 1000 * 1000 / mode->v_refresh();
    // Dots per 1000 inches
    mExynosDisplay->mXdpi = mm_width ? (mode->h_display() * kUmPerInch) / mm_width : -1;
    // Dots per 1000 inches
    mExynosDisplay->mYdpi = mm_height ? (mode->v_display() * kUmPerInch) / mm_height : -1;

    mModeState.mode = *mode;
    if (mModeState.blob_id)
        mDrmDevice->DestroyPropertyBlob(mModeState.blob_id);

    struct drm_mode_modeinfo drm_mode;
    memset(&drm_mode, 0, sizeof(drm_mode));
    mode->ToDrmModeModeInfo(&drm_mode);

    uint32_t id = 0;
    int ret = mDrmDevice->CreatePropertyBlob(&drm_mode, sizeof(struct drm_mode_modeinfo),
            &id);
    if (ret) {
        HWC_LOGE(mExynosDisplay, "Failed to create mode property blob %d", ret);
        return ret;
    }
    mModeState.blob_id = id;
    mModeState.needs_modeset = true;

    applyDisplayMode();
    return 0;
}

int32_t ExynosDisplayDrmInterface::applyDisplayMode()
{
    if (mModeState.needs_modeset == false)
        return NO_ERROR;

    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq(this);

    ret = drmModeAtomicAddProperty(drmReq.pset(), mDrmCrtc->id(),
           mDrmCrtc->active_property().id(), 1);
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to add crtc active to pset, ret(%d)",
                __func__, ret);
        return ret;
    }

    ret = drmModeAtomicAddProperty(drmReq.pset(), mDrmCrtc->id(),
            mDrmCrtc->mode_property().id(), mModeState.blob_id);
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to add mode %d to pset, ret(%d)",
                __func__, mModeState.blob_id, ret);
        return ret;
    }

    ret = drmModeAtomicAddProperty(drmReq.pset(), mDrmConnector->id(),
            mDrmConnector->crtc_id_property().id(), mDrmCrtc->id());
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to add crtc id %d to pset, ret(%d)",
                __func__, mDrmCrtc->id(), ret);
        return ret;
    }

    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    ret = drmModeAtomicCommit(mDrmDevice->fd(), drmReq.pset(), flags, mDrmDevice);
    if (ret) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to commit pset ret=%d in applyDisplayMode()\n",
                __func__, ret);
        return ret;
    }

    if (mModeState.old_blob_id) {
        ret = mDrmDevice->DestroyPropertyBlob(mModeState.old_blob_id);
        if (ret) {
            HWC_LOGE(mExynosDisplay, "Failed to destroy old mode property blob %" PRIu32 "/%d",
                    mModeState.old_blob_id, ret);
        }
    }
    mDrmConnector->set_active_mode(mModeState.mode);
    mModeState.old_blob_id = mModeState.blob_id;
    mModeState.blob_id = 0;
    mModeState.needs_modeset = false;
    return NO_ERROR;
}

int32_t ExynosDisplayDrmInterface::setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos)
{
    return 0;
}

int32_t ExynosDisplayDrmInterface::updateHdrCapabilities()
{
    /* Init member variables */
    mExynosDisplay->mHdrTypeNum = 0;
    mExynosDisplay->mMaxLuminance = 0;
    mExynosDisplay->mMaxAverageLuminance = 0;
    mExynosDisplay->mMinLuminance = 0;

    const DrmProperty &prop_max_luminance = mDrmConnector->max_luminance();
    const DrmProperty &prop_max_avg_luminance = mDrmConnector->max_avg_luminance();
    const DrmProperty &prop_min_luminance = mDrmConnector->min_luminance();
    const DrmProperty &prop_hdr_formats = mDrmConnector->hdr_formats();

    int ret = 0;
    uint64_t max_luminance = 0;
    uint64_t max_avg_luminance = 0;
    uint64_t min_luminance = 0;
    uint64_t hdr_formats = 0;

    if ((prop_max_luminance.id() == 0) ||
        (prop_max_avg_luminance.id() == 0) ||
        (prop_min_luminance.id() == 0) ||
        (prop_hdr_formats.id() == 0)) {
        ALOGE("%s:: there is no property for hdrCapabilities (max_luminance: %d, max_avg_luminance: %d, min_luminance: %d, hdr_formats: %d",
                __func__, prop_max_luminance.id(), prop_max_avg_luminance.id(),
                prop_min_luminance.id(), prop_hdr_formats.id());
        return -1;
    }

    std::tie(ret, max_luminance) = prop_max_luminance.value();
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: there is no max_luminance (ret = %d)",
                __func__, ret);
        return -1;
    }
    mExynosDisplay->mMaxLuminance = (float)max_luminance / DISPLAY_LUMINANCE_UNIT;

    std::tie(ret, max_avg_luminance) = prop_max_avg_luminance.value();
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: there is no max_avg_luminance (ret = %d)",
                __func__, ret);
        return -1;
    }
    mExynosDisplay->mMaxAverageLuminance = (float)max_avg_luminance / DISPLAY_LUMINANCE_UNIT;

    std::tie(ret, min_luminance) = prop_min_luminance.value();
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: there is no min_luminance (ret = %d)",
                __func__, ret);
        return -1;
    }
    mExynosDisplay->mMinLuminance = (float)min_luminance / DISPLAY_LUMINANCE_UNIT;

    std::tie(ret, hdr_formats) = prop_hdr_formats.value();
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: there is no hdr_formats (ret = %d)",
                __func__, ret);
        return -1;
    }

    uint32_t typeBit;
    std::tie(typeBit, ret) = prop_hdr_formats.GetEnumValueWithName("Dolby Vision");
    if ((ret == 0) && (hdr_formats & (1 << typeBit))) {
        mExynosDisplay->mHdrTypes[mExynosDisplay->mHdrTypeNum++] = HAL_HDR_DOLBY_VISION;
        HDEBUGLOGD(eDebugHWC, "%s: supported hdr types : %d",
                mExynosDisplay->mDisplayName.string(), HAL_HDR_DOLBY_VISION);
    }
    std::tie(typeBit, ret) = prop_hdr_formats.GetEnumValueWithName("HDR10");
    if ((ret == 0) && (hdr_formats & (1 << typeBit))) {
        mExynosDisplay->mHdrTypes[mExynosDisplay->mHdrTypeNum++] = HAL_HDR_HDR10;
        HDEBUGLOGD(eDebugHWC, "%s: supported hdr types : %d",
                mExynosDisplay->mDisplayName.string(), HAL_HDR_HDR10);
    }
    std::tie(typeBit, ret) = prop_hdr_formats.GetEnumValueWithName("HLG");
    if ((ret == 0) && (hdr_formats & (1 << typeBit))) {
        mExynosDisplay->mHdrTypes[mExynosDisplay->mHdrTypeNum++] = HAL_HDR_HLG;
        HDEBUGLOGD(eDebugHWC, "%s: supported hdr types : %d",
                mExynosDisplay->mDisplayName.string(), HAL_HDR_HLG);
    }

    ALOGI("%s: get hdrCapabilities info max_luminance(%" PRId64 "), "
            "max_avg_luminance(%" PRId64 "), min_luminance(%" PRId64 "), "
            "hdr_formats(0x%" PRIx64 ")",
            mExynosDisplay->mDisplayName.string(),
            max_luminance, max_avg_luminance, min_luminance, hdr_formats);

    ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f)",
            mExynosDisplay->mDisplayName.string(), mExynosDisplay->mHdrTypeNum, mExynosDisplay->mMaxLuminance,
            mExynosDisplay->mMaxAverageLuminance, mExynosDisplay->mMinLuminance);

    return 0;
}

int ExynosDisplayDrmInterface::getDeconChannel(ExynosMPP *otfMPP)
{
    int32_t channelNum = sizeof(IDMA_CHANNEL_MAP)/sizeof(dpp_channel_map_t);
    for (int i = 0; i < channelNum; i++) {
        if((IDMA_CHANNEL_MAP[i].type == otfMPP->mPhysicalType) &&
           (IDMA_CHANNEL_MAP[i].index == otfMPP->mPhysicalIndex))
            return IDMA_CHANNEL_MAP[i].channel;
    }
    return -EINVAL;
}

int32_t ExynosDisplayDrmInterface::addFBFromDisplayConfig(
        const exynos_win_config_data &config, uint32_t &fbId)
{
    int ret = NO_ERROR;
    int drmFormat = DRM_FORMAT_UNDEFINED;
    uint32_t bpp = 0;
    uint32_t pitches[HWC_DRM_BO_MAX_PLANES] = {0};
    uint32_t offsets[HWC_DRM_BO_MAX_PLANES] = {0};
    uint32_t buf_handles[HWC_DRM_BO_MAX_PLANES] = {0};
    uint64_t modifiers[HWC_DRM_BO_MAX_PLANES] = {0};
    uint32_t bufferNum, planeNum = 0;

    if (config.state == config.WIN_STATE_BUFFER) {
        drmFormat = halFormatToDrmFormat(config.format, config.compression);
        if (drmFormat == DRM_FORMAT_UNDEFINED) {
            HWC_LOGE(mExynosDisplay, "%s:: known drm format (%d)",
                    __func__, config.format);
            return -EINVAL;
        }

        bpp = getBytePerPixelOfPrimaryPlane(config.format);
        if ((bufferNum = getBufferNumOfFormat(config.format)) == 0) {
            HWC_LOGE(mExynosDisplay, "%s:: getBufferNumOfFormat(%d) error",
                    __func__, config.format);
            return -EINVAL;
        }
        if (((planeNum = getPlaneNumOfFormat(config.format)) == 0) ||
             (planeNum > MAX_PLANE_NUM)) {
            HWC_LOGE(mExynosDisplay, "%s:: getPlaneNumOfFormat(%d) error, planeNum(%d)",
                    __func__, config.format, planeNum);
            return -EINVAL;
        }
        for (uint32_t bufferIndex = 0; bufferIndex < bufferNum; bufferIndex++) {
            pitches[bufferIndex] = config.src.f_w * bpp;
            buf_handles[bufferIndex] = static_cast<uint32_t>(config.fd_idma[bufferIndex]);
        }
        if ((bufferNum == 1) && (planeNum > bufferNum)) {
            /* offset for cbcr */
            offsets[CBCR_INDEX] =
                getExynosBufferYLength(config.src.f_w, config.src.f_h, config.format);
            for (uint32_t planeIndex = 1; planeIndex < planeNum; planeIndex++)
            {
                buf_handles[planeIndex] = buf_handles[0];
                pitches[planeIndex] = pitches[0];
            }
        }

        if (config.compression)
            modifiers[0] = DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16);

        ret = drmModeAddFB2WithModifiers(mDrmDevice->fd(), config.src.f_w, config.src.f_h,
                drmFormat, buf_handles, pitches, offsets, modifiers, &fbId, modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);
    } else if (config.state == config.WIN_STATE_COLOR) {
        modifiers[0] = DRM_FORMAT_MOD_SAMSUNG_COLORMAP;
        drmFormat = DRM_FORMAT_BGRA8888;
        buf_handles[0] = config.color;
        bpp = getBytePerPixelOfPrimaryPlane(HAL_PIXEL_FORMAT_BGRA_8888);
        pitches[0] = config.dst.w * bpp;

        ret = drmModeAddFB2WithModifiers(mDrmDevice->fd(), config.dst.w, config.dst.h,
                drmFormat, buf_handles, pitches, offsets, modifiers, &fbId, modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);
    } else {
        HWC_LOGE(mExynosDisplay, "%s:: known config state(%d)",
                __func__, config.state);
        return -EINVAL;
    }
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to add FB, fb_id(%d), ret(%d), f_w: %d, f_h: %d, dst.w: %d, dst.h: %d, "
                "format: %d, buf_handles[%d, %d, %d, %d], "
                "pitches[%d, %d, %d, %d], offsets[%d, %d, %d, %d], modifiers[%#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 "]",
                __func__, fbId, ret,
                config.src.f_w, config.src.f_h, config.dst.w, config.dst.h, drmFormat,
                buf_handles[0], buf_handles[1], buf_handles[2], buf_handles[3],
                pitches[0], pitches[1], pitches[2], pitches[3],
                offsets[0], offsets[1], offsets[2], offsets[3],
                modifiers[0], modifiers[1], modifiers[2], modifiers[3]);
        return ret;
    }
    return NO_ERROR;

}

int32_t ExynosDisplayDrmInterface::setupCommitFromDisplayConfig(
        ExynosDisplayDrmInterface::DrmModeAtomicReq &drmReq,
        const exynos_win_config_data &config,
        const uint32_t configIndex,
        const std::unique_ptr<DrmPlane> &plane,
        uint32_t &fbId)
{
    int ret = NO_ERROR;

    if ((fbId == 0) && (ret = addFBFromDisplayConfig(config, fbId)) < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to add FB, fbId(%d), ret(%d)",
                __func__, fbId, ret);
        return ret;
    }

    if ((ret = drmReq.atomicAddProperty(plane->id(),
                plane->crtc_property(), mDrmCrtc->id())) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->fb_property(), fbId)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->crtc_x_property(), config.dst.x)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->crtc_y_property(), config.dst.y)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->crtc_w_property(), config.dst.w)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->crtc_h_property(), config.dst.h)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->src_x_property(), (int)(config.src.x) << 16)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->src_y_property(), (int)(config.src.y) << 16)) < 0)
        HWC_LOGE(mExynosDisplay, "%s:: Failed to add src_y property to plane",
                __func__);
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->src_w_property(), (int)(config.src.w) << 16)) < 0)
        return ret;
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->src_h_property(), (int)(config.src.h) << 16)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(plane->id(),
            plane->rotation_property(),
            halTransformToDrmRot(config.transform), true)) < 0)
        return ret;

    uint64_t blend = 0;
    if (plane->blend_property().id()) {
        switch (config.blending) {
            case HWC2_BLEND_MODE_PREMULTIPLIED:
                std::tie(blend, ret) = plane->blend_property().GetEnumValueWithName(
                        "Pre-multiplied");
                break;
            case HWC2_BLEND_MODE_COVERAGE:
                std::tie(blend, ret) = plane->blend_property().GetEnumValueWithName(
                        "Coverage");
                break;
            case HWC2_BLEND_MODE_NONE:
            default:
                std::tie(blend, ret) = plane->blend_property().GetEnumValueWithName(
                        "None");
                break;
        }
        if (ret) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to get blend for plane %d, blend(%" PRId64 "), ret(%d)",
                    __func__, plane->id(), blend, ret);
            return ret;
        }
        if ((ret = drmReq.atomicAddProperty(plane->id(),
                        plane->blend_property(), blend, true)) < 0)
            return ret;
    }

    if (plane->zpos_property().id() &&
        !plane->zpos_property().is_immutable()) {
        uint64_t min_zpos = 0;

        // Ignore ret and use min_zpos as 0 by default
        std::tie(std::ignore, min_zpos) = plane->zpos_property().range_min();

        if ((ret = drmReq.atomicAddProperty(plane->id(),
                plane->zpos_property(), configIndex + min_zpos)) < 0)
            return ret;
    }

    if (plane->alpha_property().id()) {
        uint64_t min_alpha = 0;
        uint64_t max_alpha = 0;
        std::tie(std::ignore, min_alpha) = plane->alpha_property().range_min();
        std::tie(std::ignore, max_alpha) = plane->alpha_property().range_max();
        if ((ret = drmReq.atomicAddProperty(plane->id(),
                plane->alpha_property(),
                (uint64_t)(((max_alpha - min_alpha) * config.plane_alpha) + 0.5) + min_alpha, true)) < 0)
            return ret;
    }

    if (config.acq_fence >= 0) {
        if ((ret = drmReq.atomicAddProperty(plane->id(),
                        plane->in_fence_fd_property(), config.acq_fence)) < 0)
            return ret;
    }

    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->standard_property(),
                    dataspaceToPlaneStandard(config.dataspace, *plane), true)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->transfer_property(),
                    dataspaceToPlaneTransfer(config.dataspace, *plane), true)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->range_property(),
                    dataspaceToPlaneRange(config.dataspace, *plane), true)) < 0)
        return ret;

    if (hasHdrInfo(config.dataspace)) {
        if ((ret = drmReq.atomicAddProperty(plane->id(),
                plane->min_luminance_property(), config.min_luminance)) < 0)
            return ret;
        if ((ret = drmReq.atomicAddProperty(plane->id(),
                       plane->max_luminance_property(), config.max_luminance)) < 0)
            return ret;
    }

    return NO_ERROR;
}

int32_t ExynosDisplayDrmInterface::deliverWinConfigData()
{
    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq(this);
    std::unordered_map<uint32_t, uint32_t> planeEnableInfo;
    std::vector<uint32_t> fbIds(getMaxWindowNum(), 0);
    android::String8 result;

    uint64_t out_fences[mDrmDevice->crtcs().size()];
    if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
                    mDrmCrtc->out_fence_ptr_property(),
                    (uint64_t)&out_fences[mDrmCrtc->pipe()], true)) < 0) {
        drmReq.setError(ret);
        return ret;
    }

    for (auto &plane : mDrmDevice->planes()) {
        planeEnableInfo[plane->id()] = 0;
    }

    for (size_t i = 0; i < mExynosDisplay->mDpuData.configs.size(); i++) {
        exynos_win_config_data& config = mExynosDisplay->mDpuData.configs[i];
        if ((config.state == config.WIN_STATE_BUFFER) ||
            (config.state == config.WIN_STATE_COLOR)) {
            int channelId = 0;
            if ((channelId = getDeconChannel(config.assignedMPP)) < 0) {
                HWC_LOGE(mExynosDisplay, "%s:: Failed to get channel id (%d)",
                        __func__, channelId);
                ret = -EINVAL;
                drmReq.setError(ret);
                return ret;
            }
            /* src size should be set even in dim layer */
            if (config.state == config.WIN_STATE_COLOR) {
                config.src.w = config.dst.w;
                config.src.h = config.dst.h;
            }
            auto &plane = mDrmDevice->planes().at(channelId);
            if ((ret = setupCommitFromDisplayConfig(drmReq, config, i, plane, fbIds[i])) < 0) {
                HWC_LOGE(mExynosDisplay, "setupCommitFromDisplayConfig failed, config[%zu]", i);
                drmReq.setError(ret);
                return ret;
            }
            /* Set this plane is enabled */
            planeEnableInfo[plane->id()] = 1;
        }
    }

    /* Disable unused plane */
    for (auto &plane : mDrmDevice->planes()) {
        if (planeEnableInfo[plane->id()] == 0) {
#if 0
            /* TODO: Check whether we can disable planes that are reserved to other dispaly */
            ExynosMPP* exynosMPP = mExynosMPPsForPlane[plane->id()];
            if ((exynosMPP != NULL) && (mExynosDisplay != NULL) &&
                (exynosMPP->mAssignedState & MPP_ASSIGN_STATE_RESERVED) &&
                (exynosMPP->mReservedDisplay != (int32_t)mExynosDisplay->mType))
                continue;
#endif
            if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->crtc_property(), 0)) < 0)
                return ret;

            if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->fb_property(), 0)) < 0)
                return ret;
        }
    }

    uint32_t flags = 0;
    ret = drmModeAtomicCommit(mDrmDevice->fd(), drmReq.pset(), flags, mDrmDevice);
    dumpAtomicCommitInfo(result, drmReq.pset(), true);

    for (size_t i = 0; i < getMaxWindowNum(); i++) {
        if (mOldFbIds[i])
            drmModeRmFB(mDrmDevice->fd(), mOldFbIds[i]);
        mOldFbIds[i] = fbIds[i];
    }

    if (ret) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to commit pset, ret(%d)", __func__, ret);
        drmReq.setError(ret);
        return ret;
    }

    mExynosDisplay->mDpuData.retire_fence = (int)out_fences[mDrmCrtc->pipe()];
    /*
     * [HACK] dup retire_fence for each layer's release fence
     * Do not use hwc_dup because hwc_dup increase usage count of fence treacer
     * Usage count of this fence is incresed by ExynosDisplay::deliverWinConfigData()
     */
    for (auto &display_config : mExynosDisplay->mDpuData.configs) {
        if ((display_config.state == display_config.WIN_STATE_BUFFER) ||
            (display_config.state == display_config.WIN_STATE_CURSOR)) {
            display_config.rel_fence =
                dup((int)out_fences[mDrmCrtc->pipe()]);
        }
    }

    return NO_ERROR;
}

int32_t ExynosDisplayDrmInterface::clearDisplay()
{
    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq(this);

    /* Disable all planes */
    for (auto &plane : mDrmDevice->planes()) {
#if 0
        /* TODO: Check whether we can disable planes that are reserved to other dispaly */
        ExynosMPP* exynosMPP = mExynosMPPsForPlane[plane->id()];
        /* Do not disable planes that are reserved to other dispaly */
        if ((exynosMPP != NULL) && (mExynosDisplay != NULL) &&
            (exynosMPP->mAssignedState & MPP_ASSIGN_STATE_RESERVED) &&
            (exynosMPP->mReservedDisplay != (int32_t)mExynosDisplay->mType))
            continue;
#endif
        ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                plane->crtc_property().id(), 0);
        if (ret < 0) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to disable plane %d, failed to add crtc ret(%d)",
                    __func__, plane->id(), ret);
            return ret;
        }
        ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                plane->fb_property().id(), 0);
        if (ret < 0) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to disable plane %d, failed to add fb ret(%d)",
                    __func__, plane->id(), ret);
            return ret;
        }
    }

    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    ret = drmModeAtomicCommit(mDrmDevice->fd(), drmReq.pset(), flags, mDrmDevice);
    if (ret) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to commit pset ret=%d in clearDisplay()\n",
                __func__, ret);
        return ret;
    }

    return NO_ERROR;
}

int32_t ExynosDisplayDrmInterface::disableSelfRefresh(uint32_t disable)
{
    return 0;
}

int32_t ExynosDisplayDrmInterface::setForcePanic()
{
    if (exynosHWCControl.forcePanic == 0)
        return NO_ERROR;

    usleep(20000000);

    FILE *forcePanicFd = fopen(HWC_FORCE_PANIC_PATH, "w");
    if (forcePanicFd == NULL) {
        ALOGW("%s:: Failed to open fd", __func__);
        return -1;
    }

    int val = 1;
    fwrite(&val, sizeof(int), 1, forcePanicFd);
    fclose(forcePanicFd);

    return 0;
}

String8& ExynosDisplayDrmInterface::dumpAtomicCommitInfo(String8 &result, drmModeAtomicReqPtr pset, bool debugPrint)
{
    /* print log only if eDebugDisplayInterfaceConfig flag is set when debugPrint is true */
    if (debugPrint &&
        (hwcCheckDebugMessages(eDebugDisplayInterfaceConfig) == false))
        return result;

    for (uint32_t i = 0; i < (uint32_t)drmModeAtomicGetCursor(pset); i++) {
        const DrmProperty *property = NULL;
        String8 objectName;
        /* Check crtc properties */
        if (pset->items[i].object_id == mDrmCrtc->id()) {
            for (auto property_ptr : mDrmCrtc->properties()) {
                if (pset->items[i].property_id == property_ptr->id()){
                    property = property_ptr;
                    objectName.appendFormat("Crtc");
                    break;
                }
            }
            if (property == NULL) {
                HWC_LOGE(mExynosDisplay, "%s:: object id is crtc but there is no matched property",
                        __func__);
            }
        } else if (pset->items[i].object_id == mDrmConnector->id()) {
            for (auto property_ptr : mDrmConnector->properties()) {
                if (pset->items[i].property_id == property_ptr->id()){
                    property = property_ptr;
                    objectName.appendFormat("Connector");
                    break;
                }
            }
            if (property == NULL) {
                HWC_LOGE(mExynosDisplay, "%s:: object id is connector but there is no matched property",
                        __func__);
            }
        } else {
            uint32_t channelId = 0;
            for (auto &plane : mDrmDevice->planes()) {
                if (pset->items[i].object_id == plane->id()) {
                    for (auto property_ptr : plane->properties()) {
                        if (pset->items[i].property_id == property_ptr->id()){
                            property = property_ptr;
                            objectName.appendFormat("Plane[%d]", channelId);
                            break;
                        }
                    }
                    if (property == NULL) {
                        HWC_LOGE(mExynosDisplay, "%s:: object id is plane but there is no matched property",
                                __func__);
                    }
                }
                channelId++;
            }
        }
        if (property == NULL) {
            HWC_LOGE(mExynosDisplay, "%s:: Fail to get property[%d] (object_id: %d, property_id: %d, value: %" PRId64 ")",
                    __func__, i, pset->items[i].object_id, pset->items[i].property_id,
                    pset->items[i].value);
            continue;
        }

        if (debugPrint)
            ALOGD("property[%d] %s object_id: %d, property_id: %d, name: %s,  value: %" PRId64 ")\n",
                    i, objectName.string(), pset->items[i].object_id, pset->items[i].property_id, property->name().c_str(), pset->items[i].value);
        else
            result.appendFormat("property[%d] %s object_id: %d, property_id: %d, name: %s,  value: %" PRId64 ")\n",
                i,  objectName.string(), pset->items[i].object_id, pset->items[i].property_id, property->name().c_str(), pset->items[i].value);
    }
    return result;
}

inline uint32_t ExynosDisplayDrmInterface::getMaxWindowNum()
{
        return mDrmDevice->planes().size();
}

ExynosDisplayDrmInterface::DrmModeAtomicReq::DrmModeAtomicReq(ExynosDisplayDrmInterface *displayInterface)
    : mDrmDisplayInterface(displayInterface)
{
    mPset = drmModeAtomicAlloc();
}

ExynosDisplayDrmInterface::DrmModeAtomicReq::~DrmModeAtomicReq()
{
    if ((mError != 0) && (mDrmDisplayInterface != NULL)) {
        android::String8 result;
        result.appendFormat("atomic commit error\n");
        mDrmDisplayInterface->dumpAtomicCommitInfo(result, mPset);
        HWC_LOGE(mDrmDisplayInterface->mExynosDisplay, "%s", result.string());
    }
    if(mPset)
        drmModeAtomicFree(mPset);
}

int32_t ExynosDisplayDrmInterface::DrmModeAtomicReq::atomicAddProperty(
        const uint32_t id,
        const DrmProperty &property,
        uint64_t value, bool optional)
{
    if (!optional && !property.id()) {
        HWC_LOGE(mDrmDisplayInterface->mExynosDisplay, "%s:: %s property id(%d) for id(%d) is not available",
                __func__, property.name().c_str(), property.id(), id);
        return -EINVAL;
    }

    if (property.id()) {
        int ret = drmModeAtomicAddProperty(mPset, id,
                property.id(), value);
        if (ret < 0) {
            HWC_LOGE(mDrmDisplayInterface->mExynosDisplay, "%s:: Failed to add property %d(%s) for id(%d), ret(%d)",
                    __func__, property.id(), property.name().c_str(), id, ret);
            return ret;
        }
    }

    return NO_ERROR;
}


uint32_t ExynosDisplayDrmInterface::getBytePerPixelOfPrimaryPlane(int format)
{
    if (isFormatRgb(format))
        return (formatToBpp(format)/8);
    else if (isFormat10BitYUV420(format))
        return 2;
    else if (isFormatYUV420(format))
        return 1;
    else
        return 0;
}

uint64_t ExynosDisplayDrmInterface::dataspaceToPlaneStandard(
        const android_dataspace dataspace, const DrmPlane& plane)
{
    int ret = NO_ERROR;
    uint32_t standard = dataspace & HAL_DATASPACE_STANDARD_MASK;
    uint64_t planeStandard = 0;
    switch(standard) {
        case HAL_DATASPACE_STANDARD_BT709:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT709");
            break;
        case HAL_DATASPACE_STANDARD_BT601_625:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT601_625");
            break;
        case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT601_625_UNADJUSTED");
            break;
        case HAL_DATASPACE_STANDARD_BT601_525:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT601_525");
            break;
        case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT601_525_UNADJUSTED");
            break;
        case HAL_DATASPACE_STANDARD_BT2020:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT2020");
            break;
        case HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT2020_CONSTANT_LUMINANCE");
            break;
        case HAL_DATASPACE_STANDARD_BT470M:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "BT470M");
            break;
        case HAL_DATASPACE_STANDARD_FILM:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "FILM");
            break;
        case HAL_DATASPACE_STANDARD_DCI_P3:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "DCI_P3");
            break;
        case HAL_DATASPACE_STANDARD_ADOBE_RGB:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "Adobe RGB");
            break;
        default:
            std::tie(planeStandard, ret) = plane.standard_property().GetEnumValueWithName(
                    "Unspecified");
            break;
    }
    if (ret) {
        HWC_LOGE(NULL, "failed to get plane standard, datapsace(0x%8x), standard(0x%8x), ret(%d)",
                dataspace, standard, ret);
    }
    return planeStandard;
}

uint64_t ExynosDisplayDrmInterface::dataspaceToPlaneTransfer(
        const android_dataspace dataspace, const DrmPlane& plane)
{
    int ret = NO_ERROR;
    uint32_t transfer = dataspace & HAL_DATASPACE_TRANSFER_MASK;
    uint64_t planeTransfer = 0;
    switch(transfer) {
        case HAL_DATASPACE_TRANSFER_LINEAR:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "Linear");
            break;
        case HAL_DATASPACE_TRANSFER_SRGB:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "sRGB");
            break;
        case HAL_DATASPACE_TRANSFER_SMPTE_170M:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "SMPTE 170M");
            break;
        case HAL_DATASPACE_TRANSFER_GAMMA2_2:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "Gamma 2.2");
            break;
        case HAL_DATASPACE_TRANSFER_GAMMA2_6:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "Gamma 2.6");
            break;
        case HAL_DATASPACE_TRANSFER_GAMMA2_8:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "Gamma 2.8");
            break;
        case HAL_DATASPACE_TRANSFER_ST2084:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "ST2084");
            break;
        case HAL_DATASPACE_TRANSFER_HLG:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "HLG");
            break;
        default:
            std::tie(planeTransfer, ret) = plane.transfer_property().GetEnumValueWithName(
                    "Unspecified");
            break;
    }
    if (ret) {
        HWC_LOGE(NULL, "failed to get plane transfer, datapsace(0x%8x), transfer(0x%8x), ret(%d)",
                dataspace, transfer, ret);
    }
    return planeTransfer;
}

uint64_t ExynosDisplayDrmInterface::dataspaceToPlaneRange(
        const android_dataspace dataspace, const DrmPlane& plane)
{
    int ret = NO_ERROR;
    uint32_t range = dataspace & HAL_DATASPACE_RANGE_MASK;
    uint64_t planeRange = 0;
    switch(range) {
        case HAL_DATASPACE_RANGE_FULL:
            std::tie(planeRange, ret) = plane.range_property().GetEnumValueWithName(
                    "Full");
            break;
        case HAL_DATASPACE_RANGE_LIMITED:
            std::tie(planeRange, ret) = plane.range_property().GetEnumValueWithName(
                    "Limited");
            break;
        case HAL_DATASPACE_RANGE_EXTENDED:
            std::tie(planeRange, ret) = plane.range_property().GetEnumValueWithName(
                    "Extended");
            break;
        default:
            std::tie(planeRange, ret) = plane.range_property().GetEnumValueWithName(
                    "Unspecified");
            break;
    }
    if (ret) {
        HWC_LOGE(NULL, "failed to get plane range, datapsace(0x%8x), range(0x%8x), ret(%d)",
                dataspace, range, ret);
    }
    return planeRange;
}
