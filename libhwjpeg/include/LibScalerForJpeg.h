#ifndef __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
#define __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__

#include <linux/videodev2.h>

#include <tuple>

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

    bool RunStream(int srcBuf[], int dstBuf);
    bool RunStream(char *srcBuf[], int dstBuf);

private:
    int m_fdScaler = -1;

    struct Image {
        int mDevFd = -1;
        unsigned int width;
        unsigned int height;
        unsigned int format;
        unsigned int memoryType = 0;
        unsigned int planeCount = 1;
        unsigned int planeLength[3] = {0, 0, 0};
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
        bool queueBuffer(char *buf[]);
        bool queueBuffer(int buf[]);
        bool dequeueBuffer();
    };

    template<class T>
    bool queue(T srcBuf, int dstBuf) {
        if (!mSrcImage.queueBuffer(srcBuf))
            return false;

        if (!mDstImage.queueBuffer(&dstBuf)) {
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
