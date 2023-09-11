#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "render/pixman.h"
#include "types/wlr_buffer.h"
#include "util/env.h"

static const struct wlr_renderer_impl renderer_impl;

bool wlr_renderer_is_pixman(struct wlr_renderer *wlr_renderer) {
	return wlr_renderer->impl == &renderer_impl;
}

static struct wlr_pixman_renderer *get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer_is_pixman(wlr_renderer));
	return (struct wlr_pixman_renderer *)wlr_renderer;
}

static struct wlr_pixman_buffer *get_buffer(
		struct wlr_pixman_renderer *renderer, struct wlr_buffer *wlr_buffer) {
	struct wlr_pixman_buffer *buffer;
	wl_list_for_each(buffer, &renderer->buffers, link) {
		if (buffer->buffer == wlr_buffer) {
			return buffer;
		}
	}
	return NULL;
}

static const struct wlr_texture_impl texture_impl;

bool wlr_texture_is_pixman(struct wlr_texture *texture) {
	return texture->impl == &texture_impl;
}

static struct wlr_pixman_texture *get_texture(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture_is_pixman(wlr_texture));
	return (struct wlr_pixman_texture *)wlr_texture;
}

static void texture_destroy(struct wlr_texture *wlr_texture) {
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);
	wl_list_remove(&texture->link);
	pixman_image_unref(texture->image);
	wlr_buffer_unlock(texture->buffer);
	free(texture->data);
	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.destroy = texture_destroy,
};

struct wlr_pixman_texture *pixman_create_texture(
		struct wlr_texture *wlr_texture, struct wlr_pixman_renderer *renderer);

static void destroy_buffer(struct wlr_pixman_buffer *buffer) {
	wl_list_remove(&buffer->link);
	wl_list_remove(&buffer->buffer_destroy.link);

	pixman_image_unref(buffer->image);

	free(buffer);
}

static void handle_destroy_buffer(struct wl_listener *listener, void *data) {
	struct wlr_pixman_buffer *buffer =
		wl_container_of(listener, buffer, buffer_destroy);
	destroy_buffer(buffer);
}

static struct wlr_pixman_buffer *create_buffer(
		struct wlr_pixman_renderer *renderer, struct wlr_buffer *wlr_buffer) {
	struct wlr_pixman_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	buffer->buffer = wlr_buffer;
	buffer->renderer = renderer;

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

	pixman_format_code_t format = get_pixman_format_from_drm(drm_format);
	if (format == 0) {
		wlr_log(WLR_ERROR, "Unsupported pixman drm format 0x%"PRIX32,
				drm_format);
		goto error_buffer;
	}

	buffer->image = pixman_image_create_bits(format, wlr_buffer->width,
			wlr_buffer->height, data, stride);
	if (!buffer->image) {
		wlr_log(WLR_ERROR, "Failed to allocate pixman image");
		goto error_buffer;
	}

	buffer->buffer_destroy.notify = handle_destroy_buffer;
	wl_signal_add(&wlr_buffer->events.destroy, &buffer->buffer_destroy);

	wl_list_insert(&renderer->buffers, &buffer->link);

	wlr_log(WLR_DEBUG, "Created pixman buffer %dx%d",
		wlr_buffer->width, wlr_buffer->height);

	return buffer;

error_buffer:
	free(buffer);
	return NULL;
}

static void pixman_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	renderer->width = width;
	renderer->height = height;

	struct wlr_pixman_buffer *buffer = renderer->current_buffer;
	assert(buffer != NULL);

	void *data = NULL;
	uint32_t drm_format;
	size_t stride;
	wlr_buffer_begin_data_ptr_access(buffer->buffer,
		WLR_BUFFER_DATA_PTR_ACCESS_READ | WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
		&data, &drm_format, &stride);

	// If the data pointer has changed, re-create the Pixman image. This can
	// happen if it's a client buffer and the wl_shm_pool has been resized.
	if (data != pixman_image_get_data(buffer->image)) {
		pixman_format_code_t format = get_pixman_format_from_drm(drm_format);
		assert(format != 0);

		pixman_image_unref(buffer->image);
		buffer->image = pixman_image_create_bits_no_clear(format,
			buffer->buffer->width, buffer->buffer->height, data, stride);
	}
}

