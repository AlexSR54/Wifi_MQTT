/**
 * @file http_rest.c
 * @brief Lab Module 3 & 4: HTTP Client & REST API Implementation
 *
 * ============================================================================
 * ARCHITECTURE OVERVIEW FOR STUDENTS:
 * ============================================================================
 * 1. HTTP Request/Response Lifecycle (`esp_http_client`):
 *    - HTTP is a stateless, client-initiated protocol over TCP (Port 80) or TLS (Port 443).
 *    - Because TCP streams arrive in fragments (chunks), `esp_http_client` triggers
 *      `HTTP_EVENT_ON_DATA` potentially MULTIPLE times for a single HTTP response!
 *    - We use a response accumulator buffer (`http_resp_buf_t`) passed via `user_data`
 *      to reassemble the complete JSON payload before handing it to `cJSON_Parse()`.
 *
 * 2. Memory Safety with `cJSON`:
 *    - `cJSON_Parse()` allocates a tree of nodes on the heap (`malloc`).
 *    - ALWAYS call `cJSON_Delete(root)` before returning to prevent memory leaks!
 *    - `cJSON_PrintUnformatted()` allocates a JSON string on the heap.
 *    - ALWAYS call `free(json_str)` after sending it!
 */

#include "http_rest.h"
#include "wifi_sta.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "HTTP_REST";

/* Maximum HTTP response body size we will buffer in RAM (4 KB) */
#define MAX_HTTP_OUTPUT_BUFFER 4096

/**
 * @brief Context structure passed into the HTTP event handler via `user_data`
 *        to safely reassemble chunked TCP/HTTP payloads.
 */
typedef struct {
    char   buffer[MAX_HTTP_OUTPUT_BUFFER];
    size_t len;
} http_resp_buf_t;

/**
 * @brief Event callback invoked by `esp_http_client_perform()` during the HTTP lifecycle.
 */
