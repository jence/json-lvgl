#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <lvgl.h>
#include "lvgl_helpers.h"

#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include <esp_log.h>
#include "esp_err.h"
#include "cJSON.h"

#define LED_BACKLIT_PIN GPIO_NUM_4
#define TAG "Main.c"
static lv_disp_t *lv_display = NULL;
static lv_color_t *lv_buf_1 = NULL;
static lv_color_t *lv_buf_2 = NULL;
const size_t LV_BUFFER_SIZE = LV_HOR_RES_MAX * 25; // Default to 25 rows chunk of frame

/* gui task parameters */
TaskHandle_t GUI_TASK_HANDLE = NULL;
#define GUI_TASK_STACK_SIZE 5 * 1024
#define GUI_TASK_PRIORITY 10
#define GUI_TASK_CORE 1

#define LV_TICK_PERIOD_MS 10

// dunno why
esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
esp_lcd_panel_handle_t lcd_handle = NULL;

// static lv_disp_draw_buf_t lv_disp_buf;
lv_disp_drv_t disp_drv;

extern const char _binary_data_json_start[] asm("_binary_data_json_start");
extern const char _binary_data_json_end[] asm("_binary_data_json_end");

typedef void (*event_handler_t)(lv_event_t *e);
void create_list(char *str, lv_obj_t *parent);
lv_obj_t create_label(char *str, lv_obj_t *parent);
void create_button(char *str, lv_obj_t *parent);
void create_container(char *str, lv_obj_t *parent);
void create_screen(char *str);
const char *get_lvgl_symbol(const char *symbol_name);
lv_align_t get_alignment_from_string(const char *alignment_str);

/* stack for keeping track of screens*/
lv_obj_t *screen_stack[10];
int top = -1;

void push_screen(lv_obj_t *scr)
{
    if (top < 9)
        screen_stack[++top] = scr;
}

lv_obj_t *pop_screen()
{
    return (top >= 0) ? screen_stack[top--] : NULL;
}

/*stack end here*/

void dispBacklitEn()
{
    /* Config tft display backlight signal */
    ESP_LOGI(TAG, "Configuring IO for  LCD Backlit PWM");

    configTftBacklit(1024);
    // Initialize fade service
    ESP_ERROR_CHECK(ledc_fade_func_install(0));

    ESP_LOGI(TAG, "PWM signal started on GPIO4");
}

void configTftBacklit(uint32_t duty_c)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // High speed mode
        .duty_resolution = LEDC_TIMER_10_BIT, // 10-bit duty resolution
        .timer_num = LEDC_TIMER_0,            // Timer 0
        .freq_hz = 2000,                      // Frequency of PWM signal 2khz
        .clk_cfg = LEDC_AUTO_CLK              // Auto select the source clock
    };
    // Set configuration of timer0 for high speed channels
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LED_BACKLIT_PIN, // Set GPIO4 as PWM signal output
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = duty_c, // Set duty cycle to 50%
        .hpoint = 0};
    // Set LEDC channel configuration
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void lv_tick_task(void *arg)
{
    lv_tick_inc(LV_TICK_PERIOD_MS); // 10ms
}

/*implementaion of home function that will take to the previous screen*/

void ui_home_btn_cb(lv_event_t *event)
{
    lv_scr_load_anim(pop_screen(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 50, 0, true);
}

/// agent function will create a new sceen and push it to the stack////////////////////////////////

void ui_event_agent_btn_cb(lv_event_t *e)
{

    const char *screen_str = (const char *)lv_event_get_user_data(e);
    if (screen_str)
    {
        printf("Event Triggered! Received String: %s\n", screen_str);
    }

    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_CLICKED && screen_str != NULL)
    {
        cJSON *screen_json = cJSON_Parse(screen_str);

        if (screen_json)
        {
            create_screen(screen_str);
        }
    }
}

/*end of agent function*/
static void dropdown_list_cb(lv_event_t *e)
{

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        char buf[32];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        LV_LOG_USER("Option: %s", buf);
    }
}

/**
 * Create a menu from a drop-down list and show some drop-down list features and styling
 */
void ui_settings_btn_cb(lv_event_t *e)
{
    const char *screen_str = (const char *)lv_event_get_user_data(e);
    if (screen_str)
    {
        printf("Event Triggered! Received String: %s\n", screen_str);
    }

    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_CLICKED && screen_str != NULL)
    {
        cJSON *screen_json = cJSON_Parse(screen_str);

        if (screen_json)
        {
            create_screen(screen_str);
        }
    }
}

