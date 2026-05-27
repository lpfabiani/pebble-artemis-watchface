/**
 * @file artemis_logo.c
 * @brief Artemis 'A' PDC vector logo rendering and in-place scaling.
 *
 * Loads the PDC resource at creation time, computes the actual point bounding
 * box of the draw commands, and scales all coordinates and radii in-place so
 * the logo fills the top zone while overflowing ~1/6 into the moon zone.
 * The layer is hidden by default; main.c shows it when the mission is complete
 * and no special event is active.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_logo.h"
#include "artemis.h"

#ifdef PBL_COLOR
  #define LOGO_RESOURCE RESOURCE_ID_IMAGE_ARTEMIS_LOGO_COLOR
#else
  #define LOGO_RESOURCE RESOURCE_ID_IMAGE_ARTEMIS_LOGO_BW_WHITE
#endif

static Layer             *s_logo_layer       = NULL;
static GDrawCommandImage *s_logo_pdc         = NULL;
static GSize              s_logo_draw_size   = {0, 0};
static GPoint             s_logo_draw_offset = {0, 0};

// ─── Logo (PDC vector) ────────────────────────────────────────────────────────
static void prv_scale_pdc_image(GDrawCommandImage *img, GSize target);

static void logo_update_proc(Layer *layer, GContext *ctx) {
  if (!s_logo_pdc) return;
  gdraw_command_image_draw(ctx, s_logo_pdc, s_logo_draw_offset);
}

// Builds (or rebuilds) the logo layer + scaled PDC for the current color
// theme. Creates the layer the first time it is called; subsequent calls
// release the old PDC, load the correct variant, rescale, and reposition
// the existing layer.
static void prv_setup_logo(void) {
  if (!s_root_layer) return;

  int top_zone_y, top_zone_h;
  overlay_geometry(s_root_w, s_root_h, &top_zone_y, &top_zone_h);

  // Release any previously loaded PDC — scaling is destructive in-place.
  if (s_logo_pdc) {
    gdraw_command_image_destroy(s_logo_pdc);
    s_logo_pdc = NULL;
  }

  s_logo_pdc = gdraw_command_image_create_with_resource(LOGO_RESOURCE);

  // Logo is ~1/6 taller than the top zone so the arc at the base of the A
  // spills into the bottom zone and sits naturally over the moon image.
  // Capped by screen width minus 8px side margins.
  int max_logo_side = top_zone_h + top_zone_h / 6;
  if (max_logo_side > s_root_w - 8) max_logo_side = s_root_w - 8;
  if (max_logo_side < 1) max_logo_side = 1;

  if (s_logo_pdc) {
    GSize pdc_original_size = gdraw_command_image_get_bounds_size(s_logo_pdc);
    APP_LOG(APP_LOG_LEVEL_INFO, "LOGO: pdc_original_size.w (%d), pdc_original_size.h (%d)", pdc_original_size.w, pdc_original_size.h);
    if (pdc_original_size.w > 0 && pdc_original_size.h > 0) {
      // Scale to fit max_logo_side on the longer axis, preserving aspect ratio.
      int logo_w = max_logo_side, logo_h = max_logo_side;
      if (pdc_original_size.w >= pdc_original_size.h) {
        // Wider than tall: constrain width, shrink height proportionally.
        logo_h = (max_logo_side * pdc_original_size.h) / pdc_original_size.w;
      } else {
        // Taller than wide: constrain height, shrink width proportionally.
        logo_w = (max_logo_side * pdc_original_size.w) / pdc_original_size.h;
      }
      s_logo_draw_size = GSize(logo_w, logo_h);
    } else {
      s_logo_draw_size = GSize(max_logo_side, max_logo_side);
    }
    prv_scale_pdc_image(s_logo_pdc, s_logo_draw_size);
  } else {
    s_logo_draw_size = GSize(max_logo_side, max_logo_side);
  }

  // Draw offset is always zero: the layer is positioned to match the logo exactly.
  s_logo_draw_offset = GPoint(0, 0);

  // Horizontally centered on screen; top-anchored with 2px gap so the
  // extra height spills downward into the moon zone, not upward.
  int logo_x = (s_root_w - s_logo_draw_size.w) / 2;
  int logo_y = top_zone_y + 2;
  GRect frame = GRect(logo_x, logo_y, s_logo_draw_size.w, s_logo_draw_size.h);

  if (!s_logo_layer) {
    s_logo_layer = layer_create(frame);
    layer_set_update_proc(s_logo_layer, logo_update_proc);
    layer_add_child(s_root_layer, s_logo_layer);
    layer_set_hidden(s_logo_layer, true);
  } else {
    layer_set_frame(s_logo_layer, frame);
    layer_mark_dirty(s_logo_layer);
  }
}

// Scale all points and radii in a PDC image in-place to fit target size.
// The PDC's stored bounds_size may not reflect the actual coordinate range of
// its points (some converters output points in SVG units while bounds is something
// else). Compute the actual point bounding box first, then scale + translate so
// the bbox maps into target.w × target.h, preserving aspect ratio and centering.
static void prv_scale_pdc_image(GDrawCommandImage *img, GSize target) {
  if (!img) return;

  GDrawCommandList *list = gdraw_command_image_get_command_list(img);
  if (!list) return;

  uint32_t num = gdraw_command_list_get_num_commands(list);

  int32_t min_x = INT32_MAX, min_y = INT32_MAX;
  int32_t max_x = INT32_MIN, max_y = INT32_MIN;
  bool any_points = false;

  for (uint32_t i = 0; i < num; i++) {
    GDrawCommand *cmd = gdraw_command_list_get_command(list, i);
    if (!cmd) continue;
    uint16_t np = gdraw_command_get_num_points(cmd);
    for (uint16_t j = 0; j < np; j++) {
      GPoint p = gdraw_command_get_point(cmd, j);
      if (p.x < min_x) min_x = p.x;
      if (p.y < min_y) min_y = p.y;
      if (p.x > max_x) max_x = p.x;
      if (p.y > max_y) max_y = p.y;
      any_points = true;
    }
  }

  if (!any_points) return;

  int32_t src_w = max_x - min_x;
  int32_t src_h = max_y - min_y;
  if (src_w <= 0) src_w = 1;
  if (src_h <= 0) src_h = 1;

  int32_t sx_x1000 = (int32_t)target.w * 1000 / src_w;
  int32_t sy_x1000 = (int32_t)target.h * 1000 / src_h;
  int32_t s_x1000 = (sx_x1000 < sy_x1000) ? sx_x1000 : sy_x1000;

  int32_t scaled_w = src_w * s_x1000 / 1000;
  int32_t scaled_h = src_h * s_x1000 / 1000;
  int32_t off_x = (target.w - scaled_w) / 2;
  int32_t off_y = (target.h - scaled_h) / 2;

  for (uint32_t i = 0; i < num; i++) {
    GDrawCommand *cmd = gdraw_command_list_get_command(list, i);
    if (!cmd) continue;
    uint16_t np = gdraw_command_get_num_points(cmd);
    for (uint16_t j = 0; j < np; j++) {
      GPoint p = gdraw_command_get_point(cmd, j);
      int32_t nx = ((int32_t)p.x - min_x) * s_x1000 / 1000 + off_x;
      int32_t ny = ((int32_t)p.y - min_y) * s_x1000 / 1000 + off_y;
      p.x = (int16_t)nx;
      p.y = (int16_t)ny;
      gdraw_command_set_point(cmd, j, p);
    }
    uint16_t r = gdraw_command_get_radius(cmd);
    if (r > 0) {
      gdraw_command_set_radius(cmd, (uint16_t)((uint32_t)r * s_x1000 / 1000));
    }
  }

  gdraw_command_image_set_bounds_size(img, target);
}

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_logo_create(Layer *root) {
  (void)root;  // prv_setup_logo uses s_root_layer from artemis.h
  prv_setup_logo();
}

void artemis_logo_destroy(void) {
  if (s_logo_layer) { layer_destroy(s_logo_layer);               s_logo_layer = NULL; }
  if (s_logo_pdc)   { gdraw_command_image_destroy(s_logo_pdc);   s_logo_pdc   = NULL; }
}

void artemis_logo_show(void) {
  if (s_logo_layer) layer_set_hidden(s_logo_layer, false);
}

void artemis_logo_hide(void) {
  if (s_logo_layer) layer_set_hidden(s_logo_layer, true);
}

void artemis_logo_refresh(void) {
  if (s_logo_layer) layer_mark_dirty(s_logo_layer);
}
