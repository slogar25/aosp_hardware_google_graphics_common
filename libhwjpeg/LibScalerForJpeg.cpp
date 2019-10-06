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

LibScalerForJpeg::LibScalerForJpeg()
{
    memset(&m_srcFmt, 0, sizeof(m_srcFmt));
    memset(&m_dstFmt, 0, sizeof(m_dstFmt));
    memset(&m_srcBuf, 0, sizeof(m_srcBuf));
    memset(&m_dstBuf, 0, sizeof(m_dstBuf));
    memset(&m_srcPlanes, 0, sizeof(m_srcPlanes));
    memset(&m_dstPlanes, 0, sizeof(m_dstPlanes));

    m_srcFmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    m_dstFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    m_srcBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    m_dstBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    m_srcBuf.m.planes = m_srcPlanes;
    m_dstBuf.m.planes = m_dstPlanes;

    m_fdScaler = open(SCALER_DEV_NODE, O_RDWR);

    if (m_fdScaler < 0) {
        ALOGE("Failed to open %s", SCALER_DEV_NODE);
        return;
    }

    m_needReqbuf = true;

    ALOGD("LibScalerForJpeg Created: %p", this);
}

LibScalerForJpeg::~LibScalerForJpeg()
{
    if (m_fdScaler > 0)
        close(m_fdScaler);

    m_fdScaler = -1;

    ALOGD("LibScalerForJpeg Destroyed: %p", this);
}

bool LibScalerForJpeg::SetImage(
        v4l2_format &m_fmt, v4l2_buffer &m_buf, v4l2_plane m_planes[SCALER_MAX_PLANES],
        unsigned int width, unsigned int height, unsigned int v4l2_format,
        void *addrs[SCALER_MAX_PLANES], int mem_type)
{
    /* Format information update*/
    if ((m_needReqbuf == true) ||
            (m_fmt.fmt.pix_mp.pixelformat != v4l2_format ||
            m_fmt.fmt.pix_mp.width != width ||
            m_fmt.fmt.pix_mp.height != height ||
            m_buf.memory != static_cast<v4l2_memory>(mem_type))) {
        m_fmt.fmt.pix_mp.pixelformat = v4l2_format;
        m_fmt.fmt.pix_mp.width  = width;
        m_fmt.fmt.pix_mp.height = height;
        m_buf.memory = static_cast<v4l2_memory>(mem_type);

        // The driver returns the number and length of planes through TRY_FMT.
        if (ioctl(m_fdScaler, VIDIOC_TRY_FMT, &m_fmt) < 0) {
            ALOGE("Failed to TRY_FMT for source");
            m_needReqbuf = true;
            return false;
        }
        m_needReqbuf = true;
    }

    /* Buffer information update*/
    m_buf.index = 0;
    m_buf.length = m_fmt.fmt.pix_mp.num_planes;

    for (unsigned long i = 0; i < m_buf.length; i++) {
        m_planes[i].length = m_fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
        m_planes[i].bytesused = m_planes[i].length;
        if (m_buf.memory == V4L2_MEMORY_DMABUF)
            m_planes[i].m.fd = static_cast<__s32>(reinterpret_cast<long>(addrs[i]));
        else
            m_planes[i].m.userptr = reinterpret_cast<unsigned long>(addrs[i]);
    }

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::SetFormat()
{
    if (ioctl(m_fdScaler, VIDIOC_S_FMT, &m_srcFmt) < 0) {
        ALOGE("Failed to S_FMT for the source");
        return false;
    }

    if (ioctl(m_fdScaler, VIDIOC_S_FMT, &m_dstFmt) < 0) {
        ALOGE("Failed to S_FMT for the target");
        return false;
    }

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::ReqBufs(int count)
{
    v4l2_requestbuffers reqbufs;

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type    = m_srcBuf.type;
    reqbufs.memory  = m_srcBuf.memory;
    reqbufs.count   = count;

    if (ioctl(m_fdScaler, VIDIOC_REQBUFS, &reqbufs) < 0) {
        ALOGE("Failed to REQBUFS for the source buffer");
        return false;
    }

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.type    = m_dstBuf.type;
    reqbufs.memory  = m_dstBuf.memory;
    reqbufs.count   = count;

    if (ioctl(m_fdScaler, VIDIOC_REQBUFS, &reqbufs) < 0) {
        ALOGE("Failed to REQBUFS for the target buffer");
        // rolling back the reqbufs for the source image
        reqbufs.type    = m_srcBuf.type;
        reqbufs.memory  = m_srcBuf.memory;
        reqbufs.count   = 0;
        ioctl(m_fdScaler, VIDIOC_REQBUFS, &reqbufs);
        return false;
    }

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::StreamOn()
{
    if (ioctl(m_fdScaler, VIDIOC_STREAMON, &m_srcBuf.type) < 0) {
        ALOGE("Failed StreamOn for the source buffer");
        return false;
    }

    if (ioctl(m_fdScaler, VIDIOC_STREAMON, &m_dstBuf.type) < 0) {
        ALOGE("Failed StreamOn for the target buffer");
        // cancel the streamon for the source image
        ioctl(m_fdScaler, VIDIOC_STREAMOFF, &m_srcBuf.type);
        return false;
    }

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::QBuf()
{
    if (ioctl(m_fdScaler, VIDIOC_QBUF, &m_srcBuf) < 0) {
        ALOGE("Failed to QBUF for the source buffer");
        return false;
    }

    if (ioctl(m_fdScaler, VIDIOC_QBUF, &m_dstBuf) < 0) {
        ALOGE("Failed to QBUF for the target buffer");
        // cancel the previous queued buffer
        StopStreaming();
        return false;
    }

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::StreamOff()
{
    if (ioctl(m_fdScaler, VIDIOC_STREAMOFF, &m_srcBuf.type) < 0) {
        ALOGE("Failed STREAMOFF for the source");
        return false;
    }

    if (ioctl(m_fdScaler, VIDIOC_STREAMOFF, &m_dstBuf.type) < 0) {
        ALOGE("Failed STREAMOFF for the target");
        return false;
    }

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::StopStreaming()
{
    if (!StreamOff())
        return false;

    if (!ReqBufs(0))
        return false;

    SC_LOGD("%s Success", __func__);

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

    SC_LOGD("%s Success", __func__);

    return true;
}

bool LibScalerForJpeg::RunStream()
{
    if (m_needReqbuf == true) {
        if (!StopStreaming())
            goto err;

        if (!SetFormat())
            goto err;

        if (!ReqBufs())
            goto err;

        if (!StreamOn())
            goto err;
    }

    if (!QBuf())
        goto err;

    if (!DQBuf())
        goto err;

    m_needReqbuf = false;

    SC_LOGD("%s Success", __func__);

    return true;

err:
    m_needReqbuf = true;
    return false;
}
