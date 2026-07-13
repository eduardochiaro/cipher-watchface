#include <pebble.h>

#define SHADOW_OFFSET 2
#define TIME_HEIGHT 44

// battery/numbers ~emery/~gabbro sheets are 2x
#if PBL_PLATFORM_EMERY || PBL_PLATFORM_GABBRO
  #define BATT_W 23
  #define BATT_H 11
  #define DIGIT_W 9
  #define DIGIT_H 11
  #define DIGIT_GAP 2
  #define MODULE_GAP 6
  #define EDGE_MARGIN 10
  #define TEXT_Y_OFFSET 0
  #define ICON_SZ 11
#else
  #define BATT_W 15
  #define BATT_H 8
  #define DIGIT_W 5
  #define DIGIT_H 8
  #define DIGIT_GAP 1
  #define MODULE_GAP 3
  #define EDGE_MARGIN 5
  #define TEXT_Y_OFFSET 0
  #define ICON_SZ 8
#endif
// right-column rows: up to 6 glyphs plus a trailing icon
#define ROW_W (6 * (DIGIT_W + DIGIT_GAP) + ICON_SZ)

// icons.png: 10 square cells, left to right
enum {
  ICON_BT_ON, ICON_BT_OFF, ICON_FLAME, ICON_SHOE, ICON_HEART,
  ICON_SUN, ICON_CLOUD, ICON_RAIN, ICON_SNOW, ICON_STORM,
  ICON_COUNT
};
// "WEDNESDAY" = 9 glyph cells; "MAY 31" = 6 (space advances like a glyph)
#define DAY_W (9 * (DIGIT_W + DIGIT_GAP) - DIGIT_GAP)
#define DATE_W (6 * (DIGIT_W + DIGIT_GAP) - DIGIT_GAP)
// space advance = 60% of a glyph cell, rounded
#define SPACE_ADV (((DIGIT_W + DIGIT_GAP) * 3 + 2) / 5)

#define PERSIST_ANAGLYPH 1
#define PERSIST_FLICK_ANIM 2

static bool s_anaglyph = true;
static bool s_flick_anim = true;

static Window *s_window;
static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;
static Layer *s_time_canvas;
static BitmapLayer *s_battery_layer;
static GBitmap *s_battery_sheet;
static GBitmap *s_battery_bitmap;
static GBitmap *s_numbers_sheet;
static GBitmap *s_digit_bitmaps[10];
static GBitmap *s_letters_sheet;
static GBitmap *s_letter_bitmaps[26];
static GBitmap *s_icons_sheet;
static GBitmap *s_icon_bitmaps[ICON_COUNT];
static Layer *s_day_layer;
static Layer *s_date_layer;
static Layer *s_bt_layer;
static char s_day_buffer[12];
static char s_date_buffer[8];
static bool s_bt_connected;
#if defined(PBL_HEALTH)
static Layer *s_steps_layer;
static Layer *s_dist_layer;
static Layer *s_cal_layer;
static int s_steps;
static int s_distance_m;
static int s_kcal;
#endif
static GFont s_time_font;
static char s_hour_buffer[4];
static char s_minute_buffer[4];

static void prv_update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  strftime(s_hour_buffer, sizeof(s_hour_buffer),
           clock_is_24h_style() ? "%H" : "%I", tick_time);
  strftime(s_minute_buffer, sizeof(s_minute_buffer), "%M", tick_time);
  layer_mark_dirty(s_time_canvas);

  // "SUNDAY" / "JUL 12" — letter sheet is caps-only
  char day[12], date[8], mon[4];
  strftime(day, sizeof(day), "%A", tick_time);
  strftime(mon, sizeof(mon), "%b", tick_time);
  snprintf(date, sizeof(date), "%s %d", mon, tick_time->tm_mday);
  for (char *p = day; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p -= 'a' - 'A';
    }
  }
  for (char *p = date; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p -= 'a' - 'A';
    }
  }
  if (s_day_layer && strcmp(day, s_day_buffer) != 0) {
    strncpy(s_day_buffer, day, sizeof(s_day_buffer));
    layer_mark_dirty(s_day_layer);
  }
  if (s_date_layer && strcmp(date, s_date_buffer) != 0) {
    strncpy(s_date_buffer, date, sizeof(s_date_buffer));
    layer_mark_dirty(s_date_layer);
  }
}

