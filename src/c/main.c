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

static Window    *s_window;
static MenuLayer *s_menu_layer;

#define NUM_ITEMS 10
static const char *ITEMS[NUM_ITEMS] = {
    "Alarm",
    "Bluetooth",
    "Calendar",
    "Do Not Disturb",
    "Exercise",
    "Find My Phone",
    "Goals",
    "Heart Rate",
    "Info",
    "Journal",
};

static bool s_selected = false; 
static bool s_swiped   = false; 
static int s_swiped_Dir  = -0;// false = normal, true = "selected" highlight
const char *labels[] = { "UP", "DOWN", "LEFT", "RIGHT" };

static void apply_colors(void) {
#if defined(PBL_COLOR)
    if (s_selected || s_swiped) {
        // Inverted / "selected" palette
        menu_layer_set_normal_colors(s_menu_layer, GColorCobaltBlue, GColorWhite);
        menu_layer_set_highlight_colors(s_menu_layer, GColorWhite, GColorCobaltBlue);
    } else {
        // Normal palette
        menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
        menu_layer_set_highlight_colors(s_menu_layer, GColorCobaltBlue, GColorWhite);
    }
#endif
    layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
}

static void on_wheel_click(int direction, int click_num, void *context) {
    // direction: +1 = clockwise / scroll down, -1 = CCW / scroll up
//    text_layer_set_text(s_label, direction > 0 ? "v Down" : "^ Up");
    if(direction == 1){
      menu_layer_set_selected_next(s_menu_layer, false, MenuRowAlignCenter, true);
    }
    if(direction == -1){
      menu_layer_set_selected_next(s_menu_layer, true, MenuRowAlignCenter, true);
    }    
}

static void on_swipe(RotarySwipeDirection dir, void *context) {
    s_swiped = true;
    s_swiped_Dir = dir;
    apply_colors();
}

static void on_center_tap(void *context) {
    s_selected = !s_selected;
    s_swiped = false;
    apply_colors();
 
    MenuIndex idx = menu_layer_get_selected_index(s_menu_layer);
    APP_LOG(APP_LOG_LEVEL_INFO,
            "Centre tap — %s is now %s",
            ITEMS[idx.row],
            s_selected ? "selected" : "normal");
    vibes_short_pulse();
}

static void on_wheel_liftoff(int total_clicks, int total_degrees, void *context) {
    // Optional: use this to commit a selection, log analytics, etc.
    APP_LOG(APP_LOG_LEVEL_INFO,
            "Gesture ended: %d clicks over %d degrees", total_clicks, total_degrees);
}

static uint16_t menu_get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
    return NUM_ITEMS;
}

// static void menu_draw_row(GContext *ctx, const Layer *cell_layer,
//                           MenuIndex *idx, void *context) {
//     menu_cell_basic_draw(ctx, cell_layer, ITEMS[idx->row], NULL, NULL);
// }

static void menu_draw_row(GContext *ctx, const Layer *cell_layer,
                          MenuIndex *idx, void *context) {
    // When selected-state is active, prefix the highlighted row's text
    MenuIndex cur = menu_layer_get_selected_index(s_menu_layer);
    bool is_highlighted = (menu_index_compare(&cur, idx) == 0);
 
    if (s_selected && is_highlighted) {
        static char buf1[64];
        static char buf2[64];
        snprintf(buf1, sizeof(buf1), "* %s", ITEMS[idx->row]);
        if(s_swiped)
          {
          snprintf(buf2, sizeof(buf2), "SWIPED %s", labels[s_swiped_Dir]);
          menu_cell_basic_draw(ctx, cell_layer, buf1, buf2, NULL);
        }
        else
          menu_cell_basic_draw(ctx, cell_layer, buf1, "tap to deselect", NULL);
    } else {
        menu_cell_basic_draw(ctx, cell_layer, ITEMS[idx->row], NULL, NULL);
    }
}

static void menu_select_callback(MenuLayer *ml, MenuIndex *idx, void *ctx) {
    APP_LOG(APP_LOG_LEVEL_INFO, "SELECTED: %s", ITEMS[idx->row]);
  on_center_tap(ctx);
    // TODO: push a detail window here
    //vibes_double_pulse();
}

static void select_click_handler(ClickRecognizerRef r, void *ctx) {
    MenuIndex idx = menu_layer_get_selected_index(s_menu_layer);
    APP_LOG(APP_LOG_LEVEL_INFO, "BTN SELECT: %s", ITEMS[idx.row]);
    vibes_double_pulse();
}

static void click_config_provider(void *context) {
    // Let MenuLayer own Up/Down/Select so it handles scrolling automatically.
    // Call menu_layer_set_click_config_onto_window instead of subscribing manually.
    menu_layer_set_click_config_onto_window(s_menu_layer, s_window);
    // Override Select if you want custom behaviour:
    // window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// static void layer_update_proc(Layer *layer, GContext *ctx) {
//   GRect bounds = layer_get_bounds(layer);

//   // 12 hours only, with a minimum size
//   int s_hours = 12;

//   // Minutes are expanding circle arc
//   int minute_angle = get_angle_for_minute(s_minutes);
//   GRect frame = grect_inset(bounds, GEdgeInsets(4 * INSET));
//   graphics_context_set_fill_color(ctx, MINUTES_COLOR);
//   graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, 20, 0, DEG_TO_TRIGANGLE(minute_angle));

//   // Adjust geometry variables for inner ring
//   frame = grect_inset(frame, GEdgeInsets(3 * HOURS_RADIUS));

//   // Hours are dots
//   for(int i = 0; i < 12; i++) {
//     int hour_angle = get_angle_for_hour(i);
//     GPoint pos = gpoint_from_polar(frame, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(hour_angle));

//     graphics_context_set_fill_color(ctx, i <= s_hours ? HOURS_COLOR : HOURS_COLOR_INACTIVE);
//     graphics_fill_circle(ctx, pos, HOURS_RADIUS);
//   }
// }

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
 
    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
        .get_num_rows = menu_get_num_rows,
        .draw_row     = menu_draw_row,
        .select_click = menu_select_callback,
    });
 
    apply_colors();   // set initial colours
 
    layer_add_child(root, menu_layer_get_layer(s_menu_layer));

    // Build a config — start from defaults, override what you need.
    RotaryConfig cfg      = rotary_kit_default_config();
    cfg.on_click          = on_wheel_click;
    cfg.on_liftoff        = on_wheel_liftoff;
    cfg.on_swipe          = on_swipe;
    cfg.on_center_tap     = on_center_tap;
    cfg.degrees_per_click = 30;
    // cfg.vibrate_on_click = false; // silence haptics if desired
    rotary_kit_set_window_config(window, &cfg);
}

static void window_unload(Window *window) {
    rotary_kit_clear_window_config(window);
    menu_layer_destroy(s_menu_layer);
}

static void window_appear(Window *window) {
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
}

static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, BG_COLOR);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
      .load      = window_load,
      .unload    = window_unload,
      .appear    = window_appear,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
