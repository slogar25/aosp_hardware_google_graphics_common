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

#ifndef _EXYNOSDISPLAY_H
#define _EXYNOSDISPLAY_H

#include <utils/Vector.h>
#include <utils/KeyedVector.h>
#include <system/graphics.h>

#include "ExynosHWC.h"
#include <hardware/hwcomposer2.h>
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif
#include "ExynosHWCHelper.h"
#include "ExynosMPP.h"
#include "ExynosResourceManager.h"
#include "ExynosDisplayInterface.h"
#include "ExynosHWCDebug.h"

#define HWC_CLEARDISPLAY_WITH_COLORMAP
#define HWC_PRINT_FRAME_NUM     10

#define LOW_FPS_THRESHOLD     5

#if defined(HDR_CAPABILITIES_NUM)
#define SET_HDR_CAPABILITIES_NUM HDR_CAPABILITIES_NUM
#else
#define SET_HDR_CAPABILITIES_NUM 0
#endif

typedef hwc2_composition_t exynos_composition;

class ExynosLayer;
class ExynosDevice;
class ExynosMPP;
class ExynosMPPSource;

enum dynamic_recomp_mode {
    NO_MODE_SWITCH,
    DEVICE_2_CLIENT,
    CLIENT_2_DEVICE
};

enum rendering_state {
    RENDERING_STATE_NONE = 0,
    RENDERING_STATE_VALIDATED,
    RENDERING_STATE_ACCEPTED_CHANGE,
    RENDERING_STATE_PRESENTED,
    RENDERING_STATE_MAX
};

enum composition_type {
    COMPOSITION_NONE = 0,
    COMPOSITION_CLIENT,
    COMPOSITION_EXYNOS,
    COMPOSITION_MAX
};

enum {
    PSR_NONE = 0,
    PSR_DP,
    PSR_MIPI,
    PSR_MAX,
};

enum {
    PANEL_LEGACY = 0,
    PANEL_DSC,
    PANEL_MIC,
};

enum {
    eDisplayNone     = 0x0,
    ePrimaryDisplay  = 0x00000001,
    eExternalDisplay = 0x00000002,
    eVirtualDisplay  = 0x00000004,
};

#define NUM_SKIP_STATIC_LAYER  5
struct ExynosFrameInfo
{
    uint32_t srcNum;
    exynos_image srcInfo[NUM_SKIP_STATIC_LAYER];
    exynos_image dstInfo[NUM_SKIP_STATIC_LAYER];
};

struct exynos_win_config_data
{
    enum {
        WIN_STATE_DISABLED = 0,
        WIN_STATE_COLOR,
        WIN_STATE_BUFFER,
        WIN_STATE_UPDATE,
        WIN_STATE_CURSOR,
    } state = WIN_STATE_DISABLED;

    uint32_t color = 0;
    int fd_idma[3] = {-1, -1, -1};
    int acq_fence = -1;
    int rel_fence = -1;
    float plane_alpha = 0;
    int32_t blending = HWC2_BLEND_MODE_NONE;
    ExynosMPP* assignedMPP = NULL;
    int format = 0;
    uint32_t transform = 0;
    android_dataspace dataspace = HAL_DATASPACE_UNKNOWN;
    bool hdr_enable = false;
    enum dpp_comp_src comp_src = DPP_COMP_SRC_NONE;
    uint32_t min_luminance = 0;
    uint32_t max_luminance = 0;
    struct decon_win_rect block_area = { 0, 0, 0, 0};
    struct decon_win_rect transparent_area = {0, 0, 0, 0};
    struct decon_win_rect opaque_area = {0, 0, 0, 0};
    struct decon_frame src = {0, 0, 0, 0, 0, 0};
    struct decon_frame dst = {0, 0, 0, 0, 0, 0};
    bool protection = false;
    bool compression = false;

