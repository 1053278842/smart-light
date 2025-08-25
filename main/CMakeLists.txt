idf_component_register(SRCS "wifi_connect.c"
                       PRIV_REQUIRES esp_wifi nvs_flash esp_http_server
                       INCLUDE_DIRS "")
