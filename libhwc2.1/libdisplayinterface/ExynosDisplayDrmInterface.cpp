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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include <sys/types.h>
#include "ExynosDisplayDrmInterface.h"
#include "ExynosHWCDebug.h"
#include <drm/drm_fourcc.h>
#include <xf86drm.h>

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

void ExynosDisplayDrmInterface::parseEnums(const DrmProperty& property,
        const std::vector<std::pair<uint32_t, const char *>> &enums,
        std::unordered_map<uint32_t, uint64_t> &out_enums)
{
    uint64_t value;
    int ret;
    for (auto &e : enums) {
        std::tie(value, ret) = property.GetEnumValueWithName(e.second);
        if (ret == NO_ERROR)
            out_enums[e.first] = value;
        else
            ALOGE("Fail to find enum value with name %s", e.second);
    }
}

void ExynosDisplayDrmInterface::parseBlendEnums(const DrmProperty& property)
{
    const std::vector<std::pair<uint32_t, const char *>> blendEnums = {
        {HWC2_BLEND_MODE_NONE, "None"},
        {HWC2_BLEND_MODE_PREMULTIPLIED, "Pre-multiplied"},
        {HWC2_BLEND_MODE_COVERAGE, "Coverage"},
    };

    ALOGD("Init blend enums");
    parseEnums(property, blendEnums, mBlendEnums);
    for (auto &e : mBlendEnums) {
        ALOGD("blend [hal: %d, drm: %" PRId64 "]", e.first, e.second);
    }
}

void ExynosDisplayDrmInterface::parseStandardEnums(const DrmProperty& property)
{
    const std::vector<std::pair<uint32_t, const char *>> standardEnums = {
        {HAL_DATASPACE_STANDARD_UNSPECIFIED, "Unspecified"},
        {HAL_DATASPACE_STANDARD_BT709, "BT709"},
        {HAL_DATASPACE_STANDARD_BT601_625, "BT601_625"},
        {HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED, "BT601_625_UNADJUSTED"},
        {HAL_DATASPACE_STANDARD_BT601_525, "BT601_525"},
        {HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED, "BT601_525_UNADJUSTED"},
        {HAL_DATASPACE_STANDARD_BT2020, "BT2020"},
        {HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE, "BT2020_CONSTANT_LUMINANCE"},
        {HAL_DATASPACE_STANDARD_BT470M, "BT470M"},
        {HAL_DATASPACE_STANDARD_FILM, "FILM"},
        {HAL_DATASPACE_STANDARD_DCI_P3, "DCI-P3"},
        {HAL_DATASPACE_STANDARD_ADOBE_RGB, "Adobe RGB"},
    };

    ALOGD("Init standard enums");
    parseEnums(property, standardEnums, mStandardEnums);
    for (auto &e : mStandardEnums) {
        ALOGD("standard [hal: %d, drm: %" PRId64 "]",
                e.first >> HAL_DATASPACE_STANDARD_SHIFT, e.second);
    }
}

void ExynosDisplayDrmInterface::parseTransferEnums(const DrmProperty& property)
{
    const std::vector<std::pair<uint32_t, const char *>> transferEnums = {
        {HAL_DATASPACE_TRANSFER_UNSPECIFIED, "Unspecified"},
        {HAL_DATASPACE_TRANSFER_LINEAR, "Linear"},
        {HAL_DATASPACE_TRANSFER_SRGB, "sRGB"},
        {HAL_DATASPACE_TRANSFER_SMPTE_170M, "SMPTE 170M"},
        {HAL_DATASPACE_TRANSFER_GAMMA2_2, "Gamma 2.2"},
        {HAL_DATASPACE_TRANSFER_GAMMA2_6, "Gamma 2.6"},
        {HAL_DATASPACE_TRANSFER_GAMMA2_8, "Gamma 2.8"},
        {HAL_DATASPACE_TRANSFER_ST2084, "ST2084"},
        {HAL_DATASPACE_TRANSFER_HLG, "HLG"},
    };

    ALOGD("Init transfer enums");
    parseEnums(property, transferEnums, mTransferEnums);
    for (auto &e : mTransferEnums) {
        ALOGD("transfer [hal: %d, drm: %" PRId64 "]",
                e.first >> HAL_DATASPACE_TRANSFER_SHIFT, e.second);
    }
}