/* funtion pointer for event callback*/
typedef struct
{
    const char *name;
    event_handler_t handler;
} event_handler_map_t;

event_handler_map_t event_handlers[] = {
    {"ui_settings_btn_cb", ui_settings_btn_cb},
    {"dropdown_list_cb", dropdown_list_cb},
    {"ui_home_btn_cb", ui_home_btn_cb},
    {"ui_event_agent_btn", ui_event_agent_btn_cb},
    {NULL, NULL} // Sentinel value to mark the end of the array
};
event_handler_t get_event_handler(const char *name)
{
    for (int i = 0; event_handlers[i].name != NULL; i++)
    {
        if (strcmp(event_handlers[i].name, name) == 0)
        {
            return event_handlers[i].handler;
        }
    }
    return NULL; // Return NULL if the handler is not found
}

lv_event_code_t get_event_code(const char *event_code)
{
    if (strcmp(event_code, "LV_EVENT_CLICKED") == 0)
    {
        return LV_EVENT_CLICKED;
    }
    else if (strcmp(event_code, "LV_EVENT_ALL") == 0)
    {
        return LV_EVENT_ALL;
    }
    if (strcmp(event_code, "LV_EVENT_LONG_PRESSED") == 0)
    {
        return LV_EVENT_LONG_PRESSED;
    }

    else
    {
        printf("Unknown alignment: %s\n", event_code);
        return LV_EVENT_ALL; // Default alignment
    }
}