static void pixman_end(struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);

	assert(renderer->current_buffer != NULL);

	wlr_buffer_end_data_ptr_access(renderer->current_buffer->buffer);
}

static void pixman_draw_overlay(struct wlr_renderer *wlr_renderer, pixman_op_t op)
{
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;
	static int run = -1;

	if (run == -1)
		run = env_parse_bool("WLR_PIXMAN_SHOW_COMPOSITE");

	if (!run)
		return;

	const struct pixman_color ovly_color = {
		.red = (op == PIXMAN_OP_SRC ? 0xFFFF : 0),
		.green = 0,
		.blue = (op == PIXMAN_OP_OVER ? 0xFFFF : 0),
		.alpha = 0.25 * 0xFFFF,
	};

	pixman_image_t *ovly = pixman_image_create_solid_fill(&ovly_color);
	pixman_image_composite32(PIXMAN_OP_OVER, ovly, NULL, buffer->image, 0, 0, 0,
			0, 0, 0, renderer->width, renderer->height);
	pixman_image_unref(ovly);
}

static void pixman_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	const struct pixman_color colour = {
		.red = color[0] * 0xFFFF,
		.green = color[1] * 0xFFFF,
		.blue = color[2] * 0xFFFF,
		.alpha = color[3] * 0xFFFF,
	};

	pixman_image_t *fill = pixman_image_create_solid_fill(&colour);

	pixman_image_composite32(PIXMAN_OP_SRC, fill, NULL, buffer->image, 0, 0, 0,
			0, 0, 0, renderer->width, renderer->height);

	pixman_image_unref(fill);

	pixman_draw_overlay(wlr_renderer, PIXMAN_OP_SRC);
}

static void pixman_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	if (box != NULL) {
		struct pixman_region32 region = {0};
		pixman_region32_init_rect(&region, box->x, box->y, box->width,
								  box->height);
		pixman_image_set_clip_region32(buffer->image, &region);
		pixman_region32_fini(&region);
	} else {
		pixman_image_set_clip_region32(buffer->image, NULL);
	}
}

static void matrix_to_pixman_transform(struct pixman_transform *transform,
		const float mat[static 9]) {
	struct pixman_f_transform ftr;
	ftr.m[0][0] = mat[0];
	ftr.m[0][1] = mat[1];
	ftr.m[0][2] = mat[2];
	ftr.m[1][0] = mat[3];
	ftr.m[1][1] = mat[4];
	ftr.m[1][2] = mat[5];
	ftr.m[2][0] = mat[6];
	ftr.m[2][1] = mat[7];
	ftr.m[2][2] = mat[8];

	pixman_transform_from_pixman_f_transform(transform, &ftr);
}

