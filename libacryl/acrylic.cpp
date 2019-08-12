/*
 * Copyright Samsung Electronics Co.,LTD.
 * Copyright (C) 2016 The Android Open Source Project
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

#include <algorithm>

#include <log/log.h>

#include <hardware/exynos/acryl.h>

#include "acrylic_internal.h"

Acrylic::Acrylic(const HW2DCapability &capability)
    : mLayerCount(0), mLayers(nullptr), mCapability(capability), mHasDefaultColor(false),
      mMaxTargetLuminance(100), mMinTargetLuminance(0), mTargetDisplayInfo(nullptr),
      mCanvas(this, AcrylicCanvas::CANVAS_TARGET)
{
    ALOGD_TEST("Created a new Acrylic on %p", this);
}

Acrylic::~Acrylic()
{
    mCanvas.disconnectLayer();

    for (unsigned int i = 0; i < mLayerCount; i++) {
        mLayers[i]->disconnectLayer();
        removeTransitData(mLayers[i]);
    }

    delete mLayers;

    ALOGD_TEST("Destroyed Acrylic on %p", this);
}

AcrylicLayer *Acrylic::createLayer()
{
    if (mLayerCount >= getCapabilities().maxLayerCount()) {
        ALOGE("Full of composit layer: current %u, max %u",
                mLayerCount, getCapabilities().maxLayerCount());
        return NULL;
    }

    if (mLayers == NULL)
        mLayers = new AcrylicLayer*[getCapabilities().maxLayerCount()];

    if (mLayers == NULL) {
        ALOGE("Failed to allocate layer array");
        return NULL;
    }

    AcrylicLayer *layer = new AcrylicLayer(this);
    if (!layer) {
        ALOGE("Failed to create a new compositing layer");
        return NULL;
    }

    mLayers[mLayerCount++] = layer;

    ALOGD_TEST("A new Acrylic layer is created. Total %d layers", mLayerCount);

    return layer;
}

void Acrylic::removeLayer(AcrylicLayer *layer)
{
    for (unsigned int i = 0; i < mLayerCount; i++) {
        if (mLayers[i] == layer) {
            ALOGD_TEST("Removed an Acrylic layer (%d/%d)", i, mLayerCount);
            mLayerCount--;
            while (i < mLayerCount) {
                mLayers[i] = mLayers[i + 1];
                i++;
            }
            removeTransitData(layer);
            return;
        }
    }

    ALOGE("Deleting an unregistered layer");
}

int Acrylic::prioritize(int priority)
{
    if ((priority < -1) || (priority > 15)) {
        ALOGE("Invalid priority %d", priority);
        return -1;
    }

    return 0;
}

bool Acrylic::requestPerformanceQoS(AcrylicPerformanceRequest __unused *request)
{
    return true;
}

bool Acrylic::setHDRToneMapCoefficients(uint32_t __unused *matrix[2], int __unused num_elements)
{
    return true;
}

bool Acrylic::validateAllLayers()
{
    const HW2DCapability &cap = getCapabilities();

    if (!mCanvas.isSettingOkay()) {
        ALOGE("Incomplete settting (flags: %#x) on the target layer",
              mCanvas.getSettingFlags());
        return false;
    }

    if (mCanvas.isCompressed() && !cap.isFeatureSupported(HW2DCapability::FEATURE_AFBC_ENCODE)) {
        ALOGE("AFBC encoding is not supported");
        return false;
    }

    if (mCanvas.isUOrder() && !cap.isFeatureSupported(HW2DCapability::FEATURE_UORDER_WRITE)) {
        ALOGE("Writing in U-Order is not supported");
        return false;
    }

    bool prot = false;
    hw2d_rect_t rect;
    hw2d_coord_t xy = mCanvas.getImageDimension();

    for (unsigned int i = 0; i < mLayerCount; i++) {
        if (!mLayers[i]->isSettingOkay()) {
            ALOGE("Incomplete settting (flags: %#x) on layer %d",
                  mLayers[i]->getSettingFlags(), i);
            return false;
        }

        if (mLayers[i]->isCompressed() && !cap.isFeatureSupported(HW2DCapability::FEATURE_AFBC_DECODE)) {
            ALOGE("AFBC decoding is not supported");
            return false;
        }

        if (mLayers[i]->isUOrder() && !cap.isFeatureSupported(HW2DCapability::FEATURE_UORDER_READ)) {
            ALOGE("Reading a texture in U-Order is not supported");
            return false;
        }

        if ((mLayers[i]->getPlaneAlpha() != 255) && !cap.isFeatureSupported(HW2DCapability::FEATURE_PLANE_ALPHA)) {
            ALOGE("Plane alpha is not supported but given %u for plane alpha", mLayers[i]->getPlaneAlpha());
            return false;
        }

        rect = mLayers[i]->getTargetRect();
        if (area_is_zero(rect)) {
            // If no target area is specified to a source layer,
            // the entire region of the target image becomes the target area.
            // Then, check the scaling capability
            hw2d_rect_t ir = mLayers[i]->getImageRect();
            if (!!(mLayers[i]->getCompositAttr() & AcrylicLayer::ATTR_NORESAMPLING)) {
                if (!cap.supportedResizing(ir.size, xy, mLayers[i]->getTransform())) {
                    ALOGE("Unsupported resizing from %dx%d@(%d,%d) --> Target %dx%d with transform %d",
                          ir.size.hori, ir.size.vert, ir.pos.hori, ir.pos.vert,
                          xy.hori, xy.vert, mLayers[i]->getTransform());
                    return false;
                }
            } else {
                if (!cap.supportedResampling(ir.size, xy, mLayers[i]->getTransform())) {
                    ALOGE("Unsupported scaling from %dx%d@(%d,%d) --> Target %dx%d with transform %d",
                          ir.size.hori, ir.size.vert, ir.pos.hori, ir.pos.vert,
                          xy.hori, xy.vert, mLayers[i]->getTransform());
                    return false;
                }
            }
        } else if (rect > xy) {
            ALOGE("Target area %dx%d@(%d,%d) of layer %d is out of bound (%dx%d)",
                    rect.size.hori, rect.size.vert, rect.pos.hori, rect.pos.vert,
                    i, xy.hori, xy.vert);
            return false;
        }

        prot = prot || mLayers[i]->isProtected();
    }

    if (prot && !mCanvas.isProtected()) {
        ALOGE("Target image is not protected while a source layer is protected");
        return false;
    }

    return true;
}

class AcrylicLayerSorter {
    bool mAscending;
public:
    AcrylicLayerSorter(bool ascending): mAscending(ascending) { }
    inline bool operator()(AcrylicLayer *l1, AcrylicLayer *l2)
    {
        bool result = l1->getZOrder() < l2->getZOrder();
        return mAscending ? result : !result;
    }
};

void Acrylic::sortLayers(bool ascending)
{
    AcrylicLayerSorter sorter(ascending);
    std::sort(mLayers, mLayers + mLayerCount, sorter);
}
