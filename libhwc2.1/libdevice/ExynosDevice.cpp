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

#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosPrimaryDisplayModule.h"
#include "ExynosResourceManagerModule.h"
#include "ExynosExternalDisplayModule.h"
#include "ExynosVirtualDisplayModule.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWCHelper.h"

/**
 * ExynosDevice implementation
 */

class ExynosDevice;

extern void vsync_callback(hwc2_callback_data_t callbackData,
        hwc2_display_t displayId, int64_t timestamp);
extern uint32_t mFenceLogSize;
extern feature_support_t feature_table[];

int hwcDebug;
int hwcFenceDebug[FENCE_IP_ALL];
struct exynos_hwc_control exynosHWCControl;
struct update_time_info updateTimeInfo;
char fence_names[FENCE_MAX][32];

GrallocWrapper::Mapper* ExynosDevice::mMapper = NULL;
GrallocWrapper::Allocator* ExynosDevice::mAllocator = NULL;

void handle_vsync_event(ExynosDevice *dev) {

    int err = 0;

    if ((dev == NULL) || (dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer == NULL))
        return;

    dev->compareVsyncPeriod();

    hwc2_callback_data_t callbackData =
        dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].callbackData;
    HWC2_PFN_VSYNC callbackFunc =
        (HWC2_PFN_VSYNC)dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer;

    err = lseek(dev->mVsyncFd, 0, SEEK_SET);

    if (err < 0 ) {
        ExynosDisplay *display = (ExynosDisplay*)dev->getDisplay(HWC_DISPLAY_PRIMARY);
        if (display != NULL) {
            if (display->mVsyncState == HWC2_VSYNC_ENABLE)
                ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        }
        return;
    }

    if (callbackData != NULL && callbackFunc != NULL) {
        /** Vsync read **/
        char buf[4096];
        err = read(dev->mVsyncFd , buf, sizeof(buf));
        if (err < 0) {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }

        if (dev->mVsyncDisplay != HWC_DISPLAY_PRIMARY) {
            // Vsync of primary display is not used
            return;
        }

        dev->mTimestamp = strtoull(buf, NULL, 0);

        gettimeofday(&updateTimeInfo.lastUeventTime, NULL);
        /** Vsync callback **/
        callbackFunc(callbackData, HWC_DISPLAY_PRIMARY, dev->mTimestamp);
    }
}

void handle_external_vsync_event(ExynosDevice *dev) {

    int err = 0;

    if ((dev == NULL) || (dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer == NULL))
        return;

    dev->compareVsyncPeriod();

    hwc2_callback_data_t callbackData =
        dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].callbackData;
    HWC2_PFN_VSYNC callbackFunc =
        (HWC2_PFN_VSYNC)dev->mCallbackInfos[HWC2_CALLBACK_VSYNC].funcPointer;

    err = lseek(dev->mExtVsyncFd, 0, SEEK_SET);

    if(err < 0 ) {
        ExynosExternalDisplay *display = (ExynosExternalDisplay*)dev->getDisplay(HWC_DISPLAY_EXTERNAL);
        if (display->mHpdStatus) {
            ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        }
        return;
    }

    if (callbackData != NULL && callbackFunc != NULL) {
        /** Vsync read **/
        char buf[4096];
        err = read(dev->mExtVsyncFd , buf, sizeof(buf));
        if (err < 0) {
            ALOGE("error reading vsync timestamp: %s", strerror(errno));
            return;
        }

        if (dev->mVsyncDisplay != HWC_DISPLAY_EXTERNAL) {
            // Vsync of external display is not used
            return;
        }
        dev->mTimestamp = strtoull(buf, NULL, 0);

        /** Vsync callback **/
        callbackFunc(callbackData, HWC_DISPLAY_PRIMARY, dev->mTimestamp);
    }
}