    void reset(){
        *this = {};
    };
};
struct exynos_dpu_data
{
    int retire_fence = -1;
    std::vector<exynos_win_config_data> configs;
    bool enable_win_update = false;
    struct decon_frame win_update_region = {0, 0, 0, 0, 0, 0};

    void init(uint32_t configNum) {
        for(uint32_t i = 0; i < configNum; i++)
        {
            exynos_win_config_data config_data;
            configs.push_back(config_data);
            configs.push_back(config_data);
        }
    };

    void reset() {
        retire_fence = -1;
        for (uint32_t i = 0; i < configs.size(); i++)
            configs[i].reset();
    };
    exynos_dpu_data& operator =(const exynos_dpu_data &configs_data){
        retire_fence = configs_data.retire_fence;
        if (configs.size() != configs_data.configs.size()) {
            HWC_LOGE(NULL, "invalid config, it has different configs size");
            return *this;
        }
        configs = configs_data.configs;
        return *this;
    };
};

class ExynosLowFpsLayerInfo
{
    public:
        ExynosLowFpsLayerInfo();
        bool mHasLowFpsLayer;
        int32_t mFirstIndex;
        int32_t mLastIndex;

        void initializeInfos();
        int32_t addLowFpsLayer(uint32_t layerIndex);
};

class ExynosSortedLayer : public Vector <ExynosLayer*>
{
    public:
        ssize_t remove(const ExynosLayer *item);
        status_t vector_sort();
        static int compare(ExynosLayer * const *lhs, ExynosLayer *const *rhs);
};

class ExynosCompositionInfo : public ExynosMPPSource {
    public:
        ExynosCompositionInfo():ExynosCompositionInfo(COMPOSITION_NONE){};
        ExynosCompositionInfo(uint32_t type);
        uint32_t mType;
        bool mHasCompositionLayer;
        int32_t mFirstIndex;
        int32_t mLastIndex;
        private_handle_t *mTargetBuffer;
        android_dataspace mDataSpace;
        int32_t mAcquireFence;
        int32_t mReleaseFence;
        bool mEnableSkipStatic;
        bool mSkipStaticInitFlag;
        bool mSkipFlag;
        ExynosFrameInfo mSkipSrcInfo;
        exynos_win_config_data mLastWinConfigData;

        int32_t mWindowIndex;
        bool mCompressed;

        void initializeInfos(ExynosDisplay *display);
        void setTargetBuffer(ExynosDisplay *display, private_handle_t *handle,
                int32_t acquireFence, android_dataspace dataspace);
        void setCompressed(bool compressed);
        bool getCompressed();
        void dump(String8& result);
        String8 getTypeStr();
};

// Prepare multi-resolution
struct ResolutionSize {
    uint32_t w;
    uint32_t h;
};

struct ResolutionInfo {
    uint32_t nNum;
    ResolutionSize nResolution[3];
    uint32_t nDSCYSliceSize[3];
    uint32_t nDSCXSliceSize[3];
    int      nPanelType[3];
};

struct DisplayControl {
    /** Composition crop en/disable **/
    bool enableCompositionCrop;
    /** Resource assignment optimization for exynos composition **/
    bool enableExynosCompositionOptimization;
    /** Resource assignment optimization for client composition **/
    bool enableClientCompositionOptimization;
    /** Use G2D as much as possible **/
    bool useMaxG2DSrc;
    /** Low fps layer optimization **/
    bool handleLowFpsLayers;
    /** start m2mMPP before persentDisplay **/
    bool earlyStartMPP;
    /** Adjust display size of the layer having high priority */
    bool adjustDisplayFrame;
    /** setCursorPosition support **/
    bool cursorSupport;
};

class ExynosDisplay {
    public:

        uint32_t mType;
        uint32_t mXres;
        uint32_t mYres;
        uint32_t mXdpi;
        uint32_t mYdpi;
        uint32_t mVsyncPeriod;

