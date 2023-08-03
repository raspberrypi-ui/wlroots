#include <assert.h>
#include <drm_fourcc.h>
#include <pixman.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "render/drm-writeback.h"
#include "types/wlr_buffer.h"
#include "xf86drm.h"
#include "xf86drmMode.h"

static const struct wlr_renderer_impl renderer_impl;

bool wlr_renderer_is_drm_wb(struct wlr_renderer *wlr_renderer) {
	return wlr_renderer->impl == &renderer_impl;
}

static struct wlr_drm_wb_renderer *get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer_is_drm_wb(wlr_renderer));
	return (struct wlr_drm_wb_renderer *)wlr_renderer;
}

static struct wlr_drm_wb_buffer *get_buffer(
		struct wlr_drm_wb_renderer *renderer, struct wlr_buffer *wlr_buffer) {
	struct wlr_drm_wb_buffer *buffer;
	wl_list_for_each(buffer, &renderer->buffers, link) {
		if (buffer->buffer == wlr_buffer) {
			return buffer;
		}
	}
	return NULL;
}

static const struct wlr_texture_impl texture_impl;

bool wlr_texture_is_drm_wb(struct wlr_texture *texture) {
	return texture->impl == &texture_impl;
}

static struct wlr_drm_wb_texture *get_texture(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture_is_drm_wb(wlr_texture));
	return (struct wlr_drm_wb_texture *)wlr_texture;
}

static void texture_destroy(struct wlr_texture *wlr_texture) {
	struct wlr_drm_wb_texture *texture = get_texture(wlr_texture);
	wl_list_remove(&texture->link);
	//drm_wb_image_unref(texture->image);
	wlr_buffer_unlock(texture->buffer);
	free(texture->data);
	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.destroy = texture_destroy,
};

struct wlr_drm_wb_texture *drm_wb_create_texture(
		struct wlr_texture *wlr_texture, struct wlr_drm_wb_renderer *renderer);

static void destroy_buffer(struct wlr_drm_wb_buffer *buffer) {
	wl_list_remove(&buffer->link);
	wl_list_remove(&buffer->buffer_destroy.link);

//	drm_wb_image_unref(buffer->image);

	free(buffer);
}

static void handle_destroy_buffer(struct wl_listener *listener, void *data) {
	struct wlr_drm_wb_buffer *buffer =
		wl_container_of(listener, buffer, buffer_destroy);
	destroy_buffer(buffer);
}

static struct wlr_drm_wb_buffer *create_buffer(
		struct wlr_drm_wb_renderer *renderer, struct wlr_buffer *wlr_buffer) {
	struct wlr_drm_wb_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	buffer->buffer = wlr_buffer;
	buffer->renderer = renderer;

#if 0
	void *data = NULL;
	uint32_t drm_format;
	size_t stride;
	if (!wlr_buffer_begin_data_ptr_access(wlr_buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_READ | WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
			&data, &drm_format, &stride)) {
		wlr_log(WLR_ERROR, "Failed to get buffer data");
		goto error_buffer;
	}
	wlr_buffer_end_data_ptr_access(wlr_buffer);

	drm_wb_format_code_t format = get_drm_wb_format_from_drm(drm_format);
	if (format == 0) {
		wlr_log(WLR_ERROR, "Unsupported pixman drm format 0x%"PRIX32,
				drm_format);
		goto error_buffer;
	}

	buffer->image = drm_wb_image_create_bits(format, wlr_buffer->width,
			wlr_buffer->height, data, stride);
	if (!buffer->image) {
		wlr_log(WLR_ERROR, "Failed to allocate pixman image");
		goto error_buffer;
	}

#else
	struct wlr_dmabuf_attributes dmabuf = {0};
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &dmabuf)) {
		goto error_buffer;
	}

