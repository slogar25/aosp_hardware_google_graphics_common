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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "ExynosPrimaryDisplay.h"

#include <fstream>

#include "ExynosDevice.h"
#include "ExynosDisplayDrmInterface.h"
#include "ExynosDisplayDrmInterfaceModule.h"
#include "ExynosExternalDisplay.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWCHelper.h"

#include <linux/fb.h>

extern struct exynos_hwc_control exynosHWCControl;

using namespace SOC_VERSION;

static const std::map<const DisplayType, const std::string> panelSysfsPath =
        {{DisplayType::DISPLAY_PRIMARY, "/sys/devices/platform/exynos-drm/primary-panel/"},
         {DisplayType::DISPLAY_SECONDARY, "/sys/devices/platform/exynos-drm/secondary-panel/"}};

static std::string loadPanelGammaCalibration(const std::string &file) {
    std::ifstream ifs(file);

    if (!ifs.is_open()) {
        ALOGW("Unable to open gamma calibration '%s', error = %s", file.c_str(), strerror(errno));
        return {};
    }

    std::string raw_data, gamma;
    char ch;
    while (std::getline(ifs, raw_data, '\r')) {
        gamma.append(raw_data);
        gamma.append(1, ' ');
        ifs.get(ch);
        if (ch != '\n') {
            gamma.append(1, ch);
        }
    }
    ifs.close();

    /* eliminate space character in the last byte */
    if (!gamma.empty()) {
        gamma.pop_back();
    }

    return gamma;
}

