# RotaryKit

An click wheel style gesture library for [Pebble Time/Round 2](https://www.repebble.com).
Drop two files into your project and get rotation clicks, directional swipes, and centre
taps — all from the touch display — with no boilerplate.

> **Hardware requirement:** RotaryKit requires the Pebble Time 2 or Round 2 (Emery/Gabbro platform),
>  On other hardware `rotary_kit_set_window_config()` still registers safely but no callbacks
>  will ever fire.

---

## Gesture vocabulary

| Gesture | How to perform | Callback |
|---|---|---|
| **Rotation click** | Drag finger along the ring CW or CCW | `on_click` |
| **Centre tap** | Tap and release inside the centre dead zone | `on_center_tap` |
| **Directional swipe** | Start on one side of the ring, cross the centre, lift on the opposite side | `on_swipe` |

### Swipe regions

The screen is divided into four 90° wedges (measured clockwise from 45°):

```
         UP  315°–45°
         ┌─────────┐
LEFT     │  \   /  │    RIGHT
225°–315°│    ●    │    45°–135°
         │  /   \  │
         └─────────┘
        DOWN 135°–225°
```

A valid swipe requires **all three**:
1. Touchdown inside a named region
2. Path passes through the centre dead zone
3. Liftoff inside the **opposite** region

Example: touch LEFT → cross centre → lift on RIGHT → fires `RotarySwipeDirection_Right`.

---

## Installation

1. Copy `rotary_kit.h` and `rotary_kit.c` into your project's `src/` folder.
2. `#include "rotary_kit.h"` wherever you use it.
3. No `wscript` changes needed — the SDK picks up `.c` files automatically.

---

## Quick start

```c
#include <pebble.h>
#include "rotary_kit.h"

static Window *s_window;

static void on_click(int direction, int click_num, void *context) {
    // direction: +1 = clockwise (scroll down), -1 = CCW (scroll up)
}

static void on_swipe(RotarySwipeDirection dir, void *context) {
    const char *labels[] = { "UP", "DOWN", "LEFT", "RIGHT" };
    APP_LOG(APP_LOG_LEVEL_INFO, "Swipe: %s", labels[dir]);
}

static void on_center_tap(void *context) {
    // Equivalent to pressing the physical Select button
}

static void window_load(Window *window) {
    // ... create layers ...

    RotaryConfig cfg  = rotary_kit_default_config();
    cfg.on_click      = on_click;
    cfg.on_swipe      = on_swipe;       // optional
    cfg.on_center_tap = on_center_tap;  // optional
    rotary_kit_set_window_config(window, &cfg);
}

static void window_unload(Window *window) {
    // ... destroy layers ...
    rotary_kit_clear_window_config(window);
}
```

---

## API reference

### `RotaryConfig rotary_kit_default_config(void)`

Returns a config struct pre-filled with sensible defaults. Override only what you need.

| Field | Default | Description |
|---|---|---|
| `center_x` | 130 | X pixel of screen centre |
| `center_y` | 130 | Y pixel of screen centre |
| `min_radius` | 40 | Dead zone radius in pixels. Touches inside = centre tap zone. |
| `degrees_per_click` | 30 | Degrees of arc per rotation detent. Lower = more sensitive. |
| `vibrate_on_click` | `true` | Short haptic pulse on each rotation detent. |
| `vibrate_on_swipe` | `true` | Short haptic pulse when a swipe fires. |
| `on_click` | `NULL` | **Required.** Rotation callback. |
| `on_liftoff` | `NULL` | Optional. Fires on finger lift after a rotation. |
| `on_center_tap` | `NULL` | Optional. Fires on a tap inside the dead zone. |
| `on_swipe` | `NULL` | Optional. Fires on a valid cross-screen swipe. |
| `context` | `NULL` | Passed back to all callbacks as the `context` argument. |

---

### `void rotary_kit_set_window_config(Window *window, const RotaryConfig *config)`

Registers a gesture config for `window`. Call from your window's `.load` handler.

RotaryKit subscribes to the touch service automatically on the first call. Up to 8
windows may be registered simultaneously. If a window is already registered, its
config is updated in place.

On Touchdown, RotaryKit looks up whichever window is currently on top of the stack
and uses that window's config for the entire gesture. Pushing or popping windows
between gestures just works — no extra calls needed.

---

### `void rotary_kit_clear_window_config(Window *window)`

Removes the gesture config for `window`. Call from your window's `.unload` handler.

RotaryKit unsubscribes from the touch service automatically when the last window
is removed.

---

### `bool rotary_kit_is_active(void)`

Returns `true` if RotaryKit is currently subscribed to the touch service.

---

### Callbacks

```c
// Fired every time the wheel rotates enough for one detent.
//   direction: +1 = CW (scroll down), -1 = CCW (scroll up)
//   click_num: 1-based count within the current drag gesture
typedef void (*RotaryClickCallback)(int direction, int click_num, void *context);

// Fired on finger lift after a rotation gesture.
//   total_clicks:  detents fired this gesture
//   total_degrees: total absolute arc in degrees (always positive)
typedef void (*RotaryLiftoffCallback)(int total_clicks, int total_degrees, void *context);

// Fired when the user taps and releases inside the centre dead zone.
typedef void (*RotaryCenterTapCallback)(void *context);

// Fired when a valid cross-screen swipe is recognised.
typedef void (*RotarySwipeCallback)(RotarySwipeDirection direction, void *context);
```

```c
typedef enum {
    RotarySwipeDirection_Up    = 0,
    RotarySwipeDirection_Down  = 1,
    RotarySwipeDirection_Left  = 2,
    RotarySwipeDirection_Right = 3,
} RotarySwipeDirection;
```

---

## Multi-window apps

Because RotaryKit routes each gesture to the window on top of the stack, multi-window
apps work with no extra coordination:

```c
// Window A — main menu
static void window_a_load(Window *window) {
    RotaryConfig cfg = rotary_kit_default_config();
    cfg.on_click     = menu_scroll_handler;
    cfg.on_swipe     = menu_swipe_handler;
    rotary_kit_set_window_config(window, &cfg);
}
static void window_a_unload(Window *window) {
    rotary_kit_clear_window_config(window);
}

// Window B — detail view, pushed on top of A
static void window_b_load(Window *window) {
    RotaryConfig cfg  = rotary_kit_default_config();
    cfg.on_click      = detail_scroll_handler;  // different callbacks
    cfg.on_center_tap = detail_select_handler;
    rotary_kit_set_window_config(window, &cfg);
}
static void window_b_unload(Window *window) {
    rotary_kit_clear_window_config(window);
}
```

When B is on top, its handlers fire. Pop B and A's handlers resume automatically.

---

## Tips and gotchas

**RotaryKit and MenuLayer play well together.** RotaryKit uses the *touch service*;
MenuLayer uses *button click recognisers*. They don't conflict. Call
`menu_layer_set_click_config_onto_window()` in `.appear` and
`rotary_kit_set_window_config()` in `.load` — both work simultaneously.

**Don't mix `window_set_click_config_provider` with `menu_layer_set_click_config_onto_window`.**
Pick one: your own provider *or* MenuLayer's. Calling both causes the later one to
overwrite the earlier one.

**Rotation clicks fire during the gesture; swipes fire on liftoff.** This is by design —
rotation gives immediate per-detent feedback, swipes are committed gestures.

**`on_liftoff` only fires for rotation gestures**, not swipes or centre taps.

**Swipes do not suppress rotation clicks.** If rotation clicks accumulated during a
swipe gesture bother you, check `click_num` in `on_click` and ignore clicks after a
swipe fires, or set `on_click = NULL` for windows where you only want swipes.

**Tune `degrees_per_click` for your use case.** 30° works well for menu navigation.
For fine value adjustment try 10°–15°. For coarse jumps try 45°.

**Swipe left makes a natural back button.** `RotarySwipeDirection_Left` mirrors the
physical back button gesture and can be wired directly to `window_stack_pop()`:

```c
static void on_swipe(RotarySwipeDirection dir, void *context) {
    if (dir == RotarySwipeDirection_Left) {
        window_stack_pop(true);  // same slide-out animation as the physical button
        return;                  // return immediately — don't touch window state
    }
    // handle other directions...
}
```

Always `return` after `window_stack_pop` — the window is leaving the stack and any
further access to its layers or state will crash. If this is the root window,
`window_stack_pop` exits the app, which is the correct Pebble behaviour.

---

## Example apps in this repo

| Folder | Description |
|---|---|
| `main.c` | MenuLayer example scrolled by the wheel; centre tap toggles highlight colour; swipes logged |


---

## License

MIT — see [LICENSE](LICENSE). Free to use in open-source and commercial Pebble apps.
A credit in your readme is appreciated but not required.

---

## Contributing

Pull requests welcome. Please keep changes to `rotary_kit.h` / `rotary_kit.c` —
the library is intentionally self-contained with no dependencies beyond the Pebble SDK.