#endif

	buffer->buffer_destroy.notify = handle_destroy_buffer;
	wl_signal_add(&wlr_buffer->events.destroy, &buffer->buffer_destroy);

	wl_list_insert(&renderer->buffers, &buffer->link);

	wlr_log(WLR_DEBUG, "Created drm-wb buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;

error_buffer:
	free(buffer);
	return NULL;
}

static void drm_wb_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	renderer->width = width;
	renderer->height = height;

	wlr_log(WLR_ERROR, "%s: wxh %ux%u ", __func__, width, height);
	struct wlr_drm_wb_buffer *buffer = renderer->current_buffer;
	assert(buffer != NULL);

//	void *data = NULL;
//	uint32_t drm_format;
//	size_t stride;

}

static void drm_wb_end(struct wlr_renderer *wlr_renderer) {
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);

	assert(renderer->current_buffer != NULL);

	wlr_log(WLR_ERROR, "%s", __func__);

}

static void drm_wb_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
/*	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_drm_wb_buffer *buffer = renderer->current_buffer;

	const struct drm_wb_color colour = {
		.red = color[0] * 0xFFFF,
		.green = color[1] * 0xFFFF,
		.blue = color[2] * 0xFFFF,
		.alpha = color[3] * 0xFFFF,
	};

	drm_wb_image_t *fill = drm_wb_image_create_solid_fill(&colour);

	drm_wb_image_composite32(DRM_WB_OP_SRC, fill, NULL, buffer->image, 0, 0, 0,
			0, 0, 0, renderer->width, renderer->height);

	drm_wb_image_unref(fill);
*/
}

static void drm_wb_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
/*	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_drm_wb_buffer *buffer = renderer->current_buffer;
*/
	if (box != NULL) {
/*		struct drm_wb_region32 region = {0};
		drm_wb_region32_init_rect(&region, box->x, box->y, box->width,
				box->height);
		drm_wb_image_set_clip_region32(buffer->image, &region);
		drm_wb_region32_fini(&region);*/
	} else {
		//drm_wb_image_set_clip_region32(buffer->image, NULL);
	}
}

#if 0
static void matrix_to_drm_wb_transform(struct drm_wb_transform *transform,
		const float mat[static 9]) {
	struct drm_wb_f_transform ftr;
	ftr.m[0][0] = mat[0];
	ftr.m[0][1] = mat[1];
	ftr.m[0][2] = mat[2];
	ftr.m[1][0] = mat[3];
	ftr.m[1][1] = mat[4];
	ftr.m[1][2] = mat[5];
	ftr.m[2][0] = mat[6];
	ftr.m[2][1] = mat[7];
	ftr.m[2][2] = mat[8];

//	drm_wb_transform_from_drm_wb_f_transform(transform, &ftr);
}
#endif

static bool drm_wb_render_subtexture_with_matrix(
		struct wlr_renderer *wlr_renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *fbox, const float matrix[static 9],
		float alpha) {
/*	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_drm_wb_texture *texture = get_texture(wlr_texture);
	struct wlr_drm_wb_buffer *buffer = renderer->current_buffer;

	if (texture->buffer != NULL) {
		void *data;
		uint32_t drm_format;
		size_t stride;
		if (!wlr_buffer_begin_data_ptr_access(texture->buffer,
				WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &drm_format, &stride)) {
			return false;
		}

		// If the data pointer has changed, re-create the Pixman image. This can
		// happen if it's a client buffer and the wl_shm_pool has been resized.
		if (data != drm_wb_image_get_data(texture->image)) {
			drm_wb_format_code_t format = get_drm_wb_format_from_drm(drm_format);
			assert(format != 0);

			drm_wb_image_unref(texture->image);
			texture->image = drm_wb_image_create_bits_no_clear(format,
				texture->wlr_texture.width, texture->wlr_texture.height,
				data, stride);
		}
	}

	// TODO: don't create a mask if alpha == 1.0
	drm_wb_image_t *mask = NULL;
	if (alpha != 1.0) {
	        struct drm_wb_color mask_colour = {0};
		mask_colour.alpha = 0xFFFF * alpha;
		drm_wb_image_create_solid_fill(&mask_colour);
	}

	wlr_log(WLR_ERROR, "%s: renderer %ux%u fbox %fx%f",
		__func__, renderer->width, renderer->height,
		fbox->width, fbox->height
		);


	float m[9];
	memcpy(m, matrix, sizeof(m));
	wlr_matrix_scale(m, 1.0 / fbox->width, 1.0 / fbox->height);

	struct drm_wb_transform transform = {0};
	matrix_to_drm_wb_transform(&transform, m);
	drm_wb_transform_invert(&transform, &transform);

	drm_wb_image_set_transform(texture->image, &transform);

	// TODO clip properly with src_x and src_y
	drm_wb_image_composite32(DRM_WB_OP_OVER, texture->image, mask,
			buffer->image, 0, 0, 0, 0, 0, 0, renderer->width,
			renderer->height);

	if (texture->buffer != NULL) {
		wlr_buffer_end_data_ptr_access(texture->buffer);
	}


	if (mask)
	        drm_wb_image_unref(mask);
*/
	return true;
}

