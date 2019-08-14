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
    memset(&m_srcBuf, 0, sizeof(m_srcBuf));
    memset(&m_dstBuf, 0, sizeof(m_dstBuf));
    memset(&m_srcPlanes, 0, sizeof(m_srcPlanes));
    memset(&m_dstPlanes, 0, sizeof(m_dstPlanes));

    m_srcBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    m_dstBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    m_srcBuf.m.planes = m_srcPlanes;
    m_dstBuf.m.planes = m_dstPlanes;

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

bool LibScalerForJpeg::QBuf()
{
    if (ioctl(m_fdScaler, VIDIOC_QBUF, &m_srcBuf) < 0) {
        ALOGE("Failed to QBUF for the source buffer");
        return false;
    }

    if (ioctl(m_fdScaler, VIDIOC_QBUF, &m_dstBuf) < 0) {
        ALOGERR("Failed to QBUF for the target buffer");
        // cancel the previous queued buffer
        mSrcImage.cancelBuffer();
        return false;
    }

    return true;
}

bool LibScalerForJpeg::DQBuf()
{
    if (ioctl(m_fdScaler, VIDIOC_DQBUF, &m_srcBuf) < 0 ) {
        ALOGE("Failed to DQBuf the source buffer");
        return false;
    }

    if (ioctl(m_fdScaler, VIDIOC_DQBUF, &m_dstBuf) < 0 ) {
        ALOGE("Failed to DQBuf the target buffer");
        return false;
    }

    return true;
}

bool LibScalerForJpeg::RunStream(int srcBuf[], int dstBuf)
{
    if (!mSrcImage.begin(V4L2_MEMORY_DMABUF) || !mDstImage.begin(V4L2_MEMORY_DMABUF))
        return false;

    m_srcBuf.index = 0;
    m_srcBuf.memory = V4L2_MEMORY_DMABUF;
    m_srcBuf.length = mSrcImage.planeCount;
    for (unsigned int i = 0; i < mSrcImage.planeCount; i++) {
        m_srcPlanes[i].m.fd = srcBuf[i];
        m_srcPlanes[i].length = mSrcImage.planeLength[i];
        m_srcPlanes[i].bytesused = mSrcImage.planeLength[i];
    }

    return RunStream(dstBuf);
}

bool LibScalerForJpeg::RunStream(char *srcBuf[], int dstBuf)
{
    if (!mSrcImage.begin(V4L2_MEMORY_USERPTR) || !mDstImage.begin(V4L2_MEMORY_DMABUF))
        return false;

    m_srcBuf.index = 0;
    m_srcBuf.memory = V4L2_MEMORY_USERPTR;
    m_srcBuf.length = mSrcImage.planeCount;
    for (unsigned int i = 0; i < mSrcImage.planeCount; i++) {
        m_srcPlanes[i].m.userptr = reinterpret_cast<unsigned long>(srcBuf[i]);
        m_srcPlanes[i].length = mSrcImage.planeLength[i];
        m_srcPlanes[i].bytesused = mSrcImage.planeLength[i];
    }

    return RunStream(dstBuf);
}

bool LibScalerForJpeg::RunStream(int dstBuf)
{
    // we always configure a single plane buffer to the destination
    m_dstBuf.index = 0;
    m_dstBuf.memory = V4L2_MEMORY_DMABUF;
    m_dstBuf.length = 1;
    m_dstPlanes[0].m.fd = dstBuf;
    m_dstPlanes[0].length = mDstImage.planeLength[0];

    if (!QBuf())
        goto err;

    if (!DQBuf())
        goto err;

    return true;

err:
    return false;
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

    ALOGD("VIDIOC_S_FMT(type=%d, %dx%d, fmt=%x)", fmt.type, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.pixelformat);
    if (ioctl(mDevFd, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGE("Failed to S_FMT for %s", getBufTypeString(bufferType));
        return false;
    }

    memoryType = 0; // new reqbufs is required.

    planeCount = fmt.fmt.pix_mp.num_planes;
    for (unsigned int i = 0; i < planeCount; i++)
        planeLength[i] = fmt.fmt.pix_mp.plane_fmt[i].sizeimage;

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

            ALOGD("REQBUFS(%d, %s, %d)", reqbufs.count, getBufTypeString(bufferType), reqbufs.memory);
            if (ioctl(mDevFd, VIDIOC_REQBUFS, &reqbufs) < 0) {
                ALOGERR("Failed to REQBUFS(0) for %s", getBufTypeString(bufferType));
                return false;
            }
        }

        reqbufs.memory = memtype;
        reqbufs.count = 1;

        ALOGD("REQBUFS(%d, %s, %d)", reqbufs.count, getBufTypeString(bufferType), reqbufs.memory);
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
