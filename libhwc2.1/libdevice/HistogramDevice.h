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

#pragma once

#include <aidl/android/hardware/graphics/common/Rect.h>
#include <aidl/com/google/hardware/pixel/display/HistogramCapability.h>
#include <aidl/com/google/hardware/pixel/display/HistogramConfig.h>
#include <aidl/com/google/hardware/pixel/display/HistogramErrorCode.h>
#include <aidl/com/google/hardware/pixel/display/HistogramSamplePos.h>
#include <aidl/com/google/hardware/pixel/display/Weight.h>
#include <android-base/thread_annotations.h>
#include <drm/samsung_drm.h>
#include <utils/String8.h>

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "ExynosDisplay.h"
#include "ExynosDisplayDrmInterface.h"
#include "drmcrtc.h"

#define HIST_BLOB_CH_LOG(LEVEL, blobId, channelId, msg, ...)              \
    ALOG##LEVEL("[%s,pid=%d,blob#%u,chan#%d] HistogramDevice::%s: " msg,  \
                ((mDisplay) ? (mDisplay->mDisplayName.c_str()) : "NULL"), \
                AIBinder_getCallingPid(), blobId, channelId, __func__, ##__VA_ARGS__)

#define HIST_BLOB_LOG(LEVEL, blobId, msg, ...)                            \
    ALOG##LEVEL("[%s,pid=%d,blob#%u] HistogramDevice::%s: " msg,          \
                ((mDisplay) ? (mDisplay->mDisplayName.c_str()) : "NULL"), \
                AIBinder_getCallingPid(), blobId, __func__, ##__VA_ARGS__)

#define HIST_CH_LOG(LEVEL, channelId, msg, ...)                           \
    ALOG##LEVEL("[%s,pid=%d,chan#%d] HistogramDevice::%s: " msg,          \
                ((mDisplay) ? (mDisplay->mDisplayName.c_str()) : "NULL"), \
                AIBinder_getCallingPid(), channelId, __func__, ##__VA_ARGS__)

#define HIST_LOG(LEVEL, msg, ...)                                         \
    ALOG##LEVEL("[%s,pid=%d] HistogramDevice::%s: " msg,                  \
                ((mDisplay) ? (mDisplay->mDisplayName.c_str()) : "NULL"), \
                AIBinder_getCallingPid(), __func__, ##__VA_ARGS__)

