// rotary_kit.h — RotaryKit: iPod-style click wheel gesture library for Pebble
// Drop rotary_kit.h + rotary_kit.c into your project and include this header.
//
// Usage:
//   1. Create a RotaryConfig and fill in your callbacks + tuning values.
//   2. Call rotary_kit_init() in your window's .appear handler.
//   3. Call rotary_kit_deinit() in your window's .disappear handler.
//   That's it — RotaryKit owns the touch subscription.

#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

// Fired every time the wheel rotates enough for one "detent" click.
//   direction: +1 = clockwise (scroll down), -1 = counter-clockwise (scroll up)
//   click_num: 1-based count of clicks fired in the current drag gesture
typedef void (*RotaryClickCallback)(int direction, int click_num, void *context);

// Fired when the user lifts their finger (optional — pass NULL to skip).
//   total_clicks : total click-events fired during this gesture
//   total_degrees: total absolute rotation in degrees (always positive)
typedef void (*RotaryLiftoffCallback)(int total_clicks, int total_degrees, void *context);

// Fired when the user taps and releases inside the dead-zone centre without
// drifting onto the wheel — equivalent to pressing the physical Select button.
// (optional — pass NULL to skip)
typedef void (*RotaryCenterTapCallback)(void *context);

// Direction reported by on_swipe.
typedef enum {
    RotarySwipeDirection_Up    = 0,
    RotarySwipeDirection_Down  = 1,
    RotarySwipeDirection_Left  = 2,
    RotarySwipeDirection_Right = 3,
} RotarySwipeDirection;

// Fired when a linear drag is recognised as a directional swipe (on liftoff).
//   direction: one of the four RotarySwipeDirection values.
// (optional — pass NULL to skip)
typedef void (*RotarySwipeCallback)(RotarySwipeDirection direction, void *context);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

typedef struct {
    // --- Geometry ---
    int16_t center_x;          // X pixel of wheel centre (default: 130)
    int16_t center_y;          // Y pixel of wheel centre (default: 130)
    int16_t min_radius;        // Dead zone radius — touches inside are ignored
                               //   (default: 40)

    // --- Sensitivity ---
    int16_t degrees_per_click;  // Degrees of rotation per detent (default: 30)
                                // Lower = more sensitive, higher = coarser

    // --- Haptics ---
    bool    vibrate_on_click;   // Pulse on every rotation detent (default: true)
    bool    vibrate_on_swipe;   // Short pulse when a swipe fires (default: true)

    // --- Callbacks ---
    RotaryClickCallback     on_click;       // Required
    RotaryLiftoffCallback   on_liftoff;     // Optional — pass NULL
    RotaryCenterTapCallback on_center_tap;  // Optional — pass NULL
    RotarySwipeCallback     on_swipe;       // Optional — pass NULL

    // --- User data passed back to callbacks ---
    void *context;
} RotaryConfig;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Returns a RotaryConfig pre-filled with sensible defaults.
// Override only the fields you care about before passing to rotary_kit_init().
RotaryConfig rotary_kit_default_config(void);

// Subscribe to the touch service and begin gesture tracking.
// Call from your window's .appear handler (or anywhere after the window loads).
// Returns false if touch is unavailable on this hardware.
bool rotary_kit_init(const RotaryConfig *config);

// Unsubscribe from the touch service and reset state.
// Call from your window's .disappear handler.
void rotary_kit_deinit(void);

// Returns true if RotaryKit is currently subscribed and tracking.
bool rotary_kit_is_active(void);
