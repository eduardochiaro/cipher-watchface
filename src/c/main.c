#include <pebble.h>
#include <ctype.h>

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
  #define ICON_SZ 11
  #define SHADOW_OFFSET 4
#else
  #define BATT_W 15
  #define BATT_H 8
  #define DIGIT_W 5
  #define DIGIT_H 8
  #define DIGIT_GAP 1
  #define MODULE_GAP 3
  #define EDGE_MARGIN 5
  #define ICON_SZ 8
  #define SHADOW_OFFSET 2
#endif

// icons.png: 13 square cells, left to right
enum {
  ICON_BT_ON, ICON_BT_OFF, ICON_FLAME, ICON_SHOE, ICON_HEART,
  ICON_SUN, ICON_CLOUD, ICON_RAIN, ICON_SNOW, ICON_STORM,
  ICON_KM, ICON_M, ICON_MI,
  ICON_COUNT
};
// space advance = 60% of a glyph cell, rounded
#define SPACE_ADV (((DIGIT_W + DIGIT_GAP) * 3 + 2) / 5)

// user-assignable row modules; ids match config.js option values
enum {
  MOD_NONE, MOD_DAY, MOD_DATE, MOD_BATTERY, MOD_BT, MOD_WEATHER,
  MOD_STEPS, MOD_DIST, MOD_CAL, MOD_YEAR, MOD_HR,
  MOD_TYPE_COUNT
};
#define SLOTS_PER_SIDE 5
// rows start below the top edge; round leaves room for the bezel
// gabbro centers each side's column on the screen instead (see rows proc)
#define ROWS_TOP PBL_IF_ROUND_ELSE(20, EDGE_MARGIN)

#define PERSIST_ANAGLYPH 1
#define PERSIST_FLICK_ANIM 2
#define PERSIST_LEFT_MODS 3
#define PERSIST_RIGHT_MODS 4
#define PERSIST_FLAT_COLOR 5

// 0/1 keep old persisted bool meaning (flat/anaglyph);
// TIME_FLAT's wire value stays "white" for config compat
enum {
  TIME_FLAT = 0, TIME_ANAGLYPH = 1, TIME_COLORFUL = 2, TIME_ANAGLYPH_FLAT = 3
};
static uint8_t s_time_color = TIME_ANAGLYPH;
static GColor s_flat_color;  // init to white in prv_init
static bool s_flick_anim = true;
static uint8_t s_left_mods[SLOTS_PER_SIDE] = {
  MOD_DAY, MOD_DATE, MOD_BT, MOD_WEATHER, MOD_NONE
};
static uint8_t s_right_mods[SLOTS_PER_SIDE] = {
  MOD_BATTERY, MOD_STEPS, MOD_DIST, MOD_CAL, MOD_NONE
};

static Window *s_window;
static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;
static Layer *s_time_canvas;
static Layer *s_rows_layer;
static GBitmap *s_battery_sheet;
static GBitmap *s_battery_bitmap;
static GBitmap *s_numbers_sheet;
static GBitmap *s_digit_bitmaps[10];
static GBitmap *s_letters_sheet;
static GBitmap *s_letter_bitmaps[26];
static GBitmap *s_icons_sheet;
static GBitmap *s_icon_bitmaps[ICON_COUNT];
static char s_day_buffer[12];
static char s_date_buffer[8];
static bool s_bt_connected;
static bool s_weather_valid;
static int s_weather_temp;
static int s_weather_code;
#if defined(PBL_HEALTH)
static int s_steps;
static int s_distance_m;
static int s_kcal;
static int s_hrm;  // stays 0 on watches without a heart-rate monitor
#endif
static GFont s_time_font;
static char s_time_buffer[8];

static void prv_update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  layer_mark_dirty(s_time_canvas);

  // "SUNDAY" / "JUL 12" — letter sheet is caps-only
  char day[12], date[8], mon[4];
  strftime(day, sizeof(day), "%A", tick_time);
  strftime(mon, sizeof(mon), "%b", tick_time);
  snprintf(date, sizeof(date), "%s %d", mon, tick_time->tm_mday);
  for (char *p = day; *p; p++) *p = toupper((unsigned char)*p);
  for (char *p = date; *p; p++) *p = toupper((unsigned char)*p);
  bool changed = false;
  if (strcmp(day, s_day_buffer) != 0) {
    strncpy(s_day_buffer, day, sizeof(s_day_buffer));
    changed = true;
  }
  if (strcmp(date, s_date_buffer) != 0) {
    strncpy(s_date_buffer, date, sizeof(s_date_buffer));
    changed = true;
  }
  if (changed && s_rows_layer) {
    layer_mark_dirty(s_rows_layer);
  }
}

