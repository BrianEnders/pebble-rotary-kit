#include <pebble.h>
#include "rotary_kit.h"

#define MINUTES_COLOR PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack)
#define HOURS_COLOR GColorWhite
#define HOURS_COLOR_INACTIVE PBL_IF_COLOR_ELSE(GColorBlack, GColorDarkGray)
#define BG_COLOR PBL_IF_COLOR_ELSE(GColorDukeBlue, GColorWhite)

#define MINUTES_RADIUS PBL_IF_ROUND_ELSE(60, 60)
#define HOURS_RADIUS 3
#define INSET PBL_IF_ROUND_ELSE(5, 3)

static int s_hours = 0, s_minutes = 0;

static Layer *s_canvas;
static Window    *s_window;
static TextLayer *s_label;

static void on_wheel_click(int direction, int click_num, void *context) {
    // direction: +1 = clockwise / scroll down, -1 = CCW / scroll up
    text_layer_set_text(s_label, direction > 0 ? "v Down" : "^ Up");
    if(direction == 1){
      if(s_minutes < 60)s_minutes = s_minutes + 3;
      layer_mark_dirty(s_canvas);
    }
    if(direction == -1){
      if(s_minutes > 0)s_minutes = s_minutes - 3;
      layer_mark_dirty(s_canvas);
    }
    
}

static void on_wheel_liftoff(int total_clicks, int total_degrees, void *context) {
    // Optional: use this to commit a selection, log analytics, etc.
    APP_LOG(APP_LOG_LEVEL_INFO,
            "Gesture ended: %d clicks over %d degrees", total_clicks, total_degrees);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(s_label, "Select");
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(s_label, "Up");
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(s_label, "Down");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static int32_t get_angle_for_minute(int minute) {
  // Progress through 60 minutes, out of 360 degrees
  return (minute * 360) / 60;
}

static int32_t get_angle_for_hour(int hour) {
  // Progress through 12 hours, out of 360 degrees
  return (hour * 360) / 12;
}

static void layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // 12 hours only, with a minimum size
  int s_hours = 12;

  // Minutes are expanding circle arc
  int minute_angle = get_angle_for_minute(s_minutes);
  GRect frame = grect_inset(bounds, GEdgeInsets(4 * INSET));
  graphics_context_set_fill_color(ctx, MINUTES_COLOR);
  graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, 20, 0, DEG_TO_TRIGANGLE(minute_angle));

  // Adjust geometry variables for inner ring
  frame = grect_inset(frame, GEdgeInsets(3 * HOURS_RADIUS));

  // Hours are dots
  for(int i = 0; i < 12; i++) {
    int hour_angle = get_angle_for_hour(i);
    GPoint pos = gpoint_from_polar(frame, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(hour_angle));

    graphics_context_set_fill_color(ctx, i <= s_hours ? HOURS_COLOR : HOURS_COLOR_INACTIVE);
    graphics_fill_circle(ctx, pos, HOURS_RADIUS);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  s_label = text_layer_create(GRect(bounds.size.w/2-bounds.size.w/4, 72, bounds.size.w/2, 20));
  text_layer_set_text(s_label, "Turn the bezel!");
  text_layer_set_text_alignment(s_label, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_label));

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, layer_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
    text_layer_destroy(s_label);
}

static void window_appear(Window *window) {
    // Build a config — start from defaults, override what you need.
    RotaryConfig cfg     = rotary_kit_default_config();
    cfg.on_click         = on_wheel_click;
    cfg.on_liftoff       = on_wheel_liftoff;
    cfg.degrees_per_click = 30;   // tighter detents (optional tweak)
    // cfg.vibrate_on_click = false; // silence haptics if desired
 
    rotary_kit_init(&cfg);
}

static void window_disappear(Window *window) {
  rotary_kit_deinit();
}

static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, BG_COLOR);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
      .load      = window_load,
      .unload    = window_unload,
      .appear    = window_appear,
      .disappear = window_disappear,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
  layer_destroy(s_canvas);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