static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    http_resp_buf_t *resp = (http_resp_buf_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED (TCP 3-way handshake complete)");
            if (resp) {
                resp->len = 0;
                resp->buffer[0] = '\0';
            }
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "Header: %s: %s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            /* CRITICAL LESSON:
             * Even a 500-byte JSON response may arrive across two TCP segments.
             * Always append (`memcpy` at `resp->len`) and guard against buffer overflow! */
            if (resp && evt->data_len > 0) {
                size_t copy_len = evt->data_len;
                if (resp->len + copy_len >= MAX_HTTP_OUTPUT_BUFFER) {
                    copy_len = (MAX_HTTP_OUTPUT_BUFFER - 1) - resp->len;
                    ESP_LOGW(TAG, "HTTP response buffer full! Truncating incoming data.");
                }
                if (copy_len > 0) {
                    memcpy(resp->buffer + resp->len, evt->data, copy_len);
                    resp->len += copy_len;
                    resp->buffer[resp->len] = '\0'; /* Always keep null-terminated for cJSON */
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH (Total bytes received: %zu)",
                     resp ? resp->len : 0);
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Helper to parse both Open-Meteo and OpenWeatherMap JSON schemas.
 *
 * Supports:
 *  - Open-Meteo format:
 *      { "current_weather": { "temperature": 22.5, "windspeed": 10.2 } }
 *  - OpenWeatherMap format:
 *      { "name": "Bogota", "main": { "temp": 22.5, "humidity": 65 } }
 */
static esp_err_t parse_weather_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON Parse Error before: %s", error_ptr ? error_ptr : "unknown");
        return ESP_FAIL;
    }

    /* Check Schema A: Open-Meteo ("current_weather" object) */
    cJSON *current_weather = cJSON_GetObjectItemCaseSensitive(root, "current_weather");
    if (cJSON_IsObject(current_weather)) {
        cJSON *temp = cJSON_GetObjectItemCaseSensitive(current_weather, "temperature");
        cJSON *wind = cJSON_GetObjectItemCaseSensitive(current_weather, "windspeed");
        cJSON *code = cJSON_GetObjectItemCaseSensitive(current_weather, "weathercode");

        ESP_LOGI(TAG, "-----------------------------------------");
        ESP_LOGI(TAG, "Parsed Weather Telemetry (Open-Meteo API):");
        if (cJSON_IsNumber(temp)) {
            ESP_LOGI(TAG, "  Temperature : %.1f C", temp->valuedouble);
        }
        if (cJSON_IsNumber(wind)) {
            ESP_LOGI(TAG, "  Wind Speed  : %.1f km/h", wind->valuedouble);
        }
        if (cJSON_IsNumber(code)) {
            ESP_LOGI(TAG, "  WMO Code    : %d", code->valueint);
        }
        ESP_LOGI(TAG, "-----------------------------------------");
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* Check Schema B: OpenWeatherMap ("main" object) */
    cJSON *main_obj = cJSON_GetObjectItemCaseSensitive(root, "main");
    if (cJSON_IsObject(main_obj)) {
        cJSON *city = cJSON_GetObjectItemCaseSensitive(root, "name");
        cJSON *temp = cJSON_GetObjectItemCaseSensitive(main_obj, "temp");
        cJSON *hum  = cJSON_GetObjectItemCaseSensitive(main_obj, "humidity");

        ESP_LOGI(TAG, "-----------------------------------------");
        ESP_LOGI(TAG, "Parsed Weather Telemetry (OpenWeatherMap):");
        if (cJSON_IsString(city) && city->valuestring) {
            ESP_LOGI(TAG, "  City        : %s", city->valuestring);
        }
        if (cJSON_IsNumber(temp)) {
            ESP_LOGI(TAG, "  Temperature : %.1f C", temp->valuedouble);
        }
        if (cJSON_IsNumber(hum)) {
            ESP_LOGI(TAG, "  Humidity    : %d %%", hum->valueint);
        }
        ESP_LOGI(TAG, "-----------------------------------------");
        cJSON_Delete(root);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unrecognized Weather JSON schema. Raw JSON:\n%s", json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Module 3 Lab Function: Perform an HTTP GET Request and parse JSON.
 */
esp_err_t http_rest_get_weather(const char *url)
{
    /* Allocate response buffer on the heap to keep FreeRTOS task stack usage low */
    http_resp_buf_t *resp_buf = calloc(1, sizeof(http_resp_buf_t));
    if (!resp_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP response buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_client_event_handler,
        .user_data = resp_buf,
        .timeout_ms = 8000,
    };

    ESP_LOGI(TAG, "Sending HTTP GET request to: %s", url);
    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Execute the synchronous HTTP transaction (DNS -> TCP -> HTTP Send -> HTTP Recv) */
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int64_t content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP GET Status = %d, Content-Length = %lld bytes",
                 status_code, content_len);

        if (status_code == 200) {
            ESP_LOGI(TAG, "Raw JSON Payload:\n%s", resp_buf->buffer);
            err = parse_weather_json(resp_buf->buffer);
        } else {
            ESP_LOGW(TAG, "Server returned non-200 status (%d). Body: %s",
                     status_code, resp_buf->buffer);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    /* Always clean up client sockets and heap buffers */
    esp_http_client_cleanup(client);
    free(resp_buf);
    return err;
}

/**
 * @brief Module 4 Lab Function: Perform an HTTP POST Request for IoT Device Enrollment.
 */
esp_err_t http_rest_post_enrollment(const char *enrollment_url, const char *student_id)
{
    http_resp_buf_t *resp_buf = calloc(1, sizeof(http_resp_buf_t));
    if (!resp_buf) {
        return ESP_ERR_NO_MEM;
    }

    /* 1. Gather hardware telemetry to include in the enrollment payload */
    char mac_str[18] = {0};
    wifi_get_mac_str(mac_str, sizeof(mac_str));
    int current_rssi = wifi_get_rssi();

    /* 2. Construct the JSON request body using cJSON */
    cJSON *req_root = cJSON_CreateObject();
    cJSON_AddStringToObject(req_root, "mac_address", mac_str);
    cJSON_AddStringToObject(req_root, "student_id", student_id);
    cJSON_AddStringToObject(req_root, "firmware_version", "v1.0.0-lab");
    cJSON_AddNumberToObject(req_root, "rssi_dbm", current_rssi);

    /* Serialize cJSON tree into a compact string */
    char *post_payload = cJSON_PrintUnformatted(req_root);
    cJSON_Delete(req_root); /* Tree no longer needed once serialized to string */

    if (!post_payload) {
        free(resp_buf);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Prepared Enrollment POST JSON Body:\n  %s", post_payload);

    /* 3. Configure the HTTP Client for a POST request */
    esp_http_client_config_t config = {
        .url = enrollment_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_client_event_handler,
        .user_data = resp_buf,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    /* Set HTTP Request Headers and POST Body */
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_post_field(client, post_payload, strlen(post_payload));

    /* 4. Execute the HTTP POST transaction */
    ESP_LOGI(TAG, "Sending HTTP POST to FastAPI server: %s", enrollment_url);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);

        if (status_code == 200 || status_code == 201) {
            ESP_LOGI(TAG, "FastAPI Server Response:\n  %s", resp_buf->buffer);

            /* 5. Parse the enrollment credentials returned by the FastAPI server */
            cJSON *resp_json = cJSON_Parse(resp_buf->buffer);
            if (resp_json) {
                cJSON *status = cJSON_GetObjectItemCaseSensitive(resp_json, "status");
                cJSON *token  = cJSON_GetObjectItemCaseSensitive(resp_json, "device_token");
                cJSON *topic  = cJSON_GetObjectItemCaseSensitive(resp_json, "assigned_mqtt_topic");
                cJSON *intv   = cJSON_GetObjectItemCaseSensitive(resp_json, "telemetry_interval_sec");

                ESP_LOGI(TAG, "=========================================");
                ESP_LOGI(TAG, "IoT Device Enrollment Successful!");
                if (cJSON_IsString(status)) ESP_LOGI(TAG, "  Status         : %s", status->valuestring);
                if (cJSON_IsString(token))  ESP_LOGI(TAG, "  Device Token   : %s", token->valuestring);
                if (cJSON_IsString(topic))  ESP_LOGI(TAG, "  Assigned Topic : %s", topic->valuestring);
                if (cJSON_IsNumber(intv))   ESP_LOGI(TAG, "  Report Interval: %d sec", intv->valueint);
                ESP_LOGI(TAG, "=========================================");

                cJSON_Delete(resp_json);
            }
        } else {
            ESP_LOGW(TAG, "Enrollment rejected (HTTP %d): %s", status_code, resp_buf->buffer);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s (Is the FastAPI server running?)",
                 esp_err_to_name(err));
    }

    /* 6. Clean up resources */
    esp_http_client_cleanup(client);
    free(post_payload);
    free(resp_buf);
    return err;
}
