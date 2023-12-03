#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"

#include "led_strip.h"

static const char *TAG = "LED_HANDLER";

#ifdef CONFIG_LED_MODEL_WS2812
#define CONFIG_LED_MODEL_TYPE LED_MODEL_WS2812
#elif CONFIG_LED_MODEL_SK6812
#define CONFIG_LED_MODEL_TYPE LED_MODEL_SK6812
#endif

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