void ExynosDisplayDrmInterface::parseRangeEnums(const DrmProperty& property)
{
    const std::vector<std::pair<uint32_t, const char *>> rangeEnums = {
        {HAL_DATASPACE_RANGE_UNSPECIFIED, "Unspecified"},
        {HAL_DATASPACE_RANGE_FULL, "Full"},
        {HAL_DATASPACE_RANGE_LIMITED, "Limited"},
        {HAL_DATASPACE_RANGE_EXTENDED, "Extended"},
    };

    ALOGD("Init range enums");
    parseEnums(property, rangeEnums, mRangeEnums);
    for (auto &e : mRangeEnums) {
        ALOGD("range [hal: %d, drm: %" PRId64 "]",
                e.first >> HAL_DATASPACE_RANGE_SHIFT, e.second);
    }
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
    mReadbackInfo.init(mDrmDevice, mExynosDisplay->mDisplayId);

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

    if (!mDrmDevice->planes().empty()) {
        auto &plane = mDrmDevice->planes().front();
        parseBlendEnums(plane->blend_property());
        parseStandardEnums(plane->standard_property());
        parseTransferEnums(plane->transfer_property());
        parseRangeEnums(plane->range_property());
    }

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
    int ret = mDrmDevice->CreatePropertyBlob(&drm_mode, sizeof(drm_mode),
            &id);
    if (ret) {
        HWC_LOGE(mExynosDisplay, "Failed to create mode property blob %d", ret);
        return ret;
    }
    mModeState.blob_id = id;
    mModeState.needs_modeset = true;

    if (applyDisplayMode() < 0)
        HWC_LOGE(mExynosDisplay, "%s: Fail to apply display mode",
                __func__);
    return 0;
}

