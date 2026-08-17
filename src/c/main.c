#include <pebble.h>

static Window *s_window;
static TextLayer *s_shots_lbl, *s_mag_lbl, *s_thresh_lbl, *s_debug_lbl;
static GFont s_font;

static int32_t s_shots, s_mag_cap = 10, s_sens = 50, s_rof_rps = 3, s_refractory_samples = 33, s_theme;
static bool s_show_mag = true, s_debug_mode = false;
static int s_refractory = 50;
static AccelData s_prev[2];
static bool s_has_prev;

static int32_t s_peak_x, s_peak_y, s_peak_z, s_window_samples;
static int32_t s_disp_x, s_disp_y, s_disp_z;

static void update_refractory(void) {
  s_rof_rps = s_rof_rps < 1 ? 1 : (s_rof_rps > 25 ? 25 : s_rof_rps);
  s_refractory_samples = 100 / s_rof_rps;
  if (s_refractory_samples < 2) s_refractory_samples = 2;
}

static GColor get_color(bool bg) {
  static const GColor colors[] = { GColorWhite, GColorCyan, GColorGreen, GColorBlack, GColorRed };
  return bg ? (s_theme == 3 ? GColorWhite : GColorBlack) : colors[s_theme % 5];
}

static void apply_theme(void) {
  window_set_background_color(s_window, get_color(true));
  GColor fg = get_color(false);
  text_layer_set_text_color(s_shots_lbl, fg);
  text_layer_set_text_color(s_mag_lbl, fg);
  text_layer_set_text_color(s_thresh_lbl, fg);
}

static inline int64_t get_tkeo_thresh(void) {
  int32_t s = s_sens < 0 ? 0 : (s_sens > 100 ? 100 : s_sens);
  return 100000LL + (int64_t)(100 - s) * (100 - s) * 10000LL;
}

static void update_display(void) {
  static char b_shots[16], b_mag[48], b_thresh[32], b_debug[48];
  snprintf(b_shots, sizeof(b_shots), "%ld", (long)s_shots);
  text_layer_set_text(s_shots_lbl, b_shots);

  snprintf(b_mag, sizeof(b_mag), "mag cap: %ld  |  mags: %ld", (long)s_mag_cap, (long)(s_shots / s_mag_cap));
  text_layer_set_text(s_mag_lbl, b_mag);
  layer_set_hidden(text_layer_get_layer(s_mag_lbl), !s_show_mag);

  snprintf(b_thresh, sizeof(b_thresh), "Sens: %ld  |  %ld RPS", (long)s_sens, (long)s_rof_rps);
  text_layer_set_text(s_thresh_lbl, b_thresh);

  if (s_debug_mode) {
    snprintf(b_debug, sizeof(b_debug), "X:%ld Y:%ld Z:%ld", (long)s_disp_x, (long)s_disp_y, (long)s_disp_z);
    text_layer_set_text(s_debug_lbl, b_debug);
    layer_set_hidden(text_layer_get_layer(s_debug_lbl), false);
  } else {
    layer_set_hidden(text_layer_get_layer(s_debug_lbl), true);
  }
}

static void debug_layer_update_proc(Layer *layer, GContext *ctx) {
  if (!s_debug_mode) return;

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  #if defined(PBL_COLOR)
  graphics_context_set_text_color(ctx, GColorRed);
  char bx[16]; snprintf(bx, sizeof(bx), "X:%ld", (long)s_disp_x);
  graphics_draw_text(ctx, bx, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(5, 0, 45, 16), GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  graphics_context_set_text_color(ctx, GColorGreen);
  char by[16]; snprintf(by, sizeof(by), "Y:%ld", (long)s_disp_y);
  graphics_draw_text(ctx, by, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(50, 0, 45, 16), GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  graphics_context_set_text_color(ctx, GColorBlue);
  char bz[16]; snprintf(bz, sizeof(bz), "Z:%ld", (long)s_disp_z);
  graphics_draw_text(ctx, bz, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(95, 0, 45, 16), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  #else
  char buf[48];
  snprintf(buf, sizeof(buf), "X:%ld Y:%ld Z:%ld", (long)s_disp_x, (long)s_disp_y, (long)s_disp_z);
  graphics_context_set_text_color(ctx, get_color(false));
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     bounds, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  #endif
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_THRESHOLD_MG))) {
    int32_t val = t->value->int32;
    s_sens = (val >= 0 && val <= 100) ? val : 100 - ((val - 2000) * 100 / 14000);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ROF_RPS))) {
    s_rof_rps = t->value->int32;
    update_refractory();
  }
  if ((t = dict_find(iter, MESSAGE_KEY_THEME))) s_theme = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_MAG))) s_show_mag = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_DEBUG_MODE))) s_debug_mode = t->value->int32 != 0;

  apply_theme();
  update_display();
}

