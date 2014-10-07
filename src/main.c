#include <pebble.h>

#define STR_SIZE 20
#define TIME_OFFSET_PERSIST 1

static bool appStarted = false;

static Window *window;
static TextLayer *time_text_layer2;
static TextLayer *utc_time_text_layer;
static TextLayer *utc_date_text_layer;
static TextLayer *date_text_layer;

static uint8_t batteryPercent;

BitmapLayer *layer_conn_img;

GBitmap *img_bt_connect;
GBitmap *img_bt_disconnect;

static GBitmap *battery_image;
static GBitmap *battery100_image;
static BitmapLayer *battery_image_layer;
static BitmapLayer *battery100_image_layer;
static BitmapLayer *battery_layer;

int cur_day = -1;
int charge_percent = 0;

static GFont *time1_font;
static GFont *time2_font;
static GFont *day_font;
static GFont *date1_font;

static GBitmap *background_image;
static BitmapLayer *background_layer;

char *s;
// Local time is wall time, not UTC, so an offset is used to get UTC
int time_offset;

#define TOTAL_BATTERY_PERCENT_DIGITS 3
static GBitmap *battery_percent_image[TOTAL_BATTERY_PERCENT_DIGITS];
static BitmapLayer *battery_percent_layers[TOTAL_BATTERY_PERCENT_DIGITS];

const int TINY_IMAGE_RESOURCE_IDS[] = {
  RESOURCE_ID_IMAGE_TINY_0,
  RESOURCE_ID_IMAGE_TINY_1,
  RESOURCE_ID_IMAGE_TINY_2,
  RESOURCE_ID_IMAGE_TINY_3,
  RESOURCE_ID_IMAGE_TINY_4,
  RESOURCE_ID_IMAGE_TINY_5,
  RESOURCE_ID_IMAGE_TINY_6,
  RESOURCE_ID_IMAGE_TINY_7,
  RESOURCE_ID_IMAGE_TINY_8,
  RESOURCE_ID_IMAGE_TINY_9,
  RESOURCE_ID_IMAGE_TINY_PERCENT
};

InverterLayer *inverter_layer = NULL;

static void display_utc() {
	
	static char utc_time_text[] = "00:00:00";
    static char utc_date_text[] = "0000-00-00";
	
	time_t utc = time(NULL) + time_offset;
    struct tm *zulu_time = localtime(&utc);
	
    strftime(utc_time_text, sizeof(utc_time_text), "%R:%S", zulu_time);
	text_layer_set_text(utc_time_text_layer, utc_time_text);
	
    strftime(utc_date_text, sizeof(utc_date_text), "%F", zulu_time);
    text_layer_set_text(utc_date_text_layer, utc_date_text);
	
}	
	
void change_battery_icon(bool charging) {
  gbitmap_destroy(battery_image);
  if(charging) {
    battery_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_CHARGE);
  }
  else {
    battery_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
  }  
  bitmap_layer_set_bitmap(battery_image_layer, battery_image);
  layer_mark_dirty(bitmap_layer_get_layer(battery_image_layer));
}

static void set_container_image(GBitmap **bmp_image, BitmapLayer *bmp_layer, const int resource_id, GPoint origin) {
  GBitmap *old_image = *bmp_image;
  *bmp_image = gbitmap_create_with_resource(resource_id);
  GRect frame = (GRect) {
    .origin = origin,
    .size = (*bmp_image)->bounds.size
  };
  bitmap_layer_set_bitmap(bmp_layer, *bmp_image);
  layer_set_frame(bitmap_layer_get_layer(bmp_layer), frame);
  gbitmap_destroy(old_image);
}

