// rotary_kit.c — RotaryKit implementation
// See rotary_kit.h for documentation.

#include "rotary_kit.h"

// ---------------------------------------------------------------------------
// Internal state (all private to this translation unit)
// ---------------------------------------------------------------------------

// Lookup table: atan in half-degrees for the first octant.
// Index i maps (opposite/adjacent * 45) → half-degree angle [0..90].
static const int8_t s_atan_table[46] = {
     0,  3,  5,  8, 10, 13, 15, 18, 20, 23,
    25, 27, 30, 32, 35, 37, 39, 41, 44, 46,
    48, 50, 52, 54, 56, 58, 60, 62, 64, 66,
    67, 69, 71, 73, 74, 76, 77, 79, 80, 82,
    83, 85, 86, 87, 89, 90
};

static RotaryConfig s_cfg;
static bool         s_active = false;

// ---------------------------------------------------------------------------
// Per-window config table and tracking
//
// To support multiple windows with different configs, we maintain a small
// table of (window, config) pairs. On each touch event, we look up the top
// window's config and route the event accordingly.
// ---------------------------------------------------------------------------

#define MAX_WINDOW_CONFIGS 8
typedef struct {
    Window      *window;
    RotaryConfig config;
} WindowConfigEntry;

static WindowConfigEntry s_window_configs[MAX_WINDOW_CONFIGS];
static int               s_window_config_count = 0;
static bool              s_window_has_config  = false;