static void prv_draw_time(GContext *ctx, GRect bounds, int16_t dx, int16_t dy,
                          GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, s_time_buffer, s_time_font,
      GRect(dx, dy, bounds.size.w, bounds.size.h),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

#if defined(PBL_COLOR)
// no gradient text API: main pass drawn in a sentinel color nothing else on
// screen uses, then its framebuffer rows remapped to 3 horizontal bands
static void prv_gradient_remap(GContext *ctx, Layer *layer) {
  static const uint8_t bands[3] = {
    GColorYellowARGB8, GColorOrangeARGB8, GColorDarkCandyAppleRedARGB8
  };
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return;
  }
  GRect frame = layer_get_frame(layer);
  int w = frame.size.w;
  int h = frame.size.h;
  int amp = h / 4;  // arc depth of the yellow band's bottom edge
  for (int y = 0; y < h; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, frame.origin.y + y);
    for (int x = row.min_x; x <= row.max_x; x++) {
      if (row.data[x] != GColorImperialPurpleARGB8) {
        continue;
      }
      // yellow/chrome boundary bows down mid-screen (parabola, 0 at edges) 
      int bulge = amp * 4 * x * (w - 1 - x) / ((w - 1) * (w - 1));
      uint8_t c;
      if (y < h / 3 + bulge) {
        c = bands[0];
      } else if (y < 2 * h / 3) {
        c = bands[1];
      } else {
        c = bands[2];
      }
      row.data[x] = c;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}
#endif

static void prv_time_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  // two shadow passes, offset opposite ways, then main pass on top
  bool shadows = s_time_color == TIME_ANAGLYPH ||
                 s_time_color == TIME_ANAGLYPH_FLAT;
  if (shadows) {
    prv_draw_time(ctx, bounds, -SHADOW_OFFSET, -SHADOW_OFFSET,
                  PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack));
    prv_draw_time(ctx, bounds, SHADOW_OFFSET, SHADOW_OFFSET,
                  PBL_IF_COLOR_ELSE(GColorFolly, GColorBlack));
  }
  //black border
  prv_draw_time(ctx, bounds, -1, -1, GColorBlack);
  prv_draw_time(ctx, bounds, -1, 1, GColorBlack);
  prv_draw_time(ctx, bounds, 1, -1, GColorBlack);
  prv_draw_time(ctx, bounds, 1, 1, GColorBlack);
#if defined(PBL_COLOR)
  if (s_time_color != TIME_FLAT) {
    prv_draw_time(ctx, bounds, 0, 0, GColorImperialPurple);
    if (s_time_color != TIME_ANAGLYPH_FLAT) {
      prv_gradient_remap(ctx, layer);
    }
    return;
  }
#endif
  prv_draw_time(ctx, bounds, 0, 0,
                PBL_IF_COLOR_ELSE(s_flat_color, GColorWhite));
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();
  // empty ping: pkjs treats any inbound appmessage as a weather-refresh request
  if (tick_time->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      app_message_outbox_send();
    }
  }
}

#if defined(PBL_ROUND)
// ponytail: loop isqrt, runs once per row per redraw, n <= 8100
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
#endif

// usable x extents of a row at height y: hugs the circle on round screens
static void prv_row_edges(GRect bounds, int y, int *lx, int *rx) {
#if defined(PBL_ROUND)
  int halfw = prv_round_chord_halfw(bounds, y, DIGIT_H);
  *lx = bounds.size.w / 2 - halfw + EDGE_MARGIN;
  *rx = bounds.size.w / 2 + halfw - EDGE_MARGIN;
#else
  *lx = EDGE_MARGIN;
  *rx = bounds.size.w - EDGE_MARGIN;
#endif
}

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
  if (s_rows_layer) {
    layer_mark_dirty(s_rows_layer);
  }
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

#define DOT_SZ (DIGIT_GAP + 1)

static void prv_draw_glyphs(GContext *ctx, const char *str, int x, int y) {
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  for (; *str; str++) {
    GBitmap *g = prv_glyph_for(*str);
    if (g) {
      graphics_draw_bitmap_in_rect(ctx, g, GRect(x, y, DIGIT_W, DIGIT_H));
      x += DIGIT_W + DIGIT_GAP;
    } else if (*str == '.') {
      // sprites have no '.': baseline dot to match the black glyphs
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(x, y + DIGIT_H - DOT_SZ, DOT_SZ, DOT_SZ),
                         0, GCornerNone);
      x += DOT_SZ + DIGIT_GAP;
    } else {
      x += SPACE_ADV;
    }
  }
}

