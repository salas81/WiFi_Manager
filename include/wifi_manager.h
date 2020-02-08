
#include "wifi_credentials.h"

#ifndef WIFI_SSID
    #define WIFI_SSID       "YOUR_SSID"
#endif

#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD   "WIFI_PASSWORD"
#endif

typedef enum {
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
    FAILURE,
} wifi_connection_status_t;

typedef void (*wifi_callback_t)(wifi_connection_status_t status);

void wifi_init_sta(wifi_callback_t);