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

static RotaryConfig s_cfg;          // copy of active config
static bool         s_active        = false;
static bool         s_is_tracking   = false;
static int16_t      s_last_angle_hd = 0;
static int32_t      s_accumulated_hd = 0;
static int32_t      s_total_hd       = 0;
static int          s_click_count    = 0;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// Returns true if (x,y) is OUTSIDE the dead-zone circle (i.e. on the wheel).
static bool prv_on_wheel(int16_t x, int16_t y) {
    int32_t dx = x - s_cfg.center_x;
    int32_t dy = y - s_cfg.center_y;
    int32_t r  = s_cfg.min_radius;
    return (dx * dx + dy * dy) >= r * r;
}

// Returns the touch angle in half-degrees [0..720).
//   0   = right,  180 = down,  360 = left,  540 = up  (clockwise, screen coords)
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

// Shortest signed delta between two half-degree angles, clamped for noise.
// Single-frame jumps > 30 degrees (= 60 hd) are treated as sensor noise.
static int16_t prv_angle_delta_hd(int16_t from, int16_t to) {
    int16_t delta = to - from;
    if (delta >  360) delta -= 720;
    if (delta < -360) delta += 720;
    if (delta >   60) return 0;   // > 30 deg jump = noise, discard
    if (delta <  -60) return 0;
    return delta;
}

// ---------------------------------------------------------------------------
// Internal click dispatch
// ---------------------------------------------------------------------------

static void prv_fire_click(int direction) {
    s_click_count++;
    if (s_cfg.vibrate_on_click) {
        vibes_short_pulse();
    }
    if (s_cfg.on_click) {
        s_cfg.on_click(direction, s_click_count, s_cfg.context);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG,
            "RotaryKit CLICK #%d: %s  accum=%dhd  total=%dhd",
            s_click_count,
            direction > 0 ? "CW" : "CCW",
            (int)s_accumulated_hd,
            (int)s_total_hd);
}

// ---------------------------------------------------------------------------
// Touch service handler
// ---------------------------------------------------------------------------

static void prv_touch_handler(const TouchEvent *event, void *context) {
    int16_t threshold_hd = s_cfg.degrees_per_click * 2; // half-degrees

    switch (event->type) {

        case TouchEvent_Touchdown:
            // Reset per-gesture state
            s_click_count     = 0;
            s_total_hd        = 0;
            s_accumulated_hd  = 0;
            s_is_tracking     = false;

            if (prv_on_wheel(event->x, event->y)) {
                s_last_angle_hd = prv_coords_to_angle_hd(event->x, event->y);
                s_is_tracking   = true;
                APP_LOG(APP_LOG_LEVEL_DEBUG,
                        "RotaryKit TOUCHDOWN (%d,%d) angle=%dhd (%ddeg)",
                        (int)event->x, (int)event->y,
                        (int)s_last_angle_hd, (int)s_last_angle_hd / 2);
            } else {
                APP_LOG(APP_LOG_LEVEL_DEBUG,
                        "RotaryKit TOUCHDOWN (%d,%d) dead zone — ignoring",
                        (int)event->x, (int)event->y);
            }
            break;

        case TouchEvent_PositionUpdate:
            if (s_is_tracking && prv_on_wheel(event->x, event->y)) {
                int16_t cur_hd = prv_coords_to_angle_hd(event->x, event->y);
                int16_t delta  = prv_angle_delta_hd(s_last_angle_hd, cur_hd);

                s_accumulated_hd += delta;
                s_total_hd       += delta < 0 ? -delta : delta;
                s_last_angle_hd   = cur_hd;

                // Drain accumulated rotation into discrete clicks
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

        case TouchEvent_Liftoff:
            APP_LOG(APP_LOG_LEVEL_INFO,
                    "RotaryKit LIFTOFF clicks=%d total=%ddeg leftover=%ddeg",
                    s_click_count,
                    (int)s_total_hd / 2,
                    (int)s_accumulated_hd / 2);

            if (s_cfg.on_liftoff) {
                s_cfg.on_liftoff(s_click_count,
                                 (int)s_total_hd / 2,
                                 s_cfg.context);
            }

            s_is_tracking    = false;
            s_accumulated_hd = 0;
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

RotaryConfig rotary_kit_default_config(void) {
    RotaryConfig cfg = {
        .center_x         = 130,
        .center_y         = 130,
        .min_radius       = 40,
        .degrees_per_click = 30,
        .vibrate_on_click = true,
        .on_click         = NULL,
        .on_liftoff       = NULL,
        .context          = NULL,
    };
    return cfg;
}

bool rotary_kit_init(const RotaryConfig *config) {
    if (s_active) {
        APP_LOG(APP_LOG_LEVEL_WARNING,
                "RotaryKit: rotary_kit_init called while already active");
        return true;
    }
    if (!touch_service_is_enabled()) {
        APP_LOG(APP_LOG_LEVEL_WARNING,
                "RotaryKit: touch service not available on this hardware");
        return false;
    }

    s_cfg           = *config;   // take a local copy
    s_is_tracking   = false;
    s_accumulated_hd = 0;
    s_total_hd       = 0;
    s_click_count    = 0;

    touch_service_subscribe(prv_touch_handler, NULL);
    s_active = true;

    APP_LOG(APP_LOG_LEVEL_INFO,
            "RotaryKit: active — centre=(%d,%d) deadzone=%dpx detent=%ddeg",
            (int)s_cfg.center_x, (int)s_cfg.center_y,
            (int)s_cfg.min_radius, (int)s_cfg.degrees_per_click);
    return true;
}

void rotary_kit_deinit(void) {
    if (!s_active) return;
    touch_service_unsubscribe();
    s_active      = false;
    s_is_tracking = false;
    APP_LOG(APP_LOG_LEVEL_INFO, "RotaryKit: deactivated");
}

bool rotary_kit_is_active(void) {
    return s_active;
}
