#include <pebble.h>

#define RING_SIZE 256
#define MIN(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a < _b ? _a : _b; })
#define MAX(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
       _a > _b ? _a : _b; })
#define CLAMP(x, low, high) \
    ({ __typeof__ (x) _x = (x); \
       __typeof__ (low) _low = (low); \
       __typeof__ (high) _high = (high); \
       _x < _low ? _low : (_x > _high ? _high : _x); })

enum {
  PKEY_SHOTS = 1, PKEY_MAG_CAP, PKEY_THRESH, PKEY_ROF_RPS,
  PKEY_THEME, PKEY_SHOW_MAG, PKEY_DEBUG_MODE
};

static Window *s_window;
static TextLayer *s_shots_lbl, *s_mag_lbl, *s_thresh_lbl;
static Layer *s_scope;
static GFont s_font;

static int32_t s_shots, s_mag_cap = 10, s_thresh = 50, s_rof_rps = 3, s_refr_samples = 33, s_theme;
static bool s_show_mag = true, s_debug_mode = false;
static int s_refractory = 50;
static int16_t s_hist[5][3];
static int s_hist_count = 0;
static int32_t s_cand_run = 0;

static int32_t s_ring[RING_SIZE];
static int32_t s_ri;
static int32_t s_hold[RING_SIZE];
static bool s_held;
static int32_t s_win_peak, s_win_count;

static void clear_scope(void) {
  s_held = false;
  memset(s_ring, 0, sizeof(s_ring));
  s_ri = 0;
  layer_mark_dirty(s_scope);
}


/* ---------------- persistence ---------------- */
static time_t s_last_persist = 0;
static int32_t s_last_saved_shots = 0;

static void save_state(void) {
  persist_write_int(PKEY_SHOTS, s_shots);
  s_last_saved_shots = s_shots;
  persist_write_int(PKEY_MAG_CAP, s_mag_cap);
  persist_write_int(PKEY_THRESH, s_thresh);
  persist_write_int(PKEY_ROF_RPS, s_rof_rps);
  persist_write_int(PKEY_THEME, s_theme);
  persist_write_int(PKEY_SHOW_MAG, s_show_mag);
  persist_write_int(PKEY_DEBUG_MODE, s_debug_mode);
}

/* Persist the shot count once at least 10s have passed AND shots happened
 * since the last save. deinit() saves unconditionally on exit. */
static void maybe_persist_shots(void) {
  time_t now = time(NULL);
  if (now - s_last_persist >= 10 && s_shots != s_last_saved_shots) {
    persist_write_int(PKEY_SHOTS, s_shots);
    s_last_persist = now;
    s_last_saved_shots = s_shots;
  }
}

static void load_state(void) {
  if (persist_exists(PKEY_SHOTS)) { s_shots = persist_read_int(PKEY_SHOTS); s_last_saved_shots = s_shots; }
  if (persist_exists(PKEY_MAG_CAP)) s_mag_cap = persist_read_int(PKEY_MAG_CAP);
  if (persist_exists(PKEY_THRESH)) s_thresh = persist_read_int(PKEY_THRESH);
  if (persist_exists(PKEY_ROF_RPS)) s_rof_rps = persist_read_int(PKEY_ROF_RPS);
  if (persist_exists(PKEY_THEME)) s_theme = persist_read_int(PKEY_THEME);
  if (persist_exists(PKEY_SHOW_MAG)) s_show_mag = persist_read_int(PKEY_SHOW_MAG);
  if (persist_exists(PKEY_DEBUG_MODE)) s_debug_mode = persist_read_int(PKEY_DEBUG_MODE);
}

/* ---------------- helpers ---------------- */
static void update_refractory(void) {
  s_rof_rps = CLAMP(s_rof_rps, 1, 25);
  s_refr_samples = MAX(100 / s_rof_rps, 2);
  s_win_count = 0; s_win_peak = 0;
}

