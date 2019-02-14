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

#ifndef EXYNOS_EXTERNAL_DISPLAY_H
#define EXYNOS_EXTERNAL_DISPLAY_H

#include <utils/Vector.h>
#include "ExynosDisplay.h"
#include <linux/videodev2.h>
#include "videodev2_exynos_displayport.h"
#include <cutils/properties.h>
#include "ExynosDisplayFbInterface.h"

#define SUPPORTED_DV_TIMINGS_NUM        100

#define DP_RESOLUTION_DEFAULT V4L2_DV_1080P60

#define EXTERNAL_DISPLAY_SKIP_LAYER   0x00000100
#define SKIP_EXTERNAL_FRAME 5


struct preset_index_mapping {
    int preset;
    int dv_timings_index;
};

const struct preset_index_mapping preset_index_mappings[SUPPORTED_DV_TIMINGS_NUM] = {
    {V4L2_DV_480P59_94, 0}, // 720X480P59_94
    {V4L2_DV_576P50, 1},
    {V4L2_DV_720P50, 2},
    {V4L2_DV_720P60, 3},
    {V4L2_DV_1080P24, 4},
    {V4L2_DV_1080P25, 5},
    {V4L2_DV_1080P30, 6},
    {V4L2_DV_1080P50, 7},
    {V4L2_DV_1080P60, 8},
    {V4L2_DV_2160P24, 9},
    {V4L2_DV_2160P25, 10},
    {V4L2_DV_2160P30, 11},
    {V4L2_DV_2160P50, 12},
    {V4L2_DV_2160P60, 13},
    {V4L2_DV_2160P24_1, 14},
    {V4L2_DV_2160P25_1, 15},
    {V4L2_DV_2160P30_1, 16},
    {V4L2_DV_2160P50_1, 17},
    {V4L2_DV_2160P60_1, 18},
    {V4L2_DV_2160P59, 19},
    {V4L2_DV_480P60, 20}, // 640X480P60
    {V4L2_DV_1440P59, 21},
    {V4L2_DV_1440P60, 22},
    {V4L2_DV_800P60_RB, 23}, // 1280x800P60_RB
    {V4L2_DV_1024P60, 24}, // 1280x1024P60
    {V4L2_DV_1440P60_1, 25}, // 1920x1440P60
};

class ExynosExternalDisplay : public ExynosDisplay {
    public:
        hwc2_config_t mActiveConfigIndex;
        int mExternalDisplayResolution = DP_RESOLUTION_DEFAULT; //preset

        /* Methods */
        ExynosExternalDisplay(uint32_t type, ExynosDevice *device);
        ~ExynosExternalDisplay();

        virtual void init();
        virtual void deInit();

        int getDisplayConfigs(uint32_t* outNumConfigs, hwc2_config_t* outConfigs);
        virtual int enable();
        int disable();
        void hotplug();

        /* validateDisplay(..., outNumTypes, outNumRequests)
         * Descriptor: HWC2_FUNCTION_VALIDATE_DISPLAY
         * HWC2_PFN_VALIDATE_DISPLAY
         */
        virtual int32_t validateDisplay(uint32_t* outNumTypes, uint32_t* outNumRequests);
        virtual int32_t canSkipValidate();
        virtual int32_t presentDisplay(int32_t* outRetireFence);
        virtual int openExternalDisplay();
        virtual void closeExternalDisplay();
        virtual int32_t getActiveConfig(hwc2_config_t* outconfig);
        virtual int32_t setVsyncEnabled(int32_t /*hwc2_vsync_t*/ enabled);
        virtual int32_t startPostProcessing();
        virtual int32_t setClientTarget(
                buffer_handle_t target,
                int32_t acquireFence, int32_t /*android_dataspace_t*/ dataspace);
        virtual int32_t setPowerMode(int32_t /*hwc2_power_mode_t*/ mode);
        virtual void initDisplayInterface(uint32_t interfaceType);
        bool checkRotate();
        bool handleRotate();
        virtual void handleHotplugEvent();

        bool mEnabled;
        bool mBlanked;
        bool mVirtualDisplayState;
        bool mIsSkipFrame;
        int mExternalHdrSupported;
        bool mHpdStatus;
        Mutex mExternalMutex;

        int mSkipFrameCount;
        int mSkipStartFrame;
    protected:
        class ExynosExternalDisplayFbInterface: public ExynosDisplayFbInterface {
            public:
                ExynosExternalDisplayFbInterface(ExynosDisplay *exynosDisplay);
                virtual void init(ExynosDisplay *exynosDisplay);
                virtual int32_t getDisplayConfigs(
                        uint32_t* outNumConfigs,
                        hwc2_config_t* outConfigs);
                virtual void dumpDisplayConfigs();
                virtual int32_t getDisplayAttribute(
                        hwc2_config_t config,
                        int32_t attribute, int32_t* outValue);
                virtual int32_t getHdrCapabilities(uint32_t* outNumTypes,
                        int32_t* outTypes, float* outMaxLuminance,
                        float* outMaxAverageLuminance, float* outMinLuminance);
            protected:
                int32_t calVsyncPeriod(v4l2_dv_timings dv_timing);
                void cleanConfigurations();
            protected:
                ExynosExternalDisplay *mExternalDisplay;
                struct v4l2_dv_timings dv_timings[SUPPORTED_DV_TIMINGS_NUM];
                android::Vector< unsigned int > mConfigurations;
        };
        int getDVTimingsIndex(int preset);
        virtual bool getHDRException(ExynosLayer *layer);
    private:
};

#endif

