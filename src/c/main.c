#include <pebble.h>

#define SHADOW_OFFSET 2
#define TIME_HEIGHT 44

// battery/numbers ~emery/~gabbro sheets are 2x
#if PBL_PLATFORM_EMERY || PBL_PLATFORM_GABBRO
  #define BATT_W 30
  #define BATT_H 16
  #define DIGIT_W 9
  #define DIGIT_H 11
  #define DIGIT_GAP 2
  #define MODULE_GAP 6
  #define EDGE_MARGIN 10
#else
  #define BATT_W 15
  #define BATT_H 8
  #define DIGIT_W 5
  #define DIGIT_H 8
  #define DIGIT_GAP 1
  #define MODULE_GAP 3
  #define EDGE_MARGIN 5
#endif
#define STEPS_W (6 * (DIGIT_W + DIGIT_GAP) - DIGIT_GAP)

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
#if defined(PBL_HEALTH)
static Layer *s_steps_layer;
static GBitmap *s_numbers_sheet;
static GBitmap *s_digit_bitmaps[10];
static int s_steps;
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

// x of the screen circle's right edge across the band [y, y+h)
static int prv_round_right_edge(GRect bounds, int y, int h) {
  int cx = bounds.size.w / 2;
  int cy = bounds.size.h / 2;
  // widest inset of the band: its row farthest from vertical center
  int dy = (y + h <= cy) ? cy - y : (y + h) - cy;
  if (dy >= cx) {
    return cx;
  }
  return cx + prv_isqrt(cx * cx - dy * dy);
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
  s_battery_bitmap = gbitmap_create_as_sub_bitmap(s_battery_sheet,
      GRect((idx % 5) * BATT_W, (idx / 5) * BATT_H, BATT_W, BATT_H));
  bitmap_layer_set_bitmap(s_battery_layer, s_battery_bitmap);
}

#if defined(PBL_HEALTH)
// numbers.png: 10 digits of 5x8, left to right 1,2,...,9,0
static void prv_steps_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", s_steps);
  int len = strlen(buf);
  // right-aligned, digit + gap advance
  int x = bounds.size.w - (len * (DIGIT_W + DIGIT_GAP) - DIGIT_GAP);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  for (int i = 0; i < len; i++) {
    graphics_draw_bitmap_in_rect(ctx, s_digit_bitmaps[buf[i] - '0'],
                                 GRect(x, 0, DIGIT_W, DIGIT_H));
    x += DIGIT_W + DIGIT_GAP;
  }
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
    layer_mark_dirty(s_steps_layer);
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

#if defined(PBL_HEALTH)
  s_numbers_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_NUMBERS);
  for (int d = 0; d < 10; d++) {
    int col = (d == 0) ? 9 : d - 1;
    s_digit_bitmaps[d] = gbitmap_create_as_sub_bitmap(s_numbers_sheet,
        GRect(col * DIGIT_W, 0, DIGIT_W, DIGIT_H));
  }
  // right-aligned under battery, wide enough for 6 digits
  int steps_y = batt_frame.origin.y + batt_frame.size.h + MODULE_GAP;
#if defined(PBL_ROUND)
  GRect steps_frame = GRect(
      prv_round_right_edge(bounds, steps_y, DIGIT_H) - EDGE_MARGIN - STEPS_W,
      steps_y, STEPS_W, DIGIT_H);
#else
  GRect steps_frame = GRect(batt_frame.origin.x + batt_frame.size.w - STEPS_W,
                            steps_y, STEPS_W, DIGIT_H);
#endif
  s_steps_layer = layer_create(steps_frame);
  layer_set_update_proc(s_steps_layer, prv_steps_update_proc);
  layer_add_child(window_layer, s_steps_layer);
  health_service_events_subscribe(prv_health_handler, NULL);
  s_steps = (int)health_service_sum_today(HealthMetricStepCount);
#endif

  prv_update_time();
  prv_hover_animation();
}

static void prv_window_unload(Window *window) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
  layer_destroy(s_steps_layer);
  for (int d = 0; d < 10; d++) {
    gbitmap_destroy(s_digit_bitmaps[d]);
  }
  gbitmap_destroy(s_numbers_sheet);
#endif
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
