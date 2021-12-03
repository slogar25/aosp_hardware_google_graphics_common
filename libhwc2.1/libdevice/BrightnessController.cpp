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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include <cutils/properties.h>
#include <poll.h>

#include "BrightnessController.h"
#include "ExynosHWCModule.h"

BrightnessController::BrightnessController(int32_t panelIndex) :
          mPanelIndex(panelIndex),
          mEnhanceHbmReq(false),
          mLhbmReq(false),
          mBrightnessFloatReq(-1),
          mBrightnessLevel(0),
          mGhbm(HbmMode::OFF),
          mDimming(false),
          mLhbm(false),
          mHdrFullScreen(false) {
    initBrightnessSysfs();
}

int BrightnessController::initDrm(const DrmDevice& drmDevice,
                                  const DrmConnector& connector) {
    initBrightnessTable(drmDevice, connector);

    mBrightnessDimmingUsage = static_cast<BrightnessDimmingUsage>(
        property_get_int32("vendor.display.brightness.dimming.usage", 0));
    mHbmDimmingTimeUs =
        property_get_int32("vendor.display.brightness.dimming.hbm_time", kHbmDimmingTimeUs);

    mLhbmSupported = connector.lhbm_on().id() != 0;
    mGhbmSupported = connector.hbm_mode().id() != 0;
    return NO_ERROR;
}

void BrightnessController::initBrightnessSysfs() {
    String8 nodeName;
    nodeName.appendFormat(BRIGHTNESS_SYSFS_NODE, mPanelIndex);
    mBrightnessOfs.open(nodeName.string(), std::ofstream::out);
    if (mBrightnessOfs.fail()) {
        ALOGE("%s %s fail to open", __func__, nodeName.string());
        mBrightnessOfs.close();
        return;
    }

    nodeName.clear();
    nodeName.appendFormat(MAX_BRIGHTNESS_SYSFS_NODE, mPanelIndex);

    std::ifstream ifsMaxBrightness(nodeName.string());
    if (ifsMaxBrightness.fail()) {
        ALOGE("%s fail to open %s", __func__, nodeName.string());
        return;
    }

    ifsMaxBrightness >> mMaxBrightness;
    ifsMaxBrightness.close();
}

void BrightnessController::initBrightnessTable(const DrmDevice& drmDevice,
                                               const DrmConnector& connector) {
    if (connector.brightness_cap().id() == 0) {
        ALOGD("the brightness_cap is not supported");
        return;
    }

    const auto [ret, blobId] = connector.brightness_cap().value();
    if (ret) {
        ALOGE("Fail to get brightness_cap (ret = %d)", ret);
        return;
    }

    if (blobId == 0) {
        ALOGE("the brightness_cap is supported but blob is not valid");
        return;
    }

    drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(drmDevice.fd(), blobId);
    if (blob == nullptr) {
        ALOGE("Fail to get brightness_cap blob");
        return;
    }

    const struct brightness_capability *cap =
            reinterpret_cast<struct brightness_capability *>(blob->data);
    mBrightnessTable[toUnderlying(BrightnessRange::NORMAL)] = BrightnessTable(cap->normal);
    mBrightnessTable[toUnderlying(BrightnessRange::HBM)] = BrightnessTable(cap->hbm);

    drmModeFreePropertyBlob(blob);

    parseHbmModeEnums(connector.hbm_mode());

    mBrightnessIntfSupported = true;
}

int BrightnessController::processEnhancedHbm(bool on) {
    if (!mGhbmSupported) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::lock_guard<std::mutex> lock(mBrightnessMutex);
    mEnhanceHbmReq.store(on);
    if (mEnhanceHbmReq.is_dirty()) {
        updateStates();
    }
    return NO_ERROR;
}