static bool pixman_render_subtexture_with_matrix(
		struct wlr_renderer *wlr_renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *fbox, const float matrix[static 9],
		float alpha) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

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
		if (data != pixman_image_get_data(texture->image)) {
			pixman_format_code_t format = get_pixman_format_from_drm(drm_format);
			assert(format != 0);

			pixman_image_unref(texture->image);
			texture->image = pixman_image_create_bits_no_clear(format,
				texture->wlr_texture.width, texture->wlr_texture.height,
				data, stride);
		}
	}

	pixman_image_t *mask = NULL;
	if (alpha != 1.0) {
	        struct pixman_color mask_colour = {0};
		mask_colour.alpha = 0xFFFF * alpha;
		mask = pixman_image_create_solid_fill(&mask_colour);
	}

	float m[9];
	int32_t dest_x = 0;
	int32_t dest_y = 0;
	memcpy(m, matrix, sizeof(m));
	wlr_matrix_scale(m, 1.0 / fbox->width, 1.0 / fbox->height);

	bool has_rotation = false;
	if (m[1] != 0 ||
		m[3] != 0 ||
		m[6] != 0 ||
		m[7] != 0)
		has_rotation = true;

	bool has_scaling = false;
	if (m[0] != 1 ||
		m[4] != 1 ||
		m[8] != 1)
		has_scaling = true;

	bool has_translation = false;
	if (m[2] != 0 ||
		m[5] != 0)
		has_translation = true;

	/* If we are only doing a translation, do it through dest coordinates
	   instead of transformation matrix */
	if (!has_rotation &&
		!has_scaling &&
		has_translation) {
		dest_x = (int32_t) m[2];
		dest_y = (int32_t) m[5];
		m[2] = 0;
		m[5] = 0;
		pixman_image_set_transform(texture->image, NULL);
		has_translation = false;
	} else {
		struct pixman_transform transform = {0};
		matrix_to_pixman_transform(&transform, m);
		pixman_transform_invert(&transform, &transform);
		pixman_image_set_transform(texture->image, &transform);
	}

	static bool force_op_src_init = false;
	static bool disable_op_src_opt_init = false;
	static bool force_op_src, disable_op_src_opt;


	if (!force_op_src_init) {
		force_op_src = env_parse_bool("WLR_PIXMAN_FORCE_SRC");
		force_op_src_init = true;
	}

	if (!disable_op_src_opt_init) {
		disable_op_src_opt = force_op_src || env_parse_bool("WLR_PIXMAN_DISABLE_SRC_OPT");
		disable_op_src_opt_init = true;
	}

	pixman_op_t op = PIXMAN_OP_OVER;

	if (!disable_op_src_opt && !mask &&
		!has_rotation && !has_scaling &&
		renderer->width == fbox->width &&
		renderer->height == fbox->height)
		op = PIXMAN_OP_SRC;

	if (force_op_src)
		op = PIXMAN_OP_SRC;

	if (!disable_op_src_opt && !mask &&
		!has_rotation &&
		(texture->op_src_margins.x1 != 0 ||
		 texture->op_src_margins.x2 != 0 ||
		 texture->op_src_margins.y1 != 0 ||
		 texture->op_src_margins.y2 != 0)) {
		pixman_image_set_has_client_clip(texture->image, true);
		pixman_image_set_source_clipping(texture->image, true);

		assert(fbox->x == 0);
		assert(fbox->y == 0);

		/* Source clipping is applied after the transformation; so we need to
		   apply the transformation to the clip coordinates */
		pixman_region32_t src_region = { 0 };
		pixman_region32_init_rect(&src_region,
								  round(texture->op_src_margins.x1 * m[0]) + m[2],
								  round(texture->op_src_margins.y1 * m[4]) + m[5],
								  round(fbox->width - texture->op_src_margins.x1 - texture->op_src_margins.x2) * m[0],
								  round(fbox->height - texture->op_src_margins.y1 - texture->op_src_margins.y2) * m[4]);
		pixman_image_set_clip_region32(texture->image, &src_region);

		pixman_image_composite32(PIXMAN_OP_SRC, texture->image, mask, buffer->image,
								 0, 0, 0, 0, dest_x, dest_y,
								 renderer->width, renderer->height);

		pixman_region32_t full_region = { 0 };
		pixman_region32_init_rect(&full_region,
								  m[2],
								  m[5],
								  round(fbox->width * m[0]),
								  round(fbox->height * m[4]));

		pixman_region32_t over_region = { 0 };
		pixman_region32_subtract(&over_region, &full_region, &src_region);
		if (pixman_region32_not_empty(&over_region)) {
			pixman_image_set_clip_region32(texture->image, &over_region);
			pixman_image_composite32(PIXMAN_OP_OVER, texture->image, mask, buffer->image,
									 0, 0, 0, 0, dest_x, dest_y,
									 renderer->width, renderer->height);
		}

		pixman_image_set_clip_region32(texture->image, NULL);

		pixman_region32_fini(&full_region);
		pixman_region32_fini(&src_region);
		pixman_region32_fini(&over_region);
	} else {
		// TODO clip properly with src_x and src_y
		pixman_image_composite32(op, texture->image, mask,
								 buffer->image, 0, 0, 0, 0, dest_x, dest_y,
								 renderer->width, renderer->height);
	}

	if (texture->buffer != NULL) {
		wlr_buffer_end_data_ptr_access(texture->buffer);
	}

	if (fbox->width != renderer->width ||
		fbox->height != renderer->height)
		pixman_draw_overlay(wlr_renderer, op);

	if (mask)
	        pixman_image_unref(mask);

	return true;
}

