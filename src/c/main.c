#include <pebble.h>

#define CENTER_X 130
#define CENTER_Y 130

// Minimum drag radius to count as a wheel gesture (ignore center taps)
#define MIN_RADIUS 40

// How many degrees of rotation triggers one "click" (tune this)
#define DEGREES_PER_CLICK 30

static float s_last_angle = 0;
static bool s_is_tracking = false;
static float s_accumulated_degrees = 0;
static int s_click_count = 0;        // clicks fired this gesture
static float s_total_rotation = 0;   // total degrees rotated this gesture

static int16_t s_last_angle_hd    = 0;
static int32_t s_accumulated_hd    = 0;  // half-degrees accumulated
static int32_t s_total_hd          = 0;  // total half-degrees rotated

static Window *s_main_window;
static TextLayer *s_text_layer;

static const int8_t ATAN_TABLE[46] = {
     0,  3,  5,  8, 10, 13, 15, 18, 20, 23,
    25, 27, 30, 32, 35, 37, 39, 41, 44, 46,
    48, 50, 52, 54, 56, 58, 60, 62, 64, 66,
    67, 69, 71, 73, 74, 76, 77, 79, 80, 82,
    83, 85, 86, 87, 89, 90
};

#define CLICK_THRESHOLD_HD  (DEGREES_PER_CLICK * 2)  // convert to half-degrees

// Integer radius squared — avoids sqrtf entirely
// Returns true if point is within radius r of center
static bool in_radius(int16_t x, int16_t y, int16_t r) {
    int32_t dx = x - CENTER_X;
    int32_t dy = y - CENTER_Y;
    return (dx*dx + dy*dy) >= (int32_t)r * r;
}

// Returns angle in half-degrees (0..720). Divide by 2 for integer degrees.
// 0 = right, 180 = down, 360 = left, 540 = up (clockwise, screen coords)
static int16_t coords_to_angle_hd(int16_t x, int16_t y) {
    int16_t dx = x - CENTER_X;
    int16_t dy = y - CENTER_Y;

    if (dx == 0 && dy == 0) return 0;

    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;

    int8_t oct_hd;
    if (adx >= ady) {
        oct_hd = ATAN_TABLE[(ady * 45) / adx];
        if      (dx >= 0 && dy >= 0) return oct_hd;
        else if (dx <  0 && dy >= 0) return 360 - oct_hd;
        else if (dx <  0 && dy <  0) return 360 + oct_hd;
        else                         return 720 - oct_hd;
    } else {
        oct_hd = ATAN_TABLE[(adx * 45) / ady];
        if      (dx >= 0 && dy >= 0) return 180 - oct_hd;
        else if (dx <  0 && dy >= 0) return 180 + oct_hd;
        else if (dx <  0 && dy <  0) return 540 - oct_hd;
        else                         return 540 + oct_hd;
    }
}

// Shortest signed delta between two half-degree angles (-360..+360 hd)
// Clamps single-frame jumps > 30 degrees (= 60 hd) as noise
static int16_t angle_delta_hd(int16_t from, int16_t to) {
    int16_t delta = to - from;
    if (delta >  360) delta -= 720;
    if (delta < -360) delta += 720;
    if (delta >   60) delta = 0;   // > 30 deg jump = noise
    if (delta <  -60) delta = 0;
    return delta;
}

// SCROLL HANDLER — wire this to your menu/list logic
static void on_scroll_click(int direction) {
    s_click_count++;
    vibes_short_pulse();
    if(direction == 1){
      text_layer_set_text(s_text_layer, "DOWN");
    }
     if(direction == -1){
      text_layer_set_text(s_text_layer, "UP");
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG,
            "CLICK #%d: dir=%s accum=%d hd total=%d hd",
            s_click_count,
            direction > 0 ? "CW" : "CCW",
            (int)s_accumulated_hd,
            (int)s_total_hd);
}

static void touch_handler(const TouchEvent *event, void *context) {
    switch (event->type) {
        case TouchEvent_Touchdown:
            s_click_count      = 0;
            s_total_hd         = 0;
            s_accumulated_hd   = 0;
            if (in_radius(event->x, event->y, MIN_RADIUS)) {
                s_last_angle_hd = coords_to_angle_hd(event->x, event->y);
                s_is_tracking   = true;
                APP_LOG(APP_LOG_LEVEL_DEBUG,
                        "TOUCHDOWN: (%d,%d) angle=%d hd (%d deg)",
                        (int)event->x, (int)event->y,
                        (int)s_last_angle_hd, (int)s_last_angle_hd / 2);
            } else {
                APP_LOG(APP_LOG_LEVEL_DEBUG,
                        "TOUCHDOWN: (%d,%d) dead zone",
                        (int)event->x, (int)event->y);
            }
            break;

        case TouchEvent_PositionUpdate:
            if (s_is_tracking && in_radius(event->x, event->y, MIN_RADIUS)) {
                int16_t cur_hd  = coords_to_angle_hd(event->x, event->y);
                int16_t delta   = angle_delta_hd(s_last_angle_hd, cur_hd);
                s_accumulated_hd += delta;
                s_total_hd       += delta < 0 ? -delta : delta;
                s_last_angle_hd   = cur_hd;

                while (s_accumulated_hd >=  CLICK_THRESHOLD_HD) {
                    on_scroll_click(+1);
                    s_accumulated_hd -= CLICK_THRESHOLD_HD;
                }
                while (s_accumulated_hd <= -CLICK_THRESHOLD_HD) {
                    on_scroll_click(-1);
                    s_accumulated_hd += CLICK_THRESHOLD_HD;
                }
            }
            break;

        case TouchEvent_Liftoff:
            APP_LOG(APP_LOG_LEVEL_INFO,
                    "LIFTOFF: clicks=%d total=%ddeg leftover=%ddeg",
                    s_click_count,
                    (int)s_total_hd / 2,
                    (int)s_accumulated_hd / 2);
            s_is_tracking    = false;
            s_accumulated_hd = 0;
            break;
    }
}
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(s_text_layer, "Select");
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(s_text_layer, "Up");
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(s_text_layer, "Down");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_text_layer = text_layer_create(GRect(0, 72, bounds.size.w, 20));
  text_layer_set_text(s_text_layer, "Press a button");
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_text_layer);
}

static void main_window_appear(Window *window) {
  if (!touch_service_is_enabled()) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "TOUCH: not available");
    return;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "TOUCH: subscribed");
  touch_service_subscribe(touch_handler, NULL);
}

static void main_window_disappear(Window *window) {
  APP_LOG(APP_LOG_LEVEL_INFO, "TOUCH: unsubscribed");
  touch_service_unsubscribe();
}

static void init(void) {
 s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
    .appear = main_window_appear,        // ← added
    .disappear = main_window_disappear,  // ← added
  });
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
