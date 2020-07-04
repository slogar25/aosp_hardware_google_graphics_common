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

#endif
