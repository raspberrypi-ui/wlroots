/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_PIXMAN_H
#define WLR_RENDER_PIXMAN_H

#include <pixman.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_renderer *wlr_pixman_renderer_create(void);
struct wlr_renderer *wlr_pixman_renderer_create_with_drm_fd(int drm_fd);

/**
 * Returns the image of current buffer.
 */
pixman_image_t *wlr_pixman_renderer_get_current_image(
	struct wlr_renderer *wlr_renderer);

struct wlr_pixman_texture_attribs
{
   int target;
   pixman_image_t *image;
   bool has_alpha;
};

bool wlr_renderer_is_pixman(struct wlr_renderer *wlr_renderer);
bool wlr_texture_is_pixman(struct wlr_texture *texture);
pixman_image_t *wlr_pixman_texture_get_image(struct wlr_texture *wlr_texture);
void wlr_pixman_texture_get_attribs(struct wlr_texture *texture, struct wlr_pixman_texture_attribs *attribs);
void wlr_pixman_texture_set_op_src_margins(struct wlr_texture *texture, int32_t left, int32_t top, int32_t right, int32_t bottom);
struct wlr_buffer *wlr_pixman_renderer_get_current_buffer(struct wlr_renderer *wlr_renderer);

#endif
