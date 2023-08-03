#include <drm_fourcc.h>
#include <wlr/util/log.h>

#include "render/drm-writeback.h"

static const struct wlr_drm_wb_pixel_format formats[] = {
	{
		.drm_format = DRM_FORMAT_ARGB8888,
	},
	{
		.drm_format = DRM_FORMAT_XBGR8888,
	},
	{
		.drm_format = DRM_FORMAT_XRGB8888,
	},
	{
		.drm_format = DRM_FORMAT_ABGR8888,
	},
	{
		.drm_format = DRM_FORMAT_RGBA8888,
	},
	{
		.drm_format = DRM_FORMAT_RGBX8888,
	},
	{
		.drm_format = DRM_FORMAT_BGRA8888,
	},
	{
		.drm_format = DRM_FORMAT_BGRX8888,
	},
#if WLR_LITTLE_ENDIAN
	// Since DRM formats are always little-endian, they don't have an
	// equivalent on big-endian if their components are spanning across
	// multiple bytes.
	{
		.drm_format = DRM_FORMAT_RGB565,
	},
	{
		.drm_format = DRM_FORMAT_BGR565,
	},
	{
		.drm_format = DRM_FORMAT_ARGB2101010,
	},
	{
		.drm_format = DRM_FORMAT_XRGB2101010,
	},
	{
		.drm_format = DRM_FORMAT_ABGR2101010,
	},
	{
		.drm_format = DRM_FORMAT_XBGR2101010,
	},
#endif
};

const struct wlr_drm_wb_pixel_format *get_drm_wb_format_from_drm(uint32_t fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].drm_format == fmt) {
			return &formats[i];
		}
	}

	wlr_log(WLR_ERROR, "DRM format 0x%"PRIX32" has no pixman equivalent", fmt);
	return 0;
}
#if 0
uint32_t get_drm_format_from_pixman(pixman_format_code_t fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].pixman_format == fmt) {
			return formats[i].drm_format;
		}
	}

	wlr_log(WLR_ERROR, "pixman format 0x%"PRIX32" has no drm equivalent", fmt);
	return DRM_FORMAT_INVALID;
}
#endif
const uint32_t *get_drm_wb_drm_formats(size_t *len) {
	static uint32_t drm_formats[sizeof(formats) / sizeof(formats[0])];

	*len = sizeof(formats) / sizeof(formats[0]);
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		drm_formats[i] = formats[i].drm_format;
		wlr_log(WLR_ERROR, "adding format %08x in index %zu", drm_formats[i], i);
	}
	return drm_formats;
}