static inline int32_t tkeo_axis(int16_t prev, int16_t curr, int16_t next) {
  return ((int32_t)curr * curr) - ((int32_t)prev * next);
}

static void accel_handler(AccelData *data, uint32_t num_samples) {
  int64_t tkeo_thresh = get_tkeo_thresh();

  for (uint32_t i = 0; i < num_samples; i++) {
    if (s_refractory > 0) {
      s_refractory--;
      s_prev[0] = s_prev[1];
      s_prev[1] = data[i];
      continue;
    }

    if (!s_has_prev) {
      s_prev[0] = data[i];
      s_has_prev = true;
      continue;
    }

    int32_t tx = tkeo_axis(s_prev[0].x, s_prev[1].x, data[i].x);
    int32_t ty = tkeo_axis(s_prev[0].y, s_prev[1].y, data[i].y);
    int32_t tz = tkeo_axis(s_prev[0].z, s_prev[1].z, data[i].z);

    if (tx > s_peak_x) s_peak_x = tx;
    if (ty > s_peak_y) s_peak_y = ty;
    if (tz > s_peak_z) s_peak_z = tz;

    s_window_samples++;
    if (s_window_samples >= s_refractory_samples) {
      s_disp_x = s_peak_x;
      s_disp_y = s_peak_y;
      s_disp_z = s_peak_z;
      s_peak_x = s_peak_y = s_peak_z = 0;
      s_window_samples = 0;
      if (s_debug_mode) layer_mark_dirty(text_layer_get_layer(s_debug_lbl));
    }

    int64_t total_energy = (int64_t)tx + ty + tz;

    s_prev[0] = s_prev[1];
    s_prev[1] = data[i];

    if (total_energy >= tkeo_thresh) {
      s_shots++;
      s_refractory = s_refractory_samples;
      update_display();
      break;
    }
  }
}

static void select_click(ClickRecognizerRef r, void *c) { s_shots = 0; update_display(); }
static void select_long_click(ClickRecognizerRef r, void *c) { s_theme = (s_theme + 1) % 5; apply_theme(); }
static void up_click(ClickRecognizerRef r, void *c) { if (s_sens < 100) { s_sens = (s_sens + 5 > 100) ? 100 : s_sens + 5; update_display(); } }
static void up_long_click(ClickRecognizerRef r, void *c) { if (s_mag_cap < 99) { s_mag_cap++; update_display(); } }
static void down_click(ClickRecognizerRef r, void *c) { if (s_sens > 0) { s_sens = (s_sens - 5 < 0) ? 0 : s_sens - 5; update_display(); } }
static void down_long_click(ClickRecognizerRef r, void *c) { if (s_mag_cap > 1) { s_mag_cap--; update_display(); } }

static void config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_click, NULL);
}

static TextLayer* create_label(GRect frame, GFont font) {
  Layer *root = window_get_root_layer(s_window);
  TextLayer *lbl = text_layer_create(frame);
  text_layer_set_background_color(lbl, GColorClear);
  text_layer_set_font(lbl, font);
  text_layer_set_text_alignment(lbl, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(lbl));
  return lbl;
}

static void window_load(Window *window) {
  GRect b = layer_get_bounds(window_get_root_layer(window));
  s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HUGE_NUMBERS_72));
  if (!s_font) s_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);

  s_shots_lbl  = create_label(GRect(0, 5, b.size.w, 95), s_font);
  s_mag_lbl    = create_label(GRect(0, b.size.h - 55, b.size.w, 20), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  s_thresh_lbl = create_label(GRect(0, b.size.h - 35, b.size.w, 20), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_debug_lbl  = create_label(GRect(0, b.size.h - 16, b.size.w, 16), fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  layer_set_update_proc(text_layer_get_layer(s_debug_lbl), debug_layer_update_proc);

  update_refractory();
  apply_theme();
  update_display();
}

static void window_unload(Window *window) {
  if (s_font) fonts_unload_custom_font(s_font);
  text_layer_destroy(s_shots_lbl);
  text_layer_destroy(s_mag_lbl);
  text_layer_destroy(s_thresh_lbl);
  text_layer_destroy(s_debug_lbl);
}

static void init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  app_message_register_inbox_received(inbox_received);
  app_message_open(128, 128);
  window_stack_push(s_window, true);
  accel_data_service_subscribe(10, accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
}

static void deinit(void) {
  accel_data_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