static inline int32_t mexican_hat_axis(int axis) {
    // Kernel: [-1, -2, 6, -2, -1]
    int32_t val = 
        (-1 * s_hist[0][axis]) + 
        (-2 * s_hist[1][axis]) + 
        ( 6 * s_hist[2][axis]) + 
        (-2 * s_hist[3][axis]) + 
        (-1 * s_hist[4][axis]);
    return (val * val) >> 4; 
}

static GColor get_bg_color(void) { return s_theme == 3 ? GColorWhite : GColorBlack; }
static GColor get_fg_color(void) {
  static const GColor colors[] = { GColorWhite, GColorCyan, GColorGreen, GColorBlack, GColorRed };
  return colors[s_theme % 5];
}

/* Threshold rises with the setting; a higher threshold means less detection. */
static inline int32_t tkeo_thresh_of(int32_t s) {
  return 100000 + s * s * 10000;
}

#define SCOPE_MAX 120000000
#define SCOPE_MIN 10000

/* Wrist-flick false-positive suppression. A genuine tap is a single sharp,
 * high-amplitude excursion; a wrist-flick (fast rotation ramp settling into
 * a mechanical rattle) is a train of smaller, sustained oscillations that
 * can still poke the mexican_hat_axis energy above threshold on individual
 * cycles. Two cheap gates applied only at the moment of a threshold
 * crossing: the excursion must have real peak-to-peak amplitude on at least
 * one axis, and energy must not have been sitting above a low candidate
 * floor for very long beforehand (a real tap clears in 1-2 samples; a
 * rattle keeps re-crossing for many). PTP_MIN and CAND_WIDTH_CAP below are
 * initial estimates from synthetic testing -- recalibrate against real
 * recorded wrist-flick and shot traces (s_debug_mode scope capture) before
 * trusting them at the extremes. */
#define PTP_MIN 2000
#define CAND_WIDTH_CAP 3

/* Fast integer log2 approximation via Cortex-M hardware CLZ instruction.
 * int32_t is sufficient: tx/ty/tz sums stay well under INT32_MAX for real accel input. */
static inline int32_t ilog2_fast(uint32_t v) {
  if (v == 0) return 0;
  uint32_t msb = 31 - __builtin_clz(v);
  uint32_t frac = (msb >= 4) ? (v >> (msb - 4)) & 0x0F : (v << (4 - msb)) & 0x0F;
  return (int32_t)((msb << 4) | frac);
}

static inline int32_t tkeo_axis(int16_t p, int16_t c, int16_t n) {
  return ((int32_t)c * c) - ((int32_t)p * n);
}

/* ---------------- theme ---------------- */
static void apply_theme(void) {
  window_set_background_color(s_window, get_bg_color());
  GColor fg = get_fg_color();
  text_layer_set_text_color(s_shots_lbl, fg);
  text_layer_set_text_color(s_mag_lbl, fg);
  text_layer_set_text_color(s_thresh_lbl, fg);
  layer_mark_dirty(s_scope);
}

/* ---------------- scope drawing ---------------- */
static void scope_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int yTop = 26, yBot = b.size.h - 6;
  int diff = yBot - yTop;
  int32_t T = tkeo_thresh_of(s_thresh);

  /* Integer log axis setup (fixed, threshold-independent). */
  int32_t lmin = ilog2_fast(SCOPE_MIN), lmax = ilog2_fast(SCOPE_MAX);
  int32_t lspan = lmax - lmin;
  int32_t lT = ilog2_fast(MAX(T, 1));
  int32_t fT_num = CLAMP(lT - lmin, 0, lspan);
  int lineY = yBot - (fT_num * diff) / lspan;

  graphics_context_set_fill_color(ctx, get_bg_color());
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (!s_debug_mode) return;

  int32_t *dta = s_held ? s_hold : s_ring;
  int start = s_held ? 0 : s_ri;

  GPoint prev = { 0, 0 };
  bool have_prev = false;
  int w = b.size.w - 12;
  for (int i = 0; i < RING_SIZE; i++) {
    // equivalent to (but faster than) modulo, since the array length is a power of 2
    int32_t e = dta[(start + i) & (RING_SIZE - 1)]; 
    int32_t le = (e > 0) ? ilog2_fast((uint32_t)e) : lmin;
    le = CLAMP(le, lmin, lmax);
    int32_t off = le - lmin;
    int y = CLAMP(yBot - (off * diff) / lspan, yTop, yBot);
    int x = 6 + (i * w) / (RING_SIZE - 1);
    if (have_prev) {
      graphics_context_set_stroke_color(ctx, get_fg_color());
      graphics_draw_line(ctx, prev, (GPoint){ x, y });
    }
    prev.x = x; prev.y = y;
    have_prev = true;
  }

  /* threshold line (single, thin pixel) */
  graphics_context_set_stroke_color(ctx, get_fg_color());
  graphics_draw_line(ctx, (GPoint){ 4, lineY }, (GPoint){ b.size.w - 4, lineY });
}

