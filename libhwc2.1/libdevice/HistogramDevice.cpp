/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "HistogramDevice.h"

#include <drm/samsung_drm.h>

#include <sstream>
#include <string>

#include "ExynosDisplayDrmInterface.h"
#include "ExynosHWCHelper.h"
#include "android-base/macros.h"

/**
 * histogramOnBinderDied
 *
 * The binderdied callback function which is registered in registerHistogram and would trigger
 * unregisterHistogram to cleanup the resources.
 *
 * @cookie pointer to the TokenInfo of the binder object.
 */
static void histogramOnBinderDied(void* cookie) {
    HistogramDevice::HistogramErrorCode histogramErrorCode;
    HistogramDevice::TokenInfo* tokenInfo = (HistogramDevice::TokenInfo*)cookie;
    ATRACE_NAME(String8::format("%s pid=%d", __func__, tokenInfo->mPid).c_str());
    ALOGI("%s: process %d with token(%p) is died", __func__, tokenInfo->mPid,
          tokenInfo->mToken.get());

    // release the histogram resources
    tokenInfo->mHistogramDevice->unregisterHistogram(tokenInfo->mToken, &histogramErrorCode);
    if (histogramErrorCode != HistogramDevice::HistogramErrorCode::NONE) {
        ALOGW("%s: failed to unregisterHistogram, error(%s)", __func__,
              aidl::com::google::hardware::pixel::display::toString(histogramErrorCode).c_str());
    }
}

HistogramDevice::HistogramDevice(ExynosDisplay* const display, const uint8_t channelCount,
                                 const std::vector<uint8_t> reservedChannels)
      : mDisplay(display) {
    // TODO: b/295786065 - Get available channels from crtc property.
    initChannels(channelCount, reservedChannels);

    // Create the death recipient which will be deleted in the destructor
    mDeathRecipient = AIBinder_DeathRecipient_new(histogramOnBinderDied);
}

HistogramDevice::~HistogramDevice() {
    if (mDeathRecipient) {
        AIBinder_DeathRecipient_delete(mDeathRecipient);
    }
}

void HistogramDevice::initDrm(DrmDevice& device, const DrmCrtc& crtc) {
    // TODO: b/295786065 - Get available channels from crtc property.
    ATRACE_NAME("HistogramDevice::initDrm");

    {
        std::unique_lock<std::mutex> lock(mInitDrmDoneMutex);
        ::android::base::ScopedLockAssertion lock_assertion(mInitDrmDoneMutex);
        ATRACE_NAME("mInitDrmDoneMutex");
        if (mInitDrmDone) {
            HIST_LOG(W, "should be called only once, ignore!");
            return;
        }

        initHistogramCapability(crtc.histogram_channel_property(0).id() != 0);
        mDrmDevice = &device;
        mInitDrmDone = true;
        mInitDrmDone_cv.notify_all();
    }

    // print the histogram capability
    String8 logString;
    dumpHistogramCapability(logString);
    ALOGI("%s", logString.c_str());
    HIST_LOG(D, "successfully");
}

bool HistogramDevice::waitInitDrmDone() {
    ATRACE_CALL();
    std::unique_lock<std::mutex> lock(mInitDrmDoneMutex);
    ::android::base::ScopedLockAssertion lock_assertion(mInitDrmDoneMutex);

    mInitDrmDone_cv.wait_for(lock, std::chrono::milliseconds(50), [this]() -> bool {
        ::android::base::ScopedLockAssertion lock_assertion(mInitDrmDoneMutex);
        return mInitDrmDone;
    });

    return mInitDrmDone;
}

ndk::ScopedAStatus HistogramDevice::getHistogramCapability(
        HistogramCapability* histogramCapability) const {
    ATRACE_CALL();

    if (!histogramCapability) {
        HIST_LOG(E, "binder error, histogramCapability is nullptr");
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }

    std::shared_lock lock(mHistogramCapabilityMutex);
    *histogramCapability = mHistogramCapability;

    return ndk::ScopedAStatus::ok();
}

