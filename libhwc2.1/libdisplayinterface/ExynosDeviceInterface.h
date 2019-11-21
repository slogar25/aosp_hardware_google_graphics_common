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

#ifndef _EXYNOSDEVICEINTERFACE_H
#define _EXYNOSDEVICEINTERFACE_H

#include "ExynosHWCHelper.h"

class ExynosDevice;
class ExynosDeviceInterface {
    protected:
        ExynosDevice *mExynosDevice = NULL;
        bool mUseQuery = false;
    public:
        virtual ~ExynosDeviceInterface(){};
        virtual void init(ExynosDevice *exynosDevice) = 0;
        virtual void updateRestrictions() = 0;
        virtual bool getUseQuery() { return mUseQuery; };
        ExynosDevice* getExynosDevice() {return mExynosDevice;};
    public:
        uint32_t mType = INTERFACE_TYPE_NONE;
};
#endif //_EXYNOSDEVICEINTERFACE_H