int32_t ExynosDisplayDrmInterface::applyDisplayMode()
{
    if (mModeState.needs_modeset == false)
        return NO_ERROR;

    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq(this);

    if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
           mDrmCrtc->active_property(), 1)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
            mDrmCrtc->mode_property(), mModeState.blob_id)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(mDrmConnector->id(),
            mDrmConnector->crtc_id_property(), mDrmCrtc->id())) < 0)
        return ret;

    ret = drmReq.commit(DRM_MODE_ATOMIC_ALLOW_MODESET, true);
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
        DrmModeAtomicReq &drmReq,
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
    if (config.protection)
        modifiers[0] |= DRM_FORMAT_MOD_PROTECTION;

    if (config.state == config.WIN_STATE_BUFFER) {
        drmFormat = halFormatToDrmFormat(config.format,
                config.compression ? AFBC : 0);
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

        if (config.compression) {
            uint64_t compressed_modifier = AFBC_FORMAT_MOD_BLOCK_SIZE_16x16;
            switch (config.comp_src) {
                case DPP_COMP_SRC_G2D:
                    compressed_modifier |= AFBC_FORMAT_MOD_SOURCE_G2D;
                    break;
                case DPP_COMP_SRC_GPU:
                    compressed_modifier |= AFBC_FORMAT_MOD_SOURCE_GPU;
                    break;
                default:
                    break;
            }
            modifiers[0] |= DRM_FORMAT_MOD_ARM_AFBC(compressed_modifier);
        }

        for (uint32_t bufferIndex = 0; bufferIndex < bufferNum; bufferIndex++) {
            pitches[bufferIndex] = config.src.f_w * bpp;

            buf_handles[bufferIndex] = drmReq.getBufHandleFromFd(config.fd_idma[bufferIndex]);
            modifiers[bufferIndex] = modifiers[0];
        }

        if ((bufferNum == 1) && (planeNum > bufferNum)) {
            /* offset for cbcr */
            offsets[CBCR_INDEX] =
                getExynosBufferYLength(config.src.f_w, config.src.f_h, config.format);
            for (uint32_t planeIndex = 1; planeIndex < planeNum; planeIndex++)
            {
                buf_handles[planeIndex] = buf_handles[0];
                pitches[planeIndex] = pitches[0];
                modifiers[planeIndex] = modifiers[0];
            }
        }

        ret = drmReq.addFB2WithModifiers(config.src.f_w, config.src.f_h,
                drmFormat, buf_handles, pitches, offsets, modifiers, &fbId, modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);

        for (uint32_t bufferIndex = 0; bufferIndex < bufferNum; bufferIndex++) {
            /* framebuffer already holds a reference, remove ours */
            drmReq.freeBufHandle(buf_handles[bufferIndex]);
        }
    } else if (config.state == config.WIN_STATE_COLOR) {
        modifiers[0] |= DRM_FORMAT_MOD_SAMSUNG_COLORMAP;
        drmFormat = DRM_FORMAT_BGRA8888;
        buf_handles[0] = config.color;
        bpp = getBytePerPixelOfPrimaryPlane(HAL_PIXEL_FORMAT_BGRA_8888);
        pitches[0] = config.dst.w * bpp;

        ret = drmReq.addFB2WithModifiers(config.dst.w, config.dst.h,
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

    if (fbId == 0) {
        if ((ret = addFBFromDisplayConfig(drmReq, config, fbId)) < 0) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to add FB, fbId(%d), ret(%d)",
                    __func__, fbId, ret);
            return ret;
        }
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

    uint64_t drmEnum = 0;
    std::tie(drmEnum, ret) = halToDrmEnum(config.blending, mBlendEnums);
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "Fail to convert blend(%d)", config.blending);
        return ret;
    }
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->blend_property(), drmEnum, true)) < 0)
        return ret;

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

    std::tie(drmEnum, ret) =
        halToDrmEnum(config.dataspace & HAL_DATASPACE_STANDARD_MASK, mStandardEnums);
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "Fail to convert standard(%d)",
                config.dataspace & HAL_DATASPACE_STANDARD_MASK);
        return ret;
    }
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->standard_property(),
                    drmEnum, true)) < 0)
        return ret;

    std::tie(drmEnum, ret) =
        halToDrmEnum(config.dataspace & HAL_DATASPACE_TRANSFER_MASK, mTransferEnums);
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "Fail to convert transfer(%d)",
                config.dataspace & HAL_DATASPACE_TRANSFER_MASK);
        return ret;
    }
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->transfer_property(), drmEnum, true)) < 0)
        return ret;

    std::tie(drmEnum, ret) =
        halToDrmEnum(config.dataspace & HAL_DATASPACE_RANGE_MASK, mRangeEnums);
    if (ret < 0) {
        HWC_LOGE(mExynosDisplay, "Fail to convert range(%d)",
                config.dataspace & HAL_DATASPACE_RANGE_MASK);
        return ret;
    }
    if ((ret = drmReq.atomicAddProperty(plane->id(),
                    plane->range_property(), drmEnum, true)) < 0)
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
    android::String8 result;

    if (mExynosDisplay->mDpuData.enable_readback) {
        if ((ret = setupWritebackCommit(drmReq)) < 0) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to setup writeback commit ret(%d)",
                    __func__, ret);
            return ret;
        }
    }

    if (mDrmCrtc->partial_x_property().id() &&
        mDrmCrtc->partial_y_property().id() &&
        mDrmCrtc->partial_w_property().id() &&
        mDrmCrtc->partial_h_property().id()) {
        if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
                        mDrmCrtc->partial_x_property(),
                        (uint64_t)mExynosDisplay->mDpuData.win_update_region.x, true)) < 0)
            return ret;
        if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
                        mDrmCrtc->partial_y_property(),
                        (uint64_t)mExynosDisplay->mDpuData.win_update_region.y, true)) < 0)
            return ret;
        if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
                        mDrmCrtc->partial_w_property(),
                        (uint64_t)mExynosDisplay->mDpuData.win_update_region.w, true)) < 0)
            return ret;
        if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
                        mDrmCrtc->partial_h_property(),
                        (uint64_t)mExynosDisplay->mDpuData.win_update_region.h, true)) < 0)
            return ret;
    }

    uint64_t out_fences[mDrmDevice->crtcs().size()];
    if ((ret = drmReq.atomicAddProperty(mDrmCrtc->id(),
                    mDrmCrtc->out_fence_ptr_property(),
                    (uint64_t)&out_fences[mDrmCrtc->pipe()], true)) < 0) {
        return ret;
    }

    for (auto &plane : mDrmDevice->planes()) {
        planeEnableInfo[plane->id()] = 0;
    }

    if ((ret = setDisplayColorSetting(drmReq)) != 0) {
        HWC_LOGE(mExynosDisplay, "Failed to set display color setting");
        return ret;
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
                return ret;
            }
            /* src size should be set even in dim layer */
            if (config.state == config.WIN_STATE_COLOR) {
                config.src.w = config.dst.w;
                config.src.h = config.dst.h;
            }
            auto &plane = mDrmDevice->planes().at(channelId);
            uint32_t fbId = 0;
            if ((ret = setupCommitFromDisplayConfig(drmReq, config, i, plane, fbId)) < 0) {
                HWC_LOGE(mExynosDisplay, "setupCommitFromDisplayConfig failed, config[%zu]", i);
                return ret;
            }
            if ((ret = setPlaneColorSetting(drmReq, plane, config)) != 0) {
                HWC_LOGE(mExynosDisplay, "Failed to set plane color setting, config[%zu]", i);
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

    if (ATRACE_ENABLED()) {
        mExynosDisplay->traceLayerTypes();
    }

    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK;
    if (mExynosDisplay->mDpuData.enable_readback)
        flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

    if ((ret = drmReq.commit(flags, true)) < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: Failed to commit pset ret=%d in deliverWinConfigData()\n",
                __func__, ret);
        return ret;
    }

    drmReq.removeFbs(mOldFbIds);
    drmReq.moveTrackedFbs(mOldFbIds);

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