int BrightnessController::processDisplayBrightness(float brightness,
                                                   std::function<void(void)> refresh) {
    uint32_t level;
    bool ghbm;

    if (brightness < -1.0f || brightness > 1.0f) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    ATRACE_CALL();
    if (!mBrightnessIntfSupported) {
        level = brightness < 0 ? 0 : static_cast<uint32_t>(brightness * mMaxBrightness + 0.5f);
        return applyBrightnessViaSysfs(level);
    }

    {
        std::lock_guard<std::mutex> lock(mBrightnessMutex);
        mBrightnessFloatReq.store(brightness);
        if (!mBrightnessFloatReq.is_dirty()) {
            return NO_ERROR;
        }

        if (mGhbmSupported) {
            if (queryBrightness(brightness, &ghbm, &level)) {
                ALOGE("%s failed to convert brightness %f", __func__, brightness);
                return -EINVAL;
            }
            // check if this will cause a hbm transition
            if ((mGhbm.get() != HbmMode::OFF) != ghbm) {
                // this brightness change will go drm path
                updateStates();
                refresh(); // force next frame to update brightness
                return NO_ERROR;
            }
        } else {
            level = brightness < 0 ? 0 : static_cast<uint32_t>(brightness * mMaxBrightness + 0.5f);
        }
        // go sysfs path
    }

    // Sysfs path is faster than drm path. If there is an unchecked drm path change, the sysfs
    // path should check the sysfs content.
    if (mUncheckedGbhmRequest) {
        ATRACE_NAME("check_ghbm_mode");
        // check ghbm sysfs
        char status = static_cast<char>(static_cast<uint32_t>(mPendingGhbmStatus.load()) + '0');
        checkSysfsStatus(kGlobalHbmModeFileNode, status, ms2ns(200));
        mUncheckedGbhmRequest = false;
    }

    if (mUncheckedLhbmRequest) {
        ATRACE_NAME("check_lhbm_mode");
        // check lhbm sysfs
        checkSysfsStatus(kLocalHbmModeFileNode, mPendingLhbmStatus ? '1' : '0', ms2ns(200));
        mUncheckedLhbmRequest = false;
    }

    return applyBrightnessViaSysfs(level);
}

int BrightnessController::processLocalHbm(bool on) {
    if (!mLhbmSupported) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::lock_guard<std::mutex> lock(mBrightnessMutex);
    mLhbmReq.store(on);
    if (mLhbmReq.is_dirty()) {
        updateStates();
    }

    return NO_ERROR;
}

int BrightnessController::processInstantHbm(bool on) {
    if (!mGhbmSupported) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::lock_guard<std::mutex> lock(mBrightnessMutex);
    mInstantHbmReq.store(on);
    if (mInstantHbmReq.is_dirty()) {
        updateStates();
    }
    return NO_ERROR;
}

float BrightnessController::getSdrDimRatioForInstantHbm() {
    if (!mBrightnessIntfSupported || !mGhbmSupported) {
        return 1.0f;
    }

    std::lock_guard<std::mutex> lock(mBrightnessMutex);
    if (!mInstantHbmReq.get()) {
        return 1.0f;
    }

    float sdr = 0;
    if (queryBrightness(mBrightnessFloatReq.get(), nullptr, nullptr, &sdr) != NO_ERROR) {
        return 1.0f;
    }

    float peak = mBrightnessTable[toUnderlying(BrightnessRange::MAX) - 1].mNitsEnd;
    if (sdr == 0 || peak == 0) {
        ALOGW("%s error luminance value sdr %f peak %f", __func__, sdr, peak);
        return 1.0f;
    }

    float ratio = sdr / peak;
    if (ratio < kGhbmMinDimRatio) {
        ALOGW("%s sdr dim ratio %f too small", __func__, ratio);
        ratio = kGhbmMinDimRatio;
    }

    return ratio;
}

void BrightnessController::onClearDisplay() {
    std::lock_guard<std::mutex> lock(mBrightnessMutex);
    mEnhanceHbmReq.reset(false);
    mLhbmReq.reset(false);
    mBrightnessFloatReq.reset(-1);
    mInstantHbmReq.reset(false);

    mBrightnessLevel.reset(0);
    mGhbm.reset(HbmMode::OFF);
    mDimming.reset(false);
    mLhbm.reset(false);

    mLhbmBrightnessAdj = false;
    mHbmSvDimming = false;
}

