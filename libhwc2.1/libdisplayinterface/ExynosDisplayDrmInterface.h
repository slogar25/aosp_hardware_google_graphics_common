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

#ifndef _EXYNOSDISPLAYDRMINTERFACE_H
#define _EXYNOSDISPLAYDRMINTERFACE_H

#include <drm/samsung_drm.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <xf86drmMode.h>

#include <list>
#include <unordered_map>

#include "ExynosDisplay.h"
#include "ExynosDisplayInterface.h"
#include "ExynosHWC.h"
#include "ExynosMPP.h"
#include "drmconnector.h"
#include "drmcrtc.h"
#include "vsyncworker.h"

/* Max plane number of buffer object */
#define HWC_DRM_BO_MAX_PLANES 4

#ifndef HWC_FORCE_PANIC_PATH
#define HWC_FORCE_PANIC_PATH "/d/dpu/panic"
#endif

using namespace android;
using DrmPropertyMap = std::unordered_map<uint32_t, uint64_t>;

class ExynosDevice;
constexpr uint32_t MAX_CACHED_BUFFERS = 32; // TODO: find a good value for this

using BufHandles = std::array<uint32_t, HWC_DRM_BO_MAX_PLANES>;
class FramebufferManager {
    public:
        FramebufferManager(){};
        ~FramebufferManager();
        void init(int drmFd);

        // get buffer for provided config, if a buffer with same config is already cached it will be
        // reused otherwise one will be allocated. returns fbId that can be used to attach to plane, any
        // buffers allocated/reused with this call will be staged, flip() call is expected after this
        // when frame is committed
        int32_t getBuffer(const exynos_win_config_data &config, uint32_t &fbId, bool caching = true);

        // this should be called after frame update
        // this will move all staged buffers to front of the cached buffers queue
        // This will also schedule a cleanup of cached buffers if cached buffer list goes
        // beyond MAX_CACHED_BUFFERS
        void flip(bool isActiveCommit);

        // release all currently tracked buffers, this can be called for example when display is turned
        // off
        void releaseAll();

    private:
        uint32_t getBufHandleFromFd(int fd);
        // this struct should contain elements that can be used to identify framebuffer more easily
        struct Framebuffer {
            Framebuffer(int fd, BufHandles handles, uint32_t bufNum, uint32_t fb)
                  : bufHandles(handles), bufferNum(bufNum), fbId(fb), drmFd(fd){};
            ~Framebuffer() {
                for (uint32_t i = 0; i < bufferNum; i++) {
                    if (bufHandles[i]) freeBufHandle(bufHandles[i]);
                }
                drmModeRmFB(drmFd, fbId);
            };
            void freeBufHandle(uint32_t handle);
            BufHandles bufHandles;
            uint32_t bufferNum;
            uint32_t fbId;
            int drmFd;
            nsecs_t lastLookupTime = 0;
        };
        using FBList = std::list<std::unique_ptr<Framebuffer>>;

        int addFB2WithModifiers(uint32_t width, uint32_t height, uint32_t pixel_format,
                        const BufHandles handles, const uint32_t pitches[4],
                        const uint32_t offsets[4], const uint64_t modifier[4], uint32_t *buf_id,
                        uint32_t flags);

        void removeFBsThreadRoutine();

        // releases framebuffers at the back of the cached buffer queue that go beyond
        // MAX_CACHED_BUFFERS
        void cleanup(FBList &cleanupBuffers);

        // buffers that are going to be committed in the next atomic frame update
        FBList mStagingBuffers;
        // unused buffers that have been used recently, front of the queue has the most recently used
        // ones
        FBList mCachedBuffers;

        int mDrmFd = -1;
        nsecs_t mLastActiveCommitTime = 0;

        std::thread mRmFBThread;
        bool mRmFBThreadRunning = false;
        Condition mCondition;
        Mutex mMutex;
};

