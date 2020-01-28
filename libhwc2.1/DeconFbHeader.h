/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef _DECON_FB_HELPER_H
#define _DECON_FB_HELPER_H

struct decon_color_mode_info {
  int index;
  uint32_t color_mode;
};

struct decon_hdr_capabilities {
  unsigned int out_types[HDR_CAPABILITIES_NUM];
};
struct decon_hdr_capabilities_info {
  int out_num;
  int max_luminance;
  int max_average_luminance;
  int min_luminance;
};

#define S3CFB_SET_VSYNC_INT _IOW('F', 206, __u32)
#define S3CFB_DECON_SELF_REFRESH _IOW('F', 207, __u32)
#define S3CFB_WIN_CONFIG _IOW('F', 209, struct decon_win_config_data)
#define EXYNOS_DISP_INFO _IOW('F', 260, struct decon_disp_info)
#define S3CFB_FORCE_PANIC _IOW('F', 211, __u32)
#define S3CFB_WIN_POSITION _IOW('F', 222, struct decon_user_window)
#define S3CFB_POWER_MODE _IOW('F', 223, __u32)
#define EXYNOS_DISP_RESTRICTIONS _IOW('F', 261, struct dpp_restrictions_info)
#define S3CFB_START_CRC _IOW('F', 270, u32)
#define S3CFB_SEL_CRC_BITS _IOW('F', 271, u32)
#define S3CFB_GET_CRC_DATA _IOR('F', 272, u32)
#define EXYNOS_GET_DISPLAYPORT_CONFIG _IOW('F', 300, struct exynos_displayport_data)
#define EXYNOS_SET_DISPLAYPORT_CONFIG _IOW('F', 301, struct exynos_displayport_data)
#define EXYNOS_DPU_DUMP _IOW('F', 302, struct decon_win_config_data)
#define S3CFB_GET_HDR_CAPABILITIES _IOW('F', 400, struct decon_hdr_capabilities)
#define S3CFB_GET_HDR_CAPABILITIES_NUM _IOW('F', 401, struct decon_hdr_capabilities_info)
#define EXYNOS_GET_COLOR_MODE_NUM _IOW('F', 600, __u32)
#define EXYNOS_GET_COLOR_MODE _IOW('F', 601, struct decon_color_mode_info)
#define EXYNOS_SET_COLOR_MODE _IOW('F', 602, __u32)

#endif