int BrightnessController::prepareFrameCommit(ExynosDisplay& display,
                              const DrmConnector& connector,
                              ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq,
                              bool &ghbmSync, bool &lhbmSync, bool &blSync) {
    int ret;

    ghbmSync = false;
    lhbmSync = false;
    blSync = false;

    ATRACE_CALL();
    std::lock_guard<std::mutex> lock(mBrightnessMutex);

    if (mDimming.is_dirty()) {
        if ((ret = drmReq.atomicAddProperty(connector.id(), connector.dimming_on(),
                                            mDimming.get())) < 0) {
            ALOGE("%s: Fail to set dimming_on property", __func__);
        }
        mDimming.clear_dirty();
    }

    if (mLhbm.is_dirty() && mLhbmSupported) {
        if ((ret = drmReq.atomicAddProperty(connector.id(), connector.lhbm_on(),
                                            mLhbm.get())) < 0) {
            ALOGE("%s: Fail to set lhbm_on property", __func__);
        } else {
            lhbmSync = true;
        }

        auto dbv = mBrightnessLevel.get();
        auto old_dbv = dbv;
        if (mLhbm.get()) {
            uint32_t dbv_adj = 0;
            if (display.getColorAdjustedDbv(dbv_adj)) {
                ALOGW("failed to get adjusted dbv");
            } else if (dbv_adj != dbv && dbv_adj != 0) {
                dbv_adj = std::clamp(dbv_adj,
                        mBrightnessTable[toUnderlying(BrightnessRange::NORMAL)].mBklStart,
                        mBrightnessTable[toUnderlying(BrightnessRange::NORMAL)].mBklEnd);

                ALOGI("lhbm: adjust dbv from %d to %d", dbv, dbv_adj);
                dbv = dbv_adj;
                mLhbmBrightnessAdj = (dbv != old_dbv);
            }
        }

        if (mLhbmBrightnessAdj) {
            // case 1: lhbm on and dbv is changed, use the new dbv
            // case 2: lhbm off and dbv was changed at lhbm on, use current dbv
            if ((ret = drmReq.atomicAddProperty(connector.id(),
                                               connector.brightness_level(), dbv)) < 0) {
                ALOGE("%s: Fail to set brightness_level property", __func__);
            } else {
                blSync = true;
            }
        }

        // mLhbmBrightnessAdj will last from LHBM on to off
        if (!mLhbm.get() && mLhbmBrightnessAdj) {
            mLhbmBrightnessAdj = false;
        }

        mLhbm.clear_dirty();
    }

    if (mGhbm.is_dirty() && mGhbmSupported) {
        HbmMode hbmMode = mGhbm.get();
        auto [hbmEnum, ret] = DrmEnumParser::halToDrmEnum(static_cast<int32_t>(hbmMode),
                                                          mHbmModeEnums);
        if (ret < 0) {
            ALOGE("Fail to convert hbm mode(%d)", hbmMode);
            return ret;
        }

        if ((ret = drmReq.atomicAddProperty(connector.id(), connector.hbm_mode(),
                                            hbmEnum)) < 0) {
            ALOGE("%s: Fail to set hbm_mode property", __func__);
        } else {
            ghbmSync = true;
        }
        mGhbm.clear_dirty();

        if (mBrightnessLevel.is_dirty()) {
            if ((ret = drmReq.atomicAddProperty(connector.id(),
                                                connector.brightness_level(),
                                                mBrightnessLevel.get())) < 0) {
                ALOGE("%s: Fail to set brightness_level property", __func__);
            } else {
                blSync = true;
            }

            mBrightnessLevel.clear_dirty();
        }
    }

    mHdrFullScreen.clear_dirty();
    return NO_ERROR;
}