void *hwc_eventHndler_thread(void *data) {

    /** uevent init **/
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));

    ExynosDevice *dev = (ExynosDevice*)data;

    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

    uevent_init();

    /** Vsync init. **/
    char devname[MAX_DEV_NAME + 1];
    devname[MAX_DEV_NAME] = '\0';

    strncpy(devname, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
    strlcat(devname, VSYNC_DEV_NAME, MAX_DEV_NAME);

    dev->mVsyncFd = open(devname, O_RDONLY);

    char devname_ext[MAX_DEV_NAME + 1];
    devname_ext[MAX_DEV_NAME] = '\0';

    strncpy(devname_ext, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
    strlcat(devname_ext, VSYNC_DEV_NAME_EXT, MAX_DEV_NAME);

    dev->mExtVsyncFd = open(devname_ext, O_RDONLY);

    char ueventname_ext[MAX_DEV_NAME + 1];
    ueventname_ext[MAX_DEV_NAME] = '\0';
    sprintf(ueventname_ext, DP_UEVENT_NAME, DP_LINK_NAME);
    ALOGI("uevent name of ext: %s", ueventname_ext);

    if (dev->mVsyncFd < 0) {
        ALOGI("Failed to open vsync attribute at %s", devname);
        devname[strlen(VSYNC_DEV_PREFIX)] = '\0';
        strlcat(devname, VSYNC_DEV_MIDDLE, MAX_DEV_NAME);
        strlcat(devname, VSYNC_DEV_NAME, MAX_DEV_NAME);
        ALOGI("Retrying with %s", devname);
        dev->mVsyncFd = open(devname, O_RDONLY);
        ALOGI("dev->mVsyncFd %d", dev->mVsyncFd);
    }

    if (dev->mExtVsyncFd < 0) {
        ALOGI("Failed to open vsync attribute at %s", devname_ext);
        devname_ext[strlen(VSYNC_DEV_PREFIX)] = '\0';
        strlcat(devname_ext, VSYNC_DEV_MIDDLE, MAX_DEV_NAME);
        strlcat(devname_ext, VSYNC_DEV_NAME_EXT, MAX_DEV_NAME);
        ALOGI("Retrying with %s", devname_ext);
        dev->mExtVsyncFd = open(devname_ext, O_RDONLY);
        ALOGI("dev->mExtVsyncFd %d", dev->mExtVsyncFd);
    }
    /** Poll definitions **/
    /** TODO : Hotplug here **/

    struct pollfd fds[3];

    fds[0].fd = dev->mVsyncFd;
    fds[0].events = POLLPRI;
    fds[1].fd = uevent_get_fd();
    fds[1].events = POLLIN;
    fds[2].fd = dev->mExtVsyncFd;
    fds[2].events = POLLPRI;

    /** Polling events **/
    while (true) {
        int err = poll(fds, 3, -1);

        if (err > 0) {
            if (fds[0].revents & POLLPRI) {
                handle_vsync_event((ExynosDevice*)dev);
            }
            else if (fds[1].revents & POLLIN) {
                uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);

                bool dp_status = !strcmp(uevent_desc, ueventname_ext);

                if (dp_status) {
                    ExynosExternalDisplayModule *display = (ExynosExternalDisplayModule*)dev->getDisplay(HWC_DISPLAY_EXTERNAL);
                    display->handleHotplugEvent();
                }
            }
            else if (fds[2].revents & POLLPRI) {
                handle_external_vsync_event((ExynosDevice*)dev);
            }
        }
        else if (err == -1) {
            if (errno == EINTR)
                break;
            ALOGE("error in vsync thread: %s", strerror(errno));
        }
    }
    return NULL;
}

ExynosDevice::ExynosDevice()
    : mGeometryChanged(0),
    mDRThread(0),
    mVsyncFd(-1),
    mExtVsyncFd(-1),
    mVsyncDisplay(HWC_DISPLAY_PRIMARY),
    mTimestamp(0),
    mDisplayMode(0),
    mInterfaceType(INTERFACE_TYPE_FB)
{
    exynosHWCControl.forceGpu = false;
    exynosHWCControl.windowUpdate = true;
    exynosHWCControl.forcePanic = false;
    exynosHWCControl.skipStaticLayers = true;
    exynosHWCControl.skipM2mProcessing = true;
    exynosHWCControl.skipResourceAssign = true;
    exynosHWCControl.multiResolution = true;
    exynosHWCControl.dumpMidBuf = false;
    exynosHWCControl.displayMode = DISPLAY_MODE_NUM;
    exynosHWCControl.setDDIScaler = false;
    exynosHWCControl.skipWinConfig = false;
    exynosHWCControl.skipValidate = true;
    exynosHWCControl.doFenceFileDump = false;
    exynosHWCControl.fenceTracer = 0;
    exynosHWCControl.sysFenceLogging = false;

    ALOGD("HWC2 : %s : %d ", __func__, __LINE__);
    mResourceManager = new ExynosResourceManagerModule(this);

    ExynosPrimaryDisplayModule *primary_display = new ExynosPrimaryDisplayModule(HWC_DISPLAY_PRIMARY, this);

    primary_display->mPlugState = true;
    ExynosMPP::mainDisplayWidth = primary_display->mXres;
    if (ExynosMPP::mainDisplayWidth <= 0) {
        ExynosMPP::mainDisplayWidth = 1440;
    }
    ExynosMPP::mainDisplayHeight = primary_display->mYres;
    if (ExynosMPP::mainDisplayHeight <= 0) {
        ExynosMPP::mainDisplayHeight = 2560;
    }

    ExynosExternalDisplayModule *external_display = new ExynosExternalDisplayModule(HWC_DISPLAY_EXTERNAL, this);

    ExynosVirtualDisplayModule *virtual_display = new ExynosVirtualDisplayModule(HWC_DISPLAY_VIRTUAL, this);
    mNumVirtualDisplay = 0;

    mDisplays.add((ExynosDisplay*) primary_display);
    mDisplays.add((ExynosDisplay*) external_display);
    mDisplays.add((ExynosDisplay*) virtual_display);

    memset(mCallbackInfos, 0, sizeof(mCallbackInfos));
#ifndef FORCE_DISABLE_DR
    exynosHWCControl.useDynamicRecomp = (primary_display->mDREnable) || (external_display->mDREnable) ||
                                        (virtual_display->mDREnable);
#else
    exynosHWCControl.useDynamicRecomp = false;
#endif

    dynamicRecompositionThreadCreate();

    hwcDebug = 0;
    for (uint32_t i = 0; i < FENCE_IP_ALL; i++)
        hwcFenceDebug[i] = 0;

    for (uint32_t i = 0; i < FENCE_MAX; i++) {
        memset(fence_names[i], 0, sizeof(fence_names[0]));
        sprintf(fence_names[i], "_%2dh", i);
    }

    String8 saveString;
    saveString.appendFormat("ExynosDevice is initialized");
    uint32_t errFileSize = saveErrorLog(saveString);
    ALOGI("Initial errlog size: %d bytes\n", errFileSize);

    /*
     * This order should not be changed
     * new ExynosResourceManager ->
     * create displays and add them to the list ->
     * initDeviceInterface() ->
     * ExynosResourceManager::updateRestrictions()
     */
    initDeviceInterface(mInterfaceType);
    mResourceManager->updateRestrictions();
}