int32_t ExynosDisplayDrmInterface::clearDisplay(bool __unused readback)
{
    int ret = NO_ERROR;
    DrmModeAtomicReq drmReq(this);
#if 0
    /* TODO (b/151848411): Check whether clearing writeback buffer is required */
    if (readback) {
        if ((ret = setupWritebackCommit(drmReq)) < 0) {
            HWC_LOGE(mExynosDisplay, "%s:: Failed to setup writeback commit ret(%d)",
                    __func__, ret);
            return ret;
        }
    }
#endif

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
        if ((ret = drmReq.atomicAddProperty(plane->id(),
                plane->crtc_property(), 0)) < 0)
            return ret;

        if ((ret = drmReq.atomicAddProperty(plane->id(),
                plane->fb_property(), 0)) < 0)
            return ret;
    }

    ret = drmReq.commit(DRM_MODE_ATOMIC_ALLOW_MODESET, true);
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

uint32_t ExynosDisplayDrmInterface::getMaxWindowNum()
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
    if (mDrmDisplayInterface != NULL) {
        removeFbs(mFbIds);
        if (mError != 0) {
            android::String8 result;
            result.appendFormat("atomic commit error\n");
            dumpAtomicCommitInfo(result);
            HWC_LOGE(mDrmDisplayInterface->mExynosDisplay, "%s", result.string());
        }
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

String8& ExynosDisplayDrmInterface::DrmModeAtomicReq::dumpAtomicCommitInfo(
        String8 &result, bool debugPrint)
{
    /* print log only if eDebugDisplayInterfaceConfig flag is set when debugPrint is true */
    if (debugPrint &&
        (hwcCheckDebugMessages(eDebugDisplayInterfaceConfig) == false))
        return result;

    for (int i = 0; i < drmModeAtomicGetCursor(mPset); i++) {
        const DrmProperty *property = NULL;
        String8 objectName;
        /* Check crtc properties */
        if (mPset->items[i].object_id == mDrmDisplayInterface->mDrmCrtc->id()) {
            for (auto property_ptr : mDrmDisplayInterface->mDrmCrtc->properties()) {
                if (mPset->items[i].property_id == property_ptr->id()){
                    property = property_ptr;
                    objectName.appendFormat("Crtc");
                    break;
                }
            }
            if (property == NULL) {
                HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
                        "%s:: object id is crtc but there is no matched property",
                        __func__);
            }
        } else if (mPset->items[i].object_id == mDrmDisplayInterface->mDrmConnector->id()) {
            for (auto property_ptr : mDrmDisplayInterface->mDrmConnector->properties()) {
                if (mPset->items[i].property_id == property_ptr->id()){
                    property = property_ptr;
                    objectName.appendFormat("Connector");
                    break;
                }
            }
            if (property == NULL) {
                HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
                        "%s:: object id is connector but there is no matched property",
                        __func__);
            }
        } else {
            uint32_t channelId = 0;
            for (auto &plane : mDrmDisplayInterface->mDrmDevice->planes()) {
                if (mPset->items[i].object_id == plane->id()) {
                    for (auto property_ptr : plane->properties()) {
                        if (mPset->items[i].property_id == property_ptr->id()){
                            property = property_ptr;
                            objectName.appendFormat("Plane[%d]", channelId);
                            break;
                        }
                    }
                    if (property == NULL) {
                        HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
                                "%s:: object id is plane but there is no matched property",
                                __func__);
                    }
                }
                channelId++;
            }
        }
        if (property == NULL) {
            HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
                    "%s:: Fail to get property[%d] (object_id: %d, property_id: %d, value: %" PRId64 ")",
                    __func__, i, mPset->items[i].object_id, mPset->items[i].property_id,
                    mPset->items[i].value);
            continue;
        }

        if (debugPrint)
            ALOGD("property[%d] %s object_id: %d, property_id: %d, name: %s,  value: %" PRId64 ")\n",
                    i, objectName.string(), mPset->items[i].object_id, mPset->items[i].property_id, property->name().c_str(), mPset->items[i].value);
        else
            result.appendFormat("property[%d] %s object_id: %d, property_id: %d, name: %s,  value: %" PRId64 ")\n",
                i,  objectName.string(), mPset->items[i].object_id, mPset->items[i].property_id, property->name().c_str(), mPset->items[i].value);
    }
    return result;
}