static void prv_draw_time(GContext *ctx, GRect bounds, int16_t dx, int16_t dy,
                          GColor hour_color, GColor colon_color, GColor minute_color) {
  const char *pieces[3] = { s_hour_buffer, ":", s_minute_buffer };
  GColor colors[3] = { hour_color, colon_color, minute_color };
  GRect measure = GRect(0, 0, bounds.size.w, bounds.size.h);

  int widths[3];
  int total = 0;
  for (int i = 0; i < 3; i++) {
    widths[i] = graphics_text_layout_get_content_size(pieces[i], s_time_font,
        measure, GTextOverflowModeWordWrap, GTextAlignmentLeft).w;
    total += widths[i];
  }

  int x = (bounds.size.w - total) / 2 + dx;
  for (int i = 0; i < 3; i++) {
    graphics_context_set_text_color(ctx, colors[i]);
    graphics_draw_text(ctx, pieces[i], s_time_font,
        GRect(x, dy, widths[i], bounds.size.h - dy),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    x += widths[i];
  }
}

static void prv_time_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  // shadow pass behind, then colored pass
  prv_draw_time(ctx, bounds, SHADOW_OFFSET, SHADOW_OFFSET,
                GColorBlack, GColorBlack, GColorBlack);
  GColor grey = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
  if (s_anaglyph) {
    prv_draw_time(ctx, bounds, 0, 0,
                  PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite),
                  grey,
                  PBL_IF_COLOR_ELSE(GColorFolly, GColorWhite));
  } else {
    prv_draw_time(ctx, bounds, 0, 0, grey, grey, grey);
  }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();
}

#if defined(PBL_ROUND)
// ponytail: loop isqrt, runs once per module at load, n <= 8100
static int prv_isqrt(int n) {
  int x = 0;
  while ((x + 1) * (x + 1) <= n) x++;
  return x;
}

// half-width of the screen circle's chord across the band [y, y+h)
static int prv_round_chord_halfw(GRect bounds, int y, int h) {
  int cx = bounds.size.w / 2;
  int cy = bounds.size.h / 2;
  // widest inset of the band: its row farthest from vertical center
  int dy = (y + h <= cy) ? cy - y : (y + h) - cy;
  if (dy >= cx) {
    return 0;
  }
  return prv_isqrt(cx * cx - dy * dy);
}

static int prv_round_right_edge(GRect bounds, int y, int h) {
  return bounds.size.w / 2 + prv_round_chord_halfw(bounds, y, h);
}

static int prv_round_left_edge(GRect bounds, int y, int h) {
  return bounds.size.w / 2 - prv_round_chord_halfw(bounds, y, h);
}
#endif

// battery.png: 5x2 grid of 15x8 sprites, 100% top-left down to 10% bottom-right
static void prv_battery_handler(BatteryChargeState state) {
  int idx = (100 - state.charge_percent) / 10;
  if (idx < 0) idx = 0;
  if (idx > 9) idx = 9;  // 0% reuses the 10% sprite
  if (s_battery_bitmap) {
    gbitmap_destroy(s_battery_bitmap);
  }
  // stride from sheet, not BATT_W: sprite may be narrower than its grid cell
  int stride = gbitmap_get_bounds(s_battery_sheet).size.w / 5;
  s_battery_bitmap = gbitmap_create_as_sub_bitmap(s_battery_sheet,
      GRect((idx % 5) * stride, (idx / 5) * BATT_H, BATT_W, BATT_H));
  bitmap_layer_set_bitmap(s_battery_layer, s_battery_bitmap);
}

// sprite glyphs: digits from numbers.png (1..9,0), caps from letters.png (A..Z)
static GBitmap *prv_glyph_for(char c) {
  if (c >= '0' && c <= '9') {
    return s_digit_bitmaps[c - '0'];
  }
  if (c >= 'A' && c <= 'Z') {
    return s_letter_bitmaps[c - 'A'];
  }
  return NULL;  // space: advance only
}

static void prv_draw_glyphs(GContext *ctx, const char *str, int x) {
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  for (; *str; str++) {
    GBitmap *g = prv_glyph_for(*str);
    if (g) {
      graphics_draw_bitmap_in_rect(ctx, g, GRect(x, 0, DIGIT_W, DIGIT_H));
      x += DIGIT_W + DIGIT_GAP;
    } else {
      x += SPACE_ADV;
    }
  }
}