void ExynosDevice::initDeviceInterface(uint32_t interfaceType)
{
    mDeviceInterface = new ExynosDeviceFbInterface(this);
    /*
     * This order should not be changed
     * initDisplayInterface() of each display ->
     * ExynosDeviceInterface::init()
     */
    for (uint32_t i = 0; i < mDisplays.size(); i++)
        mDisplays[i]->initDisplayInterface(interfaceType);
    mDeviceInterface->init(this);
}

ExynosDevice::~ExynosDevice() {

    ExynosDisplay *primary_display = getDisplay(HWC_DISPLAY_PRIMARY);

    /* TODO kill threads here */
    pthread_kill(mDRThread, SIGTERM);
    pthread_join(mDRThread, NULL);

    if (mMapper != NULL)
        delete mMapper;
    if (mAllocator != NULL)
        delete mAllocator;

    if (mDeviceInterface != NULL)
        delete mDeviceInterface;

    delete primary_display;
}

bool ExynosDevice::isFirstValidate()
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if ((mDisplays[i]->mDisplayId != HWC_DISPLAY_VIRTUAL) &&
            (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
            continue;
        if ((mDisplays[i]->mPlugState == true) &&
            ((mDisplays[i]->mRenderingState != RENDERING_STATE_NONE) &&
             (mDisplays[i]->mRenderingState != RENDERING_STATE_PRESENTED)))
            return false;
    }

    return true;
}

bool ExynosDevice::isLastValidate(ExynosDisplay *display)
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if (mDisplays[i] == display)
            continue;
        if ((mDisplays[i]->mDisplayId != HWC_DISPLAY_VIRTUAL) &&
            (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
            continue;
        if ((mDisplays[i]->mPlugState == true) &&
            (mDisplays[i]->mRenderingState != RENDERING_STATE_VALIDATED) &&
            (mDisplays[i]->mRenderingState != RENDERING_STATE_ACCEPTED_CHANGE))
            return false;
    }
    return true;
}

bool ExynosDevice::isDynamicRecompositionThreadAlive()
{
    android_atomic_acquire_load(&mDRThreadStatus);
    return (mDRThreadStatus > 0);
}

void ExynosDevice::checkDynamicRecompositionThread()
{
    // If thread was destroyed, create thread and run. (resume status)
    if (isDynamicRecompositionThreadAlive() == false) {
        for (uint32_t i = 0; i < mDisplays.size(); i++) {
            if (mDisplays[i]->mDREnable) {
                dynamicRecompositionThreadCreate();
                return;
            }
        }
    } else {
    // If thread is running and all displays turnned off DR, destroy the thread.
        for (uint32_t i = 0; i < mDisplays.size(); i++) {
            if (mDisplays[i]->mDREnable)
                return;
        }
        mDRLoopStatus = false;
        pthread_join(mDRThread, 0);
    }
}

void ExynosDevice::dynamicRecompositionThreadCreate()
{
    if (exynosHWCControl.useDynamicRecomp == true) {
        /* pthread_create shouldn't have been failed. But, ignore if some error was occurred */
        if (pthread_create(&mDRThread, NULL, dynamicRecompositionThreadLoop, this) != 0) {
            ALOGE("%s: failed to start hwc_dynamicrecomp_thread thread:", __func__);
            mDRLoopStatus = false;
        } else {
            mDRLoopStatus = true;
        }
    }
}

void *ExynosDevice::dynamicRecompositionThreadLoop(void *data)
{
    ExynosDevice *dev = (ExynosDevice *)data;
    ExynosDisplay *display[HWC_NUM_DISPLAY_TYPES];
    uint64_t event_cnt[HWC_NUM_DISPLAY_TYPES];

    for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        display[i] = (ExynosDisplay *)dev->getDisplay(i);
        event_cnt[i] = 0;
    }
    android_atomic_inc(&(dev->mDRThreadStatus));

    while (dev->mDRLoopStatus) {
        uint32_t result = 0;
        for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++)
            event_cnt[i] = display[i]->mUpdateEventCnt;

        /*
         * If there is no update for more than 100ms, favor the 3D composition mode.
         * If all other conditions are met, mode will be switched to 3D composition.
         */
        usleep(100000);
        for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
            if (display[i]->mDREnable &&
                display[i]->mPlugState == true &&
                event_cnt[i] == display[i]->mUpdateEventCnt) {
                if (display[i]->checkDynamicReCompMode() == DEVICE_2_CLIENT) {
                    display[i]->mUpdateEventCnt = 0;
                    display[i]->setGeometryChanged(GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION);
                    result = 1;
                }
            }
        }
        if (result)
            dev->invalidate();
    }

    android_atomic_dec(&(dev->mDRThreadStatus));

    return NULL;
}
/**
 * @param display
 * @return ExynosDisplay
 */