// Process all requests to update states for next commit
int BrightnessController::updateStates() {
    bool ghbm;
    uint32_t level;
    float brightness = mInstantHbmReq.get() ? 1.0f : mBrightnessFloatReq.get();
    if (queryBrightness(brightness, &ghbm, &level)) {
        ALOGW("%s failed to convert brightness %f", __func__, mBrightnessFloatReq.get());
        return HWC2_ERROR_UNSUPPORTED;
    }

    mBrightnessLevel.store(level);
    mLhbm.store(mLhbmReq.get());

    // turn off irc for sun light visibility
    bool irc = !mEnhanceHbmReq.get();
    if (ghbm) {
        mGhbm.store(irc ? HbmMode::ON_IRC_ON : HbmMode::ON_IRC_OFF);
    } else {
        mGhbm.store(HbmMode::OFF);
    }

    if (mLhbm.is_dirty()) {
        // Next sysfs path should verify this change has been applied.
        mUncheckedLhbmRequest = true;
        mPendingLhbmStatus = mLhbm.get();
    }
    if (mGhbm.is_dirty()) {
        // Next sysfs path should verify this change has been applied.
        mUncheckedGbhmRequest = true;
        mPendingGhbmStatus = mGhbm.get();
    }

    bool dimming = !mInstantHbmReq.get();
    switch (mBrightnessDimmingUsage) {
        case BrightnessDimmingUsage::HBM:
            // turn on dimming at HBM on/off
            // turn off dimming after mHbmDimmingTimeUs or there is an instant hbm on/off
            if (mGhbm.is_dirty()) {
                gettimeofday(&mHbmDimmingStart, NULL);
                if (!mHdrFullScreen.is_dirty()) {
                    mHbmSvDimming = true;
                }
            }

            if (mHbmSvDimming) {
                struct timeval curr_time;
                gettimeofday(&curr_time, NULL);
                curr_time.tv_usec += (curr_time.tv_sec - mHbmDimmingStart.tv_sec) * 1000000;
                long duration = curr_time.tv_usec - mHbmDimmingStart.tv_usec;
                if (duration > mHbmDimmingTimeUs)
                    mHbmSvDimming = false;
            }
            dimming = dimming && (mHbmSvDimming);
            break;

        case BrightnessDimmingUsage::NONE:
            dimming = false;
            break;

        default:
            break;
    }
    mDimming.store(dimming);

    mEnhanceHbmReq.clear_dirty();
    mLhbmReq.clear_dirty();
    mBrightnessFloatReq.clear_dirty();
    mInstantHbmReq.clear_dirty();

    ALOGI("level=%d, DimmingOn=%d, Hbm=%d, LhbmOn=%d.", mBrightnessLevel.get(),
          mDimming.get(), mGhbm.get(), mLhbm.get());
    return NO_ERROR;
}

int BrightnessController::queryBrightness(float brightness, bool *ghbm, uint32_t *level,
                                               float *nits) {
    if (!mBrightnessIntfSupported) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (brightness < 0) {
        // screen off
        if (ghbm) {
            *ghbm = false;
        }
        if (level) {
            *level = 0;
        }
        if (nits) {
            *nits = 0;
        }
        return NO_ERROR;
    }

    for (uint32_t i = 0; i < toUnderlying(BrightnessRange::MAX); ++i) {
        if (brightness <= mBrightnessTable[i].mBriEnd) {
            if (ghbm) {
                *ghbm = (i == toUnderlying(BrightnessRange::HBM));
            }

            if (level || nits) {
                auto fSpan = mBrightnessTable[i].mBriEnd - mBrightnessTable[i].mBriStart;
                auto norm = fSpan == 0 ? 1 : (brightness - mBrightnessTable[i].mBriStart) / fSpan;

                if (level) {
                    auto iSpan = mBrightnessTable[i].mBklEnd - mBrightnessTable[i].mBklStart;
                    auto bl = norm * iSpan + mBrightnessTable[i].mBklStart;
                    *level = static_cast<uint32_t>(bl + 0.5);
                }

                if (nits) {
                    auto nSpan = mBrightnessTable[i].mNitsEnd - mBrightnessTable[i].mNitsStart;
                    *nits = norm * nSpan + mBrightnessTable[i].mNitsStart;
                }
            }

            return NO_ERROR;
        }
    }

    return -EINVAL;
}

// Return immediately if it's already in the status. Otherwise poll the status
int BrightnessController::checkSysfsStatus(const char *file, char status, nsecs_t timeoutNs) {
    ATRACE_CALL();
    char buf;
    auto startTime = systemTime(SYSTEM_TIME_MONOTONIC);

    String8 nodeName;
    nodeName.appendFormat(file, mPanelIndex);
    UniqueFd fd = open(nodeName.string(), O_RDONLY);

    int size = read(fd.get(), &buf, 1);
    if (size != 1) {
        ALOGE("%s failed to read from %s", __func__, kLocalHbmModeFileNode);
        return false;
    }

    if (buf == status) {
        return true;
    }

    struct pollfd pfd;
    int ret = EINVAL;

    pfd.fd = fd.get();
    pfd.events = POLLPRI;
    while (true) {
        auto currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
        // int64_t for nsecs_t
        auto remainTimeNs = timeoutNs - (currentTime - startTime);
        if (remainTimeNs <= 0) {
            remainTimeNs = ms2ns(1);
        }
        int pollRet = poll(&pfd, 1, ns2ms(remainTimeNs));
        if (pollRet == 0) {
            ALOGW("%s poll timeout", __func__);
            // time out
            ret = ETIMEDOUT;
            break;
        } else if (pollRet > 0) {
            if (!(pfd.revents & POLLPRI)) {
                continue;
            }

            lseek(fd.get(), 0, SEEK_SET);
            size = read(fd.get(), &buf, 1);
            if (size == 1) {
                if (buf == status) {
                    ret = 0;
                } else {
                    ALOGE("%s status %c expected %c after notified", __func__, buf, status);
                    ret = EINVAL;
                }
            } else {
                ret = EIO;
                ALOGE("%s failed to read after notified %d", __func__, errno);
            }
            break;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }

            ALOGE("%s poll failed %d", __func__, errno);
            ret = errno;
            break;
        }
    };

    return ret == NO_ERROR;
}

