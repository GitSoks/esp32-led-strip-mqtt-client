/**
 * @file led_handler.c
 * @brief This file contains the implementation of LED strip handling functions.
 */

#include <stdio.h>  // Standard input/output functions
#include <stdint.h> // Standard integer types
#include <stddef.h> // Standard definitions
#include <string.h> // String manipulation functions

#include "freertos/FreeRTOS.h"     // FreeRTOS library
#include "freertos/task.h"         // Task management functions
#include "freertos/semphr.h"       // Semaphore functions
#include "freertos/queue.h"        // Queue functions
#include "freertos/event_groups.h" // Event group functions

#include "esp_log.h" // ESP32 logging functions
#include "esp_err.h" // ESP32 error codes

#include "led_strip.h" // LED strip library

static const char *TAG = "LED_HANDLER";

#ifdef CONFIG_LED_MODEL_WS2812
#define CONFIG_LED_MODEL_TYPE LED_MODEL_WS2812
#elif CONFIG_LED_MODEL_SK6812
#define CONFIG_LED_MODEL_TYPE LED_MODEL_SK6812
#endif

/**
 * @brief Configures the LED strip.
 *
 * This function initializes the LED strip according to the provided configuration.
 *
 * @return The handle to the configured LED strip.
 */
led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_GPIO,        // The GPIO that connected to the LED strip's data line
        .max_leds = CONFIG_LED_COUNT,             // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = CONFIG_LED_MODEL_TYPE,       // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0,
#else
        .clk_src = RMT_CLK_SRC_DEFAULT,         // different clock source can lead to different power consumption
        .resolution_hz = CONFIG_LED_RMT_RES_HZ, // RMT counter clock frequency
        .flags.with_dma = false,                // DMA feature is available on ESP target like ESP32-S3
#endif
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}