ExynosDisplay* ExynosDevice::getDisplay(uint32_t display) {
    uint32_t physical_display_num = HWC_NUM_DISPLAY_TYPES - 1;

    if (mDisplays.isEmpty()) {
        goto err;
    }

    if ((display <= physical_display_num) && (mDisplays[display]->mDisplayId == display)) {
        return (ExynosDisplay*)mDisplays[display];
    } else  {
        for (size_t i = (physical_display_num + 1); i < mDisplays.size(); i++) {
            if (mDisplays[i]->mDisplayId == display) {
                return (ExynosDisplay*)mDisplays[i];
            }
        }
    }
err:
    ALOGE("mDisplays.size(%zu), requested display(%d)",
            mDisplays.size(), display);
    return NULL;
}

/**
 * Device Functions for HWC 2.0
 */

int32_t ExynosDevice::createVirtualDisplay(
        uint32_t __unused width, uint32_t __unused height, int32_t* /*android_pixel_format_t*/ __unused format, ExynosDisplay* __unused display) {
    ((ExynosVirtualDisplay*)display)->createVirtualDisplay(width, height, format);
    return 0;
}

/**
 * @param *display
 * @return int32_t
 */
int32_t ExynosDevice::destroyVirtualDisplay(ExynosDisplay* __unused display) {
    ((ExynosVirtualDisplay *)display)->destroyVirtualDisplay();
    return 0;
}

