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

#include "ExynosDisplayInterface.h"
#include "ExynosHWC.h"
#include "drmcrtc.h"
#include "drmconnector.h"
#include "vsyncworker.h"
#include "ExynosMPP.h"
#include "ExynosDisplay.h"
#include <unordered_map>
#include <xf86drmMode.h>

/* Max plane number of buffer object */
#define HWC_DRM_BO_MAX_PLANES 4

#ifndef HWC_FORCE_PANIC_PATH
#define HWC_FORCE_PANIC_PATH "/d/dpu/panic"
#endif

using namespace android;
using DrmPropertyMap = std::unordered_map<uint32_t, uint64_t>;

class ExynosDevice;
class ExynosDisplayDrmInterface : public ExynosDisplayInterface {
    public:
        class DrmModeAtomicReq {
            public:
                DrmModeAtomicReq(ExynosDisplayDrmInterface *displayInterface);
                ~DrmModeAtomicReq();

                DrmModeAtomicReq(const DrmModeAtomicReq&) = delete;
                DrmModeAtomicReq& operator=(const DrmModeAtomicReq&) = delete;

                drmModeAtomicReqPtr pset() { return mPset; };
                void setError(int err) { mError = err; };
                int32_t atomicAddProperty(const uint32_t id,
                        const DrmProperty &property,
                        uint64_t value, bool optional = false);
                String8& dumpAtomicCommitInfo(String8 &result, bool debugPrint = false);
                int commit(uint32_t flags, bool loggingForDebug = false);
                void removeFbs(std::vector<uint32_t> &fbs);
                void moveTrackedFbs(std::vector<uint32_t> &fbs);
                void moveTrackedLastFb(uint32_t &fb);
                int addFB2WithModifiers(
                        uint32_t width, uint32_t height,
                        uint32_t pixel_format, const uint32_t bo_handles[4],
                        const uint32_t pitches[4], const uint32_t offsets[4],
                        const uint64_t modifier[4], uint32_t *buf_id,
                        uint32_t flags);
                uint32_t getBufHandleFromFd(int fd);
                void freeBufHandle(uint32_t handle);
            private:
                drmModeAtomicReqPtr mPset;
                int mError = 0;
                ExynosDisplayDrmInterface *mDrmDisplayInterface = NULL;
                std::vector<uint32_t> mFbIds;
                int drmFd() const { return mDrmDisplayInterface->mDrmDevice->fd(); }
        };
        class ExynosVsyncCallback: public VsyncCallback {
            public:
                void Callback(int display, int64_t timestamp) override;
                void init(ExynosDevice *exynosDevice, ExynosDisplay *exynosDisplay);
            private:
                ExynosDevice *mExynosDevice;
                ExynosDisplay *mExynosDisplay;
        };
        ExynosDisplayDrmInterface(ExynosDisplay *exynosDisplay);
        ~ExynosDisplayDrmInterface();
        virtual void init(ExynosDisplay *exynosDisplay);
        virtual int32_t setPowerMode(int32_t mode);
        virtual int32_t setVsyncEnabled(uint32_t enabled);
        virtual int32_t getDisplayAttribute(
                hwc2_config_t config,
                int32_t attribute, int32_t* outValue);
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
        virtual int32_t clearDisplay(bool readback = false);
        virtual int32_t disableSelfRefresh(uint32_t disable);
        virtual int32_t setForcePanic();
        virtual int getDisplayFd() { return mDrmDevice->fd(); };
        virtual void initDrmDevice(DrmDevice *drmDevice);
        virtual uint32_t getMaxWindowNum();
        virtual int32_t getReadbackBufferAttributes(int32_t* /*android_pixel_format_t*/ outFormat,
                int32_t* /*android_dataspace_t*/ outDataspace);
        virtual int32_t getDisplayIdentificationData(uint8_t* outPort,
                uint32_t* outDataSize, uint8_t* outData);
        virtual int32_t setDisplayColorSetting(
                ExynosDisplayDrmInterface::DrmModeAtomicReq &drmReq)
        { return NO_ERROR;};
        virtual int32_t setPlaneColorSetting(
                ExynosDisplayDrmInterface::DrmModeAtomicReq &drmReq,
                const std::unique_ptr<DrmPlane> &plane,
                const exynos_win_config_data& config)
        { return NO_ERROR;};
    protected:
        int32_t applyDisplayMode();
        int32_t chosePreferredConfig();
        int getDeconChannel(ExynosMPP *otfMPP);
        uint32_t getBytePerPixelOfPrimaryPlane(int format);
        static std::tuple<uint64_t, int> halToDrmEnum(
                const int32_t halData, const DrmPropertyMap &drmEnums);
        int32_t addFBFromDisplayConfig(DrmModeAtomicReq &drmReq,
                const exynos_win_config_data &config,
                uint32_t &fbId);
        /*
         * This function adds FB and gets new fb id if fbId is 0,
         * if fbId is not 0, this reuses fbId.
         */
        int32_t setupCommitFromDisplayConfig(DrmModeAtomicReq &drmReq,
                const exynos_win_config_data &config,
                const uint32_t configIndex,
                const std::unique_ptr<DrmPlane> &plane,
                uint32_t &fbId);

        static void parseEnums(const DrmProperty& property,
                const std::vector<std::pair<uint32_t, const char *>> &enums,
                DrmPropertyMap &out_enums);
        void parseBlendEnums(const DrmProperty& property);
        void parseStandardEnums(const DrmProperty& property);
        void parseTransferEnums(const DrmProperty& property);
        void parseRangeEnums(const DrmProperty& property);

        int32_t setupWritebackCommit(DrmModeAtomicReq &drmReq);
    protected:
        struct ModeState {
            bool needs_modeset = false;
            DrmMode mode;
            uint32_t blob_id = 0;
            uint32_t old_blob_id = 0;
        };
    protected:
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
        ExynosVsyncCallback mVsyncCallbak;
        ModeState mModeState;
        /* Mapping plane id to ExynosMPP, key is plane id */
        std::unordered_map<uint32_t, ExynosMPP*> mExynosMPPsForPlane;
        /* TODO: Temporary variable to manage fb id */
        std::vector<uint32_t> mOldFbIds;

        DrmPropertyMap mBlendEnums;
        DrmPropertyMap mStandardEnums;
        DrmPropertyMap mTransferEnums;
        DrmPropertyMap mRangeEnums;

        DrmReadbackInfo mReadbackInfo;
};

#endif
