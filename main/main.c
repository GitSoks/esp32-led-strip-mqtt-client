/**
 * @file main.c
 * @brief Main application file
 *
 * This file contains the main function and configuration for the ESP32 LED strip MQTT client application.
 * It includes necessary header files, initializes components, and starts the MQTT application.
 */

#include <stdio.h>  // Standard input/output functions
#include <stdint.h> // Standard integer types
#include <stddef.h> // Standard definitions
#include <string.h> // String manipulation functions

#include "esp_system.h" // ESP32 system functions
#include "nvs_flash.h"  // Non-volatile storage (NVS) flash functions
#include "esp_event.h"  // Event handling functions
#include "esp_netif.h"  // Network interface functions

#include "freertos/FreeRTOS.h"     // FreeRTOS real-time operating system
#include "freertos/task.h"         // Task management functions
#include "freertos/semphr.h"       // Semaphore functions
#include "freertos/queue.h"        // Queue functions
#include "freertos/event_groups.h" // Event group functions

#include "esp_log.h" // ESP32 logging functions
#include "esp_err.h" // ESP32 error codes

#include "led_strip.h"    // LED strip functions
#include "mqtt_handler.h" // MQTT handler functions
#include "wifi_handler.h" // Wi-Fi handler functions
#include "led_handler.h"  // LED handler functions

static const char *TAG = "MAIN";

/**
 * @brief Main function
 *
 * This is the entry point of the ESP32 LED strip MQTT client application.
 * It configures the LED strip, initializes components, and starts the MQTT application.
 */
void app_main(void)
{
    // Configure LED strip
    led_strip_handle_t led_strip = configure_led();

    // Clear and refresh LED strip
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    // Set log levels for different components
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT", ESP_LOG_INFO);
    esp_log_level_set("outbox", ESP_LOG_INFO);

    esp_log_level_set("MAIN", ESP_LOG_INFO);
    esp_log_level_set("MQTT_HANDLER", ESP_LOG_INFO);
    esp_log_level_set("WIFI_HANDLER", ESP_LOG_INFO);
    esp_log_level_set("LED_HANDLER", ESP_LOG_INFO);

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // Erase NVS if no free pages or new version found
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi in station mode
    wifi_init_sta();

    // Start MQTT application
    mqtt_app_start(led_strip);
}
