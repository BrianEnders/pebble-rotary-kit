#include <pebble.h>
#include "rotary_kit.h"


// #define CENTER_X 130
// #define CENTER_Y 130

// // Minimum drag radius to count as a wheel gesture (ignore center taps)
// #define MIN_RADIUS 40

// // How many degrees of rotation triggers one "click" (tune this)
// #define DEGREES_PER_CLICK 30

#define MINUTES_COLOR PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack)
#define HOURS_COLOR GColorWhite
#define HOURS_COLOR_INACTIVE PBL_IF_COLOR_ELSE(GColorBlack, GColorDarkGray)
#define BG_COLOR PBL_IF_COLOR_ELSE(GColorDukeBlue, GColorWhite)

#define MINUTES_RADIUS PBL_IF_ROUND_ELSE(60, 60)
#define HOURS_RADIUS 3
#define INSET PBL_IF_ROUND_ELSE(5, 3)

static int s_hours = 0, s_minutes = 0;

// static float s_last_angle = 0;
// static bool s_is_tracking = false;
// static float s_accumulated_degrees = 0;
// static int s_click_count = 0;        // clicks fired this gesture
// static float s_total_rotation = 0;   // total degrees rotated this gesture

// static int16_t s_last_angle_hd    = 0;
// static int32_t s_accumulated_hd    = 0;  // half-degrees accumulated
// static int32_t s_total_hd          = 0;  // total half-degrees rotated

static Layer *s_canvas;
static Window    *s_window;
static TextLayer *s_label;


// static const int8_t ATAN_TABLE[46] = {
//      0,  3,  5,  8, 10, 13, 15, 18, 20, 23,
//     25, 27, 30, 32, 35, 37, 39, 41, 44, 46,
//     48, 50, 52, 54, 56, 58, 60, 62, 64, 66,
//     67, 69, 71, 73, 74, 76, 77, 79, 80, 82,
//     83, 85, 86, 87, 89, 90
// };

// #define CLICK_THRESHOLD_HD  (DEGREES_PER_CLICK * 2)  // convert to half-degrees

// Integer radius squared — avoids sqrtf entirely
// Returns true if point is within radius r of center
// static bool in_radius(int16_t x, int16_t y, int16_t r) {
//     int32_t dx = x - CENTER_X;
//     int32_t dy = y - CENTER_Y;
//     return (dx*dx + dy*dy) >= (int32_t)r * r;
// }

// // Returns angle in half-degrees (0..720). Divide by 2 for integer degrees.
// // 0 = right, 180 = down, 360 = left, 540 = up (clockwise, screen coords)
// static int16_t coords_to_angle_hd(int16_t x, int16_t y) {
//     int16_t dx = x - CENTER_X;
//     int16_t dy = y - CENTER_Y;

//     if (dx == 0 && dy == 0) return 0;

//     int16_t adx = dx < 0 ? -dx : dx;
//     int16_t ady = dy < 0 ? -dy : dy;

//     int8_t oct_hd;
//     if (adx >= ady) {
//         oct_hd = ATAN_TABLE[(ady * 45) / adx];
//         if      (dx >= 0 && dy >= 0) return oct_hd;
//         else if (dx <  0 && dy >= 0) return 360 - oct_hd;
//         else if (dx <  0 && dy <  0) return 360 + oct_hd;
//         else                         return 720 - oct_hd;
//     } else {
//         oct_hd = ATAN_TABLE[(adx * 45) / ady];
//         if      (dx >= 0 && dy >= 0) return 180 - oct_hd;
//         else if (dx <  0 && dy >= 0) return 180 + oct_hd;
//         else if (dx <  0 && dy <  0) return 540 - oct_hd;
//         else                         return 540 + oct_hd;
//     }
// }

// // Shortest signed delta between two half-degree angles (-360..+360 hd)
// // Clamps single-frame jumps > 30 degrees (= 60 hd) as noise
// static int16_t angle_delta_hd(int16_t from, int16_t to) {
//     int16_t delta = to - from;
//     if (delta >  360) delta -= 720;
//     if (delta < -360) delta += 720;
//     if (delta >   60) delta = 0;   // > 30 deg jump = noise
//     if (delta <  -60) delta = 0;
//     return delta;
// }

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

// static void touch_handler(const TouchEvent *event, void *context) {
//     switch (event->type) {
//         case TouchEvent_Touchdown:
//             s_click_count      = 0;
//             s_total_hd         = 0;
//             s_accumulated_hd   = 0;
//             if (in_radius(event->x, event->y, MIN_RADIUS)) {
//                 s_last_angle_hd = coords_to_angle_hd(event->x, event->y);
//                 s_is_tracking   = true;
//                 APP_LOG(APP_LOG_LEVEL_DEBUG,
//                         "TOUCHDOWN: (%d,%d) angle=%d hd (%d deg)",
//                         (int)event->x, (int)event->y,
//                         (int)s_last_angle_hd, (int)s_last_angle_hd / 2);
//             } else {
//                 APP_LOG(APP_LOG_LEVEL_DEBUG,
//                         "TOUCHDOWN: (%d,%d) dead zone",
//                         (int)event->x, (int)event->y);
//             }
//             break;

//         case TouchEvent_PositionUpdate:
//             if (s_is_tracking && in_radius(event->x, event->y, MIN_RADIUS)) {
//                 int16_t cur_hd  = coords_to_angle_hd(event->x, event->y);
//                 int16_t delta   = angle_delta_hd(s_last_angle_hd, cur_hd);
//                 s_accumulated_hd += delta;
//                 s_total_hd       += delta < 0 ? -delta : delta;
//                 s_last_angle_hd   = cur_hd;

//                 while (s_accumulated_hd >=  CLICK_THRESHOLD_HD) {
//                     on_scroll_click(+1);
//                     s_accumulated_hd -= CLICK_THRESHOLD_HD;
//                 }
//                 while (s_accumulated_hd <= -CLICK_THRESHOLD_HD) {
//                     on_scroll_click(-1);
//                     s_accumulated_hd += CLICK_THRESHOLD_HD;
//                 }
//             }
//             break;

//         case TouchEvent_Liftoff:
//             APP_LOG(APP_LOG_LEVEL_INFO,
//                     "LIFTOFF: clicks=%d total=%ddeg leftover=%ddeg",
//                     s_click_count,
//                     (int)s_total_hd / 2,
//                     (int)s_accumulated_hd / 2);
//             s_is_tracking    = false;
//             s_accumulated_hd = 0;
//             break;
//     }
// }

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
