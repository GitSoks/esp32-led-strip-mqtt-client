/**
 * @file mqtt_handler.c
 * @brief This file contains the implementation of the MQTT handler module.
 *
 * The MQTT handler module is responsible for handling MQTT communication and controlling an LED strip.
 * It includes functions for initializing the MQTT client, subscribing to topics, and processing received messages.
 * The module also defines the MQTT topics used for communication and stores the state of the LED strip.
 */

#include <stdio.h>  // Standard input/output functions
#include <stdint.h> // Standard integer types
#include <stddef.h> // Standard definitions
#include <string.h> // String manipulation functions

#include "esp_system.h" // ESP32 system functions
#include "nvs_flash.h"  // Non-volatile storage (NVS) flash functions
#include "esp_event.h"  // ESP32 event loop library
#include "esp_netif.h"  // ESP32 network interface functions

#include "freertos/FreeRTOS.h"     // FreeRTOS real-time operating system
#include "freertos/task.h"         // FreeRTOS task functions
#include "freertos/semphr.h"       // FreeRTOS semaphore functions
#include "freertos/queue.h"        // FreeRTOS queue functions
#include "freertos/event_groups.h" // FreeRTOS event group functions

#include "lwip/sockets.h" // Lightweight IP (lwIP) socket functions
#include "lwip/dns.h"     // lwIP DNS functions
#include "lwip/netdb.h"   // lwIP network database functions
#include "lwip/err.h"     // lwIP error codes
#include "lwip/sys.h"     // lwIP system functions

#include "esp_log.h" // ESP32 logging library
#include "esp_err.h" // ESP32 error codes
#include "cJSON.h"   // cJSON library for JSON manipulation

#include "mqtt_client.h" // MQTT client library
#include "led_strip.h"   // LED strip library

// MQTT topics
#define MQTT_TOPIC_MAIN CONFIG_MQTT_TOPIC_MAIN
#define MQTT_DEVICE_ID MQTT_TOPIC_MAIN "/" CONFIG_MQTT_DEVICE_ID
#define MQTT_TOPIC_LAST_WILL MQTT_DEVICE_ID "/last-will"
#define MQTT_TOPIC_BRODCAST_COMMAND MQTT_TOPIC_MAIN "/" CONFIG_MQTT_TOPIC_COMMAND
#define MQTT_TOPIC_STATE MQTT_DEVICE_ID "/" CONFIG_MQTT_TOPIC_STATE
#define MQTT_TOPIC_COMMAND MQTT_DEVICE_ID "/" CONFIG_MQTT_TOPIC_COMMAND

static const char *TAG = "MQTT_HANDLER"; // Tag for logging

led_strip_handle_t led_strip; // Handle for the LED strip

// Structure to store the state of each LED
struct ledState
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ledStates[CONFIG_LED_COUNT];

// Function to log an error if the error code is non-zero
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * @brief Publishes the LED state to the MQTT broker.
 *
 * This function creates a JSON object representing the LED state and publishes it to the
 * MQTT state topic. The LED state is obtained from the global variable 'ledStates'.
 *
 * @param client The MQTT client handle.
 *
 *  *  * The MQTT state message will be following format:
 * {
 *     "device-id": "my-device",
 *     "lights": {
 *         "1": {
 *             "red": 255,
 *             "green": 255,
 *             "blue": 255
 *         },
 *         "2": {
 *             "red": 0,
 *             "green": 255,
 *             "blue": 0
 *         },
 *         ...
 *         "11": {
 *             "red": 0,
 *             "green": 0,
 *             "blue": 255
 *         }
 *         ...
 *     }
 * }
 *
 *
 */
void mqtt_publish_led_state(esp_mqtt_client_handle_t client)
{

    // Create a JSON object to represent the LED state
    cJSON *stateJson = cJSON_CreateObject();
    cJSON *deviceId = cJSON_CreateString(CONFIG_MQTT_DEVICE_ID);
    cJSON_AddItemToObject(stateJson, "device-id", deviceId);
    cJSON *lights = cJSON_CreateObject();
    cJSON_AddItemToObject(stateJson, "lights", lights);

    // Populate the JSON object with LED state data
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
                char n[5];                                 // Declare the variable 'n' with an appropriate size
                sprintf(n, "%d", i);                       // Initialize the variable 'n' with the value of 'i'
                cJSON_AddItemToObject(lights, n, ledJson); // Use 'n' instead of 'sprint(n)'
            }
        }

        // Convert the JSON object to a string
        char *stateJsonStr = cJSON_Print(stateJson);
        if (stateJsonStr != NULL)
        {
            // Publish the LED state to the MQTT state topic
            esp_mqtt_client_enqueue(client, MQTT_TOPIC_STATE, stateJsonStr, strlen(stateJsonStr), 2, 1, false);
            free(stateJsonStr);
        }

        cJSON_Delete(stateJson);
    }
}

/**
 * @brief Parses the JSON data received from MQTT and updates the LED strip accordingly.
 *
 * This function extracts the color data from the JSON and sets the corresponding LED values.
 * It also performs error checking and validation on the received data.
 *
 * @param client The MQTT client handle.
 * @param event The MQTT event handle.
 *
 *  * The MQTT command message should be in the following format:
 * {
 *     "device-id": "my-device",
 *     "lights": {
 *         "2": {
 *             "red": 255,
 *             "green": 255,
 *             "blue": 255
 *         },
 *         "1": {
 *             "red": 0,
 *             "green": 255,
 *             "blue": 0
 *         },
 *         ...
 *         "10": {
 *             "red": 0,
 *             "green": 0,
 *             "blue": 255
 *         }
 *         ...
 *     }
 * }
 *
 *
 */