static int prv_glyphs_width(const char *str) {
  int w = 0;
  for (; *str; str++) {
    w += prv_glyph_for(*str) ? DIGIT_W + DIGIT_GAP : SPACE_ADV;
  }
  return w - DIGIT_GAP;  // drop trailing gap
}

// text right-aligned against the layer's right edge, optional trailing icon
static void prv_draw_row_right(Layer *layer, GContext *ctx,
                               const char *str, int icon) {
  GRect bounds = layer_get_bounds(layer);
  int x = bounds.size.w - prv_glyphs_width(str);
  if (icon >= 0) {
    x -= ICON_SZ + DIGIT_GAP;
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_icon_bitmaps[icon],
        GRect(bounds.size.w - ICON_SZ, 0, ICON_SZ, ICON_SZ));
  }
  prv_draw_glyphs(ctx, str, x);
}

static void prv_day_update_proc(Layer *layer, GContext *ctx) {
  prv_draw_glyphs(ctx, s_day_buffer, 0);
}

static void prv_date_update_proc(Layer *layer, GContext *ctx) {
  prv_draw_glyphs(ctx, s_date_buffer, 0);
}

static void prv_bt_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx,
      s_icon_bitmaps[s_bt_connected ? ICON_BT_ON : ICON_BT_OFF],
      GRect(0, 0, ICON_SZ, ICON_SZ));
  prv_draw_glyphs(ctx, s_bt_connected ? "ON" : "OFF", ICON_SZ + DIGIT_GAP);
}

static void prv_bt_handler(bool connected) {
  s_bt_connected = connected;
  if (s_bt_layer) {
    layer_mark_dirty(s_bt_layer);
  }
}

#if defined(PBL_HEALTH)
static void prv_steps_update_proc(Layer *layer, GContext *ctx) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", s_steps);
  prv_draw_row_right(layer, ctx, buf, ICON_SHOE);
}

static void prv_dist_update_proc(Layer *layer, GContext *ctx) {
  char buf[16];
  if (health_service_get_measurement_system_for_display(
          HealthMetricWalkedDistanceMeters) == MeasurementSystemImperial) {
    snprintf(buf, sizeof(buf), "%d MI", s_distance_m * 1000 / 1609344);
  } else if (s_distance_m >= 1000) {
    snprintf(buf, sizeof(buf), "%d KM", s_distance_m / 1000);
  } else {
    snprintf(buf, sizeof(buf), "%d M", s_distance_m);
  }
  prv_draw_row_right(layer, ctx, buf, -1);
}

static void prv_cal_update_proc(Layer *layer, GContext *ctx) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", s_kcal);
  prv_draw_row_right(layer, ctx, buf, ICON_FLAME);
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
    s_distance_m = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
    s_kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
    layer_mark_dirty(s_steps_layer);
    layer_mark_dirty(s_dist_layer);
    layer_mark_dirty(s_cal_layer);
  }
}
#endif

static bool s_hovering;

static void prv_hover_stopped(Animation *animation, bool finished, void *context) {
  s_hovering = false;
}

static void prv_hover_animation(void) {
  if (s_hovering) {
    return;
  }
  s_hovering = true;

  Layer *layer = bitmap_layer_get_layer(s_background_layer);
  GRect rest = layer_get_frame(layer);
  GRect up = rest;
  up.origin.y -= 20;

  PropertyAnimation *pa_up = property_animation_create_layer_frame(layer, &rest, &up);
  PropertyAnimation *pa_down = property_animation_create_layer_frame(layer, &up, &rest);
  Animation *a_up = property_animation_get_animation(pa_up);
  Animation *a_down = property_animation_get_animation(pa_down);
  animation_set_duration(a_up, 1200);
  animation_set_duration(a_down, 1200);
  animation_set_curve(a_up, AnimationCurveEaseInOut);
  animation_set_curve(a_down, AnimationCurveEaseInOut);

  Animation *seq = animation_sequence_create(a_up, a_down, NULL);
  animation_set_play_count(seq, 2);
  animation_set_handlers(seq, (AnimationHandlers) {
    .stopped = prv_hover_stopped,
  }, NULL);
  animation_schedule(seq);
}

