/**
 * @file artemis_main.c
 * @brief Always-visible background scene: sky stars, moon bitmap, time, and date.
 *
 * Draws the solid black sky with a scaled Orion constellation star field, places
 * the moon bitmap (color or B&W) centred in the bottom zone, and renders the time
 * and date text block vertically centred over the moon. These layers are always
 * present in the layer tree and are never hidden regardless of display state.
 *
 * @author LP Fabiani
 * @date 2026
 */
#include "artemis_main.h"
#include "artemis.h"

static Layer       *s_sky_layer         = NULL;
static BitmapLayer *s_moon_bitmap_layer = NULL;
static GBitmap     *s_moon_bitmap       = NULL;
static TextLayer   *s_time_layer        = NULL;
static TextLayer   *s_date_layer        = NULL;

static char s_time_buf[10];  // "12:59 PM\0" = 9 chars in 12h; "23:59\0" = 6 in 24h
static char s_date_buf[24];

// ─── Layer creation helper (local copy — also used by artemis_info) ───────────
static TextLayer *prv_make_layer(Layer *root, GRect r, GColor col,
                                 GFont font, GTextAlignment align) {
  TextLayer *tl = text_layer_create(r);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, col);
  text_layer_set_font(tl, font);
  text_layer_set_text_alignment(tl, align);
  layer_add_child(root, text_layer_get_layer(tl));
  return tl;
}

// ─── Sky background (stars) ───────────────────────────────────────────────────
static void sky_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w, h = bounds.size.h;

  // Solid black background
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  /* ── Gradient removed: two-band navy/black didn't look good ──
#ifdef PBL_COLOR
  int mid = h / 2;
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY);
  graphics_fill_rect(ctx, GRect(0, 0, w, mid), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, ARTEMIS_COLOR_SKY_HORIZON);
  graphics_fill_rect(ctx, GRect(0, mid, w, h - mid), 0, GCornerNone);
#endif
  ── */

  // Stars — positions normalized to 144×168, scaled to actual screen
  static const GPoint STARS_144[] = {
  // Main Orion stars
  {60, 6},                              // Meissa (head)
  {30,22}, {94,18},                     // Betelgeuse, Bellatrix (shoulders)
  {48,44}, {64,47}, {80,44},            // Belt: Mintaka, Alnilam, Alnitak
  {36,68}, {98,65},                     // Saiph, Rigel (feet)
  // Background fill
  {8,10},{118,8},{5,55},{135,35},
  {20,85},{130,80},{50,90},{105,90},
  {72,30},{25,40},{110,55},{145,65}
  };
  int num_stars = (int)(sizeof(STARS_144) / sizeof(STARS_144[0]));

  graphics_context_set_stroke_color(ctx, ARTEMIS_COLOR_SKY_STARS);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < num_stars; i++) {
    int sx = STARS_144[i].x * w / 144;
    int sy = STARS_144[i].y * h / 168;
    if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
      graphics_draw_pixel(ctx, GPoint(sx, sy));
    }
  }
}

// ─── Time & date ─────────────────────────────────────────────────────────────
static void prv_update_time(struct tm *tick_time) {
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  }
  else{
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M %p", tick_time);
  }
  text_layer_set_text(s_time_layer, s_time_buf);
#ifdef PBL_ROUND
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d", tick_time);   // "TUE 26" — fits narrow chord
#else
  strftime(s_date_buf, sizeof(s_date_buf), "%a, %b %d", tick_time);
#endif
  text_layer_set_text(s_date_layer, s_date_buf);
}

// ─── Bottom zone: moon bitmap + time + date ───────────────────────────────────
static void prv_create_bottom_zone(Layer *root) {
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w, h = bounds.size.h;
  int bottom_h = h - s_split_y;

  // Moon bitmap — bottom-aligned to screen edge; oversized images clip naturally
#ifdef PBL_BW
  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_BW);
