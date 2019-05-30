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

#include <sys/types.h>
#include "ExynosDisplayDrmInterface.h"
#include "ExynosDisplay.h"
#include "ExynosHWCDebug.h"

constexpr uint32_t MAX_PLANE_NUM = 3;
constexpr uint32_t CBCR_INDEX = 1;

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
    DrmModeAtomicReq drmReq;

    ret = drmModeAtomicAddProperty(drmReq.pset(), mDrmCrtc->id(),
           mDrmCrtc->active_property().id(), 1);

    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "Failed to add crtc active to pset\n");
        return ret;
    }

    ret = drmModeAtomicAddProperty(drmReq.pset(), mDrmCrtc->id(), mDrmCrtc->mode_property().id(),
            mModeState.blob_id) < 0 ||
        drmModeAtomicAddProperty(drmReq.pset(), mDrmConnector->id(),
                mDrmConnector->crtc_id_property().id(),
                mDrmCrtc->id()) < 0;
    if (ret) {
        HWC_LOGE(mExynosDisplay, "Failed to add blob %d to pset", mModeState.blob_id);
        return ret;
    }

    if (!ret) {
        uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
        ret = drmModeAtomicCommit(mDrmDevice->fd(), drmReq.pset(), flags, mDrmDevice);
        if (ret) {
            HWC_LOGE(mExynosDisplay, "Failed to commit pset ret=%d in applyDisplayMode()\n", ret);
            return ret;
        }
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
    return ret;
}

int32_t ExynosDisplayDrmInterface::setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos)
{
    return 0;
}