int BrightnessController::applyBrightnessViaSysfs(uint32_t level) {
    if (mBrightnessOfs.is_open()) {
        mBrightnessOfs.seekp(std::ios_base::beg);
        mBrightnessOfs << std::to_string(level);
        mBrightnessOfs.flush();
        if (mBrightnessOfs.fail()) {
            ALOGE("%s fail to write brightness %d", __func__, level);
            mBrightnessOfs.clear();
            return HWC2_ERROR_NO_RESOURCES;
        }

        {
            std::lock_guard<std::mutex> lock(mBrightnessMutex);
            mBrightnessLevel.reset(level);
            ALOGI("level=%d, DimmingOn=%d, Hbm=%d, LhbmOn=%d", level,
                  mDimming.get(), mGhbm.get(), mLhbm.get());
        }
        return NO_ERROR;
    }

    return HWC2_ERROR_UNSUPPORTED;
}

void BrightnessController::parseHbmModeEnums(const DrmProperty &property) {
    const std::vector<std::pair<uint32_t, const char *>> modeEnums = {
            {static_cast<uint32_t>(HbmMode::OFF), "Off"},
            {static_cast<uint32_t>(HbmMode::ON_IRC_ON), "On IRC On"},
            {static_cast<uint32_t>(HbmMode::ON_IRC_OFF), "On IRC Off"},
    };

    DrmEnumParser::parseEnums(property, modeEnums, mHbmModeEnums);
    for (auto &e : mHbmModeEnums) {
        ALOGD("hbm mode [hal: %d, drm: %" PRId64 ", %s]", e.first, e.second,
              modeEnums[e.first].second);
    }
}

void BrightnessController::dump(String8 &result) {
    std::lock_guard<std::mutex> lock(mBrightnessMutex);

    result.appendFormat("BrightnessController:\n");
    result.appendFormat("\tsysfs support %d, max %d, valid brightness table %d, "
                        "lhbm supported %d, ghbm supported %d\n", mBrightnessOfs.is_open(),
                        mMaxBrightness, mBrightnessIntfSupported, mLhbmSupported, mGhbmSupported);
    result.appendFormat("\trequests: enhance hbm %d, lhbm %d, "
                        "brightness %f, instant hbm %d\n",
                        mEnhanceHbmReq.get(), mLhbmReq.get(), mBrightnessFloatReq.get(),
                        mInstantHbmReq.get());
    result.appendFormat("\tstates: brighntess level %d, ghbm %d, dimming %d, lhbm %d\n",
                        mBrightnessLevel.get(), mGhbm.get(), mDimming.get(), mLhbm.get());
    result.appendFormat("\thdr full screen %d, unchecked lhbm request %d(%d), "
                        "unchecked ghbm request %d(%d)\n",
                        mHdrFullScreen.get(), mUncheckedLhbmRequest.load(),
                        mPendingLhbmStatus.load(), mUncheckedGbhmRequest.load(),
                        mPendingGhbmStatus.load());
    result.appendFormat("\tdimming usage %d, hbm sv dimming %d, time us %d, start (%ld, %ld)\n",
                        mBrightnessDimmingUsage, mHbmSvDimming, mHbmDimmingTimeUs,
                        mHbmDimmingStart.tv_sec, mHbmDimmingStart.tv_usec);
    result.appendFormat("\n\n");
}
