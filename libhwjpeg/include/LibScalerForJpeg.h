#ifndef __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
#define __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__

#include <linux/videodev2.h>
#define SCALER_DEV_NODE "/dev/video50"
#define SCALER_MAX_PLANES 3

#include <tuple>

class LibScalerForJpeg {
public:
    LibScalerForJpeg();
    ~LibScalerForJpeg();

    bool SetSrcImage(unsigned int width, unsigned int height, unsigned int v4l2_format, char *buf[SCALER_MAX_PLANES]);
    bool SetSrcImage(unsigned int width, unsigned int height, unsigned int v4l2_format, int buf[SCALER_MAX_PLANES]);
    bool SetDstImage(unsigned int width, unsigned int height, unsigned int v4l2_format, int buf);

    bool RunStream();
private:
    int m_fdScaler;
    bool m_needReqbuf;

    struct Image {
        unsigned int width;
        unsigned int height;
        unsigned int format;
        unsigned int planeCount;
        unsigned int planeLength[3];

        Image(unsigned int w, unsigned int h, unsigned int f): width(w), height(h), format(f) { }

        void set(unsigned int w, unsigned int h, unsigned int f) {
            width = w;
            height = h;
            format = f;
        }

        void setBufferRequirements(unsigned int plane_count, unsigned int length0, unsigned int length1, unsigned int length2) {
            planeCount = plane_count;
            planeLength[0] = length0;
            planeLength[1] = length1;
            planeLength[2] = length2;
        }

        bool same(unsigned int w, unsigned int h, unsigned int f) { return width == w && height == h && format == f; }
    };

    Image mSrcImage{512, 384, V4L2_PIX_FMT_YUYV};
    Image mDstImage{512, 384, V4L2_PIX_FMT_YUYV};

    v4l2_buffer m_srcBuf;
    v4l2_buffer m_dstBuf;
    v4l2_plane m_srcPlanes[SCALER_MAX_PLANES];
    v4l2_plane m_dstPlanes[SCALER_MAX_PLANES];

    bool SetImage(
            Image &img, v4l2_buffer &m_buf, v4l2_plane m_planes[SCALER_MAX_PLANES],
            unsigned int width, unsigned int height, unsigned int v4l2_format, unsigned int memtype, unsigned int buftype);

    bool SetFormat();
    bool ReqBufs(int count = 1);
    bool StreamOn();
    bool StreamOff();
    bool QBuf();
    bool DQBuf();
    bool StopStreaming();
};

#endif //__HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