class ExynosDisplayDrmInterface :
    public ExynosDisplayInterface,
    public VsyncCallback
{
    public:
        class DrmModeAtomicReq {
            public:
                DrmModeAtomicReq(ExynosDisplayDrmInterface *displayInterface);
                ~DrmModeAtomicReq();

                DrmModeAtomicReq(const DrmModeAtomicReq&) = delete;
                DrmModeAtomicReq& operator=(const DrmModeAtomicReq&) = delete;

                drmModeAtomicReqPtr pset() { return mPset; };
                void savePset() {
                    if (mSavedPset) {
                        drmModeAtomicFree(mSavedPset);
                    }
                    mSavedPset = drmModeAtomicDuplicate(mPset);
                }
                void restorePset() {
                    if (mPset) {
                        drmModeAtomicFree(mPset);
                    }
                    mPset = mSavedPset;
                    mSavedPset = NULL;
                }

                void setError(int err) { mError = err; };
                int getError() { return mError; };
                int32_t atomicAddProperty(const uint32_t id,
                        const DrmProperty &property,
                        uint64_t value, bool optional = false);
                String8& dumpAtomicCommitInfo(String8 &result, bool debugPrint = false);
                int commit(uint32_t flags, bool loggingForDebug = false, bool keepBlob = false);
                void addOldBlob(uint32_t blob_id) {
                    mOldBlobs.push_back(blob_id);
                };
                int destroyOldBlobs() {
                    for (auto &blob : mOldBlobs) {
                        int ret = mDrmDisplayInterface->mDrmDevice->DestroyPropertyBlob(blob);
                        if (ret) {
                            HWC_LOGE(mDrmDisplayInterface->mExynosDisplay,
                                    "Failed to destroy old blob after commit %d", ret);
                            return ret;
                        }
                    }
                    mOldBlobs.clear();
                    return NO_ERROR;
                };
            private:
                drmModeAtomicReqPtr mPset;
                drmModeAtomicReqPtr mSavedPset;
                int mError = 0;
                ExynosDisplayDrmInterface *mDrmDisplayInterface = NULL;
                /* Destroy old blobs after commit */
                std::vector<uint32_t> mOldBlobs;
                int drmFd() const { return mDrmDisplayInterface->mDrmDevice->fd(); }
        };
        class ExynosVsyncCallback {
            public:
                void enableVSync(bool enable) {
                    mVsyncEnabled = enable;
                    resetVsyncTimeStamp();
                };
                bool getVSyncEnabled() { return mVsyncEnabled; };
                void setDesiredVsyncPeriod(uint64_t period) {
                    mDesiredVsyncPeriod = period;
                    resetVsyncTimeStamp();
                };
                uint64_t getDesiredVsyncPeriod() { return mDesiredVsyncPeriod;};
                uint64_t getVsyncTimeStamp() { return mVsyncTimeStamp; };
                uint64_t getVsyncPeriod() { return mVsyncPeriod; };
                bool Callback(int display, int64_t timestamp);
                void resetVsyncTimeStamp() { mVsyncTimeStamp = 0; };
                void resetDesiredVsyncPeriod() { mDesiredVsyncPeriod = 0;};
            private:
                bool mVsyncEnabled = false;
                uint64_t mVsyncTimeStamp = 0;
                uint64_t mVsyncPeriod = 0;
                uint64_t mDesiredVsyncPeriod = 0;
        };
        void Callback(int display, int64_t timestamp) override;

        ExynosDisplayDrmInterface(ExynosDisplay *exynosDisplay);
        ~ExynosDisplayDrmInterface();
        virtual void init(ExynosDisplay *exynosDisplay);
        virtual int32_t setPowerMode(int32_t mode);
        virtual int32_t setLowPowerMode() override;
        virtual bool isDozeModeAvailable() const {
            return mDozeDrmMode.h_display() > 0 && mDozeDrmMode.v_display() > 0;
        };
        virtual int32_t setVsyncEnabled(uint32_t enabled);
        virtual int32_t getDisplayConfigs(
                uint32_t* outNumConfigs,
                hwc2_config_t* outConfigs);
        virtual void dumpDisplayConfigs();
        virtual int32_t getColorModes(
                uint32_t* outNumModes,
                int32_t* outModes);
        virtual int32_t setColorMode(int32_t mode);
        virtual int32_t setActiveConfig(hwc2_config_t config);
        virtual int32_t setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos);
        virtual int32_t updateHdrCapabilities();
        virtual int32_t deliverWinConfigData();
        virtual int32_t clearDisplay(bool needModeClear = false);
        virtual int32_t disableSelfRefresh(uint32_t disable);
        virtual int32_t setForcePanic();
        virtual int getDisplayFd() { return mDrmDevice->fd(); };
        virtual int32_t initDrmDevice(DrmDevice *drmDevice);
        virtual uint32_t getDrmDisplayId(uint32_t type, uint32_t index);
        virtual uint32_t getMaxWindowNum();
        virtual int32_t getReadbackBufferAttributes(int32_t* /*android_pixel_format_t*/ outFormat,
                int32_t* /*android_dataspace_t*/ outDataspace);
        virtual int32_t getDisplayIdentificationData(uint8_t* outPort,
                uint32_t* outDataSize, uint8_t* outData);

        /* For HWC 2.4 APIs */
        virtual int32_t getDisplayVsyncPeriod(
                hwc2_vsync_period_t* outVsyncPeriod);
        virtual int32_t getConfigChangeDuration();
        virtual int32_t getVsyncAppliedTime(hwc2_config_t config,
                int64_t* actualChangeTime);
        virtual int32_t setActiveConfigWithConstraints(
                hwc2_config_t config, bool test = false);

        virtual int32_t setDisplayColorSetting(
                ExynosDisplayDrmInterface::DrmModeAtomicReq &drmReq)
        { return NO_ERROR;};
        virtual int32_t setPlaneColorSetting(
                ExynosDisplayDrmInterface::DrmModeAtomicReq &drmReq,
                const std::unique_ptr<DrmPlane> &plane,
                const exynos_win_config_data& config)
        { return NO_ERROR;};
        virtual int32_t updateBrightness(bool syncFrame);
        virtual float getSdrDimRatio();

        bool isHbmOn() { return mBrightnessHbmOn.get(); }
        uint32_t getDbv() { return mBrightnessLevel.get(); }
    protected:
        struct ModeState {
            bool needs_modeset = false;
            DrmMode mode;
            uint32_t blob_id = 0;
            uint32_t old_blob_id = 0;
            void setMode(const DrmMode newMode, const uint32_t modeBlob,
                    DrmModeAtomicReq &drmReq) {
                drmReq.addOldBlob(old_blob_id);
                mode = newMode;
                old_blob_id = blob_id;
                blob_id = modeBlob;
            };
            void reset() {
                *this = {};
            };
            void apply(ModeState &toModeState, DrmModeAtomicReq &drmReq) {
                toModeState.setMode(mode, blob_id, drmReq);
                drmReq.addOldBlob(old_blob_id);
                reset();
            };
        };
        int32_t createModeBlob(const DrmMode &mode, uint32_t &modeBlob);
        int32_t setDisplayMode(DrmModeAtomicReq &drmReq, const uint32_t modeBlob);
	int32_t clearDisplayMode(DrmModeAtomicReq &drmReq);
        int32_t chosePreferredConfig();
        int getDeconChannel(ExynosMPP *otfMPP);
        static std::tuple<uint64_t, int> halToDrmEnum(
                const int32_t halData, const DrmPropertyMap &drmEnums);
        /*
         * This function adds FB and gets new fb id if fbId is 0,
         * if fbId is not 0, this reuses fbId.
         */
        int32_t setupCommitFromDisplayConfig(DrmModeAtomicReq &drmReq,
                const exynos_win_config_data &config,
                const uint32_t configIndex,
                const std::unique_ptr<DrmPlane> &plane,
                uint32_t &fbId);

        int32_t setupPartialRegion(DrmModeAtomicReq &drmReq);
        static void parseEnums(const DrmProperty& property,
                const std::vector<std::pair<uint32_t, const char *>> &enums,
                DrmPropertyMap &out_enums);
        void parseBlendEnums(const DrmProperty& property);
        void parseStandardEnums(const DrmProperty& property);
        void parseTransferEnums(const DrmProperty& property);
        void parseRangeEnums(const DrmProperty& property);

        int32_t setupWritebackCommit(DrmModeAtomicReq &drmReq);
    private:
        int32_t waitVBlank();
        int32_t updateColorSettings(DrmModeAtomicReq &drmReq);
        int32_t getLowPowerDrmModeModeInfo();
        int32_t setActiveDrmMode(DrmMode const &mode);

    protected:
        struct PartialRegionState {
            struct drm_clip_rect partial_rect = {0, 0, 0, 0};
            uint32_t blob_id = 0;
            bool isUpdated(drm_clip_rect rect) {
                return ((partial_rect.x1 != rect.x1) ||
                        (partial_rect.y1 != rect.y1) ||
                        (partial_rect.x2 != rect.x2) ||
                        (partial_rect.y2 != rect.y2));
            };
        };

        class DrmReadbackInfo {
            public:
                void init(DrmDevice *drmDevice, uint32_t displayId);
                ~DrmReadbackInfo() {
                    if (mDrmDevice == NULL)
                        return;
                    if (mOldFbId > 0)
                        drmModeRmFB(mDrmDevice->fd(), mOldFbId);
                    if (mFbId > 0)
                        drmModeRmFB(mDrmDevice->fd(), mFbId);
                }
                DrmConnector* getWritebackConnector() { return mWritebackConnector; };
                void setFbId(uint32_t fbId) {
                    if ((mDrmDevice != NULL) && (mOldFbId > 0))
                        drmModeRmFB(mDrmDevice->fd(), mOldFbId);
                    mOldFbId = mFbId;
                    mFbId = fbId;
                }
                void pickFormatDataspace();
                static constexpr uint32_t PREFERRED_READBACK_FORMAT =
                    HAL_PIXEL_FORMAT_RGBA_8888;
                uint32_t mReadbackFormat = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
            private:
                DrmDevice *mDrmDevice = NULL;
                DrmConnector *mWritebackConnector = NULL;
                uint32_t mFbId = 0;
                uint32_t mOldFbId = 0;
                std::vector<uint32_t> mSupportedFormats;
        };
        DrmDevice *mDrmDevice;
        DrmCrtc *mDrmCrtc;
        DrmConnector *mDrmConnector;
        VSyncWorker mDrmVSyncWorker;
        ExynosVsyncCallback mVsyncCallback;
        ModeState mActiveModeState;
        ModeState mDesiredModeState;
        PartialRegionState mPartialRegionState;
        /* Mapping plane id to ExynosMPP, key is plane id */
        std::unordered_map<uint32_t, ExynosMPP*> mExynosMPPsForPlane;

        DrmPropertyMap mBlendEnums;
        DrmPropertyMap mStandardEnums;
        DrmPropertyMap mTransferEnums;
        DrmPropertyMap mRangeEnums;

        DrmReadbackInfo mReadbackInfo;

    private:
        DrmMode mDozeDrmMode;

    protected:
        void getBrightnessInterfaceSupport();
        void setupBrightnessConfig();
        FILE *mHbmOnFd;
        FILE *mDimmingOnFd;
        bool mBrightntessIntfSupported = false;
        float mBrightnessHbmMax = 1.0f;
        /* boost brightness ratio for HDR */
        float mBrightnessHdrRatio = 1.0;
        enum class PanelHbmType {
            ONE_STEP,
            CONTINUOUS,
        };
        enum BrightnessRange {
            NORMAL = 0,
            HBM,
            MAX,
        };
        PanelHbmType mPanelHbmType;

        Mutex mBrightnessUpdateMutex;
        brightnessState_t mBrightnessState;
        CtrlValue<uint32_t> mBrightnessLevel;
        float mScaledBrightness;
        CtrlValue<bool> mBrightnessDimmingOn;
        CtrlValue<bool> mBrightnessHbmOn;
        CtrlValue<bool> mBrightnessLhbmOn;

        struct BrightnessTable {
            float mBriStart;
            float mBriEnd;
            uint32_t mBklStart;
            uint32_t mBklEnd;
            uint32_t mNitsStart;
            uint32_t mNitsEnd;
            BrightnessTable() {}
            BrightnessTable(const brightness_attribute &attr)
                  : mBriStart(static_cast<float>(attr.percentage.min) / 100.0f),
                    mBriEnd(static_cast<float>(attr.percentage.max) / 100.0f),
                    mBklStart(attr.level.min),
                    mBklEnd(attr.level.max),
                    mNitsStart(attr.nits.min),
                    mNitsEnd(attr.nits.max) {}
        };
        struct BrightnessTable mBrightnessTable[BrightnessRange::MAX];

        // TODO: hbm in dual display is not supported. It should support it in
        //      the furture.
        static constexpr const char *kHbmOnFileNode =
                "/sys/class/backlight/panel0-backlight/hbm_mode";
        static constexpr const char *kDimmingOnFileNode =
                "/sys/class/backlight/panel0-backlight/dimming_on";

        FramebufferManager mFBManager;

    private:
        int32_t getDisplayFakeEdid(uint8_t &outPort, uint32_t &outDataSize, uint8_t *outData);
};

#endif
