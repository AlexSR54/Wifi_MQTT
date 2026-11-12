/**
 * @file main.c
 * @brief Hands-On Lab Session 1: ESP32 Wi-Fi Protocol, HTTP Client & REST APIs
 *
 * ============================================================================
 * LAB OVERVIEW (3-HOUR SESSION):
 * ============================================================================
 *  - STEP 1 (Module 1): 802.11 Active Scanning (Channels, RSSI, Auth Modes)
 *  - STEP 2 (Module 2): Wi-Fi Station Association & DHCP IPv4 Acquisition
 *  - STEP 3 (Module 3): HTTP GET Request & JSON Parsing (Weather API)
 *  - STEP 4 (Module 4): HTTP POST Request & JSON Serialization (FastAPI IoT Enrollment)
 *
 * STUDENT INSTRUCTIONS:
 *  1. Update `LAB_WIFI_SSID` and `LAB_WIFI_PASS` below with the classroom Wi-Fi.
 *  2. Update `FASTAPI_SERVER_URL` with the instructor's laptop IP address.
 *  3. Toggle `LAB_ENABLE_STEP_1` .. `LAB_ENABLE_STEP_4` as instructed during class.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_sta.h"
#include "http_rest.h"

static const char *TAG = "LAB_MAIN";

/* ============================================================================
 * STUDENT CONFIGURATION SECTION
 * ============================================================================ */

/* 1. Classroom Wi-Fi Credentials (2.4 GHz network required for ESP32) */
#define LAB_WIFI_SSID           "YOUR_LAB_WIFI_SSID"
#define LAB_WIFI_PASS           "YOUR_LAB_WIFI_PASSWORD"

/* 2. Student / Team Identifier for FastAPI Device Enrollment */
#define LAB_STUDENT_ID          "team_01_alice_bob"

/* 3. Weather REST API Endpoint (GET)
 *    Option A (Recommended for Lab - No API Key Needed!): Open-Meteo Plain HTTP endpoint
 *    Option B (If students have an OpenWeatherMap API Key):
 *      "http://api.openweathermap.org/data/2.5/weather?q=Bogota&units=metric&appid=YOUR_API_KEY"
 */
#define WEATHER_API_URL         "http://api.open-meteo.com/v1/forecast?latitude=4.61&longitude=-74.08&current_weather=true"

/* 4. Instructor FastAPI IoT Enrollment Endpoint (POST)
 *    Hosted on HostingServer (eniacelectronics.cloud / 72.61.77.81:8000) */
#define FASTAPI_SERVER_URL      "http://eniacelectronics.cloud:8000/api/v1/enroll"

/* 5. Progressive Lab Step Toggles (Set to 1 to enable, 0 to skip) */
#define LAB_ENABLE_STEP_1_SCAN      1   /* Module 1: 802.11 AP Scan */
#define LAB_ENABLE_STEP_2_CONNECT   1   /* Module 2: Station Connect + DHCP */
#define LAB_ENABLE_STEP_3_GET_API   1   /* Module 3: HTTP GET Weather API */
#define LAB_ENABLE_STEP_4_POST_API  1   /* Module 4: HTTP POST FastAPI Enrollment */

/* ============================================================================
 * APPLICATION ENTRY POINT
 * ============================================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "======================================================");
    ESP_LOGI(TAG, " ESP32 Hands-On Lab: Wi-Fi & REST APIs (Session 1)    ");
    ESP_LOGI(TAG, "======================================================");

#if LAB_ENABLE_STEP_1_SCAN
    /* ------------------------------------------------------------------------
     * MODULE 1: 802.11 Discovery & Channel Inspection
     * ------------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[STEP 1] Running 802.11 Wi-Fi Scan...");
    wifi_scan_networks();
#endif

#if LAB_ENABLE_STEP_2_CONNECT
    /* ------------------------------------------------------------------------
     * MODULE 2: Wi-Fi Station Association & LwIP DHCP Handshake
     * ------------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[STEP 2] Connecting to Wi-Fi SSID: %s", LAB_WIFI_SSID);
    if (wifi_init_sta(LAB_WIFI_SSID, LAB_WIFI_PASS) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connection failed! Check SSID/Password and halt.");
        return;
    }

    char mac_str[18] = {0};
    wifi_get_mac_str(mac_str, sizeof(mac_str));
    ESP_LOGI(TAG, "Device Hardware MAC: %s | Current RSSI: %d dBm",
             mac_str, wifi_get_rssi());
#endif

#if LAB_ENABLE_STEP_3_GET_API
    /* ------------------------------------------------------------------------
     * MODULE 3: Consuming an External REST API via HTTP GET + cJSON
     * ------------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[STEP 3] Querying Weather REST API (HTTP GET)...");
    http_rest_get_weather(WEATHER_API_URL);
#endif

#if LAB_ENABLE_STEP_4_POST_API
    /* ------------------------------------------------------------------------
     * MODULE 4: IoT Device Enrollment via HTTP POST to FastAPI Backend
     * ------------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[STEP 4] Enrolling device with FastAPI Server (HTTP POST)...");
    http_rest_post_enrollment(FASTAPI_SERVER_URL, LAB_STUDENT_ID);
#endif

    /* Periodic Heartbeat Loop: Monitor RSSI and optionally re-query APIs */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "[Heartbeat] Wi-Fi RSSI: %d dBm | Free Heap: %lu bytes",
                 wifi_get_rssi(), (unsigned long)esp_get_free_heap_size());
    }
}