int32_t ExynosDisplayDrmInterface::getHdrCapabilities(uint32_t* outNumTypes,
        int32_t* outTypes, float* outMaxLuminance,
        float* outMaxAverageLuminance, float* outMinLuminance)
{
    *outNumTypes = 0;
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

int32_t ExynosDisplayDrmInterface::deliverWinConfigData()
{
    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq;
    std::unordered_map<uint32_t, uint32_t> planeEnableInfo;
    std::vector<uint32_t> fbIds(getMaxWindowNum(), 0);
    android::String8 result;

    uint64_t out_fences[mDrmDevice->crtcs().size()];
    if (mDrmCrtc->out_fence_ptr_property().id() != 0) {
        ret = drmModeAtomicAddProperty(drmReq.pset(), mDrmCrtc->id(),
                mDrmCrtc->out_fence_ptr_property().id(),
                (uint64_t)&out_fences[mDrmCrtc->pipe()]);
        if (ret < 0) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to add OUT_FENCE_PTR property to pset: %d",
                    __func__, ret);
            drmReq.setError(ret, this);
            return ret;
        }
    }

    for (auto &plane : mDrmDevice->planes()) {
        planeEnableInfo[plane->id()] = 0;
    }

    for (size_t i = 0; i < mExynosDisplay->mDpuData.configs.size(); i++) {
        exynos_win_config_data& config = mExynosDisplay->mDpuData.configs[i];
        if ((config.state == config.WIN_STATE_BUFFER) ||
            (config.state == config.WIN_STATE_COLOR)) {
            uint32_t fb_id = 0;
            int channelId = 0;
            if ((channelId = getDeconChannel(config.assignedMPP)) < 0) {
                HWC_LOGE(mExynosDisplay, "%s:: Failed to get channel id (%d)",
                        __func__, channelId);
                ret = -EINVAL;
                drmReq.setError(ret, this);
                return ret;
            }
            auto &plane = mDrmDevice->planes().at(channelId);
            /* Set this plane is enabled */
            planeEnableInfo[plane->id()] = 1;

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
                    ret = -EINVAL;
                    drmReq.setError(ret, this);
                    return ret;
                }

                bpp = getBytePerPixelOfPrimaryPlane(config.format);
                if ((bufferNum = getBufferNumOfFormat(config.format)) == 0) {
                    HWC_LOGE(mExynosDisplay, "%s:: getBufferNumOfFormat(%d) error",
                            __func__, config.format);
                    ret = -EINVAL;
                    drmReq.setError(ret, this);
                    return ret;
                }
                if (((planeNum = getPlaneNumOfFormat(config.format)) == 0) ||
                    (planeNum > MAX_PLANE_NUM)) {
                    HWC_LOGE(mExynosDisplay, "%s:: getPlaneNumOfFormat(%d) error, planeNum(%d)",
                            __func__, config.format, planeNum);
                    ret = -EINVAL;
                    drmReq.setError(ret, this);
                    return ret;
                }
                for(uint32_t bufferIndex = 0; bufferIndex < bufferNum; bufferIndex++) {
                    pitches[bufferIndex] = config.src.f_w * bpp;
                    buf_handles[bufferIndex] = (uint32_t)config.fd_idma[bufferIndex];
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
                        drmFormat, buf_handles, pitches, offsets, modifiers, &fb_id, modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);
            } else if (config.state == config.WIN_STATE_COLOR) {
                drmFormat = DRM_FORMAT_BGRA8888;
                buf_handles[0] = 0xffff;
                bpp = getBytePerPixelOfPrimaryPlane(HAL_PIXEL_FORMAT_BGRA_8888);
                pitches[0] = config.dst.w * bpp;
                config.src.w = config.dst.w;
                config.src.h = config.dst.h;

                ret = drmModeAddFB2WithModifiers(mDrmDevice->fd(), config.dst.w, config.dst.h,
                        drmFormat, buf_handles, pitches, offsets, modifiers, &fb_id, modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);
            } else {
                HWC_LOGE(mExynosDisplay, "%s:: config[%zu] known config state(%d)",
                        __func__, i, config.state);
                ret = -EINVAL;
                drmReq.setError(ret, this);
                return ret;
            }

            if (ret != NO_ERROR) {
                HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add FB, fb_id(%d), ret(%d), f_w: %d, f_h: %d, dst.w: %d, dst.h: %d, "
                        "format: %d, buf_handles[%d, %d, %d, %d], "
                        "pitches[%d, %d, %d, %d], offsets[%d, %d, %d, %d], modifiers[%#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 "]",
                        __func__, i, fb_id, ret,
                        config.src.f_w, config.src.f_h, config.dst.w, config.dst.h, drmFormat,
                        buf_handles[0], buf_handles[1], buf_handles[2], buf_handles[3],
                        pitches[0], pitches[1], pitches[2], pitches[3],
                        offsets[0], offsets[1], offsets[2], offsets[3],
                        modifiers[0], modifiers[1], modifiers[2], modifiers[3]);
                drmReq.setError(ret, this);
                return ret;
            }
            fbIds[i] = fb_id;

            ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->crtc_property().id(), mDrmCrtc->id()) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->fb_property().id(), fb_id) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->crtc_x_property().id(),
                    config.dst.x) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->crtc_y_property().id(),
                    config.dst.y) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->crtc_w_property().id(),
                    config.dst.w) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->crtc_h_property().id(),
                    config.dst.h) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->src_x_property().id(),
                    (int)(config.src.x) << 16) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->src_y_property().id(),
                    (int)(config.src.y) << 16) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->src_w_property().id(),
                    (int)(config.src.w) << 16) < 0;
            ret |= drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->src_h_property().id(),
                    (int)(config.src.h) << 16) < 0;
            if (ret) {
                HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add plane %d to set, ret(%d)",
                        __func__, i, plane->id(), ret);
                drmReq.setError(ret, this);
                return ret;
            }

            if (config.state == config.WIN_STATE_COLOR) {
                ret =  drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->color_property().id(), config.color) < 0;
                if (ret) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to set color(%d) for plane %d, ret(%d)",
                            __func__, i, config.color, plane->id(), ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }

            uint64_t rotation = halTransformToDrmRot(config.transform);
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
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to get blend for plane %d, blend(%" PRId64 "), ret(%d)",
                            __func__, i, plane->id(), blend, ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }

            if (plane->zpos_property().id() &&
                !plane->zpos_property().is_immutable()) {
                uint64_t min_zpos = 0;

                // Ignore ret and use min_zpos as 0 by default
                std::tie(std::ignore, min_zpos) = plane->zpos_property().range_min();

                ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->zpos_property().id(),
                        i + min_zpos) < 0;
                if (ret < 0) {
                    ALOGE("Failed to add zpos property %d to plane %d, ret(%d)",
                            plane->zpos_property().id(), plane->id(), ret);
                    break;
                }
            }

            if (plane->rotation_property().id()) {
                ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->rotation_property().id(),
                        rotation) < 0;
                if (ret) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add rotation property %d for plane %d, ret(%d)",
                            __func__, i, plane->rotation_property().id(), plane->id(), ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }
            if (plane->blend_property().id()) {
                ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->blend_property().id(), blend) < 0;
                if (ret) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add pixel blend mode property %d for plane %d, ret(%d)",
                            __func__, i, plane->blend_property().id(), plane->id(), ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }

            if (plane->alpha_property().id()) {
                ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->alpha_property().id(), config.plane_alpha) < 0;
                if (ret) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add alpha property %d for plane %d, ret(%d)",
                            __func__, i, plane->alpha_property().id(), plane->id(), ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }

            if (config.acq_fence >= 0) {
                int prop_id = plane->in_fence_fd_property().id();
                if (prop_id == 0) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to get IN_FENCE_FD property id for plane %d",
                            __func__, i, plane->id());
                    break;
                }
                ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(), prop_id, config.acq_fence) < 0;
                if (ret) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add IN_FENCE_FD property to pset for plane %d, ret(%d)",
                            __func__, i, plane->id(), ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }

            if (plane->compression_source_property().id()) {
                uint64_t compression_source = 0;
                switch (config.comp_src) {
                    case DPP_COMP_SRC_NONE:
                        std::tie(compression_source, ret) = plane->compression_source_property().GetEnumValueWithName(
                                "None");
                        break;
                    case DPP_COMP_SRC_G2D:
                        std::tie(compression_source, ret) = plane->compression_source_property().GetEnumValueWithName(
                                "G2D");
                        break;
                    case DPP_COMP_SRC_GPU:
                        std::tie(compression_source, ret) = plane->compression_source_property().GetEnumValueWithName(
                                "GPU");
                        break;
                    default:
                        HWC_LOGE(mExynosDisplay, "%s:: config[%zu] unknown copression source (%d)",
                                __func__, i, config.comp_src);
                        ret = -EINVAL;
                        drmReq.setError(ret, this);
                        return ret;
                }
                ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->compression_source_property().id(), compression_source);
                if (ret < 0) {
                    HWC_LOGE(mExynosDisplay, "%s:: config[%zu]: Failed to add compression source property %d for plane %d, ret(%d)",
                            __func__, i, plane->compression_source_property().id(), plane->id(), ret);
                    drmReq.setError(ret, this);
                    return ret;
                }
            }
        }
    }

    /* Disable unused plane */
    for (auto &plane : mDrmDevice->planes()) {
        if (planeEnableInfo[plane->id()] == 0) {
#if 0
            ExynosMPP* exynosMPP = mExynosMPPsForPlane[plane->id()];
            /* Do not disable planes that are reserved to other dispaly */
            if ((exynosMPP != NULL) && (mExynosDisplay != NULL) &&
                    (exynosMPP->mAssignedState & MPP_ASSIGN_STATE_RESERVED) &&
                    (exynosMPP->mReservedDisplay != (int32_t)mExynosDisplay->mType))
                continue;
#endif
            ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->crtc_property().id(), 0) < 0 ||
                drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                        plane->fb_property().id(), 0) < 0;
            if (ret) {
                HWC_LOGE(mExynosDisplay, "%s:: Failed to add plane %d disable to pset",
                        __func__, plane->id());
                drmReq.setError(ret, this);
                return ret;
            }
        }
    }

    if (!ret) {
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
            drmReq.setError(ret, this);
            return ret;
        }
    } else {
        HWC_LOGE(mExynosDisplay, "%s:: There was error to set properties for commit, ret(%d)", __func__, ret);
        drmReq.setError(ret, this);
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

    return ret;
}

int32_t ExynosDisplayDrmInterface::clearDisplay()
{
    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq;

    /* Disable all planes */
    for (auto &plane : mDrmDevice->planes()) {
#if 0
        ExynosMPP* exynosMPP = mExynosMPPsForPlane[plane->id()];
        /* Do not disable planes that are reserved to other dispaly */
        if ((exynosMPP != NULL) && (mExynosDisplay != NULL) &&
            (exynosMPP->mAssignedState & MPP_ASSIGN_STATE_RESERVED) &&
            (exynosMPP->mReservedDisplay != (int32_t)mExynosDisplay->mType))
            continue;
#endif
        ret = drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                plane->crtc_property().id(), 0) < 0 ||
            drmModeAtomicAddProperty(drmReq.pset(), plane->id(),
                    plane->fb_property().id(), 0) < 0;
        if (ret) {
            HWC_LOGE(mExynosDisplay, "Failed to add plane %d disable to pset", plane->id());
            break;
        }
    }

    if (!ret) {
        uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
        ret = drmModeAtomicCommit(mDrmDevice->fd(), drmReq.pset(), flags, mDrmDevice);
    }

    return ret;
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
