#include <pebble.h>

#define DEFAULT_THRESHOLD_MG 4000  // 4.0g default
#define DEFAULT_ROF_RPS 3          // 3 RPS default
#define STARTUP_WARMUP_SAMPLES 50

static Window *s_main_window;
static TextLayer *s_shots_layer;
static TextLayer *s_mag_info_layer;
static TextLayer *s_threshold_layer;
static GFont s_huge_font;

static int32_t s_shot_count = 0;
static int32_t s_mag_capacity = 10;
static int32_t s_threshold_mg = DEFAULT_THRESHOLD_MG;
static int32_t s_rof_rps = DEFAULT_ROF_RPS;
static int32_t s_refractory_samples = 33; // 100Hz / 3 RPS = 33 samples
static int32_t s_theme = 0;
static bool s_show_mag = true;
static int s_refractory_counter = STARTUP_WARMUP_SAMPLES;

static void update_refractory_samples(void) {
  if (s_rof_rps < 1) s_rof_rps = 1;
  if (s_rof_rps > 25) s_rof_rps = 25; // Nyquist limit for 100Hz sampling (50Hz max pulse, 25 RPS window)
  
  s_refractory_samples = 100 / s_rof_rps;
  if (s_refractory_samples < 2) s_refractory_samples = 2; // Min 20ms lockout
}

static GColor get_bg_color(void) {
  if (s_theme == 3) return GColorWhite;
  return GColorBlack;
}

static GColor get_text_color(void) {
  switch (s_theme) {
    case 1: return GColorCyan;       // Fluorescent
    case 2: return GColorGreen;      // Terminal
    case 3: return GColorBlack;      // Light mono
    case 4: return GColorRed;        // Dark red
    case 0:
    default: return GColorWhite;     // Dark mono
  }
}

static void apply_theme(void) {
  GColor bg = get_bg_color();
  GColor text = get_text_color();

  window_set_background_color(s_main_window, bg);

  text_layer_set_text_color(s_shots_layer, text);
  text_layer_set_text_color(s_mag_info_layer, text);
  text_layer_set_text_color(s_threshold_layer, text);
}

static void update_display(void) {
  static char shots_buf[16];
  snprintf(shots_buf, sizeof(shots_buf), "%ld", (long)s_shot_count);
  text_layer_set_text(s_shots_layer, shots_buf);

  if (s_show_mag) {
    static char mag_buf[48];
    long mags_fired = s_shot_count / s_mag_capacity;
    snprintf(mag_buf, sizeof(mag_buf), "mag cap: %ld  |  mags: %ld", (long)s_mag_capacity, mags_fired);
    text_layer_set_text(s_mag_info_layer, mag_buf);
    layer_set_hidden(text_layer_get_layer(s_mag_info_layer), false);
  } else {
    layer_set_hidden(text_layer_get_layer(s_mag_info_layer), true);
  }

  static char thresh_buf[32];
  snprintf(thresh_buf, sizeof(thresh_buf), "%ld.%01ld g  |  %ld RPS", 
           (long)(s_threshold_mg / 1000), (long)((s_threshold_mg % 1000) / 100), (long)s_rof_rps);
  text_layer_set_text(s_threshold_layer, thresh_buf);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *thresh_tuple = dict_find(iter, MESSAGE_KEY_THRESHOLD_MG);
  if (thresh_tuple) {
    int32_t val = thresh_tuple->value->int32;
    if (val >= 2000 && val <= 16000) {
      s_threshold_mg = val;
    }
  }

  Tuple *rof_tuple = dict_find(iter, MESSAGE_KEY_ROF_RPS);
  if (rof_tuple) {
    s_rof_rps = rof_tuple->value->int32;
    update_refractory_samples();
  }

  Tuple *theme_tuple = dict_find(iter, MESSAGE_KEY_THEME);
  if (theme_tuple) {
    s_theme = theme_tuple->value->int32;
  }

  Tuple *show_mag_tuple = dict_find(iter, MESSAGE_KEY_SHOW_MAG);
  if (show_mag_tuple) {
    s_show_mag = (show_mag_tuple->value->int32 != 0);
  }

  apply_theme();
  update_display();
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  int32_t threshold_sq = s_threshold_mg * s_threshold_mg;

  for (uint32_t i = 0; i < num_samples; i++) {
    if (s_refractory_counter > 0) {
      s_refractory_counter--;
      continue;
    }

    int32_t x = data[i].x;
    int32_t y = data[i].y;
    int32_t z = data[i].z;

    int32_t mag_sq = (x * x) + (y * y) + (z * z);

    if (mag_sq >= threshold_sq) {
      s_shot_count++;
      s_refractory_counter = s_refractory_samples;
      update_display();
      break;
    }
  }
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_shot_count = 0;
  update_display();
}

static void select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_threshold_mg <= 14000) s_threshold_mg += 2000;
  else s_threshold_mg = 2000;
  update_display();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_mag_capacity < 99) {
    s_mag_capacity++;
    update_display();
  }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_mag_capacity > 1) {
    s_mag_capacity--;
    update_display();
  }
}

static void down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_theme = (s_theme + 1) % 5;
  apply_theme();
}

static void config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click_handler, NULL);
  
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_click_handler, NULL);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_huge_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HUGE_NUMBERS_72));
  if (!s_huge_font) {
    s_huge_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  }

  s_shots_layer = text_layer_create(GRect(0, 5, bounds.size.w, 105));
  text_layer_set_background_color(s_shots_layer, GColorClear);
  text_layer_set_font(s_shots_layer, s_huge_font);
  text_layer_set_text_alignment(s_shots_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_shots_layer));

  s_mag_info_layer = text_layer_create(GRect(0, bounds.size.h - 50, bounds.size.w, 24));
  text_layer_set_background_color(s_mag_info_layer, GColorClear);
  text_layer_set_font(s_mag_info_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_mag_info_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_mag_info_layer));

  s_threshold_layer = text_layer_create(GRect(0, bounds.size.h - 25, bounds.size.w, 22));
  text_layer_set_background_color(s_threshold_layer, GColorClear);
  text_layer_set_font(s_threshold_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_threshold_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_threshold_layer));

  update_refractory_samples();
  apply_theme();
  update_display();
}

static void main_window_unload(Window *window) {
  if (s_huge_font) {
    fonts_unload_custom_font(s_huge_font);
  }
  text_layer_destroy(s_shots_layer);
  text_layer_destroy(s_mag_info_layer);
  text_layer_destroy(s_threshold_layer);
}

static void init(void) {
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(128, 128);

  window_stack_push(s_main_window, true);

  accel_data_service_subscribe(10, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
}

static void deinit(void) {
  accel_data_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