/* ---------------- display ---------------- */
static void update_display(void) {
  static char b_shots[16], b_mag[48], b_thresh[32];
  snprintf(b_shots, sizeof(b_shots), "%ld", (long)s_shots);
  text_layer_set_text(s_shots_lbl, b_shots);

  snprintf(b_mag, sizeof(b_mag), "mag cap: %d | mags: %d", (int)s_mag_cap, (int)(s_shots / s_mag_cap));
  text_layer_set_text(s_mag_lbl, b_mag);
  layer_set_hidden(text_layer_get_layer(s_mag_lbl), !s_show_mag);

  snprintf(b_thresh, sizeof(b_thresh), "Thr: %d | %d RPS", (int)s_thresh, (int)s_rof_rps);
  text_layer_set_text(s_thresh_lbl, b_thresh);

  if (s_debug_mode) layer_mark_dirty(s_scope);
}

/* ---------------- inbox ---------------- */
/* Send the current watch state back to JS so the config window can
 * populate its fields instead of resetting to defaults. */
static void send_state(void) {
  DictionaryIterator *out;
  AppMessageResult r = app_message_outbox_begin(&out);
  if (r != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_THRESHOLD, s_thresh);
  dict_write_int32(out, MESSAGE_KEY_THEME, s_theme);
  dict_write_int32(out, MESSAGE_KEY_SHOW_MAG, s_show_mag ? 1 : 0);
  dict_write_int32(out, MESSAGE_KEY_ROF_RPS, s_rof_rps);
  dict_write_int32(out, MESSAGE_KEY_DEBUG_MODE, s_debug_mode ? 1 : 0);
  dict_write_int32(out, MESSAGE_KEY_MAG_CAP, s_mag_cap);
  app_message_outbox_send();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_THRESHOLD))) {
    int32_t v = t->value->int32;
    s_thresh = (v >= 0 && v <= 100) ? v : 100 - ((v - 2000) * 100 / 14000);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ROF_RPS))) { s_rof_rps = t->value->int32; update_refractory(); }
  if ((t = dict_find(iter, MESSAGE_KEY_THEME))) { s_theme = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_MAG))) s_show_mag = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_MAG_CAP))) { s_mag_cap = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_DEBUG_MODE))) {
    s_debug_mode = t->value->int32 != 0;
    s_held = false;
  }
  if (dict_find(iter, MESSAGE_KEY_GET_STATE)) { send_state(); return; }
  apply_theme();
  update_display();
  save_state();
}