static void pixman_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	struct pixman_color colour = {
		.red = color[0] * 0xFFFF,
		.green = color[1] * 0xFFFF,
		.blue = color[2] * 0xFFFF,
		.alpha = color[3] * 0xFFFF,
	};

	pixman_image_t *fill = pixman_image_create_solid_fill(&colour);

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

        pixman_image_t *image =
                pixman_image_create_bits(PIXMAN_a8r8g8b8, width,
                                         height, NULL, 0);
        if (!image)
        {
           wlr_log(WLR_ERROR, "Cannot create pixman image in render_quad: %f %f",
                   width, height);
           return;
        }

	// TODO find a way to fill the image without allocating 2 images
	pixman_image_composite32(PIXMAN_OP_SRC, fill, NULL, image,
		0, 0, 0, 0, 0, 0, width, height);
	pixman_image_unref(fill);

	struct pixman_transform transform = {0};
	matrix_to_pixman_transform(&transform, m);
	pixman_transform_invert(&transform, &transform);

	pixman_image_set_transform(image, &transform);

	static pixman_op_t op = PIXMAN_OP_SCREEN;
	if (op == PIXMAN_OP_SCREEN)
		op = env_parse_bool("WLR_PIXMAN_FORCE_SRC") ? PIXMAN_OP_SRC : PIXMAN_OP_OVER;

	pixman_image_composite32(op, image, NULL, buffer->image,
			0, 0, 0, 0, 0, 0, renderer->width, renderer->height);

	pixman_image_unref(image);

	pixman_draw_overlay(wlr_renderer, op);
}

static const uint32_t *pixman_get_shm_texture_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	return get_pixman_drm_formats(len);
}

static const struct wlr_drm_format_set *pixman_get_render_formats(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	return &renderer->drm_formats;
}

static const struct wlr_drm_format_set *pixman_get_dmabuf_texture_formats(struct wlr_renderer *wlr_renderer)
{
   struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
   return &renderer->dmabuf_texture_formats;
}

static int pixman_get_drm_fd(struct wlr_renderer *wlr_renderer)
{
   struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
   if (renderer->drm_fd < 0)
     {
        wlr_log(WLR_DEBUG, "Pixman Drm Fd Not Set: %d", renderer->drm_fd);
        return -1;
     }
   return renderer->drm_fd;
}

static struct wlr_pixman_texture *pixman_texture_create(
		struct wlr_pixman_renderer *renderer, uint32_t drm_format,
		uint32_t width, uint32_t height) {
	struct wlr_pixman_texture *texture =
		calloc(1, sizeof(struct wlr_pixman_texture));
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

	texture->format = get_pixman_format_from_drm(drm_format);
	if (texture->format == 0) {
		wlr_log(WLR_ERROR, "Unsupported pixman drm format 0x%"PRIX32,
				drm_format);
		free(texture);
		return NULL;
	}

	wl_list_insert(&renderer->textures, &texture->link);

	return texture;
}

static struct wlr_texture *pixman_texture_from_dmabuf_buffer(struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer, struct wlr_dmabuf_attributes *attribs)
{
   void *data = NULL;
   uint32_t format;
   size_t stride;
   struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);

   struct wlr_pixman_texture *texture =
     pixman_texture_create(renderer, attribs->format,
                           buffer->width, buffer->height);
   if (!texture) return NULL;

   if (!wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                        &data, &format, &stride))
     {
        wlr_log(WLR_DEBUG, "\tCannot get buffer data");
        goto err;
     }

   texture->image =
     pixman_image_create_bits_no_clear(texture->format,
                                       buffer->width, buffer->height,
                                       data, stride);
   if (!texture->image) {
      wlr_log(WLR_ERROR, "\tFailed to create pixman image");
      wlr_buffer_end_data_ptr_access(buffer);
      goto err;
   }

   wlr_buffer_end_data_ptr_access(buffer);

   texture->buffer = wlr_buffer_lock(buffer);

   return &texture->wlr_texture;

err:
   wl_list_remove(&texture->link);
   free(texture);
   return NULL;
}

