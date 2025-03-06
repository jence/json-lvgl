// system_status.h

#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include <esp_log.h>
#include <nvs.h>
#include "nvs_flash.h"

/* ui init wait mechanism */
extern EventGroupHandle_t systemStatusEventGroup;
extern EventBits_t bits;
extern const int UI_INITIALIZED_BIT;
extern const int WIFI_CONNECTED_BIT;
extern const int WIFI_FAIL_BIT;
extern const int GOT_IP_BIT;

/* status struct */
typedef struct
{
    bool wifi;
    const char my_ip[16];
    char mac_addr[16];
    bool bt;
    bool bt_is_connected;
    bool reader;
    bool sd;
    bool reader_data_fetched;
    bool save_to_sd;
    bool nvs;
    int brightness;
    int backlight_time;
    bool ntp_time;
    bool rga_time;
    bool time_format_12hr;
} Status;

typedef struct
{
    bool panel_status;
    bool socket_message_status;
    bool bt_message_status;
    const char *socket_link;
    int socket_port;
    const char *bt_name;
    unsigned long bt_baud;
    bool rga_message;
} MessagingStatus;

extern void nvs_save_messaging_status(nvs_handle_t my_handle, MessagingStatus *status);
extern void nvs_load_messaging_status(nvs_handle_t my_handle, MessagingStatus *status);
extern void nvs_free_messaging_status(MessagingStatus *status);
// Declare a global instance of the Status structure
extern Status system_status;
extern MessagingStatus messaging_status;

/* error handling */
extern esp_err_t wifi_connect_error;
extern esp_err_t sd_card_error;
// extern esp_err_t wifi_status_error;

/* debug log*/
#define DEBUG
#ifdef DEBUG
#define __log(format, ...)                                \
    do                                                    \
    {                                                     \
        printf("Debug Log: " format "\n", ##__VA_ARGS__); \
        fflush(stdout);                                   \
    } while (0)
#else
#define __log(format, ...) // Define an empty macro for release mode
#endif

/* extern nvs handle */
extern nvs_handle_t setup_nvs_handle;

/*sleep timer and setup variables */
#define MIN_TO_MS_FACTOR (60 * 1000)
extern int32_t sleep_time_min;
extern uint32_t sleep_time_ms;

extern int32_t backlight_time_min;
extern uint32_t backlight_time_ms;

#endif // SYSTEM_STATUS_H