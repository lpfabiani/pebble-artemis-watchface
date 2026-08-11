/**
 * @file artemis_clock.c
 * @brief Always-visible background scene: sky stars, moon bitmap, time, and date.
 *
 * Draws the solid black sky with a scaled Orion constellation star field.
 * The moon bitmap, satellite indicator, time, and date all live inside
 * @c s_time_area_layer — a viewport normally covering the bottom zone
 * (s_split_y → s_root_h). Timeline Peek slides this layer itself (full
 * height, never resized) up so its bottom edge tracks the unobstructed
 * boundary, moving all its children as one rigid unit. DISPLAY_INFO hides
 * the whole zone with a single @c layer_set_hidden call, rather than
 * coordinating each child independently.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_clock.h"
#include "artemis.h"

static Layer       *s_sky_layer         = NULL;
static Layer       *s_time_area_layer   = NULL;   // moon + satellite + time + date; moved during Timeline Peek
static int          s_bottom_h          = 0;      // fixed content height (full, uncompressed)
static BitmapLayer *s_moon_bitmap_layer = NULL;
static GBitmap     *s_moon_bitmap       = NULL;
static TextLayer   *s_time_layer        = NULL;
static TextLayer   *s_date_layer        = NULL;
static Layer       *s_satellite_layer   = NULL;
static bool         s_bt_connected      = true;

static char s_time_buf[10];  // "12:59 PM\0" = 9 chars in 12h; "23:59\0" = 6 in 24h
static char s_date_buf[24];

// ─── Sky background (stars) ───────────────────────────────────────────────────
// Reference positions on a 144×168 grid — never modified.
static const GPoint STARS_144[] = {
  // Main Orion stars
  {66, 5},                              // Meissa (head)
  {46,21}, {100,17},                     // Betelgeuse, Bellatrix (shoulders)
  {54,43}, {70,46}, {86,43},            // Belt: Mintaka, Alnilam, Alnitak
  {42,67}, {104,64},                     // Saiph, Rigel (feet)
  // Background fill
  {8,10},{118,8},{5,55},{135,35},
  {20,85},{130,80},{50,90},{105,90},{80,59},
  {72,30},{25,40},{110,55},{145,65}
};
#define NUM_MAIN_STARS 8
#define NUM_STARS ((int)(sizeof(STARS_144) / sizeof(STARS_144[0])))

// Scaled to screen coordinates in artemis_clock_create(); safe across destroy/create cycles.
static GPoint s_stars_scaled[NUM_STARS];
static int    s_num_stars = 0;

static void sky_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, ARTEMIS_COLOR_SKY_STARS);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < s_num_stars; i++)
    if (i < NUM_MAIN_STARS) {
      //graphics_fill_circle(ctx, s_stars_scaled[i], 2);
      graphics_draw_pixel(ctx, s_stars_scaled[i]);
      graphics_draw_pixel(ctx, GPoint (s_stars_scaled[i].x+1, s_stars_scaled[i].y));
      graphics_draw_pixel(ctx, GPoint (s_stars_scaled[i].x-1, s_stars_scaled[i].y));
      graphics_draw_pixel(ctx, GPoint (s_stars_scaled[i].x, s_stars_scaled[i].y+1));
      graphics_draw_pixel(ctx, GPoint (s_stars_scaled[i].x, s_stars_scaled[i].y-1));
    } else
      graphics_draw_pixel(ctx, s_stars_scaled[i]);
}

// ─── Bluetooth satellite indicator ───────────────────────────────────────────
// Three-box satellite shape in a 20×14 canvas (white=connected, red=disconnected).
static void satellite_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int cx = bounds.size.w / 2;
#ifdef PBL_COLOR
  GColor sat_color = s_bt_connected ? GColorBlack : GColorRed;
#else
  GColor sat_color = s_bt_connected ? GColorBlack : GColorWhite;
#endif
  graphics_context_set_fill_color(ctx, sat_color);
  graphics_fill_rect(ctx, GRect(cx - 2, 2, 4, 10), 0, GCornerNone);  // body
  graphics_fill_rect(ctx, GRect(1,      6, 6,  3),  0, GCornerNone);  // left panel
  graphics_fill_rect(ctx, GRect(13,     6, 6,  3),  0, GCornerNone);  // right panel
}

// ─── Time & date ─────────────────────────────────────────────────────────────
#ifdef DEMO_MODE
static void prv_update_time(struct tm *tick_time) {
  snprintf(s_time_buf, sizeof(s_time_buf), DEMO_MODE_TIME);
  text_layer_set_text(s_time_layer, s_time_buf);
#ifdef PBL_ROUND
  snprintf(s_date_buf, sizeof(s_date_buf), DEMO_MODE_DATE);
#else
  snprintf(s_date_buf, sizeof(s_date_buf), DEMO_MODE_DATE_LONG);
#endif
  text_layer_set_text(s_date_layer, s_date_buf);
}
#else
static void prv_update_time(struct tm *tick_time) {
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", tick_time);
  }
  text_layer_set_text(s_time_layer, s_time_buf);
#ifdef PBL_ROUND
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d", tick_time);
#else
  strftime(s_date_buf, sizeof(s_date_buf), "%a, %b %d", tick_time);
#endif
  text_layer_set_text(s_date_layer, s_date_buf);
}
#endif

// ─── Bottom zone: s_time_area_layer (moon + time + date) ─────────────────────
static void prv_create_bottom_zone(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;
  s_bottom_h = h - s_split_y;

  // Container — one layer_set_frame/hidden call moves, hides, or shows the whole zone.
  // Timeline Peek translates this layer's own frame (see artemis_clock_peek());
  // its height always stays s_bottom_h, only its Y origin changes, moving all
  // children (moon, satellite, time, date) as one rigid unit.
  s_time_area_layer = layer_create(GRect(0, s_split_y, w, s_bottom_h));
  layer_add_child(root, s_time_area_layer);

  // Moon bitmap — child of s_time_area_layer; Y=0 is relative to container top
#ifdef PBL_BW
  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_BW);
#else
  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_COLOR);
#endif

  if (s_moon_bitmap) {
    int img_w = gbitmap_get_bounds(s_moon_bitmap).size.w;
    int layer_x = (w - img_w) / 2;  // negative when image wider than screen (clips)
    s_moon_bitmap_layer = bitmap_layer_create(GRect(layer_x, 0, img_w, s_bottom_h));
    bitmap_layer_set_bitmap(s_moon_bitmap_layer, s_moon_bitmap);
    bitmap_layer_set_alignment(s_moon_bitmap_layer, GAlignCenter);
    bitmap_layer_set_compositing_mode(s_moon_bitmap_layer, GCompOpAssign);
    layer_add_child(s_time_area_layer, bitmap_layer_get_layer(s_moon_bitmap_layer));
  }

  // Satellite BT indicator — top-right of container
  s_satellite_layer = layer_create(GRect(w - 22, 2, 20, 14));
  layer_set_update_proc(s_satellite_layer, satellite_update_proc);
  layer_add_child(s_time_area_layer, s_satellite_layer);

  // Time + date block — vertically centred in the container (relative coords)
  static int block_h = FONT_TIME_H + FONT_DATE_H + 6;
  int block_y = (s_bottom_h - block_h) / 2;

  s_time_layer = artemis_make_text_layer(s_time_area_layer,
                  GRect(0, block_y, w, FONT_TIME_H),
                  ARTEMIS_COLOR_TIME, s_font_time, GTextAlignmentCenter);
  s_date_layer = artemis_make_text_layer(s_time_area_layer,
                  GRect(0, block_y + FONT_TIME_H + 2, w, FONT_DATE_H),
                  ARTEMIS_COLOR_DATE, s_font_date, GTextAlignmentCenter);
}

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_clock_create(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;

  s_num_stars = 0;
  for (int i = 0; i < NUM_STARS; i++) {
    int sx = STARS_144[i].x * w / 144;
    int sy = STARS_144[i].y * h / 168;
    if (sx >= 0 && sx < w && sy >= 0 && sy < h)
      s_stars_scaled[s_num_stars++] = GPoint(sx, sy);
  }

  s_sky_layer = layer_create(bounds);
  layer_set_update_proc(s_sky_layer, sky_update_proc);
  layer_add_child(root, s_sky_layer);

  prv_create_bottom_zone(root);
}

void artemis_clock_destroy(void) {
  if (s_time_layer)        { text_layer_destroy(s_time_layer);          s_time_layer = NULL; }
  if (s_date_layer)        { text_layer_destroy(s_date_layer);          s_date_layer = NULL; }
  if (s_satellite_layer)   { layer_destroy(s_satellite_layer);          s_satellite_layer = NULL; }
  if (s_moon_bitmap_layer) { bitmap_layer_destroy(s_moon_bitmap_layer); s_moon_bitmap_layer = NULL; }
  if (s_moon_bitmap)       { gbitmap_destroy(s_moon_bitmap);            s_moon_bitmap = NULL; }
  if (s_time_area_layer)   { layer_destroy(s_time_area_layer);          s_time_area_layer = NULL; }
  if (s_sky_layer)         { layer_destroy(s_sky_layer);                s_sky_layer = NULL; }
}

void artemis_clock_show(void) {
  if (s_time_area_layer) layer_set_hidden(s_time_area_layer, false);
}

void artemis_clock_hide(void) {
  if (s_time_area_layer) layer_set_hidden(s_time_area_layer, true);
}

void artemis_clock_peek(int unobstructed_h) {
  if (!s_time_area_layer) return;
  // Slide the whole container (full, uncompressed height s_bottom_h) up so
  // its bottom edge tracks the unobstructed boundary: top_y + s_bottom_h ==
  // unobstructed_h. Children keep their fixed relative layout and move with it.
  int top_y = unobstructed_h - s_bottom_h;
  layer_set_frame(s_time_area_layer, GRect(0, top_y, s_root_w, s_bottom_h));
}

void artemis_clock_refresh(struct tm *tick_time) {
  prv_update_time(tick_time);
}

void artemis_clock_set_bluetooth_status(bool connected) {
  s_bt_connected = connected;
  if (s_satellite_layer) layer_mark_dirty(s_satellite_layer);
}