/* this function will be used to get the lvgl symbol from the string passed to it*/
const char *get_lvgl_symbol(const char *symbol_name)
{
    if (strcmp(symbol_name, "audio") == 0)
    {
        return LV_SYMBOL_AUDIO;
    }
    else if (strcmp(symbol_name, "video") == 0)
    {
        return LV_SYMBOL_VIDEO;
    }
    else if (strcmp(symbol_name, "list") == 0)
    {
        return LV_SYMBOL_LIST;
    }
    else if (strcmp(symbol_name, "ok") == 0)
    {
        return LV_SYMBOL_OK;
    }
    else if (strcmp(symbol_name, "close") == 0)
    {
        return LV_SYMBOL_CLOSE;
    }
    else if (strcmp(symbol_name, "power") == 0)
    {
        return LV_SYMBOL_POWER;
    }
    else if (strcmp(symbol_name, "settings") == 0)
    {
        return LV_SYMBOL_SETTINGS;
    }
    else if (strcmp(symbol_name, "home") == 0)
    {
        return LV_SYMBOL_HOME;
    }
    else if (strcmp(symbol_name, "download") == 0)
    {
        return LV_SYMBOL_DOWNLOAD;
    }
    else if (strcmp(symbol_name, "drive") == 0)
    {
        return LV_SYMBOL_DRIVE;
    }
    else if (strcmp(symbol_name, "refresh") == 0)
    {
        return LV_SYMBOL_REFRESH;
    }
    else if (strcmp(symbol_name, "mute") == 0)
    {
        return LV_SYMBOL_MUTE;
    }
    else if (strcmp(symbol_name, "volume_mid") == 0)
    {
        return LV_SYMBOL_VOLUME_MID;
    }
    else if (strcmp(symbol_name, "volume_max") == 0)
    {
        return LV_SYMBOL_VOLUME_MAX;
    }
    else if (strcmp(symbol_name, "image") == 0)
    {
        return LV_SYMBOL_IMAGE;
    }
    else if (strcmp(symbol_name, "edit") == 0)
    {
        return LV_SYMBOL_EDIT;
    }
    else if (strcmp(symbol_name, "prev") == 0)
    {
        return LV_SYMBOL_PREV;
    }
    else if (strcmp(symbol_name, "play") == 0)
    {
        return LV_SYMBOL_PLAY;
    }
    else if (strcmp(symbol_name, "pause") == 0)
    {
        return LV_SYMBOL_PAUSE;
    }
    else if (strcmp(symbol_name, "stop") == 0)
    {
        return LV_SYMBOL_STOP;
    }
    else if (strcmp(symbol_name, "next") == 0)
    {
        return LV_SYMBOL_NEXT;
    }
    else if (strcmp(symbol_name, "eject") == 0)
    {
        return LV_SYMBOL_EJECT;
    }
    else if (strcmp(symbol_name, "left") == 0)
    {
        return LV_SYMBOL_LEFT;
    }
    else if (strcmp(symbol_name, "right") == 0)
    {
        return LV_SYMBOL_RIGHT;
    }
    else if (strcmp(symbol_name, "plus") == 0)
    {
        return LV_SYMBOL_PLUS;
    }
    else if (strcmp(symbol_name, "minus") == 0)
    {
        return LV_SYMBOL_MINUS;
    }
    else if (strcmp(symbol_name, "eye_open") == 0)
    {
        return LV_SYMBOL_EYE_OPEN;
    }
    else if (strcmp(symbol_name, "eye_close") == 0)
    {
        return LV_SYMBOL_EYE_CLOSE;
    }
    else if (strcmp(symbol_name, "warning") == 0)
    {
        return LV_SYMBOL_WARNING;
    }
    else if (strcmp(symbol_name, "shuffle") == 0)
    {
        return LV_SYMBOL_SHUFFLE;
    }
    else if (strcmp(symbol_name, "up") == 0)
    {
        return LV_SYMBOL_UP;
    }
    else if (strcmp(symbol_name, "down") == 0)
    {
        return LV_SYMBOL_DOWN;
    }
    else if (strcmp(symbol_name, "loop") == 0)
    {
        return LV_SYMBOL_LOOP;
    }
    else if (strcmp(symbol_name, "directory") == 0)
    {
        return LV_SYMBOL_DIRECTORY;
    }
    else if (strcmp(symbol_name, "upload") == 0)
    {
        return LV_SYMBOL_UPLOAD;
    }
    else if (strcmp(symbol_name, "call") == 0)
    {
        return LV_SYMBOL_CALL;
    }
    else if (strcmp(symbol_name, "cut") == 0)
    {
        return LV_SYMBOL_CUT;
    }
    else if (strcmp(symbol_name, "copy") == 0)
    {
        return LV_SYMBOL_COPY;
    }
    else if (strcmp(symbol_name, "save") == 0)
    {
        return LV_SYMBOL_SAVE;
    }
    else if (strcmp(symbol_name, "charge") == 0)
    {
        return LV_SYMBOL_CHARGE;
    }
    else if (strcmp(symbol_name, "bell") == 0)
    {
        return LV_SYMBOL_BELL;
    }
    else if (strcmp(symbol_name, "keyboard") == 0)
    {
        return LV_SYMBOL_KEYBOARD;
    }
    else if (strcmp(symbol_name, "gps") == 0)
    {
        return LV_SYMBOL_GPS;
    }
    else if (strcmp(symbol_name, "file") == 0)
    {
        return LV_SYMBOL_FILE;
    }
    else if (strcmp(symbol_name, "wifi") == 0)
    {
        return LV_SYMBOL_WIFI;
    }
    else if (strcmp(symbol_name, "battery_full") == 0)
    {
        return LV_SYMBOL_BATTERY_FULL;
    }
    else if (strcmp(symbol_name, "battery_3") == 0)
    {
        return LV_SYMBOL_BATTERY_3;
    }
    else if (strcmp(symbol_name, "battery_2") == 0)
    {
        return LV_SYMBOL_BATTERY_2;
    }
    else if (strcmp(symbol_name, "battery_1") == 0)
    {
        return LV_SYMBOL_BATTERY_1;
    }
    else if (strcmp(symbol_name, "battery_empty") == 0)
    {
        return LV_SYMBOL_BATTERY_EMPTY;
    }
    else if (strcmp(symbol_name, "usb") == 0)
    {
        return LV_SYMBOL_USB;
    }
    else if (strcmp(symbol_name, "bluetooth") == 0)
    {
        return LV_SYMBOL_BLUETOOTH;
    }
    else if (strcmp(symbol_name, "trash") == 0)
    {
        return LV_SYMBOL_TRASH;
    }
    else if (strcmp(symbol_name, "backspace") == 0)
    {
        return LV_SYMBOL_BACKSPACE;
    }
    else if (strcmp(symbol_name, "sd_card") == 0)
    {
        return LV_SYMBOL_SD_CARD;
    }
    else if (strcmp(symbol_name, "new_line") == 0)
    {
        return LV_SYMBOL_NEW_LINE;
    }
    else if (strcmp(symbol_name, "dummy") == 0)
    {
        return LV_SYMBOL_DUMMY;
    }
    else
    {
        return NULL; // Return NULL if the symbol is not found
    }
}
/* this function will be used to get the alignment from the string passed to it*/
lv_align_t get_alignment_from_string(const char *alignment_str)
{
    if (strcmp(alignment_str, "center") == 0)
    {
        return LV_ALIGN_CENTER;
    }
    else if (strcmp(alignment_str, "top_mid") == 0)
    {
        return LV_ALIGN_TOP_MID;
    }
    else if (strcmp(alignment_str, "bottom_mid") == 0)
    {
        return LV_ALIGN_BOTTOM_MID;
    }
    else if (strcmp(alignment_str, "top_left") == 0)
    {
        return LV_ALIGN_TOP_LEFT;
    }
    else if (strcmp(alignment_str, "top_right") == 0)
    {
        return LV_ALIGN_TOP_RIGHT;
    }
    else if (strcmp(alignment_str, "bottom_right") == 0)
    {
        return LV_ALIGN_BOTTOM_RIGHT;
    }
    else if (strcmp(alignment_str, "bottom_left") == 0)
    {
        return LV_ALIGN_BOTTOM_LEFT;
    }
    else if (strcmp(alignment_str, "left_mid"))
    {
        return LV_ALIGN_LEFT_MID;
    }
    else
    {
        printf("Unknown alignment: %s\n", alignment_str);
        return LV_ALIGN_CENTER; // Default alignment
    }
}