static void drm_wb_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
/*	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_drm_wb_buffer *buffer = renderer->current_buffer;

	struct drm_wb_color colour = {
		.red = color[0] * 0xFFFF,
		.green = color[1] * 0xFFFF,
		.blue = color[2] * 0xFFFF,
		.alpha = color[3] * 0xFFFF,
	};

	drm_wb_image_t *fill = drm_wb_image_create_solid_fill(&colour);

	float m[9];
	memcpy(m, matrix, sizeof(m));

	// TODO get the width/height from the caller instead of extracting them
	float width, height;
	if (matrix[1] == 0.0 && matrix[3] == 0.0) {
		width = fabs(matrix[0]);
		height = fabs(matrix[4]);
	} else {
		width = sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1]);
		height = sqrt(matrix[3] * matrix[3] + matrix[4] * matrix[4]);
	}

	wlr_matrix_scale(m, 1.0 / width, 1.0 / height);

	drm_wb_image_t *image = drm_wb_image_create_bits(DRM_WB_a8r8g8b8, width,
			height, NULL, 0);

	// TODO find a way to fill the image without allocating 2 images
	drm_wb_image_composite32(DRM_WB_OP_SRC, fill, NULL, image,
		0, 0, 0, 0, 0, 0, width, height);
	drm_wb_image_unref(fill);

	struct drm_wb_transform transform = {0};
	matrix_to_drm_wb_transform(&transform, m);
	drm_wb_transform_invert(&transform, &transform);

	drm_wb_image_set_transform(image, &transform);

	drm_wb_image_composite32(DRM_WB_OP_OVER, image, NULL, buffer->image,
			0, 0, 0, 0, 0, 0, renderer->width, renderer->height);

	drm_wb_image_unref(image);
	*/
}

static const uint32_t *drm_wb_get_shm_texture_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	return get_drm_wb_drm_formats(len);
}

