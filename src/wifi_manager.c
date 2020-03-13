#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_manager.h"

#define WIFI_CONNECT_RETRIES                3
#define WIFI_CONNECTING_BIT                 BIT0
#define WIFI_CONNECTED_BIT                  BIT1
#define WIFI_DISCONNECTED_BIT               BIT2
#define WIFI_FAIL_BIT                       BIT3



static EventGroupHandle_t s_wifi_event_group;
static const char *TAG = "WiFi Manager";
static int s_retry_num = 0;
static wifi_callback_t wifi_callback;



static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = false;
    xResult = ESP_OK;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        xResult = xEventGroupSetBitsFromISR(s_wifi_event_group, WIFI_CONNECTING_BIT, &xHigherPriorityTaskWoken);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xResult = xEventGroupSetBitsFromISR(s_wifi_event_group, WIFI_DISCONNECTED_BIT, &xHigherPriorityTaskWoken);
        if (s_retry_num < WIFI_CONNECT_RETRIES) {
            esp_wifi_connect();
            xResult = xEventGroupSetBitsFromISR(s_wifi_event_group, WIFI_CONNECTING_BIT, &xHigherPriorityTaskWoken);
            s_retry_num++;
        } else {
            xResult = xEventGroupSetBitsFromISR(s_wifi_event_group, WIFI_FAIL_BIT, &xHigherPriorityTaskWoken);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_retry_num = 0;
        xResult = xEventGroupSetBitsFromISR(s_wifi_event_group, WIFI_CONNECTED_BIT, &xHigherPriorityTaskWoken);
    }
    if( xResult != pdFAIL ) {
        portYIELD_FROM_ISR();
    }
}

static void wifi_manager_task(void *args) {
    EventBits_t bits;

    while (1) {
        bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY); 
        if (bits & WIFI_CONNECTING_BIT) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTING_BIT);
            ESP_LOGI(TAG, "connecting to ap SSID:%s password:%s", WIFI_SSID, WIFI_PASSWORD);
            wifi_callback(CONNECTING);
        } else if (bits & WIFI_CONNECTED_BIT) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", WIFI_SSID, WIFI_PASSWORD);
            wifi_callback(CONNECTED);
        } else if (bits & WIFI_FAIL_BIT) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", WIFI_SSID, WIFI_PASSWORD);
            wifi_callback(FAILURE);
        } else if (bits & WIFI_DISCONNECTED_BIT) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", WIFI_SSID, WIFI_PASSWORD);
            wifi_callback(DISCONNECTED);
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);
}

void wifi_init_sta(wifi_callback_t callback) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    wifi_callback = callback;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    xTaskCreate(wifi_manager_task, "WiFi Manager Task", 4096, NULL, 3, NULL);
}
