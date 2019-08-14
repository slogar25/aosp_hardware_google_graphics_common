#ifndef __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
#define __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__

#include <linux/videodev2.h>

#include <tuple>

#define SCALER_MAX_PLANES 3

class LibScalerForJpeg {
public:
    LibScalerForJpeg();
    ~LibScalerForJpeg();

    bool SetSrcImage(unsigned int width, unsigned int height, unsigned int v4l2_format) {
        return mSrcImage.set(width, height, v4l2_format);
    }

    bool SetDstImage(unsigned int width, unsigned int height, unsigned int v4l2_format) {
        return mDstImage.set(width, height, v4l2_format);
    }

    bool RunStream(int srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen);
    bool RunStream(char *srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen);

private:
    int m_fdScaler = -1;

    struct Image {
        int mDevFd = -1;
        unsigned int width;
        unsigned int height;
        unsigned int format;
        unsigned int memoryType = 0;
        const unsigned int bufferType;

        Image(unsigned int w, unsigned int h, unsigned int f, unsigned int buftype)
            : width(w), height(h), format(f), bufferType(buftype)
        {
        }

        void init(int fd) { mDevFd = fd; }
        bool set(unsigned int width, unsigned int height, unsigned int format);
        bool same(unsigned int w, unsigned int h, unsigned int f) { return width == w && height == h && format == f; }
        bool begin(unsigned int memtype);
        bool cancelBuffer();
        bool queueBuffer(char *buf[SCALER_MAX_PLANES], int len[SCALER_MAX_PLANES]);
        bool queueBuffer(int buf[SCALER_MAX_PLANES], int len[SCALER_MAX_PLANES]);
        bool queueBuffer(int buf, size_t len);
        bool dequeueBuffer();
    };

    template<class T>
    bool queue(T srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen) {
        if (!mSrcImage.queueBuffer(srcBuf, srcLen))
            return false;

        if (!mDstImage.queueBuffer(dstBuf, dstLen)) {
            mSrcImage.cancelBuffer();
            return false;
        }

        if (!mSrcImage.dequeueBuffer() || !mDstImage.dequeueBuffer()) {
            mSrcImage.cancelBuffer();
            mDstImage.cancelBuffer();
            return false;
        }

        return true;
    }

    Image mSrcImage{0, 0, V4L2_PIX_FMT_YUYV, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
    Image mDstImage{0, 0, V4L2_PIX_FMT_YUYV, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
};

#endif //__HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