        int                     mPanelType;
        int                     mPsrMode;
        int32_t                 mDSCHSliceNum;
        int32_t                 mDSCYSliceSize;

        /* Constructor */
        ExynosDisplay(uint32_t type, ExynosDevice *device);
        /* Destructor */
        virtual ~ExynosDisplay();

        ExynosDevice *mDevice;

        uint32_t mDisplayId;
        String8 mDisplayName;

        Mutex mDisplayMutex;

        /** State variables */
        bool mPlugState;
        hwc2_power_mode_t mPowerModeState;
        hwc2_vsync_t mVsyncState;
        bool mHasSingleBuffer;

        DisplayControl mDisplayControl;

        /**
         * TODO : Should be defined as ExynosLayer type
         * Layer list those sorted by z-order
         */
        ExynosSortedLayer mLayers;

        ExynosResourceManager *mResourceManager;

        /**
         * Layer index, target buffer information for GLES.
         */
        ExynosCompositionInfo mClientCompositionInfo;

        /**
         * Layer index, target buffer information for G2D.
         */
        ExynosCompositionInfo mExynosCompositionInfo;

        /**
         * Geometry change info is described by bit map.
         * This flag is cleared when resource assignment for all displays
         * is done.
         */
        uint64_t  mGeometryChanged;

        /**
         * Rendering step information that is seperated by
         * VALIDATED, ACCEPTED_CHANGE, PRESENTED.
         */
        rendering_state  mRenderingState;

        /**
         * Rendering step information that is called by client
         */
        rendering_state  mHWCRenderingState;

        /**
         * Window total bandwidth by enabled window, It's used as dynamic re-composition enable/disable.
         */
        uint32_t  mDisplayBW;

        /**
         * Mode information Dynamic re-composition feature.
         * DEVICE_2_CLIENT: All layers are composited by GLES composition.
         * CLIENT_2_DEVICE: Device composition.
         */
        dynamic_recomp_mode mDynamicReCompMode;
        bool mDREnable;
        bool mDRDefault;
        Mutex mDRMutex;

        nsecs_t  mLastFpsTime;
        uint64_t mFrameCount;
        uint64_t mLastFrameCount;
        uint64_t mErrorFrameCount;
        uint64_t mLastModeSwitchTimeStamp;
        uint64_t mLastUpdateTimeStamp;
        uint64_t mUpdateEventCnt;
        uint64_t mUpdateCallCnt;

        /* default DMA for the display */
        decon_idma_type mDefaultDMA;

        /**
         * DECON WIN_CONFIG information.
         */
        exynos_dpu_data mDpuData;

        /**
         * Last win_config data is used as WIN_CONFIG skip decision or debugging.
         */
        exynos_dpu_data mLastDpuData;

        /**
         * Restore release fenc from DECON.
         */
        int mLastRetireFence;

        bool mUseDpu;

        /**
         * Max Window number, It should be set by display module(chip)
         */
        uint32_t mMaxWindowNum;
        uint32_t mWindowNumUsed;
        uint32_t mBaseWindowIndex;
        int32_t mBlendingNoneIndex;

        // Priority
        uint32_t mNumMaxPriorityAllowed;
        int32_t mCursorIndex;

        android_color_transform mColorTransformHint;

        ExynosLowFpsLayerInfo mLowFpsLayerInfo;

        // HDR capabilities
        uint32_t mHdrTypeNum;
        android_hdr_t mHdrTypes[HDR_CAPABILITIES_NUM];
        float mMaxLuminance;
        float mMaxAverageLuminance;
        float mMinLuminance;

        /* For debugging */
        hwc_display_contents_1_t *mHWC1LayerList;

        /* Support Multi-resolution scheme */
        int mOldScalerMode;
        int mNewScaledWidth;
        int mNewScaledHeight;
        int32_t mDeviceXres;
        int32_t mDeviceYres;
        ResolutionInfo mResolutionInfo;

        // WCG
        android_color_mode_t mColorMode;