void ExynosDevice::dump(uint32_t *outSize, char *outBuffer) {
    /* TODO : Dump here */

    if (outSize == NULL) {
        ALOGE("%s:: outSize is null", __func__);
        return;
    }

    ExynosDisplay *display = mDisplays[HWC_DISPLAY_PRIMARY];
    ExynosDisplay *external_display = mDisplays[HWC_DISPLAY_EXTERNAL];
    ExynosDisplay *virtual_display = mDisplays[HWC_DISPLAY_VIRTUAL];

    android::String8 result;
    result.append("\n\n");

    struct tm* localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastUeventTime.tv_sec);
    result.appendFormat("lastUeventTime(%02d:%02d:%02d.%03lu) lastTimestamp(%" PRIu64 ")\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastUeventTime.tv_usec/1000, mTimestamp);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastEnableVsyncTime.tv_sec);
    result.appendFormat("lastEnableVsyncTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastEnableVsyncTime.tv_usec/1000);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastDisableVsyncTime.tv_sec);
    result.appendFormat("lastDisableVsyncTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastDisableVsyncTime.tv_usec/1000);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastValidateTime.tv_sec);
    result.appendFormat("lastValidateTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastValidateTime.tv_usec/1000);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastPresentTime.tv_sec);
    result.appendFormat("lastPresentTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastPresentTime.tv_usec/1000);

    display->dump(result);

    if (external_display->mPlugState == true) {
        external_display->dump(result);
    }

    if (virtual_display->mPlugState == true) {
        virtual_display->dump(result);
    }

    if (outBuffer == NULL) {
        *outSize = (uint32_t)result.length();
    } else {
        if (*outSize == 0) {
            ALOGE("%s:: outSize is 0", __func__);
            return;
        }
        uint32_t copySize = *outSize;
        if (*outSize > result.size())
            copySize = (uint32_t)result.size();
        ALOGI("HWC dump:: resultSize(%zu), outSize(%d), copySize(%d)", result.size(), *outSize, copySize);
        strlcpy(outBuffer, result.string(), copySize);
    }

    return;
}

uint32_t ExynosDevice::getMaxVirtualDisplayCount() {
#ifdef USES_VIRTUAL_DISPLAY
    return 1;
#else
    return 0;
#endif
}

int32_t ExynosDevice::registerCallback (
        int32_t descriptor, hwc2_callback_data_t callbackData,
        hwc2_function_pointer_t point) {
    if (descriptor < 0 || descriptor > HWC2_CALLBACK_VSYNC)
        return HWC2_ERROR_BAD_PARAMETER;

    mCallbackInfos[descriptor].callbackData = callbackData;
    mCallbackInfos[descriptor].funcPointer = point;

    /* Call hotplug callback for primary display*/
    if (descriptor == HWC2_CALLBACK_HOTPLUG) {
        HWC2_PFN_HOTPLUG callbackFunc =
            (HWC2_PFN_HOTPLUG)mCallbackInfos[descriptor].funcPointer;
        if (callbackFunc != NULL)
            callbackFunc(callbackData, HWC_DISPLAY_PRIMARY, HWC2_CONNECTION_CONNECTED);
    }

    if (descriptor == HWC2_CALLBACK_VSYNC)
        mResourceManager->doPreProcessing();

    return HWC2_ERROR_NONE;
}

void ExynosDevice::invalidate()
{
    HWC2_PFN_REFRESH callbackFunc =
        (HWC2_PFN_REFRESH)mCallbackInfos[HWC2_CALLBACK_REFRESH].funcPointer;
    if (callbackFunc != NULL)
        callbackFunc(mCallbackInfos[HWC2_CALLBACK_REFRESH].callbackData,
                HWC_DISPLAY_PRIMARY);
    else
        ALOGE("%s:: refresh callback is not registered", __func__);

}

void ExynosDevice::setHWCDebug(unsigned int debug)
{
    hwcDebug = debug;
}

uint32_t ExynosDevice::getHWCDebug()
{
    return hwcDebug;
}

void ExynosDevice::setHWCFenceDebug(uint32_t typeNum, uint32_t ipNum, uint32_t mode)
{
    if (typeNum > FENCE_TYPE_ALL || typeNum < 0 || ipNum > FENCE_IP_ALL || ipNum < 0
            || mode > 1 || mode < 0) {
        ALOGE("%s:: input is not valid type(%u), IP(%u), mode(%d)", __func__, typeNum, ipNum, mode);
        return;
    }

    uint32_t value = 0;

    if (typeNum == FENCE_TYPE_ALL)
        value = (1 << FENCE_TYPE_ALL) - 1;
    else
        value = 1 << typeNum;

    if (ipNum == FENCE_IP_ALL) {
        for (uint32_t i = 0; i < FENCE_IP_ALL; i++) {
            if (mode)
                hwcFenceDebug[i] |= value;
            else
                hwcFenceDebug[i] &= (~value);
        }
    } else {
        if (mode)
            hwcFenceDebug[ipNum] |= value;
        else
            hwcFenceDebug[ipNum] &= (~value);
    }

}

void ExynosDevice::getHWCFenceDebug()
{
    for (uint32_t i = 0; i < FENCE_IP_ALL; i++)
        ALOGE("[HWCFenceDebug] IP_Number(%d) : Debug(%x)", i, hwcFenceDebug[i]);
}

void ExynosDevice::setHWCControl(uint32_t display, uint32_t ctrl, int32_t val)
{
    ExynosDisplay *exynosDisplay = NULL;
    switch (ctrl) {
        case HWC_CTL_FORCE_GPU:
            ALOGI("%s::HWC_CTL_FORCE_GPU on/off=%d", __func__, val);
            exynosHWCControl.forceGpu = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_WINDOW_UPDATE:
            ALOGI("%s::HWC_CTL_WINDOW_UPDATE on/off=%d", __func__, val);
            exynosHWCControl.windowUpdate = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_FORCE_PANIC:
            ALOGI("%s::HWC_CTL_FORCE_PANIC on/off=%d", __func__, val);
            exynosHWCControl.forcePanic = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            break;
        case HWC_CTL_SKIP_STATIC:
            ALOGI("%s::HWC_CTL_SKIP_STATIC on/off=%d", __func__, val);
            exynosHWCControl.skipStaticLayers = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            break;
        case HWC_CTL_SKIP_M2M_PROCESSING:
            ALOGI("%s::HWC_CTL_SKIP_M2M_PROCESSING on/off=%d", __func__, val);
            exynosHWCControl.skipM2mProcessing = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            break;
        case HWC_CTL_SKIP_RESOURCE_ASSIGN:
            ALOGI("%s::HWC_CTL_SKIP_RESOURCE_ASSIGN on/off=%d", __func__, val);
            exynosHWCControl.skipResourceAssign = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_SKIP_VALIDATE:
            ALOGI("%s::HWC_CTL_SKIP_VALIDATE on/off=%d", __func__, val);
            exynosHWCControl.skipValidate = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_DUMP_MID_BUF:
            ALOGI("%s::HWC_CTL_DUMP_MID_BUF on/off=%d", __func__, val);
            exynosHWCControl.dumpMidBuf = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_DISPLAY_MODE:
            ALOGI("%s::HWC_CTL_DISPLAY_MODE mode=%d", __func__, val);
            setDisplayMode((uint32_t)val);
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        // Support DDI scalser {
        case HWC_CTL_DDI_RESOLUTION_CHANGE:
            ALOGI("%s::HWC_CTL_DDI_RESOLUTION_CHANGE mode=%d", __func__, val);
            exynosDisplay = (ExynosDisplay*)getDisplay(display);
            uint32_t width, height;

            /* TODO: Add branch here for each resolution/index */
            switch(val) {
            case 1:
            case 2:
            case 3:
            default:
                width = 1440; height = 2960;
                break;
            }

            if (exynosDisplay == NULL) {
                for (uint32_t i = 0; i < mDisplays.size(); i++) {
                    mDisplays[i]->setDDIScalerEnable(width, height);
                }
            } else {
                exynosDisplay->setDDIScalerEnable(width, height);
            }
            setGeometryChanged(GEOMETRY_DISPLAY_RESOLUTION_CHANGED);
            invalidate();
            break;
        // } Support DDI scaler
        case HWC_CTL_ENABLE_COMPOSITION_CROP:
        case HWC_CTL_ENABLE_EXYNOSCOMPOSITION_OPT:
        case HWC_CTL_ENABLE_CLIENTCOMPOSITION_OPT:
        case HWC_CTL_USE_MAX_G2D_SRC:
        case HWC_CTL_ENABLE_HANDLE_LOW_FPS:
        case HWC_CTL_ENABLE_EARLY_START_MPP:
            exynosDisplay = (ExynosDisplay*)getDisplay(display);
            if (exynosDisplay == NULL) {
                for (uint32_t i = 0; i < mDisplays.size(); i++) {
                    mDisplays[i]->setHWCControl(ctrl, val);
                }
            } else {
                exynosDisplay->setHWCControl(ctrl, val);
            }
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_DYNAMIC_RECOMP:
            ALOGI("%s::HWC_CTL_DYNAMIC_RECOMP on/off = %d", __func__, val);
            this->setDynamicRecomposition((unsigned int)val);
            break;
        case HWC_CTL_ENABLE_FENCE_TRACER:
            ALOGI("%s::HWC_CTL_ENABLE_FENCE_TRACER on/off=%d", __func__, val);
            exynosHWCControl.fenceTracer = (unsigned int)val;
            break;
        case HWC_CTL_SYS_FENCE_LOGGING:
            ALOGI("%s::HWC_CTL_SYS_FENCE_LOGGING on/off=%d", __func__, val);
            exynosHWCControl.sysFenceLogging = (unsigned int)val;
            break;
        case HWC_CTL_DO_FENCE_FILE_DUMP:
            ALOGI("%s::HWC_CTL_DO_FENCE_FILE_DUMP on/off=%d", __func__, val);
            exynosHWCControl.doFenceFileDump = (unsigned int)val;
            break;
        default:
            ALOGE("%s: unsupported HWC_CTL (%d)", __func__, ctrl);
            break;
    }
}

void ExynosDevice::setDisplayMode(uint32_t displayMode)
{
    exynosHWCControl.displayMode = displayMode;
}

void ExynosDevice::setDynamicRecomposition(unsigned int on)
{
    exynosHWCControl.useDynamicRecomp = on;
}

uint32_t ExynosDevice::checkConnection(uint32_t display)
{
    int ret = 0;
	ExynosExternalDisplay *external_display = (ExynosExternalDisplay *)mDisplays[HWC_DISPLAY_EXTERNAL];
	ExynosVirtualDisplay *virtual_display = NULL;
	virtual_display = (ExynosVirtualDisplay *)mDisplays[HWC_DISPLAY_VIRTUAL];

    switch(display) {
        case HWC_DISPLAY_PRIMARY:
            return 1;
        case HWC_DISPLAY_EXTERNAL:
            if (external_display->mPlugState)
                return 1;
            else
                return 0;
        case HWC_DISPLAY_VIRTUAL:
            if (virtual_display->mPlugState)
                return 1;
            else
                return 0;
        default:
            return 0;
    }
    return ret;
}

void ExynosDevice::getCapabilities(uint32_t *outCount, int32_t* outCapabilities)
{
#ifdef HWC_SKIP_VALIDATE
    if (outCapabilities == NULL) {
        *outCount = 1;
        return;
    }
    outCapabilities[0] = HWC2_CAPABILITY_SKIP_VALIDATE;
#else
    *outCount = 0;
#endif
    return;
}

void ExynosDevice::getAllocator(GrallocWrapper::Mapper** mapper, GrallocWrapper::Allocator** allocator)
{
    if ((mMapper == NULL) && (mAllocator == NULL)) {
        ALOGI("%s:: Allocator is created", __func__);
        mMapper = new GrallocWrapper::Mapper();
        mAllocator = new GrallocWrapper::Allocator(*mMapper);
    }
    *mapper = mMapper;
    *allocator = mAllocator;
}

void ExynosDevice::clearGeometryChanged()
{
    mGeometryChanged = 0;
}

bool ExynosDevice::canSkipValidate()
{
    /*
     * This should be called by presentDisplay()
     * when presentDisplay() is called without validateDisplay() call
     */

    int ret = 0;
    if (exynosHWCControl.skipValidate == false)
        return false;

    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        /*
         * Check all displays.
         * Resource assignment can have problem if validateDisplay is skipped
         * on only some displays.
         * All display's validateDisplay should be skipped or all display's validateDisplay
         * should not be skipped.
         */
        if (mDisplays[i]->mPlugState) {
            /*
             * presentDisplay is called without validateDisplay.
             * Call functions that should be called in validateDiplay
             */
            mDisplays[i]->doPreProcessing();
            mDisplays[i]->checkLayerFps();

            if ((ret = mDisplays[i]->canSkipValidate()) != NO_ERROR) {
                HDEBUGLOGD(eDebugSkipValidate, "Display[%d] can't skip validate (%d), renderingState(%d), geometryChanged(0x%" PRIx64 ")",
                        mDisplays[i]->mType, ret,
                        mDisplays[i]->mRenderingState, mGeometryChanged);
                return false;
            } else {
                HDEBUGLOGD(eDebugSkipValidate, "Display[%d] can skip validate (%d), renderingState(%d), geometryChanged(0x%" PRIx64 ")",
                        mDisplays[i]->mType, ret,
                        mDisplays[i]->mRenderingState, mGeometryChanged);
            }
        }
    }
    return true;
}

bool ExynosDevice::validateFences(ExynosDisplay *display) {

    if (!validateFencePerFrame(display)) {
        String8 errString;
        errString.appendFormat("You should doubt fence leak!\n");
        ALOGE("%s", errString.string());
        saveFenceTrace(display);
        return false;
    }

    if (fenceWarn(display, MAX_FENCE_THRESHOLD)) {
        String8 errString;
        errString.appendFormat("Fence leak!\n");
        printLeakFds(display);
        ALOGE("Fence leak! --");
        saveFenceTrace(display);
        return false;
    }

    if (exynosHWCControl.doFenceFileDump) {
        ALOGE("Fence file dump !");
        if (mFenceLogSize != 0)
            ALOGE("Fence file not empty!");
        saveFenceTrace(display);
        exynosHWCControl.doFenceFileDump = false;
    }

    return true;
}

void ExynosDevice::compareVsyncPeriod() {

    ExynosDisplay *primary_display = mDisplays[HWC_DISPLAY_PRIMARY];
    ExynosDisplay *external_display = mDisplays[HWC_DISPLAY_EXTERNAL];

    mVsyncDisplay = HWC_DISPLAY_PRIMARY;

    if (external_display->mPowerModeState == HWC2_POWER_MODE_OFF) {
        return;
    } else if (primary_display->mPowerModeState == HWC2_POWER_MODE_OFF) {
        mVsyncDisplay = HWC_DISPLAY_EXTERNAL;
        return;
    } else if (((primary_display->mPowerModeState == HWC2_POWER_MODE_DOZE) ||
            (primary_display->mPowerModeState == HWC2_POWER_MODE_DOZE_SUSPEND)) &&
            (external_display->mVsyncPeriod >= DOZE_VSYNC_PERIOD)) { /*30fps*/
        mVsyncDisplay = HWC_DISPLAY_EXTERNAL;
        return;
    } else if (primary_display->mVsyncPeriod <= external_display->mVsyncPeriod) {
        mVsyncDisplay = HWC_DISPLAY_EXTERNAL;
        return;
    }

    return;
}

ExynosDeviceInterface::~ExynosDeviceInterface()
{
}

ExynosDevice::ExynosDeviceFbInterface::ExynosDeviceFbInterface(ExynosDevice *exynosDevice)
{
    mUseQuery = false;
    mExynosDevice = exynosDevice;
}

ExynosDevice::ExynosDeviceFbInterface::~ExynosDeviceFbInterface()
{
    /* TODO kill threads here */
    pthread_kill(mEventHandlerThread, SIGTERM);
    pthread_join(mEventHandlerThread, NULL);
}

void ExynosDevice::ExynosDeviceFbInterface::init(ExynosDevice *exynosDevice)
{
    int ret = 0;
    mExynosDevice = exynosDevice;

    ExynosDisplay *primaryDisplay = (ExynosDisplay*)mExynosDevice->getDisplay(HWC_DISPLAY_PRIMARY);
    ExynosDisplayInterface *displayInterface = primaryDisplay->mDisplayInterface;
    mDisplayFd = displayInterface->getDisplayFd();
    updateRestrictions();

    /** Event handler thread creation **/
    ret = pthread_create(&mEventHandlerThread, NULL, hwc_eventHndler_thread, mExynosDevice);
    if (ret) {
        ALOGE("failed to start vsync thread: %s", strerror(ret));
        ret = -ret;
    }
}

int32_t ExynosDevice::ExynosDeviceFbInterface::makeDPURestrictions() {
    int i, j, cnt = 0;
    int32_t ret = 0;

    struct dpp_restrictions_info *dpuInfo = &mDPUInfo.dpuInfo;
    HDEBUGLOGD(eDebugDefault, "DPP ver : %d, cnt : %d", dpuInfo->ver, dpuInfo->dpp_cnt);
    ExynosResourceManager *resourceManager = mExynosDevice->mResourceManager;

    /* format resctriction */
    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        dpp_restriction r = dpuInfo->dpp_ch[i].restriction;
        HDEBUGLOGD(eDebugDefault, "id : %d, format count : %d", i, r.format_cnt);
    }

    restriction_key_t queried_format_table[1024];

    /* Check attribute overlap */
    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        for (j = 0; j < dpuInfo->dpp_cnt; j++){
            if (i >= j) continue;
            dpp_ch_restriction r1 = dpuInfo->dpp_ch[i];
            dpp_ch_restriction r2 = dpuInfo->dpp_ch[j];
            /* If attribute is same, will not be added to table */
            if (r1.attr == r2.attr) {
                mDPUInfo.overlap[j] = true;
            }
        }
        HDEBUGLOGD(eDebugDefault, "Index : %d, overlap %d", i, mDPUInfo.overlap[i]);
    }

    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        if (mDPUInfo.overlap[i]) continue;
        dpp_restriction r = dpuInfo->dpp_ch[i].restriction;
        mpp_phycal_type_t hwType = resourceManager->getPhysicalType(i);
        for (j = 0; j < r.format_cnt; j++){
            if (S3CFormatToHalFormat(r.format[j]) != HAL_PIXEL_FORMAT_EXYNOS_UNDEFINED) {
                queried_format_table[cnt].hwType = hwType;
                queried_format_table[cnt].nodeType = NODE_NONE;
                queried_format_table[cnt].format = S3CFormatToHalFormat(r.format[j]);
                queried_format_table[cnt].reserved = 0;
                resourceManager->makeFormatRestrictions(queried_format_table[cnt], r.format[j]);
                cnt++;
            }
            HDEBUGLOGD(eDebugDefault, "%s : %d", getMPPStr(hwType).string(), r.format[j]);
        }
    }

    /* Size restriction */
    restriction_size rSize;

    for (i = 0; i < dpuInfo->dpp_cnt; i++){
        if (mDPUInfo.overlap[i]) continue;
        dpp_restriction r = dpuInfo->dpp_ch[i].restriction;

        /* RGB size restrictions */
        rSize.maxDownScale = r.scale_down;
        rSize.maxUpScale = r.scale_up;
        rSize.maxFullWidth = r.dst_f_w.max;
        rSize.maxFullHeight = r.dst_f_h.max;
        rSize.minFullWidth = r.dst_f_w.min;
        rSize.minFullHeight = r.dst_f_h.min;;
        rSize.fullWidthAlign = r.dst_x_align;
        rSize.fullHeightAlign = r.dst_y_align;;
        rSize.maxCropWidth = r.src_w.max;
        rSize.maxCropHeight = r.src_h.max;
        rSize.minCropWidth = r.src_w.min;
        rSize.minCropHeight = r.src_h.min;
        rSize.cropXAlign = r.src_x_align;
        rSize.cropYAlign = r.src_y_align;
        rSize.cropWidthAlign = r.blk_x_align;
        rSize.cropHeightAlign = r.blk_y_align;

        mpp_phycal_type_t hwType = resourceManager->getPhysicalType(i);
        resourceManager->makeSizeRestrictions(hwType, rSize, RESTRICTION_RGB);

        /* YUV size restrictions */
        rSize.minCropWidth = 32; //r.src_w.min;
        rSize.minCropHeight = 32; //r.src_h.min;
        rSize.fullWidthAlign = max(r.dst_x_align, YUV_CHROMA_H_SUBSAMPLE);
        rSize.fullHeightAlign = max(r.dst_y_align, YUV_CHROMA_V_SUBSAMPLE);
        rSize.cropXAlign = max(r.src_x_align, YUV_CHROMA_H_SUBSAMPLE);
        rSize.cropYAlign = max(r.src_y_align, YUV_CHROMA_V_SUBSAMPLE);
        rSize.cropWidthAlign = max(r.blk_x_align, YUV_CHROMA_H_SUBSAMPLE);
        rSize.cropHeightAlign = max(r.blk_y_align, YUV_CHROMA_V_SUBSAMPLE);

        resourceManager->makeSizeRestrictions(hwType, rSize, RESTRICTION_YUV);
    }
    return ret;
}