////this will be used to get the flex alignment from the string passed to it

lv_flex_align_t get_flex_alignment_from_string(const char *alignment_str)
{
    if (strcmp(alignment_str, "start") == 0)
    {
        return LV_FLEX_ALIGN_START;
    }
    else if (strcmp(alignment_str, "end") == 0)
    {
        return LV_FLEX_ALIGN_END;
    }
    else if (strcmp(alignment_str, "space_between") == 0)
    {
        return LV_FLEX_ALIGN_SPACE_BETWEEN;
    }
    else
    {
        printf("Unknown alignment: %s\n", alignment_str);
        return LV_FLEX_ALIGN_CENTER; // Default alignment
    }
}

////this will be used to get the flex flow from the string passed to it
lv_flex_flow_t get_flex_flow_from_string(const char *flow_str)
{
    if (strcmp(flow_str, "row") == 0)
    {
        return LV_FLEX_FLOW_ROW;
    }
    else if (strcmp(flow_str, "reverse") == 0)
    {
        return LV_FLEX_FLOW_ROW_REVERSE;
    }
    else
    {
        printf("Unknown alignment: %s\n", flow_str);
        return LV_FLEX_FLOW_ROW; // Default alignment
    }
}

////this will be used to get the flag from the string passed to it
lv_obj_flag_t get_flag_from_string(const char *flag_str)
{
    if (strcmp(flag_str, "scrollable") == 0)
    {
        return LV_OBJ_FLAG_SCROLLABLE;
    }
    else if (strcmp(flag_str, "clickable") == 0)
    {
        return LV_OBJ_FLAG_CLICKABLE;
    }
    else if (strcmp(flag_str, "scroll_on_focus") == 0)
    {
        return LV_OBJ_FLAG_SCROLL_ON_FOCUS;
    }
    else if (strcmp(flag_str, "hidden") == 0)
    {
        return LV_OBJ_FLAG_HIDDEN;
    }
    else if (strcmp(flag_str, "checkable") == 0)
    {
        return LV_OBJ_FLAG_CHECKABLE;
    }
    else
    {
        printf("Unknown flag: %s\n", flag_str);
        return 0; // Default: No flag
    }
}

