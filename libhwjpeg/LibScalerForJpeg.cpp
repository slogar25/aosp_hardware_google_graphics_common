/*
 * Copyright Samsung Electronics Co.,LTD.
 * Copyright (C) 2015 The Android Open Source Project
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

#include "hwjpeg-internal.h"
#include "LibScalerForJpeg.h"

#define SCALER_DEV_NODE "/dev/video50"

static const char *getBufTypeString(unsigned int buftype)
{
    if (buftype == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
        return "source";
    if (buftype == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
        return "destination";
    return "unknown";
}

LibScalerForJpeg::LibScalerForJpeg()
{
    m_fdScaler = open(SCALER_DEV_NODE, O_RDWR);

    if (m_fdScaler < 0) {
        ALOGE("Failed to open %s", SCALER_DEV_NODE);
        return;
    }

    mSrcImage.init(m_fdScaler);
    mDstImage.init(m_fdScaler);

    ALOGD("LibScalerForJpeg Created: %p", this);
}

LibScalerForJpeg::~LibScalerForJpeg()
{
    if (m_fdScaler > 0)
        close(m_fdScaler);

    m_fdScaler = -1;

    ALOGD("LibScalerForJpeg Destroyed: %p", this);
}

bool LibScalerForJpeg::RunStream(int srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen)
{
    if (!mSrcImage.begin(V4L2_MEMORY_DMABUF) || !mDstImage.begin(V4L2_MEMORY_DMABUF))
        return false;

    return queue(srcBuf, srcLen, dstBuf, dstLen);
}

bool LibScalerForJpeg::RunStream(char *srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen)
{
    if (!mSrcImage.begin(V4L2_MEMORY_USERPTR) || !mDstImage.begin(V4L2_MEMORY_DMABUF))
        return false;

    return queue(srcBuf, srcLen, dstBuf, dstLen);
}

bool LibScalerForJpeg::Image::set(unsigned int width, unsigned int height, unsigned int format)
{
    if (same(width, height, format))
        return true;

    if (memoryType != 0) {
        v4l2_requestbuffers reqbufs{};

        reqbufs.type    = bufferType;
        reqbufs.memory  = memoryType;
        reqbufs.count   = 0;

        if (ioctl(mDevFd, VIDIOC_REQBUFS, &reqbufs) < 0) {
            ALOGE("Failed to REQBUFS for(0) %s", getBufTypeString(bufferType));
            return false;
        }
    }

    v4l2_format fmt{};

    fmt.type = bufferType;
    fmt.fmt.pix_mp.pixelformat = format;
    fmt.fmt.pix_mp.width  = width;
    fmt.fmt.pix_mp.height = height;

    if (ioctl(mDevFd, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGE("Failed to S_FMT for %s", getBufTypeString(bufferType));
        return false;
    }

    memoryType = 0; // new reqbufs is required.

    return true;
}

bool LibScalerForJpeg::Image::begin(unsigned int memtype)
{
    if (memoryType != memtype) {
        v4l2_requestbuffers reqbufs{};

        reqbufs.type    = bufferType;

        if (memoryType != 0) {
            reqbufs.memory  = memoryType;
            reqbufs.count   = 0;

            if (ioctl(mDevFd, VIDIOC_REQBUFS, &reqbufs) < 0) {
                ALOGERR("Failed to REQBUFS(0) for %s", getBufTypeString(bufferType));
                return false;
            }
        }

        reqbufs.memory = memtype;
        reqbufs.count = 1;

        if (ioctl(mDevFd, VIDIOC_REQBUFS, &reqbufs) < 0) {
            ALOGERR("Failed to REQBUFS(1) for %s with %d", getBufTypeString(bufferType), reqbufs.memory);
            return false;
        }

        if (ioctl(mDevFd, VIDIOC_STREAMON, &bufferType) < 0) {
            ALOGERR("Failed to STREAMON for %s", getBufTypeString(bufferType));
            return false;
        }

        memoryType = memtype;
    }

    return true;
}

bool LibScalerForJpeg::Image::cancelBuffer()
{
    if (ioctl(mDevFd, VIDIOC_STREAMOFF, &bufferType) < 0) {
        ALOGE("Failed to STREAMOFF for %s", getBufTypeString(bufferType));
        return false;
    }

    if (ioctl(mDevFd, VIDIOC_STREAMON, &bufferType) < 0) {
        ALOGE("Failed to STREAMON for %s", getBufTypeString(bufferType));
        return false;
    }

    return true;
}

bool LibScalerForJpeg::Image::queueBuffer(int buf, size_t len)
{
    v4l2_buffer buffer{};
    v4l2_plane plane[SCALER_MAX_PLANES];

    memset(&plane, 0, sizeof(plane));

    buffer.type = bufferType;
    buffer.memory = memoryType;
    buffer.length = 1;
    buffer.m.planes = plane;
    plane[0].m.fd = buf;
    plane[0].length = len;

    if (ioctl(mDevFd, VIDIOC_QBUF, &buffer) < 0) {
        ALOGERR("Failed to QBUF for %s", getBufTypeString(bufferType));
        return false;
    }

    return true;
}

bool LibScalerForJpeg::Image::queueBuffer(int buf[SCALER_MAX_PLANES], int len[SCALER_MAX_PLANES])
{
    v4l2_buffer buffer{};
    v4l2_plane plane[SCALER_MAX_PLANES];

    memset(&plane, 0, sizeof(plane));

    buffer.type = bufferType;
    buffer.memory = memoryType;
    buffer.length = SCALER_MAX_PLANES;
    for (unsigned int i = 0; i < SCALER_MAX_PLANES; i++) {
        plane[i].m.fd = buf[i];
        plane[i].length = len[i];
    }
    buffer.m.planes = plane;

    if (ioctl(mDevFd, VIDIOC_QBUF, &buffer) < 0) {
        ALOGERR("Failed to QBUF for %s", getBufTypeString(bufferType));
        return false;
    }

    return true;
}

bool LibScalerForJpeg::Image::queueBuffer(char *buf[SCALER_MAX_PLANES], int len[SCALER_MAX_PLANES])
{
    v4l2_buffer buffer{};
    v4l2_plane plane[SCALER_MAX_PLANES];

    memset(&plane, 0, sizeof(plane));

    buffer.type = bufferType;
    buffer.memory = memoryType;
    buffer.length = SCALER_MAX_PLANES;
    for (unsigned int i = 0; i < SCALER_MAX_PLANES; i++) {
        plane[i].m.userptr = reinterpret_cast<unsigned long>(buf[i]);
        plane[i].length = len[i];
    }
    buffer.m.planes = plane;

    if (ioctl(mDevFd, VIDIOC_QBUF, &buffer) < 0) {
        ALOGERR("Failed to QBUF for %s", getBufTypeString(bufferType));
        return false;
    }

    return true;
}

bool LibScalerForJpeg::Image::dequeueBuffer()
{
    v4l2_buffer buffer{};
    v4l2_plane plane[SCALER_MAX_PLANES];

    memset(&plane, 0, sizeof(plane));

    buffer.type = bufferType;
    buffer.memory = memoryType;
    buffer.length = SCALER_MAX_PLANES;

    buffer.m.planes = plane;

    if (ioctl(mDevFd, VIDIOC_DQBUF, &buffer) < 0 ) {
        ALOGERR("Failed to DQBuf %s", getBufTypeString(bufferType));
        return false;
    }

    return true;
}