// backlight has no event API; the wrist-flick that lights it also fires accel tap
static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_flick_anim) {
    prv_hover_animation();
  }
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_TIME_COLOR);
  if (t) {
    s_anaglyph = strcmp(t->value->cstring, "anaglyph") == 0;
    persist_write_bool(PERSIST_ANAGLYPH, s_anaglyph);
    if (s_time_canvas) {
      layer_mark_dirty(s_time_canvas);
    }
  }
  t = dict_find(iter, MESSAGE_KEY_FLICK_ANIMATION);
  if (t) {
    s_flick_anim = t->value->int32 == 1;
    persist_write_bool(PERSIST_FLICK_ANIM, s_flick_anim);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  s_background_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
  bitmap_layer_set_alignment(s_background_layer, GAlignCenter);
  bitmap_layer_set_compositing_mode(s_background_layer, GCompOpSet);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_background_layer));

  // 200px-wide screens (emery, gabbro) get the biggest LECO
  bool big_screen = bounds.size.w >= 200;
  s_time_font = fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS);
  #if PBL_PLATFORM_EMERY || PBL_PLATFORM_GABBRO
    s_time_font = fonts_get_system_font(FONT_KEY_LECO_60_NUMBERS_AM_PM);
  #endif
  int time_height = big_screen ? 68 : TIME_HEIGHT;
  int bottom_margin = PBL_IF_ROUND_ELSE(24, 4);
  GRect time_frame = GRect(0, bounds.size.h - time_height - bottom_margin,
                           bounds.size.w, time_height);

  s_time_canvas = layer_create(time_frame);
  layer_set_update_proc(s_time_canvas, prv_time_update_proc);
  layer_add_child(window_layer, s_time_canvas);

  s_battery_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
#if defined(PBL_ROUND)
  // hug the circular edge: right edge of each module follows the chord at its y
  int batt_y = 20;
  GRect batt_frame = GRect(
      prv_round_right_edge(bounds, batt_y, BATT_H) - EDGE_MARGIN - BATT_W,
      batt_y, BATT_W, BATT_H);
#else
  GRect batt_frame = GRect(bounds.size.w - BATT_W - EDGE_MARGIN,
                           EDGE_MARGIN, BATT_W, BATT_H);
#endif
  s_battery_layer = bitmap_layer_create(batt_frame);
  bitmap_layer_set_compositing_mode(s_battery_layer, GCompOpSet);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_battery_layer));
  battery_state_service_subscribe(prv_battery_handler);
  prv_battery_handler(battery_state_service_peek());

  s_numbers_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_NUMBERS);
  for (int d = 0; d < 10; d++) {
    int col = (d == 0) ? 9 : d - 1;
    s_digit_bitmaps[d] = gbitmap_create_as_sub_bitmap(s_numbers_sheet,
        GRect(col * DIGIT_W, 0, DIGIT_W, DIGIT_H));
  }
  s_letters_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LETTERS);
  for (int l = 0; l < 26; l++) {
    s_letter_bitmaps[l] = gbitmap_create_as_sub_bitmap(s_letters_sheet,
        GRect(l * DIGIT_W, 0, DIGIT_W, DIGIT_H));
  }
  s_icons_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICONS);
  for (int i = 0; i < ICON_COUNT; i++) {
    s_icon_bitmaps[i] = gbitmap_create_as_sub_bitmap(s_icons_sheet,
        GRect(i * ICON_SZ, 0, ICON_SZ, ICON_SZ));
  }

  // top-left: weekday, then month + day stacked below, same gap as right side;
  // nudged down so text balances the taller battery sprite
  int day_y = batt_frame.origin.y + TEXT_Y_OFFSET;
  int date_y = day_y + DIGIT_H + MODULE_GAP;
#if defined(PBL_ROUND)
  GRect day_frame = GRect(prv_round_left_edge(bounds, day_y, DIGIT_H) + EDGE_MARGIN,
                          day_y, DAY_W, DIGIT_H);
  GRect date_frame = GRect(prv_round_left_edge(bounds, date_y, DIGIT_H) + EDGE_MARGIN,
                           date_y, DATE_W, DIGIT_H);
#else
  GRect day_frame = GRect(EDGE_MARGIN, day_y, DAY_W, DIGIT_H);
  GRect date_frame = GRect(EDGE_MARGIN, date_y, DATE_W, DIGIT_H);
