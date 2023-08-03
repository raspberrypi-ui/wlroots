/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_DRM_WB_H
#define WLR_RENDER_DRM_WB_H

//#include <drm_wb.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

typedef struct drm_wb_image_t {
   int foo;
} drm_wb_image_t;

struct wlr_renderer *wlr_drm_wb_renderer_create_with_drm_fd(int drm_fd);

struct wlr_renderer *wlr_drm_wb_renderer_create(void);
/**
 * Returns the image of current buffer.
 */
drm_wb_image_t *wlr_drm_wb_renderer_get_current_image(
	struct wlr_renderer *wlr_renderer);

struct wlr_drm_wb_texture_attribs
{
   int target;
   drm_wb_image_t *image;
   bool has_alpha;
};

bool wlr_renderer_is_drm_wb(struct wlr_renderer *wlr_renderer);
bool wlr_texture_is_drm_wb(struct wlr_texture *texture);
drm_wb_image_t *wlr_drm_wb_texture_get_image(struct wlr_texture *wlr_texture);
void wlr_drm_wb_texture_get_attribs(struct wlr_texture *texture, struct wlr_drm_wb_texture_attribs *attribs);
struct wlr_buffer *wlr_drm_wb_renderer_get_current_buffer(struct wlr_renderer *wlr_renderer);

#endif