static void update_battery(BatteryChargeState charge_state) {

  batteryPercent = charge_state.charge_percent;

  if(batteryPercent==100) {
        change_battery_icon(false);
        layer_set_hidden(bitmap_layer_get_layer(battery_layer), false);
        layer_set_hidden(bitmap_layer_get_layer(battery100_image_layer), false);
    for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; ++i) {
      layer_set_hidden(bitmap_layer_get_layer(battery_percent_layers[i]), true);
    }  
    return;
  }
  layer_set_hidden(bitmap_layer_get_layer(battery100_image_layer), true);
  layer_set_hidden(bitmap_layer_get_layer(battery_layer), charge_state.is_charging);
  change_battery_icon(charge_state.is_charging);

  for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; ++i) {
    layer_set_hidden(bitmap_layer_get_layer(battery_percent_layers[i]), false);
  }  
  set_container_image(&battery_percent_image[0], battery_percent_layers[0], TINY_IMAGE_RESOURCE_IDS[charge_state.charge_percent/10], GPoint(110, 30));
  set_container_image(&battery_percent_image[1], battery_percent_layers[1], TINY_IMAGE_RESOURCE_IDS[charge_state.charge_percent%10], GPoint(117, 30)); 
  set_container_image(&battery_percent_image[2], battery_percent_layers[2], TINY_IMAGE_RESOURCE_IDS[10], GPoint(125, 30));

}    

void battery_layer_update_callback(Layer *me, GContext* ctx) {        
  //draw the remaining battery percentage
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 1, ((batteryPercent/100.0)*16.0), 9), 0, GCornerNone);
}

void handle_bluetooth(bool connected) {
    if (connected) {
        bitmap_layer_set_bitmap(layer_conn_img, img_bt_connect);
    } else {
        bitmap_layer_set_bitmap(layer_conn_img, img_bt_disconnect);
        vibes_long_pulse();
    }
}
	
static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
  static char time_text2[] = "00:00p";
  static char date_text[] = "xxx xxx 00";

  strftime(date_text, sizeof(date_text), "%a %b %d", tick_time);
  text_layer_set_text(date_text_layer, date_text);
  
  
  if (clock_is_24h_style()) {
    strftime(time_text2, sizeof(time_text2), "%R", tick_time);
  } else {
    strftime(time_text2, sizeof(time_text2), "%l:%M%P", tick_time);
  }
	
  if (!clock_is_24h_style() && (time_text2[0] == '0')) {
    memmove(time_text2, &time_text2[1], sizeof(time_text2) - 1);
  }

  text_layer_set_text(time_text_layer2, time_text2);
  display_utc();
	
}

// Get the time from the phone, which is UTC
// Calculate and store the offset when compared to the local clock
static void app_message_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_find(iterator, 0);
  int unixtime = t->value->int32;
  int now = (int)time(NULL);
  time_offset = unixtime - now;
  display_utc();

}

static void window_load(Window *window) {
  window_set_background_color(window, GColorBlack);
  Layer *window_layer = window_get_root_layer(window);

  background_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  background_layer = bitmap_layer_create(layer_get_frame(window_layer));
  bitmap_layer_set_bitmap(background_layer, background_image);
  layer_add_child(window_layer, bitmap_layer_get_layer(background_layer));
 
// resources
    img_bt_connect     = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CONNECT);
    img_bt_disconnect  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DISCONNECT);
	
	time1_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORATOR_32));
	time2_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORATOR_24));
	day_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORATOR_18));
	date1_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORATOR_18));
	
	layer_conn_img  = bitmap_layer_create(GRect(116, 57, 20, 20));
	layer_add_child(window_layer, bitmap_layer_get_layer(layer_conn_img));

  time_text_layer2 = text_layer_create(GRect( 12, 24, 108, 40));
  text_layer_set_background_color(time_text_layer2, GColorClear);
  text_layer_set_text_color(time_text_layer2, GColorBlack);
  text_layer_set_font(time_text_layer2, time1_font);
  text_layer_set_text(time_text_layer2, "");
  text_layer_set_text_alignment(time_text_layer2, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer2)); 
	
  utc_time_text_layer = text_layer_create(GRect( 12, 88, 150, 40));
  text_layer_set_background_color(utc_time_text_layer, GColorClear);
  text_layer_set_text_color(utc_time_text_layer, GColorBlack);
  text_layer_set_font(utc_time_text_layer, time1_font);
  text_layer_set_text(utc_time_text_layer, "");
  text_layer_set_text_alignment(utc_time_text_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(utc_time_text_layer));

  date_text_layer = text_layer_create(GRect(14, 53, 144, 168-128));
  text_layer_set_background_color(date_text_layer, GColorClear);
  text_layer_set_text_color(date_text_layer, GColorBlack);
  text_layer_set_font(date_text_layer, day_font);
  text_layer_set_text(date_text_layer, "");
  text_layer_set_text_alignment(date_text_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(date_text_layer));
  
  utc_date_text_layer = text_layer_create(GRect(14, 117, 144, 168-128));
  text_layer_set_background_color(utc_date_text_layer, GColorClear);
  text_layer_set_text_color(utc_date_text_layer, GColorBlack);
  text_layer_set_font(utc_date_text_layer, date1_font);
  text_layer_set_text(utc_date_text_layer, "");
  text_layer_set_text_alignment(utc_date_text_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(utc_date_text_layer));
	
  GRect dummy_frame = { {0, 0}, {0, 0} };
  for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; ++i) {
    battery_percent_layers[i] = bitmap_layer_create(dummy_frame);
    layer_add_child(window_layer, bitmap_layer_get_layer(battery_percent_layers[i]));
  }
	
  battery100_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_100BATTERY);
  GRect framea = (GRect) {
    .origin = { .x = 111, .y = 28 },
    .size = battery100_image->bounds.size
  };
  battery100_image_layer = bitmap_layer_create(framea);
  bitmap_layer_set_bitmap(battery100_image_layer, battery100_image);
  layer_add_child(window_layer, bitmap_layer_get_layer(battery100_image_layer));

 battery_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
  GRect frameb = (GRect) {
    .origin = { .x = 112, .y = 44 },
    .size = battery_image->bounds.size
  };
  battery_layer = bitmap_layer_create(frameb);
  battery_image_layer = bitmap_layer_create(frameb);
  bitmap_layer_set_bitmap(battery_image_layer, battery_image);
  layer_set_update_proc(bitmap_layer_get_layer(battery_layer), battery_layer_update_callback);

  layer_add_child(window_layer, bitmap_layer_get_layer(battery_image_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_layer));

  handle_bluetooth(bluetooth_connection_service_peek());
  update_battery(battery_state_service_peek());
   
  appStarted = true;
  	
}