static struct wlr_texture *pixman_texture_from_pixels(struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer, uint32_t drm_format, uint32_t stride, uint32_t width, uint32_t height, void *data)
{
   struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);

   struct wlr_pixman_texture *texture =
     pixman_texture_create(renderer, drm_format, width, height);
   if (texture == NULL)
     {
        wlr_log(WLR_ERROR, "\tPixman texture create failed");
        return NULL;
     }

   texture->image =
     pixman_image_create_bits_no_clear(texture->format,
                                       width, height, data, stride);
   if (!texture->image)
     {
        wlr_log(WLR_ERROR, "\tFailed to create pixman image");
        wl_list_remove(&texture->link);
        free(texture);
        return NULL;
     }

   texture->buffer = wlr_buffer_lock(buffer);

   return &texture->wlr_texture;
}

static struct wlr_texture *pixman_texture_from_buffer(
		struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer) {
   void *data = NULL;
   uint32_t drm_format;
   size_t stride;
   struct wlr_dmabuf_attributes dmabuf;

   if (wlr_buffer_get_dmabuf(buffer, &dmabuf))
     return pixman_texture_from_dmabuf_buffer(wlr_renderer, buffer, &dmabuf);
   else if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
			&data, &drm_format, &stride))
     {
        struct wlr_texture *texture =
          pixman_texture_from_pixels(wlr_renderer, buffer, drm_format, stride,
                                     buffer->width, buffer->height, data);

        wlr_buffer_end_data_ptr_access(buffer);
        return texture;
     }
   else
     return NULL;
}

static bool pixman_bind_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);

	if (renderer->current_buffer != NULL) {
		wlr_buffer_unlock(renderer->current_buffer->buffer);
		renderer->current_buffer = NULL;
	}

	if (wlr_buffer == NULL) {
		return true;
	}

	struct wlr_pixman_buffer *buffer = get_buffer(renderer, wlr_buffer);
	if (buffer == NULL) {
		buffer = create_buffer(renderer, wlr_buffer);
	}
	if (buffer == NULL) {
		return false;
	}

	wlr_buffer_lock(wlr_buffer);
	renderer->current_buffer = buffer;

	return true;
}

static void pixman_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);

	struct wlr_pixman_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
		destroy_buffer(buffer);
	}

	struct wlr_pixman_texture *tex, *tex_tmp;
	wl_list_for_each_safe(tex, tex_tmp, &renderer->textures, link) {
		wlr_texture_destroy(&tex->wlr_texture);
	}

	wlr_drm_format_set_finish(&renderer->drm_formats);
	wlr_drm_format_set_finish(&renderer->dmabuf_texture_formats);

	free(renderer);
}

static uint32_t pixman_preferred_read_format(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	pixman_format_code_t pixman_format = pixman_image_get_format(
			buffer->image);

	return get_drm_format_from_pixman(pixman_format);
}

static bool pixman_read_pixels(struct wlr_renderer *wlr_renderer,
		uint32_t drm_format, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	struct wlr_pixman_buffer *buffer = renderer->current_buffer;

	pixman_format_code_t fmt = get_pixman_format_from_drm(drm_format);
	if (fmt == 0) {
		wlr_log(WLR_ERROR, "Cannot read pixels: unsupported pixel format");
		return false;
	}

	const struct wlr_pixel_format_info *drm_fmt =
		drm_get_pixel_format_info(drm_format);
	assert(drm_fmt);

	pixman_image_t *dst = pixman_image_create_bits_no_clear(fmt, width, height,
			data, stride);

	pixman_image_composite32(PIXMAN_OP_SRC, buffer->image, NULL, dst,
			src_x, src_y, 0, 0, dst_x, dst_y, width, height);

	pixman_image_unref(dst);

	return true;
}

static uint32_t pixman_get_render_buffer_caps(struct wlr_renderer *renderer) {
	return WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_DMABUF;
}

static struct wlr_buffer *
pixman_get_current_buffer(struct wlr_renderer *wlr_renderer)
{
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	assert(renderer->current_buffer);
	return renderer->current_buffer->buffer;
}