void led_output_json_parser(esp_mqtt_client_handle_t client, esp_mqtt_event_handle_t event)
{

    // Extract the color data from the received JSON
    cJSON *root = cJSON_Parse(event->data);

    if (root != NULL)
    {
        cJSON *deviceId = cJSON_GetObjectItemCaseSensitive(root, "device-id");
        cJSON *lights = cJSON_GetObjectItemCaseSensitive(root, "lights");

        // Check if the JSON contains the required fields
        if (cJSON_IsString(deviceId) && cJSON_IsObject(lights))
        {
            char *deviceIdStr = deviceId->valuestring;

            // Check if the device ID matches the configured device ID or "all"
            if (strcmp(deviceIdStr, CONFIG_MQTT_DEVICE_ID) == 0 || strcmp(deviceIdStr, "all") == 0)
            {
                cJSON *led = NULL;
                cJSON_ArrayForEach(led, lights)
                {
                    cJSON *red = cJSON_GetObjectItemCaseSensitive(led, "red");
                    cJSON *green = cJSON_GetObjectItemCaseSensitive(led, "green");
                    cJSON *blue = cJSON_GetObjectItemCaseSensitive(led, "blue");

                    // Check if the LED data contains valid color values
                    if (cJSON_IsNumber(red) && cJSON_IsNumber(green) && cJSON_IsNumber(blue))
                    {
                        int ledValue = atoi(led->string);
                        int redValue = red->valueint;
                        int greenValue = green->valueint;
                        int blueValue = blue->valueint;

                        ESP_LOGD(TAG, "LED Number: %d, Color: Red=%d, Green=%d, Blue=%d\r\n", ledValue, redValue, greenValue, blueValue);

                        // Check if the LED number is within the valid range
                        if (ledValue >= 0 && ledValue < CONFIG_LED_COUNT)
                        {
                            // Set the LED color and update the LED state
                            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, ledValue, redValue, greenValue, blueValue));
                            ledStates[ledValue].red = redValue;
                            ledStates[ledValue].green = greenValue;
                            ledStates[ledValue].blue = blueValue;
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Invalid LED number");
                        }
                    }
                    else
                    {
                        ESP_LOGD(TAG, "Invalid LED data");
                    }
                }

                // Refresh the LED strip to apply the changes
                ESP_ERROR_CHECK(led_strip_refresh(led_strip));
            }
            else
            {
                ESP_LOGD(TAG, "Device ID does not match");
            }
        }
        else
        {
            ESP_LOGD(TAG, "Invalid JSON data");
        }

        cJSON_Delete(root);
    }
    else
    {
        ESP_LOGD(TAG, "Failed to parse JSON data");
    }
}
/**
 * @brief Event handler registered to receive MQTT events
 *
 * This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler (always MQTT Base in this example).
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

        // Enqueue the "online" message to the last will topic
        esp_mqtt_client_enqueue(client, MQTT_TOPIC_LAST_WILL, "online", 0, 2, 1, false);

        // Subscribe to topics
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_BRODCAST_COMMAND, 2);
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMMAND, 2);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "MQTT_EVENT_DATA");

        // Print the received topic and data
        ESP_LOGD(TAG, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
        ESP_LOGD(TAG, "DATA=%.*s\r\n", event->data_len, event->data);

        // Check if the topic contains MQTT_TOPIC_COMMAND
        if (strstr(event->topic, MQTT_TOPIC_COMMAND) != NULL || strstr(event->topic, MQTT_TOPIC_BRODCAST_COMMAND) != NULL)
        {
            // Handle the LED command
            led_output_json_parser(client, event);

            // Output the ledState to the MQTT state topic
            mqtt_publish_led_state(client);
        }

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR");

        // Check if the error type is TCP transport
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            // Log the error details
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGW(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/*
 * @brief Starts the MQTT application and initializes the LED strip.
 *
 * This function initializes the MQTT client with the provided configuration and starts the client.
 * It also initializes the ledStates array with default values and publishes the LED state to the MQTT state topic.
 *
 * @param strip The handle to the LED strip.
 */

void mqtt_app_start(led_strip_handle_t strip)
{

    // Set the LED strip handle
    led_strip = strip;

    // Configure the MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        .session.last_will = {
            .topic = MQTT_TOPIC_LAST_WILL,
            .qos = 2,
            .retain = true,
            .msg_len = 7,
            .msg = "offline"},
        .session.keepalive = CONFIG_MQTT_KEEPALIVE,
    };

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    // Read the MQTT broker URL from stdin if configured to do so
    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter the URL of the MQTT broker\n");
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
        ESP_LOGI(TAG, "Broker URL: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker URL");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    // Initialize and start the MQTT client
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // Initialize the ledStates array with default values
    for (int i = 0; i < CONFIG_LED_COUNT; i++)
    {
        ledStates[i].red = 0;
        ledStates[i].green = 0;
        ledStates[i].blue = 0;
    }

    // Publish the LED count and type to MQTT topics
    char ledCount[6];
    snprintf(ledCount, sizeof(ledCount), "%d", CONFIG_LED_COUNT);
    esp_mqtt_client_enqueue(client, MQTT_DEVICE_ID "/lights/count", ledCount, 0, 2, 1, false);
    esp_mqtt_client_enqueue(client, MQTT_DEVICE_ID "/lights/type", "WS2812", 0, 2, 1, false);

    // Publish the LED state to the MQTT state topic
    mqtt_publish_led_state(client);
}