//#elif defined(PBL_PLATFORM_BASALT)
//  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_SMALL_COLOR);
#else
  s_moon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MOON_COLOR);
#endif

  if (s_moon_bitmap) {
    // Layer is positioned so the image is horizontally centered on screen;
    // x may be negative when the image is wider than the screen (clips on both sides).
    int img_w = gbitmap_get_bounds(s_moon_bitmap).size.w;
    int layer_x = (w - img_w) / 2;
    s_moon_bitmap_layer = bitmap_layer_create(GRect(layer_x, s_split_y, img_w, bottom_h));
    bitmap_layer_set_bitmap(s_moon_bitmap_layer, s_moon_bitmap);
    bitmap_layer_set_alignment(s_moon_bitmap_layer, GAlignCenter);
    bitmap_layer_set_compositing_mode(s_moon_bitmap_layer, GCompOpAssign);
    layer_add_child(root, bitmap_layer_get_layer(s_moon_bitmap_layer));
  }

  // Time + date block — vertically centered in the bottom zone
  //int time_h  = bottom_h - FONT_DATE_H - 8;
  //int block_h = time_h + 2 + FONT_DATE_H;
  int time_h  = FONT_TIME_H;

  int block_h = FONT_TIME_H + FONT_DATE_H + 6;
  int block_y = s_split_y + (bottom_h - block_h) / 2;
  int time_y = block_y;
  int date_y = block_y + time_h + 2;

  s_time_layer = prv_make_layer(root, GRect(0, time_y, w, FONT_TIME_H),
                  ARTEMIS_COLOR_TIME, s_font_time, GTextAlignmentCenter);

  s_date_layer = prv_make_layer(root, GRect(0, date_y, w, FONT_DATE_H),
                  ARTEMIS_COLOR_DATE, s_font_date, GTextAlignmentCenter);
}

// ─── Public API ───────────────────────────────────────────────────────────────
void artemis_main_create(Layer *root) {
  GRect bounds = layer_get_bounds(root);

  s_sky_layer = layer_create(bounds);
  layer_set_update_proc(s_sky_layer, sky_update_proc);
  layer_add_child(root, s_sky_layer);

  prv_create_bottom_zone(root);
}

void artemis_main_destroy(void) {
  if (s_sky_layer)         { layer_destroy(s_sky_layer);                       s_sky_layer = NULL; }
  if (s_moon_bitmap_layer) { bitmap_layer_destroy(s_moon_bitmap_layer);         s_moon_bitmap_layer = NULL; }
  if (s_moon_bitmap)       { gbitmap_destroy(s_moon_bitmap);                    s_moon_bitmap = NULL; }
  if (s_time_layer)        { text_layer_destroy(s_time_layer);                  s_time_layer = NULL; }
  if (s_date_layer)        { text_layer_destroy(s_date_layer);                  s_date_layer = NULL; }
}

void artemis_main_show(void) {
  if (s_sky_layer)         layer_set_hidden(s_sky_layer, false);
  if (s_moon_bitmap_layer) layer_set_hidden(bitmap_layer_get_layer(s_moon_bitmap_layer), false);
  if (s_time_layer)        layer_set_hidden(text_layer_get_layer(s_time_layer), false);
  if (s_date_layer)        layer_set_hidden(text_layer_get_layer(s_date_layer), false);
}

void artemis_main_hide(void) {
  if (s_sky_layer)         layer_set_hidden(s_sky_layer, true);
  if (s_moon_bitmap_layer) layer_set_hidden(bitmap_layer_get_layer(s_moon_bitmap_layer), true);
  if (s_time_layer)        layer_set_hidden(text_layer_get_layer(s_time_layer), true);
  if (s_date_layer)        layer_set_hidden(text_layer_get_layer(s_date_layer), true);
}

void artemis_main_refresh(struct tm *tick_time) {
  prv_update_time(tick_time);
}
