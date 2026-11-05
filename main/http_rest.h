/**
 * @file http_rest.h
 * @brief Lab Module 3 & 4: HTTP Client & REST API Integration (GET & POST + cJSON)
 *
 * Exposes educational functions demonstrating:
 *  - HTTP GET  : Querying external weather APIs (Open-Meteo or OpenWeatherMap)
 *                and parsing nested JSON objects using cJSON.
 *  - HTTP POST : Sending a JSON payload to a FastAPI IoT Enrollment Server
 *                and extracting the assigned device token & telemetry interval.
 */

#ifndef HTTP_REST_H
#define HTTP_REST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform an HTTP GET request to fetch live weather data and parse JSON.
 *
 * By default, queries the Open-Meteo REST API (requires NO API key, ideal for
 * classroom labs) or OpenWeatherMap API.
 *
 * @param url Full HTTP URL to query (e.g., Open-Meteo or OpenWeatherMap endpoint).
 * @return esp_err_t ESP_OK if HTTP 200 OK and JSON parsed successfully.
 */
esp_err_t http_rest_get_weather(const char *url);

/**
 * @brief Perform an HTTP POST request to enroll this ESP32 with a FastAPI server.
 *
 * Serializes a JSON body containing:
 *   {
 *     "mac_address": "AA:BB:CC:DD:EE:FF",
 *     "student_id": "student_01",
 *     "firmware_version": "v1.0.0-lab",
 *     "rssi_dbm": -52
 *   }
 * Sets "Content-Type: application/json", transmits the POST request, and parses
 * the server's JSON response (`device_token`, `assigned_mqtt_topic`, etc.).
 *
 * @param enrollment_url Full URL to the FastAPI `/api/v1/enroll` endpoint.
 * @param student_id     Identifier string for the student or team.
 * @return esp_err_t     ESP_OK on HTTP 200/201 and valid JSON response.
 */
esp_err_t http_rest_post_enrollment(const char *enrollment_url, const char *student_id);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_REST_H */