int32_t ExynosDevice::ExynosDeviceFbInterface::updateFeatureTable() {

    struct dpp_restrictions_info *dpuInfo = &mDPUInfo.dpuInfo;
    ExynosResourceManager *resourceManager = mExynosDevice->mResourceManager;
    uint32_t featureTableCnt = resourceManager->getFeatureTableSize();
    int attrMapCnt = sizeof(dpu_attr_map_table)/sizeof(dpu_attr_map_t);
    int dpp_cnt = dpuInfo->dpp_cnt;
    int32_t ret = 0;

    HDEBUGLOGD(eDebugDefault, "Before");
    for (uint32_t j = 0; j < featureTableCnt; j++){
        HDEBUGLOGD(eDebugDefault, "type : %d, feature : 0x%lx",
            feature_table[j].hwType,
            (unsigned long)feature_table[j].attr);
    }

    // dpp count
    for (int i = 0; i < dpp_cnt; i++){
        dpp_ch_restriction c_r = dpuInfo->dpp_ch[i];
        if (mDPUInfo.overlap[i]) continue;
        HDEBUGLOGD(eDebugDefault, "DPU attr : (ch:%d), 0x%lx", i, (unsigned long)c_r.attr);
        mpp_phycal_type_t hwType = resourceManager->getPhysicalType(i);
        // feature table count
        for (uint32_t j = 0; j < featureTableCnt; j++){
            if (feature_table[j].hwType == hwType) {
                // dpp attr count
                for (int k = 0; k < attrMapCnt; k++) {
                    if (c_r.attr & (1 << dpu_attr_map_table[k].dpp_attr)) {
                        feature_table[j].attr |= dpu_attr_map_table[k].hwc_attr;
                    }
                }
            }
        }
    }

    HDEBUGLOGD(eDebugDefault, "After");
    for (uint32_t j = 0; j < featureTableCnt; j++){
        HDEBUGLOGD(eDebugDefault, "type : %d, feature : 0x%lx",
            feature_table[j].hwType,
            (unsigned long)feature_table[j].attr);
    }
    return ret;
}


void ExynosDevice::ExynosDeviceFbInterface::updateRestrictions()
{
    struct dpp_restrictions_info *dpuInfo = &mDPUInfo.dpuInfo;
    int32_t ret = 0;

    if ((ret = ioctl(mDisplayFd, EXYNOS_DISP_RESTRICTIONS, dpuInfo)) < 0) {
        ALOGI("EXYNOS_DISP_RESTRICTIONS ioctl failed: %s", strerror(errno));
        mUseQuery = false;
        return;
    }
    if ((ret = makeDPURestrictions()) != NO_ERROR) {
        ALOGE("makeDPURestrictions fail");
    } else if ((ret = updateFeatureTable()) != NO_ERROR) {
        ALOGE("updateFeatureTable fail");
    }

    if (ret == NO_ERROR)
        mUseQuery = true;
    else
        mUseQuery = false;

    return;
}