ExynosPrimaryDisplay::ExynosPrimaryDisplay(uint32_t index, ExynosDevice *device)
    :   ExynosDisplay(index, device)
{
    // TODO : Hard coded here
    mNumMaxPriorityAllowed = 5;

    /* Initialization */
    mType = HWC_DISPLAY_PRIMARY;
    mIndex = index;
    mDisplayId = getDisplayId(mType, mIndex);

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

    static_assert(sizeof(BRIGHTNESS_NODE_0_BASE) != 0 && sizeof(MAX_BRIGHTNESS_NODE_0_BASE) != 0,
                  "Invalid brightness 0 node");
    static_assert(sizeof(BRIGHTNESS_NODE_1_BASE) != 0 && sizeof(MAX_BRIGHTNESS_NODE_1_BASE) != 0,
                  "Invalid brightness 1 node");
    std::string brightness_node;
    std::string max_brightness_node;
    switch (mIndex) {
        case 0:
            max_brightness_node = MAX_BRIGHTNESS_NODE_0_BASE;
            brightness_node = BRIGHTNESS_NODE_0_BASE;
            break;
        case 1:
            max_brightness_node = MAX_BRIGHTNESS_NODE_1_BASE;
            brightness_node = BRIGHTNESS_NODE_1_BASE;
            break;
        default:
            ALOGE("assgin brightness node failed (mIndex: %d)", mIndex);
            break;
    }

    FILE *maxBrightnessFd = fopen(max_brightness_node.c_str(), "r");
    ALOGI("Trying %s open for get max brightness", max_brightness_node.c_str());

    if (maxBrightnessFd != NULL) {
        char val[MAX_BRIGHTNESS_LEN] = {0};
        size_t size = fread(val, 1, MAX_BRIGHTNESS_LEN, maxBrightnessFd);
        if (size) {
            mMaxBrightness = atoi(val);
            ALOGI("Max brightness : %d", mMaxBrightness);

            mBrightnessFd = fopen(brightness_node.c_str(), "w+");
            ALOGI("Trying %s open for brightness control", brightness_node.c_str());

            if (mBrightnessFd == NULL)
                ALOGE("%s open failed! %s", brightness_node.c_str(), strerror(errno));

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

#if defined EARLY_WAKUP_NODE_BASE
    mEarlyWakeupFd = fopen(EARLY_WAKUP_NODE_BASE, "w");
    if (mEarlyWakeupFd == NULL)
        ALOGE("%s open failed! %s", EARLY_WAKUP_NODE_BASE, strerror(errno));
#endif

    mLhbmFd = fopen(kLocalHbmModeFileNode, "w+");
    if (mLhbmFd == nullptr) ALOGE("local hbm mode node open failed! %s", strerror(errno));

    mWakeupDispFd = fopen(kWakeupDispFilePath, "w");
    if (mWakeupDispFd == nullptr) ALOGE("wake up display node open failed! %s", strerror(errno));
}

ExynosPrimaryDisplay::~ExynosPrimaryDisplay()
{
    if (mWakeupDispFd) {
        fclose(mWakeupDispFd);
        mWakeupDispFd = nullptr;
    }

    if (mLhbmFd) {
        fclose(mLhbmFd);
        mLhbmFd = nullptr;
    }

    if (mBrightnessFd) {
        fclose(mBrightnessFd);
        mBrightnessFd = nullptr;
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

int32_t ExynosPrimaryDisplay::doDisplayConfigInternal(hwc2_config_t config) {
    if (mPowerModeState != HWC2_POWER_MODE_ON) {
        mPendActiveConfig = config;
        mConfigRequestState = hwc_request_state_t::SET_CONFIG_STATE_NONE;
        DISPLAY_LOGI("%s:: Pending desired Config: %d", __func__, config);
        return NO_ERROR;
    }
    return ExynosDisplay::doDisplayConfigInternal(config);
}

int32_t ExynosPrimaryDisplay::getActiveConfigInternal(hwc2_config_t *outConfig) {
    if (outConfig && mPendActiveConfig != UINT_MAX) {
        *outConfig = mPendActiveConfig;
        return HWC2_ERROR_NONE;
    }
    return ExynosDisplay::getActiveConfigInternal(outConfig);
}

int32_t ExynosPrimaryDisplay::setActiveConfigInternal(hwc2_config_t config, bool force) {
    hwc2_config_t cur_config;

    getActiveConfigInternal(&cur_config);
    if (cur_config == config) {
        ALOGI("%s:: Same display config is set", __func__);
        return HWC2_ERROR_NONE;
    }
    if (mPowerModeState != HWC2_POWER_MODE_ON) {
        mPendActiveConfig = config;
        return HWC2_ERROR_NONE;
    }
    return ExynosDisplay::setActiveConfigInternal(config, force);
}

int32_t ExynosPrimaryDisplay::applyPendingConfig() {
    hwc2_config_t config;

    if (mPendActiveConfig != UINT_MAX) {
        config = mPendActiveConfig;
        mPendActiveConfig = UINT_MAX;
    } else {
        getActiveConfigInternal(&config);
    }

    return ExynosDisplay::setActiveConfigInternal(config, true);
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

    if (mFirstPowerOn) {
        firstPowerOn();
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosPrimaryDisplay::setPowerOff() {
    ATRACE_CALL();

    clearDisplay(true);

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

int32_t ExynosPrimaryDisplay::setPowerDoze(hwc2_power_mode_t mode) {
    ATRACE_CALL();

    if (!mDisplayInterface->isDozeModeAvailable()) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if ((mPowerModeState == HWC2_POWER_MODE_OFF) || (mPowerModeState == HWC2_POWER_MODE_ON)) {
        if (mDisplayInterface->setLowPowerMode()) {
            ALOGI("Not support LP mode.");
            return HWC2_ERROR_UNSUPPORTED;
        }
    }

    mPowerModeState = mode;

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
        case HWC2_POWER_MODE_DOZE:
            return setPowerDoze(static_cast<hwc2_power_mode_t>(mode));
        case HWC2_POWER_MODE_OFF:
            setPowerOff();
            break;
        case HWC2_POWER_MODE_ON:
            setPowerOn();
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    ExynosDisplay::updateRefreshRateHint();

    return HWC2_ERROR_NONE;
}

void ExynosPrimaryDisplay::firstPowerOn() {
    SetCurrentPanelGammaSource(DisplayType::DISPLAY_PRIMARY, PanelGammaSource::GAMMA_CALIBRATION);
    mFirstPowerOn = false;
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

std::string ExynosPrimaryDisplay::getPanelSysfsPath(const DisplayType &type) {
    if ((type < DisplayType::DISPLAY_PRIMARY) || (type >= DisplayType::DISPLAY_MAX)) {
        ALOGE("Invalid display panel type %d", type);
        return {};
    }

    auto iter = panelSysfsPath.find(type);
    if (iter == panelSysfsPath.end()) {
        return {};
    }

    return iter->second;
}

int32_t ExynosPrimaryDisplay::SetCurrentPanelGammaSource(const DisplayType type,
                                                         const PanelGammaSource &source) {
    std::string &&panel_sysfs_path = getPanelSysfsPath(type);
    if (panel_sysfs_path.empty()) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::ifstream ifs;
    std::string &&path = panel_sysfs_path + "panel_name";
    ifs.open(path, std::ifstream::in);
    if (!ifs.is_open()) {
        ALOGW("Unable to access panel name path '%s' (%s)", path.c_str(), strerror(errno));
        return HWC2_ERROR_UNSUPPORTED;
    }
    std::string panel_name;
    std::getline(ifs, panel_name);
    ifs.close();

    path = panel_sysfs_path + "serial_number";
    ifs.open(path, std::ifstream::in);
    if (!ifs.is_open()) {
        ALOGW("Unable to access panel id path '%s' (%s)", path.c_str(), strerror(errno));
        return HWC2_ERROR_UNSUPPORTED;
    }
    std::string panel_id;
    std::getline(ifs, panel_id);
    ifs.close();

    std::string gamma_node = panel_sysfs_path + "gamma";
    if (access(gamma_node.c_str(), W_OK)) {
        ALOGW("Unable to access panel gamma calibration node '%s' (%s)", gamma_node.c_str(),
              strerror(errno));
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::string &&gamma_data = "default";
    if (source == PanelGammaSource::GAMMA_CALIBRATION) {
        std::string gamma_cal_file(kDisplayCalFilePath);
        gamma_cal_file.append(kPanelGammaCalFilePrefix)
                .append(1, '_')
                .append(panel_name)
                .append(1, '_')
                .append(panel_id)
                .append(".cal");
        if (access(gamma_cal_file.c_str(), R_OK)) {
            ALOGI("Fail to access `%s` (%s), try golden gamma calibration", gamma_cal_file.c_str(),
                  strerror(errno));
            gamma_cal_file = kDisplayCalFilePath;
            gamma_cal_file.append(kPanelGammaCalFilePrefix)
                    .append(1, '_')
                    .append(panel_name)
                    .append(".cal");
        }
        gamma_data = loadPanelGammaCalibration(gamma_cal_file);
    }

    if (gamma_data.empty()) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::ofstream ofs(gamma_node);
    if (!ofs.is_open()) {
        ALOGW("Unable to open gamma node '%s', error = %s", gamma_node.c_str(), strerror(errno));
        return HWC2_ERROR_UNSUPPORTED;
    }
    ofs.write(gamma_data.c_str(), gamma_data.size());
    ofs.close();

    currentPanelGammaSource = source;
    return HWC2_ERROR_NONE;
}

int32_t ExynosPrimaryDisplay::setLhbmState(bool enabled) {
    requestLhbm(enabled);
    ALOGI("setLhbmState =%d", enabled);

    std::unique_lock<std::mutex> lk(lhbm_mutex_);
    mLhbmChanged = false;
    if (!lhbm_cond_.wait_for(lk, std::chrono::milliseconds(1000),
                             [this] { return mLhbmChanged; })) {
        ALOGI("setLhbmState =%d timeout !", enabled);
        return TIMED_OUT;
    } else {
        if (enabled)
            mDisplayInterface->waitVBlank();
        return NO_ERROR;
    }
}

bool ExynosPrimaryDisplay::getLhbmState() {
    return mLhbmOn;
}

void ExynosPrimaryDisplay::notifyLhbmState(bool enabled) {
    std::lock_guard<std::mutex> lk(lhbm_mutex_);
    mLhbmChanged = true;
    lhbm_cond_.notify_one();
    mLhbmOn = enabled;
}

void ExynosPrimaryDisplay::setWakeupDisplay() {
    if (mWakeupDispFd) {
        writeFileNode(mWakeupDispFd, 1);
    }
}
