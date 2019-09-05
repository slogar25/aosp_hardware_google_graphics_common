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

#include "ExynosDeviceFbInterface.h"
#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosExternalDisplayModule.h"
#include "ExynosHWCHelper.h"
#include "ExynosHWCDebug.h"

extern update_time_info updateTimeInfo;
extern feature_support_t feature_table[];

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

    ExynosDeviceFbInterface *deviceFbInterface = (ExynosDeviceFbInterface*)data;
    ExynosDevice *dev = deviceFbInterface->getExynosDevice();

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
    while (deviceFbInterface->mEventHandlerRunning) {
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

ExynosDeviceFbInterface::ExynosDeviceFbInterface(ExynosDevice *exynosDevice)
{
    mUseQuery = false;
    mExynosDevice = exynosDevice;
}

ExynosDeviceFbInterface::~ExynosDeviceFbInterface()
{
    mEventHandlerRunning = false;
    mEventHandlerThread.join();
}

void ExynosDeviceFbInterface::init(ExynosDevice *exynosDevice)
{
    mExynosDevice = exynosDevice;

    ExynosDisplay *primaryDisplay = (ExynosDisplay*)mExynosDevice->getDisplay(HWC_DISPLAY_PRIMARY);
    mDisplayFd = primaryDisplay->mDisplayInterface->getDisplayFd();
    updateRestrictions();

    /** Event handler thread creation **/
    mEventHandlerThread = std::thread(&hwc_eventHndler_thread, this);
}

int32_t ExynosDeviceFbInterface::makeDPURestrictions() {
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

int32_t ExynosDeviceFbInterface::updateFeatureTable() {

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

void ExynosDeviceFbInterface::updateRestrictions()
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
