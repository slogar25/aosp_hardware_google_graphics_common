/*
 * Copyright Samsung Electronics Co.,LTD.
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cstring>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <log/log.h>

#include "acrylic_internal.h"
#include "acrylic_device.h"

AcrylicDevice::AcrylicDevice(const char *devpath)
    : mDevFD(-1)
{
    mDevPath = new char[std::strlen(devpath) + 1];
    if (mDevPath == NULL) {
        ALOGE("Failed to allocate pathname buffer of %s", devpath);
        return;
    }

    std::strcpy(mDevPath, devpath);
}

AcrylicDevice::~AcrylicDevice()
{
    if (mDevPath)
        delete [] mDevPath;
    if (mDevFD >= 0)
        ::close(mDevFD);
}

bool AcrylicDevice::open()
{
    if (mDevFD >= 0)
        return true;

    if (mDevPath == NULL) {
        ALOGE("Path to device node is not specified");
        return false;
    }

    mDevFD = ::open(mDevPath, O_RDWR);
    if (mDevFD < 0) {
        ALOGERR("Failed to open %s", mDevPath);
        return false;
    }

    ALOGD_TEST("Opened %s on fd %d", mDevPath, mDevFD);

    return true;
}

int AcrylicDevice::ioctl(int cmd, void *arg)
{
    if (!open())
        return -1;

    return ::ioctl(mDevFD, cmd, arg);
}

AcrylicRedundantDevice::AcrylicRedundantDevice(const char *devpath)
    : mFdIdx(0)
{
    for (int i = 0; i < MAX_DEVICE_FD; i++)
        mDevFd[i] = -1;

    mDevPath = new char[std::strlen(devpath) + 1];
    if (mDevPath == NULL) {
        ALOGE("Failed to allocate pathname buffer of %s", devpath);
        return;
    }

    std::strcpy(mDevPath, devpath);
}

AcrylicRedundantDevice::~AcrylicRedundantDevice()
{
    if (mDevPath)
        delete [] mDevPath;

    for (int i = 0; i < MAX_DEVICE_FD; i++)
        ::close(mDevFd[i]);
}

bool AcrylicRedundantDevice::open()
{
    if (mDevPath == NULL) {
        ALOGE("Path to device node is not specified");
        return false;
    }

    if (mDevFd[0] >= 0)
        return true;

    for (int i = 0; i < MAX_DEVICE_FD; i++) {
        if (mDevFd[i] < 0) {
            mDevFd[i] = ::open(mDevPath, O_RDWR);
            if (mDevFd[i] < 0) {
                ALOGERR("Failed to open %s for devfd[%d]", mDevPath, i);
                while (i-- > 0) {
                    ::close(mDevFd[i]);
                    mDevFd[i] = -1;
                }
                return false;
            }

            ALOGD_TEST("Opened %s on devfd[%d] %d", mDevPath, i, mDevFd[i]);
        }
    }

    return true;
}

int AcrylicRedundantDevice::ioctl_unique(int cmd, void *arg)
{
    if (!open())
        return -1;

    return ::ioctl(mDevFd[0], cmd, arg);
}

int AcrylicRedundantDevice::ioctl_current(int cmd, void *arg)
{
    if (!open())
        return -1;

    return ::ioctl(mDevFd[mFdIdx], cmd, arg);
}

int AcrylicRedundantDevice::ioctl_broadcast(int cmd, void *arg) {
    int ret;

    if (!open())
        return -1;

    for (int i = 0; i < MAX_DEVICE_FD; i++) {
        ret = ::ioctl(mDevFd[i], cmd, arg);
        if (ret < 0)
            return ret;
    }

    return 0;
}
