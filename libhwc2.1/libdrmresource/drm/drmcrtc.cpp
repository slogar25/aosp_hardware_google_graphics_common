/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "hwc-drm-crtc"

#include "drmcrtc.h"
#include "drmdevice.h"

#include <stdint.h>
#include <xf86drmMode.h>

#include <log/log.h>

namespace android {

DrmCrtc::DrmCrtc(DrmDevice *drm, drmModeCrtcPtr c, unsigned pipe)
    : drm_(drm), id_(c->crtc_id), pipe_(pipe), display_(-1), mode_(&c->mode) {
}

int DrmCrtc::Init() {
  int ret = drm_->GetCrtcProperty(*this, "ACTIVE", &active_property_);
  if (ret) {
    ALOGE("Failed to get ACTIVE property");
    return ret;
  }

  ret = drm_->GetCrtcProperty(*this, "MODE_ID", &mode_property_);
  if (ret) {
    ALOGE("Failed to get MODE_ID property");
    return ret;
  }

  ret = drm_->GetCrtcProperty(*this, "OUT_FENCE_PTR", &out_fence_ptr_property_);
  if (ret) {
    ALOGE("Failed to get OUT_FENCE_PTR property");
    return ret;
  }

  if (drm_->GetCrtcProperty(*this, "partial_region", &partial_region_property_))
    ALOGI("Failed to get &partial_region_property");

  if (drm_->GetCrtcProperty(*this, "cgc_lut", &cgc_lut_property_))
    ALOGI("Failed to get cgc_lut property");
  if (drm_->GetCrtcProperty(*this, "DEGAMMA_LUT", &degamma_lut_property_))
    ALOGI("Failed to get &degamma_lut property");
  if (drm_->GetCrtcProperty(*this, "DEGAMMA_LUT_SIZE", &degamma_lut_size_property_))
    ALOGI("Failed to get &degamma_lut_size property");
  if (drm_->GetCrtcProperty(*this, "GAMMA_LUT", &gamma_lut_property_))
    ALOGI("Failed to get &gamma_lut property");
  if (drm_->GetCrtcProperty(*this, "GAMMA_LUT_SIZE", &gamma_lut_size_property_))
    ALOGI("Failed to get &gamma_lut_size property");
  if (drm_->GetCrtcProperty(*this, "linear_matrix", &linear_matrix_property_))
    ALOGI("Failed to get &linear_matrix property");
  if (drm_->GetCrtcProperty(*this, "gamma_matrix", &gamma_matrix_property_))
    ALOGI("Failed to get &gamma_matrix property");
  if (drm_->GetCrtcProperty(*this, "force_bpc", &force_bpc_property_))
    ALOGI("Failed to get &force_bpc_property_");
  if (drm_->GetCrtcProperty(*this, "disp_dither", &disp_dither_property_))
    ALOGI("Failed to get &disp_dither property");
  if (drm_->GetCrtcProperty(*this, "cgc_dither", &cgc_dither_property_))
    ALOGI("Failed to get &cgc_dither property");

  properties_.push_back(&active_property_);
  properties_.push_back(&mode_property_);
  properties_.push_back(&out_fence_ptr_property_);
  properties_.push_back(&cgc_lut_property_);
  properties_.push_back(&degamma_lut_property_);
  properties_.push_back(&degamma_lut_size_property_);
  properties_.push_back(&gamma_lut_property_);
  properties_.push_back(&gamma_lut_size_property_);
  properties_.push_back(&linear_matrix_property_);
  properties_.push_back(&gamma_matrix_property_);
  properties_.push_back(&partial_region_property_);
  properties_.push_back(&force_bpc_property_);
  properties_.push_back(&disp_dither_property_);
  properties_.push_back(&cgc_dither_property_);

  return 0;
}

uint32_t DrmCrtc::id() const {
  return id_;
}

unsigned DrmCrtc::pipe() const {
  return pipe_;
}

int DrmCrtc::display() const {
  return display_;
}

void DrmCrtc::set_display(int display) {
  display_ = display;
}

bool DrmCrtc::can_bind(int display) const {
  return display_ == -1 || display_ == display;
}

const DrmProperty &DrmCrtc::active_property() const {
  return active_property_;
}

const DrmProperty &DrmCrtc::mode_property() const {
  return mode_property_;
}

const DrmProperty &DrmCrtc::out_fence_ptr_property() const {
  return out_fence_ptr_property_;
}

const DrmProperty &DrmCrtc::cgc_lut_property() const {
    return cgc_lut_property_;
}

const DrmProperty &DrmCrtc::degamma_lut_property() const {
    return degamma_lut_property_;
}

const DrmProperty &DrmCrtc::degamma_lut_size_property() const {
    return degamma_lut_size_property_;
}

const DrmProperty &DrmCrtc::gamma_lut_property() const {
    return gamma_lut_property_;
}

const DrmProperty &DrmCrtc::gamma_lut_size_property() const {
    return gamma_lut_size_property_;
}

const DrmProperty &DrmCrtc::linear_matrix_property() const {
    return linear_matrix_property_;
}

const DrmProperty &DrmCrtc::gamma_matrix_property() const {
    return gamma_matrix_property_;
}

const DrmProperty &DrmCrtc::partial_region_property() const {
    return partial_region_property_;
}

const DrmProperty &DrmCrtc::force_bpc_property() const {
    return force_bpc_property_;
}

const DrmProperty &DrmCrtc::disp_dither_property() const {
    return disp_dither_property_;
}

const DrmProperty &DrmCrtc::cgc_dither_property() const {
    return cgc_dither_property_;
}

}  // namespace android