        // Skip present frame if there was no validate after power on
        bool mSkipFrame;

        void initDisplay();

        int getDisplayId();

        int32_t setCompositionTargetExynosImage(uint32_t targetType, exynos_image *src_img, exynos_image *dst_img);
        int32_t initializeValidateInfos();
        int32_t addClientCompositionLayer(uint32_t layerIndex);
        int32_t removeClientCompositionLayer(uint32_t layerIndex);
        int32_t addExynosCompositionLayer(uint32_t layerIndex);

        /**
         * Dynamic AFBC Control solution : To get the prepared information is applied to current or not.
         */
        bool comparePreferedLayers();

        /**
         * @param *outLayer
         */
        int32_t destroyLayer(hwc2_layer_t outLayer);

        void destroyLayers();

        /**
         * @param index
         */
        ExynosLayer *getLayer(uint32_t index);
        ExynosLayer *checkLayer(hwc2_layer_t addr);

        virtual void doPreProcessing();

        int checkLayerFps();

        int checkDynamicReCompMode();

        int handleDynamicReCompMode();

        /**
         * @param compositionType
         */
        int skipStaticLayers(ExynosCompositionInfo& compositionInfo);
        int handleStaticLayers(ExynosCompositionInfo& compositionInfo);

        int doPostProcessing();

        int doExynosComposition();

        int32_t configureOverlay(ExynosLayer *layer, exynos_win_config_data &cfg);
        int32_t configureOverlay(ExynosCompositionInfo &compositionInfo);

        int32_t configureHandle(ExynosLayer &layer,  int fence_fd, exynos_win_config_data &cfg);

        virtual int setWinConfigData();

        virtual int setDisplayWinConfigData();

        virtual int32_t validateWinConfigData();

        virtual int deliverWinConfigData();

        virtual int setReleaseFences();

        virtual bool checkFrameValidation();

        /**
         * Display Functions for HWC 2.0
         */

        /**
         * Descriptor: HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES
         * HWC2_PFN_ACCEPT_DISPLAY_CHANGES
         **/
        virtual int32_t acceptDisplayChanges();

        /**
         * Descriptor: HWC2_FUNCTION_CREATE_LAYER
         * HWC2_PFN_CREATE_LAYER
         */
        virtual int32_t createLayer(hwc2_layer_t* outLayer);

        /**
         * Descriptor: HWC2_FUNCTION_GET_ACTIVE_CONFIG
         * HWC2_PFN_GET_ACTIVE_CONFIG
         */
        virtual int32_t getActiveConfig(hwc2_config_t* outConfig);

        /**
         * Descriptor: HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES
         * HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES
         */
        virtual int32_t getChangedCompositionTypes(
                uint32_t* outNumElements, hwc2_layer_t* outLayers,
                int32_t* /*hwc2_composition_t*/ outTypes);

        /**
         * Descriptor: HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT
         * HWC2_PFN_GET_CLIENT_TARGET_SUPPORT
         */
        virtual int32_t getClientTargetSupport(
                uint32_t width, uint32_t height,
                int32_t /*android_pixel_format_t*/ format,
                int32_t /*android_dataspace_t*/ dataspace);

        /**
         * Descriptor: HWC2_FUNCTION_GET_COLOR_MODES
         * HWC2_PFN_GET_COLOR_MODES
         */
        virtual int32_t getColorModes(
                uint32_t* outNumModes,
                int32_t* /*android_color_mode_t*/ outModes);

        /* getDisplayAttribute(..., config, attribute, outValue)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE
         * HWC2_PFN_GET_DISPLAY_ATTRIBUTE
         */
        virtual int32_t getDisplayAttribute(
                hwc2_config_t config,
                int32_t /*hwc2_attribute_t*/ attribute, int32_t* outValue);

