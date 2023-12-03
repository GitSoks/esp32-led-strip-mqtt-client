import unittest
from unittest.mock import patch, MagicMock

# Import the functions you want to test
from wifi_handler import wifi_init_sta
from mqtt_handler import mqtt_app_start


class TestApp(unittest.TestCase):
    @patch("wifi_handler.esp_netif_init")
    @patch("wifi_handler.esp_event_loop_create_default")
    @patch("wifi_handler.wifi_init_sta")
    def test_wifi_init_sta(
        self, mock_wifi_init_sta, mock_event_loop_create_default, mock_netif_init
    ):
        # Mock any dependencies or external calls
        mock_wifi_init_sta.return_value = None
        mock_event_loop_create_default.return_value = None
        mock_netif_init.return_value = None

        # Call the function you want to test
        wifi_init_sta()

        # Assert the expected behavior or outcome
        mock_wifi_init_sta.assert_called_once()
        mock_event_loop_create_default.assert_called_once()
        mock_netif_init.assert_called_once()

    @patch("mqtt_handler.mqtt_app_start")
    def test_mqtt_app_start(self, mock_mqtt_app_start):
        # Mock any dependencies or external calls
        mock_mqtt_app_start.return_value = None

        # Call the function you want to test
        mqtt_app_start(led_strip)

        # Assert the expected behavior or outcome
        mock_mqtt_app_start.assert_called_once_with(led_strip)


if __name__ == "__main__":
    unittest.main()