/* ---------------- acceleration ---------------- */
// uses Taeger-Kaiser Energy Operator for peak detection
static void accel_handler(AccelData *data, uint32_t num_samples) {
  int32_t T = tkeo_thresh_of(s_thresh);

  for (uint32_t i = 0; i < num_samples; i++) {
    memmove(&s_hist[0], &s_hist[1], sizeof(int16_t) * 3 * 4);
    
    // 2. Insert new sample at the end
    s_hist[4][0] = data[i].x;
    s_hist[4][1] = data[i].y;
    s_hist[4][2] = data[i].z;
    
    if (s_hist_count < 5) {
        s_hist_count++;
        continue; // Wait until we have a full buffer
    }

    // Refractory skip (simplified for the new buffer structure)
    if (s_refractory > 0) {
        s_refractory--;
        continue;
    }

    // 3. Apply the filter
    int32_t tx = mexican_hat_axis(0);
    int32_t ty = mexican_hat_axis(1);
    int32_t tz = mexican_hat_axis(2);
    
    int32_t e = tx + ty + tz;

    /* Only scope_update() reads s_ring/s_hold, and it no-ops when !s_debug_mode, so skip bookkeeping too. */
    if (s_debug_mode) {
      s_ring[s_ri] = e;
      s_ri = (s_ri + 1) & (RING_SIZE - 1);
      s_win_peak = MAX(s_win_peak, e);
      s_win_count++;
      if (s_win_count >= s_refr_samples) {
        if (s_win_peak > (T * 3) / 5) {
          int src = s_ri;
          int tail = RING_SIZE - src;
          memcpy(s_hold, &s_ring[src], tail * sizeof(int32_t));
          memcpy(&s_hold[tail], s_ring, src * sizeof(int32_t));
          s_held = true;
          layer_mark_dirty(s_scope);
        }
        s_win_peak = 0; s_win_count = 0;
      }
    }

    /* Peak-to-peak over the same 5-sample window mexican_hat_axis already
     * uses -- no extra buffer needed. Take the largest single-axis swing. */
    int32_t ptp_max = 0;
    for (int a = 0; a < 3; a++) {
      int16_t lo = s_hist[0][a], hi = s_hist[0][a];
      for (int k = 1; k < 5; k++) {
        if (s_hist[k][a] < lo) lo = s_hist[k][a];
        if (s_hist[k][a] > hi) hi = s_hist[k][a];
      }
      int32_t ptp = (int32_t)hi - (int32_t)lo;
      if (ptp > ptp_max) ptp_max = ptp;
    }

    /* Candidate-duration tracking: how many consecutive samples has energy
     * stayed above a low floor (T/8)? Resets whenever it drops back down. */
    if (e >= (T >> 3)) { s_cand_run++; } else { s_cand_run = 0; }

    if (e >= T && ptp_max >= PTP_MIN && s_cand_run <= CAND_WIDTH_CAP) {
      s_shots++;
      maybe_persist_shots();
      s_refractory = s_refr_samples;
      s_cand_run = 0;
      update_display();
      break;
    }
  }
}

/* ---------------- buttons ---------------- */
static void select_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  s_shots = 0; persist_write_int(PKEY_SHOTS, 0); clear_scope(); update_display();
}
static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  s_theme = (s_theme + 1) % 5; apply_theme(); save_state();
}
static void up_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (s_thresh < 100) { s_thresh = MIN(s_thresh + 5, 100); update_display(); save_state(); }
}
static void up_long_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (s_mag_cap < 99) { s_mag_cap++; update_display(); save_state(); }
}
static void down_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (s_thresh > 0) { s_thresh = MAX(s_thresh - 5, 0); update_display(); save_state(); }
}
static void down_long_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (s_mag_cap > 1) { s_mag_cap--; update_display(); save_state(); }
}

static void config_provider(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_click, NULL);
}

/* ---------------- UI ---------------- */
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
  (void)window;
  GRect b = layer_get_bounds(window_get_root_layer(s_window));
  s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_HUGE_NUMBERS_72));
  if (!s_font) s_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);

  s_shots_lbl  = create_label(GRect(0, 2, b.size.w, 90), s_font);
  s_mag_lbl    = create_label(GRect(0, b.size.h - 56, b.size.w, 20), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  s_thresh_lbl = create_label(GRect(0, b.size.h - 36, b.size.w, 20), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));

  s_scope = layer_create(GRect(0, 92, b.size.w, b.size.h - 92 - 56));
  layer_set_update_proc(s_scope, scope_update);
  layer_add_child(window_get_root_layer(s_window), s_scope);

  update_refractory();
  apply_theme();
  update_display();
}

static void window_unload(Window *window) {
  (void)window;
  if (s_font) fonts_unload_custom_font(s_font);
  text_layer_destroy(s_shots_lbl);
  text_layer_destroy(s_mag_lbl);
  text_layer_destroy(s_thresh_lbl);
  layer_destroy(s_scope);
}

static void init(void) {
  load_state();
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
  save_state();
  accel_data_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
