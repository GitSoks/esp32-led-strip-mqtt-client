#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"

#include "mqtt_client.h"
#include "led_strip.h"

#define MQTT_TOPIC_MAIN CONFIG_MQTT_TOPIC_MAIN
#define MQTT_TOPIC_LED MQTT_TOPIC_MAIN "/" CONFIG_MQTT_TOPIC_LED
#define MQTT_TOPIC_LED_STATE MQTT_TOPIC_LED "/" CONFIG_MQTT_TOPIC_LED_STATE
#define MQTT_TOPIC_LED_COMMAND MQTT_TOPIC_LED "/" CONFIG_MQTT_TOPIC_LED_COMMAND

// #define MQTT_TOPIC_MAIN "test"
// #define MQTT_TOPIC_LED MQTT_TOPIC_MAIN"/leds"
// #define MQTT_TOPIC_LED_STATE MQTT_TOPIC_LED"/state"
// #define MQTT_TOPIC_LED_COMMAND MQTT_TOPIC_LED"/command"

static const char *TAG = "MQTT_HANDLER";
led_strip_handle_t led_strip;

struct ledState
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ledStates[CONFIG_LED_COUNT];

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void mqtt_publish_led_state(esp_mqtt_client_handle_t client)
{
    // Output the ledState to the MQTT state topic
    cJSON *stateJson = cJSON_CreateObject();
    if (stateJson != NULL)
    {
        for (int i = 0; i < CONFIG_LED_COUNT; i++)
        {
            cJSON *ledJson = cJSON_CreateObject();
            if (ledJson != NULL)
            {
                cJSON_AddNumberToObject(ledJson, "red", ledStates[i].red);
                cJSON_AddNumberToObject(ledJson, "green", ledStates[i].green);
                cJSON_AddNumberToObject(ledJson, "blue", ledStates[i].blue);
                char n[5];                                    // Declare the variable 'n' with an appropriate size
                sprintf(n, "%d", i);                          // Initialize the variable 'n' with the value of 'i'
                cJSON_AddItemToObject(stateJson, n, ledJson); // Use 'n' instead of 'sprint(n)'
            }
        }

        char *stateJsonStr = cJSON_Print(stateJson);
        if (stateJsonStr != NULL)
        {
            // Publish the stateJsonStr to the MQTT state topic
            esp_mqtt_client_enqueue(client, MQTT_TOPIC_LED_STATE, stateJsonStr, strlen(stateJsonStr), 2, 1, false);
            free(stateJsonStr);
        }

        cJSON_Delete(stateJson);
    }
}

void led_output_json_parser(esp_mqtt_client_handle_t client, esp_mqtt_event_handle_t event)
{
    int redValue = 0;
    int greenValue = 0;
    int blueValue = 0;

    // Extract the color data
    cJSON *root = cJSON_Parse(event->data);
    if (root != NULL)
    {
        cJSON *red = cJSON_GetObjectItemCaseSensitive(root, "red");
        cJSON *green = cJSON_GetObjectItemCaseSensitive(root, "green");
        cJSON *blue = cJSON_GetObjectItemCaseSensitive(root, "blue");

        if (cJSON_IsNumber(red) && cJSON_IsNumber(green) && cJSON_IsNumber(blue))
        {
            redValue = red->valueint;
            greenValue = green->valueint;
            blueValue = blue->valueint;

            printf("Color: Red=%d, Green=%d, Blue=%d\r\n", redValue, greenValue, blueValue);
        }
        else
        {
            printf("Invalid color data\r\n");
            return;
        }

        cJSON_Delete(root);
    }
    else
    {
        printf("Failed to parse JSON data\r\n");
    }

    // Extract the number after the "/"
    char *number_str = strrchr(event->topic, '/') + 1;

    if (strstr(number_str, "all") != NULL)
    {
        // Handle "all" case
        printf("All LEDs selected\r\n");
        if (redValue == 0 && greenValue == 0 && blueValue == 0)
        {
            printf("Clearing LEDs\r\n");
            ESP_ERROR_CHECK(led_strip_clear(led_strip));
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        }
        else
        {
            printf("Setting all LEDs to color\r\n");
            for (int i = 0; i < CONFIG_LED_COUNT; i++)
            {
                ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, redValue, greenValue, blueValue));
            }
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        }
        for (int i = 0; i < CONFIG_LED_COUNT; i++)
        {
            ledStates[i].red = redValue;
            ledStates[i].green = greenValue;
            ledStates[i].blue = blueValue;
        }
    }

    else
    {
        // Handle individual LED case
        int led = atoi(number_str);
        if (led == 0 && number_str[0] != '0')
        {
            printf("Invalid LED number\r\n");
            return;
        }
        printf("LED Number: %d\r\n", led);
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, led, redValue, greenValue, blueValue));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        ledStates[led].red = redValue;
        ledStates[led].green = greenValue;
        ledStates[led].blue = blueValue;
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        char topic[50];

        for (int i = 0; i < CONFIG_LED_COUNT; i++)
        {
            snprintf(topic, sizeof(topic), "%s/%d", MQTT_TOPIC_LED_COMMAND, i);
            esp_mqtt_client_subscribe(client, topic, 2);
        }

        snprintf(topic, sizeof(topic), "%s/all", MQTT_TOPIC_LED_COMMAND);
        esp_mqtt_client_subscribe(client, topic, 2);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        // Check if the topic contains MQTT_TOPIC_LED_COMMAND

        if (strstr(event->topic, MQTT_TOPIC_LED_COMMAND) != NULL)
        {
            // Handle the LED command
            led_output_json_parser(client, event);

            // Output the ledState to the MQTT state topic
            mqtt_publish_led_state(client);
        }

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(led_strip_handle_t strip)
{

    led_strip = strip;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128)
        {
            int c = fgetc(stdin);
            if (c == '\n')
            {
                line[count] = '\0';
                break;
            }
            else if (c > 0 && c < 127)
            {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    char ledCount[6];
    snprintf(ledCount, sizeof(ledCount), "%d", CONFIG_LED_COUNT);
    esp_mqtt_client_enqueue(client, MQTT_TOPIC_LED "/count", ledCount, 0, 2, 1, false);
    esp_mqtt_client_enqueue(client, MQTT_TOPIC_LED "/type", "WS2812", 0, 2, 1, false);

    // Initialize the ledStates array
    for (int i = 0; i < CONFIG_LED_COUNT; i++)
    {
        ledStates[i].red = 0;
        ledStates[i].green = 0;
        ledStates[i].blue = 0;
    }
    // Output the ledState to the MQTT state topic
    mqtt_publish_led_state(client);
}