static int prv_glyphs_width(const char *str) {
  int w = 0;
  for (; *str; str++) {
    if (prv_glyph_for(*str)) {
      w += DIGIT_W + DIGIT_GAP;
    } else if (*str == '.') {
      w += DOT_SZ + DIGIT_GAP;
    } else {
      w += SPACE_ADV;
    }
  }
  return w - DIGIT_GAP;  // drop trailing gap
}

// WMO weather codes (Open-Meteo) to icon cell
static int prv_weather_icon(int code) {
  if (code >= 95) return ICON_STORM;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return ICON_SNOW;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return ICON_RAIN;
  if (code <= 1) return ICON_SUN;
  return ICON_CLOUD;  // partly cloudy, overcast, fog
}

// icon + minus + digits + degree dot; icon hugs the outer edge of the column
static void prv_draw_weather(GContext *ctx, int y, int lx, int rx, bool right) {
  if (!s_weather_valid) {
    return;  // row stays reserved so layout doesn't jump when data arrives
  }
  int t = s_weather_temp;
  bool neg = t < 0;
  if (neg) t = -t;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", t);
  int text_w = (neg ? DIGIT_W + DIGIT_GAP : 0) + prv_glyphs_width(buf)
             + DIGIT_GAP + DIGIT_GAP + 1;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  GBitmap *icon = s_icon_bitmaps[prv_weather_icon(s_weather_code)];
  int x;
  if (right) {
    graphics_draw_bitmap_in_rect(ctx, icon,
        GRect(rx - ICON_SZ, y, ICON_SZ, ICON_SZ));
    x = rx - ICON_SZ - DIGIT_GAP - text_w;
  } else {
    graphics_draw_bitmap_in_rect(ctx, icon, GRect(lx, y, ICON_SZ, ICON_SZ));
    x = lx + ICON_SZ + DIGIT_GAP;
  }
  if (neg) {
    // glyph sheets have no '-': draw a dash to match the white sprites
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(x, y + DIGIT_H / 2 - 1, DIGIT_W - 1, 2),
                       0, GCornerNone);
    x += DIGIT_W + DIGIT_GAP;
  }
  prv_draw_glyphs(ctx, buf, x, y);
  // degree symbol: small dot at the top-right of the number
  x += prv_glyphs_width(buf) + DIGIT_GAP;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x, y, DIGIT_GAP + 1, DIGIT_GAP + 1),
                     0, GCornerNone);
}

// a module the watch can't show (health on aplite) collapses like MOD_NONE
static bool prv_module_available(int mod) {
#if !defined(PBL_HEALTH)
  if (mod == MOD_STEPS || mod == MOD_DIST || mod == MOD_CAL || mod == MOD_HR) {
    return false;
  }
#endif
  return mod > MOD_NONE && mod < MOD_TYPE_COUNT;
}

