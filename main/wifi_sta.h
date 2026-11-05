/**
 * @file wifi_sta.h
 * @brief Lab Module 1 & 2: Wi-Fi Station (STA) Mode & Diagnostics Interface
 *
 * This header exposes the helper functions used in the hands-on lab to:
 *  1. Scan nearby 802.11 Access Points (APs) to inspect channels, RSSI, and auth modes.
 *  2. Connect the ESP32 to a target Wi-Fi network using FreeRTOS Event Groups.
 *  3. Query link-layer diagnostics (RSSI and Hardware MAC address).
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform a synchronous Wi-Fi scan and print a formatted table of nearby APs.
 *
 * NOTE: The Wi-Fi driver must be initialized and started in Station mode before
 *       calling this function, OR you can call wifi_init_sta() which handles full initialization.
 *       This function is standalone and initializes Wi-Fi if not already started.
 */
void wifi_scan_networks(void);

/**
 * @brief Initialize NVS, TCP/IP netif, Event Loop, and connect to Wi-Fi in Station Mode.
 *
 * Blocks using a FreeRTOS EventGroup until:
 *  - An IPv4 address is assigned by the DHCP server (returns ESP_OK), OR
 *  - The maximum number of connection retries is exhausted (returns ESP_FAIL).
 *
 * @param ssid      Null-terminated Wi-Fi network name (SSID)
 * @param password  Null-terminated Wi-Fi password (empty string "" for open networks)
 * @return esp_err_t ESP_OK on successful IP acquisition, ESP_FAIL otherwise.
 */
esp_err_t wifi_init_sta(const char *ssid, const char *password);

/**
 * @brief Read the current Received Signal Strength Indicator (RSSI) of the connected AP.
 *
 * @return int RSSI in dBm (e.g., -55 dBm is strong, -85 dBm is weak), or 0 on error.
 */
int wifi_get_rssi(void);

/**
 * @brief Retrieve the ESP32 Station MAC address formatted as "AA:BB:CC:DD:EE:FF".
 *
 * @param mac_str_out Buffer to store the formatted MAC string (must be at least 18 bytes).
 * @param max_len     Size of mac_str_out buffer.
 * @return esp_err_t  ESP_OK on success.
 */
esp_err_t wifi_get_mac_str(char *mac_str_out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STA_H */