#endif
  s_day_layer = layer_create(day_frame);
  layer_set_update_proc(s_day_layer, prv_day_update_proc);
  layer_add_child(window_layer, s_day_layer);
  s_date_layer = layer_create(date_frame);
  layer_set_update_proc(s_date_layer, prv_date_update_proc);
  layer_add_child(window_layer, s_date_layer);

  // bluetooth icon + ON/OFF below the date
  int bt_y = date_y + DIGIT_H + MODULE_GAP;
  int bt_w = ICON_SZ + DIGIT_GAP + 3 * (DIGIT_W + DIGIT_GAP) - DIGIT_GAP;
#if defined(PBL_ROUND)
  GRect bt_frame = GRect(prv_round_left_edge(bounds, bt_y, ICON_SZ) + EDGE_MARGIN,
                         bt_y, bt_w, ICON_SZ);
#else
  GRect bt_frame = GRect(EDGE_MARGIN, bt_y, bt_w, ICON_SZ);
#endif
  s_bt_layer = layer_create(bt_frame);
  layer_set_update_proc(s_bt_layer, prv_bt_update_proc);
  layer_add_child(window_layer, s_bt_layer);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = prv_bt_handler,
  });
  s_bt_connected = connection_service_peek_pebble_app_connection();

#if defined(PBL_HEALTH)
  // right column under battery: steps, distance, calories
  Layer **row_layers[3] = { &s_steps_layer, &s_dist_layer, &s_cal_layer };
  LayerUpdateProc row_procs[3] = {
    prv_steps_update_proc, prv_dist_update_proc, prv_cal_update_proc
  };
  int row_y = batt_frame.origin.y + batt_frame.size.h + MODULE_GAP;
  for (int i = 0; i < 3; i++) {
#if defined(PBL_ROUND)
    GRect row_frame = GRect(
        prv_round_right_edge(bounds, row_y, DIGIT_H) - EDGE_MARGIN - ROW_W,
        row_y, ROW_W, DIGIT_H);
#else
    GRect row_frame = GRect(bounds.size.w - EDGE_MARGIN - ROW_W,
                            row_y, ROW_W, DIGIT_H);
#endif
    *row_layers[i] = layer_create(row_frame);
    layer_set_update_proc(*row_layers[i], row_procs[i]);
    layer_add_child(window_layer, *row_layers[i]);
    row_y += DIGIT_H + MODULE_GAP;
  }
  health_service_events_subscribe(prv_health_handler, NULL);
  s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  s_distance_m = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
  s_kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
#endif

  prv_update_time();
  prv_hover_animation();
}

static void prv_window_unload(Window *window) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
  layer_destroy(s_steps_layer);
  layer_destroy(s_dist_layer);
  layer_destroy(s_cal_layer);
#endif
  connection_service_unsubscribe();
  layer_destroy(s_bt_layer);
  for (int i = 0; i < ICON_COUNT; i++) {
    gbitmap_destroy(s_icon_bitmaps[i]);
  }
  gbitmap_destroy(s_icons_sheet);
  layer_destroy(s_day_layer);
  layer_destroy(s_date_layer);
  for (int d = 0; d < 10; d++) {
    gbitmap_destroy(s_digit_bitmaps[d]);
  }
  gbitmap_destroy(s_numbers_sheet);
  for (int l = 0; l < 26; l++) {
    gbitmap_destroy(s_letter_bitmaps[l]);
  }
  gbitmap_destroy(s_letters_sheet);
  battery_state_service_unsubscribe();
  bitmap_layer_destroy(s_battery_layer);
  gbitmap_destroy(s_battery_bitmap);
  gbitmap_destroy(s_battery_sheet);
  layer_destroy(s_time_canvas);
  bitmap_layer_destroy(s_background_layer);
  gbitmap_destroy(s_background_bitmap);
}

static void prv_init(void) {
  if (persist_exists(PERSIST_ANAGLYPH)) {
    s_anaglyph = persist_read_bool(PERSIST_ANAGLYPH);
  }
  if (persist_exists(PERSIST_FLICK_ANIM)) {
    s_flick_anim = persist_read_bool(PERSIST_FLICK_ANIM);
  }

  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(128, 128);

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  accel_tap_service_subscribe(prv_tap_handler);
}

static void prv_deinit(void) {
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