int ExynosDisplayDrmInterface::DrmModeAtomicReq::commit(uint32_t flags, bool loggingForDebug)
{
    ATRACE_NAME("drmModeAtomicCommit");
    android::String8 result;
    int ret = drmModeAtomicCommit(mDrmDisplayInterface->mDrmDevice->fd(),
            mPset, flags, mDrmDisplayInterface->mDrmDevice);
    if (loggingForDebug)
        dumpAtomicCommitInfo(result, true);
    if (ret < 0)
        setError(ret);
    return ret;
}

uint32_t ExynosDisplayDrmInterface::DrmModeAtomicReq::getBufHandleFromFd(int fd)
{
    uint32_t gem_handle = 0;

    int ret = drmPrimeFDToHandle(drmFd(), fd, &gem_handle);
    if (ret) {
        HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
                 "drmPrimeFDToHandle failed with error %d", ret);
        return ret;
    }

    return gem_handle;
}

void ExynosDisplayDrmInterface::DrmModeAtomicReq::freeBufHandle(uint32_t handle)
{
  struct drm_gem_close gem_close {
      .handle = handle
  };
  int ret = drmIoctl(drmFd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
  if (ret) {
      HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
               "Failed to close gem handle with error %d\n", ret);
  }
}

void ExynosDisplayDrmInterface::DrmModeAtomicReq::removeFbs(std::vector<uint32_t> &fbs)
{
    for (auto &fb : fbs) {
        drmModeRmFB(mDrmDisplayInterface->mDrmDevice->fd(), fb);
    }
}

