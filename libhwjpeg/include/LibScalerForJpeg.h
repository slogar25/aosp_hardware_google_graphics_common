#ifndef __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
#define __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__

#include <linux/videodev2.h>
#define SCALER_DEV_NODE "/dev/video50"
#define SCALER_MAX_PLANES 3

#define SC_LOGE(fmt, args...) ((void)ALOG(LOG_ERROR, LOG_TAG, "%s: " fmt, __func__, ##args))

#ifdef SC_DEBUG
#define SC_LOGD(args...) ((void)ALOG(LOG_INFO, LOG_TAG, ##args))
#else
#define SC_LOGD(args...) do { } while (0)
#endif

class LibScalerForJpeg {
public:
    LibScalerForJpeg();
    ~LibScalerForJpeg();

    bool SetSrcImage(
            unsigned int width, unsigned int height, unsigned int v4l2_format,
            void *addrs[SCALER_MAX_PLANES], int mem_type) {
        return SetImage(m_srcFmt, m_srcBuf, m_srcPlanes,
                width, height, v4l2_format, addrs, mem_type);
    }

    bool SetDstImage(
            unsigned int width, unsigned int height, unsigned int v4l2_format,
            void *addrs[SCALER_MAX_PLANES], int mem_type) {
        return SetImage(m_dstFmt, m_dstBuf, m_dstPlanes,
                width, height, v4l2_format, addrs, mem_type);
    }

    bool RunStream();
private:
    int m_fdScaler;
    bool m_needReqbuf;

    v4l2_format m_srcFmt;
    v4l2_format m_dstFmt;
    v4l2_buffer m_srcBuf;
    v4l2_buffer m_dstBuf;
    v4l2_plane m_srcPlanes[SCALER_MAX_PLANES];
    v4l2_plane m_dstPlanes[SCALER_MAX_PLANES];

    bool SetImage(
            v4l2_format &m_fmt, v4l2_buffer &m_buf, v4l2_plane m_planes[SCALER_MAX_PLANES],
            unsigned int width, unsigned int height, unsigned int v4l2_format,
            void *addrs[SCALER_MAX_PLANES], int mem_type);

    bool SetFormat();
    bool ReqBufs(int count = 1);
    bool StreamOn();
    bool StreamOff();
    bool QBuf();
    bool DQBuf();
    bool StopStreaming();
};

#endif //__HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
