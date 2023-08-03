#ifndef RENDER_DRM_WB_H
#define RENDER_DRM_WB_H

#include <wlr/render/drm_writeback.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/drm_format_set.h>
#include "render/pixel_format.h"

struct wlr_drm_wb_pixel_format {
	uint32_t drm_format;
};

struct wlr_drm_wb_buffer;

struct planes {
	uint32_t plane_id;
};

struct wlr_drm_wb_renderer {
	struct wlr_renderer wlr_renderer;
	int drm_fd;

	uint32_t wb_conn_id;
	uint32_t wb_encoder_id;
	uint32_t crtc_id;
	uint32_t crtc_mask;
	struct planes plane[MAX_PLANES];

//        dn->pid.writeback_fb_id     = props_name_to_id(props, "WRITEBACK_FB_ID");
//        dn->pid.writeback_out_fence_ptr = props_name_to_id(props, "WRITEBACK_OUT_FENCE_PTR");
//        dn->pid.writeback_pixel_formats = props_name_to_id(props, "WRITEBACK_PIXEL_FORMATS");  // Blob of fourccs (no modifier info)

	struct wl_list buffers; // wlr_drm_wb_buffer.link
	struct wl_list textures; // wlr_drm_wb_texture.link

	struct wlr_drm_wb_buffer *current_buffer;
	int32_t width, height;

	struct wlr_drm_format_set drm_formats;
	uint32_t viewport_width, viewport_height;
};

struct wlr_drm_wb_buffer {
	struct wlr_buffer *buffer;
	struct wlr_drm_wb_renderer *renderer;

	drm_wb_image_t *image;

	struct wl_listener buffer_destroy;
	struct wl_list link; // wlr_drm_wb_renderer.buffers
};

struct wlr_drm_wb_texture {
	struct wlr_texture wlr_texture;
	struct wlr_drm_wb_renderer *renderer;
	struct wl_list link; // wlr_drm_wb_renderer.textures

	drm_wb_image_t *image;
	const struct wlr_drm_wb_pixel_format *format;
	const struct wlr_pixel_format_info *format_info;

	void *data; // if created via texture_from_pixels
	struct wlr_buffer *buffer; // if created via texture_from_buffer
};

const struct wlr_drm_wb_pixel_format *get_drm_wb_format_from_drm(uint32_t fmt);
const uint32_t *get_drm_wb_drm_formats(size_t *len);

#endif