static void prv_draw_module(GContext *ctx, GRect bounds, int mod, int y,
                            bool right) {
  int lx, rx;
  prv_row_edges(bounds, y, &lx, &rx);
  int icon = -1;
  char buf[12];
  const char *text = buf;
  switch (mod) {
    case MOD_DAY:
      text = s_day_buffer;
      break;
    case MOD_DATE:
      text = s_date_buffer;
      break;
    case MOD_BATTERY:
      if (s_battery_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_battery_bitmap,
            GRect(right ? rx - BATT_W : lx, y, BATT_W, BATT_H));
      }
      return;
    case MOD_BT:
      icon = s_bt_connected ? ICON_BT_ON : ICON_BT_OFF;
      strcpy(buf, s_bt_connected ? "ON" : "OFF");
      break;
    case MOD_WEATHER:
      prv_draw_weather(ctx, y, lx, rx, right);
      return;
    case MOD_YEAR: {
      time_t now = time(NULL);
      snprintf(buf, sizeof(buf), "%d", localtime(&now)->tm_year + 1900);
      break;
    }
#if defined(PBL_HEALTH)
    case MOD_STEPS:
      icon = ICON_SHOE;
      snprintf(buf, sizeof(buf), "%d", s_steps);
      break;
    case MOD_DIST:
      if (health_service_get_measurement_system_for_display(
              HealthMetricWalkedDistanceMeters) == MeasurementSystemImperial) {
        // tenths of a mile; overflows past 214 km, far beyond a day's walk
        int t = s_distance_m * 10000 / 1609344;
        icon = ICON_MI;
        snprintf(buf, sizeof(buf), "%d.%d", t / 10, t % 10);
      } else if (s_distance_m >= 1000) {
        icon = ICON_KM;
        snprintf(buf, sizeof(buf), "%d.%d", s_distance_m / 1000,
                 (s_distance_m % 1000) / 100);
      } else {
        icon = ICON_M;
        snprintf(buf, sizeof(buf), "%d", s_distance_m);
      }
      break;
    case MOD_CAL:
      icon = ICON_FLAME;
      snprintf(buf, sizeof(buf), "%d", s_kcal);
      break;
    case MOD_HR:
      if (!s_hrm) {
        return;  // no reading yet / no HRM: row stays reserved like weather
      }
      icon = ICON_HEART;
      snprintf(buf, sizeof(buf), "%d", s_hrm);
      break;
#endif
    default:
      return;
  }
  // icon sits on the outer edge: left of text on the left side, right of it
  // on the right side
  int w = prv_glyphs_width(text) + (icon >= 0 ? ICON_SZ + DIGIT_GAP : 0);
  int x = right ? rx - w : lx;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  if (icon >= 0 && !right) {
    graphics_draw_bitmap_in_rect(ctx, s_icon_bitmaps[icon],
        GRect(x, y, ICON_SZ, ICON_SZ));
    x += ICON_SZ + DIGIT_GAP;
  }
  prv_draw_glyphs(ctx, text, x, y);
  if (icon >= 0 && right) {
    graphics_draw_bitmap_in_rect(ctx, s_icon_bitmaps[icon],
        GRect(rx - ICON_SZ, y, ICON_SZ, ICON_SZ));
  }
}

static void prv_rows_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  for (int side = 0; side < 2; side++) {
    const uint8_t *mods = side ? s_right_mods : s_left_mods;
    int y = ROWS_TOP;
#if PBL_PLATFORM_GABBRO
    // taller screen: center this side's visible rows on the screen middle
    int visible = 0;
    for (int i = 0; i < SLOTS_PER_SIDE; i++) {
      if (prv_module_available(mods[i])) {
        visible++;
      }
    }
    if (visible > 0) {
      int total_h = visible * DIGIT_H + (visible - 1) * MODULE_GAP;
      y = (bounds.size.h - total_h) / 2.5;
    }
#endif
    for (int i = 0; i < SLOTS_PER_SIDE; i++) {
      if (!prv_module_available(mods[i])) {
        continue;  // unset lines collapse: no gap left behind
      }
      prv_draw_module(ctx, bounds, mods[i], y, side == 1);
      y += DIGIT_H + MODULE_GAP;
    }
  }
}

static void prv_bt_handler(bool connected) {
  s_bt_connected = connected;
  if (s_rows_layer) {
    layer_mark_dirty(s_rows_layer);
  }
}

#if defined(PBL_HEALTH)
static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
    s_distance_m = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
    s_kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
    layer_mark_dirty(s_rows_layer);
  }
  if (event == HealthEventHeartRateUpdate || event == HealthEventSignificantUpdate) {
    s_hrm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
    layer_mark_dirty(s_rows_layer);
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
    const char *v = t->value->cstring;
    if (strcmp(v, "anaglyph") == 0) {
      s_time_color = TIME_ANAGLYPH;
    } else if (strcmp(v, "colorful") == 0) {
      s_time_color = TIME_COLORFUL;
    } else if (strcmp(v, "anaglyph_flat") == 0) {
      s_time_color = TIME_ANAGLYPH_FLAT;
    } else {
      s_time_color = TIME_FLAT;
    }
    persist_write_int(PERSIST_ANAGLYPH, s_time_color);
    if (s_time_canvas) {
      layer_mark_dirty(s_time_canvas);
    }
  }
  t = dict_find(iter, MESSAGE_KEY_FLAT_COLOR);
  if (t) {
    s_flat_color = GColorFromHEX(t->value->int32);
    persist_write_int(PERSIST_FLAT_COLOR, s_flat_color.argb);
    if (s_time_canvas) {
      layer_mark_dirty(s_time_canvas);
    }
  }
  t = dict_find(iter, MESSAGE_KEY_FLICK_ANIMATION);
  if (t) {
    s_flick_anim = t->value->int32 == 1;
    persist_write_bool(PERSIST_FLICK_ANIM, s_flick_anim);
  }
  // Clay selects arrive as single-digit strings matching the MOD_* enum
  bool mods_changed = false;
  for (int i = 0; i < SLOTS_PER_SIDE; i++) {
    t = dict_find(iter, MESSAGE_KEY_LEFT_MODULE + i);
    if (t) {
      s_left_mods[i] = atoi(t->value->cstring);
      mods_changed = true;
    }
    t = dict_find(iter, MESSAGE_KEY_RIGHT_MODULE + i);
    if (t) {
      s_right_mods[i] = atoi(t->value->cstring);
      mods_changed = true;
    }
  }
  if (mods_changed) {
    persist_write_data(PERSIST_LEFT_MODS, s_left_mods, sizeof(s_left_mods));
    persist_write_data(PERSIST_RIGHT_MODS, s_right_mods, sizeof(s_right_mods));
    if (s_rows_layer) {
      layer_mark_dirty(s_rows_layer);
    }
  }
  Tuple *temp_t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMPERATURE);
  Tuple *code_t = dict_find(iter, MESSAGE_KEY_WEATHER_CODE);
  if (temp_t && code_t) {
    s_weather_temp = (int)temp_t->value->int32;
    s_weather_code = (int)code_t->value->int32;
    s_weather_valid = true;
    if (s_rows_layer) {
      layer_mark_dirty(s_rows_layer);
    }
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
#if PBL_PLATFORM_EMERY || PBL_PLATFORM_GABBRO
  s_time_font = fonts_get_system_font(FONT_KEY_LECO_60_NUMBERS_AM_PM);
  int time_height = 68;
#else
  s_time_font = fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS);
  int time_height = TIME_HEIGHT;