#if defined(EXYNOS_HISTOGRAM_CHANNEL_REQUEST)
ndk::ScopedAStatus HistogramDevice::registerHistogram(const ndk::SpAIBinder& token,
                                                      const HistogramConfig& histogramConfig,
                                                      HistogramErrorCode* histogramErrorCode) {
    ATRACE_CALL();

    if (waitInitDrmDone() == false) {
        HIST_LOG(E, "initDrm is not completed yet");
        // TODO: b/323158344 - add retry error in HistogramErrorCode and return here.
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    {
        std::shared_lock lock(mHistogramCapabilityMutex);
        if (UNLIKELY(!mHistogramCapability.supportMultiChannel)) {
            HIST_LOG(E, "multi-channel interface is not supported");
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
    }

    ndk::ScopedAStatus binderStatus =
            validateHistogramRequest(token, histogramConfig, histogramErrorCode);
    if (!binderStatus.isOk() || *histogramErrorCode != HistogramErrorCode::NONE) {
        HIST_LOG(E, "validateHistogramRequest failed");
        return binderStatus;
    }

    // Create the histogram config blob if possible, early creation can reduce critical section
    int ret;
    const auto [displayActiveH, displayActiveV] = snapDisplayActiveSize();
    std::shared_ptr<PropertyBlob> drmConfigBlob;
    if ((ret = createDrmConfigBlob(histogramConfig, displayActiveH, displayActiveV, drmConfigBlob)))
        HIST_LOG(D, "createDrmConfigBlob failed, skip creation, ret(%d)", ret);

    bool needRefresh = false;

    {
        // Insert new client's token into mTokenInfoMap
        SCOPED_HIST_LOCK(mHistogramMutex);
        auto [it, emplaceResult] =
                mTokenInfoMap.try_emplace(token.get(), this, token, AIBinder_getCallingPid());
        if (!emplaceResult) {
            HIST_LOG(E, "BAD_TOKEN, token(%p) is already registered", token.get());
            *histogramErrorCode = HistogramErrorCode::BAD_TOKEN;
            return ndk::ScopedAStatus::ok();
        }
        auto tokenInfo = &it->second;

        /* In previous design, histogram client is attached to the histogram channel directly. Now
         * we use struct ConfigInfo to maintain the config metadata. We can benefit from this
         * design:
         * 1. More elegantly to change the applied config of the histogram channels (basics of
         *    virtualization).
         * 2. We may be able to share the same struct ConfigInfo for different histogram clients
         *    when the histogramConfigs via registerHistogram are the same. */
        auto& configInfo = tokenInfo->mConfigInfo;
        replaceConfigInfo(configInfo, &histogramConfig);

        // Attach the histogram drmConfigBlob to the configInfo
        if (drmConfigBlob)
            configInfo->mBlobsList.emplace_front(displayActiveH, displayActiveV, drmConfigBlob);

        needRefresh = scheduler();

        /* link the binder object (token) to the death recipient. When the binder object is
         * destructed, the callback function histogramOnBinderDied can release the histogram
         * resources automatically. */
        binder_status_t status;
        if ((status = AIBinder_linkToDeath(token.get(), mDeathRecipient, tokenInfo))) {
            /* Not return error due to the AIBinder_linkToDeath because histogram function can
             * still work */
            HIST_CH_LOG(E, configInfo->mChannelId, "token(%p): AIBinder_linkToDeath error, ret(%d)",
                        token.get(), status);
        }
    }

    if (needRefresh) {
        ATRACE_NAME("HistogramOnRefresh");
        mDisplay->mDevice->onRefresh(mDisplay->mDisplayId);
    }

    HIST_LOG(D, "register client successfully");

    return ndk::ScopedAStatus::ok();
}
#else
ndk::ScopedAStatus HistogramDevice::registerHistogram(const ndk::SpAIBinder& token,
                                                      const HistogramConfig& histogramConfig,
                                                      HistogramErrorCode* histogramErrorCode) {
    ATRACE_CALL();
    HIST_LOG(E, "multi-channel interface is not supported");
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}
#endif

ndk::ScopedAStatus HistogramDevice::queryHistogram(const ndk::SpAIBinder& token,
                                                   std::vector<char16_t>* histogramBuffer,
                                                   HistogramErrorCode* histogramErrorCode) {
    ATRACE_CALL();

    {
        std::shared_lock lock(mHistogramCapabilityMutex);
        if (UNLIKELY(!mHistogramCapability.supportMultiChannel)) {
            ALOGE("%s: histogram interface is not supported", __func__);
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
    }

    /* No need to validate the argument (token), if the token is not correct it cannot be converted
     * to the channel id later. */

    /* validate the argument (histogramBuffer) */
    if (!histogramBuffer) {
        ALOGE("%s: binder error, histogramBuffer is nullptr", __func__);
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }

    /* validate the argument (histogramErrorCode) */
    if (!histogramErrorCode) {
        ALOGE("%s: binder error, histogramErrorCode is nullptr", __func__);
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }

    /* default histogramErrorCode: no error */
    *histogramErrorCode = HistogramErrorCode::NONE;

    if (mDisplay->isPowerModeOff()) {
        ALOGW("%s: DISPLAY_POWEROFF, histogram is not available when display is off", __func__);
        *histogramErrorCode = HistogramErrorCode::DISPLAY_POWEROFF;
        return ndk::ScopedAStatus::ok();
    }

    if (mDisplay->isSecureContentPresenting()) {
        ALOGV("%s: DRM_PLAYING, histogram is not available when secure content is presenting",
              __func__);
        *histogramErrorCode = HistogramErrorCode::DRM_PLAYING;
        return ndk::ScopedAStatus::ok();
    }

    uint8_t channelId;

    /* Hold the mAllocatorMutex for a short time just to convert the token to channel id. Prevent
     * holding the mAllocatorMutex when waiting for the histogram data back which may takes several
     * milliseconds */
    {
        ATRACE_NAME("getChannelId");
        std::scoped_lock lock(mAllocatorMutex);
        if ((*histogramErrorCode = getChannelIdByTokenLocked(token, channelId)) !=
            HistogramErrorCode::NONE) {
            return ndk::ScopedAStatus::ok();
        }
    }

    getHistogramData(channelId, histogramBuffer, histogramErrorCode);

    /* Clear the histogramBuffer when error occurs */
    if (*histogramErrorCode != HistogramErrorCode::NONE) {
        histogramBuffer->assign(HISTOGRAM_BIN_COUNT, 0);
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HistogramDevice::reconfigHistogram(const ndk::SpAIBinder& token,
                                                      const HistogramConfig& histogramConfig,
                                                      HistogramErrorCode* histogramErrorCode) {
    ATRACE_CALL();

    {
        std::shared_lock lock(mHistogramCapabilityMutex);
        if (UNLIKELY(!mHistogramCapability.supportMultiChannel)) {
            HIST_LOG(E, "multi-channel interface is not supported");
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
    }

    ndk::ScopedAStatus binderStatus =
            validateHistogramRequest(token, histogramConfig, histogramErrorCode);
    if (!binderStatus.isOk() || *histogramErrorCode != HistogramErrorCode::NONE) {
        HIST_LOG(E, "validateHistogramRequest failed");
        return binderStatus;
    }

    // Create the histogram config blob if possible, early creation can reduce critical section
    int ret;
    const auto [displayActiveH, displayActiveV] = snapDisplayActiveSize();
    std::shared_ptr<PropertyBlob> drmConfigBlob;
    if ((ret = createDrmConfigBlob(histogramConfig, displayActiveH, displayActiveV, drmConfigBlob)))
        HIST_LOG(D, "createDrmConfigBlob failed, skip creation, ret(%d)", ret);

    bool needRefresh = false;

    {
        // Search the registered tokenInfo
        TokenInfo* tokenInfo = nullptr;
        SCOPED_HIST_LOCK(mHistogramMutex);
        if ((*histogramErrorCode = searchTokenInfo(token, tokenInfo)) != HistogramErrorCode::NONE) {
            HIST_LOG(E, "searchTokenInfo failed, error(%s)",
                     aidl::com::google::hardware::pixel::display::toString(*histogramErrorCode)
                             .c_str());
            return ndk::ScopedAStatus::ok();
        }

        // Change the histogram configInfo
        auto& configInfo = tokenInfo->mConfigInfo;
        replaceConfigInfo(configInfo, &histogramConfig);

        // Attach the histogram drmConfigBlob to the configInfo
        if (drmConfigBlob)
            configInfo->mBlobsList.emplace_front(displayActiveH, displayActiveV, drmConfigBlob);

        if (configInfo->mStatus == ConfigInfo::Status_t::HAS_CHANNEL_ASSIGNED) needRefresh = true;
    }

    if (needRefresh) {
        ATRACE_NAME("HistogramOnRefresh");
        mDisplay->mDevice->onRefresh(mDisplay->mDisplayId);
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HistogramDevice::unregisterHistogram(const ndk::SpAIBinder& token,
                                                        HistogramErrorCode* histogramErrorCode) {
    ATRACE_CALL();

    {
        std::shared_lock lock(mHistogramCapabilityMutex);
        if (UNLIKELY(!mHistogramCapability.supportMultiChannel)) {
            HIST_LOG(E, "multi-channel interface is not supported");
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
    }

    // validate the argument (histogramErrorCode)
    if (!histogramErrorCode) {
        HIST_LOG(E, "binder error, histogramErrorCode is nullptr");
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }

    // default histogramErrorCode: no error
    *histogramErrorCode = HistogramErrorCode::NONE;

    bool needRefresh = false;

    {
        // Search the registered tokenInfo
        TokenInfo* tokenInfo = nullptr;
        SCOPED_HIST_LOCK(mHistogramMutex);
        if ((*histogramErrorCode = searchTokenInfo(token, tokenInfo)) != HistogramErrorCode::NONE) {
            HIST_LOG(E, "searchTokenInfo failed, error(%s)",
                     aidl::com::google::hardware::pixel::display::toString(*histogramErrorCode)
                             .c_str());
            return ndk::ScopedAStatus::ok();
        }

        // Clear the histogram configInfo
        replaceConfigInfo(tokenInfo->mConfigInfo, nullptr);

        /*
         * If AIBinder is alive, the unregisterHistogram is triggered from the histogram client, and
         * we need to unlink the binder object from death notification. If AIBinder is already dead,
         * the unregisterHistogram is triggered from binderdied callback, no need to unlink here.
         */
        if (LIKELY(AIBinder_isAlive(token.get()))) {
            binder_status_t status;
            if ((status = AIBinder_unlinkToDeath(token.get(), mDeathRecipient, tokenInfo))) {
                // Not return error due to the AIBinder_unlinkToDeath
                HIST_LOG(E, "AIBinder_unlinkToDeath error for token(%p), ret(%d)", token.get(),
                         status);
            }
        }

        // Delete the corresponding TokenInfo after the binder object is already unlinked.
        mTokenInfoMap.erase(token.get());
        tokenInfo = nullptr;

        needRefresh = scheduler();
    }

    if (needRefresh) {
        ATRACE_NAME("HistogramOnRefresh");
        mDisplay->mDevice->onRefresh(mDisplay->mDisplayId);
    }

    HIST_LOG(D, "unregister client successfully");

    return ndk::ScopedAStatus::ok();
}

void HistogramDevice::handleDrmEvent(void* event) {
    int ret = NO_ERROR;
    uint8_t channelId;
    char16_t* buffer;

    if ((ret = parseDrmEvent(event, channelId, buffer))) {
        ALOGE("%s: failed to parseDrmEvent, ret %d", __func__, ret);
        return;
    }

    ATRACE_NAME(String8::format("handleHistogramDrmEvent #%u", channelId).c_str());
    if (channelId >= mChannels.size()) {
        ALOGE("%s: histogram channel #%u: invalid channelId", __func__, channelId);
        return;
    }

    ChannelInfo& channel = mChannels[channelId];
    std::unique_lock<std::mutex> lock(channel.histDataCollectingMutex);

    /* Check if the histogram channel is collecting the histogram data */
    if (channel.histDataCollecting == true) {
        std::memcpy(channel.histData, buffer, HISTOGRAM_BIN_COUNT * sizeof(char16_t));
        channel.histDataCollecting = false;
    } else {
        ALOGW("%s: histogram channel #%u: ignore the histogram channel event", __func__, channelId);
    }

    channel.histDataCollecting_cv.notify_all();
}

void HistogramDevice::prepareAtomicCommit(ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq) {
    ATRACE_NAME("HistogramAtomicCommit");

    ExynosDisplayDrmInterface* moduleDisplayInterface =
            static_cast<ExynosDisplayDrmInterface*>(mDisplay->mDisplayInterface.get());
    if (!moduleDisplayInterface) {
        HIST_LOG(E, "failed to send atomic commit, moduleDisplayInterface is NULL");
        return;
    }

    const auto [displayActiveH, displayActiveV] = snapDisplayActiveSize();
    SCOPED_HIST_LOCK(mHistogramMutex);

    // Loop through every used channel and set config blob if needed.
    for (auto it = mUsedChannels.begin(); it != mUsedChannels.end();) {
        uint8_t channelId = *it;
        ChannelInfo& channel = mChannels[channelId];

        if (channel.mStatus == ChannelStatus_t::CONFIG_COMMITTED ||
            channel.mStatus == ChannelStatus_t::CONFIG_PENDING) {
            std::shared_ptr<ConfigInfo> configInfo = channel.mConfigInfo.lock();
            if (!configInfo) {
                HIST_CH_LOG(E, channelId, "expired configInfo, review code!");
                it = cleanupChannelInfo(channelId);
                continue;
            }
            setChannelConfigBlob(drmReq, channelId, moduleDisplayInterface, displayActiveH,
                                 displayActiveV, configInfo);
        }

        ++it;
    }

    // Loop through every free channels and disable channel if needed
    for (auto it = mFreeChannels.begin(); it != mFreeChannels.end(); ++it) {
        uint8_t channelId = *it;
        ChannelInfo& channel = mChannels[channelId];
        if (channel.mStatus == ChannelStatus_t::DISABLE_PENDING)
            clearChannelConfigBlob(drmReq, channelId, moduleDisplayInterface);
    }
}

void HistogramDevice::postAtomicCommit() {
    SCOPED_HIST_LOCK(mHistogramMutex);

    // Atomic commit is success, loop through every channel and update the channel status
    for (uint8_t channelId = 0; channelId < mChannels.size(); ++channelId) {
        ChannelInfo& channel = mChannels[channelId];

        switch (channel.mStatus) {
            case ChannelStatus_t::CONFIG_BLOB_ADDED:
                channel.mStatus = ChannelStatus_t::CONFIG_COMMITTED;
                break;
            case ChannelStatus_t::DISABLE_BLOB_ADDED:
                channel.mStatus = ChannelStatus_t::DISABLED;
                break;
            default:
                break;
        }
    }
}

void HistogramDevice::dump(String8& result) const {
    {
        std::shared_lock lock(mHistogramCapabilityMutex);
        // Do not dump the Histogram Device if it is not supported.
        if (!mHistogramCapability.supportMultiChannel) {
            return;
        }
    }

    /* print the histogram capability */
    dumpHistogramCapability(result);

    result.appendFormat("\n");

    /* print the histogram channel info*/
    result.appendFormat("Histogram channel info:\n");
    for (uint8_t channelId = 0; channelId < mChannels.size(); ++channelId) {
        // TODO: b/294489887 - Use buildForMiniDump can eliminate the redundant rows.
        TableBuilder tb;
        const ChannelInfo& channel = mChannels[channelId];
        std::scoped_lock lock(channel.channelInfoMutex);
        tb.add("ID", (int)channelId);
        tb.add("status", toString(channel.status));
        tb.add("token", channel.token.get());
        tb.add("pid", channel.pid);
        tb.add("requestedRoi", toString(channel.requestedRoi));
        tb.add("workingRoi", toString(channel.workingConfig.roi));
        tb.add("requestedBlockRoi", toString(channel.requestedBlockingRoi));
        tb.add("workingBlockRoi",
               toString(channel.workingConfig.blockingRoi.value_or(DISABLED_ROI)));
        tb.add("threshold", channel.threshold);
        tb.add("weightRGB", toString(channel.workingConfig.weights));
        tb.add("samplePos",
               aidl::com::google::hardware::pixel::display::toString(
                       channel.workingConfig.samplePos));
        result.append(tb.build().c_str());
    }

    result.appendFormat("\n");
}

void HistogramDevice::initChannels(const uint8_t channelCount,
                                   const std::vector<uint8_t>& reservedChannels) {
    HIST_LOG(I, "init with %u channels", channelCount);

    SCOPED_HIST_LOCK(mHistogramMutex);
    mChannels.resize(channelCount);

    for (const uint8_t reservedChannelId : reservedChannels) {
        if (reservedChannelId < mChannels.size()) {
            mChannels[reservedChannelId].mStatus = ChannelStatus_t::RESERVED;
        } else {
            HIST_CH_LOG(W, reservedChannelId,
                        "invalid channel cannot be reserved (channelCount: %u)", channelCount);
        }
    }

    for (uint8_t channelId = 0; channelId < channelCount; ++channelId) {
        if (mChannels[channelId].mStatus == ChannelStatus_t::RESERVED) {
            HIST_CH_LOG(D, channelId, "channel reserved for driver");
            continue;
        }

        mFreeChannels.push_back(channelId);
    }
}

void HistogramDevice::initHistogramCapability(const bool supportMultiChannel) {
    ATRACE_CALL();
    uint8_t channelCount = 0;
    {
        SCOPED_HIST_LOCK(mHistogramMutex);
        channelCount = mChannels.size();
    }

    ExynosDisplayDrmInterface* moduleDisplayInterface =
            static_cast<ExynosDisplayDrmInterface*>(mDisplay->mDisplayInterface.get());

    SCOPED_HIST_LOCK(mHistogramCapabilityMutex);
    if (!moduleDisplayInterface) {
        HIST_LOG(E, "failed to get panel full resolution, moduleDisplayInterface is NULL");
        mHistogramCapability.fullResolutionWidth = 0;
        mHistogramCapability.fullResolutionHeight = 0;
    } else {
        mHistogramCapability.fullResolutionWidth =
                moduleDisplayInterface->getPanelFullResolutionHSize();
        mHistogramCapability.fullResolutionHeight =
                moduleDisplayInterface->getPanelFullResolutionVSize();
    }
    mHistogramCapability.channelCount = channelCount;
    mHistogramCapability.supportMultiChannel = supportMultiChannel;
    mHistogramCapability.supportSamplePosList.push_back(HistogramSamplePos::POST_POSTPROC);
    mHistogramCapability.supportBlockingRoi = false;
    initPlatformHistogramCapability();
}

void HistogramDevice::replaceConfigInfo(std::shared_ptr<ConfigInfo>& configInfo,
                                        const HistogramConfig* histogramConfig) {
    ATRACE_CALL();

    // Capture the old ConfigInfo reference
    std::shared_ptr<ConfigInfo> oldConfigInfo = configInfo;

    // Populate the new ConfigInfo object based on the histogramConfig pointer
    configInfo = (histogramConfig) ? std::make_shared<ConfigInfo>(*histogramConfig) : nullptr;

    if (!oldConfigInfo && !configInfo) {
        return;
    } else if (!oldConfigInfo && configInfo) { // Case #1: registerHistogram
        addConfigToInactiveList(configInfo);
    } else if (oldConfigInfo && configInfo) { // Case #2: reconfigHistogram
        if (oldConfigInfo->mStatus == ConfigInfo::Status_t::HAS_CHANNEL_ASSIGNED) {
            configInfo->mStatus = ConfigInfo::Status_t::HAS_CHANNEL_ASSIGNED;
            configInfo->mChannelId = oldConfigInfo->mChannelId;
            mChannels[configInfo->mChannelId].mStatus = ChannelStatus_t::CONFIG_PENDING;
            mChannels[configInfo->mChannelId].mConfigInfo = configInfo;
        } else if (oldConfigInfo->mStatus == ConfigInfo::Status_t::IN_INACTIVE_LIST) {
            configInfo->mStatus = ConfigInfo::Status_t::IN_INACTIVE_LIST;
            configInfo->mInactiveListIt = oldConfigInfo->mInactiveListIt;
            *(configInfo->mInactiveListIt) = configInfo;
        } else {
            addConfigToInactiveList(configInfo);
        }
    } else if (oldConfigInfo && !configInfo) { // Case #3: unregisterHistogram
        if (oldConfigInfo->mStatus == ConfigInfo::Status_t::HAS_CHANNEL_ASSIGNED)
            cleanupChannelInfo(oldConfigInfo->mChannelId);
        else if (oldConfigInfo->mStatus == ConfigInfo::Status_t::IN_INACTIVE_LIST)
            mInactiveConfigItList.erase(oldConfigInfo->mInactiveListIt);

        oldConfigInfo->mStatus = ConfigInfo::Status_t::INITIALIZED;
    }
}

HistogramDevice::HistogramErrorCode HistogramDevice::searchTokenInfo(const ndk::SpAIBinder& token,
                                                                     TokenInfo*& tokenInfo) {
    auto it = mTokenInfoMap.find(token.get());

    if (it == mTokenInfoMap.end()) {
        HIST_LOG(E, "BAD_TOKEN, token(%p) is not registered", token.get());
        tokenInfo = nullptr;
        return HistogramErrorCode::BAD_TOKEN;
    }

    tokenInfo = &it->second;
    return HistogramErrorCode::NONE;
}

std::list<std::weak_ptr<HistogramDevice::ConfigInfo>>::iterator HistogramDevice::swapInConfigInfo(
        std::shared_ptr<ConfigInfo>& configInfo) {
    // Acquire a free histogram channel, pdate used and free channels
    const uint8_t channelId = mFreeChannels.front();
    mFreeChannels.pop_front();
    mUsedChannels.insert(channelId);

    // update the ChannelInfo
    ChannelInfo& channel = mChannels[channelId];
    channel.mStatus = ChannelStatus_t::CONFIG_PENDING;
    channel.mConfigInfo = configInfo;

    // update the configInfo and the inactive list
    configInfo->mStatus = ConfigInfo::Status_t::HAS_CHANNEL_ASSIGNED;
    configInfo->mChannelId = channelId;
    auto it = mInactiveConfigItList.erase(configInfo->mInactiveListIt);
    configInfo->mInactiveListIt = mInactiveConfigItList.end();

    return it;
}

void HistogramDevice::addConfigToInactiveList(const std::shared_ptr<ConfigInfo>& configInfo) {
    configInfo->mChannelId = -1;
    configInfo->mStatus = ConfigInfo::Status_t::IN_INACTIVE_LIST;
    configInfo->mInactiveListIt =
            mInactiveConfigItList.emplace(mInactiveConfigItList.end(), configInfo);
}

bool HistogramDevice::scheduler() {
    ATRACE_CALL();

    bool needRefresh = false;

    for (auto it = mInactiveConfigItList.begin(); it != mInactiveConfigItList.end();) {
        if (mFreeChannels.empty()) break;

        auto configInfo = it->lock();
        if (!configInfo) {
            HIST_LOG(W, "find expired configInfo ptr in mInactiveConfigItList, review code!");
            it = mInactiveConfigItList.erase(it);
            continue;
        }

        // Requires an onRefresh call to apply the config change of the channel
        needRefresh = true;

        // Swap in the config
        it = swapInConfigInfo(configInfo);
    }

    return needRefresh;
}

void HistogramDevice::getHistogramData(uint8_t channelId, std::vector<char16_t>* histogramBuffer,
                                       HistogramErrorCode* histogramErrorCode) {
    ATRACE_NAME(String8::format("%s #%u", __func__, channelId).c_str());
    int32_t ret;
    ExynosDisplayDrmInterface* moduleDisplayInterface =
            static_cast<ExynosDisplayDrmInterface*>(mDisplay->mDisplayInterface.get());
    if (!moduleDisplayInterface) {
        *histogramErrorCode = HistogramErrorCode::BAD_HIST_DATA;
        ALOGE("%s: histogram channel #%u: BAD_HIST_DATA, moduleDisplayInterface is nullptr",
              __func__, channelId);
        return;
    }

    ChannelInfo& channel = mChannels[channelId];

    std::unique_lock<std::mutex> lock(channel.histDataCollectingMutex);

    /* Check if the previous queryHistogram is finished */
    if (channel.histDataCollecting) {
        *histogramErrorCode = HistogramErrorCode::BAD_HIST_DATA;
        ALOGE("%s: histogram channel #%u: BAD_HIST_DATA, previous %s not finished", __func__,
              channelId, __func__);
        return;
    }

    /* Send the ioctl request (histogram_channel_request_ioctl) which allocate the drm event and
     * send back the drm event with data when available. */
    if ((ret = moduleDisplayInterface->sendHistogramChannelIoctl(HistogramChannelIoctl_t::REQUEST,
                                                                 channelId)) != NO_ERROR) {
        *histogramErrorCode = HistogramErrorCode::BAD_HIST_DATA;
        ALOGE("%s: histogram channel #%u: BAD_HIST_DATA, sendHistogramChannelIoctl (REQUEST) "
              "error "
              "(%d)",
              __func__, channelId, ret);
        return;
    }
    channel.histDataCollecting = true;

    {
        ATRACE_NAME(String8::format("waitDrmEvent #%u", channelId).c_str());
        /* Wait until the condition variable is notified or timeout. */
        channel.histDataCollecting_cv.wait_for(lock, std::chrono::milliseconds(50),
                                               [this, &channel]() {
                                                   return (!mDisplay->isPowerModeOff() &&
                                                           !channel.histDataCollecting);
                                               });
    }

    /* If the histDataCollecting is not cleared, check the reason and clear the histogramBuffer.
     */
    if (channel.histDataCollecting) {
        if (mDisplay->isPowerModeOff()) {
            *histogramErrorCode = HistogramErrorCode::DISPLAY_POWEROFF;
            ALOGW("%s: histogram channel #%u: DISPLAY_POWEROFF, histogram is not available "
                  "when "
                  "display is off",
                  __func__, channelId);
        } else {
            *histogramErrorCode = HistogramErrorCode::BAD_HIST_DATA;
            ALOGE("%s: histogram channel #%u: BAD_HIST_DATA, no histogram channel event is "
                  "handled",
                  __func__, channelId);
        }

        /* Cancel the histogram data request */
        ALOGI("%s: histogram channel #%u: cancel histogram data request", __func__, channelId);
        if ((ret = moduleDisplayInterface
                           ->sendHistogramChannelIoctl(HistogramChannelIoctl_t::CANCEL,
                                                       channelId)) != NO_ERROR) {
            ALOGE("%s: histogram channel #%u: sendHistogramChannelIoctl (CANCEL) error (%d)",
                  __func__, channelId, ret);
        }

        channel.histDataCollecting = false;
        return;
    }

    if (mDisplay->isSecureContentPresenting()) {
        ALOGV("%s: histogram channel #%u: DRM_PLAYING, histogram is not available when secure "
              "content is presenting",
              __func__, channelId);
        *histogramErrorCode = HistogramErrorCode::DRM_PLAYING;
        return;
    }

    /* Copy the histogram data from histogram info to histogramBuffer */
    histogramBuffer->assign(channel.histData, channel.histData + HISTOGRAM_BIN_COUNT);
}

// TODO: b/295990513 - Remove the if defined after kernel prebuilts are merged.
#if defined(EXYNOS_HISTOGRAM_CHANNEL_REQUEST)
int HistogramDevice::parseDrmEvent(void* event, uint8_t& channelId, char16_t*& buffer) const {
    struct exynos_drm_histogram_channel_event* histogram_channel_event =
            (struct exynos_drm_histogram_channel_event*)event;
    channelId = histogram_channel_event->hist_id;
    buffer = (char16_t*)&histogram_channel_event->bins;
    return NO_ERROR;
}
#else
int HistogramDevice::parseDrmEvent(void* event, uint8_t& channelId, char16_t*& buffer) const {
    channelId = 0;
    buffer = nullptr;
    return INVALID_OPERATION;
}
#endif

std::set<const uint8_t>::iterator HistogramDevice::cleanupChannelInfo(const uint8_t channelId) {
    mChannels[channelId].mStatus = ChannelStatus_t::DISABLE_PENDING;
    mChannels[channelId].mConfigInfo.reset();
    mFreeChannels.push_back(channelId);
    return mUsedChannels.erase(mUsedChannels.find(channelId));
}

void HistogramDevice::setChannelConfigBlob(ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq,
                                           const uint8_t channelId,
                                           ExynosDisplayDrmInterface* const moduleDisplayInterface,
                                           const int displayActiveH, const int displayActiveV,
                                           const std::shared_ptr<ConfigInfo>& configInfo) {
    ATRACE_NAME(String8::format("%s(chan#%u)", __func__, channelId));
    ChannelInfo& channel = mChannels[channelId];
    int ret;
    bool isRRS = false;
    uint32_t blobId = getMatchBlobId(configInfo->mBlobsList, displayActiveH, displayActiveV, isRRS);

    // Early return for config blob already committed and no RRS occurs.
    if (channel.mStatus == ChannelStatus_t::CONFIG_COMMITTED && blobId && !isRRS) return;

    // Create the histogram config blob when no matched blob found.
    if (!blobId) {
        std::shared_ptr<PropertyBlob> drmConfigBlob;
        ret = createDrmConfigBlob(configInfo->mRequestedConfig, displayActiveH, displayActiveV,
                                  drmConfigBlob);
        if (ret || !drmConfigBlob) {
            if (ret == NO_INIT) {
                HIST_CH_LOG(D, channelId, "skip channel config");
                channel.mStatus = ChannelStatus_t::CONFIG_PENDING;
            } else {
                HIST_CH_LOG(E, channelId, "createDrmConfigBlob failed, ret(%d)", ret);
                channel.mStatus = ChannelStatus_t::CONFIG_ERROR;
            }
            return;
        }

        // Attach the histogram drmConfigBlob to the configInfo
        if (!configInfo->mBlobsList.empty()) isRRS = true;
        configInfo->mBlobsList.emplace_front(displayActiveH, displayActiveV, drmConfigBlob);
        blobId = drmConfigBlob->getId();
    }

    if (channel.mStatus == ChannelStatus_t::CONFIG_COMMITTED && isRRS) {
        HIST_BLOB_CH_LOG(I, blobId, channelId, "RRS (%dx%d) detected", displayActiveH,
                         displayActiveV);
    }

    // Add histogram config blob to atomic commit
    if ((ret = moduleDisplayInterface->setHistogramChannelConfigBlob(drmReq, channelId, blobId))) {
        HIST_BLOB_CH_LOG(E, blobId, channelId, "setHistogramChannelConfigBlob failed, ret(%d)",
                         ret);
        channel.mStatus = ChannelStatus_t::CONFIG_ERROR;
    } else {
        channel.mStatus = ChannelStatus_t::CONFIG_BLOB_ADDED;
    }
}

void HistogramDevice::clearChannelConfigBlob(
        ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq, const uint8_t channelId,
        ExynosDisplayDrmInterface* const moduleDisplayInterface) {
    ATRACE_NAME(String8::format("%s(chan#%u)", __func__, channelId));
    ChannelInfo& channel = mChannels[channelId];
    int ret;

    if ((ret = moduleDisplayInterface->clearHistogramChannelConfigBlob(drmReq, channelId))) {
        HIST_CH_LOG(E, channelId, "clearHistogramChannelConfigBlob failed, ret(%d)", ret);
        channel.mStatus = ChannelStatus_t::DISABLE_ERROR;
    } else {
        channel.mStatus = ChannelStatus_t::DISABLE_BLOB_ADDED;
    }
}

uint32_t HistogramDevice::getMatchBlobId(std::list<const BlobInfo>& blobsList,
                                         const int displayActiveH, const int displayActiveV,
                                         bool& isPositionChanged) const {
    auto resultIt = blobsList.end();

    for (auto it = blobsList.begin(); it != blobsList.end(); ++it) {
        if (it->mDisplayActiveH == displayActiveH && it->mDisplayActiveV == displayActiveV) {
            resultIt = it;
            break;
        }
    }

    if (resultIt == blobsList.end()) return 0;

    // Move the matched config blob to the front
    if (resultIt != blobsList.begin()) {
        isPositionChanged = true;
        blobsList.splice(blobsList.begin(), blobsList, resultIt, std::next(resultIt));
    }

    return blobsList.begin()->mBlob->getId();
}

// TODO: b/295990513 - Remove the if defined after kernel prebuilts are merged.
#if defined(EXYNOS_HISTOGRAM_CHANNEL_REQUEST)
int HistogramDevice::createDrmConfig(const HistogramConfig& histogramConfig,
                                     const int displayActiveH, const int displayActiveV,
                                     std::shared_ptr<void>& drmConfig,
                                     size_t& drmConfigLength) const {
    int ret = NO_ERROR;

    if (UNLIKELY(!displayActiveH || !displayActiveV)) {
        HIST_LOG(I, "active mode (%dx%d) is not initialized, skip creation", displayActiveH,
                 displayActiveV);
        return NO_INIT;
    }

    HistogramRoiRect drmRoi, drmBlockingRoi;
    if (UNLIKELY(ret = convertRoi(histogramConfig.roi, drmRoi, displayActiveH, displayActiveV,
                                  ""))) {
        HIST_LOG(E, "failed to convert roi, ret(%d)", ret);
        return ret;
    }
    if (UNLIKELY(ret = convertRoi(histogramConfig.blockingRoi.value_or(DISABLED_ROI),
                                  drmBlockingRoi, displayActiveH, displayActiveV, "blocking "))) {
        HIST_LOG(E, "failed to convert blocking roi, ret(%d)", ret);
        return ret;
    }

    drmConfig = std::make_shared<struct histogram_channel_config>();
    struct histogram_channel_config* config = (struct histogram_channel_config*)drmConfig.get();
    config->roi.start_x = drmRoi.left;
    config->roi.start_y = drmRoi.top;
    config->roi.hsize = drmRoi.right - drmRoi.left;
    config->roi.vsize = drmRoi.bottom - drmRoi.top;
    if (drmBlockingRoi != DISABLED_ROI) {
        config->flags |= HISTOGRAM_FLAGS_BLOCKED_ROI;
        config->blocked_roi.start_x = drmBlockingRoi.left;
        config->blocked_roi.start_y = drmBlockingRoi.top;
        config->blocked_roi.hsize = drmBlockingRoi.right - drmBlockingRoi.left;
        config->blocked_roi.vsize = drmBlockingRoi.bottom - drmBlockingRoi.top;
    } else {
        config->flags &= ~HISTOGRAM_FLAGS_BLOCKED_ROI;
    }
    config->weights.weight_r = histogramConfig.weights.weightR;
    config->weights.weight_g = histogramConfig.weights.weightG;
    config->weights.weight_b = histogramConfig.weights.weightB;
    config->pos =
            (histogramConfig.samplePos == HistogramSamplePos::POST_POSTPROC) ? POST_DQE : PRE_DQE;
    config->threshold = calculateThreshold(drmRoi, displayActiveH, displayActiveV);

    drmConfigLength = sizeof(struct histogram_channel_config);

    return NO_ERROR;
}
#else
int HistogramDevice::createDrmConfig(const HistogramConfig& histogramConfig,
                                     const int displayActiveH, const int displayActiveV,
                                     std::shared_ptr<void>& drmConfig,
                                     size_t& drmConfigLength) const {
    HIST_LOG(E, "not supported by kernel, struct histogram_channel_config is not defined");
    drmConfig = nullptr;
    drmConfigLength = 0;
    return INVALID_OPERATION;
}
#endif

int HistogramDevice::createDrmConfigBlob(const HistogramConfig& histogramConfig,
                                         const int displayActiveH, const int displayActiveV,
                                         std::shared_ptr<PropertyBlob>& drmConfigBlob) const {
    int ret = NO_ERROR;
    std::shared_ptr<void> drmConfig;
    size_t drmConfigLength = 0;

    if ((ret = createDrmConfig(histogramConfig, displayActiveH, displayActiveV, drmConfig,
                               drmConfigLength)))
        return ret;

    std::shared_ptr<PropertyBlob> drmConfigBlobTmp =
            std::make_shared<PropertyBlob>(mDrmDevice, drmConfig.get(), drmConfigLength);
    if ((ret = drmConfigBlobTmp->getError())) {
        HIST_LOG(E, "failed to create property blob, ret(%d)", ret);
        return ret;
    }

    // If success, drmConfigBlobTmp must contain a non-zero blobId
    drmConfigBlob = drmConfigBlobTmp;

    return NO_ERROR;
}

std::pair<int, int> HistogramDevice::snapDisplayActiveSize() const {
    ExynosDisplayDrmInterface* moduleDisplayInterface =
            static_cast<ExynosDisplayDrmInterface*>(mDisplay->mDisplayInterface.get());
    if (!moduleDisplayInterface) {
        HIST_LOG(E, "failed to get active size, moduleDisplayInterface is NULL");
        return std::make_pair(0, 0);
    }

    return std::make_pair(moduleDisplayInterface->getActiveModeHDisplay(),
                          moduleDisplayInterface->getActiveModeVDisplay());
}

int HistogramDevice::convertRoi(const HistogramRoiRect& requestedRoi,
                                HistogramRoiRect& convertedRoi, const int displayActiveH,
                                const int displayActiveV, const char* roiType) const {
    int32_t panelH, panelV;

    {
        std::shared_lock lock(mHistogramCapabilityMutex);
        panelH = mHistogramCapability.fullResolutionWidth;
        panelV = mHistogramCapability.fullResolutionHeight;
    }

    HIST_LOG(V, "active: (%dx%d), panel: (%dx%d)", displayActiveH, displayActiveV, panelH, panelV);

    if (panelH < displayActiveH || displayActiveH < 0 || panelV < displayActiveV ||
        displayActiveV < 0) {
        HIST_LOG(E, "failed to convert %sroi, active: (%dx%d), panel: (%dx%d)", roiType,
                 displayActiveH, displayActiveV, panelH, panelV);
        return -EINVAL;
    }

    // Linear transform from full resolution to active resolution
    convertedRoi.left = requestedRoi.left * displayActiveH / panelH;
    convertedRoi.top = requestedRoi.top * displayActiveV / panelV;
    convertedRoi.right = requestedRoi.right * displayActiveH / panelH;
    convertedRoi.bottom = requestedRoi.bottom * displayActiveV / panelV;

    HIST_LOG(V, "working %sroi: %s", roiType, toString(convertedRoi).c_str());

    return NO_ERROR;
}

void HistogramDevice::dumpHistogramCapability(String8& result) const {
    std::shared_lock lock(mHistogramCapabilityMutex);
    // Append the histogram capability info to the dump string
    result.appendFormat("Histogram capability:\n");
    result.appendFormat("\tsupportMultiChannel: %s\n",
                        mHistogramCapability.supportMultiChannel ? "true" : "false");
    result.appendFormat("\tsupportBlockingRoi: %s\n",
                        mHistogramCapability.supportBlockingRoi ? "true" : "false");
    result.appendFormat("\tchannelCount: %d\n", mHistogramCapability.channelCount);
    result.appendFormat("\tfullscreen roi: (0,0)x(%dx%d)\n",
                        mHistogramCapability.fullResolutionWidth,
                        mHistogramCapability.fullResolutionHeight);
    result.appendFormat("\tsupportSamplePosList:");
    for (HistogramSamplePos samplePos : mHistogramCapability.supportSamplePosList) {
        result.appendFormat(" %s",
                            aidl::com::google::hardware::pixel::display::toString(samplePos)
                                    .c_str());
    }
    result.appendFormat("\n");
}

ndk::ScopedAStatus HistogramDevice::validateHistogramRequest(
        const ndk::SpAIBinder& token, const HistogramConfig& histogramConfig,
        HistogramErrorCode* histogramErrorCode) const {
    // validate the argument (histogramErrorCode)
    if (!histogramErrorCode) {
        HIST_LOG(E, "binder error, histogramErrorCode is nullptr");
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }

    // default histogramErrorCode: no error
    *histogramErrorCode = HistogramErrorCode::NONE;

    // validate the argument (token)
    if (token.get() == nullptr) {
        HIST_LOG(E, "BAD_TOKEN, token is nullptr");
        *histogramErrorCode = HistogramErrorCode::BAD_TOKEN;
        return ndk::ScopedAStatus::ok();
    }

    // validate the argument (histogramConfig)
    if ((*histogramErrorCode = validateHistogramConfig(histogramConfig)) !=
        HistogramErrorCode::NONE) {
        return ndk::ScopedAStatus::ok();
    }

    return ndk::ScopedAStatus::ok();
}

HistogramDevice::HistogramErrorCode HistogramDevice::validateHistogramConfig(
        const HistogramConfig& histogramConfig) const {
    HistogramErrorCode ret;

    std::shared_lock lock(mHistogramCapabilityMutex);

    if ((ret = validateHistogramRoi(histogramConfig.roi, "")) != HistogramErrorCode::NONE ||
        (ret = validateHistogramWeights(histogramConfig.weights)) != HistogramErrorCode::NONE ||
        (ret = validateHistogramSamplePos(histogramConfig.samplePos)) != HistogramErrorCode::NONE ||
        (ret = validateHistogramBlockingRoi(histogramConfig.blockingRoi)) !=
                HistogramErrorCode::NONE) {
        return ret;
    }

    return HistogramErrorCode::NONE;
}

HistogramDevice::HistogramErrorCode HistogramDevice::validateHistogramRoi(
        const HistogramRoiRect& roi, const char* roiType) const {
    if (roi == DISABLED_ROI) return HistogramErrorCode::NONE;

    if ((roi.left < 0) || (roi.top < 0) || (roi.right - roi.left <= 0) ||
        (roi.bottom - roi.top <= 0) || (roi.right > mHistogramCapability.fullResolutionWidth) ||
        (roi.bottom > mHistogramCapability.fullResolutionHeight)) {
        HIST_LOG(E, "BAD_ROI, %sroi: %s, full screen roi: (0,0)x(%dx%d)", roiType,
                 toString(roi).c_str(), mHistogramCapability.fullResolutionWidth,
                 mHistogramCapability.fullResolutionHeight);
        return HistogramErrorCode::BAD_ROI;
    }

    return HistogramErrorCode::NONE;
}

HistogramDevice::HistogramErrorCode HistogramDevice::validateHistogramWeights(
        const HistogramWeights& weights) const {
    if ((weights.weightR + weights.weightG + weights.weightB) != WEIGHT_SUM) {
        HIST_LOG(E, "BAD_WEIGHT, weights%s", toString(weights).c_str());
        return HistogramErrorCode::BAD_WEIGHT;
    }

    return HistogramErrorCode::NONE;
}

HistogramDevice::HistogramErrorCode HistogramDevice::validateHistogramSamplePos(
        const HistogramSamplePos& samplePos) const {
    for (HistogramSamplePos mSamplePos : mHistogramCapability.supportSamplePosList) {
        if (samplePos == mSamplePos) {
            return HistogramErrorCode::NONE;
        }
    }

    HIST_LOG(E, "BAD_POSITION, samplePos is %s",
             aidl::com::google::hardware::pixel::display::toString(samplePos).c_str());
    return HistogramErrorCode::BAD_POSITION;
}

HistogramDevice::HistogramErrorCode HistogramDevice::validateHistogramBlockingRoi(
        const std::optional<HistogramRoiRect>& blockingRoi) const {
    // If the platform doesn't support blockingRoi, client should not enable blockingRoi
    if (mHistogramCapability.supportBlockingRoi == false) {
        if (blockingRoi.has_value() && blockingRoi.value() != DISABLED_ROI) {
            HIST_LOG(E, "BAD_ROI, platform doesn't support blockingRoi, requested: %s",
                     toString(blockingRoi.value()).c_str());
            return HistogramErrorCode::BAD_ROI;
        }
        return HistogramErrorCode::NONE;
    }

    // For the platform that supports blockingRoi, use the same validate rule as roi
    return validateHistogramRoi(blockingRoi.value_or(DISABLED_ROI), "blocking ");
}

int HistogramDevice::calculateThreshold(const HistogramRoiRect& roi, const int displayActiveH,
                                        const int displayActiveV) const {
    // If roi is disabled, the targeted region is entire screen.
    int32_t roiH = (roi != DISABLED_ROI) ? (roi.right - roi.left) : displayActiveH;
    int32_t roiV = (roi != DISABLED_ROI) ? (roi.bottom - roi.top) : displayActiveV;
    int threshold = (roiV * roiH) >> 16;
    // TODO: b/294491895 - Check if the threshold plus one really need it
    return threshold + 1;
}

std::string HistogramDevice::toString(const ChannelStatus_t& status) {
    switch (status) {
        case ChannelStatus_t::RESERVED:
            return "RESERVED";
        case ChannelStatus_t::DISABLED:
            return "DISABLED";
        case ChannelStatus_t::CONFIG_PENDING:
            return "CONFIG_PENDING";
        case ChannelStatus_t::CONFIG_BLOB_ADDED:
            return "CONFIG_BLOB_ADDED";
        case ChannelStatus_t::CONFIG_COMMITTED:
            return "CONFIG_COMMITTED";
        case ChannelStatus_t::CONFIG_ERROR:
            return "CONFIG_ERROR";
        case ChannelStatus_t::DISABLE_PENDING:
            return "DISABLE_PENDING";
        case ChannelStatus_t::DISABLE_BLOB_ADDED:
            return "DISABLE_BLOB_ADDED";
        case ChannelStatus_t::DISABLE_ERROR:
            return "DISABLE_ERROR";
    }

    return "UNDEFINED";
}

std::string HistogramDevice::toString(const HistogramRoiRect& roi) {
    if (roi == DISABLED_ROI) return "OFF";

    std::ostringstream os;
    os << "(" << roi.left << "," << roi.top << ")";
    os << "x";
    os << "(" << roi.right << "," << roi.bottom << ")";
    return os.str();
}

std::string HistogramDevice::toString(const HistogramWeights& weights) {
    std::ostringstream os;
    os << "(";
    os << (int)weights.weightR << ",";
    os << (int)weights.weightG << ",";
    os << (int)weights.weightB;
    os << ")";
    return os.str();
}

HistogramDevice::PropertyBlob::PropertyBlob(DrmDevice* const drmDevice, const void* const blobData,
                                            const size_t blobLength)
      : mDrmDevice(drmDevice) {
    if (!mDrmDevice) {
        ALOGE("%s: mDrmDevice is nullptr", __func__);
        mError = BAD_VALUE;
        return;
    }

    if ((mError = mDrmDevice->CreatePropertyBlob(blobData, blobLength, &mBlobId))) {
        mBlobId = 0;
        ALOGE("%s: failed to create histogram config blob, ret(%d)", __func__, mError);
    } else if (!mBlobId) {
        mError = BAD_VALUE;
        ALOGE("%s: create histogram config blob successful, but blobId is 0", __func__);
    }
}

HistogramDevice::PropertyBlob::~PropertyBlob() {
    if (mError) return;

    int ret = mDrmDevice->DestroyPropertyBlob(mBlobId);
    if (ret)
        ALOGE("%s: failed to destroy histogram config blob %d, ret(%d)", __func__, mBlobId, ret);
}

uint32_t HistogramDevice::PropertyBlob::getId() const {
    return mBlobId;
}

int HistogramDevice::PropertyBlob::getError() const {
    return mError;
}