#define SCOPED_HIST_LOCK(mutex)   \
    std::scoped_lock lock(mutex); \
    ATRACE_NAME(String8::format("%s(%s)", #mutex, __func__));

using namespace android;

class HistogramDevice {
public:
    using HistogramCapability = aidl::com::google::hardware::pixel::display::HistogramCapability;
    using HistogramConfig = aidl::com::google::hardware::pixel::display::HistogramConfig;
    using HistogramErrorCode = aidl::com::google::hardware::pixel::display::HistogramErrorCode;
    using HistogramRoiRect = aidl::android::hardware::graphics::common::Rect;
    using HistogramSamplePos = aidl::com::google::hardware::pixel::display::HistogramSamplePos;
    using HistogramWeights = aidl::com::google::hardware::pixel::display::Weight;
    using HistogramChannelIoctl_t = ExynosDisplayDrmInterface::HistogramChannelIoctl_t;

    class PropertyBlob;

    /* For blocking roi and roi, (0, 0, 0, 0) means disabled */
    static constexpr HistogramRoiRect DISABLED_ROI = {0, 0, 0, 0};

    /* Histogram weight constraint: weightR + weightG + weightB = WEIGHT_SUM */
    static constexpr size_t WEIGHT_SUM = 1024;

    struct BlobInfo {
        const int mDisplayActiveH, mDisplayActiveV;
        const std::shared_ptr<PropertyBlob> mBlob;
        BlobInfo(const int displayActiveH, const int displayActiveV,
                 const std::shared_ptr<PropertyBlob>& drmConfigBlob)
              : mDisplayActiveH(displayActiveH),
                mDisplayActiveV(displayActiveV),
                mBlob(drmConfigBlob) {}
    };

    struct ConfigInfo {
        enum class Status_t : uint8_t {
            INITIALIZED, // Not in the inactive list and no channel assigned
            IN_INACTIVE_LIST,
            HAS_CHANNEL_ASSIGNED,
        };

        const HistogramConfig mRequestedConfig;
        Status_t mStatus = Status_t::INITIALIZED;
        int mChannelId = -1;
        std::list<const BlobInfo> mBlobsList;
        std::list<std::weak_ptr<ConfigInfo>>::iterator mInactiveListIt;
        ConfigInfo(const HistogramConfig& histogramConfig) : mRequestedConfig(histogramConfig) {}
    };

    /* Histogram channel status */
    enum class ChannelStatus_t : uint32_t {
        /* occupied by the driver for specific usage such as LHBM */
        RESERVED = 0,

        /* channel is off */
        DISABLED,

        /* channel config is ready and requires to be added into an atomic commit */
        CONFIG_PENDING,

        /* channel config (blob) is added to an atomic commit but not committed yet */
        CONFIG_BLOB_ADDED,

        /* channel config is committed to drm driver successfully */
        CONFIG_COMMITTED,

        /* channel config has error */
        CONFIG_ERROR,

        /* channel is released and requires an atomic commit to cleanup completely */
        DISABLE_PENDING,

        /* channel is released and the cleanup blob is added but not committed yet */
        DISABLE_BLOB_ADDED,

        /* channel disable has error */
        DISABLE_ERROR,
    };

    struct ChannelInfo {
        /* track the channel status */
        ChannelStatus_t mStatus;
        std::weak_ptr<ConfigInfo> mConfigInfo;

        ChannelInfo() : mStatus(ChannelStatus_t::DISABLED) {}
        ChannelInfo(const ChannelInfo& other) {}
    };

    struct TokenInfo {
        /* The binderdied callback would call unregisterHistogram (member function of this object)
         * to release resource. */
        HistogramDevice* const mHistogramDevice;

        /* The binderdied callback would call unregisterHistogram with this token to release
         * resource. */
        const ndk::SpAIBinder mToken;

        /* The process id of the client that calls registerHistogram. */
        const pid_t mPid;

        /* The shared pointer to the ConfigInfo. */
        std::shared_ptr<ConfigInfo> mConfigInfo;

        TokenInfo(HistogramDevice* histogramDevice, const ndk::SpAIBinder& token, pid_t pid)
              : mHistogramDevice(histogramDevice), mToken(token), mPid(pid) {}
    };

    /**
     * HistogramDevice
     *
     * Construct the HistogramDevice to mange histogram channel.
     *
     * @display display pointer which would be stored in mDisplay.
     * @channelCount number of the histogram channels in the system.
     * @reservedChannels a list of channel id that are reserved by the driver.
     */
    explicit HistogramDevice(ExynosDisplay* const display, const uint8_t channelCount,
                             const std::vector<uint8_t> reservedChannels);

    /**
     * ~HistogramDevice
     *
     * Destruct the HistogramDevice.
     */
    virtual ~HistogramDevice();

    /**
     * initDrm
     *
     * Get histogram info from crtc property and initialize the mHistogramCapability.
     *     1. The available histogram channel bitmask.
     *     2. Determine kernel support multi channel property or not.
     *
     * @device drm device object which will be used to create the config blob.
     * @crtc drm crtc object which would contain histogram related information.
     */
    void initDrm(DrmDevice& device, const DrmCrtc& crtc)
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * getHistogramCapability
     *
     * Return the histogram capability for the system.
     *
     * @histogramCapability: describe the histogram capability for the system.
     * @return ok() when the interface is supported and arguments are valid, else otherwise.
     */
    ndk::ScopedAStatus getHistogramCapability(HistogramCapability* histogramCapability) const
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * registerHistogram
     *
     * Register the histogram sampling config, and queue into the mInactiveConfigItList. Scheduler
     * will help to apply the config if possible. If the display is not turned on, just store the
     * histogram config. Otherwise, trigger the onRefresh call to force the config take effect, and
     * then the DPU hardware will continuously sample the histogram data.
     *
     * @token binder object created by the client whose lifetime should be equal to the client. When
     * the binder object is destructed, the unregisterHistogram would be called automatically. Token
     * serves as the handle in every histogram operation.
     * @histogramConfig histogram config from the client.
     * @histogramErrorCode NONE when no error, or else otherwise. Client should retry when failed.
     * @return ok() when the interface is supported, or EX_UNSUPPORTED_OPERATION when the interface
     * is not supported yet.
     */
    ndk::ScopedAStatus registerHistogram(const ndk::SpAIBinder& token,
                                         const HistogramConfig& histogramConfig,
                                         HistogramErrorCode* histogramErrorCode)
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * queryHistogram
     *
     * Query the histogram data from the corresponding channel of the token.
     *
     * @token is the handle registered via registerHistogram which would be used to identify the
     * channel.
     * @histogramBuffer 256 * 16 bits buffer to store the luma counts return by the histogram
     * hardware.
     * @histogramErrorCode NONE when no error, or else otherwise. Client should examine this
     * errorcode.
     * @return ok() when the interface is supported, or EX_UNSUPPORTED_OPERATION when the interface
     * is not supported yet.
     */
    ndk::ScopedAStatus queryHistogram(const ndk::SpAIBinder& token,
                                      std::vector<char16_t>* histogramBuffer,
                                      HistogramErrorCode* histogramErrorCode);

    /**
     * reconfigHistogram
     *
     * Change the histogram config for the corresponding channel of the token.
     *
     * @token is the handle registered via registerHistogram which would be used to identify the
     * channel.
     * @histogramConfig histogram config from the client.
     * @histogramErrorCode NONE when no error, or else otherwise. Client should examine this
     * errorcode.
     * @return ok() when the interface is supported, or EX_UNSUPPORTED_OPERATION when the interface
     * is not supported yet.
     */
    ndk::ScopedAStatus reconfigHistogram(const ndk::SpAIBinder& token,
                                         const HistogramConfig& histogramConfig,
                                         HistogramErrorCode* histogramErrorCode)
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * unregisterHistogram
     *
     * Release the corresponding channel of the token and add the channel id to free channel list.
     *
     * @token is the handle registered via registerHistogram which would be used to identify the
     * channel.
     * @histogramErrorCode NONE when no error, or else otherwise. Client should examine this
     * errorcode.
     * @return ok() when the interface is supported, or EX_UNSUPPORTED_OPERATION when the interface
     * is not supported yet.
     */
    ndk::ScopedAStatus unregisterHistogram(const ndk::SpAIBinder& token,
                                           HistogramErrorCode* histogramErrorCode)
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * handleDrmEvent
     *
     * Handle the histogram channel drm event (EXYNOS_DRM_HISTOGRAM_CHANNEL_EVENT) and copy the
     * histogram data from event struct to channel info.
     *
     * @event histogram channel drm event pointer (struct exynos_drm_histogram_channel_event *)
     */
    void handleDrmEvent(void* event);

    /**
     * prepareAtomicCommit
     *
     * Prepare the histogram atomic commit for each channel (see prepareChannelCommit).
     *
     * @drmReq drm atomic request object
     */
    void prepareAtomicCommit(ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq)
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex, mBlobIdDataMutex);

    /**
     * postAtomicCommit
     *
     * After the atomic commit is done, update the channel status as below.
     * Channel_Status:
     *     CONFIG_BLOB_ADDED  -> CONFIG_COMMITTED
     *     DISABLE_BLOB_ADDED -> DISABLED
     */
    void postAtomicCommit() EXCLUDES(mInitDrmDoneMutex, mHistogramMutex, mBlobIdDataMutex);

    /**
     * dump
     *
     * Dump every histogram channel information.
     *
     * @result histogram channel dump information would be appended to this string
     */
    void dump(String8& result) const;

protected:
    mutable std::shared_mutex mHistogramCapabilityMutex;
    HistogramCapability mHistogramCapability;

private:
    ExynosDisplay* const mDisplay;
    DrmDevice* mDrmDevice = nullptr;

    mutable std::mutex mHistogramMutex;
    std::unordered_map<AIBinder*, TokenInfo> mTokenInfoMap GUARDED_BY(mHistogramMutex);
    std::list<const uint8_t> mFreeChannels GUARDED_BY(mHistogramMutex); // free channel list
    std::set<const uint8_t> mUsedChannels GUARDED_BY(mHistogramMutex);  // all - free - reserved
    std::vector<ChannelInfo> mChannels GUARDED_BY(mHistogramMutex);
    std::list<std::weak_ptr<ConfigInfo>> mInactiveConfigItList GUARDED_BY(mHistogramMutex);

    mutable std::mutex mInitDrmDoneMutex;
    bool mInitDrmDone GUARDED_BY(mInitDrmDoneMutex) = false;
    std::condition_variable mInitDrmDone_cv GUARDED_BY(mInitDrmDoneMutex);

    /* Death recipient for the binderdied callback, would be deleted in the destructor */
    AIBinder_DeathRecipient* mDeathRecipient = nullptr;

    /**
     * initChannels
     *
     * Allocate channelCount channels and initialize the channel status for every channel.
     *
     * @channelCount number of channels in the system including the reserved channels.
     * @reservedChannels a list of channel id that are reserved by the driver.
     */
    void initChannels(const uint8_t channelCount, const std::vector<uint8_t>& reservedChannels)
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * initHistogramCapability
     *
     * Initialize the histogramCapability which would be queried by the client (see
     * getHistogramCapability).
     *
     * @supportMultiChannel true if the kernel support multi channel property, false otherwise.
     */
    void initHistogramCapability(const bool supportMultiChannel) EXCLUDES(mHistogramMutex);

    /**
     * initPlatformHistogramCapability
     *
     * Initialize platform specific histogram capability.
     */
    virtual void initPlatformHistogramCapability() {}

    /**
     * waitInitDrmDone
     *
     * Wait until the initDrm is finished, or when the timeout expires.
     *
     * @return true if initDrm is finished, or false when the timeout expires.
     */
    bool waitInitDrmDone() EXCLUDES(mInitDrmDoneMutex, mHistogramMutex);

    /**
     * replaceConfigInfo
     *
     * If histogramConfig is not nullptr, the configInfo pointer will point to the generated
     * ConfigInfo of the histogramConfig. Otherwise, histogramConfig is reset to the nullptr.
     * For the original ConfigInfo, every created blob will be released.
     *
     * @configInfo is the reference to the shared_ptr of ConfigInfo that will be updated depends on
     * histogramConfig.
     * @histogramConfig is the new requested config or nullptr to clear the configInfo ptr.
     */
    void replaceConfigInfo(std::shared_ptr<ConfigInfo>& configInfo,
                           const HistogramConfig* histogramConfig) REQUIRES(mHistogramMutex)
            EXCLUDES(mInitDrmDoneMutex);

    /**
     * searchTokenInfo
     *
     * Search the corresponding TokenInfo of the token object.
     *
     * @token is the key to be searched.
     * @tokenInfo is the result pointer to the corresponding TokenInfo.
     */
    HistogramErrorCode searchTokenInfo(const ndk::SpAIBinder& token, TokenInfo*& tokenInfo)
            REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex);

    /**
     * swapInConfigInfo
     *
     * Move the configInfo from the mInactiveConfigItList to the first free channel. Caller should
     * ensure mFreeChannels is not empty.
     *
     * @configInfo is the config to be moved from inactive list to the histogram channel.
     * @return is the iterator of the next object in the mInactiveConfigItList after deletion.
     */
    std::list<std::weak_ptr<ConfigInfo>>::iterator swapInConfigInfo(
            std::shared_ptr<ConfigInfo>& configInfo) REQUIRES(mHistogramMutex)
            EXCLUDES(mInitDrmDoneMutex);

    /**
     * addConfigToInactiveList
     *
     * Add the configInfo (status is NOT_READY) into mInactiveConfigItList.
     *
     * @configInfo operated configino
     */
    void addConfigToInactiveList(const std::shared_ptr<ConfigInfo>& configInfo)
            REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex);

    /**
     * scheduler
     *
     * Move every configInfo from the mInactiveConfigItList to the idle histogram channel until no
     * idle channel exists.
     *
     * @return true if there is any configInfo moved to histogram channel, false otherwise.
     */
    bool scheduler() REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex);

    /**
     * getHistogramData
     *
     * Get the histogram data by sending ioctl request which will allocate the drm event for
     * histogram, and wait on the condition variable histDataCollecting_cv until the drm event is
     * handled or timeout. Copy the histogram data from channel info to histogramBuffer.
     *
     * @channelId histogram channel id.
     * @histogramBuffer AIDL created buffer which will be sent back to the client.
     * @histogramErrorCode::NONE when success, or else otherwise.
     */
    void getHistogramData(uint8_t channelId, std::vector<char16_t>* histogramBuffer,
                          HistogramErrorCode* histogramErrorCode);

    /**
     * parseDrmEvent
     *
     * Parse the histogram drm event. This function should get the histogram channel id
     * and the histogram buffer address from the event struct.
     *
     * @event histogram drm event struct.
     * @channelId stores the extracted channel id from the event.
     * @buffer stores the extracted buffer address from the event.
     * @return NO_ERROR on success, else otherwise.
     */
    int parseDrmEvent(void* event, uint8_t& channelId, char16_t*& buffer) const;

    /**
     * cleanupChannelInfo
     *
     * Cleanup the channel info and set status to DISABLE_PENDING which means need to wait
     * for the atomic commit to release the kernel and hardware channel resources.
     *
     * @channelId the channel id to be cleanup.
     * @return next iterator of mUsedChannels after deletion.
     */
    std::set<const uint8_t>::iterator cleanupChannelInfo(const uint8_t channelId)
            REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex);

    /**
     * setChannelConfigBlob
     *
     * Check and detect if the histogram channel config blob needs change due to RRS. Send the
     * config blob commit by setHistogramChannelConfigBlob.
     *
     * Case RRS detected:
     *        CONFIG_COMMITTED / CONFIG_PENDING -> CONFIG_BLOB_ADDED
     * Case RRS not detected:
     *        CONFIG_COMMITTED -> CONFIG_COMMITTED
     *        CONFIG_PENDING -> CONFIG_BLOB_ADDED
     *
     * @drmReq drm atomic request object
     * @channelId histogram channel id
     * @moduleDisplayInterface display drm interface pointer
     * @displayActiveH current display active horizontal size (in pixel)
     * @displayActiveV current display active vertical size (in pixel)
     * @configInfo is the reference to the shared_ptr of ConfigInfo that will be updated depends on
     * histogramConfig.
     */
    void setChannelConfigBlob(ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq,
                              const uint8_t channelId,
                              ExynosDisplayDrmInterface* const moduleDisplayInterface,
                              const int displayActiveH, const int displayActiveV,
                              const std::shared_ptr<ConfigInfo>& configInfo)
            REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex, mBlobIdDataMutex);

    /**
     * clearChannelConfigBlob
     *
     * Call clearHistogramChannelConfigBlob to disable the histogram channel.
     *     Case #1: success, channel status: DISABLE_PENDING -> DISABLE_BLOB_ADDED.
     *     Case #2: failed, channel status: DISABLE_PENDING -> DISABLE_ERROR.
     *
     * @drmReq drm atomic request object
     * @channelId histogram channel id
     * @moduleDisplayInterface display drm interface pointer
     */
    void clearChannelConfigBlob(ExynosDisplayDrmInterface::DrmModeAtomicReq& drmReq,
                                const uint8_t channelId,
                                ExynosDisplayDrmInterface* const moduleDisplayInterface)
            REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex, mBlobIdDataMutex);

    /**
     * getMatchBlobId
     *
     * Traverse the blobsList to find any BlobInfo matched the active size
     * (displayActiveHxdisplayActiveV). Once found, move the BlobInfo to the front of the list which
     * means the active blob.
     *
     * @blobsList contains the BlobInfo list to be searched from.
     * @displayActiveH current display active horizontal size (in pixel)
     * @displayActiveV current display active vertical size (in pixel)
     * @return the blob id if found, 0 otherwise.
     */
    uint32_t getMatchBlobId(std::list<const BlobInfo>& blobsList, const int displayActiveH,
                            const int displayActiveV, bool& isPositionChanged) const
            REQUIRES(mHistogramMutex) EXCLUDES(mInitDrmDoneMutex, mBlobIdDataMutex);

    /**
     * createDrmConfig
     *
     * Allocate and initialize the histogram config for the drm driver. PropertyBlob class will use
     * this config to createPropertyBlob.
     *
     * @histogramConfig HistogramConfig that is requested from the client
     * @displayActiveH current display active horizontal size (in pixel)
     * @displayActiveV current display active vertical size (in pixel)
     * @drmConfig shared pointer to the allocated histogram config struct.
     * @drmConfigLength size of the histogram config.
     * @return NO_ERROR on success, else otherwise
     */
    int createDrmConfig(const HistogramConfig& histogramConfig, const int displayActiveH,
                        const int displayActiveV, std::shared_ptr<void>& drmConfig,
                        size_t& drmConfigLength) const
            EXCLUDES(mInitDrmDoneMutex, mBlobIdDataMutex);

    /**
     * createDrmConfigBlob
     *
     * Create the drmConfigBlob for the requeste histogramConfig.
     *
     * @histogramConfig HistogramConfig that is requested from the client
     * @displayActiveH current display active horizontal size (in pixel)
     * @displayActiveV current display active vertical size (in pixel)
     * @drmConfigBlob shared pointer to the created drmConfigBlob.
     */
    int createDrmConfigBlob(const HistogramConfig& histogramConfig, const int displayActiveH,
                            const int displayActiveV,
                            std::shared_ptr<PropertyBlob>& drmConfigBlob) const
            EXCLUDES(mInitDrmDoneMutex, mBlobIdDataMutex);

    std::pair<int, int> snapDisplayActiveSize() const
            EXCLUDES(mInitDrmDoneMutex, mHistogramMutex, mBlobIdDataMutex);

    /**
     * convertRoi
     *
     * Linear transform the requested roi (based on panel full resolution) into the working roi
     * (active resolution).
     *
     * @moduleDisplayInterface the displayInterface which contains the full resolution info
     * @requestedRoi requested roi
     * @workingRoi converted roi from the requested roi
     * @return NO_ERROR on success, else otherwise
     */
    int convertRoi(const HistogramRoiRect& requestedRoi, HistogramRoiRect& workingRoi,
                   const int displayActiveH, const int displayActiveV, const char* roiType) const
            EXCLUDES(mInitDrmDoneMutex, mBlobIdDataMutex);

    void dumpHistogramCapability(String8& result) const;

    ndk::ScopedAStatus validateHistogramRequest(const ndk::SpAIBinder& token,
                                                const HistogramConfig& histogramConfig,
                                                HistogramErrorCode* histogramErrorCode) const;
    HistogramErrorCode validateHistogramConfig(const HistogramConfig& histogramConfig) const;
    HistogramErrorCode validateHistogramRoi(const HistogramRoiRect& roi, const char* roiType) const;
    HistogramErrorCode validateHistogramWeights(const HistogramWeights& weights) const;
    HistogramErrorCode validateHistogramSamplePos(const HistogramSamplePos& samplePos) const;
    HistogramErrorCode validateHistogramBlockingRoi(
            const std::optional<HistogramRoiRect>& blockingRoi) const;

    int calculateThreshold(const HistogramRoiRect& roi, const int displayActiveH,
                           const int displayActiveV) const;

    static std::string toString(const ChannelStatus_t& status);
    static std::string toString(const HistogramRoiRect& roi);
    static std::string toString(const HistogramWeights& weights);
};

// PropertyBlob is the RAII class to manage the histogram PropertyBlob creation and deletion.
class HistogramDevice::PropertyBlob {
public:
    /**
     * PropertyBlob
     *
     * Construct the PropertyBlob to mange the histogram PropertyBlob.
     *
     * @drmDevice the object to call the CreatePropertyBlob.
     * @blobData pointer to the buffer that contains requested property blob data to be created.
     * @blobLength size of the buffer pointed by blobData.
     */
    PropertyBlob(DrmDevice* const drmDevice, const void* const blobData, const size_t blobLength);

    /**
     * ~PropertyBlob
     *
     * Destruct the PropertyBlob and release the allocated blob in constructor.
     */
    ~PropertyBlob();

    /**
     * getId
     *
     * @return blobId of this PropertyBlob
     */
    uint32_t getId() const;

    /**
     * getError
     *
     * @return any error in the constructor if any, otherwise return 0.
     */
    int getError() const;

private:
    DrmDevice* const mDrmDevice;
    uint32_t mBlobId = 0;
    int mError = NO_ERROR;
};