#endif
  int bottom_margin = PBL_IF_ROUND_ELSE(24, 4);
  GRect time_frame = GRect(0, bounds.size.h - time_height - bottom_margin,
                           bounds.size.w, time_height);

  s_time_canvas = layer_create(time_frame);
  layer_set_update_proc(s_time_canvas, prv_time_update_proc);
  layer_add_child(window_layer, s_time_canvas);

  s_battery_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
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

  // one transparent full-screen layer draws every configured row, both sides
  s_rows_layer = layer_create(bounds);
  layer_set_update_proc(s_rows_layer, prv_rows_update_proc);
  layer_add_child(window_layer, s_rows_layer);

  battery_state_service_subscribe(prv_battery_handler);
  prv_battery_handler(battery_state_service_peek());
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = prv_bt_handler,
  });
  s_bt_connected = connection_service_peek_pebble_app_connection();

#if defined(PBL_HEALTH)
  health_service_events_subscribe(prv_health_handler, NULL);
  s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  s_distance_m = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
  s_kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
  s_hrm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
#endif

  prv_update_time();
  prv_hover_animation();
}

static void prv_window_unload(Window *window) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  layer_destroy(s_rows_layer);
  for (int i = 0; i < ICON_COUNT; i++) {
    gbitmap_destroy(s_icon_bitmaps[i]);
  }
  gbitmap_destroy(s_icons_sheet);
  for (int d = 0; d < 10; d++) {
    gbitmap_destroy(s_digit_bitmaps[d]);
  }
  gbitmap_destroy(s_numbers_sheet);
  for (int l = 0; l < 26; l++) {
    gbitmap_destroy(s_letter_bitmaps[l]);
  }
  gbitmap_destroy(s_letters_sheet);
  gbitmap_destroy(s_battery_bitmap);
  gbitmap_destroy(s_battery_sheet);
  layer_destroy(s_time_canvas);
  bitmap_layer_destroy(s_background_layer);
  gbitmap_destroy(s_background_bitmap);
}

static void prv_init(void) {
  if (persist_exists(PERSIST_ANAGLYPH)) {
    s_time_color = persist_read_int(PERSIST_ANAGLYPH);
  }
  s_flat_color = GColorWhite;
  if (persist_exists(PERSIST_FLAT_COLOR)) {
    s_flat_color.argb = persist_read_int(PERSIST_FLAT_COLOR);
  }
  if (persist_exists(PERSIST_FLICK_ANIM)) {
    s_flick_anim = persist_read_bool(PERSIST_FLICK_ANIM);
  }
  if (persist_exists(PERSIST_LEFT_MODS)) {
    persist_read_data(PERSIST_LEFT_MODS, s_left_mods, sizeof(s_left_mods));
  }
  if (persist_exists(PERSIST_RIGHT_MODS)) {
    persist_read_data(PERSIST_RIGHT_MODS, s_right_mods, sizeof(s_right_mods));
  }

  app_message_register_inbox_received(prv_inbox_received);
  // inbox fits a full Clay save: 13 tuples
  app_message_open(256, 128);

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