static const struct wlr_drm_format_set *drm_wb_get_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	return &renderer->drm_formats;
}
#if 0
static struct wlr_drm_wb_texture *drm_wb_texture_create(
		struct wlr_drm_wb_renderer *renderer, uint32_t drm_format,
		uint32_t width, uint32_t height) {
	struct wlr_drm_wb_texture *texture =
		calloc(1, sizeof(struct wlr_drm_wb_texture));
	if (texture == NULL) {
		wlr_log_errno(WLR_ERROR, "Failed to allocate pixman texture");
		return NULL;
	}

	wlr_texture_init(&texture->wlr_texture, &texture_impl, width, height);
	texture->renderer = renderer;

	texture->format_info = drm_get_pixel_format_info(drm_format);
	if (!texture->format_info) {
		wlr_log(WLR_ERROR, "Unsupported drm format 0x%"PRIX32, drm_format);
		free(texture);
		return NULL;
	}

	texture->format = get_drm_wb_format_from_drm(drm_format);
	if (texture->format == NULL) {
		wlr_log(WLR_ERROR, "Unsupported pixman drm format 0x%"PRIX32,
				drm_format);
		free(texture);
		return NULL;
	}

	wl_list_insert(&renderer->textures, &texture->link);

	return texture;
}
#endif
static struct wlr_texture *drm_wb_texture_from_buffer(
		struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer) {
/*	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);

	void *data = NULL;
	uint32_t drm_format;
	size_t stride;
	if (!wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
			&data, &drm_format, &stride)) {
		return NULL;
	}
	wlr_buffer_end_data_ptr_access(buffer);

	struct wlr_drm_wb_texture *texture = drm_wb_texture_create(renderer,
		drm_format, buffer->width, buffer->height);
	if (texture == NULL) {
		return NULL;
	}

	texture->image = drm_wb_image_create_bits_no_clear(texture->format,
		buffer->width, buffer->height, data, stride);
	if (!texture->image) {
		wlr_log(WLR_ERROR, "Failed to create pixman image");
		wl_list_remove(&texture->link);
		free(texture);
		return NULL;
	}

	texture->buffer = wlr_buffer_lock(buffer);

	return &texture->wlr_texture;
	*/
	return NULL;
}

static bool drm_wb_bind_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);

	wlr_log(WLR_ERROR, "%s", __func__);

	if (renderer->current_buffer != NULL) {
		wlr_buffer_unlock(renderer->current_buffer->buffer);
		renderer->current_buffer = NULL;
	}

	if (wlr_buffer == NULL) {
		return true;
	}

	struct wlr_drm_wb_buffer *buffer = get_buffer(renderer, wlr_buffer);
	if (buffer == NULL) {
		buffer = create_buffer(renderer, wlr_buffer);
	}
	if (buffer == NULL) {
		wlr_log(WLR_ERROR, "%s create_buffer failed", __func__);
		return false;
	}

	wlr_buffer_lock(wlr_buffer);
	renderer->current_buffer = buffer;

	return true;
}

static void drm_wb_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);

	wlr_log(WLR_ERROR, "%s", __func__);
/*
	struct wlr_drm_wb_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
		destroy_buffer(buffer);
	}

	struct wlr_drm_wb_texture *tex, *tex_tmp;
	wl_list_for_each_safe(tex, tex_tmp, &renderer->textures, link) {
		wlr_texture_destroy(&tex->wlr_texture);
	}

	wlr_drm_format_set_finish(&renderer->drm_formats);
*/
	free(renderer);
}

static uint32_t drm_wb_preferred_read_format(
		struct wlr_renderer *wlr_renderer) {

	return DRM_FORMAT_XBGR8888;
}

static bool drm_wb_read_pixels(struct wlr_renderer *wlr_renderer,
		uint32_t drm_format, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	wlr_log(WLR_ERROR, "%s", __func__);
//	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
//	struct wlr_drm_wb_buffer *buffer = renderer->current_buffer;

//	drm_wb_image_t *dst = drm_wb_image_create_bits_no_clear(drm_format, width, height,
//			data, stride);

//	drm_wb_image_composite32(DRM_WB_OP_SRC, buffer->image, NULL, dst,
//			src_x, src_y, 0, 0, dst_x, dst_y, width, height);

//	drm_wb_image_unref(dst);

	return true;
}

static uint32_t drm_wb_get_render_buffer_caps(struct wlr_renderer *renderer) {
	return WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_DMABUF;
}

static struct wlr_buffer *
drm_wb_get_current_buffer(struct wlr_renderer *wlr_renderer)
{
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);

	wlr_log(WLR_ERROR, "%s", __func__);

	assert(renderer->current_buffer);
	return renderer->current_buffer->buffer;
}

