#ifndef _DECON_DRM_HELPER_H
#define _DECON_DRM_HELPER_H

/* TODO(b/149514037): This header file should be removed */

struct drm_dpp_restriction {
	struct dpp_size_range src_f_w;
	struct dpp_size_range src_f_h;
	struct dpp_size_range src_w;
	struct dpp_size_range src_h;
	u32 src_x_align;
	u32 src_y_align;

	struct dpp_size_range dst_f_w;
	struct dpp_size_range dst_f_h;
	struct dpp_size_range dst_w;
	struct dpp_size_range dst_h;
	u32 dst_x_align;
	u32 dst_y_align;

	struct dpp_size_range blk_w;
	struct dpp_size_range blk_h;
	u32 blk_x_align;
	u32 blk_y_align;

	u32 src_h_rot_max; /* limit of source img height in case of rotation */

	u32 scale_down;
	u32 scale_up;
};

struct drm_dpp_ch_restriction {
	int id;
	unsigned long attr;
	struct drm_dpp_restriction restriction;
};

/* cgc_lut data */
#define CGC_LUT_REG_CNT (2457)
struct cgc_lut {
    uint32_t r_values[CGC_LUT_REG_CNT];
    uint32_t g_values[CGC_LUT_REG_CNT];
    uint32_t b_values[CGC_LUT_REG_CNT];
};

#define GAMMA_LUT_SIZE (65)

#define HDR_EOTF_LUT_LEN (129)
struct hdr_eotf_lut {
    uint16_t posx[HDR_EOTF_LUT_LEN];
    uint32_t posy[HDR_EOTF_LUT_LEN];
};

#define HDR_OETF_LUT_LEN (33)
struct hdr_oetf_lut {
    uint16_t posx[HDR_OETF_LUT_LEN];
    uint16_t posy[HDR_OETF_LUT_LEN];
};

#define HDR_GM_DIMENSIONS (3)
struct hdr_gm_data {
    uint32_t coeffs[HDR_GM_DIMENSIONS * HDR_GM_DIMENSIONS];
    uint32_t offsets[HDR_GM_DIMENSIONS];
};

#define HDR_TM_LUT_LEN (33)
struct hdr_tm_data {
    uint16_t coeff_r;
    uint16_t coeff_g;
    uint16_t coeff_b;
    uint16_t rng_x_min;
    uint16_t rng_x_max;
    uint16_t rng_y_min;
    uint16_t rng_y_max;
    uint16_t posx[HDR_TM_LUT_LEN];
    uint32_t posy[HDR_TM_LUT_LEN];
};

#endif