void create_list(char *list_json_str, lv_obj_t *parent)
{
    cJSON *list_json = cJSON_Parse(list_json_str);
    if (list_json == NULL)
    {
        printf("List JSON is NULL\n");
        return;
    }

    /*Create a normal drop down list*/
    lv_obj_t *list = lv_dropdown_create(parent);

    if (cJSON_GetObjectItem(list_json, "align"))
    {
        const char *alignment = cJSON_GetObjectItemCaseSensitive(list_json, "align")->valuestring;
        lv_obj_set_align(list, get_alignment_from_string(alignment));
    }
    if (cJSON_GetObjectItem(list_json, "options"))
    {
        const char *options = cJSON_GetObjectItemCaseSensitive(list_json, "options")->valuestring;
        lv_dropdown_set_options(list, options);
    }
    if (cJSON_GetObjectItemCaseSensitive(list_json, "name") &&
        cJSON_GetObjectItemCaseSensitive(list_json, "code") &&
        cJSON_GetObjectItemCaseSensitive(list_json, "@screen"))
    {
        const char *event_name = cJSON_GetObjectItemCaseSensitive(list_json, "name")->valuestring;
        const char *event_code = cJSON_GetObjectItemCaseSensitive(list_json, "code")->valuestring;
        cJSON *screen_json = cJSON_GetObjectItemCaseSensitive(list_json, "@screen");
        char *screen = cJSON_Print(screen_json);

        lv_obj_add_event_cb(list, get_event_handler(event_name), get_event_code(event_code), (void *)screen);
    }
}
/*this function will create a label from the json string passed to it*/
lv_obj_t create_label(char *label_json_str, lv_obj_t *parent)
{
    cJSON *label_json = cJSON_Parse(label_json_str);

    if (label_json == NULL)
    {
        printf("Label JSON is NULL\n");
        return;
    }
    lv_obj_t *label = lv_label_create(parent);

    if (cJSON_GetObjectItem(label_json, "width"))
    {
        int width = cJSON_GetObjectItemCaseSensitive(label_json, "width")->valueint;
        lv_obj_set_width(label, LV_SIZE_CONTENT);
    }
    if (cJSON_GetObjectItem(label_json, "height"))
    {
        int height = cJSON_GetObjectItemCaseSensitive(label_json, "text")->valueint;
        lv_obj_set_height(label, LV_SIZE_CONTENT); /// 1
    }
    if (cJSON_GetObjectItem(label_json, "text"))
    {
        const char *text = cJSON_GetObjectItemCaseSensitive(label_json, "text")->valuestring;
        lv_label_set_text(label, text);
    }

    if (cJSON_GetObjectItem(label_json, "text_color"))
    {
        const char *text_color = cJSON_GetObjectItemCaseSensitive(label_json, "text_color")->valuestring;
        unsigned int hexValue_text_color = strtol(text_color, NULL, 16);
        lv_obj_set_style_text_color(label, lv_color_hex(hexValue_text_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(label_json, "text_opa"))
    {
        int text_opa = cJSON_GetObjectItemCaseSensitive(label_json, "text_opa")->valueint;
        lv_obj_set_style_text_opa(label, text_opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (cJSON_GetObjectItem(label_json, "text_font"))
    {
        const char *text_font = cJSON_GetObjectItemCaseSensitive(label_json, "text_font")->valuestring;
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (cJSON_GetObjectItem(label_json, "icon"))
    {
        const char *icon = cJSON_GetObjectItemCaseSensitive(label_json, "icon")->valuestring;
        lv_label_set_text(label, get_lvgl_symbol(icon));
    }
    if (cJSON_GetObjectItem(label_json, "clear_flag"))
    {
        const char *clear_flag = cJSON_GetObjectItemCaseSensitive(label_json, "clear_flag")->valuestring;
        lv_obj_clear_flag(label, get_flag_from_string(clear_flag)); /// Flags
    }
    if (cJSON_GetObjectItem(label_json, "add_flag"))
    {
        const char *add_flag = cJSON_GetObjectItemCaseSensitive(label_json, "add_flag")->valuestring;
        lv_obj_add_flag(label, get_flag_from_string(add_flag)); /// Flags
    }
    if (cJSON_GetObjectItem(label_json, "align"))
    {
        const char *alignment = cJSON_GetObjectItemCaseSensitive(label_json, "align")->valuestring;
        lv_obj_set_align(label, get_alignment_from_string(alignment));
    }
}
/* this function will create a button from the json string passed to it*/
void create_button(char *button_json_str, lv_obj_t *parent)
{
    // Parse button properties
    cJSON *button_json = cJSON_Parse(button_json_str);
    printf("Button JSON: %s\n", button_json_str);
    if (button_json == NULL)
    {
        printf("Button JSON is NULL\n");
        return;
    }
    lv_obj_t *button = lv_btn_create(parent);

    if (cJSON_GetObjectItem(button_json, "name"))
    {
        const char *name = cJSON_GetObjectItemCaseSensitive(button_json, "name")->valuestring;
    }
    if (cJSON_GetObjectItem(button_json, "width"))
    {
        int width = cJSON_GetObjectItemCaseSensitive(button_json, "width")->valueint;
        lv_obj_set_width(button, lv_pct(width));
    }
    else
    {
        lv_obj_set_width(button, 20);
    }
    if (cJSON_GetObjectItem(button_json, "height"))
    {
        int height = cJSON_GetObjectItemCaseSensitive(button_json, "height")->valueint;
        lv_obj_set_height(button, lv_pct(height));
    }
    else
    {
        lv_obj_set_height(button, 20);
    }
    if (cJSON_GetObjectItem(button_json, "x"))
    {
        int x = cJSON_GetObjectItemCaseSensitive(button_json, "x")->valueint;
        lv_obj_set_x(button, lv_pct(x));
    }
    if (cJSON_GetObjectItem(button_json, "y"))
    {
        int y = cJSON_GetObjectItemCaseSensitive(button_json, "y")->valueint;
        lv_obj_set_y(button, lv_pct(y));
    }
    if (cJSON_GetObjectItem(button_json, "align"))
    {
        const char *alignment = cJSON_GetObjectItemCaseSensitive(button_json, "align")->valuestring;
        lv_obj_set_align(button, get_alignment_from_string(alignment));
    }
    if (cJSON_GetObjectItem(button_json, "clear_flag"))
    {
        const char *clear_flag = cJSON_GetObjectItemCaseSensitive(button_json, "clear_flag")->valuestring;
        lv_obj_clear_flag(button, get_flag_from_string(clear_flag)); /// Flags
    }
    if (cJSON_GetObjectItem(button_json, "add_flag"))
    {
        const char *add_flag = cJSON_GetObjectItemCaseSensitive(button_json, "add_flag")->valuestring;
        lv_obj_add_flag(button, get_flag_from_string(add_flag)); /// Flags
    }

    if (cJSON_GetObjectItem(button_json, "radius"))
    {
        int radius = cJSON_GetObjectItemCaseSensitive(button_json, "radius")->valueint;
        lv_obj_set_style_radius(button, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(button_json, "bg_color"))
    {
        const char *bg_color = cJSON_GetObjectItemCaseSensitive(button_json, "bg_color")->valuestring;
        unsigned int hexValue_bg_color = strtol(bg_color, NULL, 16);
        lv_obj_set_style_bg_color(button, lv_color_hex(hexValue_bg_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(button_json, "bg_opa"))
    {
        int bg_opa = cJSON_GetObjectItemCaseSensitive(button_json, "bg_opa")->valueint;
        lv_obj_set_style_bg_opa(button, bg_opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(button_json, "border_color"))
    {
        const char *border_color = cJSON_GetObjectItemCaseSensitive(button_json, "border_color")->valuestring;
        unsigned int hexValue_border_color = strtol(border_color, NULL, 16);
        lv_obj_set_style_border_color(button, lv_color_hex(hexValue_border_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(button_json, "border_opa"))
    {
        int border_opa = cJSON_GetObjectItemCaseSensitive(button_json, "border_opa")->valueint;
        lv_obj_set_style_border_opa(button, border_opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(button_json, "border_width"))
    {
        int border_width = cJSON_GetObjectItemCaseSensitive(button_json, "border_width")->valueint;
        lv_obj_set_style_border_width(button, border_width, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (cJSON_GetObjectItem(button_json, "click_area"))
    {
        int click_area = cJSON_GetObjectItemCaseSensitive(button_json, "click_area")->valueint;
        lv_obj_set_ext_click_area(button, click_area);
    }
    if (cJSON_GetObjectItem(button_json, "event"))
    {
        cJSON *event_json = cJSON_GetObjectItem(button_json, "event");
        if (event_json == NULL)
        {
            printf("No event occured");
            return;
        }
        if (cJSON_GetObjectItemCaseSensitive(event_json, "name") &&
            cJSON_GetObjectItemCaseSensitive(event_json, "code") &&
            cJSON_GetObjectItemCaseSensitive(event_json, "@screen"))
        {
            const char *event_name = cJSON_GetObjectItemCaseSensitive(event_json, "name")->valuestring;
            const char *event_code = cJSON_GetObjectItemCaseSensitive(event_json, "code")->valuestring;
            cJSON *screen_json = cJSON_GetObjectItemCaseSensitive(event_json, "@screen");
            char *screen = cJSON_Print(screen_json);

            lv_obj_add_event_cb(button, get_event_handler(event_name), get_event_code(event_code), (void *)screen);
        }
    }

    // Iterate through the JSON object to get key names
    cJSON *item = button_json->child;
    while (item)
    {
        char *key = item->string;
        if (strstr(key, "label") != NULL)
        {
            char *label_json = cJSON_Print(item);
            create_label(label_json, button);
        }

        item = item->next; // Move to the next item
    }
}

//// this function will create a container from the json string passed to it
void create_container(char *container_json_str, lv_obj_t *parent)
{
    // Parse the JSON string
    cJSON *container_json = cJSON_Parse(container_json_str);
    if (container_json == NULL)
    {
        printf("Error parsing JSON\n");
        return;
    }

    // container

    lv_obj_t *ui_container = lv_obj_create(parent);

    if (cJSON_GetObjectItem(container_json, "align"))
    {
        char *alignment = cJSON_GetObjectItemCaseSensitive(container_json, "align")->valuestring;
        lv_obj_set_align(ui_container, get_alignment_from_string(alignment));
    }
    if (cJSON_GetObjectItem(container_json, "flex_flow"))
    {
        char *flex_flow = cJSON_GetObjectItemCaseSensitive(container_json, "flex_flow")->valuestring;
        lv_obj_set_flex_flow(ui_container, get_flex_flow_from_string(flex_flow));
    }
    if (cJSON_GetObjectItem(container_json, "clear_flag"))
    {
        char *clear_flag = cJSON_GetObjectItemCaseSensitive(container_json, "clear_flag")->valuestring;
        lv_obj_clear_flag(ui_container, get_flag_from_string(clear_flag));
    }
    if (cJSON_GetObjectItem(container_json, "flex_align"))
    {
        char *flex_align = cJSON_GetObjectItemCaseSensitive(container_json, "flex_align")->valuestring;
        lv_obj_set_flex_align(ui_container, get_flex_alignment_from_string(flex_align), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    if (cJSON_GetObjectItem(container_json, "style"))
    {
        cJSON *style_json = cJSON_GetObjectItem(container_json, "style");
        static lv_style_t container_style;
        lv_style_init(&container_style);

        if (cJSON_GetObjectItem(style_json, "width"))
        {
            int width = cJSON_GetObjectItemCaseSensitive(style_json, "width")->valueint;
            lv_style_set_width(&container_style, width);
        }
        if (cJSON_GetObjectItem(style_json, "height"))
        {
            int height = cJSON_GetObjectItemCaseSensitive(style_json, "height")->valueint;
            lv_style_set_height(&container_style, height);
        }
        if (cJSON_GetObjectItem(style_json, "bg_color"))
        {
            char *bg_color = cJSON_GetObjectItemCaseSensitive(style_json, "bg_color")->valuestring;
            unsigned int hexValue_bg_color = strtol(bg_color, NULL, 16);
            lv_style_set_bg_color(&container_style, lv_color_hex(hexValue_bg_color));
        }
        if (cJSON_GetObjectItem(style_json, "border_width"))
        {
            int border_width = cJSON_GetObjectItemCaseSensitive(style_json, "border_width")->valueint;
            lv_style_set_border_width(&container_style, border_width);
        }
        if (cJSON_GetObjectItem(style_json, "pad_all"))
        {
            int pad_all = cJSON_GetObjectItemCaseSensitive(style_json, "pad_all")->valueint;
            lv_style_set_pad_all(&container_style, pad_all);
        }
        if (cJSON_GetObjectItem(style_json, "radius"))
        {
            int radius = cJSON_GetObjectItemCaseSensitive(style_json, "radius")->valueint;
            lv_style_set_radius(&container_style, radius);
        }

        lv_obj_add_style(ui_container, &container_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Iterate through the JSON object to get key names
    cJSON *item = container_json->child;
    while (item)
    {

        char *key = item->string;
        if (strstr(key, "container") != NULL)
        {
            char *c_json = cJSON_Print(item);
            create_container(c_json, ui_container);
        }
        else if (strstr(key, "button") != NULL)
        {
            char *button_json = cJSON_Print(item);
            printf("container button %s\n", button_json);
            create_button(button_json, ui_container);
        }
        else if (strstr(key, "label") != NULL)
        {
            char *label_json = cJSON_Print(item);
            create_label(label_json, ui_container);
        }
        else if (strstr(key, "list") != NULL)
        {
            char *list_json = cJSON_Print(item);
            create_list(list_json, ui_container);
        }
        item = item->next; // Move to the next item
    }
}

///// this function will create a screen from the json string passed to it
void create_screen(char *screen_json_str)
{
    // Parse the JSON string
    cJSON *screen_json = cJSON_Parse(screen_json_str);
    if (screen_json == NULL)
    {
        printf("Error parsing JSON\n");
        return;
    }

    // store the screen object
    push_screen(lv_scr_act());

    lv_obj_t *ui_screen = lv_obj_create(NULL);
    lv_scr_load(ui_screen);
    lv_obj_clear_flag(ui_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Iterate through the JSON object to get key names
    cJSON *item = screen_json->child;
    while (item)
    {
        char *key = item->string;
        if (strstr(key, "container") != NULL)
        {
            char *container_json = cJSON_Print(item);
            create_container(container_json, ui_screen);
        }
        else if (strstr(key, "button") != NULL)
        {
            char *button_json = cJSON_Print(item);
            printf("container button %s\n", button_json);
            create_button(button_json, ui_screen);
        }
        else if (strstr(key, "label") != NULL)
        {
            char *label_json = cJSON_Print(item);
            create_label(label_json, ui_screen);
        }
        else if (strstr(key, "list") != NULL)
        {
            char *list_json = cJSON_Print(item);
            create_list(list_json, ui_screen);
        }

        printf("Object Key: %s\n", item->string); // Print the key (name of the object)

        item = item->next; // Move to the next item
    }
}

///// this function will read the json file and parse it to create the lvgl components
char *read_json_file()
{
    // Calculate the size of the embedded file.
    size_t file_size = _binary_data_json_end - _binary_data_json_start;

    // Allocate a buffer with one extra byte for the null terminator.
    char *json_buffer = (char *)malloc(file_size + 1);
    char *jsonString = "";
    if (json_buffer == NULL)
    {
        printf("Memory allocation failed\n");
        return " NULL ";
    }

    // Copy the embedded file into the buffer.
    memcpy(json_buffer, _binary_data_json_start, file_size);
    json_buffer[file_size] = '\0'; // Null-terminate the string

    // Now parse the JSON string using cJSON.
    cJSON *json = cJSON_Parse(json_buffer);
    if (json == NULL)
    {
        printf("Error parsing JSON\n");
    }
    else
    {
        // For demonstration, print the parsed JSON in a formatted way.
        char *printed_json = cJSON_Print(json);
        jsonString = cJSON_Print(json);

        if (printed_json != NULL)
        {
            // get_json_String(printed_json);

            printf("Embedded JSON content:\n%s\n", printed_json);
            free(printed_json);
        }
        cJSON_Delete(json);
    }

    free(json_buffer);
    return jsonString;
}

void parse_json(char *json_str)
{
    // Parse the JSON string
    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL)
    {
        printf("Error parsing JSON\n");
        return;
    }

    // Iterate through the JSON object to get key names
    cJSON *item = json->child;
    while (item)
    {
        char *key = item->string;
        if (strstr(key, "screen") != NULL)
        {
            char *screen_json = cJSON_Print(item);
            create_screen(screen_json);
        }

        printf("Object Key: %s\n", item->string); // Print the key (name of the object)

        item = item->next; // Move to the next item
    }

    // Clean up
    cJSON_Delete(json);
}

static void ui_task(void *pvParameter)
{
    dispBacklitEn();
    lv_init();
    lvgl_driver_init();
    /*----OUTPUT DEVICE-----*/
    // Allocate buffer single or double for faster processing - requires "esp_heap_caps.h"
    lv_buf_1 = (lv_color_t *)heap_caps_malloc(LV_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_buf_2 = (lv_color_t *)heap_caps_malloc(LV_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);

    // define[could be glbal] and initialize the buffer
    lv_disp_draw_buf_t lv_disp_buf;
    lv_disp_draw_buf_init(&lv_disp_buf, lv_buf_1, lv_buf_2, LV_BUFFER_SIZE);

    // define[could be global] and Initialize the display driver (struct)
    // lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    // define the resolution
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;

    // Register the flush callback for lvgl to access
    disp_drv.flush_cb = disp_driver_flush; // this is configured to call ili9488 flush

    // Register the buffer pointer for lvgl to access
    disp_drv.draw_buf = &lv_disp_buf;

    // dunno why
    disp_drv.user_data = lcd_handle;

    // Register the display driver struct
    lv_display = lv_disp_drv_register(&disp_drv);

    // initialize the input drivers
    lv_indev_drv_t my_indev_drv;
    lv_indev_drv_init(&my_indev_drv);
    my_indev_drv.read_cb = touch_driver_read;  // Set the read callback function
    my_indev_drv.type = LV_INDEV_TYPE_POINTER; // Set the input device type (e.g., pointer)
    lv_indev_drv_register(&my_indev_drv);      // Register the input device driver

    // dunno why
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"};
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    //////////////////////////////////////////////JSON Parsing STARTS HERE/////////////////////
    char *json_str = read_json_file();
    parse_json(json_str);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(LV_TICK_PERIOD_MS));
        // take this semaphore to call lvgl related function on success
        lv_task_handler();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{

    xTaskCreatePinnedToCore(ui_task, "gui", GUI_TASK_STACK_SIZE, NULL, GUI_TASK_PRIORITY, &GUI_TASK_HANDLE, GUI_TASK_CORE);
    /*INPUT DEVICE*/
}