static void window_unload(Window *window) {
  text_layer_destroy( time_text_layer2 );
  text_layer_destroy( utc_time_text_layer );
  text_layer_destroy( utc_date_text_layer );
  text_layer_destroy( date_text_layer );
	
}

static void init(void) {
  memset(&battery_percent_layers, 0, sizeof(battery_percent_layers));
	
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  
  const bool animated = true;
  window_stack_push(window, animated);

  // Load the UTC offset, if it exists
  time_offset = 0;
  if (persist_exists(TIME_OFFSET_PERSIST)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "loaded offset");
    time_offset = persist_read_int(TIME_OFFSET_PERSIST);
  }

  s = malloc(STR_SIZE);
  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
  app_message_register_inbox_received(app_message_inbox_received);
  app_message_open(30, 0);
	
  bluetooth_connection_service_subscribe(&handle_bluetooth);
  battery_state_service_subscribe(&update_battery);

	
}

static void deinit(void) {
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  free(s);

  fonts_unload_custom_font(time1_font);
  fonts_unload_custom_font(time2_font);
  fonts_unload_custom_font(date1_font);
  fonts_unload_custom_font(day_font);
	
  layer_remove_from_parent(bitmap_layer_get_layer(layer_conn_img));
  bitmap_layer_destroy(layer_conn_img);
  gbitmap_destroy(img_bt_connect);
  gbitmap_destroy(img_bt_disconnect);
	
  layer_remove_from_parent(bitmap_layer_get_layer(battery_layer));
  bitmap_layer_destroy(battery_layer);
  gbitmap_destroy(battery_image);
  
  layer_remove_from_parent(bitmap_layer_get_layer(battery_image_layer));
  bitmap_layer_destroy(battery_image_layer);
	
  layer_remove_from_parent(bitmap_layer_get_layer(battery100_image_layer));
  bitmap_layer_destroy(battery100_image_layer);
  gbitmap_destroy(battery100_image);
	
  layer_remove_from_parent(bitmap_layer_get_layer(background_layer));
  bitmap_layer_destroy(background_layer);
  gbitmap_destroy(background_image);
  background_image = NULL;
	
  for (int i = 0; i < TOTAL_BATTERY_PERCENT_DIGITS; i++) {
    layer_remove_from_parent(bitmap_layer_get_layer(battery_percent_layers[i]));
    gbitmap_destroy(battery_percent_image[i]);
    bitmap_layer_destroy(battery_percent_layers[i]); 
  } 
	
  window_destroy(window);

}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