        /* getDisplayConfigs(..., outNumConfigs, outConfigs)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_CONFIGS
         * HWC2_PFN_GET_DISPLAY_CONFIGS
         */
        virtual int32_t getDisplayConfigs(
                uint32_t* outNumConfigs,
                hwc2_config_t* outConfigs);

        /* getDisplayName(..., outSize, outName)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_NAME
         * HWC2_PFN_GET_DISPLAY_NAME
         */
        virtual int32_t getDisplayName(uint32_t* outSize, char* outName);

        /* getDisplayRequests(..., outDisplayRequests, outNumElements, outLayers,
         *     outLayerRequests)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_REQUESTS
         * HWC2_PFN_GET_DISPLAY_REQUESTS
         */
        virtual int32_t getDisplayRequests(
                int32_t* /*hwc2_display_request_t*/ outDisplayRequests,
                uint32_t* outNumElements, hwc2_layer_t* outLayers,
                int32_t* /*hwc2_layer_request_t*/ outLayerRequests);

        /* getDisplayType(..., outType)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_TYPE
         * HWC2_PFN_GET_DISPLAY_TYPE
         */
        virtual int32_t getDisplayType(
                int32_t* /*hwc2_display_type_t*/ outType);
        /* getDozeSupport(..., outSupport)
         * Descriptor: HWC2_FUNCTION_GET_DOZE_SUPPORT
         * HWC2_PFN_GET_DOZE_SUPPORT
         */
        virtual int32_t getDozeSupport(int32_t* outSupport);

        /* getReleaseFences(..., outNumElements, outLayers, outFences)
         * Descriptor: HWC2_FUNCTION_GET_RELEASE_FENCES
         * HWC2_PFN_GET_RELEASE_FENCES
         */
        virtual int32_t getReleaseFences(
                uint32_t* outNumElements,
                hwc2_layer_t* outLayers, int32_t* outFences);

        enum {
            SKIP_ERR_NONE = 0,
            SKIP_ERR_CONFIG_DISABLED,
            SKIP_ERR_FIRST_FRAME,
            SKIP_ERR_GEOMETRY_CHAGNED,
            SKIP_ERR_HAS_CLIENT_COMP,
            SKIP_ERR_SKIP_STATIC_CHANGED,
            SKIP_ERR_HAS_REQUEST,
            SKIP_ERR_DISP_NOT_CONNECTED,
            SKIP_ERR_DISP_NOT_POWER_ON,
            SKIP_ERR_FORCE_VALIDATE
        };
        virtual int32_t canSkipValidate();

        /* presentDisplay(..., outRetireFence)
         * Descriptor: HWC2_FUNCTION_PRESENT_DISPLAY
         * HWC2_PFN_PRESENT_DISPLAY
         */
        virtual int32_t presentDisplay(int32_t* outRetireFence);

        /* setActiveConfig(..., config)
         * Descriptor: HWC2_FUNCTION_SET_ACTIVE_CONFIG
         * HWC2_PFN_SET_ACTIVE_CONFIG
         */
        virtual int32_t setActiveConfig(hwc2_config_t config);

        /* setClientTarget(..., target, acquireFence, dataspace)
         * Descriptor: HWC2_FUNCTION_SET_CLIENT_TARGET
         * HWC2_PFN_SET_CLIENT_TARGET
         */
        virtual int32_t setClientTarget(
                buffer_handle_t target,
                int32_t acquireFence, int32_t /*android_dataspace_t*/ dataspace);

        /* setColorTransform(..., matrix, hint)
         * Descriptor: HWC2_FUNCTION_SET_COLOR_TRANSFORM
         * HWC2_PFN_SET_COLOR_TRANSFORM
         */
        virtual int32_t setColorTransform(
                const float* matrix,
                int32_t /*android_color_transform_t*/ hint);

        /* setColorMode(..., mode)
         * Descriptor: HWC2_FUNCTION_SET_COLOR_MODE
         * HWC2_PFN_SET_COLOR_MODE
         */
        virtual int32_t setColorMode(
                int32_t /*android_color_mode_t*/ mode);

