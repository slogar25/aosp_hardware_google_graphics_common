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

#include "ExynosDeviceDrmInterface.h"
#include "ExynosDisplayDrmInterface.h"
#include "ExynosHWCDebug.h"
#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosExternalDisplayModule.h"
#include <hardware/hwcomposer_defs.h>

ExynosDeviceDrmInterface::ExynosDeviceDrmInterface(ExynosDevice *exynosDevice)
{
}

ExynosDeviceDrmInterface::~ExynosDeviceDrmInterface()
{
}

void ExynosDeviceDrmInterface::init(ExynosDevice *exynosDevice)
{
    mUseQuery = false;
    mExynosDevice = exynosDevice;
    mDrmResourceManager.Init();
    mDrmDevice = mDrmResourceManager.GetDrmDevice(HWC_DISPLAY_PRIMARY);
    assert(mDrmDevice != NULL);

    updateRestrictions();

    mExynosDrmEventHandler.init(mExynosDevice);
    mDrmDevice->event_listener()->RegisterHotplugHandler((DrmEventHandler *)&mExynosDrmEventHandler);

    ExynosDisplay *primaryDisplay = mExynosDevice->getDisplay(HWC_DISPLAY_PRIMARY);
    if (primaryDisplay != NULL) {
        ExynosDisplayDrmInterface *displayInterface = static_cast<ExynosDisplayDrmInterface*>(primaryDisplay->mDisplayInterface.get());
        displayInterface->initDrmDevice(mDrmDevice);
    }
    ExynosDisplay *externalDisplay = mExynosDevice->getDisplay(HWC_DISPLAY_EXTERNAL);
    if (externalDisplay != NULL) {
        ExynosDisplayDrmInterface *displayInterface = static_cast<ExynosDisplayDrmInterface*>(externalDisplay->mDisplayInterface.get());
        displayInterface->initDrmDevice(mDrmDevice);
    }

}

void ExynosDeviceDrmInterface::updateRestrictions()
{
    mUseQuery = false;
}

void ExynosDeviceDrmInterface::ExynosDrmEventHandler::init(ExynosDevice *exynosDevice)
{
    mExynosDevice = exynosDevice;
}

void ExynosDeviceDrmInterface::ExynosDrmEventHandler::HandleEvent(uint64_t timestamp_us)
{
    /* TODO: Check plug status hear or ExynosExternalDisplay::handleHotplugEvent() */
    ExynosExternalDisplayModule *display = (ExynosExternalDisplayModule*)mExynosDevice->getDisplay(HWC_DISPLAY_EXTERNAL);
    display->handleHotplugEvent();
}