static const struct wlr_renderer_impl renderer_impl = {
	.begin = drm_wb_begin,
	.end = drm_wb_end,
	.clear = drm_wb_clear,
	.scissor = drm_wb_scissor,
	.render_subtexture_with_matrix = drm_wb_render_subtexture_with_matrix,
	.render_quad_with_matrix = drm_wb_render_quad_with_matrix,
	.get_shm_texture_formats = drm_wb_get_shm_texture_formats,
	.get_render_formats = drm_wb_get_render_formats,
	.texture_from_buffer = drm_wb_texture_from_buffer,
	.bind_buffer = drm_wb_bind_buffer,
	.destroy = drm_wb_destroy,
	.preferred_read_format = drm_wb_preferred_read_format,
	.read_pixels = drm_wb_read_pixels,
	.get_render_buffer_caps = drm_wb_get_render_buffer_caps,
	.get_current_buffer = drm_wb_get_current_buffer,
};

struct crtc {
	drmModeCrtc *crtc;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
	drmModeModeInfo *mode;
};

struct encoder {
	drmModeEncoder *encoder;
};

struct connector {
	drmModeConnector *connector;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
	char name[32];
};

struct fb {
	drmModeFB *fb;
};

struct plane {
	drmModePlane *plane;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
};

struct resources {
	struct crtc *crtcs;
	int count_crtcs;
	struct encoder *encoders;
	int count_encoders;
	struct connector *connectors;
	int count_connectors;
	struct fb *fbs;
	int count_fbs;
	struct plane *planes;
	uint32_t count_planes;
};