static RotaryConfig *prv_find_window_config(Window *window) {
    for (int i = 0; i < s_window_config_count; i++) {
        if (s_window_configs[i].window == window) {
            return &s_window_configs[i].config;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Swipe region
//
// The screen is divided into four 90° wedges.  Angles are measured clockwise
// from the top (12-o'clock = 0°), matching Pebble's DEG_TO_TRIGANGLE convention.
//
//   UP    region:  315° – 45°   (top)
//   RIGHT region:   45° – 135°  (right)
//   DOWN  region:  135° – 225°  (bottom)
//   LEFT  region:  225° – 315°  (left)
//
// REGION_NONE means the touch is inside the dead zone (no region).
// ---------------------------------------------------------------------------

typedef enum {
    REGION_NONE  = -1,
    REGION_UP    =  0,   // maps 1:1 to RotarySwipeDirection_Up
    REGION_DOWN  =  1,
    REGION_LEFT  =  2,
    REGION_RIGHT =  3,
} TouchRegion;

// Opposite region for each direction — used to validate a cross-screen swipe.
static const TouchRegion OPPOSITE[4] = {
    REGION_DOWN,   // opposite of UP
    REGION_UP,     // opposite of DOWN
    REGION_RIGHT,  // opposite of LEFT
    REGION_LEFT,   // opposite of RIGHT
};

// ---------------------------------------------------------------------------
// Rotation tracking
// ---------------------------------------------------------------------------

static int16_t s_last_angle_hd  = 0;
static int32_t s_accumulated_hd = 0;
static int32_t s_total_hd       = 0;
static int     s_click_count    = 0;
static bool    s_is_rotating    = false;   // true once finger is on the wheel

// ---------------------------------------------------------------------------
// Swipe tracking
// ---------------------------------------------------------------------------

static TouchRegion s_start_region   = REGION_NONE;
static bool        s_centre_crossed = false;   // did path pass through dead zone?
static bool        s_center_tap_pending = false;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// Returns true if (x,y) is OUTSIDE the dead-zone (i.e. on the wheel ring).
static bool prv_on_wheel(int16_t x, int16_t y) {
    int32_t dx = x - s_cfg.center_x;
    int32_t dy = y - s_cfg.center_y;
    int32_t r  = s_cfg.min_radius;
    return (dx * dx + dy * dy) >= r * r;
}

// Returns the touch angle in half-degrees [0..720), 0 = top, CW positive.
// Screen coords: y increases downward, so "top" is negative dy.
static int16_t prv_coords_to_angle_hd(int16_t x, int16_t y) {
    int16_t dx  = x - s_cfg.center_x;
    int16_t dy  = y - s_cfg.center_y;

    if (dx == 0 && dy == 0) return 0;

    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;
    int8_t  oct_hd;

    if (adx >= ady) {
        oct_hd = s_atan_table[(ady * 45) / adx];
        if      (dx >= 0 && dy >= 0) return oct_hd;
        else if (dx <  0 && dy >= 0) return 360 - oct_hd;
        else if (dx <  0 && dy <  0) return 360 + oct_hd;
        else                         return 720 - oct_hd;
    } else {
        oct_hd = s_atan_table[(adx * 45) / ady];
        if      (dx >= 0 && dy >= 0) return 180 - oct_hd;
        else if (dx <  0 && dy >= 0) return 180 + oct_hd;
        else if (dx <  0 && dy <  0) return 540 - oct_hd;
        else                         return 540 + oct_hd;
    }
}

// Shortest signed delta between two half-degree angles, noise-clamped.
static int16_t prv_angle_delta_hd(int16_t from, int16_t to) {
    int16_t delta = to - from;
    if (delta >  360) delta -= 720;
    if (delta < -360) delta += 720;
    if (delta >   60) return 0;
    if (delta <  -60) return 0;
    return delta;
}

// Returns which swipe region (x,y) falls in, or REGION_NONE if in dead zone.
// Angles: 0° = right, 90° = down, 180° = left, 270° = up  (screen coords CW).
// But our angle fn returns 0=right, so we map:
//   RIGHT:  45– 135  half-degrees 90–270
//   DOWN:  135– 225  half-degrees 270–450
//   LEFT:  225– 315  half-degrees 450–630
//   UP:    315– 45   half-degrees 630–720 + 0–90
//
// Simpler to work in whole degrees from the angle_hd / 2.
static TouchRegion prv_region_at(int16_t x, int16_t y) {
    if (!prv_on_wheel(x, y)) return REGION_NONE;

    // angle_hd uses 0=right, CW.  Convert to 0=top, CW by adding 270° (= 540 hd).
    int16_t raw_hd  = prv_coords_to_angle_hd(x, y);
    int16_t top_hd  = (raw_hd + 540) % 720;  // 0=top, CW, half-degrees
    int16_t deg     = top_hd / 2;             // 0–359, 0=top, CW

    // Regions: UP 315–45, RIGHT 45–135, DOWN 135–225, LEFT 225–315
    if (deg < 45 || deg >= 315) return REGION_UP;
    if (deg < 135)              return REGION_RIGHT;
    if (deg < 225)              return REGION_DOWN;
    return REGION_LEFT;
}

// ---------------------------------------------------------------------------
// Internal click dispatch
// ---------------------------------------------------------------------------

static void prv_fire_click(int direction) {
    if (!s_window_has_config) return;
    s_click_count++;
    if (s_cfg.vibrate_on_click) vibes_short_pulse();
    if (s_cfg.on_click) {
        s_cfg.on_click(direction, s_click_count, s_cfg.context);
    }
    // APP_LOG(APP_LOG_LEVEL_DEBUG,
    //         "RotaryKit CLICK #%d: %s  accum=%dhd  total=%dhd",
    //         s_click_count,
    //         direction > 0 ? "CW" : "CCW",
    //         (int)s_accumulated_hd,
    //         (int)s_total_hd);
}

// ---------------------------------------------------------------------------
// Touch service handler
// ---------------------------------------------------------------------------

static void prv_touch_handler(const TouchEvent *event, void *context) {
    int16_t threshold_hd = s_cfg.degrees_per_click * 2;

    switch (event->type) {

        case TouchEvent_Touchdown:
            // Capture the top window's config for this gesture
            {
                Window *top_window = window_stack_get_top_window();
                RotaryConfig *window_config = top_window ? prv_find_window_config(top_window) : NULL;
                if (window_config) {
                    s_cfg = *window_config;
                    s_window_has_config = true;
                } else {
                    s_window_has_config = false;
                }
            }

            // Reset all per-gesture state
            s_click_count        = 0;
            s_accumulated_hd     = 0;
            s_total_hd           = 0;
            s_is_rotating        = false;
            s_centre_crossed     = false;
            s_center_tap_pending = false;
            s_start_region       = prv_region_at(event->x, event->y);

            if (s_start_region != REGION_NONE) {
                // Started on the wheel — begin rotation tracking immediately.
                s_is_rotating   = true;
                s_last_angle_hd = prv_coords_to_angle_hd(event->x, event->y);
                // APP_LOG(APP_LOG_LEVEL_DEBUG,
                //         "RotaryKit TOUCHDOWN wheel (%d,%d) region=%d angle=%dhd",
                //         (int)event->x, (int)event->y,
                //         (int)s_start_region, (int)s_last_angle_hd);
            } else {
                // Started in dead zone — centre tap candidate.
                s_center_tap_pending = true;
                // APP_LOG(APP_LOG_LEVEL_DEBUG,
                //         "RotaryKit TOUCHDOWN dead zone (%d,%d)",
                //         (int)event->x, (int)event->y);
            }
            break;

        case TouchEvent_PositionUpdate:
            // Track centre crossing regardless of where gesture started.
            if (!s_centre_crossed && !prv_on_wheel(event->x, event->y)) {
                s_centre_crossed = true;
                // APP_LOG(APP_LOG_LEVEL_DEBUG, "RotaryKit: centre crossed");
            }

            // Cancel centre tap if finger has moved meaningfully.
            if (s_center_tap_pending && prv_on_wheel(event->x, event->y)) {
                s_center_tap_pending = false;
                // If the finger entered the wheel from the dead zone, start
                // rotation tracking from here.
                if (!s_is_rotating) {
                    s_is_rotating   = true;
                    s_start_region  = prv_region_at(event->x, event->y);
                    s_last_angle_hd = prv_coords_to_angle_hd(event->x, event->y);
                }
                // APP_LOG(APP_LOG_LEVEL_DEBUG,
                //         "RotaryKit: left dead zone — rotation tracking started");
            }

            // Rotation: accumulate angular delta while on the wheel.
            if (s_is_rotating && prv_on_wheel(event->x, event->y)) {
                int16_t cur_hd = prv_coords_to_angle_hd(event->x, event->y);
                int16_t delta  = prv_angle_delta_hd(s_last_angle_hd, cur_hd);
                s_accumulated_hd += delta;
                s_total_hd       += delta < 0 ? -delta : delta;
                s_last_angle_hd   = cur_hd;

                while (s_accumulated_hd >= threshold_hd) {
                    prv_fire_click(+1);
                    s_accumulated_hd -= threshold_hd;
                }
                while (s_accumulated_hd <= -threshold_hd) {
                    prv_fire_click(-1);
                    s_accumulated_hd += threshold_hd;
                }
            }
            break;

        case TouchEvent_Liftoff: {
            // Centre tap — finger stayed in dead zone the whole gesture.
            if (s_center_tap_pending) {
                s_center_tap_pending = false;
                // APP_LOG(APP_LOG_LEVEL_INFO, "RotaryKit: centre tap");
                if (s_window_has_config && s_cfg.on_center_tap) {
                    s_cfg.on_center_tap(s_cfg.context);
                }
                break;
            }

            // Swipe detection: start region + centre crossed + opposite end region.
            TouchRegion end_region = prv_region_at(event->x, event->y);

            if (s_centre_crossed &&
                s_start_region != REGION_NONE &&
                end_region     != REGION_NONE &&
                end_region == OPPOSITE[s_start_region]) {

                RotarySwipeDirection dir = (RotarySwipeDirection)s_start_region;
                // start=LEFT fires RIGHT swipe, start=UP fires DOWN, etc.
                // OPPOSITE maps start → end, and the swipe direction is
                // "toward the end region", which equals OPPOSITE[start].
                //dir = (RotarySwipeDirection)end_region;

                const char *dir_name[] = { "UP", "DOWN", "LEFT", "RIGHT" };
                // APP_LOG(APP_LOG_LEVEL_INFO,
                //         "RotaryKit SWIPE %s (start=%d end=%d)",
                //         dir_name[dir], (int)s_start_region, (int)end_region);

                if (s_window_has_config) {
                    if (s_cfg.vibrate_on_swipe) vibes_short_pulse();
                    if (s_cfg.on_swipe) {
                        s_cfg.on_swipe(dir, s_cfg.context);
                    }
                }
            } else {
                // Not a valid swipe — report as a normal rotation liftoff.
                // APP_LOG(APP_LOG_LEVEL_INFO,
                //         "RotaryKit LIFTOFF clicks=%d total=%ddeg leftover=%ddeg",
                //         s_click_count,
                //         (int)s_total_hd / 2,
                //         (int)s_accumulated_hd / 2);
                if (s_window_has_config && s_cfg.on_liftoff) {
                    s_cfg.on_liftoff(s_click_count,
                                     (int)s_total_hd / 2,
                                     s_cfg.context);
                }
            }

            // Reset
            s_is_rotating    = false;
            s_accumulated_hd = 0;
            s_centre_crossed = false;
            s_start_region   = REGION_NONE;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RotaryConfig rotary_kit_default_config(void) {
    RotaryConfig cfg = {
        .center_x          = 130,
        .center_y          = 130,
        .min_radius        = 40,
        .degrees_per_click = 30,
        .vibrate_on_click  = true,
        .vibrate_on_swipe  = true,
        .on_click          = NULL,
        .on_liftoff        = NULL,
        .on_center_tap     = NULL,
        .on_swipe          = NULL,
        .context           = NULL,
    };
    return cfg;
}

void rotary_kit_set_window_config(Window *window, const RotaryConfig *config) {
    for (int i = 0; i < s_window_config_count; i++) {
        if (s_window_configs[i].window == window) {
            s_window_configs[i].config = *config;
            return;
        }
    }
    if (s_window_config_count < MAX_WINDOW_CONFIGS) {
        s_window_configs[s_window_config_count].window = window;
        s_window_configs[s_window_config_count].config = *config;
        s_window_config_count++;
    } else {
        // APP_LOG(APP_LOG_LEVEL_ERROR, "RotaryKit: max window configs reached!");
        return;
    }

    if (!s_active && touch_service_is_enabled()) {
        touch_service_subscribe(prv_touch_handler, NULL);
        s_active = true;

        // APP_LOG(APP_LOG_LEVEL_INFO,
        //     "RotaryKit: active — centre=(%d,%d) deadzone=%dpx detent=%ddeg",
        //     (int)s_cfg.center_x, (int)s_cfg.center_y,
        //     (int)s_cfg.min_radius, (int)s_cfg.degrees_per_click);
    } else {
        // APP_LOG(APP_LOG_LEVEL_ERROR,
        //     "RotaryKit: touch service not available on this hardware");
    }
}

void rotary_kit_clear_window_config(Window *window) {
    for (int i = 0; i < s_window_config_count; i++) {
        if (s_window_configs[i].window == window) {
            for (int j = i; j < s_window_config_count - 1; j++) {
                s_window_configs[j] = s_window_configs[j + 1];
            }
            s_window_config_count--;
            break;
        }
    }
    if (s_window_config_count == 0 && s_active) {
        touch_service_unsubscribe();
        s_active             = false;
        s_is_rotating        = false;
        s_centre_crossed     = false;
        s_center_tap_pending = false;
        s_start_region       = REGION_NONE;
        // APP_LOG(APP_LOG_LEVEL_INFO, "RotaryKit: deactivated");
    }
}

bool rotary_kit_is_active(void) {
    return s_active;
}