static const struct wlr_renderer_impl renderer_impl = {
	.begin = pixman_begin,
	.end = pixman_end,
	.clear = pixman_clear,
	.scissor = pixman_scissor,
	.render_subtexture_with_matrix = pixman_render_subtexture_with_matrix,
	.render_quad_with_matrix = pixman_render_quad_with_matrix,
	.get_shm_texture_formats = pixman_get_shm_texture_formats,
	.get_render_formats = pixman_get_render_formats,
	.texture_from_buffer = pixman_texture_from_buffer,
	.bind_buffer = pixman_bind_buffer,
	.destroy = pixman_destroy,
	.preferred_read_format = pixman_preferred_read_format,
	.read_pixels = pixman_read_pixels,
	.get_render_buffer_caps = pixman_get_render_buffer_caps,
        .get_current_buffer = pixman_get_current_buffer,
        .get_dmabuf_texture_formats = pixman_get_dmabuf_texture_formats,
        .get_drm_fd = pixman_get_drm_fd,
};

struct wlr_renderer *wlr_pixman_renderer_create(void) {
	struct wlr_pixman_renderer *renderer =
		calloc(1, sizeof(struct wlr_pixman_renderer));
	if (renderer == NULL) {
		return NULL;
	}

	wlr_log(WLR_INFO, "Creating pixman renderer");
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	wl_list_init(&renderer->buffers);
	wl_list_init(&renderer->textures);

        renderer->drm_fd = -1;

   size_t len = 0;
   const uint32_t *formats = get_pixman_drm_formats(&len);
   for (size_t i = 0; i < len; ++i) {
      wlr_drm_format_set_add(&renderer->drm_formats, formats[i],
                             DRM_FORMAT_MOD_LINEAR);
      wlr_drm_format_set_add(&renderer->dmabuf_texture_formats, formats[i],
                             DRM_FORMAT_MOD_LINEAR);
   }

   return &renderer->wlr_renderer;
}

struct wlr_renderer *wlr_pixman_renderer_create_with_drm_fd(int drm_fd)
{
   struct wlr_pixman_renderer *renderer =
     calloc(1, sizeof(struct wlr_pixman_renderer));
   if (renderer == NULL) {
      return NULL;
   }

   wlr_log(WLR_INFO, "Creating pixman renderer with drm fd: %d", drm_fd);
   wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
   wl_list_init(&renderer->buffers);
   wl_list_init(&renderer->textures);

   renderer->drm_fd = drm_fd;

   size_t len = 0;
   const uint32_t *formats = get_pixman_drm_formats(&len);
   for (size_t i = 0; i < len; ++i) {
      wlr_drm_format_set_add(&renderer->drm_formats, formats[i],
                             DRM_FORMAT_MOD_LINEAR);
      wlr_drm_format_set_add(&renderer->dmabuf_texture_formats, formats[i],
                             DRM_FORMAT_MOD_LINEAR);
   }

   return &renderer->wlr_renderer;
}

pixman_image_t *wlr_pixman_texture_get_image(struct wlr_texture *wlr_texture) {
	struct wlr_pixman_texture *texture = get_texture(wlr_texture);
	return texture->image;
}

pixman_image_t *wlr_pixman_renderer_get_current_image(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	assert(renderer->current_buffer);
	return renderer->current_buffer->image;
}

void
wlr_pixman_texture_get_attribs(struct wlr_texture *texture, struct wlr_pixman_texture_attribs *attribs)
{
   struct wlr_pixman_texture *ptex = get_texture(texture);
   memset(attribs, 0, sizeof(*attribs));
   attribs->target = 0x0DE1; //GL_TEXTURE_2D;
   attribs->image = ptex->image;
   attribs->has_alpha = ptex->format_info->has_alpha;
}

void
wlr_pixman_texture_set_op_src_margins(struct wlr_texture *texture,
                                      int32_t left, int32_t top,
                                      int32_t right, int32_t bottom)
{
	struct wlr_pixman_texture *ptex = get_texture(texture);
	ptex->op_src_margins.x1 = left;
	ptex->op_src_margins.y1 = top;
	ptex->op_src_margins.x2 = right;
	ptex->op_src_margins.y2 = bottom;
}

struct wlr_buffer *
wlr_pixman_renderer_get_current_buffer(struct wlr_renderer *wlr_renderer)
{
	struct wlr_pixman_renderer *renderer = get_renderer(wlr_renderer);
	assert(renderer->current_buffer);
	return renderer->current_buffer->buffer;
}