static void free_resources(struct resources *res)
{
	int i;

	if (!res)
		return;

#define free_resource(_res, type, Type)					\
	do {									\
		if (!(_res)->type##s)						\
			break;							\
		for (i = 0; i < (int)(_res)->count_##type##s; ++i) {	\
			if (!(_res)->type##s[i].type)				\
				break;						\
			drmModeFree##Type((_res)->type##s[i].type);		\
		}								\
		free((_res)->type##s);						\
	} while (0)

#define free_properties(_res, type)					\
	do {									\
		for (i = 0; i < (int)(_res)->count_##type##s; ++i) {	\
			unsigned int j;										\
			for (j = 0; j < res->type##s[i].props->count_props; ++j)\
				drmModeFreeProperty(res->type##s[i].props_info[j]);\
			free(res->type##s[i].props_info);			\
			drmModeFreeObjectProperties(res->type##s[i].props);	\
		}								\
	} while (0)

	free_properties(res, plane);
	free_resource(res, plane, Plane);

	free_properties(res, connector);
	free_properties(res, crtc);

	for (i = 0; i < res->count_connectors; i++)
		free(res->connectors[i].name);

	//free_resource(res, fb, FB);
	free_resource(res, connector, Connector);
	//free_resource(res, encoder, Encoder);
	free_resource(res, crtc, Crtc);
}

static int get_resources(struct wlr_drm_wb_renderer *renderer, int drm_fd, struct resources *res)
{
	drmModeRes *_res;
	drmModePlaneRes *plane_res;
	int i;

	_res = drmModeGetResources(drm_fd);
	if (!_res) {
		wlr_log(WLR_INFO, "drmModeGetResources failed: %d",
			errno);
		return -1;
	}

	res->count_crtcs = _res->count_crtcs;
	res->count_encoders = _res->count_encoders;
	res->count_connectors = _res->count_connectors;
	res->count_fbs = _res->count_fbs;

	res->crtcs = calloc(res->count_crtcs, sizeof(*res->crtcs));
	res->encoders = calloc(res->count_encoders, sizeof(*res->encoders));
	res->connectors = calloc(res->count_connectors, sizeof(*res->connectors));
	//res->fbs = calloc(res->count_fbs, sizeof(*res->fbs));

	if (!res->crtcs || !res->encoders || !res->connectors /*|| !res->fbs*/) {
	    drmModeFreeResources(_res);
		goto error;
    }

#define get_resource(_res, __res, type, Type)					\
	do {									\
		for (i = 0; i < (int)(_res)->count_##type##s; ++i) {	\
			uint32_t type##id = (__res)->type##s[i];			\
			(_res)->type##s[i].type =							\
				drmModeGet##Type(drm_fd, type##id);			\
			if (!(_res)->type##s[i].type)						\
				wlr_log(WLR_INFO, "could not get %s %i: %s\n",	\
					#type, type##id,							\
					strerror(errno));			\
		}								\
	} while (0)

	get_resource(res, _res, connector, Connector);
	//get_resource(res, _res, fb, FB);

	drmModeFreeResources(_res);

	/* Set the name of all connectors based on the type name and the per-type ID. */
	for (i = 0; i < res->count_connectors; i++) {
		struct connector *connector = &res->connectors[i];
		drmModeConnector *conn = connector->connector;

		if (conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK) {
			renderer->wb_conn_id = conn->connector_id;
			if (conn->count_encoders >= 1) {
				renderer->wb_encoder_id = conn->encoders[0];
				wlr_log(WLR_INFO, "Use encoder %u", renderer->wb_encoder_id);
			}
			wlr_log(WLR_INFO, "FOUND WRITEBACK CONNECTOR");
		}
	}

	//get_resource(res, _res, encoder, Encoder);
	//for (i = 0; i < (int)(_res)->count_encoders; ++i) {
/*		i = 0;
		uint32_t encoder_id = (__res)->encoder_s[i];
		(_res)->encoder_s[i].type =
			drmModeGetEncoder(drm_fd, renderer->wb_encoder_id);
			//drmModeGetEncoder(drm_fd, encoder_id);
		if (!(_res)->encoder_s[i].type)
			wlr_log(WLR_INFO, "could not get %s %i: %s\n",
				#type, encoder_id,
				strerror(errno));
*/
	//}
	struct drm_mode_get_encoder enc = { 0 };
	enc.encoder_id = renderer->wb_encoder_id;

	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc))
	{
		wlr_log(WLR_INFO, "Failed to GET_ENCODER id %u", renderer->wb_encoder_id);
		return 0;
	}
	renderer->crtc_mask = enc.possible_crtcs;
	renderer->crtc_id = _res->crtcs[enc.possible_crtcs];

//	get_resource(res, _res, crtc, Crtc);
	struct drm_mode_crtc crtc = { 0 };

	crtc.crtc_id = renderer->crtc_id;

	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_GETCRTC, &crtc))
	{
		wlr_log(WLR_INFO, "Failed to GET_CRTC id %u", renderer->crtc_id);
		return 0;
	}



#define get_properties(_res, type, Type)					\
	do {									\
		for (i = 0; i < (int)(_res)->count_##type##s; ++i) {	\
			struct type *obj = &res->type##s[i];			\
			unsigned int j;						\
			obj->props =						\
				drmModeObjectGetProperties(drm_fd, obj->type->type##_id, \
							   DRM_MODE_OBJECT_##Type); \
			if (!obj->props) {					\
				wlr_log(WLR_INFO, 				\
					"could not get %s %i properties: %d\n", \
					#type, obj->type->type##_id,		\
					errno);					\
				continue;					\
			}							\
			obj->props_info = calloc(obj->props->count_props,	\
						 sizeof(*obj->props_info));	\
			if (!obj->props_info)					\
				continue;					\
			for (j = 0; j < obj->props->count_props; ++j)		\
				obj->props_info[j] =				\
					drmModeGetProperty(drm_fd, obj->props->props[j]); \
		}								\
	} while (0)

	get_properties(res, crtc, CRTC);
	get_properties(res, connector, CONNECTOR);

	for (i = 0; i < res->count_crtcs; ++i)
		res->crtcs[i].mode = &res->crtcs[i].crtc->mode;

	plane_res = drmModeGetPlaneResources(drm_fd);
	if (!plane_res) {
		wlr_log(WLR_INFO, "drmModeGetPlaneResources failed: %d",
			errno);
		return -1;
	}

	res->count_planes = plane_res->count_planes;

	res->planes = calloc(res->count_planes, sizeof(*res->planes));
	if (!res->planes) {
		drmModeFreePlaneResources(plane_res);
		goto error;
	}

	get_resource(res, plane_res, plane, Plane);
	drmModeFreePlaneResources(plane_res);
	get_properties(res, plane, PLANE);

	return 0;

error:
	free_resources(res);
	return -1;
}

struct wlr_renderer *wlr_drm_wb_renderer_create_with_drm_fd(int drm_fd) {
	struct wlr_drm_wb_renderer *renderer =
		calloc(1, sizeof(struct wlr_drm_wb_renderer));
	struct resources resources;
	int ret;

	if (renderer == NULL) {
		return NULL;
	}
	renderer->drm_fd = drm_fd;

	wlr_log(WLR_INFO, "Creating drm-wb renderer");
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	wl_list_init(&renderer->buffers);
	wl_list_init(&renderer->textures);

	ret = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (ret) {
		wlr_log(WLR_INFO, "failed to enable universal planes: %d", errno);
		return NULL;
	}

	ret = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		wlr_log(WLR_INFO, "no atomic modesetting support: %d", errno);
		return NULL;
	}

	ret = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_WRITEBACK_CONNECTORS, 1);
	if (ret) {
		wlr_log(WLR_INFO, "failed to enable writeback connectors: %d", errno);
		return NULL;
	}

	if (get_resources(renderer, drm_fd, &resources)) {
		wlr_log(WLR_INFO, "failed to get resources");
		// Free resources
		return NULL;		
	}

	/* FIXME: Should read all the formats from the planes that we support
	 * on the writeback connector, along with the modifiers.
	 */
 	size_t len = 0;
	const uint32_t *formats = get_drm_wb_drm_formats(&len);

	for (size_t i = 0; i < len; ++i) {
		wlr_drm_format_set_add(&renderer->drm_formats, formats[i],
			DRM_FORMAT_MOD_INVALID);
		wlr_drm_format_set_add(&renderer->drm_formats, formats[i],
			DRM_FORMAT_MOD_LINEAR);
	}

	return &renderer->wlr_renderer;
}

struct wlr_renderer *wlr_drm_wb_renderer_create(void) {
	struct wlr_renderer *renderer;
	int drm_fd = -1;

	/* Find me an FD */
	renderer = wlr_drm_wb_renderer_create_with_drm_fd(drm_fd);

	return renderer;
}

drm_wb_image_t *wlr_drm_wb_texture_get_image(struct wlr_texture *wlr_texture) {
	struct wlr_drm_wb_texture *texture = get_texture(wlr_texture);
	return texture->image;
}

drm_wb_image_t *wlr_drm_wb_renderer_get_current_image(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	assert(renderer->current_buffer);
	return renderer->current_buffer->image;
}

void
wlr_drm_wb_texture_get_attribs(struct wlr_texture *texture, struct wlr_drm_wb_texture_attribs *attribs)
{
   struct wlr_drm_wb_texture *ptex = get_texture(texture);
   memset(attribs, 0, sizeof(*attribs));
   attribs->target = 0x0DE1; //GL_TEXTURE_2D;
   attribs->image = ptex->image;
   attribs->has_alpha = ptex->format_info->has_alpha;
}

struct wlr_buffer *
wlr_drm_wb_renderer_get_current_buffer(struct wlr_renderer *wlr_renderer)
{
	struct wlr_drm_wb_renderer *renderer = get_renderer(wlr_renderer);
	assert(renderer->current_buffer);
	return renderer->current_buffer->buffer;
}
