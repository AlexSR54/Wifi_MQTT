/**
 * @file wifi_sta.c
 * @brief Lab Module 1 & 2: Wi-Fi Station (STA) Implementation & Event Handling
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW FOR STUDENTS:
 * ============================================================================
 * In ESP-IDF, Wi-Fi and TCP/IP are decoupled via an Event-Driven Architecture:
 *
 *   [802.11 Radio / Wi-Fi Driver]
 *          │
 *          ├── posts WIFI_EVENT_STA_START ────────► [Default Event Loop]
 *          ├── posts WIFI_EVENT_STA_CONNECTED ────► [LwIP TCP/IP Stack (esp_netif)]
 *          │                                               │
 *          │                                        (Runs DHCP Client)
 *          │                                               │
 *          └── LwIP posts IP_EVENT_STA_GOT_IP ────► [Application Event Handler]
 *                                                          │
 *                                                   Sets WIFI_CONNECTED_BIT
 *                                                   in FreeRTOS EventGroup!
 *
 * CRITICAL LESSON:
 *   `WIFI_EVENT_STA_CONNECTED` only means the 802.11 Layer-2 link is associated.
 *   You CANNOT open sockets or make HTTP requests until `IP_EVENT_STA_GOT_IP` fires!
 */

#include "wifi_sta.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_netif.h"

static const char *TAG = "WIFI_STA";

/* Maximum number of reconnection attempts before giving up */
#define WIFI_MAX_RETRY          5

/* Maximum number of APs to store during a Wi-Fi scan */
#define DEFAULT_SCAN_LIST_SIZE  15

/* FreeRTOS Event Group to synchronize async Wi-Fi events with app_main() */
static EventGroupHandle_t s_wifi_event_group = NULL;

/* Bitmasks for our Event Group */
#define WIFI_CONNECTED_BIT      BIT0  /* Set when IP_EVENT_STA_GOT_IP fires */
#define WIFI_FAIL_BIT           BIT1  /* Set when retries exceed WIFI_MAX_RETRY */

static int s_retry_num = 0;
static bool s_core_initialized = false;

/**
 * @brief Helper to convert esp_wifi authmode enum into a human-readable string.
 */
static const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2_PSK";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3_PSK";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief Ensure NVS, Netif, and Default Event Loop are initialized once.
 */
static void ensure_core_network_init(void)
{
    if (s_core_initialized) {
        return;
    }

    /* 1. Initialize NVS (Non-Volatile Storage).
     *    Why does Wi-Fi need NVS? The ESP32 Wi-Fi driver stores RF calibration
     *    data and optional saved credentials in flash NVS partitions. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Initialize the underlying TCP/IP stack (LwIP wrapper: esp_netif) */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 3. Create the system-wide default Event Loop task */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 4. Create default Wi-Fi Station network interface instance */
    esp_netif_create_default_wifi_sta();

    /* 5. Initialize Wi-Fi driver with default configuration macro */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_core_initialized = true;
}

/**
 * @brief Module 1 Lab Function: Scan and print nearby 2.4 GHz Wi-Fi networks.
 */
void wifi_scan_networks(void)
{
    ensure_core_network_init();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Starting 802.11 active scan across all 2.4 GHz channels...");

    /* Block until scan completes (second parameter = true means blocking) */
    esp_wifi_scan_start(NULL, true);

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));

    ESP_LOGI(TAG, "Scan complete! Found %u Access Points (showing top %u):", ap_count, number);
    printf("\n+----------------------------------+------+----------+-----------------+\n");
    printf("| %-32s | %-4s | %-8s | %-15s |\n", "SSID", "CH", "RSSI", "AUTH MODE");
    printf("+----------------------------------+------+----------+-----------------+\n");
    for (int i = 0; i < number; i++) {
        printf("| %-32s | %-4d | %4d dBm | %-15s |\n",
               (char *)ap_info[i].ssid,
               ap_info[i].primary,
               ap_info[i].rssi,
               authmode_to_str(ap_info[i].authmode));
    }
    printf("+----------------------------------+------+----------+-----------------+\n\n");

    /* Stop Wi-Fi so wifi_init_sta() can cleanly configure and start it */
    ESP_ERROR_CHECK(esp_wifi_stop());
}

/**
 * @brief Asynchronous Event Handler for WIFI_EVENT and IP_EVENT.
 *
 * This callback executes inside the context of the Default Event Loop task.
 * Never put long blocking delays inside an event handler!
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    /* CASE 1: Wi-Fi driver has started in Station mode -> initiate association */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Initiating 802.11 connection...");
        esp_wifi_connect();
    }
    /* CASE 2: Association lost or failed (wrong password, AP out of range, etc.) */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected from AP (Reason Code: %d)", disconn->reason);

        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying connection to AP (%d/%d)...", s_retry_num, WIFI_MAX_RETRY);
        } else {
            /* Signal to app_main() that connection failed permanently */
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    /* CASE 3: Layer-2 Connected AND Layer-3 DHCP handshake succeeded! */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "=========================================");
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: Network Ready!");
        ESP_LOGI(TAG, "  IPv4 Address : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "  Subnet Mask  : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "  Gateway      : " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "=========================================");

        s_retry_num = 0;
        /* Unblock app_main() by setting WIFI_CONNECTED_BIT */
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Module 2 Lab Function: Connect to Wi-Fi and wait for an IP address.
 */
esp_err_t wifi_init_sta(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();
    ensure_core_network_init();

    /* Register our callback for all WIFI_EVENTs and IP_EVENT_STA_GOT_IP */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    /* Populate the wifi_config_t union for Station (sta) mode */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    /* Require at least WPA2 PSK if a password is provided */
    wifi_config.sta.threshold.authmode = (strlen(password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for Wi-Fi association and DHCP IP assignment (SSID: %s)...", ssid);

    /* Block calling task (zero CPU usage) until either CONNECTED or FAIL bit is set */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, /* Do not clear bits on exit */
        pdFALSE, /* Wait for ANY bit (OR logic), not both */
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s (RSSI: %d dBm)", ssid, wifi_get_rssi());
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s after %d retries", ssid, WIFI_MAX_RETRY);
        return ESP_FAIL;
    }

    return ESP_FAIL;
}

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

esp_err_t wifi_get_mac_str(char *mac_str_out, size_t max_len)
{
    if (!mac_str_out || max_len < 18) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(mac_str_out, max_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}