void ExynosDisplayDrmInterface::DrmModeAtomicReq::moveTrackedFbs(std::vector<uint32_t> &fbs)
{
    fbs = mFbIds;
    mFbIds.clear();
}

void ExynosDisplayDrmInterface::DrmModeAtomicReq::moveTrackedLastFb(uint32_t &fb)
{
    fb = mFbIds.back();
    mFbIds.pop_back();
}

int ExynosDisplayDrmInterface::DrmModeAtomicReq::addFB2WithModifiers(
        uint32_t width, uint32_t height,
        uint32_t pixel_format, const uint32_t bo_handles[4],
        const uint32_t pitches[4], const uint32_t offsets[4],
        const uint64_t modifier[4], uint32_t *buf_id,
        uint32_t flags)
{
    int ret = drmModeAddFB2WithModifiers(mDrmDisplayInterface->mDrmDevice->fd(),
            width, height, pixel_format, bo_handles, pitches,
            offsets, modifier, buf_id, flags);
    if (!ret)
        mFbIds.push_back(*buf_id);

    return ret;
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

std::tuple<uint64_t, int> ExynosDisplayDrmInterface::halToDrmEnum(
        const int32_t halData, const DrmPropertyMap &drmEnums)
{
    auto it = drmEnums.find(halData);
    if (it != drmEnums.end()) {
        return std::make_tuple(it->second, 0);
    } else {
        HWC_LOGE(NULL, "%s::Failed to find standard enum(%d)",
                __func__, halData);
        return std::make_tuple(0, -EINVAL);
    }
}

int32_t ExynosDisplayDrmInterface::getReadbackBufferAttributes(
        int32_t* /*android_pixel_format_t*/ outFormat,
        int32_t* /*android_dataspace_t*/ outDataspace)
{
    DrmConnector *writeback_conn = mReadbackInfo.getWritebackConnector();
    if (writeback_conn == NULL) {
        ALOGE("%s: There is no writeback connection", __func__);
        return -EINVAL;
    }
    /* TODO (b/149043754): color mode should be set */
    mReadbackInfo.pickFormatDataspace(HAL_COLOR_MODE_NATIVE);

    if ((mReadbackInfo.mReadbackFormat == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) ||
        (mReadbackInfo.mReadbackDataspace == HAL_DATASPACE_UNKNOWN)) {
        ALOGE("readback format(%d) or dataspace(0x%8x) is not valid",
                mReadbackInfo.mReadbackFormat,
                mReadbackInfo.mReadbackDataspace);
        return -EINVAL;
    }
    *outFormat = mReadbackInfo.mReadbackFormat;
    *outDataspace = mReadbackInfo.mReadbackDataspace;
    return NO_ERROR;
}

int32_t ExynosDisplayDrmInterface::setupWritebackCommit(DrmModeAtomicReq &drmReq)
{
    int ret = NO_ERROR;
    DrmConnector *writeback_conn = mReadbackInfo.getWritebackConnector();
    if (writeback_conn == NULL) {
        ALOGE("%s: There is no writeback connection", __func__);
        return -EINVAL;
    }
    if (writeback_conn->writeback_fb_id().id() == 0 ||
        writeback_conn->writeback_out_fence().id() == 0) {
        ALOGE("%s: Writeback properties don't exit", __func__);
        return -EINVAL;
    }

    uint32_t writeback_fb_id = 0;
    exynos_win_config_data writeback_config;
    writeback_config.state = exynos_win_config_data::WIN_STATE_BUFFER;
    writeback_config.format = mReadbackInfo.mReadbackFormat;
    writeback_config.src = {0, 0, mExynosDisplay->mXres, mExynosDisplay->mYres,
        mExynosDisplay->mXres, mExynosDisplay->mYres};
    writeback_config.dst = {0, 0, mExynosDisplay->mXres, mExynosDisplay->mYres,
        mExynosDisplay->mXres, mExynosDisplay->mYres};
    writeback_config.fd_idma[0] = mExynosDisplay->mDpuData.readback_info.handle->fd;
    writeback_config.fd_idma[1] = mExynosDisplay->mDpuData.readback_info.handle->fd1;
    writeback_config.fd_idma[2] = mExynosDisplay->mDpuData.readback_info.handle->fd2;
    if ((ret = addFBFromDisplayConfig(drmReq, writeback_config, writeback_fb_id)) < 0) {
        ALOGE("%s: addFBFromDisplayConfig() fail ret(%d)",__func__, ret);
        return ret;
    }

    if ((ret = drmReq.atomicAddProperty(writeback_conn->id(),
            writeback_conn->writeback_fb_id(),
            writeback_fb_id)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(writeback_conn->id(),
            writeback_conn->writeback_out_fence(),
            (uint64_t)& mExynosDisplay->mDpuData.readback_info.acq_fence)) < 0)
        return ret;

    if ((ret = drmReq.atomicAddProperty(writeback_conn->id(),
            writeback_conn->crtc_id_property(),
            mDrmCrtc->id())) < 0)
        return ret;

    uint32_t fb = 0;
    drmReq.moveTrackedLastFb(fb);
    /* writeback_fb_id and fb should be same */
    mReadbackInfo.setFbId(fb);
    return NO_ERROR;
}

void ExynosDisplayDrmInterface::DrmReadbackInfo::init(DrmDevice *drmDevice, uint32_t displayId)
{
    mDrmDevice = drmDevice;
    mWritebackConnector = mDrmDevice->AvailableWritebackConnector(displayId);
    if (mWritebackConnector == NULL) {
        ALOGI("writeback is not supported");
        return;
    }
    if (mWritebackConnector->writeback_fb_id().id() == 0 ||
        mWritebackConnector->writeback_out_fence().id() == 0) {
        ALOGE("%s: Writeback properties don't exit", __func__);
        mWritebackConnector = NULL;
        return;
    }

    if (mWritebackConnector->writeback_pixel_formats().id()) {
        int32_t ret = NO_ERROR;
        uint64_t blobId;
        std::tie(ret, blobId) = mWritebackConnector->writeback_pixel_formats().value();
        if (ret) {
            ALOGE("Fail to get blob id for writeback_pixel_formats");
            return;
        }
        drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(mDrmDevice->fd(), blobId);
        if (!blob) {
            ALOGE("Fail to get blob for writeback_pixel_formats(%" PRId64 ")", blobId);
            return;
        }
        uint32_t formatNum = (blob->length)/sizeof(uint32_t);
        uint32_t *formats = (uint32_t *)blob->data;
        for (uint32_t i = 0; i < formatNum; i++) {
            int halFormat = drmFormatToHalFormat(formats[i]);
            ALOGD("supported writeback format[%d] %4.4s, %d", i, (char *)&formats[i], halFormat);
            if (halFormat != HAL_PIXEL_FORMAT_EXYNOS_UNDEFINED)
                mSupportedFormats.push_back(halFormat);
        }
        drmModeFreePropertyBlob(blob);
    }

    /* TODO (b/149043754):
     * Get dataspace form display driver
     */
    mSupportedDataspaces.push_back(HAL_DATASPACE_V0_SRGB);
}

void ExynosDisplayDrmInterface::DrmReadbackInfo::pickFormatDataspace(int32_t colorMode)
{
    /* TODO (b/149043754): This should be set according to color mode */
    if (!mSupportedFormats.empty())
        mReadbackFormat = mSupportedFormats[0];
    if (!mSupportedDataspaces.empty())
        mReadbackDataspace = mSupportedDataspaces[0];
}
