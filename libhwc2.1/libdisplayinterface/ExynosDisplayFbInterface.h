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

#ifndef _EXYNOSDISPLAYFBINTERFACE_H
#define _EXYNOSDISPLAYFBINTERFACE_H

#include <sys/types.h>
#include <hardware/hwcomposer2.h>
#include "ExynosDisplayInterface.h"

class ExynosDisplay;

class ExynosDisplayFbInterface : public ExynosDisplayInterface {
    public:
        ExynosDisplayFbInterface(ExynosDisplay *exynosDisplay);
        ~ExynosDisplayFbInterface();
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
        virtual int32_t getHdrCapabilities(uint32_t* outNumTypes,
                int32_t* outTypes, float* outMaxLuminance,
                float* outMaxAverageLuminance, float* outMinLuminance);
        virtual int32_t deliverWinConfigData();
        virtual int32_t clearDisplay();
        virtual int32_t disableSelfRefresh(uint32_t disable);
        virtual int32_t setForcePanic();
        virtual int getDisplayFd() { return mDisplayFd; };
    protected:
        /**
         * LCD device member variables
         */
        int mDisplayFd;
};
#endif
