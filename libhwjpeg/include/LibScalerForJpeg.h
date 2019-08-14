#ifndef __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
#define __HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__

#include <linux/videodev2.h>

#include <tuple>
#include <functional>

#define SCALER_MAX_PLANES 3

class LibScalerForJpeg {
public:
    LibScalerForJpeg() { }
    ~LibScalerForJpeg() { }

    bool SetSrcImage(unsigned int width, unsigned int height, unsigned int v4l2_format) {
        return mSrcImage.set(width, height, v4l2_format);
    }

    bool SetDstImage(unsigned int width, unsigned int height, unsigned int v4l2_format) {
        return mDstImage.set(width, height, v4l2_format);
    }

    bool RunStream(int srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen);
    bool RunStream(char *srcBuf[SCALER_MAX_PLANES], int srcLen[SCALER_MAX_PLANES], int dstBuf, size_t dstLen);

private:
    struct Device {
        int mFd;

        Device();
        ~Device();
        bool requestBuffers(unsigned int buftype, unsigned int memtype, unsigned int count);
        bool setFormat(unsigned int buftype, unsigned int format, unsigned int width, unsigned int height);
        bool streamOn(unsigned int buftype);
        bool streamOff(unsigned int buftype);
        bool queueBuffer(unsigned int buftype, std::function<void(v4l2_buffer &)> bufferFiller);
        bool queueBuffer(unsigned int buftype, int buf[SCALER_MAX_PLANES], int len[SCALER_MAX_PLANES]);
        bool queueBuffer(unsigned int buftype, char *buf[SCALER_MAX_PLANES], int len[SCALER_MAX_PLANES]);
        bool queueBuffer(unsigned int buftype, int buf, int len);
        bool dequeueBuffer(unsigned int buftype, unsigned int memtype);
    };

    struct Image {
        Device &mDevice;
        unsigned int width;
        unsigned int height;
        unsigned int format;
        unsigned int memoryType = 0;
        const unsigned int bufferType;

        Image(Device &dev, unsigned int w, unsigned int h, unsigned int f, unsigned int buftype)
            : mDevice(dev), width(w), height(h), format(f), bufferType(buftype)
        { }

        bool set(unsigned int width, unsigned int height, unsigned int format);
        bool begin(unsigned int memtype);
        bool cancelBuffer();

        template <class tBuf, class tLen>
        bool queueBuffer(tBuf buf, tLen len) { return mDevice.queueBuffer(bufferType, buf, len); }
        bool dequeueBuffer() { return mDevice.dequeueBuffer(bufferType, memoryType); }

        bool same(unsigned int w, unsigned int h, unsigned int f) { return width == w && height == h && format == f; }
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

    Device mDevice;
    Image mSrcImage{mDevice, 0, 0, V4L2_PIX_FMT_YUYV, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
    Image mDstImage{mDevice, 0, 0, V4L2_PIX_FMT_YUYV, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
};

#endif //__HARDWARE_EXYNOS_LIBSCALERFORJPEG_H__