        /* setOutputBuffer(..., buffer, releaseFence)
         * Descriptor: HWC2_FUNCTION_SET_OUTPUT_BUFFER
         * HWC2_PFN_SET_OUTPUT_BUFFER
         */
        virtual int32_t setOutputBuffer(
                buffer_handle_t buffer,
                int32_t releaseFence);

        virtual int clearDisplay();

        /* setPowerMode(..., mode)
         * Descriptor: HWC2_FUNCTION_SET_POWER_MODE
         * HWC2_PFN_SET_POWER_MODE
         */
        virtual int32_t setPowerMode(
                int32_t /*hwc2_power_mode_t*/ mode);

        /* setVsyncEnabled(..., enabled)
         * Descriptor: HWC2_FUNCTION_SET_VSYNC_ENABLED
         * HWC2_PFN_SET_VSYNC_ENABLED
         */
        virtual int32_t setVsyncEnabled(
                int32_t /*hwc2_vsync_t*/ enabled);

        /* validateDisplay(..., outNumTypes, outNumRequests)
         * Descriptor: HWC2_FUNCTION_VALIDATE_DISPLAY
         * HWC2_PFN_VALIDATE_DISPLAY
         */
        virtual int32_t validateDisplay(
                uint32_t* outNumTypes, uint32_t* outNumRequests);

        /* getHdrCapabilities(..., outNumTypes, outTypes, outMaxLuminance,
         *     outMaxAverageLuminance, outMinLuminance)
         * Descriptor: HWC2_FUNCTION_GET_HDR_CAPABILITIES
         */
        virtual int32_t getHdrCapabilities(uint32_t* outNumTypes, int32_t* /*android_hdr_t*/ outTypes, float* outMaxLuminance,
                float* outMaxAverageLuminance, float* outMinLuminance);

        /* TODO : TBD */
        int32_t setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos);

        void dump(String8& result);

        virtual int32_t startPostProcessing();

        void dumpConfig(const exynos_win_config_data &c);
        void dumpConfig(String8 &result, const exynos_win_config_data &c);
        void printConfig(exynos_win_config_data &c);

        unsigned int getLayerRegion(ExynosLayer *layer,
                hwc_rect *rect_area, uint32_t regionType);

        int handleWindowUpdate();
        bool windowUpdateExceptions();

        /* For debugging */
        void setHWC1LayerList(hwc_display_contents_1_t *contents) {mHWC1LayerList = contents;};
        bool validateExynosCompositionLayer();
        void printDebugInfos(String8 &reason);

        bool checkConfigChanged(const exynos_dpu_data &lastConfigsData,
                const exynos_dpu_data &newConfigsData);
        int checkConfigDstChanged(const exynos_dpu_data &lastConfigData,
                const exynos_dpu_data &newConfigData, uint32_t index);

        uint32_t getRestrictionIndex(int halFormat);
        void closeFences();
        void closeFencesForSkipFrame(rendering_state renderingState);

        int32_t getLayerCompositionTypeForValidationType(uint32_t layerIndex);
        void setHWCControl(uint32_t ctrl, int32_t val);
        void setGeometryChanged(uint64_t changedBit);
        void clearGeometryChanged();

        virtual void setDDIScalerEnable(int width, int height);
        virtual int getDDIScalerMode(int width, int height);
        void increaseMPPDstBufIndex();
        virtual void initDisplayInterface(uint32_t interfaceType);
    protected:
        virtual bool getHDRException(ExynosLayer *layer);

    public:
        /**
         * This will be initialized with differnt class
         * that inherits ExynosDisplayInterface according to
         * interface type.
         */
        std::unique_ptr<ExynosDisplayInterface> mDisplayInterface;

    private:
        bool skipStaticLayerChanged(ExynosCompositionInfo& compositionInfo);
};

#endif //_EXYNOSDISPLAY_H
